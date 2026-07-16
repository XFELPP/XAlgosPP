project('mpich', 'c', version: '5.0.1')

ext_mod = import('unstable-external_project')

# This dies if you're using ccache... Extract just the compiler bits and add below...
# BUT NOTE: THIS HAS A VERY AGRESSIVE DEPRECATION WARNING
# "DEPRECATION: Project uses feature that was always broken, and is now deprecated since '1.11.0':
#  Using `meson.get_compiler()` for languages only initialized in another subproject.
#  This is extremely fragile, as your project likely cannot be used outside of your environment."
# TODO: FIND ALTERNATIVE!!

cc = meson.get_compiler('c')
cxx = meson.get_compiler('cpp')

# For MPI configuration we only need the C API (no C++/fortran)
# Must have the shared memory/OSC and so on.
configure_opts = [
    '--prefix=@PREFIX@',
    '--libdir=@PREFIX@/@LIBDIR@',
    '--disable-fortran',
    '--disable-cxx',
    '--enable-shared',
    '--disable-static',
    'CC=' + cc.cmd_array()[-1],
    'CXX=' + cxx.cmd_array()[-1],
]

ucx_dep = dependency('ucx', required: false)
if ucx_dep.found()
    message('UCX found. Configuring MPICH with UCX netmod.')
    configure_opts += [
        '--with-device=ch4:ucx',
        '--with-ucx=auto',
    ]
else
    message('UCX not found. Configuring MPICH with OFI (libfabric) netmod (supports TCP + Shared Memory).')
    configure_opts += [
        '--with-device=ch4:ofi',
        '--with-libfabric=embedded',
    ]
endif

# Ugh... it has problems finding CUDA on its own....
py3 = import('python').find_installation()
cuda_home = run_command(py3, '-c', '''
import os
import shutil
from typing import Optional
cuda_home: Optional[str] = os.environ.get("CUDA_HOME") or os.environ.get("CUDA_PATH")
if not cuda_home:
    nvcc: Optional[str] = shutil.which("nvcc")
    if nvcc:
        cuda_home = os.path.dirname(os.path.dirname(nvcc))
print(cuda_home or "")
''', check: false).stdout().strip()

cuda_dep = dependency('cuda', required: false)

if cuda_dep.found()
    message('CUDA found. Configuring MPICH with CUDA-aware support.')
    if cuda_home != ''
        message('CUDA found and will use installation from: ' + cuda_home)
        configure_opts += [
            '--with-cuda=' + cuda_home,
            # Jeez... This is picky. Libfabric fails to find CUDA as well... unless we do this
            'LDFLAGS=-L' + cuda_home + '/lib64/stubs -L' + cuda_home + '/lib64 -L' + cuda_home + '/lib'
        ]
    else
        message('CUDA found, but not sure where CUDA home is... MPI may fail to compile...')
        configure_opts += [
            '--with-cuda=auto',
        ]
    endif
endif

mpich_proj = ext_mod.add_project(
    'configure',
    configure_options: configure_opts,
)

mpich_dep = mpich_proj.dependency('mpi')
