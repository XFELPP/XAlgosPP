#!/usr/bin/env bash
set -e

## This build script will create an isolated build environment, and cache it for reuse
## By caching, we can use meson (fast) to actually build C++ code.
## pip gets used only at the very end to install the entry-points.

PROJECT_NAME="XAlgosPP"

usage()
{
    cat << EOF
$(basename "$0"):
    Build an installation of ${PROJECT_NAME}.

    This build script will create an isolated build environment. It is cached in
    your home directory under ~/.cache/${PROJECT_NAME}_build_env_XXXX where the final portion
    is created from a hash of base installation directory.

    Subsequent runs of the build will not need to re-create the build environment,
    speeding up the process significantly. You can of course delete the build environment
    from the specified folder at any time, and it will be recreated next time the script
    is run. You can pass a parameter to this script to do cleanup as well.

    Options:
        -c|--clean
          Clean up the build environment
        -h|--help
          Display this message.

    Options that apply on subsequent runs of the build script:
        -e|--entry_points
          Re-run the pip install command. This is only needed if pyproject.toml is
          modified.
        -r|--reconfigure
          Re-run the meson setup. This is only required if meson.build files have been
          modified, or meson options/the install prefix have changed since the last
          time it was run.
EOF
}

POSITIONAL=()

while [[ $# -gt 0 ]]
do
    flag="$1"

    case $flag in
    -c|--clean)
        NEED_CLEANUP=1
        shift
        ;;
    -e|--entry_points)
        NEED_ENTRYPOINTS=1
        shift
        ;;
    -r|--reconfigure)
        NEED_RECONFIG=1
        shift
        ;;
    -h|--help)
        usage
        exit
        ;;
    *)
        POS+=("$1")
        shift
        ;;
    esac
done
set -- "${POS[@]}"

# Bunch of functions to pretty print...
center_text() {
    local TEXT="$1"
    local INNER_WIDTH="$2"
    local LEN=${#TEXT}
    if (( LEN >= INNER_WIDTH )); then
        echo "===== ${TEXT} ====="
    else
        local LEFT_PAD=$(( (INNER_WIDTH - LEN) / 2))
        local RIGHT_PAD=$(( INNER_WIDTH - LEN - LEFT_PAD ))
        # Calculate spaces on each side
        printf "===== %*s%s%*s =====\n" \
               "${LEFT_PAD}" "" \
               "${TEXT}" \
               "${RIGHT_PAD}" ""
        #NEW_LINE=$(printf "%*s%s%*s\n" "${PAD_SIZE}" "" "${TEXT}" "$((WIDTH - LEN - PAD_SIZE))" " ")
        #echo "===== ${NEW_LINE} ====="
    fi
}

## Create a banner that looks like:
## ===============================
## =====         Line 1      =====
## =====         Line 2      =====
## =====         Line 3      =====
## ===============================
## For the LINES that are provided
print_banner() {
    local LINES=("$@")
    local MAXLEN=0
    local NEWLINES=()
    for LINE in "${LINES[@]}"; do
        NEWLINES+=("$LINE")
        (( ${#LINE} > MAXLEN )) && MAXLEN=${#LINE}
    done

    local BORDERLEN=$((MAXLEN+12))
    if (( BORDERLEN < 80)); then
        BORDERLEN=80
        #MAXLEN=$((BORDERLEN - 12))
    fi
    local INNER_WIDTH=$((BORDERLEN - 12))

    # Print top border
    printf '%*s\n' "${BORDERLEN}" '' | tr ' ' '='

    # Print centered lines
    for LINE in "${NEWLINES[@]}"; do
        center_text "${LINE}" "${INNER_WIDTH}"
    done

    # Print the bottom border
    printf '%*s\n' "${BORDERLEN}" '' | tr ' ' '='
}

LINES=("Building an installation of ${PROJECT_NAME}...")
print_banner "${LINES[@]}"

# Determine build directories, install directory, and where to put the build env
BASE_DIR="$( readlink -f "$( dirname "${BASH_SOURCE[0]}" )" )"
BUILD_DIR="${BASE_DIR}/_build"
INSTALL_DIR="${BASE_DIR}/install"

# Virtual environment needs to be outside the source tree
# Otherwise you get some path errors with meson
# At least I couldn't figure out any other way to do it...
BUILD_ENV="${HOME}/.cache/${PROJECT_NAME}_build_env_$(echo ${BASE_DIR} | md5sum | cut -d' ' -f1)"

mkdir -p "${INSTALL_DIR}"
mkdir -p "${HOME}/.cache"

# Save host/conda env Python for later
HOST_PYTHON=$(which python3)

LINES=(
    "Will build and installation of ${PROJECT_NAME} at ${INSTALL_DIR}"
    "(Build cache available at: ${BUILD_DIR})"
)

print_banner "${LINES[@]}"

# Create a build environment if it doesn't yet exist
if [ ! -d "${BUILD_ENV}" ]; then
    LINES=("Creating isolated build environment in ${BUILD_ENV}...")
    print_banner "${LINES[@]}"
    python3 -m venv "${BUILD_ENV}"
    # Source before installing dependencies
    source "${BUILD_ENV}/bin/activate"
    pip install --upgrade pip
    # Install the relevant build dependencies and nothing else
    pip install auditwheel patchelf "meson>=1.10.1" "meson-python" "ninja" "numpy" "pybind11" "setuptools"

    pip install ncarray sbio --extra-index-url https://pypi.xfelpp.org/host/
    # Mark that this is a first-build. This is used to make decisions later
    FIRST_BUILD=1
else
    # If it already exists (you've run build.sh before) just activate it
    LINES=("Activating build environment at ${BUILD_ENV}")
    print_banner "${LINES[@]}"
    source "${BUILD_ENV}/bin/activate"
fi

export PKG_CONFIG_PATH="$(nca-pkg-config --pkg-config-path):${PKG_CONFIG_PATH}"
export PKG_CONFIG_PATH="$(sbio-pkg-config --pkg-config-path):${PKG_CONFIG_PATH}"

# Run meson configure/setup if it hasn't be done yet or it has been requested
# It generally only needs to rerun if install prefix has changed, or meson.build
# files have been modified.
if [ ! -d "${BUILD_DIR}" ]; then
    LINES=("Running meson setup for build configuration")
    print_banner "${LINES[@]}"
    meson setup "${BUILD_DIR}"      \
          --prefix="${INSTALL_DIR}" \
          -Dxalg_as_wheel=true      \
          -Dbuildtype=release       \
          -Dbuild_examples=true     \
          -Dbuild_python=false
elif [[ ${FIRST_BUILD} || ${NEED_RECONFIG} ]]; then
    LINES=("Running meson setup reconfiguration")
    print_banner "${LINES[@]}"
    # Reconfigure in case prefix or options changed, but keep cache
    meson setup "${BUILD_DIR}"      \
          --reconfigure             \
          --prefix="${INSTALL_DIR}" \
          -Dxalg_as_wheel=true      \
          -Dbuildtype=release       \
          -Dbuild_examples=true     \
          -Dbuild_python=false
else
    LINES=(
        "!!!!! Skipping meson setup reconfiguration !!!!!"
        "This is normally fine; however, if you have modified any meson.build"
        "files, have changed meson options, or the install prefix, you should"
        "re-run this script with the -r option to reconfigure meson."
    )
    print_banner "${LINES[@]}"
fi

# Build... This is mostly for XAlgosPP and C/C++ extensions
LINES=("Compiling...")
print_banner "${LINES[@]}"
meson compile -C "${BUILD_DIR}"

# Installation - this always needs to run. This does do the installation of
# the Python source code as well
LINES=("Installing files in ${INSTALL_DIR}...")
print_banner "${LINES[@]}"
meson install -C "${BUILD_DIR}"
PY_VER=$(python3 -V | awk '{print $2}' | cut -d. -f1,2)
export LD_LIBRARY_PATH="${INSTALL_DIR}/lib/python${PY_VER}/site-packages/xalgospp/lib:${LD_LIBRARY_PATH}"
export LIBRARY_PATH="${INSTALL_DIR}/lib/python${PY_VER}/site-packages/xalgospp/lib:${LIBRARY_PATH}"
export LD_FLAGS="-L${INSTALL_DIR}/lib/python${PY_VER}/site-packages/xalgospp/lib ${LD_FLAGS}"

mkdir -p "${INSTALL_DIR}/tmp_wheel"
python3 -m pip wheel .                             \
        --wheel-dir="${INSTALL_DIR}/tmp_wheel"     \
        --no-deps                                  \
        -Csetup-args=-Dxalg_as_wheel=true          \
        -Csetup-args=-Dbuild_core=false            \
        -Csetup-args=-Dbuildtype=release           \
        --no-build-isolation

REPAIRED_WHEEL_DIR="${INSTALL_DIR}/dist"
mkdir -p "${REPAIRED_WHEEL_DIR}"

WHEEL_FILE=$(ls "${INSTALL_DIR}/tmp_wheel"/xalgospp-*.whl | head -n 1)
python3 utilities/repair_wheel.py "${WHEEL_FILE}" "${REPAIRED_WHEEL_DIR}" --no-exclude-core

# Run pip at the end - this gets the entrypoints defined in pyproject.toml
# It can be pointed at the build directory to prevent pip from trying to rebuild
# the rest of the stuff from scratch.

if [[ ${NEED_CLEANUP} ]]; then
    LINES=(
        "!!!!! Cleaning up the build environment !!!!!"
        "It will be re-created if you re-run this script."
    )
    print_banner "${LINES[@]}"
    rm -rf "${BUILD_ENV}"
fi
