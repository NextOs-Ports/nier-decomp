#!/bin/bash
# Build only a private, already prepared SDK copy. Never modify the reference.
set -euo pipefail
if (( $# != 3 )); then
    echo 'usage: build_runtime_sdk.sh PRIVATE_SOURCE PRIVATE_BUILD host|arm64' >&2
    exit 2
fi
project_root=$(cd "$(dirname "$0")/../.." && pwd)
sdk_source=$(realpath "$1")
build_dir=$(realpath -m "$2")
platform=$3
task_temp_root=$(ia-temp codex)
export TMPDIR="$task_temp_root/nier-x1"
mkdir -p "$TMPDIR"
case "$platform" in
    host)
        options=(-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
            -DCMAKE_C_FLAGS=-msse4.1 -DCMAKE_CXX_FLAGS=-msse4.1
            -DREXGLUE_USE_SYSTEM_SDL3=OFF -DREXGLUE_LINUX_DESKTOP_WSI=ON)
        ;;
    arm64)
        : "${NEXTOS_TOOLCHAIN:?Set the NextOS firmware toolchain path}"
        options=(-DCMAKE_TOOLCHAIN_FILE="$project_root/port/cmake/aarch64.cmake"
            -DREXGLUE_USE_SYSTEM_SDL3=ON -DREXGLUE_LINUX_DESKTOP_WSI=OFF)
        ;;
    *) exit 2 ;;
esac
cmake -S "$sdk_source" -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DREXGLUE_ENABLE_TRACY=OFF -DREXGLUE_BUILD_TESTS=OFF \
    -DREXGLUE_ENABLE_FIDELITYFX=OFF "${options[@]}"
cmake --build "$build_dir" --target rexruntime --parallel "${NIER_BUILD_JOBS:-4}"
