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
    AlgorithmTask(std::shared_ptr<Algo> algo,
                  std::shared_ptr<ParentTask> parent,
                  std::shared_ptr<ncarray::NCOwnerFor<MemTag>> output)
      : m_algo(algo)
      , m_parent(parent)
      , m_output(output)
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
      // TODO: Fix the base Algorithm param resolution -- having to do these sorts
      // of tedious extractions because of the tuple overload is annoying.
      ncarray::NCViewFor<MemTag> input { m_parent->get_output_view() };

      if (m_output->size() == 0) {
        // TODO: Make this better! We dont know what the output is supposed to be
        //       It should come from the wrapped algorithm somehow.
        *m_output = ncarray::NCOwnerFor<MemTag>(input.ndim(),
                                                input.shape(),
                                                ncarray::DType::float32);
      }

      ncarray::NCViewFor<MemTag> out_view = m_output->view();
      m_algo->process(input, out_view);
    }

  private:
    std::shared_ptr<Algo> m_algo;
    std::shared_ptr<ParentTask> m_parent;
    std::shared_ptr<ncarray::NCOwnerFor<MemTag>> m_output;
  };

} // namespace xalgospp::scheduling

#endif // XALGOSPP_SCHEDULING_TASKS_ANALYSIS_HH
