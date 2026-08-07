project('hwloc-external', 'c', version: '2.12.2')

ep = import('unstable-external_project')

if host_machine.system() == 'windows'
    pymod = import('python')

    py3 = pymod.find_installation(pure: false)

    # Fill in the config data....
    hwloc_cfg_data = configuration_data()

    hwloc_cfg_data.set('HWLOC_WIN_SYS', 1)
    hwloc_cfg_data.set('HWLOC_HAVE_WINDOWS_H', 1)
    hwloc_cfg_data.set('hwloc_pid_t', 'HANDLE')
    hwloc_cfg_data.set('hwloc_thread_t', 'HANDLE')

    # Assume x86-64 MSVC Windows builds only for now...
    hwloc_cfg_data.set('HWLOC_X86_32_ARCH', 0)
    hwloc_cfg_data.set('HWLOC_X86_64_ARCH', 1)
    hwloc_cfg_data.set('HWLOC_HAVE_X86_CPUID', 1)
    hwloc_cfg_data.set('HWLOC_HAVE_MSVC_CPUIDEX', 1)

    # Need types and sizes.....
    hwloc_cfg_data.set('HWLOC_SIZEOF_UNSIGNED_INT', 4)
    hwloc_cfg_data.set('HWLOC_SIZEOF_UNSIGNED_LONG', 4)
    hwloc_cfg_data.set('SIZEOF_VOID_P', 8)
    hwloc_cfg_data.set('SIZEOF_UNSIGNED_INT', 4)
    hwloc_cfg_data.set('SIZEOF_UNSIGNED_LONG', 4)

    # Need version info ....
    hwloc_cfg_data.set('HWLOC_VERSION', '"2.12.2"')
    hwloc_cfg_data.set('HWLOC_VERSION_MAJOR', 2)
    hwloc_cfg_data.set('HWLOC_VERSION_MINOR', 12)
    hwloc_cfg_data.set('HWLOC_VERSION_RELEASE', 2)
    hwloc_cfg_data.set('HWLOC_VERSION_GREEK', '""')

    # Need symbol prefixes ....
    hwloc_cfg_data.set('HWLOC_SYM_TRANSFORM', 0)
    hwloc_cfg_data.set('HWLOC_SYM_PREFIX', 'hwloc_')
    hwloc_cfg_data.set('HWLOC_SYM_PREFIX_CAPS', 'HWLOC_')

    copy_cfg_cmd = '''
import os
import shutil
import sys

os.makedirs(sys.argv[2], exist_ok=True)
shutil.copyfile(sys.argv[1], os.path.join(sys.argv[2], "config.h"))
'''

    configure_file(
        # input: 'include/private/autogen/config.h.in', ## Which to use???
        input: 'contrib/windows-cmake/private_config.h.in',
        output: 'priv_config.h',
        configuration: hwloc_cfg_data,
        format: 'cmake', # deal with `cmakedefine`
    )

    run_command(
        py3, '-c', copy_cfg_cmd,
        meson.current_build_dir() / 'priv_config.h',
        meson.current_build_dir() / 'include/private/autogen',
        check: true,
    )

    configure_file(
        input: 'contrib/windows/hwloc_config.h',
        # input: 'include/hwloc/autogen/config.h.in',
        output: 'pub_config.h',
        copy: true,
    )

    run_command(
        py3, '-c', copy_cfg_cmd,
        meson.current_build_dir() / 'pub_config.h',
        meson.current_build_dir() / 'include/hwloc/autogen',
        check: true,
    )

    hwloc_headers = include_directories(
        'include',
        'contrib/windows',
    )

    hwloc_sources = files(
        'hwloc/topology.c',
        'hwloc/traversal.c',
        'hwloc/distances.c',
        'hwloc/memattrs.c',
        'hwloc/cpukinds.c',
        'hwloc/components.c',
        'hwloc/bind.c',
        'hwloc/bitmap.c',
        'hwloc/pci-common.c',
        'hwloc/diff.c',
        'hwloc/shmem.c',
        'hwloc/misc.c',
        'hwloc/base64.c',
        'hwloc/topology-noos.c',
        'hwloc/topology-synthetic.c',
        'hwloc/topology-xml.c',
        'hwloc/topology-xml-nolibxml.c',
        'hwloc/topology-windows.c',
        'hwloc/topology-x86.c'
    )

    hwloc_lib = static_library(
        'hwloc',
        hwloc_sources,
        include_directories: hwloc_headers,
        c_args: ['-D_CRT_SECURE_NO_WARNINGS', '-DHWLOC_WIN_SYS'],
    )

    hwloc_dep = declare_dependency(
        link_with: hwloc_lib,
        include_directories: hwloc_headers
    )
else
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
