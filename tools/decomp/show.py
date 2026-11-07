#!/usr/bin/env python3
"""Mostra uma função para reconstrução: PowerPC (comentários do código gerado),
pseudocódigo do Ghidra (se exportado) e quem chama / quem é chamado.

Uso: show.py sub_82102878 [--gen <generated/default>] [--ghidra <pasta .c>]
Lê só arquivos privados locais; nada é gravado no repositório.
"""
import argparse, glob, os, re

ap = argparse.ArgumentParser()
ap.add_argument('name')
ap.add_argument('--gen', default=os.environ.get('NIER_GENERATED_DIR', '/mnt/ARQUIVOS/HOME-FELIPE/ports_compile/nier-data/generated/default'))
ap.add_argument('--ghidra', default=os.environ.get('NIER_GHIDRA_C', '/mnt/ARQUIVOS/HOME-FELIPE/ports_compile/nier-data/ghidra-c'))
a = ap.parse_args()
name = a.name if a.name.startswith('sub_') else 'sub_%08X' % int(a.name, 16)
body = None
for f in sorted(glob.glob(os.path.join(a.gen, 'nier_recomp.*.cpp'))):
    s = open(f).read()
    i = s.find('DEFINE_REX_FUNC(%s)' % name)
    if i >= 0:
        body = s[i:s.index('\n}\n', i)]
        break
if body is None: raise SystemExit('função não encontrada no código gerado')
print('=== PowerPC', name)
for line in body.split('\n'):
    t = line.strip()
    if t.startswith('// '): print('   ', t[3:])
    elif t.startswith('loc_'): print(t)
calls = sorted(set(re.findall(r'\b(sub_[0-9A-F]{8}|__imp__\w+|__savegprlr_\d+|__restgprlr_\d+)\(ctx', body)))
print('=== chama:', ', '.join(c for c in calls if 'gprlr' not in c) or '(nada)')
if 'REX_CALL_INDIRECT_FUNC' in body: print('=== tem chamada indireta')
g = os.path.join(a.ghidra, name + '.c')
if os.path.exists(g):
    print('=== Ghidra'); print(open(g).read())
