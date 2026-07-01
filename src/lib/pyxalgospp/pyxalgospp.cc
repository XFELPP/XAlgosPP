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

#include "xalgospp/detector/detector.hh"
#include "xalgospp/detector/lcls2/calibdb.hh"

#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <string>

namespace py = pybind11;

PYBIND11_MODULE(_pyxalgospp, pyxalgospp_module, py::mod_gil_not_used()) {
  // NOTE: This must be in PYTHONPATH (or generally accessible!) or init will fail
  py::module_::import("ncarray");

  py::classh<xalgospp::CalibParameters>(pyxalgospp_module, "CalibParameters")
    .def(py::init<>())
    .def_readwrite("gain_shift", &xalgospp::CalibParameters::gain_shift)
    .def_readwrite("gain_mask", &xalgospp::CalibParameters::gain_mask)
    .def_readwrite("data_mask", &xalgospp::CalibParameters::data_mask)
    .def_readwrite("num_gains", &xalgospp::CalibParameters::num_gains)
    .def_readwrite("invalid_pattern", &xalgospp::CalibParameters::invalid_pattern)
    .def_readwrite("invalid_value", &xalgospp::CalibParameters::invalid_value)
    .def_property("mapping",
                  [](const xalgospp::CalibParameters& self) { return self.mapping; },
                  [](xalgospp::CalibParameters& self, xalgospp::CalibParameters::MappingMode mode) {
                    self.mapping = mode;
                  });

  py::enum_<xalgospp::CalibParameters::MappingMode>(pyxalgospp_module, "MappingMode")
    .value("Direct", xalgospp::CalibParameters::MappingMode::Direct)
    .value("Epix10k", xalgospp::CalibParameters::MappingMode::Epix10k)
    .export_values();

  pyxalgospp_module.attr("JungfrauCalibParameters") = xalgospp::JungfrauCalibParameters;
  pyxalgospp_module.attr("Epix10kCalibParameters") = xalgospp::Epix10kCalibParameters;
/*
  py::classh<xalgospp::LCLS2::CalibrationConstants>(pyxalgospp_module, "CalibrationConstants")
    .def_readonly("data", &xalgospp::LCLS2::CalibrationConstants::data)
    .def_readonly("dtype", &xalgospp::LCLS2::CalibrationConstants::dtype)
    .def_readonly("shape", &xalgospp::LCLS2::CalibrationConstants::shape)
    .def("to_ncarray",
         [](const xalgospp::LCLS2::CalibrationConstants& self) {
           return xalgospp::LCLS2::to_ncarray(self);
         });
*/
  pyxalgospp_module.def("get_detector_short_name",
                        &xalgospp::LCLS2::get_detector_short_name,
                        py::arg("base_url"),
                        py::arg("det_type"),
                        py::arg("det_serial_no"));

  pyxalgospp_module.def("retrieve_calib_constants_of_type",
                        &xalgospp::LCLS2::retrieve_calib_constants_of_type,
                        py::arg("base_url"),
                        py::arg("det_short_name"),
                        py::arg("experiment"),
                        py::arg("run"),
                        py::arg("constants_types"));

  // Bind Detector Algorithm flow
  //py::classh<xalgospp::DetectorAlgorithm, std::unique_ptr<xalgospp::DetectorAlgorithm>>(pyxalgospp_module,
  //                                                                                      "DetectorAlgorithm")
  /*
  py::classh<xalgospp::DetectorAlgorithm>(pyxalgospp_module, "DetectorAlgorithm")
    .def_property_readonly("name", &xalgospp::DetectorAlgorithm::name)
    .def("process",
         &xalgospp::DetectorAlgorithm::process,
         py::arg("input"),
         py::arg("output"));

  py::classh<xalgospp::AlgorithmFactory>(pyxalgospp_module, "AlgorithmFactory")
    .def_static("create_calibration",
                &xalgospp::AlgorithmFactory::create_calibration,
                py::arg("params"),
                py::arg("constants"));
  */
} // pyxalgospp_module
