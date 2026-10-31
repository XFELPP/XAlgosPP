#include "xalgospp/detector/calibration.hh"
#include "xalgospp/scheduling/dag_scheduler.hh"
#include "xalgospp/scheduling/staging.hh"
#include "xalgospp/scheduling/tasks/analysis.hh"
#include "xalgospp/scheduling/tasks/io.hh"
#include "xalgospp/scheduling/timing.hh"

#include <sbio/core/datasource.hh>
#include <sbio/formats/xtc2/xtc2_locator.hh>
#include <sbio/formats/xtc2/xtc2_traits.hh>
#include <sbio/execution/mpi.hh>
#include <sbio/execution/mpi_threaded.hh>
#include <sbio/execution/threaded.hh>
#include <sbio/io/posix.hh>

#include <mpi.h>
#include <ncarray/ncarrays.hh>
#include <ncarray/soarrays.hh>

#include <unistd.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <map>
#include <string>
#include <unordered_set>
#include <vector>

void usage(char* progname) {
  std::cerr << "Usage: " << progname
    // --- Scheduler settings --- //
            << " [-a <autotune>] [-b <back pressure>] [-m <max_hm>] [-s <shmem_type>]"
            << " [-t <threads>] [-l <lanes>]"
    // --- Fake Data or Real Data --- //
            << " [[-d <det_name>] -e <experiment> -r <run_num> [-o <off_per_read>]]"
            << " [-f [-n <niter>]]"
    // --- Printing, help etc. --- //
            << " [-h] [-p <interval>]" << std::endl
            << std::endl
            << R"a(
Test the DagScheduler for the given configuration.

Args:
  -a <autotune>      Whether to autotune. If 1, the other scheduler params are ignored
                     except backpressure settings. Default: 1.
  -b <back pressure> Enable dynamic back pressure throttling. Default: 1.
  -d <det_name>      Detector name. Default: `jungfrau`
  -e <experiment>    Experiment to load data from.
  -f                 Use fake generated data.
  -g <batch size>    Size of contiguous batch to request. Steps still handed out
                     1 at a time. Default: 1.
  -h                 Display help and exit.
  -l <lanes>         Number of IO lanes (concurrent generaotrs). 0 = one per worker. Default: 0.
  -m <max_hm>        Max concurrent high-memory-bandwidth tasks. Default: 2.
  -n <niter>         Number of iterations for timing. Default: 100.
  -o <off_read>      Offsets to fetch per .smd.xtc2 read. Default: 40000.
  -p <interval>      Print a message every <interval> steps. If 0, don't print. Default: 0.
  -r <run_number>    Run to load data from.
  -s <shmem_type>    The granularity of the shared memory allocation for constants:
                       - 0 = Machine/Node.
                       - 1 = Per-socket.
                       - 2 = Per NUMA domain
                       - 3 = Per L3 Cache
                       - 4 = Per L2 Cache
                     Default: 0 --> 1 copy of constants per node/machine.
  -t <threads>       Threads per node. Default: 0.
  -x <MiB>           Max buffer size in MiB. Default: 16 MiB.
)a" << std::endl;
}

using namespace sbio;

int main(int argc, char* argv[]) {
  char c;

  std::string det_name { "jungfrau" };
  std::string experiment;
  std::size_t max_concurrent_hm { 2 };
  unsigned events_per_read { 40000 };
  unsigned run_num { 0 };
  std::size_t threads_per_node { 0 };
  std::size_t num_lanes { 0 };
  unsigned n_iter { 100 };
  bool autotune { true };
  bool backpressure { true };

  std::size_t batch_size { 1 };

  unsigned print_interval { 0 };
  bool use_fake_generator { false };

  std::size_t max_buffer_size_mib { 16 };

  using xalgospp::scheduling::ShmemType;

  ShmemType shmem_type { ShmemType::MACHINE };

  while ((c = getopt(argc, argv, "a:b:d:e:fg:hi:l:m:n:o:p:r:s:t:x:")) != -1) {
    switch (c) {
    case 'a': {
      autotune = static_cast<bool>(std::atoi(optarg));
      break;
    }
    case 'b': {
      backpressure = static_cast<bool>(std::atoi(optarg));
      break;
    }
    case 'd': {
      det_name = optarg;
      break;
    }
    case 'e': {
      experiment = optarg;
      break;
    }
    case 'f': {
      use_fake_generator = true;
      break;
    }
    case 'g': {
      batch_size = static_cast<std::size_t>(std::atoi(optarg));
      break;
    }
    case 'h': {
      usage(argv[0]);
      exit(0);
    }
    case 'l': {
      num_lanes = static_cast<std::size_t>(std::atoi(optarg));
      break;
    }
    case 'm': {
      max_concurrent_hm = static_cast<std::size_t>(std::atoi(optarg));
      break;
    }
    case 'n': {
      n_iter = static_cast<unsigned>(std::atoi(optarg));
      break;
    }
    case 'o': {
      events_per_read = static_cast<unsigned>(std::atoi(optarg));
      break;
    }
    case 'p': {
      print_interval = static_cast<unsigned>(std::atoi(optarg));
      break;
    }
    case 'r': {
      run_num = static_cast<unsigned>(std::atoi(optarg));
      break;
    }
    case 's': {
      int st { std::atoi(optarg) };
      if (st < 0 || st > 4) {
        std::cerr << "Shared memory granarity must be between [0, 4]!"
                  << std::endl << std::endl;
        usage(argv[0]);
        exit(0);
      }
      shmem_type = static_cast<ShmemType>(std::atoi(optarg));
      break;
    }
    case 't': {
      threads_per_node = static_cast<std::size_t>(std::atoi(optarg));
      break;
    }
    case 'x': {
      max_buffer_size_mib = static_cast<std::size_t>(std::atoi(optarg));
      break;
    }
    default: {
      usage(argv[0]);
      exit(-1);
    }
    }
  }

  int provided;
  MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
  if (provided < MPI_THREAD_MULTIPLE) {
    // Need to check if you get the thread_multiple or not.
    std::cout << "Was not able to provide full MPI thread support." << std::endl
              << "- Support was provied at level: " << provided << std::endl;
  }


  int rank;
  int size;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);

  using MPIDataSource = DataSource<
    SyncPOSIXIO,
    MPIThreadedExecution,
    XTC2Traits
  >;

  using SerialDataSource = DataSource<
    SyncPOSIXIO,
    ThreadedExecution,
    XTC2Traits
  >;

  {
    // ---- Setup configuration for the IO DataSource ---- //
    MPIDataSource ds;
    // SerialDataSource ds;

    sbio::GenericStreamConfig<sbio::XTC2Traits> base_cfg;
    base_cfg.index_batch_size = events_per_read;
    base_cfg.max_buffer_size = max_buffer_size_mib << 20;

    if (!ds.load_source(base_cfg, experiment, run_num)) {
      std::cout << "[Main] Did not find any data streams for "
                << experiment << " (run " << run_num << ")"
                << std::endl;
    }

    ds.discover_metadata();
    auto det = ds.get_stream_group(det_name.c_str());

    std::string det_serial_number;
    std::map<std::string, std::unordered_set<std::string>> unique_metadata;
    std::map<std::uint32_t, std::string> segment_serial_nos;
    std::string detector_type = det.group_name();

    using BrokerType = std::decay_t<decltype(*det.stream_brokers()[0])>;
    using InvT = typename BrokerType::StreamMetadata;

    for (std::size_t i = 0; i < det.num_stream_brokers(); ++i) {
      auto& inv {
        const_cast<const InvT&>(det.stream_brokers()[i]->metadata())
      };

      for (const auto& group : inv.groups()) {
        const auto& grp_key { group.key };

        if (grp_key.group_name == det_name) {
          segment_serial_nos[grp_key.segment] = grp_key["serial_number"];

          if (detector_type.empty()) {
            detector_type = grp_key.group_type;
          }

          std::uint32_t gid { group.group_id };
          for (const auto& field : inv.fields()) {
            if (field.key.group_id == gid) {
              std::string working_alg_name;
              std::string working_field_name;

              if constexpr (inv.KeyCount == 2) {
                working_alg_name = field.key.keys[0];
                working_field_name = field.key.keys[1];
              } else {
                working_alg_name = "raw";
                working_field_name = field.key.keys[0];
              }
            }
          }
        }
      }
    }

    if (!segment_serial_nos.empty()) {
      det_serial_number = detector_type + "_";
      bool first { true };

      for (const auto& [seg, ser] : segment_serial_nos) {
        if (!first) {
          det_serial_number += "_";
        }

        det_serial_number += ser;
        first = false;
      }
    }

    std::map<std::string, std::vector<std::string>> results;
    for (auto& [alg, fields] : unique_metadata) {
      results[alg].assign(fields.begin(), fields.end());
    }

    // ---- Setup configuration for the scheduler ---- //
    xalgospp::scheduling::DagScheduler::Config scheduler_cfg {
      /* num_numa_nodes                   = */ 0,                 // auto-detect
      /* threads_per_node                 = */ threads_per_node,  // 0 would be auto-detect
      /* enable_pinning                   = */ true,
      /* max_concurrent_high_mem          = */ max_concurrent_hm,
      /* max_concurrency_multiplier       = */ 32,
      /* enable_dynamic_backpressure      = */ backpressure,
      /* enable_autotuning                = */ autotune,
      /* raw_frame_size_bytes             = */ 0,                 // auto-detect
      /* warmup_submissions               = */ 10,
      /* node_memory_bandwidth_limit_gbps = */ 50.0,
      /* percent_bandwidth_is_high_mem    = */ 0.25
    };

    auto fetcher = [&det, print_interval](typename XTC2Traits::StepIdxType idx) {
      if (print_interval && (idx % print_interval == 0)) {
        std::cout << "Processing step: " << idx << std::endl;
      }

      auto t0 { std::chrono::high_resolution_clock::now() };

      using namespace sbio::literals;
      auto arr = det.get_data(idx, "raw"_alg, "raw"_field);

      auto t1 { std::chrono::high_resolution_clock::now() };

      std::size_t slot { g_fetch_count.fetch_add(1, std::memory_order_relaxed) };
      if (slot < MAX_TRACE_STEPS) {
        g_fetch_log[slot] = {
          static_cast<std::uint32_t>(idx),
          static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count())
        };
      }

      return arr;
    };

    // ---- Setup configuration for the Calibration algorithm ---- //
    // using Calibrator = xalgospp::det::Calibration<xalgospp::det::RuntimeCalibPolicy, ncarray::HostTag>;
    using Calibrator = xalgospp::det::Calibration<xalgospp::det::JungfrauPolicy, ncarray::HostTag>;
    Calibrator::Params params;
    params.gain_shift = 14;
    params.base_url = "https://pswww.slac.stanford.edu";
    params.gain_mask = 0x3;
    params.data_mask = 0x3FFF;
    params.num_gains = 3;
    params.invalid_pattern = 2;
    params.invalid_value = std::numeric_limits<float>::quiet_NaN();
    params.mapping = xalgospp::det::CalibParameters::MappingMode::Direct;
    params.run = run_num;
    params.experiment = experiment;
    params.det_type = detector_type;
    params.det_serial_no = det_serial_number;

    auto calib_algo = std::make_shared<Calibrator>(params);

    // We used the real datasource to get constants. If generating fake data, setup
    // now
    using xalgospp::scheduling::DummyDataSource;
    xalgospp::scheduling::DummyDataSource fake_ds(n_iter);

    {
      // ---- Prepare and launch the workflow ---- //
      xalgospp::scheduling::DagScheduler scheduler(scheduler_cfg);

      scheduler.stage_algorithm(*calib_algo, shmem_type);

      scheduler.check_memory_bandwidth();

      //std::size_t frame_count { 0 };
      std::atomic<std::size_t> frame_count { 0 };

      ssize_t ndim { 3 };
      ssize_t shape[3] { 32, 512, 1024 };

      std::size_t num_elements { 32 * 512 * 1024 };
      std::size_t total_bytes { num_elements * sizeof(std::uint16_t) };

      if (use_fake_generator) {
        auto random_reader =
            std::make_shared<xalgospp::scheduling::RandomDeviceReader>("/dev/urandom");

        auto fake_fetcher = [&](std::size_t idx) {
          thread_local ncarray::SOArray arr(ndim, shape, ncarray::DType::uint16);
          (void)idx;
          random_reader->read_bytes(arr.data(), total_bytes);

          auto fake_view = arr.view();
          return fake_view;
        };

        auto builder = [&] (std::size_t idx) {
          frame_count.fetch_add(1, std::memory_order_relaxed);

          auto read_task {
            xalgospp::scheduling::make_read_image_task(fake_ds, fake_fetcher, idx)
          };

          using AlgTask = xalgospp::scheduling::AlgorithmTask<
            Calibrator,
            typename decltype(read_task)::element_type
          >;

          auto calib_task = std::make_shared<AlgTask>(scheduler, calib_algo, read_task);

          calib_task->add_dependency(read_task);

          return std::vector<std::shared_ptr<xalgospp::scheduling::Task>>{read_task, calib_task};
        };

        auto start = std::chrono::high_resolution_clock::now();
        const std::size_t lanes { num_lanes != 0 ? num_lanes : scheduler.num_workers() };
        for (std::size_t lane = 0; lane < lanes; ++lane) {
          scheduler.submit_dag({
            std::make_shared<xalgospp::scheduling::IOGeneratorTask<decltype(fake_ds)>>(scheduler,
                                                                                       fake_ds,
                                                                                       builder,
                                                                                       batch_size)
          });
        }

        scheduler.wait_all();
        auto end = std::chrono::high_resolution_clock::now();

        std::chrono::duration<double> diff { end - start };
        double total_time_s { diff.count() };

        double per_evt_time_s { total_time_s / frame_count };
        double per_evt_rate_hz { 1 / per_evt_time_s };

        std::cout << "Successfully processed " << frame_count << " frames with DagScheduler." << std::endl
                  << " - Processing took: " << total_time_s << " seconds." << std::endl
                  << " - Per event time was: " << per_evt_time_s << " seconds" << std::endl
                  << " - Per event rate was: " << per_evt_rate_hz << " events/s" << std::endl;
      } else {

        auto builder = [&](typename XTC2Traits::StepIdxType idx) {
          if (static_cast<std::size_t>(idx) >= n_iter) {
            return std::vector<std::shared_ptr<xalgospp::scheduling::Task>> {};
          }

          frame_count++;

          auto t0 = std::chrono::high_resolution_clock::now();
          auto read_task { xalgospp::scheduling::make_read_image_task(ds, fetcher, idx) };
          using AlgTask = xalgospp::scheduling::AlgorithmTask<
            Calibrator,
            typename decltype(read_task)::element_type
          >;

          auto calib_task = std::make_shared<AlgTask>(scheduler, calib_algo, read_task);

          calib_task->add_dependency(read_task);

          return std::vector<std::shared_ptr<xalgospp::scheduling::Task>>{read_task, calib_task};
        };

        auto start = std::chrono::high_resolution_clock::now();
        const std::size_t lanes { num_lanes != 0 ? num_lanes : scheduler.num_workers() };
        for (std::size_t lane = 0; lane < lanes; ++lane) {
          scheduler.submit_dag({
            std::make_shared<xalgospp::scheduling::IOGeneratorTask<decltype(ds)>>(scheduler,
                                                                                  ds,
                                                                                  builder,
                                                                                  batch_size)
          });
        }

        scheduler.wait_all();
        auto end = std::chrono::high_resolution_clock::now();

        std::chrono::duration<double> diff { end - start };
        double total_time_s { diff.count() };

        double per_evt_time_s { total_time_s / frame_count };
        double per_evt_rate_hz { 1 / per_evt_time_s };

        std::cout << "Successfully processed " << frame_count << " frames with DagScheduler." << std::endl
                  << " - Processing took: " << total_time_s << " seconds." << std::endl
                  << " - Per event time was: " << per_evt_time_s << " seconds" << std::endl
                  << " - Per event rate was: " << per_evt_rate_hz << " events/s" << std::endl;

        dump_trace_analysis();
      }
    }
  }

  MPI_Finalize();

  return 0;
}
