#!python
import os
import sys
import glob
import re
import subprocess
import zipfile
from typing import List, Optional


def update_pc_in_repaired_wheel(whl_path: str):
    with zipfile.ZipFile(whl_path, "r") as z:
        pc_path_in_whl: str = next(
            (f for f in z.namelist() if f.endswith("xalgospp.pc")), None
        )
        if not pc_path_in_whl:
            return

        pc_content: str = z.read(pc_path_in_whl).decode("utf-8")

        repaired_libs: List[str] = [
            f
            for f in z.namelist()
            if re.search(r"(lib)?(dev)?(xalgospp)[a-zA-Z0-9_.\-]*\.lib", f)
        ]

    if repaired_libs and pc_content:
        pc_dir: str = os.path.dirname(pc_path_in_whl)
        updated_libs: List[str] = []
        for lib in repaired_libs:
            rel_lib_path: str = os.path.relpath(lib, start=pc_dir).replace("\\", "/")
            updated_libs.append(rel_lib_path)

        new_link_str: str = " ".join(f"${{pcfiledir}}/{rl}" for rl in updated_libs)
        updated_content: str = re.sub(r"Libs:.*", f"Libs: {new_link_str}", pc_content)

        temp_whl: str = whl_path + ".tmp"
        with zipfile.ZipFile(whl_path, "r") as zin:
            with zipfile.ZipFile(temp_whl, "w", compression=zin.compression) as zout:
                for item in zin.infolist():
                    if item.filename == pc_path_in_whl:
                        zout.writestr(item, updated_content.encode("utf-8"))
                    else:
                        zout.writestr(item, zin.read(item.filename))
        os.replace(temp_whl, whl_path)


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

    delvewheel_cmd: List[str] = ["delvewheel", "repair", "--include-imports"]
    delvewheel_cmd.extend(
        [
            "--no-dll",
            "ncarray-1.dll",
            "--no-dll",
            "ncdevarray-1.dll",
            "--no-dll",
            "ncarrayjit-1.dll",
            "--no-dll",
            "sbio-1.dll",
            "--no-dll",
            "xtc1slim-1.dll",
            "--no-dll",
            "xtc2slim-1.dll",
        ]
    )
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

        nc_dlls: List[str] = glob.glob(
            os.path.join(root, "**", "*.dll"), recursive=True
        )
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

        sbio_dlls: List[str] = glob.glob(
            os.path.join(sb_root, "**", "*.dll"), recursive=True
        )
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
    repaired_wheels: List[str] = glob.glob(os.path.join(dest_dir, "*.whl"))
    for whl in repaired_wheels:
        update_pc_in_repaired_wheel(whl_path=whl)


if __name__ == "__main__":
    main()
