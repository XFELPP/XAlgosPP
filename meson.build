project(
    'xalgospp',
    'cpp',
    version: '0.1.0',
    default_options: [
        'cpp_std=c++23,c++latest',
        'cuda_std=c++20',
        'optimization=3',
    ],
)
add_languages('cpp', native: true, required: true)
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
    brotli_proj = subproject('google-brotli')
    zlib_proj = subproject('zlib')

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
eigen_dep = dependency('eigen', required: false)
if not eigen_dep.found()
    message('eigen not found - using subproject')
    eigen_proj = subproject('eigen')
    eigen_dep = dependency('eigen3')
endif
deps_for_xalgospp += eigen_dep

## MPI
# mpi_dep = dependency('mpi', language: 'cpp', required: true)
# deps_for_xalgospp += mpi_dep

## ncarray
libncarray = cpp.find_library('ncarray', required: false)
if libncarray.found()
    ncarray_dep = libncarray
else
    message('ncarray not found via `find_library` - checking via Python')
    nc_libs_raw = run_command(
        py3, '-c',
        'import glob, ncarray; print("\\n".join(glob.glob(f"{ncarray.get_lib_dir()}/libnc*")))',
        check: false,
    ).stdout().strip().split('\n')

    if (nc_libs_raw.length() == 0) or (nc_libs_raw.length() == 1  and nc_libs_raw[0] == '')
        message('ncarray not found via Python - will use subproject.')
        ncarray_proj = subproject('ncarray')
        ncarray_dep = ncarray_proj.get_variable('ncarray_dep')
    else
        fs = import('fs')
        nc_link_args = []
        foreach lib : nc_libs_raw
            libdir = fs.parent(lib)
            libname = fs.name(lib)
            nc_link_args += [
                '-L' + libdir,
                '-l:' + libname,
            ]
        endforeach

        nc_inc = run_command(
            py3, '-c', 'import ncarray; print(ncarray.get_include())',
            check: false,
        ).stdout().strip()

        ncarray_dep = declare_dependency(
            link_args: nc_link_args,
            include_directories: include_directories(nc_inc),
        )
    endif # if nca_libs_raw empty (Python search for ncarray)
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

spdlog_dep = dependency('spdlog', required: false)
if not spdlog_dep.found()
    message('spdlog not found - using subproject')
    spdlog_proj = subproject('spdlog')
    spdlog_dep = spdlog_proj.get_variable('spdlog_dep')
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

pyxalgospp_install_dir = py_install_dir / 'xalgospp/core'

if xalg_as_wheel_opt
    # Install C++ libraries inside the wheel structure
    # Package and library setup will be:
    # site-packages/
    #    | - xalgospp
    #           | - lib
    #           |    | - libxalgospp.so
    #           |
    #           | - core
    #           |    | - _pyxalgospp.so
    #           |
    #           | - __init__.py (utility functions and expose core lib)

    libxalgospp_install_dir = py_install_dir / 'xalgospp/lib'
    header_install_dir = py_install_dir / 'xalgospp/include'

    # Relative path from 'xalgospp/core' to 'xalgospp/lib'
    py_to_lib_path = '../lib'

    exe_prefix = py_install_dir
    lib_prefix = pyxalgospp_install_dir
else
    # Normal install path
    libxalgospp_install_dir = get_option('libdir')
    header_install_dir = get_option('includedir')

    # Relative path from '$PREFIX/lib/pythonMAJ.MIN/site-packages/xalgospp/core' to '$PREFIX/lib'
    py_to_lib_path = '../../../../'

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

installed_headers = [
    'src/lib/xalgospp/algorithm.hh',
]

installed_detector_headers = [
    'src/lib/xalgospp/detector/calibration.hh',
    'src/lib/xalgospp/detector/detector.hh',
]

installed_detector_lcls2_headers = [
    'src/lib/xalgospp/detector/lcls2/calibdb.hh',
]

installed_scheduling_headers = [
    'src/lib/xalgospp/scheduling/dag_scheduler.hh',
    'src/lib/xalgospp/scheduling/queue.hh',
    'src/lib/xalgospp/scheduling/task.hh',
]

installed_utilities_headers = [
    'src/lib/xalgospp/utilities/dispatch.hh',
    'src/lib/xalgospp/utilities/interconversion.hh',
    'src/lib/xalgospp/utilities/mongodb.hh',
]

#### Core Library ####
######################
xalgospp_headers = include_directories('src/lib', 'src/lib/xalgospp')

if build_core_opt
    libxalgospp = shared_library(
        'xalgospp',
        [            
            'src/lib/xalgospp/detector/lcls2/calibdb.cc',
            'src/lib/xalgospp/scheduling/dag_scheduler.cc',
            'src/lib/xalgospp/utilities/interconversion.cc',
            'src/lib/xalgospp/utilities/mongodb.cc',
        ],
        cpp_args: ['-DXALG_BUILD_XALGOSPP_API'],
        dependencies: deps_for_xalgospp,
        include_directories: xalgospp_headers,
        install: true,
        install_dir: libxalgospp_install_dir,
        install_rpath: lib_install_rpath,
        version: '1',
        soversion: '1',
    )

    xalgospp_dep = declare_dependency(
        link_with: [
            libxalgospp,
        ],
        dependencies: deps_for_xalgospp,
        include_directories: xalgospp_headers,
    )

    if not xalg_as_wheel_opt
        install_headers(
            installed_headers,
            install_dir: header_install_dir / 'xalgospp',
        )

        install_headers(
            installed_detector_headers,
            install_dir: header_install_dir / 'xalgospp/detector',
        )

        install_headers(
            installed_detector_lcls2_headers,
            install_dir: header_install_dir / 'xalgospp/detector/lcls2',
        )

        install_headers(
            installed_scheduling_headers,
            install_dir: header_install_dir / 'xalgospp/scheduling',
        )

        install_headers(
            installed_utilities_headers,
            install_dir: header_install_dir / 'xalgospp/utilities',
        )
    endif # not nca_as_wheel_opt
else
    libxalgospp = cpp.find_library('xalgospp', required: true)
    xalgospp_dep = declare_dependency(
        dependencies: deps_for_xalgospp + [
            libxalgospp,
        ],
        include_directories: xalgospp_headers,
    )
endif # end build_core_opt


#### Python Bindings ####
#########################
if build_python_opt
    # Figure out dependencies
    deps_for_py_mod = deps_for_xalgospp + [ncarray_dep, pybind11_dep, py3_dep, xalgospp_dep]

    # If building bindings as a wheel, install the headers into the package itself
    # If not building as a wheel, then these should be installed in prefix/include when
    # building the core libraries
    if xalg_as_wheel_opt
        py3.install_sources(
            installed_headers,
            pure: false,
            subdir: 'xalgospp/include/xalgospp',
        )
        py3.install_sources(
            installed_detector_headers,
            pure: false,
            subdir: 'xalgospp/include/xalgospp/detector',
        )
        py3.install_sources(
            installed_detector_lcls2_headers,
            pure: false,
            subdir: 'xalgospp/include/xalgospp/detector/lcls2',
        )
        py3.install_sources(
            installed_scheduling_headers,
            pure: false,
            subdir: 'xalgospp/include/xalgospp/scheduling',
        )
        py3.install_sources(
            installed_utilities_headers,
            pure: false,
            subdir: 'xalgospp/include/xalgospp/utilities',
        )
    endif # xalg_as_wheel_opt

    # py_bindings_headers = []
    # if xalg_as_wheel_opt
    #     py3.install_sources(
    #         py_bindings_headers,
    #         pure: false,
    #         subdir: 'xalgospp/include/pyxalgospp',
    #     )
    # endif

    py_mod = py3.extension_module(
        '_pyxalgospp',
        [
            'src/lib/pyxalgospp/pyxalgospp.cc',
        ],
        install: true,
        install_dir: pyxalgospp_install_dir,
        include_directories: xalgospp_headers,
        dependencies : deps_for_py_mod,
        install_rpath: py_to_lib_install_rpath,
        build_rpath: py_to_lib_install_rpath,
        # link_args: py_mod_link_args,
    )

    py3.install_sources(
        'src/xalgospp/__init__.py',
        pure: false,
        subdir: 'xalgospp',
    )

    py3.install_sources(
        'src/xalgospp/core/__init__.py',
        pure: false,
        subdir: 'xalgospp/core',
    )
endif # build_python_opt

#### Test executables ####
##########################
# subdir('tests')
