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

#include "xalgospp/features/impl/pf8.hh"

#include "xalgospp/utilities/type_lists.hh"

#ifdef XALG_HAS_CUDA
#include <ncarray/ncdevarrays.cuh>
#include <ncarray/sodevarrays.cuh>
#include "xalgospp/features/peakfinder8_gpu.cuh"
#endif

#ifdef __CUDACC_RTC__
typedef long long ssize_t;
#else
#ifdef _WIN32
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#else
#include <sys/types.h>
#endif
#endif

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace xalgospp::features::impl {
  void compute_radial_background(const float* data,
                                 const float* r_map,
                                 const bool* mask,
                                 RadialStatistics& rstats,
                                 std::size_t num_pixels,
                                 int niter,
                                 float ADC_threshold,
                                 float min_SNR) {
    for (int it = 0; it < niter; ++it) {
      std::fill(rstats.offset.begin(), rstats.offset.end(), 0.0f);
      std::fill(rstats.sigma.begin(), rstats.sigma.end(), 0.0f);
      std::fill(rstats.count.begin(), rstats.count.end(), 0);

      for (std::size_t i = 0; i < num_pixels; ++i) {
        if (mask[i]) {
          int curr_r { static_cast<int>(std::round(r_map[i])) };

          if (curr_r >= 0 &&
              curr_r < rstats.num_bins) {
            float value { data[i] };
            if (value < rstats.r_threshold[curr_r] &&
                value > rstats.l_threshold[curr_r]) {
              rstats.offset[curr_r] += value;
              rstats.sigma[curr_r] += value * value;
              rstats.count[curr_r] += 1;
            }
          }
        }
      }

      for (int ri = 0; ri < rstats.num_bins; ++ri) {
        int count { rstats.count[ri] };

        if (count == 0) {
          rstats.offset[ri] = 0.0f;
          rstats.sigma[ri] = 0.001f;
          rstats.r_threshold[ri] = 1e9f;
          rstats.l_threshold[ri] = -1e9f;
        } else {
          float mean { rstats.offset[ri] / count };
          float var { (rstats.sigma[ri] / count) - (mean * mean) };
          float sigma { (var > 0.0f) ? std::sqrt(var) : 0.01f };

          rstats.offset[ri] = mean;
          rstats.sigma[ri] = sigma;
          rstats.r_threshold[ri] = std::max(ADC_threshold, mean + min_SNR * sigma);
          rstats.l_threshold[ri] = mean - min_SNR * sigma;
        }
      }
    }
  }

  void compute_local_background_ring(const float* data,
                                     const float* r_map,
                                     const bool* mask,
                                     float peak_com_fs,
                                     float peak_com_ss,
                                     int width,
                                     int height,
                                     std::vector<std::uint8_t>& pix_in_peak_map,
                                     float& bg_sum,
                                     float& bg_sum_sq,
                                     float& bg_max_i,
                                     int& bg_count,
                                     RadialStatistics& rstats,
                                     int local_background_radius) {
    int ring_width { 2 * local_background_radius };

    int c_fs { static_cast<int>(std::round(peak_com_fs)) };
    int c_ss { static_cast<int>(std::round(peak_com_ss)) };

    for (int ssj = -ring_width; ssj <= ring_width; ++ssj) {
      for (int fsi = -ring_width; fsi <= ring_width; ++fsi) {
        int n_fs { c_fs + fsi };
        int n_ss { c_ss + ssj };
        if (n_fs < 0      ||
            n_fs >= width ||
            n_ss < 0      ||
            n_ss >= height) {
          continue;
        }

        float r_dist { std::sqrt(static_cast<float>(fsi * fsi + ssj * ssj)) };
        if (r_dist > ring_width) {
          continue;
        }

        int n_idx { n_ss * width + n_fs };
        int n_rad { static_cast<int>(std::round(r_map[n_idx])) };
        if (n_rad < 0 || n_rad >= rstats.num_bins) {
          continue;
        }

        if (data[n_idx] < rstats.r_threshold[n_rad] &&
            pix_in_peak_map[n_idx] == 0      &&
            mask[n_idx]) {
          bg_sum += data[n_idx];
          bg_sum_sq += data[n_idx] * data[n_idx];
          bg_max_i = std::max(bg_max_i, data[n_idx]);
          bg_count++;
        }
      }
    }
  }

  void integrate_peak_intensity(const float* data,
                                int width,
                                int height,
                                int num_pix_in_peak,
                                int com_idx,
                                PF8Peak_v1* peak_list_out,
                                int& peak_count,
                                std::vector<int>& peak_pixels,
                                float local_offset,
                                float local_sigma,
                                float bg_max_i,
                                int max_number_peaks,
                                long max_pixel_count,
                                float min_SNR) {
    // Final intensity integration
    float peak_tot_i { 0 };
    float pk_tot_i_raw { 0 };
    float peak_max_i { 0 };

    float sum_com_fs { 0 };
    float sum_com_ss { 0 };
    for (int peak_idx = 0; peak_idx < num_pix_in_peak && peak_idx < max_pixel_count; ++peak_idx) {
      int curr_idx { peak_pixels[peak_idx] };
      float curr_i_raw { data[curr_idx] };
      float curr_i { curr_i_raw - local_offset };

      peak_tot_i += curr_i;
      pk_tot_i_raw += curr_i_raw;

      int curr_fs { curr_idx % width };
      int curr_ss { curr_idx / width };
      sum_com_fs += curr_i_raw * curr_fs;
      sum_com_ss += curr_i_raw * curr_ss;

      peak_max_i = std::max(peak_max_i, curr_i);
    }

    if (std::abs(pk_tot_i_raw) < 1e-10) {
      return;
    }

    float peak_com_fs { sum_com_fs / std::abs(pk_tot_i_raw) };
    float peak_com_ss { sum_com_ss / std::abs(pk_tot_i_raw) };

    float peak_snr { 0 };
    if (std::abs(local_sigma) > 1e-10) {
      peak_snr = peak_tot_i / local_sigma;
    }

    if (peak_snr < min_SNR) {
      return;
    }

    if (peak_max_i < (bg_max_i - local_offset)) {
      return;
    }

    if (peak_count < max_number_peaks) {
      peak_list_out[peak_count] = {
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

  void ccl_peak_search(const float* data,
                       const float* r_map,
                       const bool* mask,
                       PF8Peak_v1* peak_list_out,
                       int& num_peaks_out,
                       RadialStatistics& rstats,
                       std::vector<std::uint8_t>& pix_in_peak_map,
                       std::vector<int>& infs,
                       std::vector<int>& inss,
                       std::vector<int>& peak_pixels,
                       int width,
                       int height,
                       long min_pixel_count,
                       long max_pixel_count,
                       int local_background_radius,
                       int max_number_peaks,
                       float min_SNR) {
    int peak_count { 0 };
    int search_fs[9] = { 0, -1, 0, 1, -1, 1, -1, 0, 1 };
    int search_ss[9] = { 0, -1, -1, -1, 0, 0, 1, 1, 1 };
    for (int ss = 1; ss < height - 1; ++ss) {
      for (int fs = 1; fs < width - 1; ++fs) {
        int pxidx { ss * width + fs };
        int curr_rad { static_cast<int>(std::round(r_map[pxidx])) };

        if (curr_rad < 0 || curr_rad >= rstats.num_bins) {
          continue;
        }

        float curr_thresh { rstats.r_threshold[curr_rad] };
        if (data[pxidx] > curr_thresh   &&
            pix_in_peak_map[pxidx] == 0 &&
            mask[pxidx]) {
          infs[0] = fs;
          inss[0] = ss;
          peak_pixels[0] = pxidx;

          int num_pix_in_peak { 1 };
          float sum_com_fs { data[pxidx] * fs };
          float sum_com_ss { data[pxidx] * ss };
          float sum_i { data[pxidx] };

          int lt_num_pix_in_pk;
          do {
            lt_num_pix_in_pk = num_pix_in_peak;
            for (int p = 0; p < lt_num_pix_in_pk; ++p) {
              for (int k = 0; k < 9; ++k) {
                int n_fs { infs[p] + search_fs[k] };
                int n_ss { inss[p] + search_ss[k] };
                if (n_fs < 0 || n_fs >= width || n_ss < 0 || n_ss >= height) {
                  continue;
                }

                int n_idx { n_ss * width + n_fs };
                int n_rad { static_cast<int>(std::round(r_map[n_idx])) };

                if (n_rad < 0 || n_rad >= rstats.num_bins) {
                  continue;
                }

                if (data[n_idx] > rstats.r_threshold[n_rad] &&
                    pix_in_peak_map[n_idx] == 0      &&
                    mask[n_idx]) {
                  float curr_i { data[n_idx] - rstats.offset[n_rad] };
                  sum_i += curr_i;
                  sum_com_fs += curr_i * n_fs;
                  sum_com_ss += curr_i * n_ss;
                  inss[num_pix_in_peak] = n_ss;
                  infs[num_pix_in_peak] = n_fs;
                  pix_in_peak_map[n_idx] = 1;

                  if (num_pix_in_peak < max_pixel_count) {
                    peak_pixels[num_pix_in_peak] = n_idx;
                  }
                  num_pix_in_peak++;
                }
              }
            }
          } while (lt_num_pix_in_pk != num_pix_in_peak);

          if (num_pix_in_peak < min_pixel_count || num_pix_in_peak > max_pixel_count) {
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
          float bg_max_i { 0 };
          int bg_count { 0 };
          compute_local_background_ring(data,
                                        r_map,
                                        mask,
                                        peak_com_fs,
                                        peak_com_ss,
                                        width,
                                        height,
                                        pix_in_peak_map,
                                        bg_sum,
                                        bg_sum_sq,
                                        bg_max_i,
                                        bg_count,
                                        rstats,
                                        local_background_radius);

          float local_offset { 0 };
          float local_sigma { 0.01f };
          if (bg_count > 0) {
            local_offset = bg_sum / bg_count;
            float var { (bg_sum_sq / bg_count) - (local_offset * local_offset) };
            local_sigma = (var >= 0) ? std::sqrt(var) : 0.01f;
          } else {
            int rad { static_cast<int>(std::round(r_map[com_idx])) };
            if (rad >= 0 && rad < rstats.num_bins) {
              local_offset = rstats.offset[rad];
            }
          }

          // Final intensity integration
          integrate_peak_intensity(data,
                                   width,
                                   height,
                                   num_pix_in_peak,
                                   com_idx,
                                   peak_list_out,
                                   peak_count,
                                   peak_pixels,
                                   local_offset,
                                   local_sigma,
                                   bg_max_i,
                                   max_number_peaks,
                                   max_pixel_count,
                                   min_SNR);
        }
      }
    }

    num_peaks_out = peak_count;
  }
} // namespace xalgospp::features::impl
