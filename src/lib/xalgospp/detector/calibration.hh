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

#ifndef XALGOSPP_DETECTOR_CALIBRATION_HH
#define XALGOSPP_DETECTOR_CALIBRATION_HH

#include "ncarray/custom_types.hh" // Include Float2 vectors
#include "ncarray/dtype.hh"
#include "ncarray/ncarrays.hh"

#include <cstddef>
#include <cstdint>

namespace xalgospp::det {
  // Setup a Float2 vector as our pixel calibration struct
  // PixelCalibStruct {
  //   x == ped (holding pedestal + offset)
  //   y == gain
  // };
  using PixelCalibStruct = ncarray::Float2;

  /**
   * Run-time calibration parameters for detectors with bit-packed gain and data.
   */
  struct CalibParameters {
    enum class MappingMode { Direct, Epix10k };

    int gain_shift = 14;
    int gain_mask = 0x3;
    int data_mask = 0x3FFF;
    int num_gains = 3;
    int invalid_pattern = -1; // -1 means no invalid pattern check
    float invalid_value = std::numeric_limits<float>::quiet_NaN();
    MappingMode mapping = MappingMode::Direct;
  };

  inline constexpr CalibParameters JungfrauCalibParameters {
    .gain_shift = 14,
    .gain_mask = 0x3,
    .data_mask = 0x3FFF,
    .num_gains = 3,
    .invalid_pattern = 2,
    .invalid_value = std::numeric_limits<float>::quiet_NaN(),
    .mapping = CalibParameters::MappingMode::Direct
  };

  inline constexpr CalibParameters Epix10kCalibParameters {
    .gain_shift = 14,
    .gain_mask = 0x3,
    .data_mask = 0x3FFF,
    .num_gains = 7,
    .invalid_pattern = -1,
    .invalid_value = std::numeric_limits<float>::quiet_NaN(),
    .mapping = CalibParameters::MappingMode::Epix10k
  };

  struct JungfrauPolicy {
    static constexpr int GAIN_SHIFT { 14 };
    static constexpr int GAIN_MASK { 0x3 };
    static constexpr int DATA_MASK { 0x3FFF };
    static constexpr int NUM_GAINS { 3 };
    static constexpr int INVALID_PATTERN { 2 };
  };

  struct Epix10kPolicy {
    static constexpr int GAIN_SHIFT { 14 };
    static constexpr int GAIN_MASK { 0x3 };
    static constexpr int DATA_MASK { 0x3FFF };
    static constexpr int NUM_GAINS { 7 };
    static constexpr int INVALID_PATTERN { -1 };
  };

  template <typename U>
  struct CalibFunctor {
    CalibFunctor(const CalibParameters& p_,
                 const std::span<PixelCalibStruct>& cc,
                 std::size_t seg_off)
      : p(p_)
      , calib_const(cc)
      , seg_offset(seg_off)
    {
      NPIX = cc.size() / p.num_gains;
    }

    inline U operator()(int raw, int i) const {
      int g_bits { (raw >> p.gain_shift) & p.gain_mask };
      if (g_bits == p.invalid_pattern) {
        return p.invalid_value;
      }

      int data { raw & p.data_mask };
      int g_idx { 0 };
      if (p.mapping == CalibParameters::MappingMode::Epix10k) {
        g_idx = g_bits;
      } else {
        if (g_bits == 0) {
          g_idx = 0;
        } else if (g_bits == 1) {
          g_idx = 1;
        } else if (g_bits == 3 || g_bits == 2) {
          g_idx = 2;
        }
      }

      // Using a vector datatype for the constants, c.x is pedestal, c.y is gain
      const auto& c { calib_const[g_idx * NPIX + seg_offset + i] };
      return (static_cast<U>(data) - c.x) * c.y;
    }

    const CalibParameters& p;
    // const ncarray::NCArrayView calib_const; // DType::vfloat2
    const std::span<PixelCalibStruct>& calib_const;
    std::size_t NPIX;
    std::size_t seg_offset;
  };

  template <typename Policy, typename U>
  struct PolicyCalibFunctor {
    PolicyCalibFunctor(const std::span<PixelCalibStruct>& cc, std::size_t seg_off)
      : calib_const(cc)
      , seg_offset(seg_off)
    {
      NPIX = cc.size() / Policy::NUM_GAINS;
    }

    inline U operator()(int raw, int i) const {
      int g_bits { (raw >> Policy::GAIN_SHIFT) & Policy::GAIN_MASK };
      if constexpr (Policy::INVALID_PATTERN != -1) {
        if (g_bits == Policy::INVALID_PATTERN) {
          return std::numeric_limits<U>::quiet_NaN();
        }
      }

      int data { raw & Policy::DATA_MASK };
      int g_idx { 0 };
      if (g_bits == 0) {
        g_idx = 0;
      } else if (g_bits == 1) {
        g_idx = 1;
      } else if (g_bits == 3 || g_bits == 2) {
        g_idx = 2;
      }

      // Using a vector datatype for the constants, c.x is pedestal, c.y is gain
      const auto& c { calib_const[g_idx * NPIX + seg_offset + i] };
      return (static_cast<U>(data) - c.x) * c.y;
    }

    // const ncarray::NCArrayView calib_const; // DType::vfloat2
    const std::span<PixelCalibStruct>& calib_const;
    std::size_t NPIX;
    std::size_t seg_offset;
  };

  /**
   * Calibrate raw data using run-time parameters.
   */
  template <typename Derived, typename U = float>
  auto calibrate(const Eigen::ArrayBase<Derived>& raw,
                 const CalibParameters& p,
                 const std::span<PixelCalibStruct>& calib_const,
                 std::size_t seg_offset) {
    return
      raw.template cast<int>().binaryExpr(Eigen::ArrayXi::LinSpaced(raw.size(), 0, raw.size() - 1),
                                          CalibFunctor<U>(p, calib_const, seg_offset));
  }

  /**
   * Calibrate raw data using compile-time policy.
   */
  template <typename Policy, typename Derived, typename U = float>
  auto calibrate(const Eigen::ArrayBase<Derived>& raw,
                 const std::span<PixelCalibStruct>& calib_const,
                 std::size_t seg_offset) {
    return
      raw.template cast<int>().binaryExpr(Eigen::ArrayXi::LinSpaced(raw.size(), 0, raw.size() - 1),
                                          PolicyCalibFunctor<Policy, U>(calib_const, seg_offset));
  }
} // namespace xalgospp::det

#endif // XALGOSPP_DETECTOR_CALIBRATION_HH
