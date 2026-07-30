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

#ifndef XALGOSPP_UTILITIES_DISPATCH_HH
#define XALGOSPP_UTILITIES_DISPATCH_HH


#include <Eigen/Dense>
#include <ncarray/array_traits.hh>
#include <ncarray/dtype.hh>

namespace XAlgosPP {
  /**
   * Dispatch an operation over an (SO/)NCArray type via an Eigen map.
   *
   * This utility dispatches the array type to the correct dtype, and builds an
   * Eigen map over it. The visiting callback takes the map as input, so you can
   * use normal Eigen operations.
   */
  template <ncarray::ArrayLike A, typename Visitor>
  auto ncarray_eigen_dispatch(const A& view, Visitor&& visitor) {
    // Eigen has pixel/element strides, but ncarray (like NumPy) uses byte strides
    if (view.is_pointer_axis(0) || view.is_pointer_axis(1)) {
      throw std::runtime_error("Only contiguous arrays supported right now!");
    }

    auto cast_and_visit = [&] <typename T> () {
      long outer_stride = view.stride(0) / sizeof(T);
      long inner_stride = view.stride(1) / sizeof(T);

      using MatrixT = const Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>;
      using StrideT = Eigen::Stride<Eigen::Dynamic, Eigen::Dynamic>;

      using MapT = Eigen::Map<MatrixT, Eigen::Unaligned, StrideT>;
      auto stride = StrideT(outer_stride, inner_stride);

      MapT map(static_cast<const T*>(view.data()),
               view.shape(0),
               view.shape(1),
               stride);
      return visitor(map);
    };

    return ncarray::dispatch(view.dtype(), cast_and_visit);
  }

} // namespace XAlgosPP

#endif // XALGOSPP_UTILITIES_DISPATCH_HH
