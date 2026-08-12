project(
    'xalgospp',
    'c',
    'cpp',
    version: '0.1.1',
    meson_version: '>=1.10.0',
    default_options: [
        'c_std=c17',
        'cpp_std=c++23,c++latest',
        'cuda_std=c++20',
        'optimization=3',
        'pkgconfig.relocatable=true',
    ],
)
add_languages('c', 'cpp', native: true, required: true)
xalg_has_cuda = add_languages('cuda', required: false)

if xalg_has_cuda
    # Source code guards CUDA specific features behind XALG_HAS_CUDA
    add_project_arguments('-DXALG_HAS_CUDA', language: ['cpp', 'cuda'])

    cuda = meson.get_compiler('cuda')
    cuda_version = cuda.version()
endif

cpp = meson.get_compiler('cpp')

# Check compiler versions (for C++23 suppport especially)
if cpp.get_id() == 'gcc'
    gcc_version = cpp.version()
    if not gcc_version.version_compare('>=13')
        if gcc_version.version_compare('>=12')
            warning('GCC >=13 is recommended, however, GCC 12+ should work.')
        else
            error('Builds with GCC require version >=12')
        endif
    endif

    if xalg_has_cuda
        # Too new a compiler for CUDA support also causes problems
        if cuda_version.version_compare('<12.4') and not gcc_version.version_compare('<13')
            error('For CUDA < 12.4 builds, must use GCC < 13')
        elif cuda_version.version_compare('<12.8') and not gcc_version.version_compare('<14')
            error('For CUDA < 12.8 builds, must use GCC < 14')
        elif not gcc_version.version_compare('<15')
            error('For CUDA 12.8+ builds, must use GCC < 15')
        endif
    endif
elif cpp.get_id() == 'clang'
    clang_version = cpp.version()
    if not clang_version.version_compare('>=16')
        error('Builds with clang require version >=16')
    endif

    if xalg_has_cuda
        # Too new a compiler for CUDA support also causes problems
        if cuda_version.version_compare('<12.4') and not clang_version.version_compare('<17')
            error('For CUDA < 12.4 builds, must use clang < 17')
        elif cuda_version.version_compare('<12.8') and not clang_version.version_compare('<19')
            error('For CUDA < 12.8 builds, must use clang < 19')
        elif not clang_version.version_compare('<20')
            error('For CUDA 12.8+ builds, must use clang < 20')
        endif
    endif
elif cpp.get_id() == 'msvc'
    msvc_version = cpp.version()
    if not msvc_version.version_compare('>=19.30')
        error('Builds with msvc require version >=19.30')
    endif

    if xalg_has_cuda
        # Too new a compiler for CUDA support also causes problems
        if cuda_version.version_compare('<12.4') and not msvc_version.version_compare('<19.38')
            error('For CUDA < 12.4 builds, must use msvc < 19.38 (VS 2022 < 17.8)')
        elif cuda_version.version_compare('<12.8') and not msvc_version.version_compare('<19.41')
            error('For CUDA < 12.8 builds, must use msvc < 19.41 (VS 2022 < 17.11)')
        elif not msvc_version.version_compare('<19.50')
            error('For CUDA 12.8+ builds, must use msvc < 19.50 (VS 2022 < 18.0)')
        endif
    endif
endif

# For Windows adjust for limit of 65,536 COFF sections
if cpp.get_id() == 'msvc'
    add_project_arguments('/bigobj', '/DNOMINMAX', language: 'cpp')
    if xalg_has_cuda
        add_project_arguments(
            '-Xcompiler=/bigobj',
            '-Xcompiler=/DNOMINMAX',
            '-D_SOURCE_LOCATION_',
            language: 'cuda',
        )
    endif
endif

#### Fetch Build Options ####
#############################
# Core options
build_core_opt = get_option('build_core')
build_mpi_opt = get_option('build_mpi')
build_python_opt = get_option('build_python')
xalg_cuda_archs_opt = get_option('xalg_cuda_archs')
xalg_as_wheel_opt = get_option('xalg_as_wheel')

# Add-ons and development
build_examples_opt = get_option('build_examples')

#### Parse CUDA architectures and set CUDA args ####
####################################################

# NOTE: I think there may be a clang-frontend for CUDA... These may not all work if
# using that instead of nvcc specifically!
xalg_cuda_args = [
    # On Windows, inspite of overrides and global settings, C++14 gets chosen
    # as the CUDA standard unless this gets added here
    '--std=c++20',
    '--extended-lambda',
    '--expt-relaxed-constexpr',
    # silence warning: declared but not referenced (e.g. pybind, stl...)
    '-Xcudafe', '--diag_suppress=445',
    # silence warning: long double treated as double
    '-Xcudafe', '--diag_suppress=20208',
    # silence warning: float overflow in CUDA cmath header (Windows)
    # TODO: Try to limit this to includes/callsites eventually
    '-Xcudafe', '--diag_suppress=221',
    # silence warning: field of class type without a DLL interface (Windows)
    # TODO: Try to limit this to includes/callsites eventually (tried a bit already)
    '-Xcudafe', '--diag_suppress=1394',
    # silence warning: __host__ called from __host__ __device__ (Linux - use of dispatch in GPUEngine)
    # TODO: Try to limit this to includes/callsites eventually (tried a bit already)
    '-Xcudafe', '--diag_suppress=20011',
    # silence warning: unrecognized GCC pragma (Linux)
    '-Xcudafe', '--diag_suppress=1675',
]
if xalg_has_cuda
    if cuda_version.version_compare('>=12.8')
        xalg_cuda_args += [
            '-static-global-template-stub=false',
            '-device-entity-has-hidden-visibility=false',
        ]
    endif
endif

xalg_cuda_args += ['-Xfatbin', '-compress-all']

if xalg_cuda_archs_opt == 'all'
    # Setup a fat binary for Turing, Ampere, Ada and Hopper
    xalg_cuda_args += [
        '-gencode', 'arch=compute_75,code=sm_75',
        '-gencode', 'arch=compute_80,code=sm_80',
        '-gencode', 'arch=compute_86,code=sm_86',
        '-gencode', 'arch=compute_89,code=sm_89',
        '-gencode', 'arch=compute_90,code=[sm_90,compute_90]',
    ]
else
    # Parse comma-separated architectures ('80', or '75,90')
    # Last value will generate [sm_ARCH,compute_ARCH]
    selected_cuda_archs = xalg_cuda_archs_opt.split(',')

    num_archs = selected_cuda_archs.length()
    last_arch = selected_cuda_archs[num_archs - 1].strip()
    foreach arch : selected_cuda_archs
        arch = arch.strip()
        if arch != ''
            if arch == last_arch
                # Generate PTX in this case, for forward-compatibility
                xalg_cuda_args += [
                    '-gencode', 'arch=compute_' + arch + ',code=[sm_' + arch + ',compute_' + arch + ']'
                ]
            else
                xalg_cuda_args += [
                    '-gencode', 'arch=compute_' + arch + ',code=sm_' + arch
                ]
            endif
        endif
    endforeach
endif

#### Main Python Setup ####
###########################

pymod = import('python')

py3 = pymod.find_installation(pure: false)
py3_dep = py3.dependency()

min_python_ver = '3.8'
py_ver = py3.language_version()
if py_ver.version_compare(f'<@min_python_ver@')
    error(f'Minimum supported Python version is @min_python_ver@, found @py_ver@')
endif

#### Core Dependencies ####
###########################
deps_for_xalgospp = []

if xalg_has_cuda
    # CUDA driver and nvrtc are both for JIT
    cuda_dep = dependency('cuda', modules: ['cuda', 'cudart', 'nvrtc'])
    deps_for_xalgospp += cuda_dep

    # On Windows need symbol demangling for the JIT compiler.
    dbghelp_dep = []
    if cpp.get_id() == 'msvc'
        dbghelp_dep = [cpp.find_library('dbghelp', required: true)]
    endif
    deps_for_xalgospp += dbghelp_dep
endif

cpp_httplib_dep = dependency('cpp-httplib', required: false)
if not cpp_httplib_dep.found()
    message('cpp-httplib not found - using subproject')
    # cpp-httplib deps:
    # Since we added C to support Windows MPI, strict C17 causes issues with brotli CLI
    if host_machine.system() != 'windows'
        add_global_arguments('-D_GNU_SOURCE', language: 'c')
    endif
    brotli_proj = subproject('google-brotli', default_options: ['default_library=static'])
    zlib_proj = subproject('zlib', default_options: ['default_library=static'])

    # Now build
    cpp_httplib_proj = subproject('cpp-httplib')
    cpp_httplib_dep = cpp_httplib_proj.get_variable('cpp_httplib_dep')
endif
deps_for_xalgospp += cpp_httplib_dep

# Add libdl as needed for JIT if not merged directly into the cstdlib
# dl_dep = cpp.find_library('dl', required: false)
# if dl_dep.found()
#     deps_for_xalgospp += dl_dep
# endif

## Eigen
eigen_dep = dependency('eigen3', 'eigen', required: false)
if not eigen_dep.found()
    message('eigen not found - using subproject')
    eigen_proj = subproject('eigen')
    eigen_dep = dependency('eigen3')
endif
deps_for_xalgospp += eigen_dep

## MPI
if build_mpi_opt == 'none'
    message('Skipping MPI support.')
else
    try_sys = false
    try_mpich = false
    try_ompi = false
    if build_mpi_opt == 'auto'
        try_sys = true
        try_mpich = true
        try_ompi = true
    endif
    if build_mpi_opt == 'mpich'
        try_mpich = true
    endif
    if build_mpi_opt == 'ompi'
        try_ompi = true
    endif

    found_mpi = false
    bundle_mpi = false # Only bundle if using subproject
    if try_sys
        if host_machine.system() == 'windows'
            mpi_dep = dependency('mpi', language: 'c', required: false)
        else
            mpi_dep = dependency('mpi', required: false)
            if not mpi_dep.found()
                mpi_dep = dependency('mpi', language: 'c', required: false)
            endif
        endif
        if mpi_dep.found()
            found_mpi = true
        endif
    endif
    if not found_mpi and try_mpich
        message('Will try building MPICH subproject.')
        mpi_proj = subproject('mpich')
        mpi_dep = mpi_proj.get_variable('mpich_dep')
        bundle_mpi = true
        found_mpi = true
    endif
    if not found_mpi and try_ompi
        message('Will try building OpenMPI subproject.')
        mpi_proj = subproject('ompi')
        mpi_dep = mpi_proj.get_variable('ompi_dep')
        bundle_mpi = true
        found_mpi = true
    endif

    if not found_mpi
        warning('MPI was requested, but was not built!')
    else
        message('Adding MPI Dependency')

        add_project_arguments('-DSBIO_HAS_MPI', language: ['cpp', 'cuda'])
        add_project_arguments('-DXALG_HAS_MPI', language: ['cpp', 'cuda'])
        deps_for_xalgospp += mpi_dep

        # ---------------------- Also will use `hwloc` ---------------------- #
        hwloc_dep = dependency('hwloc', required: false)
        if not hwloc_dep.found()
            message('Falling back to building hwloc by subproject.')

            hwloc_proj = subproject('hwloc')
            hwloc_dep = hwloc_proj.get_variable('hwloc_dep')
        endif
        message('Adding the `hwloc` dependency.')
        deps_for_xalgospp += hwloc_dep
    endif
endif # MPI builds

## ncarray
libncarray = cpp.find_library('ncarray', required: false)
if libncarray.found()
    ncarray_dep = libncarray
else
    message('ncarray not found via `find_library` - checking via Python pkg-config helper')
    nca_pkg_config_bin = find_program('nca-pkg-config', required: false)
    if nca_pkg_config_bin.found()
        nc_pc_path_cmd_res = run_command(
            nca_pkg_config_bin, '--pkg-config-path',
            check: false,
        )
        nc_pc_path = nc_pc_path_cmd_res.stdout().strip()
        if (nc_pc_path_cmd_res.returncode() == 0) and (nc_pc_path != '' and nc_pc_path != 'None')
            nca_inc_rel_pc = nc_pc_path + '/../include'
            nc_cflags = run_command(
                nca_pkg_config_bin, '--pkg-config-cflags',
                check: false,
            ).stdout().strip().replace('${includedir}', nca_inc_rel_pc)
            message('**** Using these NCArray compile args: ' + nc_cflags)

            nc_libs = run_command(
                nca_pkg_config_bin, '--pkg-config-libs',
                check: false,
            ).stdout().strip().replace('${pcfiledir}', nc_pc_path)
            message('**** Using these NCArray link args: ' + nc_libs)

            ncarray_dep = declare_dependency(
                compile_args: nc_cflags.split(),
                link_args: nc_libs.split(),
            )
        endif
    endif

    if not is_variable('ncarray_dep')
        message('ncarray not found via pkg-config helper - checking via Python lib and inc getters')
        nc_libs_cmd = '''
import glob
import os
from typing import List, Set

import ncarray

d: str = ncarray.get_lib_dir()
files: Set[str] = set(glob.glob(os.path.join(d, "*")))
libs: List[str] = [f for f in files if f.endswith((".so", ".dylib", ".dll", ".lib")) or ".so." in f]

if any(f.endswith(".lib") for f in libs):
    libs = [f for f in libs if not f.endswith(".dll")]
print(";".join(libs).replace("\r", ""))
'''

        nc_cmd_res = run_command(
            py3, '-c', nc_libs_cmd,
            check: false,
        )
        message('NCArray Python Lib Search RETURNCODE: ' + nc_cmd_res.returncode().to_string())
        message('NCArray Python Lib Search STDOUT: ' + nc_cmd_res.stdout())
        message('NCArray Python Lib Search STDERR: ' + nc_cmd_res.stderr())

        nc_libs_raw = nc_cmd_res.stdout().strip().split(';')

        if (nc_libs_raw.length() == 0) or (nc_libs_raw.length() == 1  and nc_libs_raw[0] == '')
            message('ncarray not found via Python - will use subproject.')
            ncarray_proj = subproject('ncarray')
            ncarray_dep = ncarray_proj.get_variable('ncarray_dep')
        else
            fs = import('fs')
            nc_link_args = []
            foreach lib : nc_libs_raw
                if lib != ''
                    if 'libpython' in lib or lib.endswith('/Python')
                        message('Skipping ' + lib + ' from ncarray list. Causes problem on macOS!')
                    else
                        nc_link_args += [lib]
                    endif # macOS Python wheel repair guard.....
                endif
            endforeach

            nc_inc_cmd_res = run_command(
                py3, '-c', 'import ncarray; print(ncarray.get_include())',
                check: false,
            )

            message('NCArray Python Include Search RETURNCODE: ' + nc_inc_cmd_res.returncode().to_string())
            message('NCArray Python Include Search STDOUT: ' + nc_inc_cmd_res.stdout())
            message('NCArray Python Include Search STDERR: ' + nc_inc_cmd_res.stderr())

            nc_inc = nc_inc_cmd_res.stdout().strip()

            ncarray_dep = declare_dependency(
                link_args: nc_link_args,
                include_directories: include_directories(nc_inc),
            )
        endif # if nca_libs_raw empty (Python search for ncarray)
    endif # nca-pkg-config helper
endif
deps_for_xalgospp += ncarray_dep

# openmp_dep = dependency('openmp', required: false)
# xalg_has_openmp = openmp_dep.found()
# if xalg_has_openmp
#     add_project_arguments('-DXALG_HAS_OPENMP', language: ['cpp', 'cuda'])
#     deps_for_xalgospp += openmp_dep
# endif

rapidjson_dep = dependency('rapidjson', required: false)
if not rapidjson_dep.found()
    message('rapidjson not found - using subproject')
    rapidjson_proj = subproject('rapidjson')
    rapidjson_dep = rapidjson_proj.get_variable('rapidjson_dep')
endif
deps_for_xalgospp += rapidjson_dep


## sbio
libsbio = cpp.find_library('sbio', required: false)
if libsbio.found()
    sbio_libs = [libsbio]

    # Check which components may have been built
    libxtc1slim = cpp.find_library('xtc1slim', required: false)
    if libxtc1slim.found()
        add_project_arguments('-DSBIO_HAS_XTC1', language: ['cpp', 'cuda'])
        sbio_libs += libxtc1slim
    endif

    libxtc2slim = cpp.find_library('xtc2slim', required: false)
    if libxtc2slim.found()
        add_project_arguments('-DSBIO_HAS_XTC2', language: ['cpp', 'cuda'])
        sbio_libs += libxtc2slim
    endif

    sbio_dep = declare_dependency(
        dependencies: sbio_deps,
    )
else
    message('sbio not found via `find_library` - checking via Python pkg-config helper')
    sbio_pkg_config_bin = find_program('sbio-pkg-config', required: false)
    if sbio_pkg_config_bin.found()
        sbio_pc_path_cmd_res = run_command(
            sbio_pkg_config_bin, '--pkg-config-path',
            check: false,
        )
        sbio_pc_path = sbio_pc_path_cmd_res.stdout().strip()
        if (sbio_pc_path_cmd_res.returncode() == 0) and (sbio_pc_path != '' and sbio_pc_path != 'None')
            sbio_inc_rel_pc = sbio_pc_path + '/../include'
            # The wheel repair should provide the feature macros (-DSBIO_HAS_XTC2 etc)
            # --> Do not need to check for any other libs if we find sbio this way then
            sbio_cflags = run_command(
                sbio_pkg_config_bin, '--pkg-config-cflags',
                check: false,
            ).stdout().strip().replace('${includedir}', sbio_inc_rel_pc)
            message('**** Using these sbio compile args: ' + sbio_cflags)

            # The wheel repair process should correct the lib paths
            sbio_libs = run_command(
                sbio_pkg_config_bin, '--pkg-config-libs',
                check: false,
            ).stdout().strip().replace('${pcfiledir}', sbio_pc_path)
            message('**** Using these sbio link args: ' + sbio_libs)

            # NOTE: Again, the wheel repair will include all feature flags and libs
            #       Don't need to search for various libraries, or define macros.
            sbio_dep = declare_dependency(
                compile_args: sbio_cflags.split(),
                link_args: sbio_libs.split(),
            )
        endif
    endif

    if not is_variable('sbio_dep')
        sbio_libs_cmd = '''
import ctypes
import glob
import os
import platform
from typing import List, Set

import sbio

d: str = sbio.get_lib_dir()
files: Set[str] = set(glob.glob(os.path.join(d, "*")))
libs: List[str] = [f for f in files if f.endswith((".so", ".dylib", ".dll", ".lib")) or ".so." in f]

if any(f.endswith(".lib") for f in libs):
    libs = [f for f in libs if f.endswith(".lib")]
print(";".join(libs).replace("\r", ""))
    '''

        sbio_cmd_res = run_command(
            py3, '-c',
            sbio_libs_cmd,
            check: false,
        )
        message('SBIO Python Lib Search RETURNCODE: ' + sbio_cmd_res.returncode().to_string())
        message('SBIO Python Lib Search STDOUT: ' + sbio_cmd_res.stdout())
        message('SBIO Python Lib Search STDERR: ' + sbio_cmd_res.stderr())

        sbio_libs_raw = sbio_cmd_res.stdout().strip().split(';')

        if (sbio_libs_raw.length() == 0) or (sbio_libs_raw.length() == 1 and sbio_libs_raw[0] == '')
            message('sbio not found via Python - will use subproject.')
            sbio_proj = subproject('sbio')
            sbio_dep = sbio_proj.get_variable('sbio_dep')
            sbio_fmts = sbio_proj.get_variable('sbio_enabled_formats')['formats']
            if 'xtc1' in sbio_fmts
                message('Enabling XTC1 support as it was built by sbio')
                add_project_arguments('-DSBIO_HAS_XTC1', language: ['cpp', 'cuda'])
            endif
            if 'xtc2' in sbio_fmts
                message('Enabling XTC2 support as it was built by sbio')
                add_project_arguments('-DSBIO_HAS_XTC2', language: ['cpp', 'cuda'])
            endif
        else
            fs = import('fs')
            sbio_link_args = []
            foreach lib : sbio_libs_raw
                if lib != ''
                    if 'libpython' in lib or lib.endswith('/Python')
                        message('Skipping ' + lib + ' from sbio list. Causes problem on macOS!')
                    else
                        libname = fs.name(lib)
                        if libname.contains('xtc1')
                            add_project_arguments('-DSBIO_HAS_XTC1', language: ['cpp', 'cuda'])
                        endif

                        if libname.contains('xtc2')
                            add_project_arguments('-DSBIO_HAS_XTC2', language: ['cpp', 'cuda'])
                        endif
                        if not lib.endswith('.dll') # Windows guard....
                            sbio_link_args += [lib]
                        endif
                    endif # macOS Python wheel repair guard.....
                endif
            endforeach

            sbio_inc_cmd_res = run_command(
                py3, '-c', 'import sbio; print(sbio.get_include())',
                check: false,
            )

            message('SBIO Python Include Search RETURNCODE: ' + sbio_inc_cmd_res.returncode().to_string())
            message('SBIO Python Include Search STDOUT: ' + sbio_inc_cmd_res.stdout())
            message('SBIO Python Include Search STDERR: ' + sbio_inc_cmd_res.stderr())

            sbio_inc = sbio_inc_cmd_res.stdout().strip()

            sbio_dep = declare_dependency(
                link_args: sbio_link_args,
                include_directories: include_directories(sbio_inc),
            )
        endif # if sbio_libs_raw empty (Python search for sbio)
    endif # sbio-pkg-config helper
endif
deps_for_xalgospp += sbio_dep

if host_machine.system() == 'darwin'
    # macOS CI is the worst. Fails in all psosible configurations
    # BUILD ONLY WITH SUBPROJECT, STATIC ONLY, DONT ALLOW ANYTHING TO BE PULLED BY HOMEBREW
    fmt_proj = subproject('fmt', default_options: ['default_library=static'])
    fmt_dep = fmt_proj.get_variable('fmt_dep')
    deps_for_xalgospp += fmt_dep
    spdlog_proj = subproject('spdlog', default_options: ['default_library=static'])
    spdlog_dep = spdlog_proj.get_variable('spdlog_dep')
else
    spdlog_dep = dependency('spdlog', required: false, static: true)
    if not spdlog_dep.found()
        message('spdlog not found - using subproject')
        spdlog_proj = subproject('spdlog', default_options: ['default_library=static'])
        spdlog_dep = spdlog_proj.get_variable('spdlog_dep')
    endif
endif
deps_for_xalgospp += spdlog_dep

if build_python_opt
    pybind11_dep = dependency('pybind11', required: false)
    if not pybind11_dep.found()
        message('pybind11 not found, but required for Python bindings - will build subproject.')
        pybind11_proj = subproject('pybind11')
        pybind11_dep = pybind11_proj.get_variable('pybind11_dep')
    endif # pybind11 not found
endif

#### Path Setup ####
####################

py_install_dir = py3.get_install_dir(pure: false)

pyxalgospp_install_dir = py_install_dir / 'xalgospp'

if xalg_as_wheel_opt
    # Install C++ libraries inside the wheel structure
    # Package and library setup will be:
    # site-packages/
    #    | - xalgospp
    #           | - lib
    #           |    | - libxalgospp.so
    #           |
    #           | - detector.so
    #           | - scheduling.so # ... etc.
    #           |
    #           | - __init__.py (utility functions and expose core lib)

    libxalgospp_install_dir = py_install_dir / 'xalgospp/lib'
    header_install_dir = py_install_dir / 'xalgospp/include'

    # Relative path from 'xalgospp/' to 'xalgospp/lib'
    py_to_lib_path = './lib'

    exe_prefix = py_install_dir
    lib_prefix = pyxalgospp_install_dir
else
    # Normal install path
    libxalgospp_install_dir = get_option('libdir')
    header_install_dir = get_option('includedir')

    # Relative path from '$PREFIX/lib/pythonMAJ.MIN/site-packages/xalgospp/' to '$PREFIX/lib'
    py_to_lib_path = '../../../'

    exe_prefix = get_option('prefix') / 'bin'
    lib_prefix = pyxalgospp_install_dir
endif

# For both the wheel and non-wheel case, executables relative path, for
# $PREFIX/bin to $PREFIX/lib is
exe_to_libxalgospp = '..' / libxalgospp_install_dir

lib_install_rpath = '$ORIGIN'

abs_exe_rpath = exe_prefix / exe_to_libxalgospp
abs_lib_rpath = lib_prefix / py_to_lib_path

# Setup RPATH for various platforms, for any executables that may need it
if host_machine.system() == 'darwin'
    exe_install_rpath = '@executable_path' + ':' + ('@executable_path' / exe_to_libxalgospp) + ':' + abs_exe_rpath
    py_to_lib_install_rpath = '@loader_path' + ':' + ('@loader_path' / py_to_lib_path)  + ':' + abs_lib_rpath
else
    exe_install_rpath = '$ORIGIN' + ':' + ('$ORIGIN' / exe_to_libxalgospp)  + ':' + abs_exe_rpath
    py_to_lib_install_rpath = '$ORIGIN' + ':' + ('$ORIGIN' / py_to_lib_path) + ':' + abs_lib_rpath
endif

#### XAlgosPP ####
##################

xalgospp_headers = include_directories('src/lib', 'src/lib/xalgospp')

# Traverse the main source first so the dependency objects are available
# for examples and tests
subdir('src')

# Build example programs
subdir('examples')

# Build the test suite
# subdir('tests')

# Setup a pkgconfig file now after the build completed.
pkg = import('pkgconfig')

if build_core_opt and not xalg_as_wheel_opt
    pc_install_dir = get_option('libdir') / 'pkgconfig'
else
    pc_install_dir = py_install_dir / 'xalgospp/pkgconfig'
endif

pc_libs = [libxalgospp]

if xalg_has_cuda
    pc_libs += [libdevxalgospp]
endif

feature_flags = []
if xalg_has_cuda
    feature_flags += ['-DXALG_HAS_CUDA']
endif

if get_variable('found_mpi', false)
    feature_flags += ['-DXALG_HAS_MPI']
endif

pc_headers = ['.', 'xalgospp']
pkg.generate(
    name: 'xalgospp',
    description: 'Algorithms and Utilities for X-Ray Free Electron Laser (XFEL) Area Detector Analysis.',
    libraries: pc_libs,
    extra_cflags: feature_flags,
    subdirs: pc_headers,
    version: meson.project_version(),
    url: 'https://xalgospp.xfelpp.org',
    license: 'AGPL-3.0-only',
    install_dir: pc_install_dir,
)
