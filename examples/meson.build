if build_examples_opt
    deps_for_examples = deps_for_xalgospp + [xalgospp_dep]

    executable(
        'test_scheduler',
        'test_scheduler.cc',
        include_directories: xalgospp_headers,
        dependencies: deps_for_examples,
        install_rpath: exe_install_rpath,
        install: true,
    )
endif
