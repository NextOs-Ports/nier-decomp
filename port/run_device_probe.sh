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
user_data_root=${NIER_USER_DATA_ROOT:-/storage/roms/ports/nier-dev/user-data}
mkdir -p "$log_dir" "$user_data_root"
# Check an actual write, not just mode bits (the game source may be on ro NFS).
# Keep the uniquely-created marker; no existing files or saves are removed.
write_probe=$(mktemp "$user_data_root/.nier-writecheck.XXXXXX")
printf 'NieR writable user-data\n' > "$write_probe"
printf '[nier] game_data_root=%s user_data_root=%s write_probe=%s\n' \
    "$game_root" "$user_data_root" "$write_probe" >&2
export LD_LIBRARY_PATH="$binary_dir${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec timeout -s TERM -k 5 55 "$binary_dir/nier" \
    --game_data_root="$game_root" \
    --cache_root="$log_dir/cache" --gpu_plugin=null --headless \
    --log_file="$log_dir/runtime.log" --log_level=debug "$@" \
    --user_data_root="$user_data_root"
