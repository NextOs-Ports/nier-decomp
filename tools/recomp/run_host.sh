#!/bin/bash
# Authorized CPU boot probe; process group is killed mechanically within 125 s.
set -euo pipefail
if (( $# < 3 )); then
    echo 'usage: run_host.sh BINARY GAME_ROOT PRIVATE_LOG_DIR [extra arguments...]' >&2
    exit 2
fi
binary=$(realpath "$1")
game_root=$(realpath "$2")
log_dir=$(realpath -m "$3")
shift 3
mkdir -p "$log_dir"
task_temp_root=$(ia-temp codex)
export TMPDIR="$task_temp_root/nier-x1"
mkdir -p "$TMPDIR"
ulimit -c 0
export SDL_VIDEODRIVER=${SDL_VIDEODRIVER:-x11}
exec_timeout=${NIER_TEST_SECONDS:-120}
if [[ ! "$exec_timeout" =~ ^[0-9]+$ ]] || (( exec_timeout < 1 || exec_timeout > 120 )); then
    echo "NIER_TEST_SECONDS must be between 1 and 120" >&2
    exit 2
fi
set +e
python3 "$(dirname "$0")/run_bounded.py" "$log_dir/resources.json" timeout --signal=TERM --kill-after=5 "$exec_timeout" \
    "$binary" --game_data_root="$game_root" --user_data_root="$log_dir/user" \
    --cache_root="$log_dir/cache" --gpu_plugin=null --headless \
    --log_level="${NIER_LOG_LEVEL:-debug}" --log_file="$log_dir/runtime.log" --log_max_file_size_mb=20 \
    --log_max_files=3 "$@" >"$log_dir/console.log" 2>&1
result=$?
set -e
printf '%s\n' "$result" > "$log_dir/exit-status.txt"
exit "$result"
