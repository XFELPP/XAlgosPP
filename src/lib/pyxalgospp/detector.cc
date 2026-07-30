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

#include <ncarray/soarrays.hh>
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;

PYBIND11_MODULE(detector, detector_module) {
  // NOTE: This must be accessible (in PYTHONPATH minimally!) or init will fail
  py::module_::import("ncarray");

  using RtCalibration = xalgospp::det::Calibration<xalgospp::det::RuntimeCalibPolicy>;

  py::enum_<xalgospp::det::CalibParameters::MappingMode>(detector_module, "MappingMode")
    .value("Direct", xalgospp::det::CalibParameters::MappingMode::Direct)
    .value("Epix10k", xalgospp::det::CalibParameters::MappingMode::Epix10k)
    .export_values();

  py::classh<RtCalibration::Params>(detector_module, "CalibrationParams")
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
  py::classh<RtCalibration>(detector_module, "Calibration")
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

  py::classh<xalgospp::lcls2::CalibDocMetadata>(detector_module, "CalibDocMetadata")
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

  py::classh<xalgospp::lcls2::CalibrationConstants>(detector_module, "CalibrationConstants")
    .def_readonly("data", &xalgospp::lcls2::CalibrationConstants::data)
    .def_readonly("dtype", &xalgospp::lcls2::CalibrationConstants::dtype)
    .def_readonly("shape", &xalgospp::lcls2::CalibrationConstants::shape)
    .def_readonly("metadata", &xalgospp::lcls2::CalibrationConstants::metadata)
    .def("to_ncarray",
         [](const xalgospp::lcls2::CalibrationConstants& self) {
           return xalgospp::lcls2::CalibrationConstants::to_ncarray(self);
         });

  detector_module.def("get_detector_short_name",
                      &xalgospp::lcls2::get_detector_short_name,
                      py::arg("base_url"),
                      py::arg("det_type"),
                      py::arg("det_serial_no"));

  detector_module.def("retrieve_calib_constants_of_type",
                      &xalgospp::lcls2::retrieve_calib_constants_of_type,
                      py::arg("base_url"),
                      py::arg("det_short_name"),
                      py::arg("experiment"),
                      py::arg("run"),
                      py::arg("constants_types"));
} // detector_module
