#!/bin/sh
# Coordinator/C1 physical probe. No clock/governor or frontend changes here.
set -eu
port_root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export NIER_NATIVE_RENDER=1
unset NIER_RENDER_PBUFFER
exec "$port_root/run_device_probe.sh" "$@"
