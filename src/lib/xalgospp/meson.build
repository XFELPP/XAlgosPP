if build_core_opt
    xalg_sources = [
        'detector/lcls2/calibdb.cc',
        'features/impl/pf8.cc',
        'scheduling/dag_scheduler.cc',
        'utilities/interconversion.cc',
        'utilities/mongodb.cc',
    ]

    # Add DELAYLOAD for Windows as needed
    delay_lib_link_args = []
    if xalg_has_cuda and host_machine.system() == 'windows' and cpp.get_id() == 'msvc'
        delay_lib_link_args = [
            '/DELAYLOAD:nvcuda.dll',
            'delayimp.lib',
        ]
    endif

    # There seems to be no possible way to have a soversion included in the
    # import lib .lib in Windows. This causes problems with delvewheel when trying
    # to package the import libs in Python wheels. Just remove it for Windows....
    if host_machine.system() == 'windows'
        xalg_version_kwargs = {}
    else
        xalg_version_kwargs = {'version': '1', 'soversion': '1'}
    endif

    libxalgospp = shared_library(
        'xalgospp',
        xalg_sources,
        cpp_args: ['-DXALG_BUILD_API'],
        dependencies: deps_for_xalgospp,
        include_directories: xalgospp_headers,
        install: true,
        install_dir: libxalgospp_install_dir,
        install_rpath: lib_install_rpath,
        link_args: delay_lib_link_args,
        kwargs: xalg_version_kwargs,
    )

    xalgospp_dep = declare_dependency(
        link_with: [
            libxalgospp,
        ],
        dependencies: deps_for_xalgospp,
        include_directories: xalgospp_headers,
    )

    if xalg_has_cuda
        xalg_dev_sources = [
            'features/impl/pf8.cu',
        ]

        libdevxalgospp = shared_library(
            'devxalgospp',
            xalg_dev_sources,
            cpp_args: ['-DXALG_BUILD_API'],
            cuda_args: xalg_cuda_args + ['-DNCA_BUILD_NCARRAY_API'],
            dependencies: deps_for_xalgospp,
            include_directories: xalgospp_headers,
            install: true,
            install_dir: libxalgospp_install_dir,
            install_rpath: lib_install_rpath,
            link_args: delay_lib_link_args,
            kwargs: xalg_version_kwargs,

            # Force C++ compiler linkage (no RDC so fine)
            link_language: 'cpp',
            override_options: ['cpp_std=c++20', 'cuda_std=c++20'],
        )

        devxalgospp_dep = declare_dependency(
            link_with: [
                libdevxalgospp,
            ],
            dependencies: deps_for_xalgospp,
            include_directories: xalgospp_headers,
        )
    endif # xalg_has_cuda
else
    libxalgospp = cpp.find_library('xalgospp', required: true)
    xalgospp_dep = declare_dependency(
        dependencies: deps_for_xalgospp + [
            libxalgospp,
        ],
        include_directories: xalgospp_headers,
    )

    libdevxalgospp = cpp.find_library('devxalgospp', required: true)
    devxalgospp_dep = declare_dependency(
        dependencies: deps_for_xalgospp + [
            libdevxalgospp,
        ],
        include_directories: xalgospp_headers,
    )
endif # end build_core_opt

installed_headers = [
    'algorithm.hh',
    'export_macro.hh',
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
subdir('features')
subdir('scheduling')
subdir('utilities')
