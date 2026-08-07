project('hwloc-external', 'c', version: '2.12.2')

if host_machine.system() == 'windows'
    # Use CMake module on Windows
    cmake = import('cmake')
    hwloc_cmake_proj = cmake.subproject('hwloc', cmake_options: ['-DBUILD_SHARED_LIBS=OFF'])
    hwloc_dep = hwloc_cmake_proj.dependency('hwloc')
else
    ep = import('unstable-external_project')

    configure_opts = [
      '--prefix=@PREFIX@',
      '--libdir=@PREFIX@/@LIBDIR@',
      '--disable-shared',
      '--enable-static',
    ]

    hwloc = ep.add_project(
      'bootstrap.sh',
      configure_options: configure_opts,
      env: {
        'CFLAGS': '-fPIC',
      },
    )

    hwloc_dep = hwloc.dependency('hwloc')
endif

meson.override_dependency('hwloc', hwloc_dep)
