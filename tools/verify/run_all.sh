#!/bin/bash
# Roda o comparador em N fatias paralelas e junta o resultado num JSON.
# Uso: run_all.sh <build> <default.pe> <saida.json> [N=6] [funções,separadas,por,vírgula]
set -u
B=$1 PE=$2 OUT=$3 N=${4:-6} ONLY=${5:-}
tmp=$(mktemp -d)
for i in $(seq 0 $((N-1))); do
  NIER_SHARD=$i/$N NIER_OUT=$tmp/$i.json NIER_PE=$PE "$B/nierverify" $ONLY >/dev/null 2>&1 &
done
wait
python3 - "$tmp" "$OUT" "$N" <<'PY'
import json, sys, glob
tmp, out, n = sys.argv[1], sys.argv[2], int(sys.argv[3])
res = []; trials = None
for i in range(n):
    d = json.load(open(f'{tmp}/{i}.json')); res += d['results']; trials = d['trials']
json.dump({'tool': 'nierverify', 'trials': trials, 'results': res, 'failed': sum(not r['pass'] for r in res)}, open(out, 'w'), indent=1)
print(f"{len(res)} funções, {sum(r['pass'] for r in res)} aprovadas")
slow = [r for r in res if r.get('time_pass') is False and r['pass'] is False and 'mais lenta' in r['error']]
print(f"tempo: {sum(1 for r in res if r.get('time_pass'))} dentro do gate, {len(slow)} mais lentas que a tradução")
for r in res:
    if not r['pass']: print('REPROVADA', r['name'], r['error'])
PY
