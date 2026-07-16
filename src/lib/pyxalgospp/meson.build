#### Python Bindings ####
#########################
if build_python_opt
    # Figure out dependencies
    deps_for_py_mod = deps_for_xalgospp + [ncarray_dep, pybind11_dep, py3_dep, xalgospp_dep]

    py_mod = py3.extension_module(
        '_pyxalgospp',
        [
            'pyxalgospp.cc',
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
