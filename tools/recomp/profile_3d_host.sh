#!/bin/bash
# Private outputs; empty user-data for each invocation. perf needs local sudo.
set -euo pipefail
if (( $# != 3 )); then echo 'usage: profile_3d_host.sh BUILD GAME NEW_OUT' >&2; exit 2; fi
build=$(realpath "$1"); game=$(realpath "$2"); out=$(realpath -m "$3")
root=$(cd "$(dirname "$0")/../.." && pwd)
[[ ! -e "$out" ]] || { echo 'NEW_OUT must not exist (preserve saves)' >&2; exit 2; }
mkdir -p "$out"
task_temp_root=$(ia-temp codex)
export TMPDIR="$task_temp_root/nier-x1-h"
mkdir -p "$TMPDIR"
export NIER_INPUT_SCRIPT=$(cat "$root/tools/render/roteiro-3d.txt")
export NIER_GLES2_CAPTURE_EVERY=15
# Keep both measurements under identical driver/profiler conditions.
export SDL_VIDEO_DRIVER=offscreen MESA_GLES_VERSION_OVERRIDE=2.0
sudo -n perf record -e cpu-clock:u -F 199 --call-graph dwarf,16384 \
  --delay=140000 -o "$out/perf.data" -- \
  sudo -u "$(id -un)" env TMPDIR="$TMPDIR" \
    NIER_INPUT_SCRIPT="$NIER_INPUT_SCRIPT" NIER_GLES2_CAPTURE_EVERY=15 \
    SDL_VIDEO_DRIVER=offscreen MESA_GLES_VERSION_OVERRIDE=2.0 \
    "$root/tools/render/run_host.sh" "$build" "$game" "$out/run" 195 \
    --mnk_mode=false > "$out/record.log" 2>&1
sudo -n perf script -i "$out/perf.data" -F comm,pid,tid,time,period,event,ip,sym,dso \
  > "$out/stacks.txt" 2> "$out/decode.log"
