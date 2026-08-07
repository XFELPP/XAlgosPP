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

#include "xalgospp/algorithm.hh"
#include "xalgospp/features/peakfinder8.hh"

#include <ncarray/ncarrays.hh>
#include <ncarray/soarrays.hh>
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;

PYBIND11_MODULE(features, features_module) {
  // NOTE: This must be accessible (in PYTHONPATH minimally!) or init will fail
  py::module_::import("ncarray");

  using HostPF8 = xalgospp::features::Peakfinder8<ncarray::HostTag>;

  py::classh<HostPF8::Params>(features_module, "Peakfinder8Params")
    .def(py::init<>())
    .def_readwrite("ADC_threshold", &HostPF8::Params::ADC_threshold)
    .def_readwrite("min_SNR", &HostPF8::Params::min_SNR)
    .def_readwrite("min_pixel_count", &HostPF8::Params::min_pixel_count)
    .def_readwrite("max_pixel_count", &HostPF8::Params::max_pixel_count)
    .def_readwrite("local_background_radius", &HostPF8::Params::local_background_radius)
    .def_readwrite("max_number_peaks", &HostPF8::Params::max_number_peaks)
    .def_readwrite("num_radial_bins", &HostPF8::Params::num_radial_bins)
    .def_readwrite("iterations", &HostPF8::Params::iterations)
    .def_readwrite("width", &HostPF8::Params::width)
    .def_readwrite("height", &HostPF8::Params::height);

  py::classh<HostPF8>(features_module, "Peakfinder8")
    .def(py::init<>())
    .def(py::init<HostPF8::Params>(), py::arg("params"))
    .def("configure",
         [](HostPF8& self, const HostPF8::Params& params) {
           self.configure(params);
         },
         py::arg("params"))
    .def("print_configuration",
         py::overload_cast<>(&HostPF8::print_configuration, py::const_))
    .def("stage", [](HostPF8& self) { self.stage(); })
    .def("process",
         [](const HostPF8& self,
            const ncarray::SOArrayView& input,
            ncarray::SOArrayView& output) {
           return self.process(input, output);
         },
         py::arg("input"),
         py::arg("output"),
         py::call_guard<py::gil_scoped_release>())
    .def("__call__",
         [](const HostPF8& self,
            const ncarray::SOArrayView& input,
            ncarray::SOArrayView& output) {
           return self.process(input, output);
         },
         py::arg("input"),
         py::arg("output"),
         py::call_guard<py::gil_scoped_release>());
} // features_module
