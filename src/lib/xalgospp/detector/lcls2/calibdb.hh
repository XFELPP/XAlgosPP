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

#ifndef XALGOSPP_DETECTOR_LCLS2_CALIBDB_HH
#define XALGOSPP_DETECTOR_LCLS2_CALIBDB_HH

#include "httplib.h"
#include "rapidjson/document.h"
#include "spdlog/spdlog.h"

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace xalgospp::lcls2 {
  /**
   * This is currently the maximum run number for an LCLS experiment.
   * The calibration database uses this as the cutoff when checking run validity
   * ranges for retrieved constants.
   */
  static constexpr unsigned LCLS_MAX_EXP_RUN_NUM { 9999 };

  /**
   * A descriptor for a single kind of calibration constants.
   *
   * This struct is used when retrieving raw constants from the LCLS2 calibration
   * database endpoints. It holds the raw bytes along with information on the
   * underlying datatype and the shape the bytes should be reformed into.
   */
  struct CalibrationConstants {
    std::vector<std::uint8_t> data; ///< Raw byte stream
    std::string dtype;              ///< The datatype of each element
    std::vector<std::size_t> shape; ///< The final shape the byte stream should take
  };

  /**
   * Retains information on calibration constants retrieved from metadata queries.
   *
   * The calibration constants are stored across two different documents in MongoDB.
   * The first document has metadata about the constants, including an id. This points
   * to a gridfs entry that holds the bulk data. The rest of the associated metadata
   * aids in interpreting the bulk data.
   */
  struct CalibDocMetadata {
    std::uint32_t doc_unix_ts { 0 };       ///< Unix timestamp for the document
    unsigned doc_run_begin { 0 };          ///< The starting run of validity (for sorting)
    unsigned doc_run_end { 0 };            ///< The ending run of validity (for sorting)
    std::string data_doc_id { "" };        ///< Pointer for bulk data gridfs lookup
    std::string doc_type { "" };           ///< The kind of constants (e.g. ndarray, string)
    std::string consts_name { "" };        ///< The constants type name (pedestals, gain etc)
    std::string consts_dtype { "" };       ///< The element type (e.g. if ndarray, uint8 etc.)
    std::size_t consts_ndim { 0 };         ///< Dimensionality of the constants
    std::size_t consts_nelem { 0 };        ///< Total number of elements in the constants
    std::vector<std::size_t> consts_shape; ///< Final shape of the constants

    /**
     * Comparison operator for selecting the most appropriate calibration constants.
     *
     * From a set of calibration constants that are all valid for a given run,
     * the following procedure is used for sorting them in order of priority:
     * 1. Choose the constants that have a higher beginning run validity
     *    -- Presumably more recently applicable
     * 2. Choose the constants with lower end run validity - i.e., choose constants
     *    that are more specific.
     * 3. Choose the document acquired more recently in terms of Unix TS.
     *
     * @param[in] other The other CalibDocMetadata to compare with this one.
     * @returns Whether this doc is less -- for sorting in ascending order.
     */
    bool operator<(const CalibDocMetadata& other) const {
      if (doc_run_begin != other.doc_run_begin) {
        return doc_run_begin < other.doc_run_begin;
      }

      if (doc_run_end != other.doc_run_end) {
        return doc_run_end > other.doc_run_end;
      }

      return doc_unix_ts < other.doc_unix_ts;
    }
  };

  /**
   * Parse a metadata document retrieved from the calibdb.
   *
   * The metadata documents have the following schema (among others):
   * - id_data (string): gridfs identifier for bulk data
   * - data_type (string): Whether the constants are an array, string, or so on.
   * - data_dtype (string): The datatype of the constants elements.
   * - data_ndim (integer): The dimensionality of the constants data.
   * - data_size (integer): The total number of elements in the constants.
   * - data_shape (string): A serialized string of the constants shape. E.g. '(1, 2, 3)'
   *
   * The metadata documents have additional fields such as various timestamps,
   * the commands used for creating the constants and so on. Not all of these
   * are relevant for our current use.
   *
   * @param[in] meta_doc The document with metadata retrieved from CalibDB.
   * @returns The parsed struct with relevant metadata.
   */
  CalibDocMetadata parse_metadata_doc(rapidjson::Value& meta_doc);

  /**
   * Retrieve the `short name` for a detector of given type using its serial number.
   *
   * The `short name` is used for all CalibDB API requests, so its retrieval is
   * the first step in retrieving constants of any type.
   *
   * @param[in] base_url The main host/URL to use for CalibDB access.
   * @param[in] det_type The detector type. E.g. epixuhr3x2, jungfrau.
   * @param[in] det_serial_no The full serial number of the specific detector.
   * @returns The `short name` of the indicated detector.
   */
  std::string get_detector_short_name(std::string_view base_url,
                                      std::string_view det_type,
                                      std::string_view det_serial_no);

  /**
   * Extract the data from the raw byte stream given the provided metadata.
   *
   * This function is intended to traverse the bulk data from a gridfs request.
   * It must be provided with certain metadata that is retrieved from a separate
   * API call than the bulk data call.
   *
   * @param[in] byte_stream The raw bytes from the CalibDB HTTP request.
   * @param[in] data_dtype The element datatype from the metadata doc.
   * @param[in] data_nelem The total number of elements from the metadata doc.
   * @param[out] out_buf The buffer to copy the bytes to.
   */
  void load_values_from_byte_stream(const std::uint8_t* byte_stream,
                                    std::string_view data_dtype,
                                    std::size_t data_nelem,
                                    std::vector<std::uint8_t>& out_buf);

  /**
   * Split a string and peform an operation on it.
   *
   * @tparam T The type to populate the output vector with.
   * @tparam Fn The operation to perform on the string parts.
   * @param[in] s The string to split.
   * @param[in] delim The delimiter to use to split s.
   * @param[in] cast The operation to use on each part of the split string.
   * @returns The vector of split string components.
   */
  template <typename T, class Fn>
  std::vector<T> split_string(const std::string_view s,
                              const std::string_view delim,
                              Fn&& cast) {
    std::vector<T> parts;
    std::size_t nextPos { 0 };
    std::size_t lastPos { 0 };

    std::string part;
    while ((nextPos = s.find(delim, lastPos)) != std::string::npos) {
      part = s.substr(lastPos, nextPos - lastPos);
      if (!part.empty()) {
        parts.push_back(part);
      }
      lastPos = nextPos + 1;
    }

    part = s.substr(lastPos);
    parts.push_back(cast(part));
    return parts;
  }

  /**
   * Recursively traverse a CalibDB JSON dict, deserializing as it goes.
   *
   * Some calibration constants are serialized (even multiple times) as nested
   * dictionaries, encoded as JSON strings. This is done, for example, with the
   * XTCAV constants (at least at some points in history). This function will
   * recurse the dictionary adding all parts to the output constants map.
   *
   * The caller should check whether this is an appropraite function to call based
   * on the constants type. Alternatively, a simpler, single call to the load function
   * may be appropriate.
   *
   * @param[in] json_dict The JSON dictionary retrieved from the CalibDB API request.
   * @param[out] constants The map to store the deserialized constants.
   */
  void deserialize_json_dict(rapidjson::Value& json_dict,
                             std::map<std::string, CalibrationConstants>& constants);

  /**
   * For the provided short name get most recent valid constants for the experiment/run.
   *
   * This function will try to find the most recent constants of the provided type(s)
   * for the experiment. It returns a map, as multiple constants can be requested,
   * or in some cases, a single specified type will lead to a nested series of
   * JSON dicts. In that case, the keys will be populated into the map.
   * An empty map will be returned if no constants are found for any requested types.
   *
   * @param[in] base_url The main host/URL to use for CalibDB access.
   * @param[in] det_short_name The `short name` of the indicated detector.
   * @param[in] experiment The experiment. If no constants are in the experiment DB,
   *            the more general detector database will also be searched.
   * @param[in] run The run number.
   * @param[in] constants_types The type of constants being looked for.
   */
  std::map<std::string, CalibrationConstants>
  retrieve_calib_constants_of_type(std::string_view base_url,
                                   std::string_view det_short_name,
                                   std::string_view experiment,
                                   unsigned run,
                                   std::set<std::string> constants_type);

} // namespace xalgospp::lcls2

#endif // XALGOSPP_DETECTOR_LCLS2_CALIBDB_HH
