#### Python Bindings ####
#########################
installed_py_scheduling_headers = [
    'scheduling.hh',
]

if build_python_opt
    # Figure out dependencies
    deps_for_py_mod = deps_for_xalgospp + [ncarray_dep, pybind11_dep, py3_dep, xalgospp_dep]

    # Add DELAYLOAD for Windows as needed
    py_delay_lib_link_args = []
    if xalg_has_cuda and host_machine.system() == 'windows' and cpp.get_id() == 'msvc'
        py_delay_lib_link_args = [
            '/DELAYLOAD:nvcuda.dll',
            'delayimp.lib',
        ]
    endif

    py_mod = py3.extension_module(
        'detector',
        [
            'detector.cc',
        ],
        install: true,
        install_dir: pyxalgospp_install_dir,
        include_directories: xalgospp_headers,
        dependencies : deps_for_py_mod,
        install_rpath: py_to_lib_install_rpath,
        build_rpath: py_to_lib_install_rpath,
        link_args: py_delay_lib_link_args,
    )

    py_mod = py3.extension_module(
        'features',
        [
            'features.cc',
        ],
        install: true,
        install_dir: pyxalgospp_install_dir,
        include_directories: xalgospp_headers,
        dependencies : deps_for_py_mod,
        install_rpath: py_to_lib_install_rpath,
        build_rpath: py_to_lib_install_rpath,
        link_args: py_delay_lib_link_args,
    )

    py_mod = py3.extension_module(
        'scheduling',
        [
            'scheduling.cc',
        ],
        install: true,
        install_dir: pyxalgospp_install_dir,
        include_directories: xalgospp_headers,
        dependencies : deps_for_py_mod,
        install_rpath: py_to_lib_install_rpath,
        build_rpath: py_to_lib_install_rpath,
        link_args: py_delay_lib_link_args,
    )

    if xalg_has_cuda
        # Provide the stub for Linux
        # Seems that libnvidia-ml.so can be required too
        if host_machine.system() == 'linux'
            fs = import('fs')
            nvcc_bin = find_program('nvcc')
            nvcc_path = nvcc_bin.full_path()

            cuda_root = fs.parent(fs.parent(nvcc_path))

            cuda_stub_dirs = [
                cuda_root / 'lib64/stubs',
                cuda_root / 'lib/stubs',
                # Conda dirs.
                cuda_root / 'targets/x86_64-linux/lib/stubs',
                cuda_root / 'targets/sbsa-linux/lib/stubs',
            ]
            cuda_stubs = [
                ['libcuda.so', true],
                ['libnvidia-ml.so', false],
            ]

            foreach stub : cuda_stubs
                stub_name = stub[0]
                stub_required = stub[1]

                stub_path = ''
                foreach lib_dir : cuda_stub_dirs
                    if stub_path == '' and fs.exists(lib_dir / stub_name)
                        stub_path = lib_dir / stub_name
                    endif
                endforeach

                if stub_path != ''
                    message('**** Installing libcuda stub library: ' + stub_path + ' ****')
                    install_data(
                        stub_path,
                        install_dir: py3.get_install_dir() / 'xalgospp/core/stubs',
                    )
                elif stub_required
                    # Need at least libcuda.so. Not sure on the others.
                    error('Cannot find CUDA driver stub (libcuda.so) from CUDA root: ' + cuda_root)
                else
                    warning('Cannot find CUDA stub (' + stub_name + ') from CUDA root: ' + cuda_root)
                endif
            endforeach
        endif # Linux guard
    endif # xalg_has_cuda
endif # build_python_opt

if not xalg_as_wheel_opt and build_core_opt
    install_headers(
        installed_py_scheduling_headers,
        install_dir: header_install_dir / 'pyxalgospp',
    )
else
    py3.install_sources(
        installed_py_scheduling_headers,
        pure: false,
        subdir: 'xalgospp/include/pyxalgospp',
    )
endif # not xalg_as_wheel_opt and build_core_opt
