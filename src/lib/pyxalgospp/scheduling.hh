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

#include <memory>

namespace py = pybind11;

namespace pyxalgospp::scheduling {
  /**
   * A minimal trampoline class for allowing Python Task subclassing with DagScheduler.
   */
  class PyTask : public xalgospp::scheduling::Task {
  public:
    using xalgospp::scheduling::Task::Task;

    void attach_python_object(py::object obj) {
      if (!m_py_obj) {
        m_py_obj = obj;
      }
    }

    void release_python_object() {
      py::gil_scoped_acquire acquire;
      m_py_obj = py::object();
    }

    ~PyTask() override {
      if (m_py_obj) {
        py::gil_scoped_acquire acquire;
        m_py_obj = py::object();
      }
    }

    void execute() override {
      {
        py::gil_scoped_acquire acquire;
        PYBIND11_OVERRIDE_PURE(void, xalgospp::scheduling::Task, execute);
      }
      release_python_object();
    }

    bool is_generator() const override {
      py::gil_scoped_acquire acquire;
      PYBIND11_OVERRIDE(bool, xalgospp::scheduling::Task, is_generator);
    }

  private:
    py::object m_py_obj;
  };
} // namespace pyxalgospp::scheduling

#endif // PYXALGOSPP_SCHEDULING_HH
