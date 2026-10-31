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

#ifndef XALGOSPP_SCHEDULING_TIMING_HH
#define XALGOSPP_SCHEDULING_TIMING_HH

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <vector>

struct FetchTrace {
  std::uint32_t step;
  std::uint32_t fetch_us;
};

static constexpr std::size_t MAX_TRACE_STEPS { 200000 };

inline std::atomic<std::size_t> g_fetch_count { 0 };
inline FetchTrace g_fetch_log[MAX_TRACE_STEPS];

inline std::atomic<std::size_t> g_copy_count { 0 };
inline std::uint32_t g_copy_log[MAX_TRACE_STEPS];

inline std::atomic<std::size_t> g_pop_count{0};
inline std::uint32_t g_pop_log[MAX_TRACE_STEPS];

inline std::atomic<std::size_t> g_comp_count { 0 };
inline std::uint32_t g_comp_log[MAX_TRACE_STEPS];

inline void dump_trace_analysis(std::size_t window_size = 500) {
  std::size_t n_fetch =
    std::min(g_fetch_count.load(std::memory_order_relaxed), MAX_TRACE_STEPS);

  std::size_t n_copy =
    std::min(g_copy_count.load(std::memory_order_relaxed), MAX_TRACE_STEPS);

  std::size_t n_pop =
    std::min(g_pop_count.load(std::memory_order_relaxed), MAX_TRACE_STEPS);

  std::size_t n_comp =
    std::min(g_comp_count.load(std::memory_order_relaxed), MAX_TRACE_STEPS);

  //std::size_t total { std::min(n_fetch, std::min(n_copy, std::min(n_pop, n_comp))) };
  std::size_t total { std::min(n_fetch, std::min(n_pop, n_comp)) };

  if (total == 0) {
    std::cout << "[TimingTrace] No timing samples recorded.\n";
    return;
  }

  std::cout << "\n================ PERFORMANCE TRACE SUMMARY ================\n";
  std::cout << "Total recorded steps: " << total << "\n\n";
  std::cout << "Step Range\t\tAvg Fetch (us)\tAvg Copy (us)\tAvg Pop (us)\tAvg Compute (us)\n";
  for (std::size_t i = 0; i < total; i += window_size) {
    std::size_t end { std::min(i + window_size, total) };
    double sum_fetch { 0 };
    double sum_copy { 0 };
    double sum_pop { 0 };
    double sum_comp { 0 };

    for (std::size_t j = i; j < end; ++j) {
      sum_fetch += g_fetch_log[j].fetch_us;
      sum_copy += g_copy_log[j];
      sum_pop += g_pop_log[j];
      sum_comp  += g_comp_log[j];
    }

    std::size_t count { end - i };
    std::cout << i << " - " << (end - 1) << "\t\t"
              << (sum_fetch / count) << "\t\t"
              << (sum_copy / count) << "\t\t"
              << (sum_pop / count) << "\t\t"
              << (sum_comp / count) << "\n";
  }
  std::cout << "===========================================================\n";

  double global_fetch_sum { 0 };
  double global_copy_sum { 0 };
  double global_pop_sum { 0 };
  double global_comp_sum { 0 };
  for (std::size_t i = 0; i < total; ++i) {
    global_fetch_sum += g_fetch_log[i].fetch_us;
    global_copy_sum += g_copy_log[i];
    global_pop_sum += g_pop_log[i];
    global_comp_sum += g_comp_log[i];
  }

  double mean_fetch { global_fetch_sum / total };
  double mean_copy { global_copy_sum / total };
  double mean_pop { global_pop_sum / total };
  double mean_comp { global_comp_sum / total };

  std::cout << "\n--- Significant Outliers (> 5x Mean Latency) ---\n";
  std::size_t spikes { 0 };
  for (std::size_t i = 0; i < total; ++i) {
    bool fetch_spike =
      g_fetch_log[i].fetch_us > (5 * mean_fetch) && g_fetch_log[i].fetch_us > 1000;

    bool copy_spike =
      g_copy_log[i] > (5 * mean_copy) && g_copy_log[i] > 1000;

    bool pop_spike =
      g_pop_log[i] > (5 * mean_pop) && g_pop_log[i] > 1000;

    bool comp_spike =
      g_comp_log[i] > (5 * mean_comp) && g_comp_log[i] > 1000;

    if (fetch_spike || comp_spike) {
      spikes++;
      if (spikes <= 20) { // Limit output to first 20 spikes
        std::cout << "Step " << g_fetch_log[i].step << ": ";
        if (fetch_spike) {
          std::cout << "FETCH SPIKE = " << g_fetch_log[i].fetch_us << " us ";
        }

        if (copy_spike) {
          std::cout << "COPY SPIKE = " << g_copy_log[i] << " us";
        }

        if (pop_spike) {
          std::cout << "QUEUE POP SPIKE = " << g_pop_log[i] << " us";
        }

        if (comp_spike) {
          std::cout << "COMPUTE SPIKE = " << g_comp_log[i] << " us";
        }

        std::cout << "\n";
      }
    }
  }

  if (spikes == 0) {
    std::cout << "No major outliers detected.\n";
  } else if (spikes > 20) {
    std::cout << "... (" << (spikes - 20) << " more spikes omitted)\n";
  }

  std::cout << "===========================================================\n";
}

#endif // XALGOSPP_SCHEDULING_TIMING_HH
