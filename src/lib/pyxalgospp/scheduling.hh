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

#ifndef PYXALGOSPP_SCHEDULING_HH
#define PYXALGOSPP_SCHEDULING_HH

#include "xalgospp/scheduling/task.hh"

#include <ncarray/ncarrays.hh>
#include <ncarray/soarrays.hh>
#include <pybind11/pybind11.h>
#include <pybind11/trampoline_self_life_support.h>

#include <atomic>
#include <memory>
#include <stdexcept>

namespace py = pybind11;

namespace pyxalgospp::scheduling {
  /**
   * A minimal trampoline class for allowing Python Task subclassing with DagScheduler.
   */
  class PyTask
    : public xalgospp::scheduling::Task, public py::trampoline_self_life_support {
  public:
    using xalgospp::scheduling::Task::Task;

    void keep_alive_until_executed(std::shared_ptr<xalgospp::scheduling::Task> self) {
      m_keep_alive = std::move(self);
    }

    void execute() override {
      py::gil_scoped_acquire acquire;
      struct Release {
        std::shared_ptr<xalgospp::scheduling::Task>& ptr;

        ~Release() { ptr.reset(); }
      };

      Release release { m_keep_alive };

      try {
        PYBIND11_OVERRIDE_PURE(void, xalgospp::scheduling::Task, execute);
      } catch (py::error_already_set& err) {
        // Need to be holding GIL
        throw std::runtime_error(err.what());
      }
    }

    bool is_generator() const override {
      int cached { m_is_generator.load(std::memory_order_acquire) };
      if (cached < 0) {
        // Check once and thten cache to speed up query on hot-path
        bool value { false };
        {
          py::gil_scoped_acquire acquire;
          auto check_is_generator = [this]() -> bool {
            PYBIND11_OVERRIDE(bool, xalgospp::scheduling::Task, is_generator);
          };
          value = check_is_generator();
        }

        if (value) {
          cached = 1;
        } else {
          cached = 0;
        }

        m_is_generator.store(cached, std::memory_order_release);
      }

      return cached == 1;
    }

  private:
    mutable std::atomic<int> m_is_generator { -1 };
    std::shared_ptr<xalgospp::scheduling::Task> m_keep_alive;
  };
} // namespace pyxalgospp::scheduling

#endif // PYXALGOSPP_SCHEDULING_HH
