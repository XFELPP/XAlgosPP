#!/usr/bin/env python3
import os
import re
import sys
import glob
import subprocess
import zipfile
from typing import List


def main():
    if len(sys.argv) < 3:
        print("Usage: repair_wheel.py <wheel_path> <dest_dir> [--no-exclude-core]")
        sys.exit(1)

    wheel_path: str = sys.argv[1]
    dest_dir: str = sys.argv[2]

    # Check if the exclusion flag is set. When building in the split mode you
    # do NOT exclude them, otherwise you do.
    no_exclude_core: bool = "--no-exclude-core" in sys.argv

    cmd: List[str] = [
        "auditwheel",
        "repair",
    ]

    with zipfile.ZipFile(wheel_path, "r") as z:
        for name in z.namelist():
            if name.endswith(".so") or ".so." in name:
                content = z.read(name)
                for lib in set(
                    re.findall(rb"libncarray[a-zA-Z0-9_\-]*\.so[0-9\.]*", content)
                ):
                    cmd.extend(["--exclude", lib.decode("utf-8")])
                for lib in set(
                    re.findall(rb"libncdevarray[a-zA-Z0-9_\-]*\.so[0-9\.]*", content)
                ):
                    cmd.extend(["--exclude", lib.decode("utf-8")])
                for lib in set(
                    re.findall(rb"libmpi[a-zA-Z0-9_\-]*\.so[0-9\.]*", content)
                ):
                    cmd.extend(["--exclude", lib.decode("utf-8")])
                for lib in set(
                    re.findall(rb"libpciaccess[a-zA-Z0-9_\-]*\.so[0-9\.]*", content)
                ):
                    cmd.extend(["--exclude", lib.decode("utf-8")])
                for lib in set(
                    re.findall(rb"libsbio[a-zA-Z0-9_\-]*\.so[0-9\.]*", content)
                ):
                    cmd.extend(["--exclude", lib.decode("utf-8")])
                for lib in set(
                    re.findall(rb"libxtc[a-zA-Z0-9_\-]*\.so[0-9\.]*", content)
                ):
                    cmd.extend(["--exclude", lib.decode("utf-8")])
                for lib in set(
                    re.findall(
                        rb"lib(?:gcc_s|stdc\+\+|gomp)[a-zA-Z0-9_\-]*\.so[0-9\.]*",
                        content,
                    )
                ):
                    cmd.extend(["--exclude", lib.decode("utf-8")])

    if not no_exclude_core:
        cmd.extend(
            [
                "--exclude",
                "libxalgospp.so.1",
            ]
        )
    cmd.extend(
        [
            "--exclude",
            "libcuda.so.1",
            "--lib-sdir",
            ".",
            "-w",
            dest_dir,
            wheel_path,
        ]
    )
    print(f"Running: {' '.join(cmd)}")
    subprocess.run(cmd, check=True)

    repaired_wheels: List[str] = glob.glob(os.path.join(dest_dir, "*.whl"))
    if not repaired_wheels:
        print("No repaired wheels found.")
        sys.exit(1)

    cuda_home: str = os.environ.get("CUDA_HOME", "/usr/local/cuda")
    builtins_src: List[str] = glob.glob(
        os.path.join(cuda_home, "lib64/libnvrtc-builtins.so*")
    )

    if not builtins_src:
        print(
            "Warning: libnvrtc-builtins.so not found in CUDA directory. Skipping injection."
        )
        sys.exit(0)

    builtins_file: str = builtins_src[0]

    for whl in repaired_wheels:
        print(f"Injecting builtins into {whl}...")
        with zipfile.ZipFile(whl, "a") as z:
            libs_dir: str = next(
                (
                    os.path.dirname(name)
                    for name in z.namelist()
                    if "libnvrtc-" in os.path.basename(name)
                ),
                None,
            )

            if not libs_dir:
                libs_dir = next(
                    (
                        os.path.dirname(name)
                        for name in z.namelist()
                        if "libcudart-" in os.path.basename(name)
                    ),
                    "xalgospp.libs",
                )

            for builtins_file in builtins_src:
                # Make sure to update symlink'd filenames appropriately
                real_file: str = os.path.realpath(builtins_file)
                basename: str = os.path.basename(builtins_file)
                target_path: str = os.path.join(libs_dir, basename)
                print(f"  Adding {real_file} -> {target_path}")
                z.write(real_file, target_path)


if __name__ == "__main__":
    main()
