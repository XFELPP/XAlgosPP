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

#ifndef XALGOSPP_FEATURES_PEAKFINDER8_HH
#define XALGOSPP_FEATURES_PEAKFINDER8_HH

#include "xalgospp/algorithm.hh"
#include "xalgospp/features/impl/pf8.hh"
#ifdef XALG_HAS_CUDA
#include "xalgospp/features/impl/pf8.cuh"
#endif
#include "xalgospp/parameters.hh"

#include <ncarray/ncarrays.hh>
#include <ncarray/soarrays.hh>
#ifdef XALG_HAS_CUDA
#include <ncarray/ncdevarrays.cuh>
#include <ncarray/sodevarrays.cuh>
#endif

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
#include <algorithm>
#include <stdexcept>

namespace xalgospp::features {

  using Peak = impl::PF8Peak_v1;

  template <typename MemTag = ncarray::HostTag>
  class Peakfinder8 : public Algorithm<Peakfinder8<MemTag>, MemTag> {
  public:
    using Input = type_list<ncarray::NCViewFor<MemTag>, ncarray::SOViewFor<MemTag>>;
    using Output = type_list<ncarray::NCViewFor<MemTag>, ncarray::SOViewFor<MemTag>>;

    static constexpr AlgTypeConstraint TypeConstraint { AlgTypeConstraint::StrictOrdering };

    struct Params : public Parameters<Params> {
      float ADC_threshold { 10.0f };
      float min_SNR { 7.0f };
      long min_pixel_count { 2 };
      long max_pixel_count { 50 };
      int local_background_radius { 3 };
      int max_number_peaks { 2048 };
      int num_radial_bins { 512 };
      int iterations { 5 };
      int width { 0 };
      int height { 0 };

      // Mask and radial map views/pointers provided in Params or staging
      const float* r_map { nullptr };
      const bool* mask { nullptr };

      static constexpr auto get_metadata() {
        return std::make_tuple(
          make_param_desc("ADC_threshold", &Params::ADC_threshold),
          make_param_desc("min_SNR", &Params::min_SNR),
          make_param_desc("min_pixel_count", &Params::min_pixel_count),
          make_param_desc("max_pixel_count", &Params::max_pixel_count),
          make_param_desc("local_background_radius", &Params::local_background_radius),
          make_param_desc("max_number_peaks", &Params::max_number_peaks),
          make_param_desc("num_radial_bins", &Params::num_radial_bins),
          make_param_desc("iterations", &Params::iterations),
          make_param_desc("width", &Params::width),
          make_param_desc("height", &Params::height)
        );
      }
    };

    Peakfinder8() = default;
    Peakfinder8(const Params& params) { configure_impl(params); }

    void configure_impl(const Params& params) {
      m_params = params;
      m_num_pixels = m_params.width * m_params.height;
      if constexpr (std::is_same_v<MemTag, ncarray::HostTag>) {
        m_pix_in_peak_map.resize(m_num_pixels, 0);
        m_infs.resize(m_num_pixels, 0);
        m_inss.resize(m_num_pixels, 0);
        m_peak_pixels.resize(m_params.max_pixel_count, 0);
        m_rstats = impl::RadialStatistics(m_params.num_radial_bins);
      }
    }

    /**
     * For staging, the Peakfinder8 Algorithm stages the mask and radius map.
     *
     * Currently, the staging requires one parallel unit to provide both mask and
     * radius map via the parameters object. In the future, the staging will have
     * the option to also do some calcualtion based on other input types, e.g.,
     * a geometry.
     *
     * @note The fallback if no mask is provided is to make all pixels valid. The
     *       radius map fallback is to fill with 0s. This just prevents seg faults,
     *       the algorithm CANNOT work without a radius map.
     *
     * @todo Work on providing alternative staging mechanism than just a full input
     *       mask and radius map.
     *
     * After staging, the data from the contiguous memory allocation is split into two
     * spans which are used everywhere else for the mask and radius map.
     */
    void stage_impl() {
      // TODO: Add option for doing rmap calculation instead of providing.

      std::size_t mask_bytes { m_num_pixels * sizeof(bool) };
      std::size_t r_map_bytes { m_num_pixels * sizeof(float) };
      m_serialized_data.resize(mask_bytes + r_map_bytes);

      if (m_params.mask) {
        std::memcpy(m_serialized_data.data(), m_params.mask, mask_bytes);
      } else {
        std::memset(m_serialized_data.data(), 1, mask_bytes);
      }

      if (m_params.r_map) {
        std::memcpy(m_serialized_data.data() + mask_bytes, m_params.r_map, r_map_bytes);
      } else {
        std::memset(m_serialized_data.data() + mask_bytes, 0, r_map_bytes);
      }

      m_staged_mask =
        std::span<const bool>(reinterpret_cast<const bool*>(m_serialized_data.data()),
                              m_num_pixels);
      m_staged_r_map =
        std::span<const float>(reinterpret_cast<const float*>(m_serialized_data.data() + mask_bytes),
                               m_num_pixels);

      if constexpr (std::is_same_v<MemTag, ncarray::DevTag>) {
#ifdef XALG_HAS_CUDA
        m_d_mask_r_map = impl::DevImageData(m_num_pixels);
        m_d_rstats = impl::DevRadialStatistics(m_params.num_radial_bins);
        m_d_peak_data = impl::DevPeakLabelData(m_num_pixels);
#else
        throw std::runtime_error("[Peakfinder8] DevTag requested but CUDA is not enabled!");
#endif
      }
    }

    /**
     * Retrieve a pointer to the serialized mask and radius map.
     *
     * The staged data is organized as: | mask | radius_map |
     * in a single contiguous block.
     *
     * @note Depending on when this function is called, the mask and radial map
     *       may not have been populated yet! Make sure to consider staging, and any
     *       other lifecycle considerations before using the pointer!
     *
     * @returns A pointer to the buffer containing mask and radius map.
     */
    const std::uint8_t* get_staged_data_impl() const {
      return m_serialized_data.data();
    }

    /**
     * Provide access from a buffer from elsewhere containing the calibration constants.
     *
     * @note The staged data is held on the host - if running the GPU variant of the
     *       algorithm, it is copied over to the device memory, but the "canonical"
     *       version remains on the host.
     *
     * @param[in] buf A buffer containing calibration constants.
     * @param[in] nbytes The size of the buffer in bytes.
     */
    void set_staged_data_impl(std::uint8_t* buf, std::size_t nbytes) {
      m_serialized_data.assign(buf, buf + nbytes);
      std::size_t mask_bytes { m_num_pixels * sizeof(bool) };

      m_staged_mask =
        std::span<const bool>(reinterpret_cast<const bool*>(m_serialized_data.data()),
                              m_num_pixels);

      m_staged_r_map =
        std::span<const float>(reinterpret_cast<const float*>(m_serialized_data.data() + mask_bytes),
                               m_num_pixels);

      if constexpr (std::is_same_v<MemTag, ncarray::DevTag>) {
#ifdef XALG_HAS_CUDA
        allocate_gpu_buffers();
        cudaMemcpy(m_d_mask.get(), m_staged_mask.data(), mask_bytes, cudaMemcpyHostToDevice);
        cudaMemcpy(m_d_r_map.get(), m_staged_r_map.data(), nbytes - mask_bytes,
                   cudaMemcpyHostToDevice);
#endif
      }
    }

    /**
     * The total size in bytes of the mask and radius map.
     *
     * @return The total size in bytes of the mask and radius map.
     */
    std::size_t staged_data_size_impl() const { return m_serialized_data.size(); }

    template <typename InputArg, typename OutputArg>
    requires (Input::template accepts_when_paired<InputArg, OutputArg, Output>)
    void process_impl(const InputArg& input, OutputArg& output) const {
      if constexpr (std::is_same_v<MemTag, ncarray::HostTag>) {
        process_cpu(input, output);
      } else {
#ifdef XALG_HAS_CUDA
        process_gpu(input, output);
#else
        throw std::runtime_error("[Peakfinder8] DevTag requested but CUDA is not enabled!");
#endif
      }
    }

    template <typename InputArg, typename OutputArg>
    requires (Input::template accepts_when_paired<InputArg, OutputArg, Output>)
    void process_many_impl(std::size_t count, const InputArg& input, OutputArg& output) {
      for (std::size_t i = 0; i < count; ++i) {
        process(input(i), output(i));
      }
    }

    const char* name_impl() const { return "Peakfinder8"; }

    const Params& params_impl() const { return m_params; }

  private:
    Params m_params;
    std::size_t m_num_pixels { 0 };

    mutable std::vector<std::uint8_t> m_pix_in_peak_map;
    mutable std::vector<int> m_infs;
    mutable std::vector<int> m_inss;
    mutable std::vector<int> m_peak_pixels;

    mutable impl::RadialStatistics m_rstats; ///< Information for radial background stats

    std::vector<std::uint8_t> m_serialized_data;  ///< Serialized mask and radius map
    std::span<const bool> m_staged_mask;          ///< Mask, span over serialized data
    std::span<const float> m_staged_r_map;        ///< Radius map, span over serialized data

#ifdef XALG_HAS_CUDA
    // --- Device only buffers --- //
    impl::DevImageData m_d_mask_r_map;    ///< Simple wrapper for holding mask and radius
    impl::DevRadialStatistics m_d_rstats; ///< Information for radial background stats
    impl::DevPeakLabelData m_d_peak_data; ///< Buffers for label propagation during search
#endif

    template <typename InputArg, typename OutputArg>
    void process_cpu(const InputArg& data, OutputArg& peaks_out) const {
      if ((m_staged_mask.empty() || m_staged_r_map.empty()) &&
          (!m_params.r_map || !m_params.mask)) {
        throw std::runtime_error("[Peakfinder8] r_map or mask parameters not set!");
      }

      std::fill(m_pix_in_peak_map.begin(), m_pix_in_peak_map.end(), 0);

      const float* data_ptr { reinterpret_cast<const float*>(data.data()) };
      const bool* mask_ptr { m_staged_mask.data() };
      const float* r_map_ptr { m_staged_r_map.data() };

      Peak* peaks_ptr { reinterpret_cast<Peak*>(peaks_out.data()) };

      // Compute background statistics radially
      impl::compute_radial_background(data_ptr,
                                      r_map_ptr,
                                      mask_ptr,
                                      m_rstats,
                                      m_num_pixels,
                                      m_params.iterations,
                                      m_params.ADC_threshold,
                                      m_params.min_SNR);

      // Run the connected-component search and integration
      int num_found_peaks { 0 };
      impl::ccl_peak_search(data_ptr,
                            r_map_ptr,
                            mask_ptr,
                            peaks_ptr,
                            num_found_peaks,
                            m_rstats,
                            m_pix_in_peak_map,
                            m_infs,
                            m_inss,
                            m_peak_pixels,
                            m_params.width,
                            m_params.height,
                            m_params.min_pixel_count,
                            m_params.max_pixel_count,
                            m_params.local_background_radius,
                            m_params.max_number_peaks,
                            m_params.min_SNR);
    }

#ifdef XALG_HAS_CUDA
    template <typename InputArg, typename OutputArg>
    void process_gpu(const InputArg& data, OutputArg& peaks_out) const {
      int threads { 256 };
      int blocks { (m_num_pixels + threads - 1) / threads };

      for (int i = 0; i < m_params.iterations; ++i) {
        impl::k_radial_stats_accumulate<<<blocks, threads>>>(static_cast<const float*>(data.data()),
                                                             m_d_mask_r_map.mask,
                                                             m_d_mask_r_map.r_map,
                                                             m_d_rstats.r_threshold,
                                                             m_d_rstats.l_threshold,
                                                             m_d_rstats.sum,
                                                             m_d_rstats.sum_sq,
                                                             m_d_rstats.count,
                                                             m_num_pixels,
                                                             m_d_rstats.num_bins);

        impl::k_radial_stats_finalize<<<(m_params.num_radial_bins + 255)/256, 256>>>(m_d_rstats.offset,
                                                                                     m_d_rstats.sigma,
                                                                                     m_d_rstats.r_threshold,
                                                                                     m_d_rstats.l_threshold,
                                                                                     m_d_rstats.sum,
                                                                                     m_d_rstats.sum_sq,
                                                                                     m_d_rstats.count,
                                                                                     m_params.num_radial_bins,
                                                                                     m_params.min_SNR,
                                                                                     m_params.ADC_threshold);
      }

      impl::k_init_labels<<<blocks, threads>>>(static_cast<const float*>(data.data()),
                                               m_d_mask_r_map.mask,
                                               m_d_rstats.r_threshold,
                                               m_d_mask_r_map.r_map,
                                               m_d_peak_data.labels_v1,
                                               m_num_pixels,
                                               m_params.num_radial_bins);

      // 6 ping-pong iterations --- should capture up to ~50 pixel blobs
      for (int i = 0; i < 3; ++i) {
        impl::k_propagate_labels<<<blocks, threads>>>(m_d_peak_data.labels_v1,
                                                      m_d_peak_data.labels_v2,
                                                      m_params.width,
                                                      m_params.height,
                                                      m_d_peak_data.changed);

        impl::k_propagate_labels<<<blocks, threads>>>(m_d_peak_data.labels_v2,
                                                      m_d_peak_data.labels_v1,
                                                      m_params.width,
                                                      m_params.height,
                                                      m_d_peak_data.changed);
      }

      impl::k_accumulate_peak_props<<<blocks, threads>>>(static_cast<const float*>(data.data()),
                                                         m_d_peak_data.labels_v1,
                                                         m_d_peak_data.label_sum_i,
                                                         m_d_peak_data.label_sum_fs,
                                                         m_d_peak_data.label_sum_ss,
                                                         m_d_peak_data.label_max_i,
                                                         m_d_peak_data.label_npix,
                                                         m_num_pixels,
                                                         m_params.width);

      impl::k_refine_and_filter_peaks<<<blocks, threads>>>(static_cast<const float*>(data.data()),
                                                           m_d_mask_r_map.mask,
                                                           m_d_peak_data.labels_v1,
                                                           m_d_peak_data.label_sum_i,
                                                           m_d_peak_data.label_sum_fs,
                                                           m_d_peak_data.label_sum_ss,
                                                           m_d_peak_data.label_max_i,
                                                           m_d_peak_data.label_npix,
                                                           m_params.width,
                                                           m_params.height,
                                                           m_params.local_background_radius,
                                                           m_params.min_SNR,
                                                           m_params.min_pixel_count,
                                                           m_params.max_pixel_count,
                                                           reinterpret_cast<GPUPeak*>(peaks_out.data()),
                                                           m_d_peak_data.peak_count,
                                                           m_params.max_number_peaks);
      cudaDeviceSynchronize();
    }
#endif
  };
} // namespace xalgospp::features

#endif // XALGOSPP_FEATURES_PEAKFINDER8_HH
