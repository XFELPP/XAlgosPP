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

#include "xalgospp/algorithm.hh"
#include "xalgospp/detector/lcls2/calibdb.hh"
#include "xalgospp/parameters.hh"
#include "xalgospp/utilities/interconversion.hh"

#include "ncarray/custom_types.hh" // Include Float2 vectors
#include "ncarray/dtype.hh"
#include "ncarray/ncarrays.hh"

#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/spdlog.h"
#include <Eigen/Dense>

#include <cstddef>
#include <cstdint>
#include <set>
#include <span>

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

  /**
   * Specific compile-time policy with how to calibrate an image from a Jungfrau.
   */
  struct JungfrauPolicy {
    static constexpr int GAIN_SHIFT { 14 };
    static constexpr int GAIN_MASK { 0x3 };
    static constexpr int DATA_MASK { 0x3FFF };
    static constexpr int NUM_GAINS { 3 };
    static constexpr int INVALID_PATTERN { 2 };
  };

  /**
   * Specific compile-time policy with how to calibrate an image from an Epix10k.
   */
  struct Epix10kPolicy {
    static constexpr int GAIN_SHIFT { 14 };
    static constexpr int GAIN_MASK { 0x3 };
    static constexpr int DATA_MASK { 0x3FFF };
    static constexpr int NUM_GAINS { 7 };
    static constexpr int INVALID_PATTERN { -1 };
  };

  /**
   * Tag to indicate that runtime calibration must be used.
   */
  struct RuntimeCalibPolicy {};

  template <typename U>
  struct CalibFunctor {
    CalibFunctor(const CalibParameters& p_,
                 const std::span<const PixelCalibStruct>& cc,
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
    const std::span<const PixelCalibStruct>& calib_const;
    std::size_t NPIX;
    std::size_t seg_offset;
  };

  template <typename Policy, typename U>
  struct PolicyCalibFunctor {
    PolicyCalibFunctor(const std::span<const PixelCalibStruct>& cc, std::size_t seg_off)
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
    const std::span<const PixelCalibStruct>& calib_const;
    std::size_t NPIX;
    std::size_t seg_offset;
  };

  /**
   * Calibrate raw data using run-time parameters.
   */
  template <typename Derived, typename U = float>
  auto calibrate(const Eigen::ArrayBase<Derived>& raw,
                 const CalibParameters& p,
                 const std::span<const PixelCalibStruct>& calib_const,
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
                 const std::span<const PixelCalibStruct>& calib_const,
                 std::size_t seg_offset) {
    return
      raw.template cast<int>().binaryExpr(Eigen::ArrayXi::LinSpaced(raw.size(), 0, raw.size() - 1),
                                          PolicyCalibFunctor<Policy, U>(calib_const, seg_offset));
  }

  /**
   * The Calibration algorithm will calibrate an input array image.
   *
   * This Algorithm implements the optional stage step. Currently, this is only
   * supported for LCLS2-based workflows. In that case, the stage step will fetch
   * constants from the LCLS2 CalibDB based on the provided serial number for the
   * detector of interest.
   *
   * For actual data calibration, the Algorithm accepts either a compile-time
   * policy, or runtime set of parameters for learning *how* to calibrate the image.
   * By default, it fallsback on the runtime policy for generic use.
   *
   * @tparam Policy The compile-time calibration policy - or RuntimeCalibPolicy.
   */
  template <typename Policy = RuntimeCalibPolicy>
  class Calibration : public Algorithm<Calibration<Policy>> {
  public:
    using Input = type_list<ncarray::NCArrayView>;  // Raw detector frames
    using Output = type_list<ncarray::NCArrayView>; // Calibrated frames

    struct Params : public Parameters<Params> {
      // These are LCLS2-specific metadata parameters for fetching constants.
      std::string base_url;
      //const char* base_url;
      std::string det_type;
      //const char* det_type;
      std::string det_serial_no;
      //const char* det_serial_no;
      std::string experiment;
      //const char* experiment;
      unsigned run = 0;

      // The following parameters are used for run-time-learned calibration,
      // as opposed to when using the compile-time policies
      int gain_shift = 14;
      int gain_mask = 0x3;
      int data_mask = 0x3FFF;
      int num_gains = 3;
      float default_gain = 1.0f;
      int invalid_pattern = -1;
      float invalid_value = std::numeric_limits<float>::quiet_NaN();
      CalibParameters::MappingMode mapping = CalibParameters::MappingMode::Direct;
      static constexpr auto get_metadata() {
        return std::make_tuple(
          make_param_desc("base_url", &Params::base_url),
          make_param_desc("det_type", &Params::det_type),
          make_param_desc("det_serial_no", &Params::det_serial_no),
          make_param_desc("experiment", &Params::experiment),
          make_param_desc("run", &Params::run),
          make_param_desc("gain_shift", &Params::gain_shift),
          make_param_desc("gain_mask", &Params::gain_mask),
          make_param_desc("data_mask", &Params::data_mask),
          make_param_desc("num_gains", &Params::num_gains),
          make_param_desc("default_gain", &Params::default_gain),
          make_param_desc("invalid_pattern", &Params::invalid_pattern),
          make_param_desc("invalid_value", &Params::invalid_value)
        );
      }
    };

    Calibration() = default;
    Calibration(const Params& params)
      : m_params(params)
    {}

    void print_configuration() {
      print_parameters(m_params);
    }

    void configure_impl(const Params& params) { m_params = params; }

    /**
     * For staging, the Calibration algorithm fetchs constants.
     */
    void stage_impl() {
#ifndef __CUDA_ARCH__
      // NOTE: Eventually want to have other constants fetchers.
      get_lcls2_calibdb_constants();
#else
      printf("[XAlgosPP::Calibrator][Constants Fetch] Constants fetching only supported on host!\n");
#endif
    }

    /**
     * The processing step calibrates an input image using the provided constants.
     *
     * Currently, either compile-time or run-time based dispatch mechanisms are
     * available; however, only the host-side routines are currently exposed.
     *
     * @param[in] input The uncalibrated raw data.
     * @param[out] output The array to hold the output calibrated data.
     */
    void process_impl(const ncarray::NCArrayView& input, ncarray::NCArrayView& output) const {
      if (m_constants_buf.empty()) {
        throw std::runtime_error("[Calibration] Run-time error: Staging has not been run!");
      }

      // Currently, ony support host-only calibration routines via Eigen.
      auto raw_map { to_eigen_array<std::uint16_t>(input) };
      auto out_map { to_eigen_array<float>(output) };

      std::span<const PixelCalibStruct> consts_span(m_constants_buf.data(),
                                                    m_constants_buf.size());
      if constexpr (std::is_same_v<Policy, RuntimeCalibPolicy>) {
        // Construct runtime calibration parameters from configuration
        CalibParameters cp;
        cp.gain_shift = m_params.gain_shift;
        cp.gain_mask = m_params.gain_mask;
        cp.data_mask = m_params.data_mask;
        cp.num_gains = m_params.num_gains;
        cp.invalid_pattern = m_params.invalid_pattern;
        cp.invalid_value = m_params.invalid_value;
        cp.mapping = m_params.mapping;
        out_map = calibrate(raw_map, cp, consts_span, 0);
      } else {
        // Use the compile-time policy calibrator
        out_map = calibrate<Policy, decltype(raw_map), float>(raw_map, consts_span, 0);
      }
    }

    const char* name_impl() const { return "Calibration"; }

  private:
    /**
     * Fetch calibration constants from the LCLS2 CalibDB.
     *
     * This function is used during staging.
     */
    void get_lcls2_calibdb_constants() {
#ifndef __CUDA_ARCH__
      auto logger = spdlog::get("XAlgosPP::Calibrator");
      if (!logger) {
        logger = spdlog::stdout_color_mt("XAlgosPP::Calibrator");
      }
      logger->info("[Calibration] Staging: Fetching constants for detector type '{}' (run={})",
                   m_params.det_type,
                   m_params.run);

      std::string det_short_name = lcls2::get_detector_short_name(m_params.base_url,
                                                                  m_params.det_type,
                                                                  m_params.det_serial_no);

      if (det_short_name.empty()) {
        throw std::runtime_error("[Calibration] Failed to resolve short name for: " + m_params.det_type);
      }

      // Fetch "pedestals" and "gain" constants
      std::set<std::string> ctypes { "pedestals", "pixel_gain", "pixel_offset" };
      auto consts_map = lcls2::retrieve_calib_constants_of_type(m_params.base_url,
                                                                det_short_name,
                                                                m_params.experiment,
                                                                m_params.run,
                                                                ctypes);
      if (consts_map.count("pedestals")) {
        auto& pedestals { consts_map["pedestals"] };

        // Will combine pedestals + offsets and gain into Float2 structs
        // CalibrationConstants.data is a byte buffer -> need to adjust by dtype.
        ncarray::DType peds_dtype = string_to_dtype(pedestals.dtype);

        std::size_t nelem { pedestals.metadata.consts_nelem };
        m_constants_buf.resize(nelem);

        bool have_offsets { consts_map.count("pixel_offset") ? true : false };
        bool have_gains { consts_map.count("pixel_gain") ? true : false };

        if (have_offsets) {
          auto& offsets { consts_map["pixel_offset"] };
          ncarray::DType offsets_dtype = string_to_dtype(offsets.dtype);

          if (have_gains) {
            auto& gains { consts_map["pixel_gain"] };
            ncarray::DType gains_dtype = string_to_dtype(gains.dtype);

            auto cast_peds = [&] <typename PedT> () {
              auto cast_offsets = [&] <typename OffT> () {
                auto cast_gains = [&] <typename GainT> () {
                  const PedT* ped_ptr { reinterpret_cast<const PedT*>(pedestals.data.data()) };
                  const OffT* off_ptr { reinterpret_cast<const OffT*>(offsets.data.data()) };
                  const GainT* gain_ptr { reinterpret_cast<const GainT*>(gains.data.data()) };

                  for (std::size_t i = 0; i < nelem; ++i) {
                    m_constants_buf[i].x =
                      ncarray::op_traits<PedT>::template cast<float>(ped_ptr[i]) +
                      ncarray::op_traits<OffT>::template cast<float>(off_ptr[i]);

                    m_constants_buf[i].y =
                      ncarray::op_traits<GainT>::template cast<float>(gain_ptr[i]);
                  }
                };

                dispatch(gains_dtype, cast_gains);
              };

              dispatch(offsets_dtype, cast_offsets);
            };

            dispatch(peds_dtype, cast_peds);
          } else {
            auto cast_peds = [&] <typename PedT> () {
              auto cast_offsets = [&] <typename OffT> () {
                const PedT* ped_ptr { reinterpret_cast<const PedT*>(pedestals.data.data()) };
                const OffT* off_ptr { reinterpret_cast<const OffT*>(offsets.data.data()) };

                for (std::size_t i = 0; i < nelem; ++i) {
                  m_constants_buf[i].x =
                    ncarray::op_traits<PedT>::template cast<float>(ped_ptr[i]) +
                    ncarray::op_traits<OffT>::template cast<float>(off_ptr[i]);

                  m_constants_buf[i].y = m_params.default_gain;
                }
              };

              dispatch(offsets_dtype, cast_offsets);
            };

            dispatch(peds_dtype, cast_peds);
          }
        } else if (have_gains) {
          auto& gains { consts_map["pixel_gain"] };
          ncarray::DType gains_dtype = string_to_dtype(gains.dtype);

          auto cast_peds = [&]<typename PedT>() {
            auto cast_gains = [&]<typename GainT>() {
              const PedT* ped_ptr { reinterpret_cast<const PedT*>(pedestals.data.data()) };
              const GainT* gain_ptr { reinterpret_cast<const GainT*>(gains.data.data()) };

              for (std::size_t i = 0; i < nelem; ++i) {
                m_constants_buf[i].x =
                  ncarray::op_traits<PedT>::template cast<float>(ped_ptr[i]);
                m_constants_buf[i].y =
                  ncarray::op_traits<GainT>::template cast<float>(gain_ptr[i]);
              }
            };

            dispatch(gains_dtype, cast_gains);
          };

          dispatch(peds_dtype, cast_peds);
        }

        logger->info("[CalibDB] Staging complete: Loaded {} calibration elements.", nelem);
      } else {
        logger->warn("[CalibDB] No pedestals were retrieved! Calibration will be a NoOp!");
      }
#else
      printf("[XAlgosPP::Calibrator][CalibDB] CalibDB interface only supported on host!\n");
#endif
    }

  private:
    Params m_params;
    std::vector<PixelCalibStruct> m_constants_buf;
  };
} // namespace xalgospp::det

#endif // XALGOSPP_DETECTOR_CALIBRATION_HH
