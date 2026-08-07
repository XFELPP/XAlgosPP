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

#include "xalgospp/utilities/interconversion.hh"

#include "ncarray/dtype.hh"

#include <Eigen/Dense>

#include <stdexcept>
#include <string>

namespace xalgospp {
  ncarray::DType string_to_dtype(const std::string& dtype_str) {
    if (dtype_str == "bool" || dtype_str == "bool_") {
      return ncarray::DType::bool_;
    } else if (dtype_str == "char" || dtype_str == "char_") {
      return ncarray::DType::char_;
    } else if (dtype_str == "uint8") {
      return ncarray::DType::uint8;
    } else if (dtype_str == "uint16") {
      return ncarray::DType::uint16;
    } else if (dtype_str == "uint32") {
      return ncarray::DType::uint32;
    } else if (dtype_str == "uint64") {
      return ncarray::DType::uint64;
    } else if (dtype_str == "int8") {
      return ncarray::DType::int8;
    } else if (dtype_str == "int16") {
      return ncarray::DType::int16;
    } else if (dtype_str == "int32") {
      return ncarray::DType::int32;
    } else if (dtype_str == "int64") {
      return ncarray::DType::int64;
    /* float16/half not currently supported. */
    } else if (dtype_str == "float32") {
      return ncarray::DType::float32;
    } else if (dtype_str == "float64" || dtype_str == "double") {
      return ncarray::DType::float64;
    } else if (dtype_str == "float96"  ||
               dtype_str == "float128" ||
               dtype_str == "longdouble") {
      return ncarray::DType::float128;
    } else if (dtype_str == "complex64" || dtype_str == "csingle") {
      return ncarray::DType::complex64;
    } else if (dtype_str == "complex128" || dtype_str == "cdouble") {
      return ncarray::DType::complex128;
    } else if (dtype_str == "complex192" ||
               dtype_str == "complex256" ||
               dtype_str == "clongdouble") {
      return ncarray::DType::complex256;
    }

    throw invalid_type_string("Unsupported type str repr: " + dtype_str + "!");
  }
} // namespace XAlgosPP
