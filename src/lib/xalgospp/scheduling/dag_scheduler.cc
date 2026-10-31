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

#include "xalgospp/scheduling/dag_scheduler.hh"

#include "xalgospp/scheduling/queue.hh"
#include "xalgospp/scheduling/task.hh"

#include <spdlog/cfg/env.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>

// Provide fallback definitions if missing in headers
#ifndef MPOL_F_NODE
#define MPOL_F_NODE (1<<0)
#endif
#ifndef MPOL_F_ADDR
#define MPOL_F_ADDR (1<<1)
#endif
#ifndef __NR_get_mempolicy
// I think this should've been defined in unistd.h/syscall.h, but in case, define now
// Using the sys call directly can avoid the need to link libnuma directly
// The second defined checks are MSVC-specific - so not really Linux... doesn't hurt to
// leave them though.
#if defined(__x86_64__) || defined(_M_X64)
#define __NR_get_mempolicy 239
#elif defined(__aarch64__) || defined(_M_ARM64)
#define __NR_get_mempolicy 236
#elif defined(__i386__) || defined(_M_IX86)
// Don't think anyone is using 32-bit.... but I guess just in case...
#define __NR_get_mempolicy 275
#elif define(__arm__) || defined(_M_ARM)
// Don't think anyone is using 32-bit.... but I guess just in case...
#define __NR_get_mempolicy 320
#endif
#endif
#endif

namespace fs = std::filesystem;

namespace xalgospp::scheduling {
  namespace {
    thread_local std::size_t t_worker_id { 0 };
  } // anonymous namespace

  DagScheduler::DagScheduler(DagScheduler::Config cfg)
    : m_config(cfg)
  {
    int mpi_initialized { 0 };
    MPI_Initialized(&mpi_initialized);
    if (mpi_initialized) {
      // Don't interfere with user comm
      MPI_Comm_dup(MPI_COMM_WORLD, &m_world_comm);
      MPI_Comm_rank(m_world_comm, &m_world_rank);
      MPI_Comm_size(m_world_comm, &m_world_size);

      // Use a node-local comm for autotuning - determine once one main thread
      MPI_Comm node_comm { MPI_COMM_NULL };
      MPI_Comm_split_type(m_world_comm,
                          MPI_COMM_TYPE_SHARED,
                          0,
                          MPI_INFO_NULL,
                          &node_comm);
      if (node_comm != MPI_COMM_NULL) {
        MPI_Comm_size(node_comm, &m_local_size);
        MPI_Comm_free(&node_comm);
      }
    }

    m_max_concurrent_high_mem.store(m_config.max_concurrent_high_mem);
    m_max_concurrency_multiplier.store(m_config.max_concurrency_multiplier);

    spdlog::cfg::load_env_levels("XALGOSPP_DAG_LOG_LEVEL");
    std::string logger_name { "XAlgosPP::Scheduling::DagScheduler" };
    if (mpi_initialized && m_world_rank >= 0) {
      logger_name += "::Rank" + std::to_string(m_world_rank);
    }

    auto logger = spdlog::get(logger_name);
    if (!logger) {
      m_logger = spdlog::stdout_color_mt(logger_name);
    } else {
      m_logger = logger;
    }

    m_topology = detect_numa_topology();
    std::size_t num_numa_nodes { m_topology.size() };
    m_logger->info("Detected {} NUMA node{}.",
                   num_numa_nodes,
                   num_numa_nodes > 1 ? "s" : "");

    // Create a queue for each NUMA node mapped to the physical IDs
    // - If we're bound to a specific node, the returned topology will be potentially
    //   smaller than the home node ID. So to provide remote queues, and make
    //   the queues directly indexable, they are created up to the largest physical
    //   ID.
    std::size_t max_node_id { 0 };
    for (const auto& [node_id, cores] : m_topology) {
      if (static_cast<std::size_t>(node_id) > max_node_id) {
        max_node_id = static_cast<std::size_t>(node_id);
      }
    }
    std::size_t num_queues { m_topology.empty() ? 0 : (max_node_id + 1) };

    m_logger->info("Creating {} NUMA-node specific queues.", num_queues);
    for (std::size_t i = 0; i < num_queues; ++i) {
      m_logger->debug("Creating queue for NUMA node {}.", i);
      m_node_queues.push_back(std::make_unique<WorkQueue>());
    }

    for (const auto& [node_id, cores] : m_topology) {
      m_num_workers +=
        m_config.threads_per_node != 0 ? m_config.threads_per_node : cores.size();
    }
    m_workers.reserve(m_num_workers);

    m_logger->info("Launching workers for each queue.");
    // Launch worker threads
    std::size_t thread_id { 0 };
    for (const auto& [node_id, cores] : m_topology) {
      std::size_t threads_on_node { m_config.threads_per_node };
      if (threads_on_node == 0) {
        threads_on_node = cores.size();
      }

      m_logger->debug("Launching {} workers on node {}.", threads_on_node, node_id);
      for (std::size_t t = 0; t < threads_on_node; ++t) {
        m_logger->trace("Launching worker {} on node {}.", thread_id, node_id);
        m_workers.emplace_back(&DagScheduler::worker_loop, this, thread_id++, node_id);
      }
    }
  }

  DagScheduler::~DagScheduler() {
    // Always wait on all outstanding work on teardown
    m_running = false;
    {
      std::lock_guard<std::mutex> lock(m_cv_mutex);
      m_job_cv.notify_all();
    }

    {
      std::lock_guard<std::mutex> lock(m_hm_mutex);
      m_hm_cv.notify_all();
    }

    for (auto& worker : m_workers) {
      if (worker.joinable()) {
        worker.join();
      }
    }

    m_suspended_generators.clear();
    m_pools.clear();

    int mpi_initialized { 0 };
    MPI_Initialized(&mpi_initialized);

    if (mpi_initialized) {
      // After tearing down queues, cleanup any Algorithm data we prepared
      if (m_world_comm != MPI_COMM_NULL) {
        MPI_Barrier(m_world_comm);
      }

      m_algo_windows.clear();
      for (auto& comm : m_shmem_comms) {
        if (comm != MPI_COMM_NULL) {
          MPI_Comm_free(&comm);
          comm = MPI_COMM_NULL;
        }
      }
      m_shmem_comms.clear();

      if (m_world_comm != MPI_COMM_NULL) {
        MPI_Comm_free(&m_world_comm);
      }
    }
  }

  bool DagScheduler::pin_thread_to_cores(const std::vector<int>& core_ids) {
#ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    for (int core : core_ids) {
      CPU_SET(core, &cpuset);
    }

    pthread_t current_thread { pthread_self() };
    int rc { pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset) };

    return rc == 0;
#else
    return false;
#endif
  }

  std::map<numa_node_t, std::vector<int>> DagScheduler::detect_numa_topology() {
    std::map<numa_node_t, std::vector<int>> topology;

#ifdef __linux__
    std::string base_path { "/sys/devices/system/node" };

    cpu_set_t process_cpuset;
    CPU_ZERO(&process_cpuset);
    if (sched_getaffinity(0, sizeof(cpu_set_t), &process_cpuset) != 0) {
      CPU_ZERO(&process_cpuset);
      for (int i = 0; i < CPU_SETSIZE; ++i) {
        CPU_SET(i, &process_cpuset);
      }
    }

    if (fs::exists(base_path)) {
      for (const auto& entry : fs::directory_iterator(base_path)) {
        std::string filename { entry.path().filename().string() };

        if (filename.rfind("node", 0) == 0 && std::isdigit(filename[4])) {
          numa_node_t node_id = std::stoi(filename.substr(4));
          std::vector<int> cores;

          std::ifstream cpulist_file(entry.path() / "cpulist");
          if (cpulist_file) {
            std::string line;
            std::getline(cpulist_file, line);
            std::stringstream ss(line);
            std::string range;

            while (std::getline(ss, range, ',')) {
              std::size_t dash { range.find('-') };
              if (dash == std::string::npos) {
                int core_id { std::stoi(range) };
                if (CPU_ISSET(core_id, &process_cpuset)) {
                  cores.push_back(core_id);
                }
              } else {
                int start { std::stoi(range.substr(0, dash)) };
                int end { std::stoi(range.substr(dash + 1)) };
                for (int c = start; c <= end; ++c) {
                  if (CPU_ISSET(c, &process_cpuset)) {
                    cores.push_back(c);
                  }
                }
              }
            }
          }

          if (!cores.empty()) {
            topology[node_id] = std::move(cores);
          }
        }
      }
    }
#endif
    if (topology.empty()) {
      // Fallback: single NUMA node containing 4 hardware threads
      topology[0] = {0, 1, 2, 3};
    }

    return topology;
  }

  bool DagScheduler::should_throttle_generators() {
    std::size_t unfinished { m_unfinished_tasks.load(std::memory_order_acquire) };

    return unfinished >= m_num_workers * m_max_concurrency_multiplier.load(std::memory_order_relaxed);
  }

  std::size_t DagScheduler::get_system_ram_bytes() {
#ifdef __linux__
    long pages { sysconf(_SC_PHYS_PAGES) };
    long page_size { sysconf(_SC_PAGE_SIZE) };
    if (pages > 0 && page_size > 0) {
      return static_cast<std::size_t>(pages) * page_size;
    }
#endif
    return 16ULL * 1024 * 1024 * 1024; // Dumb fallback on 16 GB ram
  }

  void DagScheduler::perform_autotune() {
    if (!m_config.enable_autotuning) {
      return;
    }

    std::size_t system_ram { get_system_ram_bytes() };

    // Account for MPI world - specifically, ranks on this same node
    int local_size { m_local_size };
    system_ram /= local_size;

    std::size_t total_workers { m_num_workers };

    std::size_t raw_frame_size { m_config.raw_frame_size_bytes };

    if (raw_frame_size == 0) {
      std::lock_guard<std::mutex> lock(m_pool_mutex);
      if (!m_pools.empty()) {
        auto& pool = m_pools.begin()->second;

        raw_frame_size = pool->buffer_size_bytes();
      }
    }

    if (raw_frame_size == 0) {
      return;
    }

    double t_io { 0.002 };
    double t_downstream { 0.010 };

    std::size_t io_count { m_io_profile.count.load(std::memory_order_relaxed) };
    if (io_count > 0) {
      t_io =
        static_cast<double>(m_io_profile.total_execution_time_ns.load(std::memory_order_relaxed)) / (io_count * 1e9);
    }

    std::size_t compute_count { m_compute_profile.count.load(std::memory_order_relaxed) };
    if (compute_count > 0) {
      t_downstream =
        static_cast<double>(m_compute_profile.total_execution_time_ns.load(std::memory_order_relaxed)) / (compute_count * 1e9);
    }

    double avg_multiplier { 8.0 };
    std::size_t mem_per_step { static_cast<std::size_t>(raw_frame_size * avg_multiplier) };
    std::size_t max_steps_by_ram { (system_ram / 2) / mem_per_step };

    // Little's Law target: N_steps = Throughput * Latency
    double ideal_throughput { total_workers / t_downstream };
    std::size_t min_steps {
      static_cast<std::size_t>(std::ceil(ideal_throughput * (t_io + t_downstream)))
    };

    std::size_t target_steps { std::min(min_steps * 2, max_steps_by_ram) };
    target_steps = std::max(target_steps, min_steps);

    std::size_t tasks_per_step { 3 };
    std::size_t concurrency_multiplier =
      std::ceil(static_cast<double>(target_steps * tasks_per_step) / total_workers);
    concurrency_multiplier = std::max(std::size_t(4), concurrency_multiplier);
    m_max_concurrency_multiplier.store(concurrency_multiplier, std::memory_order_relaxed);

    // Bandwidth limit calculation uses bytes accessed as (1 * read + multiplier * written)
    double task_traffic_gb {
      static_cast<double>(raw_frame_size * (1.0 + avg_multiplier)) / 1e9
    };
    double task_bandwidth_gbps { task_traffic_gb / t_downstream };

    double node_bandwidth_limit {
      m_config.node_memory_bandwidth_limit_gbps / local_size
    };
    double max_concurrent_tasks_by_bandwidth {
      node_bandwidth_limit / task_bandwidth_gbps
    };

    std::size_t threads_per_node { m_config.threads_per_node };
    if (threads_per_node == 0 && !m_topology.empty()) {
      threads_per_node = m_topology.begin()->second.size();
    }
    threads_per_node = std::max(std::size_t(1), threads_per_node);

    std::size_t max_concurrent_high_mem =
      std::min(threads_per_node,
               std::max(std::size_t(1),
                        static_cast<std::size_t>(std::floor(max_concurrent_tasks_by_bandwidth))));
    m_max_concurrent_high_mem.store(max_concurrent_high_mem, std::memory_order_relaxed);

    m_logger->info("[Autotuner] Learned stats: T_io = {:.3f} ms, T_downstream = {:.3f} ms, Task "
                   "Bandwidth = {:.2f} GB/s",
                   t_io * 1000.0,
                   t_downstream * 1000.0,
                   task_bandwidth_gbps);
    m_logger->info("[Autotuner] Tuned max_concurrency_multiplier to {} (Target steps in flight: {})",
                   concurrency_multiplier,
                   target_steps);
    m_logger->info("[Autotuner] Tuned max_concurrent_high_mem to {} (Node limit: {} GB/s, "
                   "Concurrent by bandwidth: {:.2f})",
                   max_concurrent_high_mem,
                   node_bandwidth_limit,
                   max_concurrent_tasks_by_bandwidth);
  }

  void DagScheduler::record_task_metrics(std::shared_ptr<Task> task,
                                         std::uint64_t elapsed_ns) {
    if (task->is_generator()) {
      // IO/Generators aren't profiled for processing time
      return;
    }

    if (task->resources().memory_intensity <= 2) {
      m_io_profile.count.fetch_add(1, std::memory_order_relaxed);
      m_io_profile.total_execution_time_ns.fetch_add(elapsed_ns, std::memory_order_relaxed);
    } else {
      std::size_t count {
        m_compute_profile.count.fetch_add(1, std::memory_order_relaxed) + 1
      };

      m_compute_profile.total_execution_time_ns.fetch_add(elapsed_ns, std::memory_order_relaxed);
      // Autotune once, using worker that completes warmup
      if (m_config.enable_autotuning                                   &&
          count >= m_config.warmup_submissions                         &&
          !m_autotune_started.exchange(true, std::memory_order_acq_rel)) {
        perform_autotune();

        // Switch back to normal submission now
        m_profiling_phase.store(false, std::memory_order_release);
      }
    }
  }

  numa_node_t DagScheduler::resolve_locality(const LocalityHint& hint) {
    if (hint.preferred_node != ANY_NODE) {
      return hint.preferred_node;
    }

    // For dynamic lookup, check if the ptr to ptr was set first
    const void* ptr { hint.memory_affinity_ptr };
    if (hint.memory_affinity_ptr_ptr != nullptr) {
      ptr = *(hint.memory_affinity_ptr_ptr);
    }

    if (ptr != nullptr) {
#ifdef __linux__
      int mode { 0 };
      unsigned long mask { 0 };

      long status = syscall(__NR_get_mempolicy,
                            &mode,
                            &mask,
                            sizeof(mask) * 8,
                            const_cast<void*>(ptr),
                            MPOL_F_NODE | MPOL_F_ADDR);

      if (status == 0) {
        // Double check that the node actually has a queue
        if (mode >= 0 && static_cast<std::size_t>(mode) < m_node_queues.size()) {
          return mode;
        }
      }
#endif
    }

    return ANY_NODE;
  }

  void DagScheduler::submit_dag(std::vector<std::shared_ptr<Task>> tasks) {
    m_unfinished_tasks.fetch_add(tasks.size(), std::memory_order_release);

    // Check parents/roots before the enqueueing
    // During the loop, they could run. If the children are released, they
    // may end up getting queued a second (incorrect) time to run.
    std::vector<std::shared_ptr<Task>> roots;
    roots.reserve(tasks.size());
    for (auto& task : tasks) {
      if (task->m_pending_parents.load(std::memory_order_acquire) == 0) {
        roots.push_back(std::move(task));
      }
    }

    // Only enquee tasks that are not waiting on the resolution of outstanding work
    for (auto& task : roots) {
      enqueue(std::move(task));
    }
  }

  void DagScheduler::submit_dag_inline(std::vector<std::shared_ptr<Task>> tasks) {
    m_unfinished_tasks.fetch_add(tasks.size(), std::memory_order_release);

    // Check parents/roots before the enqueueing
    // During the loop, they could run. If the children are released, they
    // may end up getting queued a second (incorrect) time to run.
    std::vector<std::shared_ptr<Task>> roots;
    roots.reserve(tasks.size());
    for (auto& task : tasks) {
      if (task->m_pending_parents.load(std::memory_order_acquire) == 0) {
        roots.push_back(std::move(task));
      }
    }

    // Run direclty in thread
    for (auto& task : roots) {
      bool has_token { is_high_mem(*task) && acquire_hm_token_blocking() };
      run_task(std::move(task), has_token);
    }
  }

  bool DagScheduler::throttle_generator() {
    if (m_config.enable_dynamic_backpressure) {
      while (m_running.load(std::memory_order_relaxed) && should_throttle_generators()) {
        // Drain other work
        auto [task, has_token] = find_task(g_current_numa_node, false);
        if (task) {
          run_task(std::move(task), has_token);
        } else {
          std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
      }
    }

    return m_running.load(std::memory_order_relaxed);
  }

  void DagScheduler::enqueue(std::shared_ptr<Task> task) {
    numa_node_t target_node { resolve_locality(task->locality()) };

    if (m_config.enable_dynamic_backpressure && task->is_generator()) {
      std::lock_guard<std::mutex> lock(m_suspension_mutex);
      if (should_throttle_generators()) {
        m_suspended_generators.push_back(std::move(task));
        m_num_suspended_generators.fetch_add(1, std::memory_order_release);
        return;
      }
    }

    if (target_node == ANY_NODE) {
      m_global_queue.push(std::move(task));
    } else {
      m_node_queues[target_node]->push(std::move(task));
    }

    notify_worker();
  }

  void DagScheduler::notify_worker() {
    // Only grab mutex when a worker is sleeping
    // A worker is "sleeping" before its final check on queues
    // If a task is pushed between now and the final check, the latter can catch it
    if (m_num_sleeping.load(std::memory_order_seq_cst) > 0) {
      std::lock_guard<std::mutex> lock(m_cv_mutex);
      m_job_cv.notify_one();
    }
  }

  bool DagScheduler::is_high_mem(const Task& task) const {
    if (!m_profiling_phase.load(std::memory_order_relaxed) &&
        m_config.raw_frame_size_bytes > 0) {
      double task_mem_gb =
        static_cast<double>(m_config.raw_frame_size_bytes * (1 + task.resources().memory_intensity)) / 1e9;

      double avg_time_s { 0.010 }; // Hard-coded fallback

      std::size_t count { m_compute_profile.count.load(std::memory_order_relaxed) };
      if (count > 0) {
        avg_time_s =
          static_cast<double>(m_compute_profile.total_execution_time_ns.load(std::memory_order_relaxed)) /
          (count * 1e9);
      }
      double task_bandwidth { task_mem_gb / avg_time_s };

      return
        task_bandwidth > m_config.node_memory_bandwidth_limit_gbps * m_config.percent_bandwidth_is_high_mem;
    }

    // Fallback on arbitrary threshold of 8 for mem-bound throttling
    return task.resources().memory_intensity >= 8;
  }

  bool DagScheduler::try_acquire_hm_token() {
    std::size_t current { m_active_high_mem_tasks.load(std::memory_order_seq_cst) };
    while (current < m_max_concurrent_high_mem.load(std::memory_order_relaxed)) {
      if (m_active_high_mem_tasks.compare_exchange_weak(current,
                                                        current + 1,
                                                        std::memory_order_seq_cst)) {
        return true;
      }
    }

    return false;
  }

  bool DagScheduler::acquire_hm_token_blocking() {
    if (try_acquire_hm_token()) {
      return true;
    }

    // Pairs with release_hm_token - we count ourselves as waiting before the final
    // try, it releases before checking for waiters so one of us sees the other
    bool acquired { false };
    std::unique_lock<std::mutex> lock(m_hm_mutex);
    m_hm_waiters.fetch_add(1, std::memory_order_seq_cst);
    m_hm_cv.wait(lock, [&]() {
      acquired = try_acquire_hm_token();
      return acquired || !m_running.load(std::memory_order_relaxed);
    });
    m_hm_waiters.fetch_sub(1, std::memory_order_relaxed);

    return acquired;
  }

  void DagScheduler::release_hm_token() {
    m_active_high_mem_tasks.fetch_sub(1, std::memory_order_seq_cst);

    if (m_hm_waiters.load(std::memory_order_seq_cst) > 0) {
      std::lock_guard<std::mutex> lock(m_hm_mutex);
      m_hm_cv.notify_one();
    }

    if (!m_hm_deferred.empty()) {
      notify_worker();
    }
  }

  void DagScheduler::wait_all() {
    {
      std::unique_lock<std::mutex> lock(m_wait_mutex);

      auto wait_on_work = [this]() {
        std::size_t unfinished { m_unfinished_tasks.load(std::memory_order_acquire) };
        if (unfinished != 0 && unfinished < 0x7FFFFFFFFFFFFFFF) {
          return false;
        }
        if (m_num_suspended_generators.load(std::memory_order_acquire) != 0) {
          return false;
        }
        if (!m_global_queue.empty() || !m_hm_deferred.empty()) {
          return false;
        }
        for (const auto& q : m_node_queues) {
          if (!q->empty()) {
            return false;
          }
        }
        return true;
      };

      m_wait_cv.wait(lock, wait_on_work);
    }

    if (m_world_comm != MPI_COMM_NULL) {
      MPI_Barrier(m_world_comm);
    }
  }

  void DagScheduler::on_task_complete(std::shared_ptr<Task> completed_task,
                                      std::vector<std::shared_ptr<Task>>& run_here,
                                      std::shared_ptr<Task>& continuation) {
    const bool affine { completed_task->thread_affine_output() };

    for (auto& child : completed_task->m_children) {
      // Decrement the child's counter. If it reaches 0, all dependencies are done.
      if (child->m_pending_parents.fetch_sub(1, std::memory_order_acq_rel) != 1) {
        continue;
      }

      if (affine) {
        // The input is on this thread and must stay here
        run_here.push_back(std::move(child));
      } else if (!continuation && !child->is_generator()) {
        // Keep the child in the same thread, its in the cache
        continuation = std::move(child);
      } else {
        enqueue(std::move(child));
      }
    }

    // Need to clear this to avoid some cycles which caused memory leaks
    // The child is queued up, so the parent can drop the reference now
    completed_task->m_children.clear();
    completed_task.reset();

    // NOTE: Must perform the decrement on the Task count before making throttling
    //       decision, since this Task is currently completing. Otherwise, things
    //       exit early/will hang if Barriers have been introduced in some places.
    std::size_t current = m_unfinished_tasks.load(std::memory_order_acquire);
    while (current > 0) {
      if (m_unfinished_tasks.compare_exchange_weak(current,
                                                   current - 1,
                                                   std::memory_order_acq_rel)) {
        break;
      }
    }

    std::vector<std::shared_ptr<Task>> to_resume;
    {
      std::lock_guard<std::mutex> lock(m_suspension_mutex);
      if (!m_suspended_generators.empty()) {
        std::size_t unfinished { m_unfinished_tasks.load(std::memory_order_acquire) };
        std::size_t suspended { m_num_suspended_generators.load(std::memory_order_acquire) };

        if (!should_throttle_generators() || unfinished <= suspended) {
          to_resume = std::move(m_suspended_generators);
          m_suspended_generators.clear();
        }
      }
    }

    for (auto& gen_task : to_resume) {
      enqueue(std::move(gen_task));
      m_num_suspended_generators.fetch_sub(1, std::memory_order_release);
    }

    // Careful with underflows since these are not signed
    if (m_unfinished_tasks.load(std::memory_order_acquire) == 0         &&
        m_num_suspended_generators.load(std::memory_order_acquire) == 0 &&
        m_global_queue.empty()) {
      std::lock_guard<std::mutex> lock(m_wait_mutex);
      m_wait_cv.notify_all();
    }
  }

  std::pair<std::shared_ptr<Task>, bool> DagScheduler::find_task(numa_node_t home_node,
                                                                 bool allow_generators) {
    // Memory-bound work deferred for a token goes first, once a token is free
    if (!m_hm_deferred.empty() && try_acquire_hm_token()) {
      if (auto task { m_hm_deferred.pop() }) {
        return { std::move(task), true };
      }
      release_hm_token();
    }

    const std::size_t num_numa_nodes { m_node_queues.size() };
    const numa_node_t home { home_node == ANY_NODE ? 0 : home_node };

    // General search is as follows:
    // - Check the local queue first
    // - If no work locally, check the global queue
    // - If still no work, fall back to stealing work from remote nodes
    // - Finally, if a task has been found, check concurrency/throttling limits
    while (true) {
      std::shared_ptr<Task> task { m_node_queues[home]->pop() };

      if (!task) {
        task = m_global_queue.pop();
      }

      for (std::size_t i = 1; !task && i < num_numa_nodes; ++i) {
        numa_node_t remote_node { static_cast<int>((home + i) % num_numa_nodes) };
        task = m_node_queues[remote_node]->steal();
      }

      if (!task) {
        return { nullptr, false };
      }

      if (!allow_generators && task->is_generator()) {
        m_global_queue.push(std::move(task));
        notify_worker();
        return { nullptr, false };
      }

      if (!is_high_mem(*task)) {
        return { std::move(task), false };
      }

      if (try_acquire_hm_token()) {
        return { std::move(task), true };
      }

      // Otherwise, throtlled - wait until getting more work
      m_hm_deferred.push(std::move(task));
    }
  }

  void DagScheduler::execute_task(const std::shared_ptr<Task>& task) {
    // Execute task (include timing)
    auto start_time { std::chrono::high_resolution_clock::now() };
    try {
      // m_logger->trace("Worker {} executing task.", home_node);
      task->execute();
    } catch (const std::exception& e) {
      m_logger->error("[Worker Thread {}] Task failed with exception: {}",
                      t_worker_id,
                      e.what());
    } catch (...) {
      m_logger->error("[Worker Thread {}] Task failed with unknown exception!",
                      t_worker_id);
    }
    auto end_time { std::chrono::high_resolution_clock::now() };
    auto elapsed_ns {
      std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count()
    };

    if (m_profiling_phase.load(std::memory_order_relaxed)) {
      record_task_metrics(task, elapsed_ns);
    }
  }

  void DagScheduler::run_task(std::shared_ptr<Task> task, bool has_token) {
    std::vector<std::shared_ptr<Task>> run_here;

    while (task) {
      execute_task(task);

      std::shared_ptr<Task> continuation;
      on_task_complete(std::move(task), run_here, continuation);

      bool must_run_here { false };

      if (!run_here.empty()) {
        if (continuation) {
          enqueue(std::move(continuation));
        }
        task = std::move(run_here.back());
        run_here.pop_back();
        must_run_here = true;
      } else {
        task = std::move(continuation);
      }

      const bool needs_token { task && is_high_mem(*task) };
      if (has_token && !needs_token) {
        release_hm_token();
        has_token = false;
      } else if (!has_token && needs_token) {
        if (must_run_here) {
          has_token = acquire_hm_token_blocking();
        } else {
          has_token = try_acquire_hm_token();
          if (!has_token) {
            enqueue(std::move(task)); // Throttled
          }
        }
      }
    }
  }

  void DagScheduler::worker_loop(std::size_t thread_id, numa_node_t home_node) {
    g_current_numa_node = home_node; // Set the thread's current NUMA node globally
    t_worker_id = thread_id;

    if (m_config.enable_pinning) {
      auto it { m_topology.find(home_node) };

      if (it != m_topology.end()) {
        m_logger->debug("[Thread {}][Posix Thread ID: {}][NUMA Node {}] Pinning this to core.",
                        thread_id,
                        std::hash<std::thread::id>{}(std::this_thread::get_id()),
                        home_node);
        pin_thread_to_cores(it->second);
      }
    }

    while (m_running) {
      auto [task, has_token] = find_task(home_node, true);

      if (task) {
        run_task(std::move(task), has_token);
        continue;
      }

      std::unique_lock<std::mutex> lock(m_cv_mutex);

      auto wait_for_work = [this]() {
        if (!m_running) {
          return true;
        }
        if (!m_global_queue.empty()) {
          return true;
        }
        for (auto& q : m_node_queues) {
          if (!q->empty()) {
            return true;
          }
        }
        if (!m_hm_deferred.empty() &&
            m_active_high_mem_tasks.load() < m_max_concurrent_high_mem.load()) {
          return true;
        }
        return false;
      };

      m_num_sleeping.fetch_add(1, std::memory_order_seq_cst);
      m_job_cv.wait_for(lock, std::chrono::milliseconds(100), wait_for_work);
      m_num_sleeping.fetch_sub(1, std::memory_order_relaxed);
    }

    m_logger->debug("[Thread {}][Posix Thread ID: {}][NUMA Node {}] Exiting worker loop. Unfinished tasks = {}, "
                    "Suspended Generators = {}",
                    thread_id,
                    std::hash<std::thread::id>{}(std::this_thread::get_id()),
                    home_node,
                    m_unfinished_tasks.load(),
                    m_num_suspended_generators.load());
  }

  std::shared_ptr<ncarray::SOArray> DagScheduler::acquire_buffer(numa_node_t node,
                                                                 ssize_t ndim,
                                                                 const ssize_t* shape,
                                                                 ncarray::DType dtype) {
    ShapeKey skey(shape, shape + ndim);
    PoolKey pool_key { node, skey, dtype };
    std::shared_ptr<ArrayBufferPool> pool;

    {
      std::lock_guard<std::mutex> lock(m_pool_mutex);
      auto it { m_pools.find(pool_key) };
      if (it == m_pools.end()) {
        pool = std::make_shared<ArrayBufferPool>(ndim, shape, dtype);
        m_pools[pool_key] = pool;
      } else {
        pool = it->second;
      }
    }

    return pool->acquire();
  }

  void DagScheduler::check_memory_bandwidth(std::size_t test_bytes, std::size_t niter) {
    std::size_t num_threads { 0 };
    if (m_workers.empty()) {
      num_threads = std::thread::hardware_concurrency();
    } else {
      num_threads = m_workers.size();
    }
    num_threads = std::max(std::size_t(1), num_threads);

    std::size_t bytes_per_thread { test_bytes / num_threads };
    std::size_t nelem_per_thread { bytes_per_thread / sizeof(double) };

    std::vector<double> copy_gbps_vec(num_threads, 0.0);
    std::vector<double> multiply_gbps_vec(num_threads, 0.0);
    std::vector<double> add_gbps_vec(num_threads, 0.0);
    std::vector<double> multiply_add_gbps_vec(num_threads, 0.0);

    // Run a basic STREAM test (can find info on this online easily)
    auto benchmark_worker = [&](std::size_t tid) {
      std::size_t nelem { nelem_per_thread };

      std::vector<double> vec_a(nelem);
      std::vector<double> vec_b(nelem);
      std::vector<double> vec_c(nelem);

      std::random_device rand_dev;
      std::mt19937 rng(rand_dev());
      std::uniform_real_distribution<> rand_dist(-1024.0, 1024.0);

      for (std::size_t i = 0; i < nelem; ++i) {
        vec_a[i] = rand_dist(rng);
        vec_b[i] = rand_dist(rng);
        vec_c[i] = rand_dist(rng);
      }

      double scalar_1 { rand_dist(rng) };
      double scalar_2 { rand_dist(rng) };

      double copy_ns { 0.0 };
      double multiply_ns { 0.0 };
      double add_ns { 0.0 };
      double multiply_add_ns { 0.0 };

      for (std::size_t n = 0; n < niter; ++n) {
        // Run copy test - total traffic = 2 x bytes
        auto copy_start { std::chrono::high_resolution_clock::now() };
        for (std::size_t i = 0; i < nelem; ++i) {
          vec_c[i] = vec_a[i];
        }
        auto copy_end { std::chrono::high_resolution_clock::now() };

        copy_ns +=
          std::chrono::duration_cast<std::chrono::nanoseconds>(copy_end - copy_start).count();

        // Run scalar multiplier - total traffic = 2 x bytes
        auto multiply_start { std::chrono::high_resolution_clock::now() };
        for (std::size_t i = 0; i < nelem; ++i) {
          vec_b[i] = scalar_1 * vec_c[i];
        }
        auto multiply_end { std::chrono::high_resolution_clock::now() };

        multiply_ns +=
          std::chrono::duration_cast<std::chrono::nanoseconds>(multiply_end - multiply_start).count();

        // Run array addition - total traffic = 3 x bytes
        auto add_start { std::chrono::high_resolution_clock::now() };
        for (std::size_t i = 0; i < nelem; ++i) {
          vec_c[i] = vec_a[i] + vec_b[i];
        }
        auto add_end { std::chrono::high_resolution_clock::now() };

        add_ns +=
            std::chrono::duration_cast<std::chrono::nanoseconds>(add_end - add_start).count();

        // Run multiply add - total traffic = 3 x bytes (often called `triad`)
        auto multiply_add_start { std::chrono::high_resolution_clock::now() };
        for (std::size_t i = 0; i < nelem; ++i) {
          vec_a[i] = scalar_2 * vec_c[i] + vec_b[i];
        }
        auto multiply_add_end { std::chrono::high_resolution_clock::now() };

        multiply_add_ns +=
            std::chrono::duration_cast<std::chrono::nanoseconds>(multiply_add_end - multiply_add_start).count();
      }

      double thread_bytes { static_cast<double>(nelem * sizeof(double)) };
      copy_gbps_vec[tid] = (2.0 * thread_bytes / 1e9) / ((copy_ns / niter) / 1e9);
      multiply_gbps_vec[tid] = (2.0 * thread_bytes / 1e9) / ((multiply_ns / niter) / 1e9);
      add_gbps_vec[tid] = (2.0 * thread_bytes / 1e9) / ((add_ns / niter) / 1e9);
      multiply_add_gbps_vec[tid] = (2.0 * thread_bytes / 1e9) / ((multiply_add_ns / niter) / 1e9);
    };

    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    for (std::size_t t = 0; t < num_threads; ++t) {
      threads.emplace_back(benchmark_worker, t);
    }

    for (auto& t : threads) {
      if (t.joinable()) {
        t.join();
      }
    }

    double total_copy_gbps { 0.0 };
    double total_multiply_gbps { 0.0 };
    double total_add_gbps { 0.0 };
    double total_multiply_add_gbps { 0.0 };
    for (std::size_t t = 0; t < num_threads; ++t) {
      total_copy_gbps += copy_gbps_vec[t];
      total_multiply_gbps += multiply_gbps_vec[t];
      total_add_gbps += add_gbps_vec[t];
      total_multiply_add_gbps += multiply_add_gbps_vec[t];
    }

    m_logger->info("[Autotuner] Memory bandwidth results from STREAM test, "
                   "with {} threads, show: "
                   "Copy = {:.2f} GB/s, "
                   "Scalar Multiply = {:.2f} GB/s, "
                   "Array Add = {:.2f} GB/s, "
                   "Array Multiply Add ('triad') = {:.2f} GB/s",
                   num_threads,
                   total_copy_gbps,
                   total_multiply_gbps,
                   total_add_gbps,
                   total_multiply_add_gbps);

    double avg_node_gbps =
      (total_copy_gbps + total_multiply_gbps + total_add_gbps + total_multiply_add_gbps) / 4;

    m_config.node_memory_bandwidth_limit_gbps = avg_node_gbps;

    m_logger->info("[Autotuner] Set bandwidth limit to average ({:.2f} GB/s)",
                   avg_node_gbps);
  }
} // namespace xalgospp::scheduling
