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

#ifndef XALGOSPP_FEATURES_IMPL_PF8_CUH
#define XALGOSPP_FEATURES_IMPL_PF8_CUH

#include "xalgospp/features/impl/pf8.hh" // Has Peak struct

#include "xalgospp/export_macro.hh"

namespace xalgospp::features::impl {
  /**
   * A wrapping container for image metadata: mask and radius map.
   *
   * This struct allocates and frees the necessary buffers for holding pixel mask
   * and radius map information during peak finding algorithms.
   */
  struct XALG_API DevImageData {
    DevImageData() = default;

    // No copy
    DevImageData(const DevImageData& other) = delete;
    DevImageData& operator=(const DevImageData& other) = delete;

    DevImageData(DevImageData&& other) noexcept;
    DevImageData& operator=(DevImageData&& other) noexcept;

    DevImageData(int npixels);

    ~DevImageData();

    void swap(DevImageData& other) noexcept;

    bool* mask { nullptr };   ///< Mask of good/bad pixels. 1 == valid pixel.
    float* r_map { nullptr }; ///< Radius map

    int num_pixels { 0 };     ///< Total number of pixels in the image
  };

  /**
   * A wrapping container for buffers needed to compute radial background statistics.
   *
   * This struct allocates and frees the necessary buffers for computation of the
   * background signal (done in radial segments) during peak finding algorithms.
   */
  struct XALG_API DevRadialStatistics {
    DevRadialStatistics() = default;

    // No copy
    DevRadialStatistics(const DevRadialStatistics& other) = delete;
    DevRadialStatistics& operator=(const DevRadialStatistics& other) = delete;

    DevRadialStatistics(DevRadialStatistics&& other) noexcept;
    DevRadialStatistics& operator=(DevRadialStatistics&& other) noexcept;

    DevRadialStatistics(int nbins);

    ~DevRadialStatistics();

    void swap(DevRadialStatistics& other) noexcept;

    float* offset { nullptr };      ///< Mean background offset per radial bin
    float* sigma { nullptr };       ///< Background std dev per radial bin
    int* count { nullptr };         ///< Pixel count per radial bin
    float* r_threshold { nullptr }; ///< Upper intensity threshold for candidate per radial bin
    float* l_threshold { nullptr }; ///< Lower intensity threhsold for candidate per radial bin

    float* sum { nullptr };         ///< Intermediate sum of pixel intensities per radial bin
    float* sum_sq { nullptr };      ///< Intermediate squared sum of intensities per radial bin

    int num_bins { 512 };           ///< Total number of radial bins.
  };

  /**
   * A wrapping container for peak labelling buffers.
   *
   * This struct allocates and frees the necessary buffers for doing label propagation
   * (ie connected components) during peak finding algorithms. In general, these buffers
   * are intermediate scratch buffers -- They don't necessarily need to be used by
   * the end user, and so are separated out from the final peak data.
   */
  struct XALG_API DevPeakLabelData {
    DevPeakLabelData() = default;

    // No copy
    DevPeakLabelData(const DevPeakLabelData& other) = delete;
    DevPeakLabelData& operator=(const DevPeakLabelData& other) = delete;

    DevPeakLabelData(DevPeakLabelData&& other) noexcept;
    DevPeakLabelData& operator=(DevPeakLabelData&& other) noexcept;

    DevPeakLabelData(int npixels);

    ~DevPeakLabelData();

    void swap(DevPeakLabelData& other) noexcept;

    int* labels_v1 { nullptr };      ///< Connected-component peak labels
    int* labels_v2 { nullptr };      ///< Connected-component peak labels (secondary buffer)

    bool* changed { nullptr };       ///< Flag for state change during label propagation

    float* label_sum_i { nullptr };  ///< Accumulated (background subtracted) intensity per peak label
    float* label_sum_fs { nullptr }; ///< Accumulated fast-scan weighted position per peak label
    float* label_sum_ss { nullptr }; ///< Accumulated slow-scan weighted position per peak label
    float* label_max_i { nullptr };  ///< Maximum pixel intensity within a peak label
    int* label_npix { nullptr };     ///< Number of pixels within a peak

    int* peak_count { nullptr };     ///< Total number of peaks found

    int num_pixels { 0 };            ///< Total number of pixels in an image
  };

  /**
   * Run initial statistics computation for background in radial bins.
   *
   * @param[in] data The image data.
   * @param[in] mask The validity mask for each pixel in the image.
   * @param[in] r_map The radius map for computing radial bins.
   * @param[out] r_threshold Upper intensity threshold for candidate per radial bin
   * @param[out] l_threshold Lower intensity threshold for candidate per radial bin
   * @param[in] num_pixels The total number of pixels in the image (and mask, etc.).
   * @param[in] num_bins The number of radial bins.
   * @param[out] r_count Pixel count per radial bin
   * @param[out] r_sum Intermediate sum of pixel intensities per radial bin
   * @param[out] r_sum_sq Intermediate squared sum of intensities per radial bin
   */
  __global__ XALG_API void k_radial_stats_accumulate(const float* __restrict__ data,
                                                     const bool* __restrict__ mask,
                                                     const float* __restrict__ r_map,
                                                     const float* __restrict__ r_threshold,
                                                     const float* __restrict__ l_threshold,
                                                     int num_pixels,
                                                     int num_bins,
                                                     int* r_count,
                                                     float* r_sum,
                                                     float* r_sum_sq);

  /**
   * Finalize the radial statistics and compute remaining metrics.
   *
   * @note The kernel will reset r_count, r_sum, and r_sum_sq for the next iteration.
   *
   * @param[out] r_offset Mean background offset per radial bin
   * @param[out] r_sigma Background std dev per radial bin
   * @param[out] r_threshold Upper intensity threshold for candidate per radial bin
   * @param[out] l_threshold Lower intensity threshold for candidate per radial bin
   * @param[in] r_sum Intermediate sum of pixel intensities per radial bin
   * @param[in] r_sum_sq Intermediate squared sum of intensities per radial bin
   * @param[in] r_count Pixel count per radial bin
   * @param[in] num_bins The number of radial bins.
   * @param[in] adc_threshold The cut-off for the pixel intensity.
   */
  __global__ XALG_API void k_radial_stats_finalize(float* r_offset,
                                                   float* r_sigma,
                                                   float* r_threshold,
                                                   float* l_threshold,
                                                   float* r_sum,
                                                   float* r_sum_sq,
                                                   int* r_count,
                                                   int num_bins,
                                                   float min_snr,
                                                   float adc_threshold);

  /**
   * Initailize labels for finding peak candidates.
   *
   * @note label_sum_i, label_sum_fs, label_sum_ss, label_max_i, label_npix, and
   *       peak_count are passed solely so they can be reset on each iteration.
   *
   * @param[in] data The image data.
   * @param[in] mask The validity mask for each pixel in the image.
   * @param[in] r_threshold Upper intensity threshold for candidate per radial bin
   * @param[in] r_map The radius map for computing radial bins.
   * @param[out] labels The initial labels for peak candidates (non-zero is candidate)
   * @param[in] num_pixels The number of pixels in the image.
   * @param[in] num_bins The number of radial bins.
   * @param[out] label_sum_i Accumulated (background subtracted) intensity per peak label
   * @param[out] label_sum_fs Accumulated fast-scan weighted position per peak label
   * @param[out] label_sum_ss Accumulated slow-scan weighted position per peak label
   * @param[out] label_max_i Maximum pixel intensity within a peak label
   * @param[out] label_npix Number of pixels within a peak
   * @param[out] peak_count Number of found peaks.
   */
  __global__ XALG_API void k_init_labels(const float* __restrict__ data,
                                         const bool* __restrict__ mask,
                                         const float* __restrict__ r_threshold,
                                         const float* __restrict__ r_map,
                                         int* labels,
                                         int num_pixels,
                                         int num_bins,
                                         float* label_sum_i,
                                         float* label_sum_fs,
                                         float* label_sum_ss,
                                         float* label_max_i,
                                         int* label_npix,
                                         int* peak_count);

  /**
   * Run label propagation (connected-components search) from an initial set.
   *
   * @note This function should be called a number of times, alternating the
   *       input and output buffers to fully complete the label propagation.
   *       The number of iterations will depend on how large of a "blob" or "peak"
   *       you want to find, but 6 iterations (3, of two calls each) should be
   *       sufficient for blobs/peaks up to ~50 or so pixels.
   *
   * @code{.cpp}
   * // Example to propagate up to ~50 pixels.
   * for (int i = 0; i < 3; ++i) {
   *   k_propagate_labels<<<blocks, threads>>>(labels_in,
   *                                           labels_out,
   *                                           width,
   *                                           height,
   *                                           changed);
   *
   *   // NOTE: we alternated the labels_in and labels_out
   *   k_propagate_labels<<<blocks, threads>>>(labels_out,
   *                                           labels_in,
   *                                           width,
   *                                           height,
   *                                           changed);
   * }
   * @endcode
   *
   * @param[in] labels_in The input initial label set.
   * @param[out] labels_out The output propagated label set.
   * @param[in] width The width of the image (fast-scan axis).
   * @param[in] height The height of the image (slow-scan axis).
   */
  __global__ XALG_API void k_propagate_labels(const int* __restrict__ labels_in,
                                              int* labels_out,
                                              int width,
                                              int height,
                                              bool* changed);

  /**
   * Given a finalized set of peak candidates, calculate statistics on them.
   *
   * Statistics include the integrated intensities, SNR, etc.
   *
   * @param[in] data The image data.
   * @param[in] labels The finalized peak candidate labels.
   * @param[out] label_sum_i Accumulated (background subtracted) intensity per peak label
   * @param[out] label_sum_fs Accumulated fast-scan weighted position per peak label
   * @param[out] label_sum_ss Accumulated slow-scan weighted position per peak label
   * @param[out] label_max_i Maximum pixel intensity within a peak label
   * @param[out] label_npix Number of pixels within a peak
   * @param[in] The total number of pixels in the image.
   * @param[in] width The width of the image (fast-scan axis).
   */
  __global__ XALG_API void k_accumulate_peak_props(const float* __restrict__ data,
                                                   const int* __restrict__ labels,
                                                   float* label_sum_i,
                                                   float* label_sum_fs,
                                                   float* label_sum_ss,
                                                   float* label_max_i,
                                                   int* label_npix,
                                                   int num_pixels,
                                                   int width);

  /**
   * Filter peak candidates using calculated metrics and input parameters, and write to output.
   *
   * @param[in] data The image data.
   * @param[in] mask The validity mask for each pixel in the image.
   * @param[in] labels The finalized peak candidate labels.
   * @param[in] labels_sum_i Accumulated (background subtracted) intensity per peak label
   * @param[in] label_sum_fs Accumulated fast-scan weighted position per peak label
   * @param[in] label_sum_ss Accumulated slow-scan weighted position per peak label
   * @param[in] label_max_i Maximum pixel intensity within a peak label
   * @param[in] label_npix Number of pixels within a peak
   * @param[in] width The width of the image (fast-scan axis).
   * @param[in] height The height of the image (slow-scan axis).
   * @param[in] local_background_radius The radius to use for the local background.
   * @param[in] min_snr The minimum signal-to-noise ratio to allow for a peak.
   * @param[in] min_pix Minimum number of pixels to allow in a peak.
   * @param[in] max_pix Maximum number of pixels to allow in a peak.
   * @param[out] peaks_out The output buffer to write integrated peaks.
   * @param[out] peaks_out_count The counter for the total number of peaks found so far.
   * @param[in] max_peaks The maximum number of peaks to find.
   */
  __global__ XALG_API void k_refine_and_filter_peaks(const float* __restrict__ data,
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
                                                     int max_peaks);
} // namespace xalgospp::features::impl

#endif // XALGOSPP_FEATURES_IMPL_PF8_CUH
