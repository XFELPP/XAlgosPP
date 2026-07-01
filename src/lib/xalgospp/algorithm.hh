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

#ifndef XALGOSPP_ALGORITHM_HH
#define XALGOSPP_ALGORITHM_HH

#include "ncarray/ncarrays.hh"

namespace xalgospp {

  template <class Derived>
  class AlgorithmBase {
  public:
    const char* name() const {
      if constexpr (requires { static_cast<const Derived*>(this)->name_impl(); }) {
        return static_cast<const Derived*>(this)->name_impl();
      }

      return "UnnamedAlgorithm";
    }

    void process(const ncarray::NCArrayView& input, ncarray::NCArrayView& output) {
      if constexpr (requires { static_cast<Derived*>(this)->process_impl(input, output); }) {
        static_cast<Derived*>(this)->process_impl(input, output);
      }
    }
  };
} // namespace xalgospp

#endif // XALGOSPP_ALGORITHM_HH
