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

#ifndef XALGOSPP_UTILITIES_INTERCONVERSION_HH
#define XALGOSPP_UTILITIES_INTERCONVERSION_HH

#include "ncarray/dtype.hh"

#include <Eigen/Dense>

#include <stdexcept>
#include <string>

namespace xalgospp {
  /**
   * Error indicating an unknown, unrecognized, or invalid type representation.
   */
  class invalid_type_string : public std::invalid_argument {
  public:
    using std::invalid_argument::invalid_argument;
  };

  /**
   * Convert a string representation of a type to an ncarray::DType.
   *
   * This utility handles converting various string/character forms/representations
   * for data types that may be encountered to the ncarray::DType. The latter is
   * used as the standard type identifier in this library.
   *
   * This is a host only function.
   *
   * @param[in] dtype_str The string representation of the datatype.
   * @returns The ncarray::DType for the string.
   * @exception invalid_type_string Thrown if the dtype_str is an unrecognized type
   *            representation.
   */
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

  /**
   * Convert an NCArrayView to an Eigen Map.
   *
   * @tparam T The underlying datatype of the array views.
   * @param view The NCArrayView to convert.
   * @returns An Eigen::Map over the NCArrayView's data.
   */
  template <typename T>
  inline Eigen::Map<Eigen::Array<T, Eigen::Dynamic, 1>>
  to_eigen_array(const ncarray::NCArrayView& view) {
    return Eigen::Map<Eigen::Array<T, Eigen::Dynamic, 1>>(static_cast<T*>(view.data()),
                                                          view.size());
  }
} // namespace XAlgosPP

#endif // XALGOSPP_UTILITIES_INTERCONVERSION_HH
