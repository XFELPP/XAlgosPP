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

#ifndef XALGOSPP_DETECTOR_DETECTOR_HH
#define XALGOSPP_DETECTOR_DETECTOR_HH

#include "xalgospp/algorithm.hh"
#include "xalgospp/detector/calibration.hh"

#include "ncarray/layout.hh" // Re-use ncarray's metadata struct for det descr

#include <Eigen/Dense>

#ifdef _WIN32
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#else
#include <sys/types.h>
#endif

#include <cstdint>
#include <cstring>
#include <span>

#ifndef XALGOS_HD
#ifdef __CUDACC__
#define XALGOS_HD __host__ __device__
#else
#define XALGOS_HD
#endif
#endif

namespace xalgospp {
  using det::CalibParameters;
  using det::PixelCalibStruct;

  template <typename T>
  Eigen::Map<Eigen::Array<T, Eigen::Dynamic, 1>>
  ncarray_to_eigen(const ncarray::NCArrayView& view) {
    if (view.ndim() != 1 && view.ndim() != 2) {
      throw std::runtime_error("Only 1D or 2D views supported for simple Eigen mapping!");
    }

    return
      Eigen::Map<Eigen::Array<T, Eigen::Dynamic, 1>>(static_cast<T*>(view.data()),
                                                     view.size());
  }

  class CalibrationAlgorithm : public AlgorithmBase<CalibrationAlgorithm> {
  public:
    CalibrationAlgorithm(const CalibParameters& params,
                         const ncarray::NCArrayView& constants_view)
      : m_params(params)
    {
      // Map the ncarray view of constants to our internal calibration span
      m_constants =
        std::span<PixelCalibStruct>(static_cast<PixelCalibStruct*>(constants_view.data()),
                                    constants_view.size());
    }

    const char* name_impl() const { return "Calibration"; }

    void process_impl(const ncarray::NCArrayView& input,
                      ncarray::NCArray& output) {
      if (input.dtype() != ncarray::DType::uint16) {
        throw std::runtime_error("Calibration input raw data must be uint16!");
      }

      auto raw_map = ncarray_to_eigen<std::uint16_t>(input);
      auto out_map = ncarray_to_eigen<float>(output);
      out_map = calibrate(raw_map, m_params, m_constants, 0);
    }

  private:
    CalibParameters m_params;
    std::span<PixelCalibStruct> m_constants;
  };

  class AlgorithmFactory {
  public:
    static std::unique_ptr<CalibrationAlgorithm>
    create_calibration(const CalibParameters& params, const ncarray::NCArrayView& constants) {
      return std::make_unique<CalibrationAlgorithm>(params, constants);
    }
  };

  static constexpr std::uint16_t MaxNameSize { 256 };
  static constexpr std::uint8_t MaxNDim { 10 };
  static constexpr std::uint16_t MaxDetectors { 256 };

  struct DetectorDescriptor {
    char detector_type[MaxNameSize] { 0 };
    // We'll re-use ncarray's metadata struct for detector descriptions (GPU friendly)
    ncarray::Metadata shape;
    CalibParameters calib_params;

    XALGOS_HD inline bool operator<(const DetectorDescriptor& other) const {
      int cmp { std::strcmp(detector_type, other.detector_type) };
      if (cmp != 0) {
        return cmp < 0;
      }

      for (ssize_t i = 0; i < MaxNDim; ++i) {
        if (shape[i] != other.shape[i]) {
          return shape[i] < other.shape[i];
        }
      }
    }
  };

  struct DetectorEntry {
    std::uint64_t hash { 0 };
    DetectorDescriptor desc;
  };

  class DetectorRegistry {
  public:
    XALGOS_HD static DetectorRegistry& create_registry() {
      static DetectorRegistry REGISTRY;
      return REGISTRY;
    }

    XALGOS_HD static std::uint64_t hash_str(const char* str) {
      std::uint64_t hash { 1469598103934665603ULL };

      while (*str) {
        hash ^= static_cast<std::uint64_t>(*str++);
        hash *= 1099511628211ULL;
      }

      return hash;
    }

    XALGOS_HD inline void register_detector(const char* name,
                                            const DetectorDescriptor& desc) {
      std::uint64_t det_hash = DetectorRegistry::hash_str(name);
      if (auto* desc = get_detector_internal(det_hash)) {
        return;
      }

      m_entries[m_num_detectors].desc = desc;
      m_num_detectors++;
    }

    XALGOS_HD inline const DetectorDescriptor* get_detector(const char* name) const {
      std::uint64_t det_hash = DetectorRegistry::hash_str(name);

      return get_detector_internal(det_hash);
    }


    XALGOS_HD inline bool has_detector(const char* name) const {
      std::uint64_t det_hash = DetectorRegistry::hash_str(name);

      if (get_detector_internal(det_hash)) {
        return true;
      }

      return false;
    }

    DetectorRegistry(const DetectorRegistry&) = delete;
    DetectorRegistry(DetectorRegistry&&) = delete;
    DetectorRegistry& operator=(const DetectorRegistry&) = delete;
    DetectorRegistry& operator=(DetectorRegistry&&) = delete;

  private:
    DetectorRegistry() = default;

    XALGOS_HD inline const DetectorDescriptor*
    get_detector_internal(std::uint64_t det_hash) const {
      for (ssize_t i = 0; i < m_num_detectors; ++i) {
        if (m_entries[i].hash == det_hash) {
          return &m_entries[i].desc;
        }
      }

      return nullptr;
    }

    std::uint16_t m_num_detectors;
    DetectorEntry m_entries[MaxDetectors];
  };
} // namespace xalgospp

#endif // XALGOSPP_DETECTOR_DETECTOR_HH
