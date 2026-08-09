#!/usr/bin/env python3
import glob
import os
import re
import site
import sys
import subprocess
import zipfile
from typing import Dict, List


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
            if re.search(
                r"^(lib)?(xalgospp|devxalgospp)[a-zA-Z0-9_.\-]*\.(so|dylib|dll)",
                f,
            )
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
        print("Usage: repair_wheel.py <wheel_path> <dest_dir> [--no-exclude-core]")
        sys.exit(1)

    wheel_path: str = sys.argv[1]
    dest_dir: str = sys.argv[2]

    # Check if the exclusion flag is set. When building in the split mode you
    # do NOT exclude them, otherwise you do.
    no_exclude_core: bool = "--no-exclude-core" in sys.argv

    cmd: List[str]
    if sys.platform == "darwin":
        cmd = ["delocate-wheel"]
        if "--require-archs" in sys.argv:
            idx: int = sys.argv.index("--require-archs")
            delocate_archs: str = sys.argv[idx + 1]
            cmd.extend(["--require-archs", delocate_archs])
    else:
        cmd = [
            "auditwheel",
            "repair",
        ]

    if sys.platform == "darwin":
        mac_excludes: set = set()
        files_to_scan: List[bytes] = []

        with zipfile.ZipFile(wheel_path, "r") as z:
            for name in z.namelist():
                if (
                    name.endswith(".dylib")
                    or ".dylib." in name
                    or name.endswith(".so")
                    or ".so." in name
                ):
                    files_to_scan.append(z.read(name))

        install_dir: str = os.path.join(
            os.environ.get("GITHUB_WORKSPACE", "."), "install"
        )
        if os.path.exists(install_dir):
            for root, _, files in os.walk(install_dir):
                for f in files:
                    if (
                        f.endswith(".dylib")
                        or ".dylib." in f
                        or f.endswith(".so")
                        or ".so." in f
                    ):
                        try:
                            with open(os.path.join(root, f), "rb") as fp:
                                files_to_scan.append(fp.read())
                        except Exception:
                            pass
        prefixes: List[bytes] = [
            b"ncarray",
            b"ncdevarray",
            b"mpi",
            b"mpich",
            b"pciaccess",
            b"sbio",
            b"devsbio",
            b"xtc",
            b"brotli",
            b"libz",
            b"opa",
            b"gcc_s",
            b"stdc++",
            b"gomp",
            b"crypto",
            b"ssl",
        ]
        if not no_exclude_core:
            prefixes.append(b"xalgospp")

        for content in files_to_scan:
            for p in prefixes:
                escaped_p: bytes = re.escape(p)
                matches: List[bytes] = re.findall(
                    rb"(?:lib)?[a-zA-Z0-9_\-]*"
                    + escaped_p
                    + rb"[a-zA-Z0-9_\-]*\.[0-9\.]*\.?dylib",
                    content,
                )

                for m in matches:
                    mac_excludes.add(m.decode("utf-8"))

                matches_so: List[bytes] = re.findall(
                    rb"(?:lib)?[a-zA-Z0-9_\-]*"
                    + escaped_p
                    + rb"[a-zA-Z0-9_\-]*\.so[0-9\.]*",
                    content,
                )

                for m in matches_so:
                    mac_excludes.add(m.decode("utf-8"))

        for exc in mac_excludes:
            cmd.extend(["--exclude", exc])
    else:
        linux_excludes: List[str] = [
            "libncarray*",
            "libncdevarray*",
            "libmpi*",
            "libmpich*",
            "libpciaccess*",
            "libsbio*",
            "libdevsbio*",
            "libxtc*",
            "libbrotli*",
            "libz*",
            "libopa*",
            "libgcc_s*",
            "libstdc++*",
            "libgomp*",
            "libcrypto*",
            "libssl*",
        ]
        if not no_exclude_core:
            linux_excludes.append("*xalgospp*")
        for pat in linux_excludes:
            cmd.extend(["--exclude", pat])

    if sys.platform == "darwin":
        env: Dict[str, str] = os.environ.copy()
        try:
            site_pkg: str = subprocess.check_output(
                [sys.executable, "-c", "import site; print(site.getsitepackages()[0])"],
                text=True,
            ).strip()
            extra_paths: List[str] = [
                os.path.join(os.environ.get("GITHUB_WORKSPACE", "."), "install", "lib"),
                os.path.join(site_pkg, "ncarray", ".dylibs"),
                os.path.join(site_pkg, "sbio", ".dylibs"),
            ]
            env["DYLD_LIBRARY_PATH"] = (
                ":".join(p for p in extra_paths if os.path.exists(p))
                + ":"
                + env.get("DYLD_LIBRARY_PATH", "")
            )
        except Exception:
            pass

        cmd.extend(
            [
                "-w",
                dest_dir,
                wheel_path,
            ]
        )
        print(f"Running: {' '.join(cmd)}")
        subprocess.run(cmd, check=True, env=env)
    else:
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

    if sys.platform == "darwin":
        for whl in repaired_wheels:
            update_pc_in_repaired_wheel(whl_path=whl)
    else:
        cuda_home: str = os.environ.get("CUDA_HOME", "/usr/local/cuda")
        builtins_src: List[str] = glob.glob(
            os.path.join(cuda_home, "lib64/libnvrtc-builtins.so*")
        )

        if not builtins_src:
            print(
                "Warning: libnvrtc-builtins.so not found in CUDA directory. Skipping injection."
            )

        for whl in repaired_wheels:
            if builtins_src:
                print(f"Injecting builtins into {whl}...")
                with zipfile.ZipFile(whl, "a") as z:
                    libs_dir: str = next(
                        (
                            os.path.dirname(name)
                            for name in z.namelist()
                            if "libnvrtc-" in os.path.basename(name)
                        ),
                        "",
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

                    builtins_file: str
                    for builtins_file in builtins_src:
                        # Make sure to update symlink'd filenames appropriately
                        real_file: str = os.path.realpath(builtins_file)
                        basename: str = os.path.basename(builtins_file)
                        target_path: str = os.path.join(libs_dir, basename)
                        print(f"  Adding {real_file} -> {target_path}")
                        z.write(real_file, target_path)

            update_pc_in_repaired_wheel(whl_path=whl)


if __name__ == "__main__":
    main()
