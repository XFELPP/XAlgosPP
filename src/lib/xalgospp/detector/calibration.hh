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

#include <ncarray/custom_types.hh> // Include Float2 vectors
#include <ncarray/dtype.hh>
#ifdef XALG_HAS_CUDA
#include <ncarray/expression/stencil.hh>
#endif
#include <ncarray/ncarrays.hh>

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
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
    const std::span<const PixelCalibStruct> calib_const;
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
    const std::span<const PixelCalibStruct> calib_const;
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
  template <typename Policy = RuntimeCalibPolicy, typename MemTag = ncarray::HostTag>
  class Calibration : public Algorithm<Calibration<Policy, MemTag>, MemTag> {
  public:
    using Input = type_list<ncarray::NCViewFor<MemTag>>;  // Raw detector frames
    using Output = type_list<ncarray::NCViewFor<MemTag>>; // Calibrated frames

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

      // During the staging, we may use heuristics to override parameters to make
      // life easier for people. If used in sbio, however, different parallel units
      // may not see these. So, we pack both the parameters and the constants into
      // the serialized staged data.
      constexpr std::size_t align { alignof(PixelCalibStruct) };
      std::size_t header_size { sizeof(CalibParameters) };
      std::size_t constants_offset { (header_size + align - 1) & ~(align - 1) };
      std::size_t nelem { m_constants.size() };

      const std::uint8_t* params_ptr { reinterpret_cast<const std::uint8_t*>(&m_params) };
      // NOTE: m_constants is now invalid since we shifted its backing buffer
      m_serialized_data.insert(m_serialized_data.begin(),
                               params_ptr,
                               params_ptr + sizeof(CalibParameters));
      m_serialized_data.insert(m_serialized_data.begin() + sizeof(CalibParameters),
                               constants_offset - sizeof(CalibParameters),
                               0);

      auto* new_ptr { reinterpret_cast<PixelCalibStruct*>(m_serialized_data.data() + constants_offset) };
      m_constants = std::span<PixelCalibStruct>(new_ptr, nelem);

      if constexpr (std::is_same_v<MemTag, ncarray::DevTag>) {
#ifdef XALG_HAS_CUDA
        constexpr ssize_t ndim { 1 };
        ssize_t shape[ndim] { nelem };
        ssize_t strides[ndim] { ncarray::itemsize(ncarray::DType::vfloat2) };
        m_dev_constants = ncarray::NCOwnerFor<ncarray::DevTag>(1, shape, ncarray::DType::vfloat2);

        ncarray::NCViewFor<ncarray::HostTag> consts_view(m_constants.data(),
                                                         ndim,
                                                         shape,
                                                         strides,
                                                         ncarray::DType::vfloat2,
                                                         -1,
                                                         true);
        m_dev_constants.assign(consts_view);
#else
        throw std::runtime_error("[Calibration] DevTag requested but CUDA is not enabled!");
#endif
      }
#else
      printf("[Calibration] Constants fetching only supported on host!\n");
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
    void process_impl(const ncarray::NCViewFor<MemTag>& input, ncarray::NCViewFor<MemTag>& output) const {
      if constexpr (std::is_same_v<MemTag, ncarray::HostTag>) {
        if (m_constants.empty()) {
          throw std::runtime_error("[Calibration] Run-time error: Staging has not been run!");
        }

        // Currently, ony support host-only calibration routines via Eigen.
        ssize_t num_segments { input.shape()[0] };

        // Iterate segments in case of multi-panel detectors with pointer axes
        for (ssize_t i = 0; i < num_segments; ++i) {
          auto raw_map { to_eigen_array<std::uint16_t>(input(i)) };
          auto out_map { to_eigen_array<float>(output(i)) };

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
            out_map = calibrate(raw_map, cp, m_constants, i * raw_map.size());
          } else {
            // Use the compile-time policy calibrator
            out_map = calibrate<Policy, decltype(raw_map), float>(raw_map,
                                                                  m_constants,
                                                                  i * raw_map.size());
          }
        }
      } else {
#ifdef XALG_HAS_CUDA
        if (m_dev_constants.size() == 0) {
          throw std::runtime_error("[Calibration] Run-time error: Device constants have not been staged!");
        }

        if (!m_stencil.has_value()) {
          build_stencil(input);
        }

        std::size_t total_elements { input.size() };
        auto shape { input.shape() };
        std::size_t ndim { input.ndim() };
        std::size_t NPIX { total_elements };

        if (ndim >= 3) {
          NPIX = 1;
          for (std::size_t d = 1; d < ndim; ++d) {
            NPIX *= shape[d];
          }
        }

        int mapping_val { (m_params.mapping == CalibParameters::MappingMode::Epix10k) ? 1 : 0 };

        m_stencil->apply(input,
                         output,
                         std::nullopt,
                         m_dev_constants.data(),
                         m_params.gain_shift,
                         m_params.gain_mask,
                         m_params.data_mask,
                         m_params.invalid_pattern,
                         m_params.invalid_value,
                         mapping_val,
                         static_cast<int>(NPIX));
#else
        throw std::runtime_error("[Calibration] GPU execution requested but CUDA support is disabled!");
#endif
      }
    }

    const char* name_impl() const { return "Calibration"; }

    /**
     * Retrieve a pointer to the calibration constants.
     *
     * @note This uses the *span* as the source of truth for constants, as different
     *       instances of the Algorithm may or may not own the data.
     * @note Depending on when this function is called, the constants may or may
     *       not have been populated yet! Make sure to consider staging, and any
     *       other lifecycle considerations before using the pointer!
     *
     * @returns A pointer to the constants buffer.
     */
    const std::uint8_t* get_staged_data_impl() const {
      return m_serialized_data.data();
    }

    /**
     * Provide access from a buffer from elsewhere containing the calibration constants.
     *
     * @param[in] buf A buffer containing calibration constants.
     * @param[in] nbytes The size of the buffer in bytes.
     */
    void set_staged_data_impl(std::uint8_t* buf, std::size_t nbytes) {
      // During the staging, we may use heuristics to override parameters to make
      // life easier for people. If used in sbio, however, different parallel units
      // may not see these. So, we pack both the parameters and the constants into
      // the serialized staged data.
      constexpr std::size_t align { alignof(PixelCalibStruct) };
      std::size_t header_size { sizeof(CalibParameters) };
      std::size_t constants_offset { (header_size + align - 1) & ~(align - 1) };

      // First copy any parameter updates
      std::memcpy(&m_params, buf, sizeof(CalibParameters));

      // Now build a span over the remaining data
      std::size_t nelem { (nbytes - constants_offset) / sizeof(PixelCalibStruct) };
      m_constants = std::span<PixelCalibStruct>(reinterpret_cast<PixelCalibStruct*>(buf + constants_offset),
                                                nelem);

      if constexpr (std::is_same_v<MemTag, ncarray::DevTag>) {
#ifdef XALG_HAS_CUDA
        constexpr ssize_t ndim { 1 };
        ssize_t shape[ndim] { nelem };
        ssize_t strides[ndim] { ncarray::itemsize(ncarray::DType::vfloat2) };
        m_dev_constants = ncarray::NCOwnerFor<ncarray::DevTag>(1,
                                                               shape,
                                                               ncarray::DType::vfloat2);

        ncarray::NCViewFor<ncarray::HostTag> consts_view(m_constants.data(),
                                                         ndim,
                                                         shape,
                                                         strides,
                                                         ncarray::DType::vfloat2,
                                                         -1,
                                                         true);
        m_dev_constants.assign(consts_view);
#else
        throw std::runtime_error("[Calibration] DevTag requested but CUDA is not enabled!");
#endif
      }
    }

    /**
     * The total size in bytes of the calibration constants and any serialized params.
     *
     * @note For facilitating the sharing of any overloads for parameters, they may
     *       be serialized alongside the constants themselves in one buffer.
     *
     * @return The total size in bytes of the calibration constants.
     */
    std::size_t staged_data_size_impl() const {
      return m_serialized_data.size();
    }

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

        // We'll use some heuristics for the RuntimeCalib case if people made
        // a mistake
        if constexpr (std::is_same_v<Policy, RuntimeCalibPolicy>) {
          // If the constants only have 2 dimensions (i.e. match image dims)
          // then set the number of gains to 1
          if (m_params.num_gains > 2 && pedestals.metadata.consts_ndim == 2) {
            logger->debug("[Calibration] Overriding the number of gains. We think its 1.");
            m_params.num_gains = 1;
            // That also means there are no fancy masking things to do.
            m_params.gain_mask = 0;
            m_params.gain_shift = 0;
            // And the data mask is all 16 bits
            m_params.data_mask = 0xFFFF;
          }
        }

        // Will combine pedestals + offsets and gain into Float2 structs
        // CalibrationConstants.data is a byte buffer -> need to adjust by dtype.
        ncarray::DType peds_dtype = string_to_dtype(pedestals.dtype);

        std::size_t nelem { pedestals.metadata.consts_nelem };
        // The buffer is a byte buffer, it will hold the constants and the calibration
        // parameters in serialized fashion, should they need to be shared
        m_serialized_data.resize(nelem * sizeof(PixelCalibStruct));

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

                  auto* consts_ptr { reinterpret_cast<PixelCalibStruct*>(m_serialized_data.data()) };
                  for (std::size_t i = 0; i < nelem; ++i) {
                    consts_ptr[i].x =
                      ncarray::op_traits<PedT>::template cast<float>(ped_ptr[i]) +
                      ncarray::op_traits<OffT>::template cast<float>(off_ptr[i]);

                    // Gains stored as ADU/keV - invert so its keV/ADU and can
                    // just multiply in hot loop
                    consts_ptr[i].y =
                      1.0f / ncarray::op_traits<GainT>::template cast<float>(gain_ptr[i]);
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

                auto* consts_ptr { reinterpret_cast<PixelCalibStruct*>(m_serialized_data.data()) };
                for (std::size_t i = 0; i < nelem; ++i) {
                  consts_ptr[i].x =
                    ncarray::op_traits<PedT>::template cast<float>(ped_ptr[i]) +
                    ncarray::op_traits<OffT>::template cast<float>(off_ptr[i]);

                  consts_ptr[i].y = m_params.default_gain;
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

              auto* consts_ptr { reinterpret_cast<PixelCalibStruct*>(m_serialized_data.data()) };
              for (std::size_t i = 0; i < nelem; ++i) {
                consts_ptr[i].x =
                  ncarray::op_traits<PedT>::template cast<float>(ped_ptr[i]);

                // Gains stored as ADU/keV - invert so its keV/ADU and can
                // just multiply in hot loop
                consts_ptr[i].y =
                  1.0f / ncarray::op_traits<GainT>::template cast<float>(gain_ptr[i]);
              }
            };

            dispatch(gains_dtype, cast_gains);
          };

          dispatch(peds_dtype, cast_peds);
        }

        logger->info("[CalibDB] Staging complete: Loaded {} calibration elements.", nelem);

        // Create a span over the buffered data
        auto* consts_ptr { reinterpret_cast<PixelCalibStruct*>(m_serialized_data.data()) };
        m_constants = std::span<PixelCalibStruct>(consts_ptr, nelem);
      } else {
        logger->warn("[CalibDB] No pedestals were retrieved! Calibration will be a NoOp!");
      }
#else
      printf("[XAlgosPP::Calibrator][CalibDB] CalibDB interface only supported on host!\n");
#endif
    }

    void build_stencil(const ncarray::NCViewFor<MemTag>& input) const {
#ifdef XALG_HAS_CUDA
      std::vector<ncarray::StaticCoords<3>> offsets = { {0, 0, 0} };

      std::vector<std::uint8_t> is_pointer_axis(3, 0);
      for (int dim = 0; dim < 3; dim++) {
        is_pointer_axis[dim] = input.is_pointer_axis(dim) ? 1 : 0;
      }

      ncarray::device::StencilJITExtensions ext;
      ext.extra_params =
        ", const float2* calib_const"
        ", int gain_shift"
        ", int gain_mask"
        ", int data_mask"
        ", int invalid_pattern"
        ", float invalid_value"
        ", int mapping"
        ", int npix";

      ext.epilogue_code =
        "{\n"
        "  const uint16_t* raw_data = reinterpret_cast<const uint16_t*>(src_data);\n"
        "  uint16_t raw = raw_data[b_idx];\n"
        "  int g_bits = (raw >> gain_shift) & gain_mask;\n"
        "  if (invalid_pattern != -1 && g_bits == invalid_pattern) {\n"
        "    reinterpret_cast<float*>(dest_data)[b_idx] = invalid_value;\n"
        "  } else {\n"
        "    int data_val = raw & data_mask;\n"
        "    int g_idx = 0;\n"
        "    if (mapping == 1) {\n"
        "      g_idx = g_bits;\n"
        "    } else {\n"
        "      if (g_bits == 0) g_idx = 0;\n"
        "      else if (g_bits == 1) g_idx = 1;\n"
        "      else if (g_bits == 2 || g_bits == 3) g_idx = 2;\n"
        "    }\n"
        "    int local_idx = b_idx % npix;\n"
        "    int seg_idx = b_idx / npix;\n"
        "    int seg_offset = seg_idx * npix;\n"
        "    float2 c = calib_const[g_idx * npix + seg_offset + local_idx];\n"
        "    reinterpret_cast<float*>(dest_data)[b_idx] = (static_cast<float>(data_val) - c.x) * c.y;\n"
        "  }\n"
        "}\n";

      auto calib_expr = [](auto views) {
        // Return dummy value to let JIT compiler compile normally
        return views[0] * 0.0f;
      };

      m_stencil = ncarray::Stencil<3>::create<std::uint16_t>(offsets,
                                                             is_pointer_axis,
                                                             calib_expr,
                                                             ext,
                                                             /*is_soarr=*/false);
#endif
    }

  private:
    Params m_params;
    /**
     * The calibration constants used for processing. This span may be constructed
     * over a non-owned buffer in some cases.
     */
    std::span<PixelCalibStruct> m_constants;
    /**
     * A raw bytes buffer containing the calibration constants and parameters.
     * The parameters are also serialized into the buffer to facilitate data sharing
     * as needed. This may be empty if this particular instance of hte Algorithm does
     * not actually own the data.
     * The span above (m_constants) is what is always used for processing, and it
     * may be constructed over a non-owned buffer, in some cases.
     */
    std::vector<std::uint8_t> m_serialized_data;

#ifdef XALG_HAS_CUDA
    ncarray::NCOwnerFor<ncarray::DevTag> m_dev_constants;

    mutable std::optional<ncarray::Stencil<3>> m_stencil;
#endif
  };
} // namespace xalgospp::det

#endif // XALGOSPP_DETECTOR_CALIBRATION_HH
