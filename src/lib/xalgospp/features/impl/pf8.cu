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

#include <utility>

namespace xalgospp::features::impl {
  DevImageData::DevImageData(int npixels)
    : num_pixels(npixels)
  {
    cudaMalloc(reinterpret_cast<void**>(&mask), npixels * sizeof(bool));
    cudaMalloc(reinterpret_cast<void**>(&r_map), npixels * sizeof(float));
  }

  DevImageData::~DevImageData() {
    if (mask) {
      cudaFree(mask);
      mask = nullptr;
    }

    if (r_map) {
      cudaFree(r_map);
      r_map = nullptr;
    }
  }

  void DevImageData::swap(DevImageData& other) noexcept {
    std::swap(mask, other.mask);
    std::swap(r_map, other.r_map);
    std::swap(num_pixels, other.num_pixels);
  }

  DevImageData::DevImageData(DevImageData&& other) noexcept { swap(other); }
  DevImageData& DevImageData::operator=(DevImageData&& other) noexcept {
    if (this != &other) {
      swap(other);
    }

    return *this;
  }

  DevRadialStatistics::DevRadialStatistics(int nbins)
    : num_bins(nbins)
  {
    std::size_t bins_bytes { static_cast<std::size_t>(nbins * sizeof(float)) };
    std::size_t count_bytes { static_cast<std::size_t>(nbins * sizeof(int)) };

    cudaMalloc(reinterpret_cast<void**>(&offset), bins_bytes);
    cudaMalloc(reinterpret_cast<void**>(&sigma), bins_bytes);
    cudaMalloc(reinterpret_cast<void**>(&count), count_bytes);

    cudaMalloc(reinterpret_cast<void**>(&r_threshold), bins_bytes);
    cudaMalloc(reinterpret_cast<void**>(&l_threshold), bins_bytes);

    cudaMalloc(reinterpret_cast<void**>(&sum), bins_bytes);
    cudaMalloc(reinterpret_cast<void**>(&sum_sq), bins_bytes);
  }

  DevRadialStatistics::~DevRadialStatistics() {
    if (offset) {
      cudaFree(offset);
      offset = nullptr;
    }

    if (sigma) {
      cudaFree(sigma);
      sigma = nullptr;
    }

    if (count) {
      cudaFree(count);
      count = nullptr;
    }

    if (r_threshold) {
      cudaFree(r_threshold);
      r_threshold = nullptr;
    }

    if (l_threshold) {
      cudaFree(l_threshold);
      l_threshold = nullptr;
    }

    if (sum) {
      cudaFree(sum);
      sum = nullptr;
    }

    if (sum_sq) {
      cudaFree(sum_sq);
      sum_sq = nullptr;
    }
  }

  void DevRadialStatistics::swap(DevRadialStatistics& other) noexcept {
    std::swap(offset, other.offset);
    std::swap(sigma, other.sigma);
    std::swap(count, other.count);
    std::swap(r_threshold, other.r_threshold);
    std::swap(l_threshold, other.l_threshold);
    std::swap(sum, other.sum);
    std::swap(sum_sq, other.sum_sq);
    std::swap(num_bins, other.num_bins);
  }

  DevRadialStatistics::DevRadialStatistics(DevRadialStatistics&& other) noexcept {
    swap(other);
  }
  DevRadialStatistics& DevRadialStatistics::operator=(DevRadialStatistics&& other) noexcept {
    if (this != &other) {
      swap(other);
    }

    return *this;
  }

  DevPeakLabelData::DevPeakLabelData(int npixels)
    : num_pixels(npixels)
  {
    std::size_t pix_bytes { static_cast<std::size_t>(npixels * sizeof(float)) };
    std::size_t pix_int_bytes { static_cast<std::size_t>(npixels * sizeof(int)) };

    cudaMalloc(reinterpret_cast<void**>(&labels_v1), pix_int_bytes);
    cudaMalloc(reinterpret_cast<void**>(&labels_v2), pix_int_bytes);

    cudaMalloc(reinterpret_cast<void**>(&changed), sizeof(bool));

    cudaMalloc(reinterpret_cast<void**>(&label_sum_i), pix_bytes);
    cudaMalloc(reinterpret_cast<void**>(&label_sum_fs), pix_bytes);
    cudaMalloc(reinterpret_cast<void**>(&label_sum_ss), pix_bytes);
    cudaMalloc(reinterpret_cast<void**>(&label_max_i), pix_bytes);
    cudaMalloc(reinterpret_cast<void**>(&label_npix), pix_int_bytes);

    cudaMalloc(reinterpret_cast<void**>(&peak_count), sizeof(int));
  }

  DevPeakLabelData::~DevPeakLabelData() {
    if (labels_v1) {
      cudaFree(labels_v1);
      labels_v1 = nullptr;
    }

    if (labels_v2) {
      cudaFree(labels_v2);
      labels_v2 = nullptr;
    }

    if (changed) {
      cudaFree(changed);
      changed = nullptr;
    }

    if (label_sum_i) {
      cudaFree(label_sum_i);
      label_sum_i = nullptr;
    }

    if (label_sum_fs) {
      cudaFree(label_sum_fs);
      label_sum_fs = nullptr;
    }

    if (label_sum_ss) {
      cudaFree(label_sum_ss);
      label_sum_ss = nullptr;
    }

    if (label_max_i) {
      cudaFree(label_max_i);
      label_max_i = nullptr;
    }

    if (label_npix) {
      cudaFree(label_npix);
      label_npix = nullptr;
    }

    if (peak_count) {
      cudaFree(peak_count);
      peak_count = nullptr;
    }
  }

  void DevPeakLabelData::swap(DevPeakLabelData& other) noexcept {
    std::swap(labels_v1, other.labels_v1);
    std::swap(labels_v2, other.labels_v2);
    std::swap(changed, other.changed);
    std::swap(label_sum_i, other.label_sum_i);
    std::swap(label_sum_fs, other.label_sum_fs);
    std::swap(label_sum_ss, other.label_sum_ss);
    std::swap(label_max_i, other.label_max_i);
    std::swap(label_npix, other.label_npix);
    std::swap(peak_count, other.peak_count);
    std::swap(num_pixels, other.num_pixels);
  }

  DevPeakLabelData::DevPeakLabelData(DevPeakLabelData&& other) noexcept {
    swap(other);
  }
  DevPeakLabelData& DevPeakLabelData::operator=(DevPeakLabelData&& other) noexcept {
    if (this != &other) {
      swap(other);
    }

    return *this;
  }

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
    unsigned bin { blockIdx.x * blockDim.x + threadIdx.x };
    if (static_cast<int>(bin) >= num_bins) {
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
          static_cast<int>(label)
        };
      }
    }
  }
} // namespace xalgospp::features::impl

