#### Python Bindings ####
#########################
installed_py_scheduling_headers = [
    'scheduling.hh',
]

if build_python_opt
    # Figure out dependencies
    deps_for_py_mod = deps_for_xalgospp + [ncarray_dep, pybind11_dep, py3_dep, xalgospp_dep]

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
        # link_args: py_mod_link_args,
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
        # link_args: py_mod_link_args,
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
        # link_args: py_mod_link_args,
    )
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
