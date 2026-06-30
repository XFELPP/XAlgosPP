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

#include "xalgospp/detector/lcls2/calibdb.hh"

#include "httplib.h"
#include "rapidjson/document.h"
#include "spdlog/spdlog.h"
#include "spdlog/sinks/stdout_color_sinks.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <stdfloat>
#include <string>
#include <string_view>
#include <vector>

namespace xalgospp {
  namespace LCLS2 {
    std::string get_detector_short_name(std::string_view base_url,
                                        std::string_view det_type,
                                        std::string_view det_serial_no) {
      auto logger = spdlog::get("XAlgosPP::Calibrator");
      if (!logger) {
        logger = spdlog::stdout_color_mt("XAlgosPP::Calibrator");
      }

      std::string short_name { "" };

      // May want to use psdmint or others instead
      httplib::Client cli(base_url); // "https://pswww.slac.stanford.edu"

      // The detnames endpoint for looking up detectors by type and serial number
      std::string endpoint { "/calib_ws/cdb_detnames/" + det_type };

      logger->debug("[LCLS2][cdb_detnames] Searching for `shortname` for a {}", det_type);
      logger->trace("[LCLS2][cdb_detnames] Looking for serial number: {}", det_serial_no);

      if (auto res = cli.Get(endpoint)) {
        rapidjson::Document docs;
        docs.Parse(res->body.c_str());
        if (!docs.IsArray()) {
          logger->error("[LCLS2][cdb_detnames] calibdb response was not a list of docs!");
          return short_name;
        }

        for (const auto& doc : docs.GetArray()) {
          if (!doc.IsObject()) {
            continue;
          }

          std::string doc_serial_no { doc["long"].GetString() };
          if (doc_serial_no == det_serial_no) {
            short_name = doc["short"].GetString();
            logger->debug("[LCLS2][cdb_detnames] Found {}", short_name);

            return short_name;
          }
        }
      } else {
        logger->error("Could not find shortname for detector type: {} (Serial No: {})",
                      det_type,
                      det_serial_no);
        return short_name;
      }
    }

    void load_values_from_byte_stream(std::uint8_t* byte_stream,
                                      std::string_view data_dtype,
                                      std::size_t data_nelem,
                                      std::vector<std::uint8_t>& out_buf) {
      auto logger = spdlog::get("XAlgosPP::Calibrator");
      if (!logger) {
        logger = spdlog::stdout_color_mt("XAlgosPP::Calibrator");
      }
      std::string log_id { "ByteLoader" };

      std::size_t nbytes { 0 };
      // Floats are most likely, really float32 and float64. Leave the rest in case
      if (data_dtype == "float16" || data_dtype == "half") {
        logger->debug("[LCLS2][{}] Getting {} float16 values", log_id, data_nelem);
        nbytes = data_nelem * sizeof(std::float16_t);
      } else if (data_dtype == "float32") {
        logger->debug("[LCLS2][{}] Getting {} float32 values", log_id, data_nelem);
        nbytes = data_nelem * sizeof(std::float32_t);
      } else if (data_dtype == "float64") {
        logger->debug("[LCLS2][{}] Getting {} float64 values", log_id, data_nelem);
        nbytes = data_nelem * sizeof(std::float64_t);
      } else if (data_dtype == "float96" || data_dtype == "float128" ||
                 data_dtype == "longdouble") {
        std::size_t real_size{sizeof(long double)};
        logger->debug("[LCLS2][{}] Getting {} float128 values (true size = {})",
                      log_id,
                      data_nelem,
                      real_size);
        nbytes = data_nelem * real_size;

      } else if (data_dtype == "bool" || data_dtype == "bool_") {
        logger->debug("[LCLS2][{}] Getting {} bool values", log_id, data_nelem);
        nbytes = data_nelem * sizeof(bool);
      } else if (data_dtype == "uint8") {
        logger->debug("[LCLS2][{}] Getting {} uint8 values", log_id, data_nelem);
        nbytes = data_nelem * sizeof(std::uint8_t);
      } else if (data_dtype == "uint16") {
        logger->debug("[LCLS2][{}] Getting {} uint16 values", log_id, data_nelem);
        nbytes = data_nelem * sizeof(std::uint16_t);
      } else if (data_dtype == "uint32") {
        logger->debug("[LCLS2][{}] Getting {} uint32 values", log_id, data_nelem);
        nbytes = data_nelem * sizeof(std::uint32_t);
      } else if (data_dtype == "uint64") {
        logger->debug("[LCLS2][{}] Getting {} uint64 values", log_id, data_nelem);
        nbytes = data_nelem * sizeof(std::uint64_t);

      } else if (data_dtype == "int8") {
        logger->debug("[LCLS2][{}] Getting {} int8 values", log_id, data_nelem);
        nbytes = data_nelem * sizeof(std::int8_t);
      } else if (data_dtype == "int16") {
        logger->debug("[LCLS2][{}] Getting {} int16 values", log_id, data_nelem);
        nbytes = data_nelem * sizeof(std::int16_t);
      } else if (data_dtype == "int32") {
        logger->debug("[LCLS2][{}] Getting {} int32 values", log_id, data_nelem);
        nbytes = data_nelem * sizeof(std::int32_t);
      } else if (data_dtype == "int64") {
        logger->debug("[LCLS2][{}] Getting {} int64 values", log_id, data_nelem);
        nbytes = data_nelem * sizeof(std::int64_t);

      } else if (data_dtype == "complex64" || data_dtype == "csingle") {
        logger->debug("[LCLS2][{}] Getting {} complex64 values", log_id, data_nelem);
        nbytes = data_nelem * sizeof(std::float32_t) * 2;
      } else if (data_dtype == "complex128" || data_dtype == "cdouble") {
        logger->debug("[LCLS2][{}] Getting {} complex128 values", log_id, data_nelem);
        nbytes = data_nelem * sizeof(std::float64_t) * 2;
      } else if (data_dtype == "complex192" || data_dtype == "complex256" ||
                 data_dtype == "clongdouble") {
        std::size_t real_size { sizeof(long double) * 2 };
        logger->debug("[LCLS2][{}] Getting {} complex256 values (true size = {})",
                      log_id,
                      data_nelem,
                      real_size);
        nbytes = data_nelem * real_size;
      }

      out_buf.resize(nbytes);
      std::memcpy(out_buf.data(), byte_stream, nbytes);
    }

    void deserialize_json_dict(rapidjson::Value& json_dict,
                               std::map<std::string, CalibrationConstants>& constants_out) {
      if (!json_dict.IsObject()) {
        return;
      }

      auto logger = spdlog::get("XAlgosPP::Calibrator");
      if (!logger) {
        logger = spdlog::stdout_color_mt("XAlgosPP::Calibrator");
      }
      std::string log_id { "JSONDeserialize" };

      for (rapidjson::Value::ConstValueIterator itr = json_dict.Begin(); itr != json_dict.End(); ++itr) {
        if (!itr->IsObject()) {
          continue;
        }

        auto& sub_dict { itr->value.GetObject() };
        if (!sub_dict.HasMember("data")) {
          continue;
        }

        std::string constants_name { itr->name.GetString() };

        const char* data_str { sub_dict["data"].GetString() };
        std::uint8_t* raw_data { reinterpret_cast<std::uint8_t*>(data_str) };

        std::string dict_type;
        if (sub_dict.HasMember("type")) {
          dict_type = sub_dict["type"].GetString();
        }

        std::string dtype;
        if (sub_dict.HasMember("dtype")) {
          dtype = sub_dict["dtype"].GetString();
        }

        std::size_t nelem { 0 };
        if (sub_dict.HasMember("size")) {
          nelem = static_cast<std::size_t>(std::atoi(sub_dict["size"].GetString()));
        }

        std::string shape_str;
        if (sub_dict.HasMember("shape")) {
          shape_str = sub_dict["shape"].GetString();
        }

        if (dict_type == "nd") {
          if (dtype.empty() || nelem == 0 || shape_str.empty()) {
            continue;
          }

          CalibrationConstants constants;
          constants.dtype = dtype;
          auto cast_func = [](const std::string_view sv) {
            return static_cast<std::size_t>(std::stoi(sv));
          };
          constants.shape = split_string(shape_str, ",", cast_func);
          load_values_from_byte_stream(raw_data,
                                       dtype,
                                       nelem,
                                       constants.data);
          constants_out[constants_name] = constants;
        } else if (dict_type == "sc") {
          if (dtype.empty()) {
            continue;
          }
          nelem = 1;

          CalibrationConstants constants;
          constants.dtype = dtype;
          load_values_from_byte_stream(raw_data, dtype, nelem, constants.data);
          constants_out[constants_name] = constants;
        } else {
          // Dictionary....
          deserialize_json_dict(sub_dict, constants_out);
        }
      }
    }

    CalibDocMetadata parse_metadata_doc(rapidjson::Value& meta_doc) {
      CalibDocMetadata metadata;

      // Really --- Here would need to do the run validity checks!!!
      metadata.data_doc_id = meta_doc["id_data"].GetString();

      metadata.doc_type = meta_doc["data_type"].GetString();
      metadata.consts_dtype = meta_doc["data_dtype"].GetString();
      metadata.consts_ndim = static_cast<std::size_t>(std::atoi(meta_doc["data_ndim"].GetString()));
      metadata.data_nelem = static_cast<std::size_t>(std::atoi(meta_doc["data_size"].GetString()));

      std::string data_shape_str { meta_doc["data_shape"].GetString() };
      data_shape_str = data_shape_str.substr(1, data_shape_str.size() - 2);

      auto cast_func = [](const std::string_view sv) {
        return static_cast<std::size_t>(std::stoi(sv));
      };
      metadata.data_shape = split_string(data_shape_str, ",", cast_func);
    }

    //std::pair<std::vector<std::uint8_t>, std::vector<std::size_t>>
    std::map<std::string, CalibrationConstants>
    load_calib_constants_type(std::string_view base_url,
                              std::string_view det_short_name,
                              std::string_view experiment,
                              unsigned run,
                              std::set<std::string> constants_types) {
      auto logger = spdlog::get("XAlgosPP::Calibrator");
      if (!logger) {
        logger = spdlog::stdout_color_mt("XAlgosPP::Calibrator");
      }

      //std::vector<std::uint8_t> constants;   // The final constants data (in bytes)

      // CalibDB API endpoints use the detector "short name"
      if (det_short_name.empty()) {
        logger->error("[LCLS2][load_constants] Cannot load constants without a short name!");
        return std::make_pair(constants, data_shape);
      }

      httplib::Client cli(base_url); // "https://pswww.slac.stanford.edu"

      // Try to use the "experiment" DB first. If nothing found, fallback on "detector" DB
      std::string db_in_use { "cdb_" + experiment };
      std::string endpoint { "/calib_ws/" + db_in_use + "/" + det_short_name };
      logger->debug("[LCLS2][{}] Attempting to get {} constants for {}",
                    endpoint,
                    constants_type,
                    short_name);

      std::vector<CalibDocMetadata> metadata_docs;
      std::string data_doc_id { "" };
      if (auto res = cli.Get(endpoint)) {
        rapidjson::Document docs;
        docs.Parse(res->body.c_str());
        if (!docs.IsArray()) {
          logger->debug("[LCLS2][{}] CalibDB response was not a list of docs - Will try detector DB.",
                        endpoint);
        } else {
          for (const auto& doc : docs.GetArray()) {
            if (!doc.IsObject()) {
              continue;
            }

            std::string doc_constants_type { doc["ctype"].GetString() };
            bool want_ctype { false };
            for (const auto& ct : constants_types) {
              if (doc_constants_type == ct) {
                want_ctype = true;
                break;
              }
            }
            if (!want_ctype) {
              continue;
            }

            // Really --- Here would need to do the run validity checks!!!
            CalibDocMetadata metadata = parse_metadata_doc(doc);
            metadata.constants_name = doc_constants_type;
            logger->debug("[LCLS2][{}] Selected data ID: {}",
                          endpoint,
                          metadata.data_doc_id);
            metadata_docs.push_back(metadata);
          }
        }
      }

      if (metadata_docs.empty()) {
        // Couldn't get anything from experiment endpoint
        db_in_use = "cdb_" + short_name;
        endpoint = "/calib_ws/" + db_in_use + "/" + short_name;
        logger->debug("[LCLS2][{}] Falling back to detector endpoint.", endpoint);

        if (auto res = cli.Get(det_endpoint)) {
          rapidjson::Document docs;
          docs.Parse(res->body.c_str());
          if (!docs.IsArray()) {
            logger->error("[LCLS2][{}] CalibDB response was not a list of docs!",
                          endpoint);
          } else {
            for (const auto& doc : docs.GetArray()) {
              if (!doc.IsObject()) {
                continue;
              }

              std::string doc_constants_type { doc["ctype"].GetString() };
              bool want_ctype { false };
              for (const auto& ct : constants_types) {
                if (doc_constants_type == ct) {
                  want_ctype = true;
                  break;
                }
              }
              if (!want_ctype) {
                continue;
              }

              // Really --- Here would need to do the run validity checks!!!
              CalibDocMetadata metadata = parse_metadata_doc(doc);
              logger->debug("[LCLS2][{}] Selected data ID: {}",
                            endpoint,
                            metadata.data_doc_id);
              metadata_docs.push_back(metadata);
            }
          }
        }
      }

      // Now get the data using data_doc_id
      std::map<std::string, CalibrationConstants> constants_map;
      for (const auto& metadata : metadata_docs) {
        CalibrationConstants constants;
        std::string data_endpoint { "/calib_ws/" + db_in_use + "/gridfs/" + data_doc_id };
        if (auto res = cli.Get(data_endpoint)) {
          auto* raw_data { reinterpret_cast<std::uint8_t*>(res->body.data()) };
          if (data_type == "ndarray") {
            constants.dtype = metadata.consts_dtype;
            constants.shape = metadata.consts_shape;
            load_values_from_byte_stream(raw_data,
                                         metadata.consts_dtype,
                                         metadata.consts_nelem,
                                         constants.data);
            constants_map[metadata.constants_name] = constants;
          } else if (data_type == "str") {
            logger->debug("[LCLS2][{}] Getting string constants", endpoint);
            // ... for whatever reason, XTCAV was handled as a special JSON-string...
            if (metadata.constants_name == "xtcav_lasingoff" ||
                metadata.constants_name == "xtcav_pedestals" ||
                metadata.constants_name == "lasingoffreference") {
              // Deserialize JSON string...
              rapidjson::Document xtcav_json;
              /// Here's to hoping its a real null-terminated string...
              xtcav_json.Parse(reinterpret_cast<const char*>(raw_data));

              deserialize_json_dict(xtcav_json, constants_map);
            } else {
              // Its just a string... We have a char pointer already
              constants.data.resize(data_nelem);
              std::memcpy(constants.data.data(), raw_data, data_nelem);

              constants_map[metadata.constants_name] = constants;
            }
          }
        }
      }

      return constants_map;
    }
  } // namespace LCLS2
} // namespace xalgospp
