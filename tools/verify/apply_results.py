#!/usr/bin/env python3
"""Aplica uma rodada do nierverify ao inventário.

Uso: apply_results.py <saida-nierverify.json> [--date AAAA-MM-DD]

- Grava a rodada sanitizada em evidence/verify/<data>.json (sem dados do jogo).
- Função aprovada passa a `verified`, com `source` (arquivo da reconstrução) e a
  rodada como evidência. Função reprovada que estava `verified` volta a `decompiled`.
"""
import argparse, datetime, json, pathlib, sys

root = pathlib.Path(__file__).resolve().parents[2]
ap = argparse.ArgumentParser()
ap.add_argument('run'); ap.add_argument('--date', default=datetime.date.today().isoformat())
a = ap.parse_args()
run = json.load(open(a.run))
inv_path = root / 'progress/functions.json'
inv = json.load(open(inv_path))
by_addr = {f['address']: f for f in inv['functions']}

out_dir = root / 'evidence/verify'; out_dir.mkdir(parents=True, exist_ok=True)
ev_path = out_dir / f'{a.date}.json'
old = json.load(open(ev_path)) if ev_path.exists() else {'results': []}
merged = {r['address']: r for r in old['results']}
changed = 0
for r in run['results']:
    addr = int(r['address'], 16)
    f = by_addr.get(addr)
    if f is None: sys.exit(f'{r["address"]} não está no inventário')
    src = pathlib.Path(r['file']).resolve().relative_to(root).as_posix()
    rec = {'name': r['name'], 'address': r['address'], 'size': f['size'], 'source': src,
           'trials': run['trials'], 'equal': r['equal'], 'both_faulted': r['both_faulted'],
           'pass': r['pass'], 'error': r['error'],
           'time_inputs': r.get('time_inputs'), 'orig_ns': r.get('orig_ns'), 'decomp_ns': r.get('decomp_ns'),
           'time_ratio': r.get('time_ratio'), 'time_pass': r.get('time_pass')}
    merged[r['address']] = rec
    if r['pass']:
        f['status'] = 'verified'; f['source'] = src; f['evidence'] = [ev_path.relative_to(root).as_posix()]
        changed += 1
    elif f.get('status') == 'verified':
        f['status'] = 'decompiled'; f.pop('evidence', None)
doc = {'tool': 'tools/verify/verify_main.cpp', 'executable_sha256': inv['baseline']['executable_sha256'],
       'method': 'Mesmo estado inicial aleatório para a função traduzida (oráculo) e a reconstruída; '
                 'compara r1-r3, r13-r31, f1, f14-f31, memória tocada da imagem, dados de teste, '
                 'área do r13 e pilha acima do r1. Aprovação exige zero divergência e ao menos metade '
                 'das tentativas sem falha de acesso, e tempo da reconstruída <= tempo da tradução '
                 '(mínimo de repetições por entrada, somado nas entradas válidas, só a chamada da função).',
       'results': sorted(merged.values(), key=lambda x: x['address'])}
json.dump(doc, open(ev_path, 'w'), ensure_ascii=False, indent=1); open(ev_path, 'a').write('\n')
json.dump(inv, open(inv_path, 'w'), ensure_ascii=False, indent=1); open(inv_path, 'a').write('\n')
print(f'{changed} funções verificadas; evidência {ev_path.relative_to(root)}')
