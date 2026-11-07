#!/usr/bin/env python3
"""Prova de link: quais reconstruções venceram o código recompilado no ELF.

Para cada nome da lista (saída .txt do gen_overrides_header.py), lê a tabela de símbolos
com llvm-nm e exige: `sub_X` definido, forte (T) e em endereço diferente de `__imp__sub_X`
(o alias fraco gerado tem o MESMO endereço do __imp__). Grava um JSON com o resultado.

Uso: check_overrides.py <elf> <lista.txt> [--nm llvm-nm] [--json saida.json]
Sai com 1 se alguma reconstrução esperada não venceu.
"""
import argparse, json, pathlib, subprocess, sys

ap = argparse.ArgumentParser()
ap.add_argument('elf'); ap.add_argument('names')
ap.add_argument('--nm', default='llvm-nm'); ap.add_argument('--json')
a = ap.parse_args()
names = [l.strip() for l in open(a.names) if l.strip()]
syms = {}
for line in subprocess.run([a.nm, '--defined-only', a.elf], capture_output=True, text=True, check=True).stdout.splitlines():
    parts = line.split()
    if len(parts) == 3: syms[parts[2]] = (int(parts[0], 16), parts[1])
won, lost, missing = [], [], []
for n in names:
    s = syms.get(n); imp = syms.get('__imp__' + n)
    if s is None: missing.append(n); continue
    if s[1] in ('T', 't') and (imp is None or s[0] != imp[0]): won.append(n)
    else: lost.append({'name': n, 'type': s[1], 'same_as_imp': imp is not None and s[0] == imp[0]})
doc = {'elf': a.elf, 'expected': len(names), 'overrides_won': len(won), 'lost': lost, 'missing': missing}
backend_list = pathlib.Path(a.names).with_suffix('.backends.txt')
backends = backend_list.read_text().split() if backend_list.exists() else []
backend_missing = [n for n in backends if n not in syms or syms[n][1] not in ('T', 't')]
doc.update(backends_expected=len(backends), backends_defined=len(backends)-len(backend_missing),
           backends_missing=backend_missing)
if a.json: json.dump(doc, open(a.json, 'w'), indent=1); open(a.json, 'a').write('\n')
print(f'overrides: {len(won)}/{len(names)} venceram; {len(lost)} perderam; {len(missing)} ausentes')
print(f'backends nativos dos hooks: {len(backends)-len(backend_missing)}/{len(backends)} definidos')
for x in lost[:10]: print('  perdeu', x)
for x in missing[:10]: print('  ausente', x)
for x in backend_missing[:10]: print('  backend ausente', x)
sys.exit(0 if not lost and not missing and not backend_missing else 1)
