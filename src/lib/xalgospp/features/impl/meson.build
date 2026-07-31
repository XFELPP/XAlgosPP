installed_features_impl_headers = [
    'pf8.hh',
    'pf8.cuh',
]

if not xalg_as_wheel_opt and build_core_opt
    install_headers(
        installed_features_impl_headers,
        install_dir: header_install_dir / 'xalgospp/features/impl',
    )
else
    py3.install_sources(
        installed_features_impl_headers,
        pure: false,
        subdir: 'xalgospp/include/xalgospp/features/impl',
    )
endif # not xalg_as_wheel_opt and build_core_opt
