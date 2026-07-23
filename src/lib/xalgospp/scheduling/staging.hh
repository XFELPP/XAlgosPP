/*
 * XAlgosPP - Algorithms and Utilities for XFEL Area Detector Analysis.
 *
 * Copyright (C) 2025-2026 Gabriel Dorlhiac
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Affero General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Affero General Public License along with
 * this program. If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef XALGOSPP_SCHEDULING_STAGING_HH
#define XALGOSPP_SCHEDULING_STAGING_HH

#include "xalgospp/scheduling/pool.hh"
#include "xalgospp/scheduling/queue.hh"
#include "xalgospp/scheduling/task.hh"

// sbio has a number of MPI utilities and storage classes that can be reused here
#include <sbio/storage/mpi_shared.hh>
#include <sbio/util/mpi.hh>
#include <sbio/util/rc.hh> // Thread and device-safe shared-pointer-like wrapper

#include <hwloc.h>
#include <mpi.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#ifdef _WIN32
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#else
#include <sys/types.h>
#endif

#include <cstdint>
#include <cstring>
#include <string>

namespace xalgospp::scheduling {
  /**
   * @file
   * The staging routines provide a variety of strategies for preparation of any
   * Algorithm ("staged") data. The set creates a toolkit of drop-in coordination
   * mechanisms to distribute "staged" data across parallel instances of the
   * Algorithm. E.g., if running under MPI, strategies include selecting to share
   * staged data by world, by node, by NUMA domain, by custom group or just in a
   * standard rank-local fashion.
   */

  using sbio::RC;
  using sbio::MPIWinAllocator;
  using sbio::MPIWinDeleter;

  using RCWindow = sbio::RC<MPI_Win, sbio::MPIWinAllocator, sbio::MPIWinDeleter>;

  /**
   * @brief Enumerators to indicate the specificity of shared memory to use.
   *
   * When creating shared-memory partitions, the allocation can be created to
   * be shared at varying levels of specificity and inclusion. At the top, the
   * allocation is shared across the entire machine. However, it can be created
   * to only be used by a smaller subset, e.g., by ranks associated to a NUMA
   * domain.
   */
  enum class ShmemType : std::uint8_t {
    // WORLD = 0,   ///< The whole MPI world (may span multiple machines)
    MACHINE = 0, ///< Whole node/machine shared memory
    SOCKET= 1,   ///< By hardware socket
    NUMA = 2,    ///< By NUMA domain
    L3CACHE = 3, ///< Sharing L3 cache
    L2CACHE = 4  ///< Sharing L2 cache
  };

  /**
   * @brief Setup an Algorithm using a shared-memory MPI strategy.
   *
   * Algorithms which require some staged data (e.g. constant matrices) can be
   * setup to use various MPI shared communication strategies. Doing so, however,
   * requires preparing the memory backing this data in a particular fashion.
   * This function simplifies this process, setting up any needed communicators
   * as well as ensuring proper synchronization so the memory will be valid for
   * use by an rank running the Algorithm.
   *
   * @tparam Algo The type of the Algorithm.
   * @param[in] algo The Algorithm to run the data staging for.
   * @param[out] window The window which will back the shared, staged data.
   * @param[out] shmem_comms Any created communicators will be returned via this vector.
   * @param[in] main_comm The starting communicator from which splitting will be done.
   * @param[in] shmem_type The granularity of the shared memory backing strategy.
   *            The enumerator specifies from machine/node down to cache level.
   */
  template <class Algo>
  void prepare_shmem_mpi_algo(Algo& algo,
                              RCWindow& window,
                              std::vector<MPI_Comm>& shmem_comms,
                              MPI_Comm main_comm = MPI_COMM_WORLD,
                              ShmemType shmem_type = ShmemType::MACHINE) {
    int initialized { 0 };
    int finalized { 0 };

    MPI_Initialized(&initialized);
    MPI_Finalized(&finalized);

    if (!initialized || finalized) {
      // Not initialized or the world was finalized, so each process will stage
      algo.stage();
      return;
    }

    int rank { -1 };
    int size { -1 };

    MPI_Comm_rank(main_comm, &rank);
    MPI_Comm_size(main_comm, &size);

    std::string logger_name { "XAlgosPP::Scheduling::Staging" };
    if (rank >= 0) {
      logger_name += "::Rank" + std::to_string(rank);
    }

    auto logger = spdlog::get(logger_name);
    if (!logger) {
      logger = spdlog::stdout_color_mt(logger_name);
    }

    if (rank < 0 || size < 0) {
      // Something went wrong...
      logger->warn("MPI rank and size retrieval failed! Will stage on each process!");
      algo.stage();
      return;
    } else {
      // Build the requested communicator

      // First create a shared memory communicator across the node.
      // We may need to further subdivide after depending on the request.
      MPI_Comm node_comm { MPI_COMM_NULL };
      MPI_Comm_split_type(main_comm,
                          MPI_COMM_TYPE_SHARED,
                          0,
                          MPI_INFO_NULL,
                          &node_comm);

      if (node_comm == MPI_COMM_NULL) {
        // Something went wrong....
        logger->warn("Node Communicator creation failed! Will stage on each process!");
        algo.stage();
        return;
      }

      shmem_comms.push_back(node_comm);

      MPI_Comm shmem_comm { MPI_COMM_NULL };
      if (shmem_type != ShmemType::MACHINE) {
        hwloc_topology_t topology;
        hwloc_topology_init(&topology);
        hwloc_topology_load(topology);

        hwloc_cpuset_t cpuset { hwloc_bitmap_alloc() };
        hwloc_get_cpubind(topology, cpuset, HWLOC_CPUBIND_PROCESS);

        hwloc_obj_t obj { nullptr };
        int first_cpu { hwloc_bitmap_first(cpuset) };
        if (first_cpu != -1) {
          hwloc_obj_t pu_obj { hwloc_get_pu_obj_by_os_index(topology, first_cpu) };
          if (pu_obj) {
            obj = pu_obj;
          }
        }

        hwloc_obj_type_t topo_type;
        bool set_topo_type { false };

        if (shmem_type == ShmemType::SOCKET) {
          topo_type = HWLOC_OBJ_PACKAGE;
        } else if (shmem_type == ShmemType::NUMA) {
          topo_type = HWLOC_OBJ_NUMANODE;
        } else if (shmem_type == ShmemType::L3CACHE) {
          topo_type = HWLOC_OBJ_L3CACHE;
        } else if (shmem_type == ShmemType::L2CACHE) {
          topo_type = HWLOC_OBJ_L2CACHE;
        }

        int locality_id { -1 };

        if (obj) {
          obj = hwloc_get_ancestor_obj_by_type(topology, topo_type, obj);

          if (obj) {
            locality_id = obj->logical_index;
          }
        }

        if (locality_id == -1) {
          // Have to fallback to 0, to avoid an MPI_COMM_NULL
          locality_id = 0;

          std::string shmem_type_str;
          switch (shmem_type) {
          case ShmemType::MACHINE: {
            // This should always be supported... if not, bigger problems.
            // Just include the case for the switch
            shmem_type_str = "ShmemType::MACHINE";
            break;
          }
          case ShmemType::SOCKET: {
            shmem_type_str = "ShmemType::SOCKET";
            break;
          }
          case ShmemType::NUMA: {
            shmem_type_str = "ShmemType::NUMA";
            break;
          }
          case ShmemType::L3CACHE: {
            shmem_type_str = "ShmemType::L3CACHE";
            break;
          }
          case ShmemType::L2CACHE: {
            shmem_type_str = "ShmemType::L2CACHE";
            break;
          }
          }

          logger->warn("Requested SHMEM split type ({}) was not supported. "
                       "Will fallback to using ShmemType::MACHINE.",
                       shmem_type_str);
        }

        MPI_Comm_split(node_comm, locality_id, rank, &shmem_comm);

        hwloc_bitmap_free(cpuset);
        hwloc_topology_destroy(topology);
      } else {
        // ShmemType::MACHINE -- in this case shmem_comm == node_comm
        shmem_comm = node_comm;
      }

      if (shmem_comm == MPI_COMM_NULL) {
        shmem_comm = node_comm;
      } else if (shmem_comm != node_comm) {
        shmem_comms.push_back(shmem_comm);
      }

      // Now, get the node local size and rank
      int shmem_rank { -1 };
      int shmem_size { -1 };

      MPI_Comm_rank(shmem_comm, &shmem_rank);
      MPI_Comm_size(shmem_comm, &shmem_size);

      // In case of any error, have all processes stage so at least we can conintue
      if (shmem_rank < 0 || shmem_size < 0) {
        algo.stage();
        return;
      }

      // Setup a communicator for just the leaders (rank 0) down to the requested
      // shmem granularity level -- will only stage once, and distribute using this
      // comm.
      MPI_Comm leaders_comm { MPI_COMM_NULL };
      bool is_leader { shmem_rank == 0 };
      MPI_Comm_split(main_comm, is_leader ? 0 : MPI_UNDEFINED, rank, &leaders_comm);

      std::size_t bytes_for_algo { 0 };
      if (leaders_comm != MPI_COMM_NULL) {
        int leaders_rank { -1 };
        MPI_Comm_rank(leaders_comm, &leaders_rank);

        if (leaders_rank == 0) {
          logger->info("Leader-of-leaders running Algorithm staging.");
          algo.stage();
          bytes_for_algo = algo.staged_data_size();
        }

        // Leader of leaders broadcasts info to the other shmem rank 0s
        auto mpi_type { sbio::mpi::type_for<decltype(bytes_for_algo)>() };
        MPI_Bcast(&bytes_for_algo, 1, mpi_type, 0, leaders_comm);
      }

      // Shmem rank 0s now broadcast to the other ranks
      auto mpi_type { sbio::mpi::type_for<decltype(bytes_for_algo)>() };
      MPI_Bcast(&bytes_for_algo, 1, mpi_type, 0, shmem_comm);

      void* baseptr { nullptr };
      window = RCWindow::make_rc(sbio::use_rc_alloc_del,
                                 sbio::MPIWinAllocator {},
                                 sbio::MPIWinDeleter { shmem_comm },
                                 shmem_comm,
                                 bytes_for_algo,
                                 &baseptr);

      // Synchronize the window
      if (*window != MPI_WIN_NULL) {
        logger->trace("Synchronizing Window before copy.");
        MPI_Win_fence(0, *window);
      }

      // Leader of leaders sets window memory, then broadcasts to other leaders
      if (leaders_comm != MPI_COMM_NULL) {
        int leaders_rank { -1 };
        MPI_Comm_rank(leaders_comm, &leaders_rank);
        if (leaders_rank == 0) {
          logger->debug("Leader-of-leaders copying data to Window.");
          std::memcpy(baseptr, algo.get_staged_data(), bytes_for_algo);
        }

        logger->info("Distributing staged data among leader ranks.");
        // This can be slow sadly...
        MPI_Bcast(baseptr, static_cast<int>(bytes_for_algo), MPI_BYTE, 0, leaders_comm);
        MPI_Comm_free(&leaders_comm);
      }

      if (*window != MPI_WIN_NULL) {
        logger->trace("Synchronizing Window after copy.");
        MPI_Win_fence(0, *window);
      }

      // Finally, reset the Algorithm for ALL ranks so that the backing data is
      // the window
      algo.set_staged_data(reinterpret_cast<std::uint8_t*>(baseptr), bytes_for_algo);
    }
  }
} // namespace xalgospp::scheduling

#endif // XALGOSPP_SCHEDULING_STAGING_HH
