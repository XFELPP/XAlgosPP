installed_features_headers = [
    'peakfinder8.hh',
]

if not xalg_as_wheel_opt and build_core_opt
    install_headers(
        installed_features_headers,
        install_dir: header_install_dir / 'xalgospp/features',
    )
else
    py3.install_sources(
        installed_features_headers,
        pure: false,
        subdir: 'xalgospp/include/xalgospp/features',
    )
endif # not xalg_as_wheel_opt and build_core_opt

subdir('impl')
