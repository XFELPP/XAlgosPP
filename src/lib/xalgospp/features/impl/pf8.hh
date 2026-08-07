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

#ifndef XALGOSPP_FEATURES_IMPL_PF8_HH
#define XALGOSPP_FEATURES_IMPL_PF8_HH

#include "xalgospp/export_macro.hh"

#include <ncarray/ncarrays.hh>
#include <ncarray/soarrays.hh>

#ifdef XALG_HAS_CUDA
#include <ncarray/ncdevarrays.cuh>
#include <ncarray/sodevarrays.cuh>
#include "xalgospp/features/peakfinder8_gpu.cuh"
#endif

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <stdexcept>

namespace xalgospp::features::impl {

  /**
   * An identified peak within an image including associated metadata.
   */
  struct PF8Peak_v1 {
    float com_fs;          ///< The center of mass along the fast-scan dimension
    float com_ss;          ///< The center of mass along the slow-scan dimension
    float total_intensity; ///< The total integrated intensity of all pixels in the peak
    float max_intensity;   ///< The maximum pixel intensity of any pixel in the peak
    float snr;             ///< The signal-to-background ratio of the peak
    int npix;              ///< The total number of pixels in the peak
    int label;             ///< Peak identifier - generally the linearized pixel index (`com_idx`)
  };

  /**
   * A wrapping container for buffers needed to compute radial background statistics.
   *
   * This struct allocates and frees the necessary buffers for computation of the
   * background signal (done in radial segments) during peak finding algorithms.
   */
  struct XALG_API RadialStatistics {
    RadialStatistics() = default;

    RadialStatistics(const RadialStatistics& other) = default;
    RadialStatistics(RadialStatistics&& other) noexcept = default;

    RadialStatistics& operator=(const RadialStatistics& other) = default;
    RadialStatistics& operator=(RadialStatistics&& other) noexcept = default;

    RadialStatistics(int nbins)
      : offset(nbins, 0.0f)
      , sigma(nbins, 0.0f)
      , count(nbins, 0)
      , r_threshold(nbins, 1e9f)
      , l_threshold(nbins, -1e9f)
      , num_bins(nbins)
    {}

    std::vector<float> offset;      ///< Mean background offset per radial bin
    std::vector<float> sigma;       ///< Background std dev per radial bin
    std::vector<int> count;         ///< Pixel count per radial bin
    std::vector<float> r_threshold; ///< Upper intensity threshold for candidate per radial bin
    std::vector<float> l_threshold; ///< Lower intensity threhsold for candidate per radial bin
    int num_bins { 512 };           ///< Total number of radial bins.
  };

  /**
   * Compute the statistics to determine background signal in radial segments.
   *
   * @param[in] data The image data.
   * @param[in] r_map The radius map for computing radial bins.
   * @param[in] mask The validity mask for each pixel in the image.
   * @param[out] rstats The output where background radial statistics are stored.
   * @param[in] num_pixels The total number of pixels in the image (and mask, etc.).
   * @param[in] ADC_threshold The cut-off for the pixel intensity.
   * @param[in] min_SNR The minimum signal to noise ratio to use.
   */
  XALG_API void compute_radial_background(const float* data,
                                          const float* r_map,
                                          const bool* mask,
                                          RadialStatistics& rstats,
                                          std::size_t num_pixels,
                                          int niter = 5,
                                          float ADC_threshold = 10.0f,
                                          float min_SNR = 7.0f);

  /**
   * Compute the background signal locally around a peak candidate.
   *
   * @note The radial statistics are computed globally, while this second background
   *       signal is computed only in the local candidate's neighborhood.
   *
   * @param[in] data The image data.
   * @param[in] r_map The radius map for computing radial bins.
   * @param[in] mask The validity mask for each pixel in the image.
   * @param[in] peak_com_fs The center of mass of the candidate in the fast-scan axis.
   * @param[in] peak_com_ss The center of mass of the candidate in the slow-scan axis.
   * @param[in] width The width of the image (fast-scan axis).
   * @param[in] height The height of the image (slow-scan axis).
   * @param[out] pix_in_peak_map Registry holding pixels in each candidate peak.
   * @param[out] bg_sum The output local background intensity sum.
   * @param[out] bg_sum_sq The output local background intensity sum squared.
   * @param[out] bg_max_i The output local background maximum pixel intensity.
   * @param[out] bg_count The output number of pixels in the local background.
   * @param[in] rstats The pre-computed radial statistics for the *GLOBAL* background.
   * @param[in] local_background_radius The radius to use for the local background.
   */
  XALG_API void compute_local_background_ring(const float* data,
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
                                              int local_background_radius);

  /**
   * Integrate a finalized peak candidate and write it to the output buffer.
   *
   * @param[in] data The image data.
   * @param[in] width The width of the image (fast-scan axis).
   * @param[in] height The height of the image (slow-scan axis).
   * @param[in] num_pix_in_peak The total number of pixels in the candidate peak.
   * @param[in] com_idx The linearized pixel index for the peak center.
   * @param[out] peak_list_out The output buffer to write integrated peaks.
   * @param[out] peak_count The output counter for the total number of peaks found so far.
   * @param[out] peak_pixels The pixels in the peak.
   * @param[in] local_offset The local background mean offset.
   * @param[in] local_sigma The local background standard deviation.
   * @param[in] bg_max_i The local background maximum pixel intensity.
   * @param[in] max_number_peaks The maximum number of peaks to find.
   * @param[in] max_pixel_count The maximum number of pixels to allow in a peak.
   * @param[in] min_SNR The minimum signal-to-noise ratio to allow for a peak.
   */
  XALG_API void integrate_peak_intensity(const float* data,
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
                                         float min_SNR);

  /**
   * Run the connected-components labelling and peak integration on an image.
   *
   * @note The radial background statistics (for the global background) must be
   *       computed and provided as an argument to this function.
   *
   * @param[in] data The image data.
   * @param[in] r_map The radius map for computing radial bins.
   * @param[in] mask The validity mask for each pixel in the image.
   * @param[out] peak_list_out The output buffer to write integrated peaks.
   * @param[out] num_peaks_out The finalized count of peaks found.
   * @param[in] rstats The pre-computed radial statistics for the *GLOBAL* background.
   * @param[in] pix_in_peak_map Registry holding pixels in each candidate peak.
   * @param[in] infs Buffer for tracking peak pixels in the fast-scan axis.
   * @param[in] inss Buffer for tracking peak pixels in the slow-scan axis.
   * @param[in] peak_pixels The pixels in the peak.
   * @param[in] width The width of the image (fast-scan axis).
   * @param[in] height The height of the image (slow-scan axis).
   * @param[in] min_pixel_count The minimum number of pixels to allow in a peak.
   * @param[in] max_pixel_count The maximum number of pixels to allow in a peak.
   * @param[in] local_background_radius The radius, around a peak candidate, to use
   *            for the local background calculations.
   * @param[in] max_number_peaks The maximum number of peaks to find.
   * @param[in] min_SNR The minimum peak candidate signal-to-noise ratio to allow.
   */
  XALG_API void ccl_peak_search(const float* data,
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
                                float min_SNR = 7.0f);
} // namespace xalgospp::features::impl

#endif // XALGOSPP_FEATURES_IMPL_PF8_HH
