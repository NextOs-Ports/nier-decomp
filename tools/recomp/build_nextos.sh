#!/bin/bash
# Cross-build with private copies of the approved SDK and patched source tree.
set -euo pipefail
if (( $# < 3 )); then
    echo 'usage: build_nextos.sh PRIVATE_SDK PRIVATE_SDK_SOURCE PRIVATE_BUILD_DIR [extra CMake flags...]' >&2
    exit 2
fi
: "${NEXTOS_TOOLCHAIN:?Set the NextOS firmware toolchain path}"
: "${REXGLUE_HOST_TOOL:?Set the host rexglue binary path}"
project_root=$(cd "$(dirname "$0")/../.." && pwd)
sdk_prefix=$(realpath "$1")
sdk_source=$(realpath "$2")
build_dir=$(realpath -m "$3")
shift 3
task_temp_root=$(ia-temp codex)
export TMPDIR="$task_temp_root/nier-x1"
mkdir -p "$TMPDIR"
cmake -S "$project_root/port" -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$project_root/port/cmake/aarch64.cmake" \
    -DCMAKE_PREFIX_PATH="$sdk_prefix" -Drexglue_DIR="$sdk_prefix/lib/cmake/rexglue" \
    -Dfmt_DIR="$sdk_prefix/lib/cmake/fmt" -Dspdlog_DIR="$sdk_prefix/lib/cmake/spdlog" \
    -Dutf8cpp_DIR="$sdk_prefix/share/utf8cpp/cmake" \
    -DVulkanHeaders_DIR="$sdk_prefix/share/cmake/VulkanHeaders" \
    -DVulkanMemoryAllocator_DIR="$sdk_prefix/share/cmake/VulkanMemoryAllocator" \
    -DREXGLUE_PLUGIN_SOURCE="$sdk_source" -DREXGLUE_HOST_TOOL="$REXGLUE_HOST_TOOL" \
    -DNIER_RUNTIME_LIBRARY="${NIER_RUNTIME_LIBRARY:-$sdk_source/out/linux-arm64/librexruntime.so}" \
    -DNIER_THINLTO=OFF "$@"
cmake --build "$build_dir" --parallel "${NIER_BUILD_JOBS:-4}"
