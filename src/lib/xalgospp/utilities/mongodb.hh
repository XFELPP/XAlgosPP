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

#ifndef XALGOSPP_UTILITIES_MONGODB_HH
#define XALGOSPP_UTILITIES_MONGODB_HH

#include "xalgospp/export_macro.hh"

#include <cstdint>
#include <string>

namespace xalgospp {
  /**
   * Return the timestamp from a MongoDB object ID.
   *
   * The timestamps from MongoDB are stored as a big-endian Unix timestamp.
   * The first 4 bytes of the ID contain it.
   */
  XALG_API std::uint32_t timestamp_from_bson_object_id(std::string& oid);
} // namespace xalgospp

#endif // XALGOSPP_UTILITIES_MONGODB_HH
