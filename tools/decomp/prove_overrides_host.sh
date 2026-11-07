#!/bin/bash
# Prova no PC das reconstruções ligadas no jogo: roda a referência (build sem overrides) e o
# candidato (build com overrides) com o MESMO roteiro até o 3D, capturando o PCM do runtime
# (interposer privado) e os quadros GLES2; depois compara chegada ao 3D e métricas do áudio.
# Uso: prove_overrides_host.sh BUILD_REF BUILD_CAND GAME OUT [segundos<=300]
set -euo pipefail
ref=$(realpath "$1"); cand=$(realpath "$2"); game=$(realpath "$3"); out=$(realpath -m "$4"); secs=${5:-195}
root=$(cd "$(dirname "$0")/../.." && pwd)
pcm_so=${NIER_PCM_SO:-/mnt/ARQUIVOS/HOME-FELIPE/ports_compile/nier-build/x1/wait-probe/libnier_pcm_capture.so}
[ -f "$pcm_so" ] || { echo "interposer de PCM ausente: $pcm_so" >&2; exit 2; }
mkdir -p "$out"
export NIER_INPUT_SCRIPT
NIER_INPUT_SCRIPT=$(cat "$root/tools/render/roteiro-3d.txt")
export NIER_GLES2_CAPTURE_EVERY=15 SDL_VIDEO_DRIVER=offscreen MESA_GLES_VERSION_OVERRIDE=2.0
run_one() {   # nome build
  local name=$1 build=$2
  mkdir -p "$out/$name/pcm"
  LD_PRELOAD="$pcm_so" NIER_PCM_DIR="$out/$name/pcm" \
    "$root/tools/render/run_host.sh" "$build" "$game" "$out/$name/run" "$secs" --mnk_mode=false \
    > "$out/$name/record.log" 2>&1 || true
}
run_one referencia "$ref" &
run_one candidato "$cand" &
wait
python3 - "$out" <<'PY'
import glob, json, os, re, subprocess, sys
out = sys.argv[1]
rep = {}
for name in ('referencia', 'candidato'):
    con = open(f'{out}/{name}/run/console.log', errors='replace').read()
    gl = re.findall(r'\[nier-gl\] (\d+) quadros(?: \(janela \d+\))?, ([\d.]+) ms, (\d+) draws no quadro', con)
    frames = [(int(a), float(b), int(c)) for a, b, c in gl]
    max_draws = max((c for _, _, c in frames), default=0)
    late = [c for f, _, c in frames if f >= frames[-1][0] - 600] if frames else []
    wavs = sorted(glob.glob(f'{out}/{name}/pcm/*.wav'))
    status = open(f'{out}/{name}/run/exit-status.txt').read().strip() if os.path.exists(f'{out}/{name}/run/exit-status.txt') else '?'
    rep[name] = {'frames': frames[-1][0] if frames else 0, 'max_draws': max_draws, 'late_draws_avg': sum(late) / len(late) if late else 0,
                 'reached_3d': max_draws >= 100, 'wavs': [os.path.basename(w) for w in wavs], 'exit_status': status,
                 'gl_errors': len(re.findall(r'GL 0x[1-9a-fA-F]', con))}
    for w in wavs[:1]:
        try:
            r = subprocess.run([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)) if '__file__' in dir() else '.', 'x')], capture_output=True)
        except Exception: pass
json.dump(rep, open(f'{out}/resumo.json', 'w'), indent=1)
print(json.dumps(rep, indent=1))
PY
# métricas de áudio (RMS/pico/espectro) por lado, se houver NumPy
for name in referencia candidato; do
  for w in "$out/$name"/pcm/*.wav; do
    [ -f "$w" ] || continue
    python3 "$root/tools/recomp/analyze_pcm.py" "$w" "$out/$name/pcm-analise" > "$out/$name/pcm-metricas.txt" 2>&1 || true
    break
  done
done
echo "resumo em $out/resumo.json; métricas de áudio em $out/*/pcm-metricas.txt"
