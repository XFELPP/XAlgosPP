installed_detector_headers = [
    'calibration.hh',
    'detector.hh',
]

if not xalg_as_wheel_opt and build_core_opt
    install_headers(
        installed_detector_headers,
        install_dir: header_install_dir / 'xalgospp/detector',
    )
else
    py3.install_sources(
        installed_detector_headers,
        pure: false,
        subdir: 'xalgospp/include/xalgospp/detector',
    )
endif # not xalg_as_wheel_opt and build_core_opt

subdir('lcls2')
