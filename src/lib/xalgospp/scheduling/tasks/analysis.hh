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

#ifndef XALGOSPP_SCHEDULING_TASKS_ANALYSIS_HH
#define XALGOSPP_SCHEDULING_TASKS_ANALYSIS_HH

#include "xalgospp/scheduling/task.hh"

#include <ncarray/ncarrays.hh>
#ifdef XALG_HAS_CUDA
#include <ncarray/ncdevarrays.cuh>
#endif
#include <ncarray/storage.hh>

#include <memory>
#include <type_traits>

namespace xalgospp::scheduling {
  template <class Algo, class ParentTask, class MemTag = ncarray::HostTag>
  class AlgorithmTask : public Task {
  public:
    AlgorithmTask(DagScheduler& scheduler,
                  std::shared_ptr<Algo> algo,
                  std::shared_ptr<ParentTask> parent)
      : m_scheduler(scheduler)
      , m_algo(algo)
      , m_parent(parent)
    {
      // Try to resolve the locality based on the input buffer pointer
      LocalityHint hint {
        /* preferred_node          = */ ANY_NODE, // Set to ANY_NODE so ptr is used
        /* memory_affinity_ptr     = */ nullptr,
        /* memory_affinity_ptr_ptr = */ parent->get_output_ptr_address()
      };
      set_locality(hint);

      ResourceRequirements reqs {
        /* memory_intensity = */ 8, // Trigger throttling
        /* requires_gpu     = */ std::is_same_v<MemTag, ncarray::DevTag>,
        /* custom_slots     = */ 0
      };
      set_resources(reqs);
    }

    void execute() override {
      auto input = m_parent->get_output_view();

      numa_node_t current_node { get_current_thread_numa_node() };

      m_output = m_scheduler.acquire_buffer(current_node,
                                            input.ndim(),
                                            input.shape(),
                                            ncarray::DType::float32);

      ncarray::NCViewFor<MemTag> out_view = m_output->view();
      m_algo->process(input, out_view);
    }

    ncarray::NCViewFor<MemTag> get_output_view() const { return m_output->view(); }

  private:
    DagScheduler& m_scheduler;
    std::shared_ptr<Algo> m_algo;
    std::shared_ptr<ParentTask> m_parent;
    std::shared_ptr<ncarray::NCOwnerFor<MemTag>> m_output;
  };

} // namespace xalgospp::scheduling

#endif // XALGOSPP_SCHEDULING_TASKS_ANALYSIS_HH
