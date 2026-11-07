#!/bin/sh
# Rodada do NieR no PC com o renderizador GLES2 (C1). Sem entradas de controle; corte duro por timeout.
# Uso: run_host.sh BUILD_DIR GAME_ROOT OUT_DIR [segundos<=300] [opções do runtime...]
set -eu
build=$1 game=$2 out=$3
secs=${4:-90}
[ "$secs" -le 300 ] || { echo "limite da casa: no máximo 300 s" >&2; exit 2; }
shift 3; [ $# -gt 0 ] && shift
mkdir -p "$out/capturas" "$out/shaders" "$out/user" "$out/cache"
# O link do host copia a librexruntime.so do SDK do Rayman (sem os patches NieR: teclado padrão "Nier", perfil
# local). Sem eles o nome é recusado para sempre ("Please enter another name"). Usa a do X1 quando faltar o patch.
x1lib=/mnt/ARQUIVOS/HOME-FELIPE/ports_compile/nier-build/x1/host/librexruntime.so
if ! strings "$build/librexruntime.so" 2>/dev/null | grep -q default_keyboard_text; then
  if [ -f "$x1lib" ] && strings "$x1lib" | grep -q default_keyboard_text; then
    cp -a "$x1lib" "$build/librexruntime.so"; echo "run_host: librexruntime.so sem patch NieR; copiada a do X1 ($(sha256sum "$x1lib" | cut -c1-12))" >&2
  else echo "run_host: AVISO: librexruntime.so sem patch do teclado e sem lib do X1" >&2; fi
fi
export NIER_NATIVE_RENDER=1
export NIER_GLES2_CAPTURE="$out/capturas"
export NIER_GLES2_SHADERS="$out/shaders"
export SDL_VIDEO_DRIVER="${SDL_VIDEO_DRIVER:-offscreen}"
export LD_LIBRARY_PATH="$build${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
set +e
timeout -s TERM -k 5 "$secs" "$build/nier" --game_data_root="$game" --user_data_root="$out/user" \
  --cache_root="$out/cache" --gpu_plugin=null --headless --log_file="$out/runtime.log" --log_level=info "$@" \
  > "$out/console.log" 2>&1
status=$?
echo "$status" > "$out/exit-status.txt"
for p in $(pgrep -x nier); do [ "$(readlink /proc/$p/exe 2>/dev/null)" = "$(readlink -f "$build/nier")" ] && echo "AVISO: processo $p deste build restante" >&2; done
exit 0
