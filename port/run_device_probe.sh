#!/bin/sh
# Coordinator-only diagnostic. Device preparation/lock is managed externally.
# 55 s + 5 s forced kill bounds the complete process group to at most 60 s.
set -eu
if [ "$#" -lt 2 ]; then
    echo 'usage: run_device_probe.sh GAME_ROOT PRIVATE_LOG_DIR [runtime options...]' >&2
    exit 2
fi
game_root=$1
log_dir=$2
shift 2
binary_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
mkdir -p "$log_dir"
export LD_LIBRARY_PATH="$binary_dir${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec timeout -s TERM -k 5 55 "$binary_dir/nier" \
    --game_data_root="$game_root" --user_data_root="$log_dir/user" \
    --cache_root="$log_dir/cache" --gpu_plugin=null --headless \
    --log_file="$log_dir/runtime.log" --log_level=debug "$@"
