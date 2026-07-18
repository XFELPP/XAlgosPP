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

#ifdef _WIN32
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#else
#include <sys/types.h>
#endif

#include <cstdint>
#include <cstring>

namespace xalgospp::scheduling {
  /**
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
   * Enumerators to indicate the specificity of shared memory to use.
   *
   * When creating shared-memory partitions, the allocation can be created to
   * be shared at varying levels of specificity and inclusion. At the top, the
   * allocation is shared across the entire machine. However, it can be created
   * to only be used by a smaller subset, e.g., by ranks associated to a NUMA
   * domain.
   */
  enum class ShmemType : std::uint8_t {
    MACHINE = 0, ///< Whole node/machine shared memory
    SOCKET= 1,   ///< By hardware socket
    NUMA = 2,    ///< By NUMA domain
    L3CACHE = 3, ///< Sharing L3 cache
    L2CACHE = 4  ///< Sharing L2 cache
  };

  /**
   * Setup an Algorithm using a shared-memory MPI strategy.
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

    if (rank < 0 || size < 0) {
      // Something went wrong...
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

        hwloc_obj_t obj { hwloc_get_obj_covering_cpuset(topology, cpuset) };

        hwloc_obj_type_t topo_type;
        bool set_topo_type { false };

        if (shmem_type == ShmemType::SOCKET) {
#ifdef OMPI_COMM_TYPE_SOCKET
        // OpenMPI provides some specific variants, MPICH does not, e..g
        MPI_Comm_split_type(node_comm,
                            OMPI_COMM_TYPE_SOCKET,
                            0,
                            MPI_INFO_NULL,
                            &shmem_comm);
#else
        topo_type = HWLOC_OBJ_PACKAGE;
        set_topo_type = true;
#endif
        } else if (shmem_type == ShmemType::NUMA) {
#ifdef OMPI_COMM_TYPE_NUMA
          MPI_Comm_split_type(node_comm,
                              OMPI_COMM_TYPE_NUMA,
                              0,
                              MPI_INFO_NULL,
                              &shmem_comm);
#else
          topo_type = HWLOC_OBJ_NUMANODE;
          set_topo_type = true;
#endif
        } else if (shmem_type == ShmemType::L3CACHE) {
#ifdef OMPI_COMM_TYPE_L3CACHE
          MPI_Comm_split_type(node_comm,
                              OMPI_COMM_TYPE_L3CACHE,
                              0,
                              MPI_INFO_NULL,
                              &shmem_comm);
#else
          topo_type = HWLOC_OBJ_L3CACHE;
          set_topo_type = true;
#endif
        } else if (shmem_type == ShmemType::L2CACHE) {
#ifdef OMPI_COMM_TYPE_L2CACHE
          MPI_Comm_split_type(node_comm,
                              OMPI_COMM_TYPE_L2CACHE,
                              0,
                              MPI_INFO_NULL,
                              &shmem_comm);
#else
          topo_type = HWLOC_OBJ_L2CACHE;
          set_topo_type = true;
#endif
        }

        if (set_topo_type) {
          obj = hwloc_get_ancestor_obj_by_type(topology, topo_type, obj);

          int locality_id = obj ? obj->logical_index : -1;

          MPI_Comm_split(node_comm, locality_id, rank, &shmem_comm);
        }

        if (shmem_comm != MPI_COMM_NULL) {
          shmem_comms.push_back(shmem_comm);
        }

        hwloc_bitmap_free(cpuset);
        hwloc_topology_destroy(topology);
      } else {
        // ShmemType::MACHINE -- in this case shmem_comm == node_comm
        shmem_comm = node_comm;
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

      // Node rank 0 stages, then we redistribute info.
      std::size_t bytes_for_algo { 0 };
      if (shmem_rank == 0) {
        algo.stage();
        bytes_for_algo = algo.staged_data_size();
      }

      // Distribute the size information to all ranks
      auto mpi_type { sbio::mpi::type_for<decltype(bytes_for_algo)>() };
      MPI_Bcast(&bytes_for_algo, 1, mpi_type, 0, shmem_comm);

      // Create the reference counted MPI window
      void* baseptr { nullptr };
      window = RCWindow::make_rc(sbio::use_rc_alloc_del,
                                 sbio::MPIWinAllocator {},
                                 sbio::MPIWinDeleter { shmem_comm },
                                 shmem_comm,
                                 bytes_for_algo,
                                 &baseptr);

      // Will synchronize on the window
      if (*window != MPI_WIN_NULL) {
        MPI_Win_fence(0, *window);
      }

      if (shmem_rank == 0) {
        // The node rank 0 now populates the RC-wrapped window with the data
        std::memcpy(baseptr, algo.get_staged_data(), bytes_for_algo);
      }

      // Will synchronize on the window
      if (*window != MPI_WIN_NULL) {
        MPI_Win_fence(0, *window);
      }

      // Finally, reset the Algorithm for ALL ranks so that the backing data is
      // the window
      algo.set_staged_data(reinterpret_cast<std::uint8_t*>(baseptr), bytes_for_algo);
    }
  }
} // namespace xalgospp::scheduling

#endif // XALGOSPP_SCHEDULING_STAGING_HH
