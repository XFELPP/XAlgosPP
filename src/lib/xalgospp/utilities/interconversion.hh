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

#include "xalgospp/parameters.hh"

#include <Eigen/Dense>
#include <ncarray/dtype.hh>
#include <ncarray/ncarrays.hh>
#include <ncarray/soarrays.hh>

#include <stdexcept>
#include <string>
#include <type_traits>

namespace xalgospp {
  namespace impl {
    using HostArrayViewTypes =
      type_list<ncarray::NCViewFor<ncarray::HostTag>, ncarray::SOViewFor<ncarray::HostTag>>;
  };

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
  ncarray::DType string_to_dtype(const std::string& dtype_str);

  /**
   * Convert an NCArrayView or SOArrayView to an Eigen Map.
   *
   * @tparam T The underlying datatype of the array views.
   * @tparam InputArg The NCArrayView or SOArrayView type.
   * @param view The NCArrayView to convert.
   * @returns An Eigen::Map over the NCArrayView's data.
   */
  template <typename T, typename InputArg>
  requires (impl::HostArrayViewTypes::template accepts<InputArg>)
  inline Eigen::Map<Eigen::Array<T, Eigen::Dynamic, 1>>
  to_eigen_array(const InputArg& view) {
    return Eigen::Map<Eigen::Array<T, Eigen::Dynamic, 1>>(static_cast<T*>(view.data()),
                                                          view.size());
  }
} // namespace XAlgosPP

#endif // XALGOSPP_UTILITIES_INTERCONVERSION_HH
