#!/usr/bin/env python3
"""Mostra o efeito de uma rodada do nierverify na porcentagem SEM gravar progress/*
(o coordenador integra com apply_results.py). Uso: preview_progress.py <run.json> [--json saida]"""
import argparse, json, pathlib

root = pathlib.Path(__file__).resolve().parents[2]
ap = argparse.ArgumentParser()
ap.add_argument('run'); ap.add_argument('--json'); ap.add_argument('--lote', default='')
a = ap.parse_args()
run = json.load(open(a.run))
inv = json.load(open(root / 'progress/functions.json'))
fs = inv['functions']; by_addr = {f['address']: f for f in fs}
total = sum(f['size'] for f in fs)
before_f = sum(1 for f in fs if f['status'] == 'verified'); before_b = sum(f['size'] for f in fs if f['status'] == 'verified')
status = {f['address']: f['status'] for f in fs}
gained, lost, slow = [], [], []
for r in run['results']:
    addr = int(r['address'], 16); f = by_addr.get(addr)
    if f is None: continue
    if r['pass']:
        if status[addr] != 'verified': gained.append((r['name'], f['size']))
        status[addr] = 'verified'
    else:
        if status[addr] == 'verified': lost.append((r['name'], f['size'], r['error'][:80])); status[addr] = 'decompiled'
        if r.get('time_pass') is False and r['equal'] >= 500: slow.append((r['name'], f['size'], r.get('time_ratio')))
after_f = sum(1 for s in status.values() if s == 'verified'); after_b = sum(by_addr[k]['size'] for k, s in status.items() if s == 'verified')
doc = {'lote': a.lote, 'run': str(pathlib.Path(a.run).resolve()), 'trials': run['trials'],
       'results': len(run['results']), 'passed': sum(r['pass'] for r in run['results']),
       'before': {'functions': before_f, 'bytes': before_b, 'percent': round(100 * before_b / total, 3)},
       'after': {'functions': after_f, 'bytes': after_b, 'percent': round(100 * after_b / total, 3)},
       'gained': len(gained), 'gained_bytes': sum(s for _, s in gained), 'lost': lost, 'slow_but_equal': slow,
       'inventory_bytes': total}
print(json.dumps(doc, ensure_ascii=False, indent=1))
if a.json: json.dump(doc, open(a.json, 'w'), ensure_ascii=False, indent=1); open(a.json, 'a').write('\n')
