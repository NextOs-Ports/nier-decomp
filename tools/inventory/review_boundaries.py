#!/usr/bin/env python3
"""Revisa limites do inventário contra a imagem PE (privada).

Uso: review_boundaries.py <default.pe> <inventario.json> <saida.json>
- Para cada sobreposição: conta chamadas `bl` e saltos `b` para a entrada cortada.
- Para cada lacuna do .text fora do inventário: classifica como enchimento
  (zeros / 0x00000000 / nop / trap) ou código.
Não altera o inventário; produz um relatório para a revisão.
"""
import json, struct, sys, collections

pe_path, inv_path, out_path = sys.argv[1:4]
pe = open(pe_path, 'rb').read()
inv = json.load(open(inv_path))
BASE = 0x82000000
text = inv['baseline']['code_ranges'][0]
t0, tsz = text['address'], text['size']

def word(va):
    o = va - BASE
    return struct.unpack('>I', pe[o:o + 4])[0]

# Alvos de bl/b em todo o .text
calls = collections.Counter(); jumps = collections.Counter()
for va in range(t0, t0 + tsz, 4):
    w = word(va)
    if w >> 26 == 18:
        li = w & 0x03FFFFFC
        if li & 0x02000000: li -= 0x04000000
        tgt = (li if w & 2 else va + li) & 0xFFFFFFFF
        (calls if w & 1 else jumps)[tgt] += 1

funcs = sorted((f for f in inv['functions'] if f.get('boundary_origin') != 'import-thunk'),
               key=lambda f: f['address'])
starts = {f['address'] for f in funcs}

report = {'overlaps': [], 'gaps': []}
for x in inv['analysis']['boundary_issues']:
    a = x['address']; nxt = a + x['clipped_size']
    report['overlaps'].append({
        'address': hex(a), 'next_entry': hex(nxt),
        'original_size': x['original_size'], 'clipped_size': x['clipped_size'],
        'calls_to_next': calls[nxt], 'jumps_to_next': jumps[nxt],
        'calls_to_self': calls[a],
        'verdict': 'shared-tail-entry' if calls[nxt] or jumps[nxt] else 'review',
    })

PAD = {0x00000000, 0x60000000, 0x7FE00008}
cur = t0
for f in funcs:
    if f['address'] > cur:
        ws = [word(v) for v in range(cur, f['address'], 4)]
        kind = 'padding' if all(w in PAD for w in ws) else 'code-or-data'
        report['gaps'].append({'address': hex(cur), 'size': f['address'] - cur, 'kind': kind,
                               'calls_in': sum(calls[v] for v in range(cur, f['address'], 4))})
    cur = max(cur, f['address'] + f['size'])
if cur < t0 + tsz:
    report['gaps'].append({'address': hex(cur), 'size': t0 + tsz - cur, 'kind': 'tail', 'calls_in': 0})

s = collections.Counter(o['verdict'] for o in report['overlaps'])
g = collections.defaultdict(int)
for x in report['gaps']: g[x['kind']] += x['size']
report['summary'] = {'overlaps': dict(s), 'gap_bytes_by_kind': dict(g),
                     'gap_count': len(report['gaps']),
                     'gaps_with_calls': sum(1 for x in report['gaps'] if x['calls_in'])}
json.dump(report, open(out_path, 'w'), indent=1)
print(json.dumps(report['summary'], indent=1))
