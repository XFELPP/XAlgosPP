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
// #include "xalgospp/features/impl/pf8.hh"
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

  // using impl::Peak;

  struct Peak {
    float com_fs;
    float com_ss;
    float total_intensity;
    float max_intensity;
    float snr;
    int npix;
    int label;
  };

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
      for (int it = 0; it < m_params.iterations; ++it) {
        std::fill(m_r_offset.begin(), m_r_offset.end(), 0.0f);
        std::fill(m_r_sigma.begin(), m_r_sigma.end(), 0.0f);
        std::fill(m_r_count.begin(), m_r_count.end(), 0);

        for (std::size_t i = 0; i < m_num_pixels; ++i) {
          if (mask_ptr[i]) {
            int curr_r { static_cast<int>(std::round(r_map_ptr[i])) };

            if (curr_r >= 0 && curr_r < m_params.num_radial_bins) {
              float value = data_ptr[i];
              if (value < m_r_threshold[curr_r] && value > m_l_threshold[curr_r]) {
                m_r_offset[curr_r] += value;
                m_r_sigma[curr_r] += value * value;
                m_r_count[curr_r] += 1;
              }
            }
          }
        }

        for (int ri = 0; ri < m_params.num_radial_bins; ++ri) {
          int count { m_r_count[ri] };
          if (count == 0) {
            m_r_offset[ri] = 0.0f;
            m_r_sigma[ri] = 0.001f;
            m_r_threshold[ri] = 1e9f;
            m_l_threshold[ri] = -1e9f;
          } else {
            float mean { m_r_offset[ri] / count };
            float var { (m_r_sigma[ri] / count) - (mean * mean) };
            float sigma { (var > 0.0f) ? std::sqrt(var) : 0.01f };

            m_r_offset[ri] = mean;
            m_r_sigma[ri] = sigma;
            m_r_threshold[ri] = std::max(m_params.ADC_threshold, mean + m_params.min_SNR * sigma);
            m_l_threshold[ri] = mean - m_params.min_SNR * sigma;
          }
        }
      }

      // Run the connected-component search and integration
      int peak_count { 0 };
      int width { m_params.width };
      int height { m_params.height };
      int search_fs[9] = { 0, -1, 0, 1, -1, 1, -1, 0, 1 };
      int search_ss[9] = { 0, -1, -1, -1, 0, 0, 1, 1, 1 };
      for (int ss = 1; ss < height - 1; ++ss) {
        for (int fs = 1; fs < width - 1; ++fs) {
          int pxidx { ss * width + fs };
          int curr_rad { static_cast<int>(std::round(r_map_ptr[pxidx])) };

          if (curr_rad < 0 || curr_rad >= m_params.num_radial_bins) {
            continue;
          }

          float curr_thresh { m_r_threshold[curr_rad] };
          if (data_ptr[pxidx] > curr_thresh && m_pix_in_peak_map[pxidx] == 0 &&
              mask_ptr[pxidx]) {
            m_infs[0] = fs;
            m_inss[0] = ss;
            m_peak_pixels[0] = pxidx;

            int num_pix_in_peak { 1 };
            float sum_com_fs { data_ptr[pxidx] * fs };
            float sum_com_ss { data_ptr[pxidx] * ss };
            float sum_i { data_ptr[pxidx] };

            int lt_num_pix_in_pk;
            do {
              lt_num_pix_in_pk = num_pix_in_peak;
              for (int p = 0; p < lt_num_pix_in_pk; ++p) {
                for (int k = 0; k < 9; ++k) {
                  int n_fs { m_infs[p] + search_fs[k] };
                  int n_ss { m_inss[p] + search_ss[k] };
                  if (n_fs < 0 || n_fs >= width || n_ss < 0 || n_ss >= height) {
                    continue;
                  }

                  int n_idx { n_ss * width + n_fs };
                  int n_rad { static_cast<int>(std::round(r_map_ptr[n_idx])) };

                  if (n_rad < 0 || n_rad >= m_params.num_radial_bins) {
                    continue;
                  }

                  if (data_ptr[n_idx] > m_r_threshold[n_rad] &&
                      m_pix_in_peak_map[n_idx] == 0          &&
                      mask_ptr[n_idx]) {
                    float curr_i { data_ptr[n_idx] - m_r_offset[n_rad] };
                    sum_i += curr_i;
                    sum_com_fs += curr_i * n_fs;
                    sum_com_ss += curr_i * n_ss;
                    m_inss[num_pix_in_peak] = n_ss;
                    m_infs[num_pix_in_peak] = n_fs;
                    m_pix_in_peak_map[n_idx] = 1;

                    if (num_pix_in_peak < m_params.max_pixel_count) {
                      m_peak_pixels[num_pix_in_peak] = n_idx;
                    }
                    num_pix_in_peak++;
                  }
                }
              }
            } while (lt_num_pix_in_pk != num_pix_in_peak);

            if (num_pix_in_peak < m_params.min_pixel_count || num_pix_in_peak > m_params.max_pixel_count) {
              continue;
            }

            if (std::abs(sum_i) < 1e-10) {
              continue;
            }

            float peak_com_fs { sum_com_fs / std::abs(sum_i) };
            float peak_com_ss { sum_com_ss / std::abs(sum_i) };
            int com_idx =
              static_cast<int>(std::round(peak_com_fs)) + static_cast<int>(std::round(peak_com_ss)) * width;

            // Background ring estimation
            float bg_sum { 0 };
            float bg_sum_sq { 0 };
            float bg_max_i { -1e9f };

            int bg_count { 0 };
            int ring_width { 2 * m_params.local_background_radius };

            int c_fs { static_cast<int>(std::round(peak_com_fs)) };
            int c_ss { static_cast<int>(std::round(peak_com_ss)) };

            for (int ssj = -ring_width; ssj <= ring_width; ++ssj) {
              for (int fsi = -ring_width; fsi <= ring_width; ++fsi) {
                int n_fs { c_fs + fsi };
                int n_ss { c_ss + ssj };
                if (n_fs < 0 || n_fs >= width || n_ss < 0 || n_ss >= height) {
                  continue;
                }

                float r_dist { std::sqrt(fsi * fsi + ssj * ssj) };
                if (r_dist > ring_width) {
                  continue;
                }

                int n_idx { n_ss * width + n_fs };
                int n_rad { static_cast<int>(std::round(r_map_ptr[n_idx])) };
                if (n_rad < 0 || n_rad >= m_params.num_radial_bins) {
                  continue;
                }

                if (data_ptr[n_idx] < m_r_threshold[n_rad] &&
                    m_pix_in_peak_map[n_idx] == 0          &&
                    mask_ptr[n_idx]) {
                  bg_sum += data_ptr[n_idx];
                  bg_sum_sq += data_ptr[n_idx] * data_ptr[n_idx];
                  bg_max_i = std::max(bg_max_i, data_ptr[n_idx]);
                  bg_count++;
                }
              }
            }

            float local_offset { 0 };
            float local_sigma { 0.01f };
            if (bg_count > 0) {
              local_offset = bg_sum / bg_count;
              float var { (bg_sum_sq / bg_count) - (local_offset * local_offset) };
              local_sigma = (var >= 0) ? std::sqrt(var) : 0.01f;
            } else {
              int rad { static_cast<int>(std::round(r_map_ptr[com_idx])) };
              if (rad >= 0 && rad < m_params.num_radial_bins) {
                local_offset = m_r_offset[rad];
              }
            }

            // Final intensity integration
            float peak_tot_i { 0 };
            float pk_tot_i_raw { 0 };
            float peak_max_i { 0 };

            sum_com_fs = 0;
            sum_com_ss = 0;
            for (int peak_idx = 0; peak_idx < num_pix_in_peak && peak_idx < m_params.max_pixel_count; ++peak_idx) {
              int curr_idx = m_peak_pixels[peak_idx];
              float curr_i_raw = data_ptr[curr_idx];
              float curr_i = curr_i_raw - local_offset;
              peak_tot_i += curr_i;
              pk_tot_i_raw += curr_i_raw;
              int curr_fs = curr_idx % width;
              int curr_ss = curr_idx / width;
              sum_com_fs += curr_i_raw * curr_fs;
              sum_com_ss += curr_i_raw * curr_ss;
              peak_max_i = std::max(peak_max_i, curr_i);
            }

            if (std::abs(pk_tot_i_raw) < 1e-10) {
              continue;
            }

            peak_com_fs = sum_com_fs / std::abs(pk_tot_i_raw);
            peak_com_ss = sum_com_ss / std::abs(pk_tot_i_raw);

            float peak_snr { 0 };
            if (std::abs(local_sigma) > 1e-10) {
              peak_snr = peak_tot_i / local_sigma;
            }
            if (peak_snr < m_params.min_SNR) {
              continue;
            }
            if (peak_max_i < (bg_max_i - local_offset)) {
              continue;
            }
            if (peak_count < m_params.max_number_peaks) {
              peaks_ptr[peak_count] = {
                peak_com_fs,
                peak_com_ss,
                peak_tot_i,
                peak_max_i,
                peak_snr,
                num_pix_in_peak,
                com_idx
              };
            }
            peak_count++;
          }
        }
      }
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
