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

#ifndef XALGOSPP_SCHEDULING_QUEUE_HH
#define XALGOSPP_SCHEDULING_QUEUE_HH

#include "xalgospp/scheduling/task.hh"

#include <deque>
#include <memory>
#include <mutex>

namespace xalgospp::scheduling {
  /**
   * A generic queue from which DAG tasks will be taken and scheduled.
   */
  class WorkQueue {
  public:
    /**
     * Add new work to the queue.
     *
     * @param[in] The new work to be queued.
     */
    void push(std::shared_ptr<Task> task) {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_tasks.push_back(std::move(task));
    }

    /**
     * Take the next work item from the front of the queue.
     *
     * Workers preferrentially take work from their local (in NUMA locality sense)
     * queues. When doing so, they pop from the front using this routine. Work
     * can also be stolen (see below), in which case it pulls from the back.
     *
     * @returns The work to do or nullptr.
     */
    std::shared_ptr<Task> pop() {
      std::lock_guard<std::mutex> lock(m_mutex);

      if (m_tasks.empty()) {
        return nullptr;
      }

      auto task { std::move(m_tasks.front()) };
      m_tasks.pop_front();

      return task;
    }

    /**
     * Steal work from the queue.
     *
     * If a worker does not find pending work locally, it can take work from a remote
     * queue. In that case, it will steal from the back of the queue in order to
     * minimize contention with the local workers that are popping from the front.
     *
     * @returns The stolen work or nullptr.
     */
    std::shared_ptr<Task> steal() {
      std::lock_guard<std::mutex> lock(m_mutex);

      if (m_tasks.empty()) {
        return nullptr;
      }

      // Tasks are stolen from the back of the queue to minimize conflict/contention
      // with other workers popping from the front
      auto task { std::move(m_tasks.back()) };
      m_tasks.pop_back();

      return task;
    }

    /**
     * Whether there is outstanding work to do on this queue.
     *
     * @returns Whether there is outstanding work to do on this queue.
     */
    bool empty() {
      std::lock_guard<std::mutex> lock(m_mutex);

      return m_tasks.empty();
    }

  private:
    std::mutex m_mutex;
    std::deque<std::shared_ptr<Task>> m_tasks;
  };
} // namespace xalgospp::scheduling

#endif // XALGOSPP_SCHEDULING_QUEUE_HH
