project('hwloc-external', 'c', version: '2.12.2')

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

meson.override_dependency('hwloc', hwloc_dep)
