# XAlgosPP - Algorithms and Utilities for XFEL Area Detector Analysis.
#
# Copyright (C) 2025-2026 Gabriel Dorlhiac
#
# This program is free software: you can redistribute it and/or modify it under
# the terms of the GNU Affero General Public License as published by the
# Free Software Foundation, either version 3 of the License, or (at your option)
# any later version.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for
# more details.

# You should have received a copy of the GNU Affero General Public License along with
# this program. If not, see <https://www.gnu.org/licenses/>.

import ctypes
import ctypes.util
import os
import platform
import sys


def get_include() -> str:
    xalgospp_wheel_include_dir: str = os.path.join(os.path.dirname(__file__), "include")
    if os.path.exists(xalgospp_wheel_include_dir):
        return xalgospp_wheel_include_dir

    # When installing with conda or not pure pip, the headers will be in the standard
    # prefix
    win_prefix_include_dir: str = os.path.join(sys.prefix, "Library", "include")
    if os.path.exists(win_prefix_include_dir):
        return win_prefix_include_dir

    unix_prefix_include_dir: str = os.path.join(sys.prefix, "include")
    return unix_prefix_include_dir


def get_lib_dir() -> str:
    py_package_dir: str = os.path.dirname(__file__)

    # Check full-wheel path first: site-packages/xalgospp/lib
    nca_wheel_lib_dir: str = os.path.join(py_package_dir, "lib")
    if os.path.exists(nca_wheel_lib_dir):
        return nca_wheel_lib_dir

    # Check split-wheel: site-packages/xalgospp/libxalgospp.so
    #                    site-packages/xalgospp.libs/libxalgospp.so
    parent_dir: str = os.path.dirname(py_package_dir)
    for folder in os.listdir(parent_dir):
        if "xalgospp" in folder and folder != os.path.basename(py_package_dir):
            libs_dir: str = os.path.join(parent_dir, folder)
            if not os.path.isdir(libs_dir):
                continue
            for fname in os.listdir(libs_dir):
                if fname.startswith("libxalgospp") or fname.startswith("xalgospp"):
                    if (
                        fname.endswith(".so")
                        or fname.endswith(".dylib")
                        or fname.endswith(".dll")
                        or ".so." in fname
                    ):
                        return libs_dir

    # When installing with conda or not pure pip, the lib will be in the standard
    # prefix
    win_prefix_lib_dir: str = os.path.join(sys.prefix, "Library", "lib")
    if os.path.exists(win_prefix_lib_dir):
        return win_prefix_lib_dir

    unix_prefix_lib_dir: str = os.path.join(sys.prefix, "lib")
    return unix_prefix_lib_dir


if platform.system() == "Windows":
    # Need to explicitly register DLL directories on Windows
    lib_dir: str = get_lib_dir()
    if os.path.exists(lib_dir):
        os.add_dll_directory(lib_dir)
elif platform.system() == "Linux":
    # Add stub file if needed
    has_driver: bool = ctypes.util.find_library("cuda") is not None
    if not has_driver:
        stub_path: str = os.path.join(
            os.path.dirname(__file__), "core", "stubs", "libcuda.so"
        )
        if os.path.exists(stub_path):
            try:
                ctypes.CDLL(stub_path, mode=ctypes.RTLD_GLOBAL)
            except Exception:
                pass

from xalgospp.core._pyxalgospp import *
