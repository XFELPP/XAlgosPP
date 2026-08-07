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

#include "xalgospp/utilities/interconversion.hh"
#include "xalgospp/utilities/mongodb.hh"

#include "httplib.h"
#include "rapidjson/document.h"
#include "spdlog/spdlog.h"
#include "spdlog/sinks/stdout_color_sinks.h"

#ifdef _WIN32
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#else
#include <sys/types.h>
#endif

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <stdfloat>
#include <string>
#include <utility>
#include <vector>

namespace xalgospp::lcls2 {

  ncarray::NCArray CalibrationConstants::to_ncarray(const CalibrationConstants& consts) {
    ncarray::DType dtype = string_to_dtype(consts.dtype);

    std::vector<ssize_t> sshape(consts.shape.begin(), consts.shape.end());

    ncarray::NCArray arr(sshape, dtype);
    std::memcpy(arr.data(), consts.data.data(), consts.data.size());

    return arr;
  }

  std::string get_detector_short_name(std::string& base_url,
                                      std::string& det_type,
                                      std::string& det_serial_no) {
    auto logger = spdlog::get("XAlgosPP::LCLS2::CalibDB");
    if (!logger) {
      logger = spdlog::stdout_color_mt("XAlgosPP::LCLS2::CalibDB");
    }

    std::string short_name { "" };

    // May want to use psdmint or others instead
    httplib::Client cli(base_url); // "https://pswww.slac.stanford.edu"

    // The detnames endpoint for looking up detectors by type and serial number
    std::string endpoint { std::string("/calib_ws/cdb_detnames/") + det_type };

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
    return short_name;
  }

  void load_values_from_byte_stream(const std::uint8_t* byte_stream,
                                    const std::string& data_dtype,
                                    std::size_t data_nelem,
                                    std::vector<std::uint8_t>& out_buf) {
    auto logger = spdlog::get("XAlgosPP::LCLS2::CalibDB");
    if (!logger) {
      logger = spdlog::stdout_color_mt("XAlgosPP::LCLS2::CalibDB");
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
    } else if (data_dtype == "complex192" ||
               data_dtype == "complex256" ||
               data_dtype == "clongdouble") {
      std::size_t real_size { sizeof(long double) * 2 };
      logger->debug("[LCLS2][{}] Getting {} complex256 values (true size = {})",
                    log_id,
                    data_nelem,
                    real_size);
      nbytes = data_nelem * real_size;
    } else {
      logger->warn("[LCLS2][{}] Unrecognized dtype {}! Assuming 1-byte sized elemnts!",
                   log_id,
                   data_dtype);
      nbytes = data_nelem;
    }

    out_buf.resize(nbytes);
    std::memcpy(out_buf.data(), byte_stream, nbytes);
  }

  void deserialize_json_dict(rapidjson::Value& json_dict,
                             std::map<std::string, CalibrationConstants>& constants_out) {
    if (!json_dict.IsObject()) {
      return;
    }

    auto logger = spdlog::get("XAlgosPP::LCLS2::CalibDB");
    if (!logger) {
      logger = spdlog::stdout_color_mt("XAlgosPP::LCLS2::CalibDB");
    }
    std::string log_id { "JSONDeserialize" };

    for (auto itr = json_dict.MemberBegin(); itr != json_dict.MemberEnd(); ++itr) {
      if (!itr->value.IsObject()) {
        // We are looking for dictionaries with `data` keys.
        continue;
      }

      auto& sub_dict { itr->value };
      if (!sub_dict.HasMember("data")) {
        // We are looking for dictionaries with `data` keys.
        continue;
      }

      std::string constants_name { itr->name.GetString() };

      const char* data_str { sub_dict["data"].GetString() };
      const std::uint8_t* raw_data { reinterpret_cast<const std::uint8_t*>(data_str) };

      std::string dict_type;
      if (sub_dict.HasMember("type")) {
        dict_type = sub_dict["type"].GetString();
      }

      std::string dtype {
        sub_dict.HasMember("dtype") ? sub_dict["dtype"].GetString() : ""
      };

      std::size_t nelem = sub_dict.HasMember("size")
        ? static_cast<std::size_t>(std::atoi(sub_dict["size"].GetString()))
        : 0;

      std::string shape_str {
        sub_dict.HasMember("shape") ? sub_dict["shape"].GetString() : ""
      };

      if (dict_type == "nd") {
        if (dtype.empty() || nelem == 0 || shape_str.empty()) {
          continue;
        }

        CalibrationConstants constants;
        constants.dtype = dtype;
        auto cast_func = [](const std::string& sv) {
          return static_cast<std::size_t>(std::stoi(sv));
        };
        constants.shape = split_string<std::size_t>(shape_str, ",", cast_func);
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

  CalibDocMetadata parse_metadata_doc(const rapidjson::Value& meta_doc) {
    CalibDocMetadata metadata;

    // Really --- Here would need to do the run validity checks!!!
    metadata.data_doc_id = meta_doc["id_data"].GetString();

    metadata.doc_type = meta_doc["data_type"].GetString();
    metadata.consts_dtype = meta_doc["data_dtype"].GetString();
    metadata.consts_ndim = static_cast<std::size_t>(std::atoi(meta_doc["data_ndim"].GetString()));
    metadata.consts_nelem = static_cast<std::size_t>(std::atoi(meta_doc["data_size"].GetString()));

    std::string data_shape_str { meta_doc["data_shape"].GetString() };
    data_shape_str = data_shape_str.substr(1, data_shape_str.size() - 2); // Strip the enclosing ()

    auto cast_func = [](const std::string& sv) {
      return static_cast<std::size_t>(std::stoi(sv));
    };
    metadata.consts_shape = split_string<std::size_t>(data_shape_str, ",", cast_func);

    if (meta_doc.HasMember("_id")) {
      std::string oid = meta_doc["_id"].GetString();
      metadata.doc_unix_ts = timestamp_from_bson_object_id(oid);
    }

    return metadata;
  }

  std::optional<std::pair<unsigned, unsigned>>
  is_doc_valid_for_run(const rapidjson::Value& metadata_doc,
                       unsigned target_run,
                       bool is_det_db_doc) {
    auto logger = spdlog::get("XAlgosPP::LCLS2::CalibDB");
    if (!logger) {
      logger = spdlog::stdout_color_mt("XAlgosPP::LCLS2::CalibDB");
    }

    auto is_int = [](std::string& sv) -> bool {
      if (sv.empty()) {
        return false;
      }

      int val;
      auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), val);

      return ec == std::errc{} && ptr == sv.data() + sv.size();
    };

    // Check validity
    unsigned run_begin { 0 };
    if (metadata_doc.HasMember("run")) {
      if (metadata_doc["run"].IsInt()) {
        run_begin = static_cast<unsigned>(metadata_doc["run"].GetInt());
      } else if (metadata_doc["run"].IsString()) {
        run_begin = static_cast<unsigned>(std::stoul(metadata_doc["run"].GetString()));
      } else {
        logger->warn("[LCLS2][RunValidity] Skipping document with invalid run!");
        return std::nullopt;
      }

      if (run_begin > LCLS_MAX_EXP_RUN_NUM) {
        logger->debug("[LCLS2][RunValidity] Skipping doc with begin run past max: {}.",
                      run_begin);
        return std::nullopt;
      }
    }

    // For "detector" database documents, don't skip based on run value.
    if (!is_det_db_doc && target_run < run_begin) {
      // If the run_begin starting validity is after this run, skip
      logger->trace("[LCLS2][RunValidity] Skipping doc begin run after target: {} > {}.",
                    run_begin,
                    target_run);
      return std::nullopt;
    }

    unsigned run_end { 0 };
    if (metadata_doc.HasMember("run_end")) {
      std::string run_end_str = metadata_doc["run_end"].GetString();
      if (is_int(run_end_str)) {
        run_end = static_cast<unsigned>(std::stoul(run_end_str));
        if (run_end > LCLS_MAX_EXP_RUN_NUM) {
          logger->debug("[LCLS2][RunValidity] Skipping doc with end run past max: {}.",
                        run_end);
          return std::nullopt;
        }
      } else if (run_end_str == "end") {
        run_end = LCLS_MAX_EXP_RUN_NUM;
      } else {
        logger->debug("[LCLS2][RunValidity] Skipping doc with invalid end run: {}.",
                      run_end_str);
        return std::nullopt;
      }
    }

    // For "detector" database documents, don't skip based on run value.
    if (!is_det_db_doc && run_end < target_run) {
      logger->trace("[LCLS2][RunValidity] Skipping doc end run before target: {} < {}.",
                    run_end,
                    target_run);
      return std::nullopt;
    }

    return std::make_optional(std::make_pair(run_begin, run_end));
  }

  std::map<std::string, CalibrationConstants>
  retrieve_calib_constants_of_type(std::string& base_url,
                                   std::string& det_short_name,
                                   std::string& experiment,
                                   unsigned run,
                                   std::set<std::string> constants_types) {
    auto logger = spdlog::get("XAlgosPP::LCLS2::CalibDB");
    if (!logger) {
      logger = spdlog::stdout_color_mt("XAlgosPP::LCLS2::CalibDB");
    }

    // CalibDB API endpoints use the detector "short name"
    if (det_short_name.empty()) {
      logger->error("[LCLS2][load_constants] Cannot load constants without a short name!");
      return {};
    }

    logger->debug("[LCLS2][load_constants] Trying URL: {}", base_url);

    httplib::Client cli(base_url); // "https://pswww.slac.stanford.edu"

    // Try to use the "experiment" DB first. If nothing found, fallback on "detector" DB
    std::string db_in_use { "cdb_" + experiment };
    std::string endpoint { "/calib_ws/" + db_in_use + "/" + det_short_name };
    logger->debug("[LCLS2][{}] Attempting to get constants for {}",
                  endpoint,
                  det_short_name);
    std::string data_doc_id{""};

    std::map<std::string, CalibrationConstants> constants_map; // Per-ctype constants
    for (const auto& ct : constants_types) {
      logger->debug("------ Looking for constants of type: {}", ct);

      // Procedure for each of the types passed in the inital set:
      // - Select all candidate metadata documents that have valid run range.
      // - Sort those, and select the back (see CalibDocMetadata for sort alg.)
      // - Use the selected metadata's bulk data ID to make final request for constants
      std::vector<CalibDocMetadata> docs_for_ctype;
      if (auto res = cli.Get(endpoint)) {
        if (res->status == 200) {
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

              if (!doc.HasMember("ctype")) {
                logger->debug("[LCLS2][{}] Skipping document without ctype!", endpoint);
                continue;
              }
              std::string doc_constants_type { doc["ctype"].GetString() };

              if (doc_constants_type != ct) {
                continue;
              }

              // Check validity
              auto valid_run_range = is_doc_valid_for_run(doc, run, /*is_det_db_doc=*/false);
              if (!valid_run_range.has_value()) {
                // Not valid range
                continue;
              }

              auto [run_begin, run_end] = *valid_run_range;

              CalibDocMetadata metadata = parse_metadata_doc(doc);
              metadata.consts_name = doc_constants_type;
              metadata.doc_run_begin = run_begin;
              metadata.doc_run_end = run_end;
              docs_for_ctype.push_back(metadata);

              logger->trace("[LCLS2][{}] Possible candidate constants have ID: {}",
                            endpoint,
                            metadata.data_doc_id);
            }
          }
        } else {
          logger->error("[LCLS2][{}] Call to endpoint returned non-200 status {} - skipping!",
                        endpoint,
                        res->status);
        }
      }

      if (docs_for_ctype.empty()) {
        // Couldn't get anything from experiment endpoint
        db_in_use = "cdb_" + det_short_name;
        endpoint = "/calib_ws/" + db_in_use + "/" + det_short_name;
        logger->debug("[LCLS2][{}] Falling back to detector endpoint.", endpoint);

        if (auto res = cli.Get(endpoint)) {
          if (res->status == 200) {
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

                if (doc_constants_type != ct) {
                  continue;
                }

                // Check validity
                auto valid_run_range = is_doc_valid_for_run(doc, run, /*is_det_db_doc=*/true);
                if (!valid_run_range.has_value()) {
                  // Not valid range
                  continue;
                }

                auto [run_begin, run_end] = *valid_run_range;

                CalibDocMetadata metadata = parse_metadata_doc(doc);
                metadata.consts_name = doc_constants_type;
                metadata.doc_run_begin = run_begin;
                metadata.doc_run_end = run_end;
                docs_for_ctype.push_back(metadata);

                logger->trace("[LCLS2][{}] Possible candidate constants have ID: {}",
                              endpoint,
                              metadata.data_doc_id);
              }
            }
          } else {
            logger->error("[LCLS2][{}] Call to endpoint returned non-200 status {} - skipping!",
                          endpoint,
                          res->status);
          }
        }
      }

      CalibDocMetadata final_metadata_doc;
      if (!docs_for_ctype.empty()) {
        // Sort the candidate constants documents
        std::sort(docs_for_ctype.begin(), docs_for_ctype.end());

        final_metadata_doc = docs_for_ctype.back();

        logger->debug("[LCLS2][Validity] Selected best doc for constants type '{}' (begin={}, end={}, ts={})",
                      ct,
                      final_metadata_doc.doc_run_begin,
                      final_metadata_doc.doc_run_end,
                      final_metadata_doc.doc_unix_ts);

        // Now get the data using data_doc_id
        CalibrationConstants constants;
        std::string data_endpoint { "/calib_ws/" + db_in_use + "/gridfs/" + final_metadata_doc.data_doc_id };
        if (auto res = cli.Get(data_endpoint)) {
          if (res->status != 200) {
            logger->error("[LCLS2][{}] Call for data returned non-200 status {} - skipping!",
                          data_endpoint,
                          res->status);
            continue;
          }
          // Store the metadata in the final struct in case its needed
          constants.metadata = final_metadata_doc;

          // Extract the actual data from the HTTP request
          auto* raw_data { reinterpret_cast<std::uint8_t*>(res->body.data()) };
          if (final_metadata_doc.doc_type == "ndarray") {
            constants.dtype = final_metadata_doc.consts_dtype;
            constants.shape = final_metadata_doc.consts_shape;
            load_values_from_byte_stream(raw_data,
                                         final_metadata_doc.consts_dtype,
                                         final_metadata_doc.consts_nelem,
                                         constants.data);
            constants_map[ct] = constants;
          } else if (final_metadata_doc.doc_type == "str") {
            logger->debug("[LCLS2][{}] Getting string constants", data_endpoint);
            // ... for whatever reason, XTCAV was handled as a special JSON-string...
            if (final_metadata_doc.consts_name == "xtcav_lasingoff" ||
                final_metadata_doc.consts_name == "xtcav_pedestals" ||
                final_metadata_doc.consts_name == "lasingoffreference") {
              // Deserialize JSON string...
              rapidjson::Document xtcav_json;
              /// Here's to hoping its a real null-terminated string...
              xtcav_json.Parse(reinterpret_cast<const char*>(raw_data));

              deserialize_json_dict(xtcav_json, constants_map);
            } else {
              // Its just a string... We have a char pointer already
              constants.data.resize(final_metadata_doc.consts_nelem);
              std::memcpy(constants.data.data(), raw_data, final_metadata_doc.consts_nelem);

              constants_map[ct] = constants;
            }
          }

          logger->info("[LCLS2][{}] Retrieved constants for '{}'", endpoint, ct);
        }
      } else {
        logger->warn("[LCLS2][Validity] No valid calibration document found for ctype '{}' at run {}",
                     ct,
                     run);
      }
    }

    return constants_map;
  }
} // namespace xalgospp::lcls2
