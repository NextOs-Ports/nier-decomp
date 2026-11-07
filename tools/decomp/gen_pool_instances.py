#!/usr/bin/env python3
"""Localiza as instâncias do template de pool do NieR e gera o registro delas.

Compara a estrutura de cada função do inventário (opcode e registradores, sem os
imediatos de 16 bits) com as três formas de referência já reconstruídas à mão em
src/decomp/game/pools.cpp, extrai os deslocamentos e escreve
src/decomp/game/pool_instances.cpp. Cada instância é provada pelo tools/verify.

Uso: gen_pool_instances.py <default.pe privado>
"""
import json, pathlib, re, struct, sys

root = pathlib.Path(__file__).resolve().parents[2]
pe = open(sys.argv[1], 'rb').read()
inv = json.load(open(root / 'progress/functions.json'))['functions']
B = 0x82000000
def words(a, n): return struct.unpack_from('>%dI' % (n // 4), pe, a - B)
def s16(w): w &= 0xFFFF; return w - 0x10000 if w & 0x8000 else w

REF = {'take': (0x821538D0, 128), 'build': (0x82153758, 156), 'take_late': (0x82155C60, 164),
       'release': (0x82103970, 176)}
refw = {k: words(a, n) for k, (a, n) in REF.items()}
def same_shape(w, r): return len(w) == len(r) and all((x & 0xFFFF0000) == (y & 0xFFFF0000) for x, y in zip(w, r))

done = set()
for f in (root / 'src/decomp').rglob('*.cpp'):
    if f.name == 'pool_instances.cpp': continue
    done |= set(int(m, 16) for m in re.findall(r'NIER_DECOMP\(sub_([0-9A-F]{8})\)', f.read_text()))

out = []
for fn in inv:
    a, n = fn['address'], fn['size']
    for kind, (ra, rn) in REF.items():
        if n != rn or a in done: continue
        w = words(a, n)
        if not same_shape(w, refw[kind]): continue
        if kind == 'take':
            free, nxt, prv, used, head, count, tail = (w[1] & 0xFFFF, w[4] & 0xFFFF, w[9] & 0xFFFF, w[11] & 0xFFFF,
                                                         w[12] & 0xFFFF, w[19] & 0xFFFF, w[27] & 0xFFFF)
            call = f'pool_take(base, ctx.r3.u32, {{{prv}, {nxt}, {used}}}, {{{tail}, {head}, {free}, {count}}})'
            hi = max(free, head, count, tail)
            spec = f'@r3.{free}=ptr @r3.{head}=ptr'
            body = f'ctx.r3.u64 = {call};'
        elif kind == 'take_late':
            free = (s16(w[0]) << 16) + s16(w[2]); head = (s16(w[14]) << 16) + s16(w[15])
            tail = ((w[28] & 0xFFFF) << 16) | (w[29] & 0xFFFF); count = (s16(w[31]) << 16) + s16(w[33])
            nxt, prv, used = w[6] & 0xFFFF, w[11] & 0xFFFF, w[13] & 0xFFFF
            call = (f'pool_take(base, ctx.r3.u32, {{{prv}, {nxt}, {used}}}, {{{tail:#x}, {head:#x}, {free:#x}, {count:#x}}}, '
                    'CountOrder::AfterHead)')
            hi = max(free, head, count, tail)
            spec = f'@r3.{free:#x}=ptr @r3.{head:#x}=ptr'
            body = f'ctx.r3.u64 = {call};'
        elif kind == 'release':
            used, prv, nxt, tail, head, free, count = (w[3] & 0xFFFF, w[6] & 0xFFFF, w[9] & 0xFFFF, w[14] & 0xFFFF,
                                                        w[22] & 0xFFFF, w[30] & 0xFFFF, w[40] & 0xFFFF)
            hi = max(tail, head, free, count)
            spec = (f'r4=ptr @r4.0=ptr @r4.0.{used}=range:0..0xFFFFFFFF @r4.0.{prv}=ptr @r4.0.{nxt}=ptr '
                    f'@r3.{free}=ptr')
            body = (f'pool_release(base, ctx.r3.u32, ctx.r4.u32, {{{prv}, {nxt}, {used}}}, '
                    f'{{{tail}, {head}, {free}, {count}}});')
        else:
            tail, lb, la, head, count = w[3] & 0xFFFF, w[4] & 0xFFFF, w[7] & 0xFFFF, w[9] & 0xFFFF, w[-2] & 0xFFFF
            hi = max(tail, head, count)
            spec = 'r4=ptr r5=range:1..6 ' + ' '.join('@r4.%d=ptr' % (4 * i) for i in range(6))
            body = (f'build_used(base, ctx.r3.u32, ctx.r4.u32, static_cast<int32_t>(ctx.r5.u32), {la}, {lb}, '
                    f'{{{tail}, {head}, 0, {count}}});')
        if hi >= 0x7C000: continue          # não cabe na área de teste
        base_spec = 'r3=const:0x40000000' if hi >= 0x70000 else 'r3=ptr'
        out.append((a, n, kind, body, f'{base_spec} {spec}'))
        break

lines = ['// GERADO por tools/decomp/gen_pool_instances.py — não editar à mão.',
         '// Instâncias do template de pool (src/decomp/game/pools.cpp), localizadas pela',
         '// estrutura das instruções e provadas uma a uma pelo tools/verify.',
         '#include "pools_template.h"', '']
for a, n, kind, body, spec in out:
    lines += [f'// sub_{a:08X} ({n} bytes) — {kind}',
              f'NIER_DECOMP(sub_{a:08X}) {{ {body} }}',
              f'NIER_VERIFY_SPEC(sub_{a:08X}, 0x{a:08X}, "{spec}");', '']
(root / 'src/decomp/game/pool_instances.cpp').write_text('\n'.join(lines))
print(len(out), 'instâncias;', sum(n for _, n, *_ in out), 'bytes')
