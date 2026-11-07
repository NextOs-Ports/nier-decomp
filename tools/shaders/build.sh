#!/bin/sh
# Compila tools/shaders/nier_shaders contra o tradutor Xenos->ESSL100 do DMC3 (repo privado
# NextOs-Ports/dmc3-hd-nextos, runtime/xenos_shader.cpp). O código do DMC3 não é copiado para cá.
# DMC3_RUNTIME aponta para o diretório runtime/ de um checkout no commit fixado abaixo.
set -e
DMC3_RUNTIME=${DMC3_RUNTIME:-/mnt/NEXTOS-COMPILACAO/dmc3-fps/t1/runtime}
PIN=c76b074
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${1:-/mnt/ARQUIVOS/HOME-FELIPE/ports_compile/nier-build/c1/shaders}
mkdir -p "$OUT"
c++ -O2 -std=c++20 -DNEXTOS_GLES2 -I"$DMC3_RUNTIME" "$HERE/nier_shaders.cpp" "$DMC3_RUNTIME/xenos_shader.cpp" -o "$OUT/nier_shaders"
echo "nier_shaders -> $OUT/nier_shaders (tradutor DMC3 $PIN, xenos_shader.cpp sha256 $(sha256sum "$DMC3_RUNTIME/xenos_shader.cpp" | cut -c1-16))"
