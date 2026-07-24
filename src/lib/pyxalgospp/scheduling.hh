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

#include <pybind11/pybind11.h>

namespace py = pybind11;

namespace pyxalgospp::scheduling {
  /**
   * A minimal trampoline class for allowing Python Task subclassing with DagScheduler.
   */
  class PyTask : xalgospp::scheduling::Task {
  public:
    using xalgospp::scheduling::Task::Task;

    void execute() override {
      PYBIND11_OVERRIDE_PURE(void, Task, execute);
    }

    bool is_generator() const override {
      PYBIND11_OVERRIDE(bool, Task, is_generator);
    }
  };
} // namespace pyxalgospp::scheduling

#endif // PYXALGOSPP_SCHEDULING_HH
