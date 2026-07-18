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

#include "xalgospp/scheduling/pool.hh"
#include "xalgospp/scheduling/queue.hh"
#include "xalgospp/scheduling/task.hh"

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
  using ShapeKey = std::vector<ssize_t>;

  using PoolKey = std::tuple<numa_node_t, ShapeKey, ncarray::DType>;

  /**
   * The primary scheduler of processing work defined by a DAG.
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
   *                               [ Input Generator ]
   *                                       |
   *                                    (Submit)
   *                                       |
   *                               +-------v-------+
   *                               |   enqueue()   |
   *                               +-------+-------+
   *                                       |
   *                               +-------v-------+
   *                               |  Overloaded?  |
   *                               +-------+-------+
   *                                       |
   *                       +-----+         |       +----+
   *                 +-----| Yes |---------+-------| No |--------+
   *                 |     +-----+                 +----+        |
   *                 |                                           |
   *                 v                                           v
   *         +---------------+                           +---------------+
   *         | Suspend Queue |             +------------>|  Queues Above |
   *         +-------+-------+             |             +---------------+
   *                 |                     |                     |
   *                 |                     |             +-------v-------+
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
   *    of the input generation. Currently this uses a fixed rate, hard coded. It could
   *    be learned, as an improvement in future versions of the algorithm.
   * 6. From 5, calculate a target number of steps to have in flight accounting for
   *    system jitter.
   * 7. From 6, determine the remaining concurrency numbers.
   */
  class DagScheduler {
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
    };

    // NOTE: There seems to be a bug in both GCC and clang - Config{} will
    // not compile here. Seems related: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=88165
    explicit DagScheduler(Config cfg = Config { 0, 0, true, 2, 2, true, true, 0 });
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

    std::shared_ptr<ncarray::NCArray> acquire_buffer(numa_node_t node,
                                                     ssize_t ndim,
                                                     const ssize_t* shape,
                                                     ncarray::DType dtype);

  private:
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

    std::shared_ptr<spdlog::logger> m_logger;
  };

} // namespace xalgospp::scheduling

#endif // XALGOSPP_SCHEDULING_DAG_SCHEDULER_HH
