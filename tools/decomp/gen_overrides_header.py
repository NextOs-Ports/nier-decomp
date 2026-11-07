#!/usr/bin/env python3
"""Gera o cabeçalho que liga as reconstruções verificadas no build do jogo.

Para cada função `verified` do inventário (progress/functions.json) — ou aprovada numa
rodada do nierverify passada com --run — emite `#define NIER_OVERRIDE_sub_X sub_X`.
Com isso NIER_DECOMP(sub_X) define o símbolo forte `sub_X`, que vence o alias fraco do
código recompilado; as demais reconstruções ficam com nome inerte (NIER_OVERRIDE_sub_Y).

Uso: gen_overrides_header.py <functions.json> <saida.h> [--run rodada.json ...] [--only lista.txt]
  --only: arquivo com um sub_X por linha; só esses entram (subconjunto para A/B).
  --exclude: idem, para tirar funções específicas.
"""
import argparse, json, pathlib, re, sys

ap = argparse.ArgumentParser()
ap.add_argument('inventory'); ap.add_argument('output')
ap.add_argument('--run', action='append', default=[])
ap.add_argument('--only'); ap.add_argument('--exclude')
a = ap.parse_args()
inv = json.load(open(a.inventory))
names = {f['name'] for f in inv['functions'] if f.get('status') == 'verified'}
for r in a.run:
    d = json.load(open(r))
    for x in d['results']:
        if x['pass']: names.add(x['name'])
        else: names.discard(x['name'])
def read_list(p): return {l.strip() for l in open(p) if l.strip() and not l.startswith('#')}
if a.only: names &= read_list(a.only)
if a.exclude: names -= read_list(a.exclude)
# Hooks fortes do render precisam manter captura/estado e sua chamada ao original.
# Não substituir silenciosamente o hook nem produzir dois símbolos fortes no link.
hooks_file = pathlib.Path(__file__).resolve().parents[2] / 'port/render/d3d_hooks.cpp'
hooks = set(re.findall(r'REX_HOOK_RAW\((sub_[0-9A-F]{8})\)', hooks_file.read_text()))
reserved = names & hooks
if reserved:
    print('preservados hooks do render: ' + ', '.join(sorted(reserved)), file=sys.stderr)
names -= hooks
out = ['// GERADO por tools/decomp/gen_overrides_header.py — não editar.',
       f'// {len(names)} reconstruções verificadas substituem o código recompilado.',
       '#pragma once']
out += [f'#define NIER_OVERRIDE_{n} {n}' for n in sorted(names)]
def write_if_changed(path, content):
    if not path.exists() or path.read_text() != content:
        path.write_text(content)
write_if_changed(pathlib.Path(a.output), '\n'.join(out) + '\n')
write_if_changed(pathlib.Path(a.output).with_suffix('.txt'), '\n'.join(sorted(names)) + '\n')
print(f'{len(names)} overrides em {a.output}', file=sys.stderr)
