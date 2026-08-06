#!python
import os
import sys
import glob
import subprocess
from typing import List, Optional


def main():
    if len(sys.argv) < 3:
        print("Usage: repair_wheel.py <dest_dir> <wheel_path>")
        sys.exit(1)

    dest_dir: str = sys.argv[1]
    wheel_path: str = sys.argv[2]

    # Check if the exclusion flag is set. When building in the split mode you
    # do NOT exclude them, otherwise you do.
    no_exclude_core: bool = "--no-exclude-core" in sys.argv

    scripts_dir: str = os.path.dirname(os.path.abspath(__file__))
    root_dir: str = os.path.dirname(scripts_dir)
    bin_dir: str = os.path.normpath(os.path.join(root_dir, "install", "bin"))
    lib_dir: str = os.path.normpath(os.path.join(root_dir, "install", "lib"))

    delvewheel_cmd: List[str] = ["delvewheel", "repair"]
    if not no_exclude_core:
        delvewheel_cmd.extend(
            [
                "--no-dll",
                "xalgospp-1.dll",
            ]
        )

    delvewheel_search_path: str = f"{lib_dir};{bin_dir}"
    try:
        import ncarray
        nc_dir: str = ncarray.get_lib_dir()
        root: str = os.path.dirname(nc_dir)

        nc_dlls: List[str] = glob.glob(os.path.join(root, "**", "*.dll"), recursive=True)
        nc_dirs: List[str] = list(set(os.path.dirname(p) for p in nc_dlls))
        if os.path.exists(nc_dir):
            nc_dirs.append(nc_dir)

        for d in set(nc_dirs):
            delvewheel_search_path = f"{delvewheel_search_path};{d}"
    except ImportError:
        pass

    try:
        import sbio
        sbio_dir: str = sbio.get_lib_dir()
        sb_root: str = os.path.dirname(sbio_dir)

        sbio_dlls: List[str] = glob.glob(os.path.join(sb_root, "**", "*.dll"), recursive=True)
        sbio_dirs: List[str] = list(set(os.path.dirname(p) for p in sbio_dlls))
        if os.path.exists(sbio_dir):
            sbio_dirs.append(sbio_dir)

        for d in set(sbio_dirs):
            delvewheel_search_path = f"{delvewheel_search_path};{d}"
    except ImportError:
        pass

    cuda_args: List[str] = []
    cuda_path: Optional[str] = os.getenv("CUDA_PATH")
    if cuda_path is None:
        print(
            "CUDA_PATH is not defined! Cannot find CUDA! Skipping NVRTC builtins packaging."
        )
    else:
        cuda_args.extend(["--no-dll", "nvcuda.dll"])

        cuda_path = os.path.normpath(cuda_path)
        # Seems like there's an [ARCH] sub-directory after CUDA13....
        # See: https://github.com/shader-slang/slangpy/issues/614
        cuda_dlls: List[str] = glob.glob(f"{cuda_path}/**/*.dll", recursive=True)
        cuda_dlls_dirs: List[str] = sorted(
            list(set(os.path.dirname(os.path.normpath(p)) for p in cuda_dlls))
        )
        for dll_dir in cuda_dlls_dirs:
            delvewheel_search_path = f"{delvewheel_search_path};{dll_dir}"

        builtins_dlls: List[str] = glob.glob(
            f"{cuda_path}/**/nvrtc-builtins*.dll", recursive=True
        )

        if not builtins_dlls:
            print("No NVRTC builtins dlls found!")
        else:
            if len(builtins_dlls) > 1:
                print(f"Multiple NVRTC DLLs found: {builtins_dlls}")
            selected_dll: str = os.path.basename(builtins_dlls[0])

            cuda_args.extend(["--add-dll", selected_dll])

    delvewheel_cmd.extend(["--add-path", delvewheel_search_path])
    delvewheel_cmd.extend(cuda_args)
    delvewheel_cmd.extend(["-w", dest_dir, wheel_path])

    subprocess.run(delvewheel_cmd, check=True)


if __name__ == "__main__":
    main()
