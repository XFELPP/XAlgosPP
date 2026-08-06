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
#include <vector>
#include <algorithm>
#include <stdexcept>

namespace xalgospp::features {

  using Peak = impl::PF8Peak_v1;

  struct Peakfinder8Params : public Parameters<Peakfinder8Params> {
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
        make_param_desc("ADC_threshold", &Peakfinder8Params::ADC_threshold),
        make_param_desc("min_SNR", &Peakfinder8Params::min_SNR),
        make_param_desc("min_pixel_count", &Peakfinder8Params::min_pixel_count),
        make_param_desc("max_pixel_count", &Peakfinder8Params::max_pixel_count),
        make_param_desc("local_background_radius", &Peakfinder8Params::local_background_radius),
        make_param_desc("max_number_peaks", &Peakfinder8Params::max_number_peaks),
        make_param_desc("num_radial_bins", &Peakfinder8Params::num_radial_bins),
        make_param_desc("iterations", &Peakfinder8Params::iterations),
        make_param_desc("width", &Peakfinder8Params::width),
        make_param_desc("height", &Peakfinder8Params::height)
      );
    }
  };

  template <typename MemTag = ncarray::HostTag>
  class Peakfinder8 : public Algorithm<Peakfinder8<MemTag>, MemTag> {
  public:
    using Params = Peakfinder8Params;

    using Input = type_list<ncarray::NCViewFor<MemTag>, ncarray::SOViewFor<MemTag>>;
    using Output = type_list<ncarray::NCViewFor<MemTag>, ncarray::SOViewFor<MemTag>>;

    static constexpr AlgTypeConstraint TypeConstraint { AlgTypeConstraint::StrictOrdering };

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
        m_r_offset.resize(m_params.num_radial_bins, 0.0f);
        m_r_sigma.resize(m_params.num_radial_bins, 0.0f);
        m_r_threshold.resize(m_params.num_radial_bins, 1e9f);
        m_l_threshold.resize(m_params.num_radial_bins, -1e9f);
        m_r_count.resize(m_params.num_radial_bins, 0);
      }
    }

    void stage_impl() {}

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
    mutable std::vector<float> m_r_offset;
    mutable std::vector<float> m_r_sigma;
    mutable std::vector<float> m_r_threshold;
    mutable std::vector<float> m_l_threshold;
    mutable std::vector<int> m_r_count;

    template <typename InputArg, typename OutputArg>
    void process_cpu(const InputArg& data, OutputArg& peaks_out) const {
      if (!m_params.r_map || !m_params.mask) {
        throw std::runtime_error("[Peakfinder8] r_map or mask parameters not set!");
      }

      std::fill(m_pix_in_peak_map.begin(), m_pix_in_peak_map.end(), 0);
      std::fill(m_r_threshold.begin(), m_r_threshold.end(), 1e9f);
      std::fill(m_l_threshold.begin(), m_l_threshold.end(), -1e9f);

      const float* data_ptr { reinterpret_cast<const float*>(data.data()) };
      const bool* mask_ptr { m_params.mask };
      const float* r_map_ptr { m_params.r_map };
      Peak* peaks_ptr { reinterpret_cast<Peak*>(peaks_out.data()) };

      // Compute background statistics radially
      impl::compute_radial_background(data_ptr,
                                      r_map_ptr,
                                      mask_ptr,
                                      m_r_offset,
                                      m_r_sigma,
                                      m_r_count,
                                      m_r_threshold,
                                      m_l_threshold,
                                      m_num_pixels,
                                      m_params.iterations,
                                      m_params.num_radial_bins,
                                      m_params.ADC_threshold,
                                      m_params.min_SNR);

      // Run the connected-component search and integration
      impl::ccl_peak_search(data_ptr,
                            r_map_ptr,
                            mask_ptr,
                            peaks_ptr,
                            m_pix_in_peak_map,
                            m_infs,
                            m_inss,
                            m_peak_pixels,
                            m_r_offset,
                            m_r_threshold,
                            m_params.width,
                            m_params.height,
                            m_params.num_radial_bins,
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
                                                             m_params.mask,
                                                             m_params.r_map,
                                                             m_d_r_threshold.get(),
                                                             m_d_l_threshold.get(),
                                                             m_d_r_sum.get(),
                                                             m_d_r_sum_sq.get(),
                                                             m_d_r_count.get(),
                                                             m_num_pixels,
                                                             m_params.num_radial_bins);

        impl::k_radial_stats_finalize<<<(m_params.num_radial_bins + 255)/256, 256>>>(m_d_r_offset.get(),
                                                                                     m_d_r_sigma.get(),
                                                                                     m_d_r_threshold.get(),
                                                                                     m_d_l_threshold.get(),
                                                                                     m_d_r_sum.get(),
                                                                                     m_d_r_sum_sq.get(),
                                                                                     m_d_r_count.get(),
                                                                                     m_params.num_radial_bins,
                                                                                     m_params.min_SNR,
                                                                                     m_params.ADC_threshold);
      }

      impl::k_init_labels<<<blocks, threads>>>(static_cast<const float*>(data.data()),
                                               m_params.mask,
                                               m_d_r_threshold.get(),
                                               m_params.r_map,
                                               m_d_labels_v1.get(),
                                               m_num_pixels,
                                               m_params.num_radial_bins);

      // 6 ping-pong iterations --- should capture up to ~50 pixel blobs
      for (int i = 0; i < 3; ++i) {
        impl::k_propagate_labels<<<blocks, threads>>>(m_d_labels_v1.get(),
                                                      m_d_labels_v2.get(),
                                                      m_params.width,
                                                      m_params.height,
                                                      m_d_changed.get());

        impl::k_propagate_labels<<<blocks, threads>>>(m_d_labels_v2.get(),
                                                      m_d_labels_v1.get(),
                                                      m_params.width,
                                                      m_params.height,
                                                      m_d_changed.get());
      }

      impl::k_accumulate_peak_props<<<blocks, threads>>>(static_cast<const float*>(data.data()),
                                                         m_d_labels_v1.get(),
                                                         m_d_label_sum_i.get(),
                                                         m_d_label_sum_fs.get(),
                                                         m_d_label_sum_ss.get(),
                                                         m_d_label_max_i.get(),
                                                         m_d_label_npix.get(),
                                                         m_num_pixels,
                                                         m_params.width);

      impl::k_refine_and_filter_peaks<<<blocks, threads>>>(static_cast<const float*>(data.data()),
                                                           m_params.mask,
                                                           m_d_labels_v1.get(),
                                                           m_d_label_sum_i.get(),
                                                           m_d_label_sum_fs.get(),
                                                           m_d_label_sum_ss.get(),
                                                           m_d_label_max_i.get(),
                                                           m_d_label_npix.get(),
                                                           m_params.width,
                                                           m_params.height,
                                                           m_params.local_background_radius,
                                                           m_params.min_SNR,
                                                           m_params.min_pixel_count,
                                                           m_params.max_pixel_count,
                                                           reinterpret_cast<GPUPeak*>(peaks_out.data()),
                                                           m_d_out_peak_count.get(),
                                                           m_params.max_number_peaks);
      cudaDeviceSynchronize();
    }
#endif
  };
} // namespace xalgospp::features

#endif // XALGOSPP_FEATURES_PEAKFINDER8_HH
