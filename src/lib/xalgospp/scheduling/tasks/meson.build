installed_scheduling_tasks_headers = [
    'analysis.hh',
    'io.hh',
]

if not xalg_as_wheel_opt and build_core_opt
    install_headers(
        installed_scheduling_tasks_headers,
        install_dir: header_install_dir / 'xalgospp/scheduling/tasks',
    )
else
    py3.install_sources(
        installed_scheduling_tasks_headers,
        pure: false,
        subdir: 'xalgospp/include/xalgospp/scheduling/tasks',
    )
endif # not xalg_as_wheel_opt and build_core_opt
