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

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
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
  DagScheduler::DagScheduler(DagScheduler::Config cfg)
    : m_config(cfg)
  {
    auto logger = spdlog::get("XAlgosPP::Scheduling::DagScheduler");
    if (!logger) {
      m_logger = spdlog::stdout_color_mt("XAlgosPP::Scheduling::DagScheduler");
    }

    m_topology = detect_numa_topology();
    std::size_t num_numa_nodes { m_topology.size() };
    m_logger->info("Detected {} NUMA node{}.",
                   num_numa_nodes,
                   num_numa_nodes > 1 ? "s" : "");

    // Create a queue for each NUMA node
    for (std::size_t i = 0; i < num_numa_nodes; ++i) {
      m_logger->info("Creating queue for NUMA node {}.", i);
      m_node_queues.push_back(std::make_unique<WorkQueue>());
    }

    // Launch worker threads
    std::size_t thread_id { 0 };
    for (const auto& [node_id, cores] : m_topology) {
      m_logger->info("Launching worker {} on node {}.", thread_id, node_id);
      std::size_t threads_on_node { m_config.threads_per_node };
      if (threads_on_node == 0) {
        threads_on_node = cores.size();
      }

      for (std::size_t t = 0; t < threads_on_node; ++t) {
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

    for (auto& worker : m_workers) {
      if (worker.joinable()) {
        worker.join();
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
                cores.push_back(std::stoi(range));
              } else {
                int start { std::stoi(range.substr(0, dash)) };
                int end { std::stoi(range.substr(dash + 1)) };
                for (int c = start; c <= end; ++c) {
                  cores.push_back(c);
                }
              }
            }
          }

          topology[node_id] = std::move(cores);
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

    // Only enquee tasks that are not waiting on the resolution of outstanding work
    for (auto& task : tasks) {
      if (task->m_pending_parents.load(std::memory_order_acquire) == 0) {
        enqueue(task);
      }
    }
  }

  void DagScheduler::enqueue(std::shared_ptr<Task> task) {
    numa_node_t target_node { resolve_locality(task->locality()) };

    if (target_node == ANY_NODE) {
      // Global work queue contains work that is not specified for a specific place
      m_global_queue.push(std::move(task));
    } else {
      m_node_queues[target_node]->push(std::move(task));
    }

    {
      // Wake up the workers to run the newly enqueued task. Notify all, first come
      // first serve
      std::lock_guard<std::mutex> lock(m_cv_mutex);
      m_job_cv.notify_all();
    }
  }

  void DagScheduler::wait_all() {
    std::unique_lock<std::mutex> lock(m_wait_mutex);

    auto wait_on_work = [this] () {
      return m_unfinished_tasks.load(std::memory_order_acquire) == 0;
    };

    m_wait_cv.wait(lock, wait_on_work);
  }

  void DagScheduler::on_task_complete(std::shared_ptr<Task> completed_task) {
    for (auto& child : completed_task->m_children) {
      // Decrement the child's counter. If it reaches 0, all dependencies are done.
      if (child->m_pending_parents.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        enqueue(child);
      }
    }

    if (m_unfinished_tasks.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      std::lock_guard<std::mutex> lock(m_wait_mutex);
      m_wait_cv.notify_all();
    }
  }

  void DagScheduler::worker_loop(std::size_t thread_id, numa_node_t home_node) {
    if (m_config.enable_pinning) {
      auto it { m_topology.find(home_node) };

      if (it != m_topology.end()) {
        pin_thread_to_cores(it->second);
      }
    }

    std::size_t num_numa_nodes { m_node_queues.size() };

    // General work loop is as follows:
    // - Check the local queue first
    // - If no work locally, check the global queue
    // - If still no work, fall back to stealing work from remote nodes
    // - Finally, if a task has been found, check concurrency/throttling limits
    //   If its okay to run, execute.
    while (m_running) {
      std::shared_ptr<Task> task { nullptr };

      task = m_node_queues[home_node]->pop();

      if (!task) {
        task = m_global_queue.pop();
      }

      if (!task) {
        for (std::size_t i = 1; i < num_numa_nodes; ++i) {
          numa_node_t remote_node = (home_node + i) % num_numa_nodes;
          task = m_node_queues[remote_node]->steal();
          if (task) break;
        }
      }

      if (task) {
        // Currently, set an arbitrary threshold of 8 for mem-bound throttling.
        bool is_high_mem { task->resources().memory_intensity >= 8 };
        bool acquired_token { false };

        if (is_high_mem) {
          std::size_t current_high_mem { m_active_high_mem_tasks.load(std::memory_order_acquire) };
          if (current_high_mem < m_config.max_concurrent_high_mem) {
            // Try to increment the count
            if (m_active_high_mem_tasks.fetch_add(1, std::memory_order_relaxed) < m_config.max_concurrent_high_mem) {
              acquired_token = true;
            } else {
              // Reverted because limit was hit
              m_active_high_mem_tasks.fetch_sub(1, std::memory_order_relaxed);
            }
          }

          // If throttled, sleep, or see if another queue can be used.
          if (!acquired_token) {
            m_node_queues[home_node]->push(std::move(task));

            std::this_thread::yield(); // Yield to avoid massive busy spin
            continue;
          }
        }

        // Execute task
        try {
          task->execute();
        } catch (const std::exception& e) {
          m_logger->error("[Worker Thread {}] Task failed with exception: {}",
                          thread_id,
                          e.what());
        } catch (...) {
          m_logger->error("[Worker Thread {}] Task failed with unknown exception!",
                          thread_id);
        }

        if (acquired_token) {
          m_active_high_mem_tasks.fetch_sub(1, std::memory_order_release);
        }

        on_task_complete(std::move(task));
      } else {
        std::unique_lock<std::mutex> lock(m_cv_mutex);

        auto wait_for_work = [this]() {
          return !m_running;
        };

        m_job_cv.wait_for(lock, std::chrono::milliseconds(5), wait_for_work);
      }
    }
  }
} // namespace xalgospp::scheduling
