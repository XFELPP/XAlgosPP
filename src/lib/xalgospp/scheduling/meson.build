installed_scheduling_headers = [
    'dag_scheduler.hh',
    'queue.hh',
    'task.hh',
]

if not xalg_as_wheel_opt and build_core_opt
    install_headers(
        installed_scheduling_headers,
        install_dir: header_install_dir / 'xalgospp/scheduling',
    )
else
    py3.install_sources(
        installed_scheduling_headers,
        pure: false,
        subdir: 'xalgospp/include/xalgospp/scheduling',
    )
endif # not xalg_as_wheel_opt and build_core_opt

subdir('tasks')
