#include "xalgospp/detector/calibration.hh"
#include "xalgospp/scheduling/dag_scheduler.hh"
#include "xalgospp/scheduling/staging.hh"
#include "xalgospp/scheduling/tasks/analysis.hh"
#include "xalgospp/scheduling/tasks/io.hh"

#include <sbio/core/datasource.hh>
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
            << " [-t <threads>]"
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
  -h                 Display help and exit.
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
  unsigned n_iter { 100 };
  bool autotune { true };
  bool backpressure { true };

  unsigned print_interval { 0 };
  bool use_fake_generator { false };

  using xalgospp::scheduling::ShmemType;

  ShmemType shmem_type { ShmemType::MACHINE };

  while ((c = getopt(argc, argv, "a:b:d:e:fhm:n:o:p:r:s:t:")) != -1) {
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
    case 'h': {
      usage(argv[0]);
      exit(0);
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
    default: {
      usage(argv[0]);
      exit(-1);
    }
    }
  }

  //MPI_Init(&argc, &argv);
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
    //SerialDataSource ds;
    XTC2Traits::StreamParameters base_cfg;
    base_cfg.events_per_read = events_per_read;
    base_cfg.max_dgram_size = 0x4000000;

    if (!ds.load_run(experiment, run_num, base_cfg)) {
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

    using BrokerT = std::decay_t<decltype(*det.stream_brokers()[0])>;
    using InvT = typename BrokerT::MetadataInventory;

    for (std::size_t i = 0; i < det.num_stream_brokers(); ++i) {
      auto& inv {
        const_cast<const InvT&>(det.stream_brokers()[i]->metadata())
      };

      for (std::size_t j = 0; j < inv.m_names_id_count; ++j) {
        auto const& key { inv.m_names_id_table[j].key };

        if (std::strcmp(key.detname, det_name.c_str()) == 0) {
          std::uint32_t nid { inv.m_names_id_table[j].names_id };

          segment_serial_nos[key.segment] = key.detId;
          if (detector_type.empty()) {
            detector_type = key.dettype;
          }

          for (std::size_t k = 0; k < inv.m_field_count; ++k) {
            if (inv.m_field_table[k].key.names_id == nid) {
              std::string working_alg_name;
              if constexpr (requires { key.algname; }) {
                working_alg_name = key.algname;
              } else {
                working_alg_name = "raw";
              }
              unique_metadata[working_alg_name].insert(inv.m_field_table[k].key.fieldname);
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

      return det.get_data(idx, "raw", "raw");
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

        auto circular_buffers = std::make_shared<std::vector<ncarray::SOArray>>();
        for (int i = 0; i < 4; ++i) {
          circular_buffers->emplace_back(ndim, shape, ncarray::DType::uint16);
        }

        //auto fake_fetcher = [random_reader, circular_buffers, total_bytes](std::size_t step_idx) {
        auto fake_fetcher = [&](std::size_t idx) {
          auto& arr = (*circular_buffers)[idx % circular_buffers->size()];
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
        auto init_gen =
          std::make_shared<xalgospp::scheduling::IOGeneratorTask<decltype(fake_ds)>>(scheduler,
                                                                                     fake_ds,
                                                                                     builder);
        scheduler.submit_dag({ init_gen });

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
          frame_count++;

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
        auto init_gen =
          std::make_shared<xalgospp::scheduling::IOGeneratorTask<decltype(ds)>>(scheduler,
                                                                                ds,
                                                                                builder);
        scheduler.submit_dag({ init_gen });

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
      }
    }
  }

  MPI_Finalize();

  return 0;
}
