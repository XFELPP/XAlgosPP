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

#include "xalgospp/parameters.hh"

#include "ncarray/storage.hh" // HostTag/DevTag
#include "ncarray/ncarrays.hh"

#ifdef __CUDACC__

#include <cuda/std/tuple>

namespace hd_std = cuda::std;

#ifndef XALG_HD
#define XALG_HD __host__ __device__
#endif

#else

#include <tuple>

namespace hd_std = std;

#ifndef XALG_HD
#define XALG_HD
#endif

#endif // __CUDACC__

namespace xalgospp {
  /**
   * Base Algorithm class.
   *
   * All algorithms in XAlgosPP derive from this basic class. An Algorithm is simply
   * a wrapper which consists of the following states/life-cycle stages:
   * 1. Construction/configuration - The algorithm is setup with input parameters.
   * 2. Staging [OPTIONAL] - The algorithm may optionally have a staging step when
   *    appropriate. This allows for performing auxiliary actions like determining
   *    extra metadata that may be useful for processing.
   * 3. Processing - Various APIs/interfaces are provided to run the actual
   *    algorithm.
   *
   * Furthermore, each algorithm will define, via type aliases, the inputs and
   * outputs that it can accept, along with the type of Parameters object it requires.
   *
   * @tparam Derived CRTP subclass type.
   * @tparam Tag An indicator of whether the algorithm is to operate on host or device
   *         memory.
   */
  template <class Derived, typename Tag = ncarray::HostTag>
  class Algorithm {
  public:
    Algorithm() = default;

    using Input = type_list<>;
    using Output = type_list<>;

    /**
     * Perform configuration of the algorithm given an input set of parameters.
     *
     * @tparam Params The type of the Parameters to use.
     * @param[in] params The Algorithm-specific Parameters to use.
     */
    template <typename Params> void configure(const Params& params) {
      if constexpr (requires { static_cast<Derived*>(this)->configure_impl(params); }) {
        static_cast<Derived*>(this)->configure_impl(params);
      }
    }

    /**
     * Optional stage to perform associated actions before processing.
     *
     * An algorithm implementation may optionally provide a staging routine when
     * associated steps, e.g. for metadata retreival, would be useful.
     */
    void stage() {
      if constexpr (requires { static_cast<Derived*>(this)->stage_impl(); }) {
        static_cast<Derived*>(this)->stage_impl();
      }
    }

    /**
     * For Algorithms which provide staging of associated data, retrieve what was staged.
     *
     * In some execution contexts, it may be desirable to control which of many parallel
     * processing units performs staging. This function and the related setter allow
     * for retrieving and setting staged data, allowing precise control of how and when
     * different parallel units perform these actions.
     *
     * @returns Any staged data for the Algorithm. If applicable.
     */
    const auto get_staged_data() const {
      if constexpr (requires { static_cast<const Derived*>(this)->get_staged_data_impl(); }) {
        return static_cast<const Derived*>(this)->get_staged_data_impl();
      }
    }

    /**
     * For Algorithms which provide staging of associated data, alternatively set it.
     *
     * In some execution contexts, it may be desirable to control which of many parallel
     * processing units performs staging. This function and the related getter allow
     * for retrieving and setting staged data, allowing precise control of how and when
     * different parallel units perform these actions.
     *
     * @tparam StagedData The type(s) of the staged data.
     * @param[in] staged_data The data that was staged somehow and should be used by the
     *            Algorithm. This is Algorithm-specific.
     */
    template <typename... StagedData>
    void set_staged_data(StagedData&&... staged_data) {
      if constexpr (requires {
          static_cast<Derived*>(this)->set_staged_data_impl(hd_std::forward<StagedData>(staged_data)...);
      }) {
        static_cast<Derived*>(this)->set_staged_data_impl(hd_std::forward<StagedData>(staged_data)...);
      }
    }

    /**
     * For Algorithms which provide staging, retrieve the size (in bytes) of the staged data.
     *
     * @returns The size in bytes of any (and all) staged data. If no data is staged, then 0.
     */
    hd_std::size_t staged_data_size() const {
      if constexpr (requires { static_cast<const Derived*>(this)->staged_data_size_impl(); }) {
        return static_cast<const Derived*>(this)->staged_data_size_impl();
      }

      return 0;
    }

    template <typename... Inputs, typename... Outputs>
    void process(const hd_std::tuple<Inputs...>& inputs,
                 hd_std::tuple<Outputs...>& outputs) {
      static_cast<Derived*>(this)->process_impl(inputs, outputs);
    }

    template <typename... Inputs, typename... Outputs>
    void operator()(const hd_std::tuple<Inputs...>& inputs,
                    hd_std::tuple<Outputs...>& outputs) {
      static_cast<Derived*>(this)->process_impl(inputs, outputs);
    }

    template <typename Input, typename Output>
    void process(const Input& input, Output& output) const {
      if constexpr (requires {
          static_cast<const Derived*>(this)->process_impl(input, output);
        }) {
        static_cast<const Derived*>(this)->process_impl(input, output);
      } else {
        auto in_t { hd_std::forward_as_tuple(input) };
        auto out_t { hd_std::forward_as_tuple(output) };

        static_cast<const Derived*>(this)->process_impl(in_t, out_t);
      }
    }

    template <typename Input, typename Output>
    void operator()(const Input& input, Output& output) {
      process(input, output);
    }

    const char* name() const {
      if constexpr (requires { static_cast<const Derived*>(this)->name_impl(); }) {
        return static_cast<const Derived*>(this)->name_impl();
      }

      return "UnnamedAlgorithm";
    }
  };
} // namespace xalgospp

#endif // XALGOSPP_ALGORITHM_HH
