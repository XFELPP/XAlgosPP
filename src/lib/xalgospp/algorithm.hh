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

#include <cuda/std/cstdint>
#include <cuda/std/type_traits>

namespace hd_std = cuda::std;

#ifndef XALG_HD
#define XALG_HD __host__ __device__
#endif

#else

#include <cstdint>
#include <type_traits>

namespace hd_std = std;

#ifndef XALG_HD
#define XALG_HD
#endif

#endif // __CUDACC__

namespace xalgospp {
  /**
   * Indicates the degree of input/output type restrictions for an Algorithm.
   *
   * Algorithms provide a type_list of supported inputs and outputs. These are
   * used both to provide easy access to supported types, but also to constrain
   * the inputs on the Algorithm's processing routine. By default, the constraint
   * is to allow the cartesian product of the Input and Output type_lists. An
   * Algorithm subclass may opt instead to enforce stronger constraints, e.g., it
   * may require strict ordering such that the first item in the Input list must be
   * paired with the first item in the output list. Refer to each Algorithm to
   * verify if extra constraints are in effect.
   */
  enum class AlgTypeConstraint : hd_std::uint8_t {
    Product = 0,       ///< Any input type can pair with any output type
    StrictOrdering = 1 ///< Inputs must be paired with outputs in the order the appear
  };

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

    /**
     * The list of supported input types.
     */
    using Input = type_list<>;

    /**
     * The list of supported output types.
     *
     * Algorithm's process data by receiving an output to write the result to. These
     * types are those supported for that purpose.
     *
     * Algorithms provide a type_list of supported inputs and outputs. These are
     * used both to provide easy access to supported types, but also to constrain
     * the inputs on the Algorithm's processing routine. By default, the constraint
     * is to allow the cartesian product of the Input and Output type_lists. An
     * Algorithm subclass may opt instead to enforce stronger constraints, e.g., it
     * may require strict ordering such that the first item in the Input list must be
     * paired with the first item in the output list. Refer to each Algorithm to
     * verify if extra constraints are in effect.
     */
    using Output = type_list<>;

    static constexpr AlgTypeConstraint TypeConstraint { AlgTypeConstraint::Product };

    /**
     * Perform configuration of the algorithm given an input set of parameters.
     *
     * @tparam Params The type of the Parameters to use.
     * @param[in] params The Algorithm-specific Parameters to use.
     */
    template <typename Params>
    void configure(const Params& params) {
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

    template <
      typename InputArg,
      typename OutputArg,
      typename D = Derived,
      typename = hd_std::enable_if_t<
      []() {
        if constexpr (requires { D::TypeConstraint == AlgTypeConstraint::StrictOrdering; }) {
          return D::Input::template accepts_when_paired<InputArg, OutputArg, typename D::Output>;
        } else {
          return D::Input::template accepts<InputArg> && D::Output::template accepts<OutputArg>;
        }
      }()
      >
    >
    requires (D::Input::template accepts<InputArg> && D::Output::template accepts<OutputArg>)
    void process(InputArg&& input, OutputArg&& output) const {
      static_assert(Derived::Input::template accepts<InputArg>,
                    "Algorithm Error: Input type not supported by Derived::Input list");
      static_assert(Derived::Output::template accepts<OutputArg>,
                    "Algorithm Error: Output type not supported by Derived::Output list");

      static_cast<const Derived*>(this)->process_impl(hd_std::forward<InputArg>(input),
                                                      hd_std::forward<OutputArg>(output));
    }

    template <
      typename InputArg,
      typename OutputArg,
      typename D = Derived,
      typename = hd_std::enable_if_t<
      []() {
        if constexpr (requires { D::TypeConstraint == AlgTypeConstraint::StrictOrdering; }) {
          return D::Input::template accepts_when_paired<InputArg, OutputArg, typename D::Output>;
        } else {
          return D::Input::template accepts<InputArg> && D::Output::template accepts<OutputArg>;
        }
      }()
      >
    >
    void process(InputArg&& input, OutputArg&& output) {
      static_assert(Derived::Input::template accepts<InputArg>,
                    "Algorithm Error: Input type not supported by Derived::Input list");
      static_assert(Derived::Output::template accepts<OutputArg>,
                    "Algorithm Error: Output type not supported by Derived::Output list");

      if constexpr (requires {
          static_cast<Derived*>(this)->process_impl(hd_std::forward<InputArg>(input),
                                                    hd_std::forward<OutputArg>(output));
        }) {
        static_cast<Derived*>(this)->process_impl(hd_std::forward<InputArg>(input),
                                                  hd_std::forward<OutputArg>(output));
      } else {
        static_cast<const Derived*>(this)->process_impl(hd_std::forward<InputArg>(input),
                                                        hd_std::forward<OutputArg>(output));
      }
    }

    template <typename InputArg, typename OutputArg>
    void operator()(InputArg&& input, OutputArg&& output) {
      process(hd_std::forward<InputArg>(input), hd_std::forward<OutputArg>(output));
    }

    template <typename InputArg, typename OutputArg>
    void operator()(InputArg&& input, OutputArg&& output) const {
      process(hd_std::forward<InputArg>(input), hd_std::forward<OutputArg>(output));
    }

    template <
      typename InputArg,
      typename OutputArg,
      typename D = Derived,
      typename = hd_std::enable_if_t<
      []() {
        if constexpr (requires { D::TypeConstraint == AlgTypeConstraint::StrictOrdering; }) {
          return D::Input::template accepts_when_paired<InputArg, OutputArg, typename D::Output>;
        } else {
          return D::Input::template accepts<InputArg> && D::Output::template accepts<OutputArg>;
        }
      }()
      >
    >
    void process_many(hd_std::size_t count, InputArg&& input, OutputArg&& output) const {
      static_assert(Derived::Input::template accepts<InputArg>,
                    "Algorithm Error: Input type not supported by Derived::Input list");
      static_assert(Derived::Output::template accepts<OutputArg>,
                    "Algorithm Error: Output type not supported by Derived::Output list");

      static_cast<const Derived*>(this)->process_many_impl(count,
                                                           hd_std::forward<InputArg>(input),
                                                           hd_std::forward<OutputArg>(output));
    }

    template <
      typename InputArg,
      typename OutputArg,
      typename D = Derived,
      typename = hd_std::enable_if_t<
      []() {
        if constexpr (requires { D::TypeConstraint == AlgTypeConstraint::StrictOrdering; }) {
          return D::Input::template accepts_when_paired<InputArg, OutputArg, typename D::Output>;
        } else {
          return D::Input::template accepts<InputArg> && D::Output::template accepts<OutputArg>;
        }
      }()
      >
    >
    void process_many(hd_std::size_t count, InputArg&& input, OutputArg&& output) {
      static_assert(Derived::Input::template accepts<InputArg>,
                    "Algorithm Error: Input type not supported by Derived::Input list");
      static_assert(Derived::Output::template accepts<OutputArg>,
                    "Algorithm Error: Output type not supported by Derived::Output list");

      if constexpr (requires {
          static_cast<Derived*>(this)->process_many_impl(count,
                                                         hd_std::forward<InputArg>(input),
                                                         hd_std::forward<OutputArg>(output));
        }) {
        static_cast<Derived*>(this)->process_many_impl(count,
                                                       hd_std::forward<InputArg>(input),
                                                       hd_std::forward<OutputArg>(output));
      } else {
        static_cast<const Derived*>(this)->process_many_impl(count,
                                                             hd_std::forward<InputArg>(input),
                                                             hd_std::forward<OutputArg>(output));
      }
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
