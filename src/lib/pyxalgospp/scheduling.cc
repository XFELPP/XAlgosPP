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

#include "pyxalgospp/scheduling.hh"

#include "xalgospp/detector/calibration.hh"
#include "xalgospp/scheduling/dag_scheduler.hh"
#include "xalgospp/scheduling/staging.hh"

#include <ncarray/soarrays.hh>
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#ifdef _WIN32
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#else
#include <sys/types.h>
#endif

#include <memory>
#include <vector>

namespace py = pybind11;

PYBIND11_MODULE(scheduling, scheduling_module) {
  // NOTE: This must be accessible (in PYTHONPATH minimally!) or init will fail
  py::module_::import("ncarray");

  py::enum_<xalgospp::scheduling::ShmemType>(scheduling_module, "ShmemType")
    .value("MACHINE", xalgospp::scheduling::ShmemType::MACHINE)
    .value("SOCKET", xalgospp::scheduling::ShmemType::SOCKET)
    .value("NUMA", xalgospp::scheduling::ShmemType::NUMA)
    .value("L3CACHE", xalgospp::scheduling::ShmemType::L3CACHE)
    .value("L2CACHE", xalgospp::scheduling::ShmemType::L2CACHE)
    .export_values();

  py::classh<xalgospp::scheduling::ResourceRequirements>(scheduling_module,
                                                         "ResourceRequirements")
    .def(py::init<>())
    .def_readwrite("memory_intensity",
                   &xalgospp::scheduling::ResourceRequirements::memory_intensity)
    .def_readwrite("requires_gpu",
                   &xalgospp::scheduling::ResourceRequirements::requires_gpu)
    .def_readwrite("custom_slots",
                   &xalgospp::scheduling::ResourceRequirements::custom_slots);

  py::classh<xalgospp::scheduling::LocalityHint>(scheduling_module, "LocalityHint")
    .def(py::init<>())
    .def_readwrite("preferred_node",
                   &xalgospp::scheduling::LocalityHint::preferred_node);

  py::class_<
    xalgospp::scheduling::Task,
    pyxalgospp::scheduling::PyTask,
    std::shared_ptr<xalgospp::scheduling::Task>
  >(scheduling_module, "Task")
    .def(py::init<>())
    .def("execute", &xalgospp::scheduling::Task::execute)
    .def("is_generator", &xalgospp::scheduling::Task::is_generator)
    .def_property("data",
                  [](xalgospp::scheduling::Task& self) -> py::object {
                    if (auto* py_self = dynamic_cast<pyxalgospp::scheduling::PyTask*>(&self)) {
                      return py_self->get_data();
                    }
                    return py::none();
                  },
                  [](xalgospp::scheduling::Task& self, py::object data) {
                    if (auto* py_self = dynamic_cast<pyxalgospp::scheduling::PyTask*>(&self)) {
                      py_self->set_data(data);
                    }
                  })
    .def("locality", &xalgospp::scheduling::Task::locality)
    .def("set_locality", &xalgospp::scheduling::Task::set_locality)
    .def("resources", &xalgospp::scheduling::Task::resources)
    .def("set_resources", &xalgospp::scheduling::Task::set_resources)
    .def("add_dependency",
         [](py::object self_obj, py::object parent_obj) {
           auto self_task = py::cast<std::shared_ptr<xalgospp::scheduling::Task>>(self_obj);
           auto parent_task = py::cast<std::shared_ptr<xalgospp::scheduling::Task>>(parent_obj);
           if (auto py_self = std::dynamic_pointer_cast<pyxalgospp::scheduling::PyTask>(self_task)) {
             py_self->attach_python_object(self_obj);
           }
           if (auto py_parent = std::dynamic_pointer_cast<pyxalgospp::scheduling::PyTask>(parent_task)) {
             py_parent->attach_python_object(parent_obj);
           }
           self_task->add_dependency(parent_task);
         },
         py::arg("parent"));

  using DagScheduler = xalgospp::scheduling::DagScheduler;
  py::classh<DagScheduler::Config>(scheduling_module, "DagSchedulerConfig")
    .def(py::init<>())
    .def_readwrite("num_numa_nodes", &DagScheduler::Config::num_numa_nodes)
    .def_readwrite("threads_per_node", &DagScheduler::Config::threads_per_node)
    .def_readwrite("enable_pinning", &DagScheduler::Config::enable_pinning)
    .def_readwrite("max_concurrent_high_mem",
                   &DagScheduler::Config::max_concurrent_high_mem)
    .def_readwrite("max_concurrency_multiplier",
                   &DagScheduler::Config::max_concurrency_multiplier)
    .def_readwrite("enable_dynamic_backpressure",
                   &DagScheduler::Config::enable_dynamic_backpressure)
    .def_readwrite("enable_autotuning", &DagScheduler::Config::enable_autotuning)
    .def_readwrite("raw_frame_size_bytes", &DagScheduler::Config::raw_frame_size_bytes)
    .def_readwrite("warmup_submissions", &DagScheduler::Config::warmup_submissions)
    .def_readwrite("node_memory_bandwidth_limit_gbps",
                   &DagScheduler::Config::node_memory_bandwidth_limit_gbps)
    .def_readwrite("percent_bandwidth_is_high_mem",
                   &DagScheduler::Config::percent_bandwidth_is_high_mem);

  using RtCalibration = xalgospp::det::Calibration<xalgospp::det::RuntimeCalibPolicy>;
  py::classh<DagScheduler>(scheduling_module, "DagScheduler")
    .def(py::init<DagScheduler::Config>(), py::arg("config") = DagScheduler::Config{})
    .def("stage_algorithm",
         [](DagScheduler& self, RtCalibration& algo, xalgospp::scheduling::ShmemType st) {
           self.stage_algorithm(algo, st);
         },
         py::arg("algo"),
         py::arg("shmem_type") = xalgospp::scheduling::ShmemType::SOCKET,
         py::call_guard<py::gil_scoped_release>())
    .def("acquire_buffer",
         [](DagScheduler& self,
            const std::vector<ssize_t>& shape,
            ncarray::DType dtype,
            int node) {
           const ssize_t* s { shape.data() };
           ssize_t ndim { static_cast<ssize_t>(shape.size()) };

           return self.acquire_buffer(node, ndim, s, dtype);
         },
         py::arg("shape"),
         py::arg("dtype") = ncarray::DType::float32,
         py::arg("node") = -1)
    .def("submit_dag",
         &DagScheduler::submit_dag,
         py::arg("tasks"),
         py::call_guard<py::gil_scoped_release>())
    .def("enqueue",
         &DagScheduler::enqueue,
         py::arg("task"),
         py::call_guard<py::gil_scoped_release>())
    .def("wait_all",
         &DagScheduler::wait_all,
         py::call_guard<py::gil_scoped_release>())
    .def("check_memory_bandwidth",
         &DagScheduler::check_memory_bandwidth,
         py::arg("test_bytes") = 32ULL * 1024 * 1024,
         py::arg("niter") = 10,
         py::call_guard<py::gil_scoped_release>());
} // scheduling_module
