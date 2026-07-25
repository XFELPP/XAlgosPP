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

#include "xalgospp/algorithm.hh"
#include "xalgospp/detector/calibration.hh"
#include "xalgospp/detector/lcls2/calibdb.hh"
#include "xalgospp/scheduling/dag_scheduler.hh"
#include "xalgospp/scheduling/staging.hh"

#include <ncarray/soarrays.hh>
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <spdlog/cfg/env.h>

#include <memory>
#include <string>
#include <vector>

namespace py = pybind11;

PYBIND11_MODULE(_pyxalgospp, pyxalgospp_module) {
  // NOTE: This must be accessible (in PYTHONPATH minimally!) or init will fail
  py::module_::import("ncarray");

  spdlog::cfg::load_env_levels("PYXALGOSPP_LOG_LEVEL");

  // ----- Scheduler and DAG Bindings ----- //

  py::enum_<xalgospp::scheduling::ShmemType>(pyxalgospp_module, "ShmemType")
    .value("MACHINE", xalgospp::scheduling::ShmemType::MACHINE)
    .value("SOCKET", xalgospp::scheduling::ShmemType::SOCKET)
    .value("NUMA", xalgospp::scheduling::ShmemType::NUMA)
    .value("L3CACHE", xalgospp::scheduling::ShmemType::L3CACHE)
    .value("L2CACHE", xalgospp::scheduling::ShmemType::L2CACHE)
    .export_values();

  py::classh<xalgospp::scheduling::ResourceRequirements>(pyxalgospp_module,
                                                         "ResourceRequirements")
    .def(py::init<>())
    .def_readwrite("memory_intensity",
                   &xalgospp::scheduling::ResourceRequirements::memory_intensity)
    .def_readwrite("requires_gpu",
                   &xalgospp::scheduling::ResourceRequirements::requires_gpu)
    .def_readwrite("custom_slots",
                   &xalgospp::scheduling::ResourceRequirements::custom_slots);

  py::classh<xalgospp::scheduling::LocalityHint>(pyxalgospp_module, "LocalityHint")
    .def(py::init<>())
    .def_readwrite("preferred_node",
                   &xalgospp::scheduling::LocalityHint::preferred_node);

  py::class_<
    xalgospp::scheduling::Task,
    pyxalgospp::scheduling::PyTask,
    std::shared_ptr<xalgospp::scheduling::Task>
  >(pyxalgospp_module, "Task")
    .def(py::init<>())
    .def("execute", &xalgospp::scheduling::Task::execute)
    .def("is_generator", &xalgospp::scheduling::Task::is_generator)
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
  py::classh<DagScheduler::Config>(pyxalgospp_module, "DagSchedulerConfig")
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
  py::classh<DagScheduler>(pyxalgospp_module, "DagScheduler")
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

  // ----- Direct Algorithm Bindings ----- //

  py::enum_<xalgospp::det::CalibParameters::MappingMode>(pyxalgospp_module, "MappingMode")
    .value("Direct", xalgospp::det::CalibParameters::MappingMode::Direct)
    .value("Epix10k", xalgospp::det::CalibParameters::MappingMode::Epix10k)
    .export_values();

  py::classh<RtCalibration::Params>(pyxalgospp_module, "CalibrationParams")
    .def(py::init<>())
    .def_readwrite("base_url", &RtCalibration::Params::base_url)
    .def_readwrite("det_type", &RtCalibration::Params::det_type)
    .def_readwrite("det_serial_no", &RtCalibration::Params::det_serial_no)
    .def_readwrite("experiment", &RtCalibration::Params::experiment)
    .def_readwrite("run", &RtCalibration::Params::run)

    .def_readwrite("gain_shift", &RtCalibration::Params::gain_shift)
    .def_readwrite("gain_mask", &RtCalibration::Params::gain_mask)
    .def_readwrite("data_mask", &RtCalibration::Params::data_mask)
    .def_readwrite("num_gains", &RtCalibration::Params::num_gains)
    .def_readwrite("default_gain", &RtCalibration::Params::default_gain)
    .def_readwrite("invalid_pattern", &RtCalibration::Params::invalid_pattern)
    .def_readwrite("invalid_value", &RtCalibration::Params::invalid_value)
    .def_property("mapping",
                  [](const RtCalibration::Params& self) { return self.mapping; },
                  [](RtCalibration::Params& self,
                     xalgospp::det::CalibParameters::MappingMode mode) {
                    self.mapping = mode;
                  });
  py::classh<RtCalibration>(pyxalgospp_module, "Calibration")
    .def(py::init<>())
    .def(py::init<RtCalibration::Params>(), py::arg("params"))
    .def("configure",
         [](RtCalibration& self, const RtCalibration::Params& params) {
           self.configure(params);
         },
         py::arg("params"))
    .def("print_configuration", &RtCalibration::print_configuration)
    .def("stage", [](RtCalibration& self) { self.stage(); })
    .def("process",
         [](const RtCalibration& self,
            const ncarray::SOArrayView& input,
            ncarray::SOArrayView& output) {
           return self.process(input, output);
         },
         py::arg("input"),
         py::arg("output"),
         py::call_guard<py::gil_scoped_release>())
    .def("__call__",
         [](const RtCalibration& self,
            const ncarray::SOArrayView& input,
            ncarray::SOArrayView& output) {
           return self.process(input, output);
         },
         py::arg("input"),
         py::arg("output"),
         py::call_guard<py::gil_scoped_release>());

  py::classh<xalgospp::lcls2::CalibDocMetadata>(pyxalgospp_module, "CalibDocMetadata")
    .def_readonly("unix_timestamp", &xalgospp::lcls2::CalibDocMetadata::doc_unix_ts)
    .def_readonly("begin_run_validity",
                  &xalgospp::lcls2::CalibDocMetadata::doc_run_begin)
    .def_readonly("end_run_validity", &xalgospp::lcls2::CalibDocMetadata::doc_run_end)
    .def_readonly("bulk_data_id", &xalgospp::lcls2::CalibDocMetadata::data_doc_id)
    .def_readonly("serialized_type", &xalgospp::lcls2::CalibDocMetadata::doc_type)
    .def_readonly("type_of_constants", &xalgospp::lcls2::CalibDocMetadata::consts_name)
    .def_readonly("constants_element_datatype",
                  &xalgospp::lcls2::CalibDocMetadata::consts_dtype)
    .def_readonly("constants_ndim", &xalgospp::lcls2::CalibDocMetadata::consts_ndim)
    .def_readonly("constants_nelem", &xalgospp::lcls2::CalibDocMetadata::consts_nelem)
    .def_readonly("constants_shape", &xalgospp::lcls2::CalibDocMetadata::consts_shape);

  py::classh<xalgospp::lcls2::CalibrationConstants>(pyxalgospp_module, "CalibrationConstants")
    .def_readonly("data", &xalgospp::lcls2::CalibrationConstants::data)
    .def_readonly("dtype", &xalgospp::lcls2::CalibrationConstants::dtype)
    .def_readonly("shape", &xalgospp::lcls2::CalibrationConstants::shape)
    .def_readonly("metadata", &xalgospp::lcls2::CalibrationConstants::metadata)
    .def("to_ncarray",
         [](const xalgospp::lcls2::CalibrationConstants& self) {
           return xalgospp::lcls2::CalibrationConstants::to_ncarray(self);
         });

  pyxalgospp_module.def("get_detector_short_name",
                        &xalgospp::lcls2::get_detector_short_name,
                        py::arg("base_url"),
                        py::arg("det_type"),
                        py::arg("det_serial_no"));

  pyxalgospp_module.def("retrieve_calib_constants_of_type",
                        &xalgospp::lcls2::retrieve_calib_constants_of_type,
                        py::arg("base_url"),
                        py::arg("det_short_name"),
                        py::arg("experiment"),
                        py::arg("run"),
                        py::arg("constants_types"));
} // pyxalgospp_module
