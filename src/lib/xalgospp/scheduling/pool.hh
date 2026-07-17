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

#ifndef XALGOSPP_SCHEDULING_POOL_HH
#define XALGOSPP_SCHEDULING_POOL_HH

#include "xalgospp/scheduling/task.hh"

#include <ncarray/ncarrays.hh>

#include <mutex>
#include <vector>
#include <memory>
#include <iostream>

namespace xalgospp::scheduling {

  /**
   * The ArrayBufferPool provides a pool of NCArrays for workers managed by a scheduler.
   *
   * The pool can be accessed by enqueued Tasks wrapping Algorithm's that require memory.
   * The pool minimizes the need for dynamic allocations and allows for reuse helping
   * with performance.
   *
   * @todo The implementation is currently host only, but is not difficult to move
   *       to an agnostic implementation with tags, like in other areas.
   */
  class ArrayBufferPool : public std::enable_shared_from_this<ArrayBufferPool> {
  public:
    ArrayBufferPool(ssize_t ndim, const ssize_t* shape, ncarray::DType dtype)
      : m_ndim(ndim)
      , m_dtype(dtype)
    {
      for (ssize_t d = 0; d < ndim; ++d) {
        m_shape.push_back(shape[d]);
      }
    }

    ~ArrayBufferPool() {
      std::lock_guard<std::mutex> lock(m_mutex);
      for (auto* ptr : m_free_buffers) {
        delete ptr;
      }
    }

    std::shared_ptr<ncarray::NCArray> acquire() {
      std::lock_guard<std::mutex> lock(m_mutex);
      ncarray::NCArray* raw_ptr { nullptr };

      if (m_free_buffers.empty()) {
        raw_ptr = new ncarray::NCArray(m_ndim, m_shape.data(), m_dtype);
      } else {
        raw_ptr = m_free_buffers.back();
        m_free_buffers.pop_back();
      }

      return
        std::shared_ptr<ncarray::NCArray>(raw_ptr,
                                          [this](ncarray::NCArray* ptr) {
                                            this->release(ptr);
                                          });
    }

  private:
    void release(ncarray::NCArray* ptr) {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_free_buffers.push_back(ptr);
    }

    std::mutex m_mutex;
    std::vector<ncarray::NCArray*> m_free_buffers;

    ssize_t m_ndim;
    std::vector<ssize_t> m_shape;
    ncarray::DType m_dtype;
  };

} // namespace xalgospp::scheduling

#endif // XALGOSPP_SCHEDULING_POOL_HH
