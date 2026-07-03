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

#ifndef XALGOSPP_PARAMETERS_HH
#define XALGOSPP_PARAMETERS_HH

#ifdef __CUDACC__

#include <cuda/std/cstddef>
#include <cuda/std/cstring>
#include <cuda/std/tuple>
#include <cuda/std/type_traits>
#include <cuda/std/utility>

namespace hd_std = cuda::std;

#ifndef XALG_HD
#define XALG_HD __host__ __device__
#endif

#else

#include <cstddef>
#include <cstring>
#include <iostream>
#include <tuple>
#include <type_traits>

namespace hd_std = std;

#ifndef XALG_HD
#define XALG_HD
#endif

#endif // __CUDACC__

namespace xalgospp {
  /**
   * Simple type-list for compile-time listing of inputs/outputs.
   *
   * Versus the `ncarray` type list, this one also provides a size accessor.
   */
  template <typename... Ts>
  struct type_list {
    XALG_HD static constexpr hd_std::size_t size() { return sizeof...(Ts); }
  };

  /**
   * A compound descriptor for a single parameter.
   *
   * To provide a device-friendly Parameters object, the key/value mappings of
   * each individual are coalesced via these descriptor objects, which only
   * pair the name of the parameter, to a pointer to member for retrieval.
   */
  template <typename Class, typename T>
  struct ParameterDesc {
    const char* name; ///< Provided parameter name
    T Class::* ptr;   ///< Pointer to member where the parameter value is stored
  };

  template <typename Class, typename T>
  XALG_HD constexpr ParameterDesc<Class, T> make_param_desc(const char* name, T Class::* ptr) {
    return { name, ptr };
  }

  /**
   * A base Parameters struct with generic setting and getting routines.
   *
   * The Parameters object provides a device-safe key/value mapping with ergonomic
   * lookup and retrieval based on string keys. Algorithms define their specific
   * Parameters with the key/values needed to properly configure the analysis.
   *
   * @tparam Derived CRTP subclass.
   */
  template <typename Derived>
  struct Parameters {
    /**
     * Set a parameter for the Parameters-set by string.
     *
     * @tparam T The type of value of the parameter being set.
     * @param[in] name The name for the parameter.
     * @param[in] val The value for the parameter.
     */
    template <typename T>
    XALG_HD bool set(const char* name, const T& val) {
      auto meta { Derived::get_metadata() };
      constexpr auto size { hd_std::tuple_size_v<decltype(meta)> };

      return set_impl(static_cast<Derived&>(*this),
                      meta,
                      name,
                      val,
                      hd_std::make_index_sequence<size>{});
    }

    /**
     * Retrieve a parameter by name from the Parameters-set.
     *
     * @tparam T The type of the value being retrieved.
     * @param[in] name The name of the parameter to retrieve.
     * @param[in] default_val A default value to return as a fallback.
     * @returns The requested parameter if present, otherwise the default_val.
     */
    template <typename T>
    XALG_HD T get(const char* name, const T& default_val = T{}) const {
      auto meta { Derived::get_metadata() };
      constexpr auto size { hd_std::tuple_size_v<decltype(meta)> };

      T result { default_val };
      get_impl(static_cast<const Derived&>(*this),
               meta,
               name,
               result,
               hd_std::make_index_sequence<size>{});

      return result;
    }

  private:
    template <typename Class, typename Tuple, typename T, hd_std::size_t... Is>
    XALG_HD static bool set_impl(Class& obj,
                                 const Tuple& t,
                                 const char* name,
                                 const T& val,
                                 hd_std::index_sequence<Is...>) {
      bool found { false };

      auto set_one = [&](const char* field_name, auto member_ptr) {
        if (hd_std::strcmp(field_name, name) == 0) {
          using MemberType = hd_std::decay_t<decltype(obj.*member_ptr)>;

          if constexpr (hd_std::is_convertible_v<T, MemberType>) {
            obj.*member_ptr = static_cast<MemberType>(val);
            found = true;
          }
        }
      };

      ( set_one(hd_std::get<Is>(t).name, hd_std::get<Is>(t).ptr), ... );

      return found;
    }

    template <typename Class, typename Tuple, typename T, hd_std::size_t... Is>
    XALG_HD static void get_impl(const Class& obj,
                                 const Tuple& t,
                                 const char* name,
                                 T& out_val,
                                 hd_std::index_sequence<Is...>) {
      auto get_one = [&](const char* field_name, auto member_ptr) {
        if (hd_std::strcmp(field_name, name) == 0) {
          using MemberType = hd_std::decay_t<decltype(obj.*member_ptr)>;

          if constexpr (hd_std::is_convertible_v<MemberType, T>) {
            out_val = static_cast<T>(obj.*member_ptr);
          }
        }
      };

      ( get_one(hd_std::get<Is>(t).name, hd_std::get<Is>(t).ptr), ... );
    }
  };

  /**
   * Recurse a Parameters object and print the name/value pairs.
   *
   * @note This function is NOT compatible with device code.
   *
   * @tparam Params The type of the specific Parameters object to print.
   * @param[in] params The Parameters to print.
   */
  template <typename Params>
  void print_parameters(const Params& params) {
#ifndef __CUDACC__
    auto meta { Params::get_metadata() };

    constexpr auto size = hd_std::tuple_size_v<decltype(meta)>;

    auto print_helper = [&](auto... descs) {
      auto print_one = [&](const char* name, auto member_ptr) {
        hd_std::cout << "  " << name << ": " << params.*member_ptr << "\n";
      };

      ( print_one(descs.name, descs.ptr), ... );
    };

    hd_std::apply(print_helper, meta);
#endif
  }
} // namespace xalgospp

#endif // XALGOSPP_PARAMETERS_HH
