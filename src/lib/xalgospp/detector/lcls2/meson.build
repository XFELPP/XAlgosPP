installed_detector_lcls2_headers = [
    'calibdb.hh',
]

if not xalg_as_wheel_opt and build_core_opt
    install_headers(
        installed_detector_lcls2_headers,
        install_dir: header_install_dir / 'xalgospp/detector/lcls2',
    )
else
    py3.install_sources(
        installed_detector_lcls2_headers,
        pure: false,
        subdir: 'xalgospp/include/xalgospp/detector/lcls2',
    )
endif # not xalg_as_wheel_opt and build_core_opt
