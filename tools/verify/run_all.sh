#!/bin/bash
# Roda o comparador em N fatias paralelas e junta o resultado num JSON. As funções que
# só reprovaram no gate de tempo são medidas de novo numa 2ª passada com 2 processos e
# 24 repetições (a 1ª passada com N processos na mesma máquina tem ruído de agenda).
# Uso: run_all.sh <build> <default.pe> <saida.json> [N=6] [funções,separadas,por,vírgula]
set -u
B=$1 PE=$2 OUT=$3 N=${4:-6} ONLY=${5:-}
tmp=$(mktemp -d)
for i in $(seq 0 $((N-1))); do
  NIER_SHARD=$i/$N NIER_OUT=$tmp/$i.json NIER_PE=$PE "$B/nierverify" $ONLY >/dev/null 2>&1 &
done
wait
slow=$(python3 - "$tmp" "$N" <<'PY'
import json, sys
tmp, n = sys.argv[1], int(sys.argv[2])
names = []
for i in range(n):
    for r in json.load(open(f'{tmp}/{i}.json'))['results']:
        if not r['pass'] and 'mais lenta' in r['error']: names.append(r['name'])
print(','.join(names))
PY
)
if [ -n "$slow" ]; then
  for i in 0 1; do
    NIER_SHARD=$i/2 NIER_TIME_REPS=24 NIER_OUT=$tmp/slow$i.json NIER_PE=$PE "$B/nierverify" "$slow" >/dev/null 2>&1 &
  done
  wait
fi
python3 - "$tmp" "$OUT" "$N" <<'PY'
import json, sys, glob
import os
tmp, out, n = sys.argv[1], sys.argv[2], int(sys.argv[3])
res = []; trials = None
for i in range(n):
    d = json.load(open(f'{tmp}/{i}.json')); res += d['results']; trials = d['trials']
# 2ª passada do gate de tempo substitui o resultado da 1ª
again = {}
for f in glob.glob(f'{tmp}/slow*.json'):
    for r in json.load(open(f))['results']: again[r['address']] = r
res = [again.get(r['address'], r) for r in res]
# caminhos relativos ao repo (src/decomp/...), para o apply_results em qualquer checkout
for r in res:
    k = r['file'].find('/src/decomp/')
    if k >= 0: r['file'] = r['file'][k + 1:]
json.dump({'tool': 'nierverify', 'trials': trials, 'results': res, 'failed': sum(not r['pass'] for r in res)}, open(out, 'w'), indent=1)
print(f"{len(res)} funções, {sum(r['pass'] for r in res)} aprovadas")
slow = [r for r in res if r.get('time_pass') is False and r['pass'] is False and 'mais lenta' in r['error']]
print(f"tempo: {sum(1 for r in res if r.get('time_pass'))} dentro do gate, {len(slow)} mais lentas que a tradução")
for r in res:
    if not r['pass']: print('REPROVADA', r['name'], r['error'])
PY
