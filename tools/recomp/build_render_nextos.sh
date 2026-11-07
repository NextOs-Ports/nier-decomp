#!/bin/bash
set -euo pipefail
if (( $# != 4 )); then
    echo 'usage: build_render_nextos.sh PRIVATE_SDK PRIVATE_SDK_SOURCE PRIVATE_BUILD PRIVATE_TRANSLATOR' >&2
    exit 2
fi
exec "$(dirname "$0")/build_nextos.sh" "$1" "$2" "$3" \
    -DNIER_RENDER_GLES2=ON -DCMAKE_CXX_FLAGS=-mcpu=cortex-a53 \
    -DNIER_XENOS_TRANSLATOR_DIR="$(realpath "$4")"
