if build_core_opt
    libxalgospp = shared_library(
        'xalgospp',
        [            
            'detector/lcls2/calibdb.cc',
            'scheduling/dag_scheduler.cc',
            'utilities/interconversion.cc',
            'utilities/mongodb.cc',
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
else
    libxalgospp = cpp.find_library('xalgospp', required: true)
    xalgospp_dep = declare_dependency(
        dependencies: deps_for_xalgospp + [
            libxalgospp,
        ],
        include_directories: xalgospp_headers,
    )
endif # end build_core_opt

installed_headers = [
    'algorithm.hh',
    'parameters.hh',
]

if not xalg_as_wheel_opt and build_core_opt
    install_headers(
        installed_headers,
        install_dir: header_install_dir / 'xalgospp',
    )
else
    py3.install_sources(
        installed_headers,
        pure: false,
        subdir: 'xalgospp/include/xalgospp',
    )
endif # not xalg_as_wheel_opt and build_core_opt

subdir('detector')
subdir('scheduling')
subdir('utilities')
