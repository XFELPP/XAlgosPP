import argparse
import time
from typing import Callable, List, Optional

import mpi4py

mpi4py.rc.thread_level = "multiple"
from mpi4py import MPI

import ncarray
import sbio
import xalgospp


def main() -> None:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(
        description="Python DagScheduler Test for XAlgosPP"
    )
    parser.add_argument(
        "-a", "--autotune", type=int, default=1, help="Autotune (1=true, 0=false)"
    )
    parser.add_argument(
        "-b",
        "--backpressure",
        type=int,
        default=1,
        help="Backpressure (1=true, 0=false)",
    )
    parser.add_argument(
        "-d", "--detector", type=str, default="jungfrau", help="Detector name"
    )
    parser.add_argument(
        "-e",
        "--experiment",
        type=str,
        required=True,
        help="Experiment name (e.g. mfx101210926)",
    )
    parser.add_argument(
        "-m", "--max-hm", type=int, default=2, help="Max concurrent high-memory tasks"
    )
    parser.add_argument(
        "-n",
        "--num_events",
        type=int,
        default=0,
        help="Break after N events.",
    )
    parser.add_argument(
        "-o", "--off-per-read", type=int, default=40000, help="Events per read"
    )
    parser.add_argument(
        "-p", "--print", type=int, default=0, help="Print a counter every N steps."
    )
    parser.add_argument(
        "-r", "--run", type=int, required=True, help="Run number (e.g. 293)"
    )
    parser.add_argument(
        "-t", "--threads", type=int, default=0, help="Threads per node (0 = autodetect)"
    )
    args: argparse.Namespace = parser.parse_args()

    comm = MPI.COMM_WORLD
    rank = comm.Get_rank()
    size = comm.Get_size()

    ds: sbio.DataSource = sbio.DataSource(
        exp=args.experiment,
        run=args.run,
        events_per_read=args.off_per_read,
        max_dgram_size=0x4000000,  # ~67MB
        xtc_ver=2,
        ds_type="mpi_threads",
    )

    det: sbio.Detector = ds.detector(args.detector)
    det_serial_number: str = det.serial_number
    det_type: str = det.detector_type

    if rank == 0:
        print(
            f"[MPI Main] Detector: {args.detector} | Type: {det_type} | Serial: {det_serial_number}"
        )

    scheduler_cfg: xalgospp.DagSchedulerConfig = xalgospp.DagSchedulerConfig()
    scheduler_cfg.num_numa_nodes = 0  # Auto-detect
    scheduler_cfg.threads_per_node = args.threads  # 0 = auto-detect
    scheduler_cfg.enable_pinning = True
    scheduler_cfg.max_concurrent_high_mem = args.max_hm
    scheduler_cfg.max_concurrency_multiplier = 32
    scheduler_cfg.enable_dynamic_backpressure = bool(args.backpressure)
    scheduler_cfg.enable_autotuning = bool(args.autotune)
    scheduler_cfg.warmup_submissions = 10
    scheduler_cfg.node_memory_bandwidth_limit_gbps = 50.0
    scheduler_cfg.percent_bandwidth_is_high_mem = 0.25

    scheduler: xalgospp.DagScheduler = xalgospp.DagScheduler(scheduler_cfg)
    scheduler.check_memory_bandwidth()

    params: xalgospp.CalibrationParams = xalgospp.CalibrationParams()
    params.gain_shift = 14
    params.base_url = "https://pswww.slac.stanford.edu"
    params.gain_mask = 0x3
    params.data_mask = 0x3FFF
    params.num_gains = 3
    params.invalid_pattern = 2
    params.invalid_value = float("nan")
    params.mapping = xalgospp.MappingMode.Direct
    params.run = args.run
    params.experiment = args.experiment
    params.det_type = det_type
    params.det_serial_no = det_serial_number

    calib_algo: xalgospp.Calibration = xalgospp.Calibration(params)

    scheduler.stage_algorithm(calib_algo, xalgospp.ShmemType.SOCKET)

    class ReadTask(xalgospp.Task):
        def __init__(self, step_idx: int):
            super().__init__()
            self.step_idx: int = step_idx
            self.data: Optional[ncarray.SOArrayView] = None

            reqs: xalgospp.ResourceRequirements = xalgospp.ResourceRequirements()
            reqs.memory_intensity = 2
            self.set_resources(reqs)

        def execute(self):
            self.data = det.get_data(self.step_idx, "raw", "raw")

    class CalibTask(xalgospp.Task):
        def __init__(self, read_task: xalgospp.Task, scheduler: xalgospp.DagScheduler):
            super().__init__()
            self.read_task: xalgospp.Task = read_task
            self.scheduler = scheduler
            self.output: Optional[ncarray.SOArray] = None

            reqs: xalgospp.ResourceRequirements = xalgospp.ResourceRequirements()
            reqs.memory_intensity = 8
            self.set_resources(reqs)

        def execute(self):
            raw_view: Optional[ncarray.SOArrayView] = self.read_task.data
            if raw_view is not None:
                self.output = self.scheduler.acquire_buffer(
                    raw_view.shape, ncarray.DType.float32, -1
                )

                output_view: ncarray.SOArrayView = self.output.view()
                calib_algo.process(raw_view, output_view)

                # TODO: This breaks reference cycles and GC works fine after
                #       But its not a nice UI/UX -- So improve this.
                self.read_task.data = None

    local_count: int = 0
    frame_count: int = 0
    print_every: int = args.print
    num_events: int = args.num_events

    class GeneratorTask(xalgospp.Task):
        def __init__(
            self,
            scheduler: xalgospp.DagScheduler,
            ds: sbio.DataSource,
            builder_func: Callable[[int], List[xalgospp.Task]],
            num_events: Optional[int] = None,
        ):
            super().__init__()
            self.scheduler: xalgospp.DagScheduler = scheduler
            self.ds: sbio.DataSource = ds
            self.builder_func: Callable[[int], List[xalgospp.Task]] = builder_func
            self.num_events = num_events

        def is_generator(self) -> bool:
            return True

        def execute(self):
            global local_count
            local_count += 1
            step_idx: int = self.ds.next()

            if step_idx >= 0xFFFFFFFFFFFFFFFE or step_idx is None:
                return

            if self.num_events and step_idx >= self.num_events:
                return

            frame_count = step_idx
            if frame_count % print_every == 0:
                print(frame_count)

            tasks: List[xalgospp.Task] = self.builder_func(step_idx)
            self.scheduler.submit_dag(tasks)

            next_gen: GeneratorTask = GeneratorTask(
                self.scheduler, self.ds, self.builder_func
            )

            next_gen.add_dependency(self)
            self.scheduler.submit_dag([next_gen])

    def build_dag_for_step(step_idx: int):
        read_task: ReadTask = ReadTask(step_idx)
        calib_task: CalibTask = CalibTask(read_task=read_task, scheduler=scheduler)

        calib_task.add_dependency(read_task)

        return [read_task, calib_task]

    start_time: float = time.perf_counter()
    init_gen: GeneratorTask = GeneratorTask(
        scheduler=scheduler,
        ds=ds,
        builder_func=build_dag_for_step,
        num_events=num_events,
    )
    scheduler.submit_dag([init_gen])
    scheduler.wait_all()
    end_time: float = time.perf_counter()

    total_time_s: float = end_time - start_time
    per_evt_time_s: float = total_time_s / local_count if local_count > 0 else 0
    rate_hz: float = (1.0 / per_evt_time_s) if per_evt_time_s > 0 else 0

    print(f"[Rank {rank}] Successfully processed {local_count} frames.")
    print(f"  - Total time: {total_time_s:.4f} s | Rate: {rate_hz:.2f} ev/s")


if __name__ == "__main__":
    main()
