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

#ifndef XALGOSPP_SCHEDULING_TASKS_IO_HH
#define XALGOSPP_SCHEDULING_TASKS_IO_HH

#include "xalgospp/scheduling/task.hh"

#include <ncarray/ncarrays.hh>
#ifdef XALG_HAS_CUDA
#include <ncarray/ncdevarrays.cuh>
#endif
#include <ncarray/storage.hh>

#include <memory>
#include <type_traits>

namespace xalgospp::scheduling {
  template <class DataSource, class DataFetcher, class MemTag = ncarray::HostTag>
  class ReadImageTask : public Task {
  public:
    using StepIdxType = typename DataSource::DSTraits::StepIdxType;

    ReadImageTask(DataSource& ds,
                  DataFetcher& fetcher,
                  StepIdxType idx)
      : m_ds(ds)
      , m_fetcher(std::move(fetcher))
      , m_idx(idx)
    {
      // For IO, we want to schedule the Task to run on any CPU.
      // Once the data has been fetched, then down-stream Tasks must use the
      // locality hints to try to remain running where it was initially fetched.
      LocalityHint hint {
        /* preferred_node          = */ ANY_NODE,
        /* memory_affinity_ptr     = */ nullptr,
        /* memory_affinity_ptr_ptr = */ nullptr
      };
      set_locality(hint);

      // We'll assume that mem-bus utilization will be low for the IO
      ResourceRequirements reqs {
        /* memory_intensity = */ 2, // Scale is 1 (low) to 10 (staturated). 8 throttles
        /* requires_gpu     = */ std::is_same_v<MemTag, ncarray::DevTag>,
        /* custom_slots     = */ 0
      };
      set_resources(reqs);
    }

    const void* const* get_output_ptr_address() const { return &m_output_ptr; }

    ncarray::NCViewFor<MemTag> get_output_view() const { return m_output_view; }

    void execute() override {
      m_output_view = m_fetcher(m_idx);

      m_output_ptr = m_output_view.data();
    }

  private:
    DataSource& m_ds;
    DataFetcher m_fetcher;
    StepIdxType m_idx;
    ncarray::NCViewFor<MemTag> m_output_view;
    const void* m_output_ptr { nullptr };
  };

  template <class DataSource, class DataFetcher, class MemTag = ncarray::HostTag>
  auto make_read_image_task(DataSource& ds,
                            DataFetcher&& fetcher,
                            typename DataSource::DSTraits::StepIdxType idx) {
    using FetcherT = std::decay_t<DataFetcher>;

    return
      std::make_shared<ReadImageTask<DataSource, FetcherT, MemTag>>(ds,
                                                                    std::forward<DataFetcher>(fetcher),
                                                                    idx);
  }

  template <typename ViewType>
  class ValueTask : public Task {
  public:
    ValueTask(ViewType view)
      : m_view(view)
    {}

    void execute() override {}

    ViewType get_output_view() const { return m_view; }

    const void* const* get_output_ptr_address() const {
      m_ptr = m_view.data();
      return &m_ptr;
    }

  private:
    ViewType m_view;
    mutable const void* m_ptr { nullptr };
  };

  template <class DataSource, class MemTag = ncarray::HostTag>
  class IOGeneratorTask : public Task {
  public:
    using DSTraits = typename DataSource::DSTraits;
    using StepIdxType = typename DSTraits::StepIdxType;

    template <typename TaskBuilder>
    IOGeneratorTask(DagScheduler& scheduler,
                    DataSource& ds,
                    TaskBuilder&& downstream_builder)
      : m_scheduler(scheduler)
      , m_ds(ds)
      , m_builder(std::forward<TaskBuilder>(downstream_builder))
    {
      // For IO, we want to schedule the Task to run on any CPU.
      // Once the data has been fetched, then down-stream Tasks must use the
      // locality hints to try to remain running where it was initially fetched.
      LocalityHint hint {
        /* preferred_node          = */ ANY_NODE,
        /* memory_affinity_ptr     = */ nullptr,
        /* memory_affinity_ptr_ptr = */ nullptr
      };
      set_locality(hint);

      // We'll assume that mem-bus utilization will be low for the IO
      ResourceRequirements reqs {
          /* memory_intensity = */ 1, // Scale is 1 (low) to 10 (staturated). 8 throttles
          /* requires_gpu     = */ std::is_same_v<MemTag, ncarray::DevTag>,
          /* custom_slots     = */ 0
      };
      set_resources(reqs);
    }

    void execute() override {
      auto idx { m_ds.next() };

      if (idx == DSTraits::ExhaustedSentinel) {
        return;
      }

      auto downstream_tasks { m_builder(idx) };

      m_scheduler.submit_dag(downstream_tasks);

      auto next_gen = std::make_shared<IOGeneratorTask>(m_scheduler, m_ds, m_builder);
      next_gen->add_dependency(shared_from_this());

      m_scheduler.submit_dag({ next_gen });
    }

    bool is_generator() const override { return true; }

  private:
    DagScheduler& m_scheduler;
    DataSource& m_ds;
    std::function<std::vector<std::shared_ptr<Task>>(StepIdxType)> m_builder;
  };

} // namespace xalgospp::scheduling

#endif // XALGOSPP_SCHEDULING_TASKS_IO_HH
