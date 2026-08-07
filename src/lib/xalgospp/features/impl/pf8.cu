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

#include "xalgospp/features/impl/pf8.cuh"

#include "xalgospp/features/impl/pf8.hh" // Has Peak struct

namespace xalgospp::features::impl {
  __global__ void k_radial_stats_accumulate(const float* __restrict__ data,
                                            const bool* __restrict__ mask,
                                            const float* __restrict__ r_map,
                                            const float* __restrict__ r_threshold,
                                            const float* __restrict__ l_threshold,
                                            int num_pixels,
                                            int num_bins,
                                            int* r_count,
                                            float* r_sum,
                                            float* r_sum_sq) {
    unsigned idx { blockIdx.x * blockDim.x + threadIdx.x };
    if (idx >= num_pixels) {
      return;
    }

    if (mask[idx]) {
      int bin { static_cast<int>(roundf(r_map[idx])) };

      if (bin >= 0 && bin < num_bins) {
        float val { data[idx] };
        if (val < r_threshold[bin] &&
            val > l_threshold[bin]) {
          atomicAdd(&r_sum[bin], val);
          atomicAdd(&r_sum_sq[bin], val * val);
          atomicAdd(&r_count[bin], 1);
        }
      }
    }
  }

  __global__ void k_radial_stats_finalize(float* r_offset,
                                          float* r_sigma,
                                          float* r_threshold,
                                          float* l_threshold,
                                          const float* r_sum,
                                          const float* r_sum_sq,
                                          const int* r_count,
                                          int num_bins,
                                          float min_snr,
                                          float adc_threshold) {
    int bin { blockIdx.x * blockDim.x + threadIdx.x };
    if (bin >= num_bins) {
      return;
    }

    int count { r_count[bin] };
    if (count > 0) {
      float mean { r_sum[bin] / count };
      float sigma2 { (r_sum_sq[bin] / count) - (mean * mean) };
      float sigma { (sigma2 > 0.0f) ? sqrtf(sigma2) : 0.01f };

      r_offset[bin] = mean;
      r_sigma[bin] = sigma;
      r_threshold[bin] = fmaxf(adc_threshold, mean + min_snr * sigma);
      l_threshold[bin] = mean - min_snr * sigma;
    } else {
      r_offset[bin] = 0.0f;
      r_sigma[bin] = 0.001f;
      r_threshold[bin] = 1e9f;
      l_threshold[bin] = -1e9f;
    }
  }

  __global__ void k_init_labels(const float* __restrict__ data,
                                const bool* __restrict__ mask,
                                const float* __restrict__ r_threshold,
                                const float* __restrict__ r_map,
                                int* labels,
                                int num_pixels,
                                int num_bins) {
    unsigned idx { blockIdx.x * blockDim.x + threadIdx.x };
    if (idx >= num_pixels) {
      return;
    }

    int bin { static_cast<int>(roundf(r_map[idx])) };
    if (bin >= 0                     &&
        bin < num_bins               &&
        mask[idx]                    &&
        data[idx] > r_threshold[bin]) {
      labels[idx] = idx;
    } else {
      labels[idx] = -1;
    }
  }

  __global__ void k_propagate_labels(const int* __restrict__ labels_in,
                                     int* labels_out,
                                     int width,
                                     int height,
                                     bool* changed) {
      unsigned idx { blockIdx.x * blockDim.x + threadIdx.x };
      if (idx >= static_cast<unsigned>(width * height)) {
        return;
      }

      unsigned fs { idx % width };
      unsigned ss { idx / width };

      int current_label { labels_in[idx] };
      if (current_label == -1) {
        labels_out[idx] = -1;
        return;
      }

      int min_label { current_label };
      for (int dss = -1; dss <= 1; ++dss) {
        for (int dfs = -1; dfs <= 1; ++dfs) {
          int n_fs { static_cast<int>(fs) + dfs };
          int n_ss { static_cast<int>(ss) + dss };

          if (n_fs >= 0     &&
              n_fs < width  &&
              n_ss >= 0     &&
              n_ss < height) {
            int n_idx { n_ss * width + n_fs };
            int n_label { labels_in[n_idx] };

            if (n_label != -1       &&
                n_label < min_label) {
              min_label = n_label;
            }
          }
        }
      }

      if (min_label != current_label) {
        labels_out[idx] = min_label;
        *changed = true;
      } else {
        labels_out[idx] = current_label;
      }
    }

    __global__ void k_accumulate_peak_props(const float* __restrict__ data,
                                            const int* __restrict__ labels,
                                            float* label_sum_i,
                                            float* label_sum_fs,
                                            float* label_sum_ss,
                                            float* label_max_i,
                                            int* label_npix,
                                            int num_pixels,
                                            int width) {
      unsigned idx { blockIdx.x * blockDim.x + threadIdx.x };
      if (idx >= num_pixels) {
        return;
      }

      int label { labels[idx] };
      if (label == -1) {
        return; // No peak
      }

      float val { data[idx] };
      unsigned fs { idx % static_cast<unsigned>(width) };
      unsigned ss { idx / static_cast<unsigned>(width) };

      atomicAdd(&label_sum_i[label], val);
      atomicAdd(&label_sum_fs[label], val * fs);
      atomicAdd(&label_sum_ss[label], val * ss);
      atomicMax(reinterpret_cast<int*>(&label_max_i[label]), __float_as_int(val));
      atomicAdd(&label_npix[label], 1);
    }

  __global__ void k_refine_and_filter_peaks(const float* __restrict__ data,
                                            const bool* __restrict__ mask,
                                            const int* __restrict__ labels,
                                            const float* label_sum_i,
                                            const float* label_sum_fs,
                                            const float* label_sum_ss,
                                            const float* label_max_i,
                                            const int* label_npix,
                                            int width,
                                            int height,
                                            int local_bg_radius,
                                            float min_snr,
                                            int min_pix,
                                            int max_pix,
                                            PF8Peak_v1* peaks_out,
                                            int* peaks_out_count,
                                            int max_peaks) {
    unsigned label { blockIdx.x * blockDim.x + threadIdx.x };

    if (label >= width * height) {
      // Remember: the labels are pixel indices
      return;
    }

    int npix { label_npix[label] };
    if (npix < min_pix ||
        npix > max_pix) {
      // Too few, or too many pixels in peak
      return;
    }

    float sum_i { label_sum_i[label] };
    if (abs(sum_i) < 1e-10) {
      // Protection against numerical artifacts
      return;
    }

    float com_fs { label_sum_fs[label] / sum_i };
    float com_ss { label_sum_ss[label] / sum_i };

    // --- Estimate local background --- //
    int c_fs { static_cast<int>(roundf(com_fs)) };
    int c_ss { static_cast<int>(roundf(com_ss)) };

    float bg_sum { 0 };
    float bg_sum_sq { 0 };
    unsigned bg_count { 0 };
    float bg_max_i { -1e9f };

    for (int dss = -local_bg_radius; dss <= local_bg_radius; ++dss) {
      for (int dfs = -local_bg_radius; dfs <= local_bg_radius; ++dfs) {
        int n_fs { c_fs + dfs };
        int n_ss { c_ss + dss };

        if (n_fs >= 0 && n_fs < width && n_ss >= 0 && n_ss < height) {
          int n_idx { n_ss * width + n_fs };
          // NOTE: Only pixels which are NOT in a peak count as background
          if (mask[n_idx] && labels[n_idx] == -1) {
            float val { data[n_idx] };
            bg_sum += val;
            bg_sum_sq += val * val;
            bg_count++;
            bg_max_i = fmaxf(bg_max_i, val);
          }
        }
      }
    }

    float local_offset { (bg_count > 0) ? (bg_sum / bg_count) : 0 };
    float sigma2 {
      (bg_count > 1) ? ((bg_sum_sq / bg_count) - (local_offset * local_offset)) : 0.01f
    };
    float local_sigma { (sigma2 > 0) ? sqrtf(sigma2) : 0.01f };

    float peak_max_i { label_max_i[label] - local_offset };
    float peak_tot_i { sum_i - npix * local_offset };
    float snr { (local_sigma > 1e-10) ? (peak_tot_i / local_sigma) : 0 };

    if (snr >= min_snr && peak_max_i >= (bg_max_i - local_offset)) {
      int out_idx = atomicAdd(peaks_out_count, 1);
      if (out_idx < max_peaks) {
        peaks_out[out_idx] = {
          com_fs,
          com_ss,
          peak_tot_i,
          peak_max_i,
          snr,
          npix,
          label
        };
      }
    }
  }
} // namespace xalgospp::features::impl

