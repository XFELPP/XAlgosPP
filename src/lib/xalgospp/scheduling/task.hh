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

#ifndef XALGOSPP_SCHEDULING_TASK_HH
#define XALGOSPP_SCHEDULING_TASK_HH

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

namespace xalgospp::scheduling {
  using numa_node_t = int;
  constexpr numa_node_t ANY_NODE { -1 };

  /**
   * A representation of resource availability and concurrency tokens for task throttling.
   */
  struct ResourceRequirements {
    /**
     * A scale form 1 (low) to 10 (staturated) of memory saturation.
     */
    std::size_t memory_intensity { 1 };
    bool requires_gpu { false };        ///< Whether host/device bound.
    std::size_t custom_slots { 0 };     ///< Additional resources like IO slots
  };

  /**
   * Details for placement of a task on specific resources.
   */
  struct LocalityHint {
    /**
     * Indicate the NUMA node preference.
     */
    numa_node_t preferred_node { ANY_NODE };
    /**
     * The physical page location if provided.
     *
     * This will generally mean that there is a single static lookup where the data
     * will always be.
     */
    const void* memory_affinity_ptr { nullptr };
    /**
     * A pointer to the location.
     *
     * For dynamic lookup, where the data may be in different places, the pointer
     * to pointer can be used instead of the static option above.
     */
    const void* const* memory_affinity_ptr_ptr { nullptr };
  };

  class DagScheduler;

  /**
   * The Task represents the fundamental unit of processing work in a DAG.
   *
   * Inherits from enable_shared_from_this to allow generating new Tasks from shared_ptr
   * to Task that shares the ownership.
   */
  class Task : public std::enable_shared_from_this<Task> {
  public:
    virtual ~Task() = default;
    virtual void execute() = 0;

    const LocalityHint& locality() const { return m_locality; }

    void set_locality(LocalityHint hint) { m_locality = hint; }

    void set_locality_resolver(std::function<LocalityHint()> resolver) {
      m_locality_resolver = std::move(resolver);
    }

    const ResourceRequirements& resources() const { return m_resources; }

    void set_resources(ResourceRequirements reqs) { m_resources = reqs; }

    void add_dependency(std::shared_ptr<Task> parent) {
      parent->m_children.push_back(shared_from_this());
      m_pending_parents.fetch_add(1, std::memory_order_relaxed);
    }

  private:
    friend class DagScheduler;

    LocalityHint m_locality;
    std::function<LocalityHint()> m_locality_resolver;

    ResourceRequirements m_resources;

    std::vector<std::shared_ptr<Task>> m_children;

    std::atomic<size_t> m_pending_parents { 0 };
  };
} // namespace xalgospp::scheduling

#endif // XALGOSPP_SCHEDULING_TASK_HH
