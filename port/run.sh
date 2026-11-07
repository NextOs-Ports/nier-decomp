#!/bin/sh
# Experimental normal launch: persistent port-owned content, no test timer.
set -eu
port_root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
mkdir -p "$port_root/user-data" "$port_root/cache"
unset SDL_AUDIODRIVER SDL_AUDIO_DRIVER AUDIODEV SDL_AUDIO_ALSA_DEFAULT_PLAYBACK_DEVICE
export LD_LIBRARY_PATH="$port_root${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$port_root/nier" --game_data_root="${NIER_GAME_ROOT:-$port_root/game}" \
    --user_data_root="$port_root/user-data" --cache_root="$port_root/cache" \
    --gpu_plugin=null --mnk_mode=false "$@"
