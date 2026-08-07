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

#ifndef XALGOSPP_SCHEDULING_DAG_SCHEDULER_HH
#define XALGOSPP_SCHEDULING_DAG_SCHEDULER_HH

#include "xalgospp/export_macro.hh"
#include "xalgospp/scheduling/pool.hh"
#include "xalgospp/scheduling/queue.hh"
#include "xalgospp/scheduling/staging.hh"
#include "xalgospp/scheduling/task.hh"

#include <mpi.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#ifdef _WIN32
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#else
#include <sys/types.h>
#endif

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <map>
#include <memory>
#include <thread>
#include <vector>

namespace xalgospp::scheduling {
  /**
   * Alias for the definition of an array's shape - used mostly for buffer pools.
   */
  using ShapeKey = std::vector<ssize_t>;

  /**
   * Alias for look-up of buffer pools by node locality, shape and datatype.
   */
  using PoolKey = std::tuple<numa_node_t, ShapeKey, ncarray::DType>;

  /**
   * @brief The primary scheduler of processing work defined by a DAG.
   *
   * The scheduler has a hierarchy of preferences for selecting the next piece of
   * work to be run. It starts by searching its local queues (in the sense of NUMA
   * locality) for work, before moving on to the items which do not have preferential
   * locations, and finally to queues on remote NUMA nodes. This set of preferences
   * is currently implemented solely for Linux, with a simplified fallback set of
   * queues for other operating systems.
   *
   * Schematically, the process can be viewed like:
   *
   *                                   [ Pipeline Tasks ]
   *                                           |
   *                                   +-------v-------+
   *                                   | Locality Hint |
   *                                   +-------+-------+
   *                                           |
   *                     +---------------------+---------------------+
   *                     | (Affinity: Node 0)  | (Affinity: Node 1)  | (No Affinity)
   *                     v                     v                     v
   *             +---------------+     +---------------+     +---------------+
   *             |  Node 0 Queue |     |  Node 1 Queue |     |  Global Queue |
   *             +-------+-------+     +-------+-------+     +-------+-------+
   *                     |                     |                     |
   *        +------------+------------+        |                     |
   *        |                         |        |                     |
   *   +----v-----+              +----v-----+  |                     |
   *   | Worker 0 |              | Worker 1 |  |                     |
   *   | (Node 0) |              | (Node 0) |  |                     |
   *   +----------+              +----------+  |                     |
   *        | (Steal Option 1)        |        |                     |
   *        +-------------------------v--------+                     |
   *        | (Steal Option 2)                                       |
   *        +--------------------------------------------------------+
   *
   * @note To try to remain generic, the scheduler relies on locality hints that may not
   *       be fully determined at construction. This allows for Tasks to generate data
   *       using mechanisms that are fully under their control - i.e., the Task and
   *       scheduling infrastructure does not enforce the use of specific buffers or
   *       memory spaces, so the generator can control this themselves. From a scheduling
   *       perspective this makes things more complex as the locality must be resolved
   *       lazily after preceeding parent Tasks have executed.
   *
   * On top of the base hierarchy, the scheduler uses two context-aware throttling
   * metrics to attempt to optimize the overall throughput of the system.
   *
   * 1. A concurrency limit is used for memory-bound steps to avoid negative impacts
   *    from oversubscription of available bandwidth.
   * 2. There furthermore is a dynamic backpressure mechanism to avoid excessive
   *    in flight Tasks from steps which are NOT memory-bound themselves.
   *
   * Beginning from a simple pipeline consisting of:
   *
   *           [ Input Data Generator ] -----> [ Memory Bound Processing Step]
   *
   * The overall behaviour can be modelled as:
   *
   *                          [ Input Generator ]         [ High Mem Processing ]
   *                                  |                             |
   *                               (Submit)                      (Submit)
   *                                  |                             |
   *                               +-----------------------------------+
   *                               |             enqueue()             |
   *                               +-----------------------------------+
   *                                  |                             |
   *                               +--v------------+                |
   *                               |  Overloaded?  |                |
   *                               +-------+-------+                |
   *                                       |                        |
   *                                       |                        |
   *                       +-----+         |       +----+           |
   *                 +-----| Yes |<--------+------>| No |>-----+    |
   *                 |     +-----+                 +----+      |    |
   *                 |                                         |    |
   *                 v                                         |    |
   *         +---------------+                           +-----v----v----+
   *         | Suspend Queue |             +------------>|  Queues Above |<-----------+
   *         +-------+-------+             |             +-----v----v----+            |
   *                 |                     |                   |    |                 |
   *                 |                     |                   |    |                 |
   *                 |                     |                   | +--v--+    +-----+   |
   *                 |                     |                   | | Hm? |--->| Yes |---+
   *                 |                     |                   | +-----+    +-----+
   *                 |                     |                   |    |
   *                 |                     |                   |    |
   *                 |                     |                   | +--v--+
   *                 |                     |                   | | No. |
   *                 |         (Pull from suspendeded)         | +--v--+
   *                 |          (Upon any completion)          |    |
   *                 |                     |                   |    |
   *                 |                     |             +-----v----v----+
   *                 +-------------------->+<------------|  On Complete  |
   *                                                     +---------------+
   *
   * Complete control of the queueing and throttling systems is exposed via the Config
   * which can be configured at construction. There is additionally, in the Config, an
   * option to perform "autotuning". The DagScheduler will then attempt to optimize the
   * parameters to maximize the throughput of the above systems. There is currently
   * only a single simple algorithm available using basic hints from the system, the
   * wrapped Tasks, and Little's law.
   *
   * The algorithm works as follows:
   *
   * 1. Determine the system resources.
   * 2. Start from the input data size.
   * 3. Using the input data size, and the memory multipliers from wrapped Task steps,
   *    determine an average memory footprint.
   * 4. Using the resources and footprint, find the maximum number of steps that could
   *    be run concurrently.
   * 5. Based on Little's law determine the minimum number of steps to hide the latency
   *    of the input generation. A profiling step is used after a few submissions to
   *    get estimates of step latencies.
   * 6. Calculate bandwidths from the total traffic and latencies.
   * 7. From 5 and 6 calculate a limit based on memory bandiwdth.
   * 8. From 7, determine the remaining concurrency numbers.
   */
  class XALG_API DagScheduler {
  public:
    struct Config {
      /**
       * The total number of NUMA nodes available. 0 means to auto-detect.
       */
      std::size_t num_numa_nodes { 0 };
      /**
       * Threads to allocate per NUMA node. 0 means to map to physical cores.
       */
      std::size_t threads_per_node { 0 };
      /**
       * Whether to allow pinning of tasks to specific cores.
       */
      bool enable_pinning { true };
      /**
       * The threshold at which throttling will begin for high-memory-bandwidth tasks.
       */
      std::size_t max_concurrent_high_mem { 2 };
      /**
       * A generalized concurrency limit for suspension of generator Tasks (if enabled).
       */
      std::size_t max_concurrency_multiplier { 8 };
      /**
       * Whether back pressure monitoring for generator Tasks should be enabled.
       */
      bool enable_dynamic_backpressure { true };
      /**
       * Allow the Scheduler to automatically determine resource and queue parameters.
       */
      bool enable_autotuning { true };
      /**
       * Raw frame size used for auto-tuning. Auto-determined if set to 0.
       */
      std::size_t raw_frame_size_bytes { 0 };
      /**
       * When autotuning, the number of DAG submissions used for warmup before timing.
       */
      std::size_t warmup_submissions { 5 };
      /**
       * The peak memory bandwidth, per NUMA node.
       */
      double node_memory_bandwidth_limit_gbps { 50.0 };
      /**
       * Percentage of total bandwidth usage to classify as "high memory" (memory-bound).
       */
      double percent_bandwidth_is_high_mem { 0.25 };
    };

    // NOTE: There seems to be a bug in both GCC and clang - Config{} will
    // not compile here. Seems related: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=88165
    explicit DagScheduler(Config cfg = Config {
        /* num_num_nodes                    = */ 0,
        /* threads_per_node                 = */ 0,
        /* enable_pinning                   = */ true,
        /* max_concurrent_high_mem          = */ 2,
        /* max_concurrency_multiplier       = */ 2,
        /* enable_dynamic_backpressure      = */ true,
        /* enable_autotuning                = */ true,
        /* raw_frame_size_bytes             = */ 0,
        /* warmup_submissions               = */ 5,
        /* node_memory_bandwidth_limit_gbps = */ 50.0,
        /* percent_bandwidth_is_high_mem    = */ 0.25
      });
    ~DagScheduler();

    /**
     * Submit a complete DAG for execution.
     *
     * @param[in] tasks The complete DAG - the root task contains the edges for its
     *            dependent tasks.
     */
    void submit_dag(std::vector<std::shared_ptr<Task>> tasks);

    /**
     * Enqueue a single task directly for execution.
     *
     * @param[in] task The task to be enqueued for execution.
     */
    void enqueue(std::shared_ptr<Task> task);

    /**
     * Wait on all submitted work to complete.
     */
    void wait_all();

    /**
     * Retrieve an array buffer of the requested size from the buffer pool.
     *
     * @param[in] node The NUMA node for the buffer pool.
     * @param[in] ndim The dimensions for the requested array.
     * @param[in] shape The shape for the requested array.
     * @param[in] dtype The datatype for the requested array.
     * @returns A shared pointer to the array in the pool.
     */
    std::shared_ptr<ncarray::SOArray> acquire_buffer(numa_node_t node,
                                                     ssize_t ndim,
                                                     const ssize_t* shape,
                                                     ncarray::DType dtype);

    /**
     * Run a test timing routine for estimation of memory bandwidth.
     *
     * This routine will estimate the total bandwidth using test cases of a copy,
     * scalar multiplier (buffer * scalar), addition of two buffers, and a multiply
     * add routine. The input test buffer size should be of the order ~4-5x the total
     * cache memory size. After that point, additionally increases in the size wont
     * affect the measurement much, but will increase the total memory footprint.
     *
     * @note Currently, this is a very simple function, and it is not thread-safe.
     *       It should be called from the main thread only. In the future it will
     *       be improved to account for multi-thread behaviour. In the meantime,
     *       the best measurements will be collected when run from an isolated
     *       NUMA domain (I.e., the process is the only one on the domain).
     *
     * @param[in] test_bytes The size in bytes of the array buffer to use. Should be
     *            on the order of 4-5x the cache size (L3 cache).
     * @param[in] niter The number of iterations to use. The timings will be taken from
     *            the average across iterations.
     */
    void check_memory_bandwidth(std::size_t test_bytes = 32ULL * 1024 * 1024,
                                std::size_t niter = 10);

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
     * @param[in] shmem_type The granularity of the shared memory backing strategy.
     *            The enumerator specifies from machine/node down to cache level.
     */
    template <class Algo>
    void stage_algorithm(Algo& algo, ShmemType shmem_type = ShmemType::MACHINE) {
      RCWindow win;

      MPI_Comm comm = m_world_comm != MPI_COMM_NULL ? m_world_comm : MPI_COMM_WORLD;
      prepare_shmem_mpi_algo(algo, win, m_shmem_comms, comm, shmem_type);
      if (*win != MPI_WIN_NULL) {
        m_algo_windows.push_back(std::move(win));
      }
    }

  private:
    /**
     * When autotuning, TaskProfileData is used for profiling job steps.
     */
    struct TaskProfileData {
      std::atomic<std::size_t> count { 0 };
      std::atomic<std::size_t> total_execution_time_ns { 0 };
    };

    /**
     * Enter the main work queue.
     *
     * The work loop is as follows:
     * 1. First check the local queue for work. The local queue will be configured to
     *    provide work that should preferrentially be performed within the NUMA node.
     *    If work is found, go to 4.
     * 2. If no work is found check the global queue. This queue is configured to
     *    hold work that is not preferrentially localized to a NUMA node. If work is
     *    found, go to 4.
     * 3. If both the local and global queues do not currently have work to do, then
     *    see if there is work to be stolen from remote NUMA node queues. If found go
     *    to 4.
     * 4. Once a Task been found, first check thresholds for throttling. Currently this
     *    scheduler only has a throttling threshold on memory pressure. If too high
     *    and the concurrency limit has been reached, then sleep or go back to check
     *    the queues for other work.
     * 5. Finally, execute the Task.
     *
     * @param[in] thread_id This worker's thread id.
     * @param[in] home_node The indicator for which is the local queue.
     */
    void worker_loop(std::size_t thread_id, numa_node_t home_node);

    /**
     * Try to determine NUMA support using __NR_get_mempolicy syscall.
     *
     * This is linux-only and will only try to resolve the locality if the provided
     * hint includes a memory affinity set with physical page info, and the hint does
     * NOT include a preferred node already. If it does, that is returned. This routine
     * will also return ANY_NODE in the case that a node was found, but there does not
     * currently exist a work queue for it.
     *
     * As fallback it will return ANY_NODE as the locality.
     *
     * @param[in] hint The locality hint.
     * @returns The NUMA node, or ANY_NODE.
     */
    numa_node_t resolve_locality(const LocalityHint& hint);

    bool pin_thread_to_cores(const std::vector<int>& core_ids);

    /**
     * Try to auto-determine the NUMA topology (Linux only).
     *
     * This routine will try to use the exposed data from the filesystem as provided by
     * the kernel.
     *
     * The fallback currently just returns a dumb 4-thread "node".
     *
     * @returns The determined NUMA topology.
     */
    std::map<numa_node_t, std::vector<int>> detect_numa_topology();

    /**
     * When dynamic back-pressure monitoring is enabled, decide whether to throttle.
     *
     * @returns When using the back-pressure system, returns true if throttling should
     *          begin for back pressure. Otherwise, false.
     */
    bool should_throttle_generators();

    /**
     * Determine system memory resources.
     *
     * @returns If on Linux, and it could be determined, the available ram.
     *          Otherwise, it just returns 16 GB for now.
     */
    std::size_t get_system_ram_bytes();

    /**
     * Run the automatic parameter tuning.
     */
    void perform_autotune();

    void record_task_metrics(std::shared_ptr<Task> task, std::uint64_t elapsed_ns);

    Config m_config;
    std::map<numa_node_t, std::vector<int>> m_topology;    ///< Mapping of node topology
    std::vector<std::thread> m_workers;                    ///< Complete set of workers

    std::vector<std::unique_ptr<WorkQueue>> m_node_queues; ///< Node-specific queues
    WorkQueue m_global_queue;                              ///< Global work queue

    std::atomic<bool> m_running { true };              ///< Whether still running
    std::atomic<std::size_t> m_unfinished_tasks { 0 }; ///< Number of pending tasks

    // ---- Synchronization primitives for waiting on complete DAG ---- //
    std::mutex m_wait_mutex;           ///< Mutex for waitixng on all DAG work.
    std::condition_variable m_wait_cv; ///< CV for waiting on all DAG work.

    // ---- Synchronization primitives for alerting individual workers ---- //
    std::mutex m_cv_mutex;             ///< Mutex for alerting a worker
    std::condition_variable m_job_cv;  ///< CV for alerting a worker

    // ---- Synchronization primitives for sleeping for memory throttling ---- //
    std::mutex m_hm_mutex;            ///< Mutex for high-memory throttling
    std::condition_variable m_hm_cv;  ///< CV for alerting when the HM status releases (token)

    // Simple resource budget/token management
    std::atomic<std::size_t> m_active_high_mem_tasks { 0 };

    void on_task_complete(std::shared_ptr<Task> completed_task);

    // ---- Synchronization primitives and memory for shared buffer pools ---- //
    std::mutex m_pool_mutex;
    std::map<PoolKey, std::shared_ptr<ArrayBufferPool>> m_pools;

    // ---- Synchronization primitives and state for back pressure monitoring ---- //
    std::mutex m_suspension_mutex;
    std::atomic<std::size_t> m_num_suspended_generators { 0 };
    std::vector<std::shared_ptr<Task>> m_suspended_generators;

    // ---- Utilities for Management when using MPI ---- //
    MPI_Comm m_world_comm { MPI_COMM_NULL };
    int m_world_rank { -1 };
    int m_world_size { -1 };

    std::vector<MPI_Comm> m_shmem_comms;  ///< Any communicators used for Algorithms
    std::vector<RCWindow> m_algo_windows; ///< Backing windows for staged Algorithm data

    // ---- Utilities for Autotuning and Task Profiling ---- //
    std::atomic<std::size_t> m_submissions_count { 0 }; ///< Number of DAGs submitted
    std::atomic<bool> m_profiling_phase { true };       ///< Whether currently profiling

    TaskProfileData m_io_profile;      ///< Profile for IO-type Tasks
    TaskProfileData m_compute_profile; ///< Profiel for compute/processing Tasks

    std::shared_ptr<spdlog::logger> m_logger;
  };

} // namespace xalgospp::scheduling

#endif // XALGOSPP_SCHEDULING_DAG_SCHEDULER_HH
