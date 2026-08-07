installed_utilities_headers = [
    'dispatch.hh',
    'interconversion.hh',
    'mongodb.hh',
    'type_lists.hh',
]

if not xalg_as_wheel_opt and build_core_opt
    install_headers(
        installed_utilities_headers,
        install_dir: header_install_dir / 'xalgospp/utilities',
    )
else
    py3.install_sources(
        installed_utilities_headers,
        pure: false,
        subdir: 'xalgospp/include/xalgospp/utilities',
    )
endif # not xalg_as_wheel_opt and build_core_opt
