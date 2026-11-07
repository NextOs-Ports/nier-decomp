#!/usr/bin/env python3
"""Localiza instâncias dos templates de src/decomp/game/pattern_template.h pela estrutura
das instruções (opcode e registradores, sem imediatos) e gera
src/decomp/game/pattern_instances.cpp. Cada instância é provada pelo tools/verify.
Uso: gen_pattern_instances.py <default.pe privado>"""
import json, pathlib, re, struct, sys

root = pathlib.Path(__file__).resolve().parents[2]
pe = open(sys.argv[1], 'rb').read()
inv = json.load(open(root / 'progress/functions.json'))['functions']
B = 0x82000000
def words(a, n): return struct.unpack_from('>%dI' % (n // 4), pe, a - B)
def s16(w): w &= 0xFFFF; return w - 0x10000 if w & 0x8000 else w
def bl_target(pc, w):
    li = w & 0x03FFFFFC
    if li & 0x02000000: li -= 0x04000000
    return (pc + li) & 0xFFFFFFFF
def shape(w): return tuple((x & 0xFC000003) if (x >> 26) == 18 and x & 1 else (x if (x >> 26) in (16, 18) else x & 0xFFFF0000) for x in w)
def rlw(x): return f'{(x >> 11) & 31}, {(x >> 6) & 31}, {(x >> 1) & 31}'
targets = set()
def sym(addr):
    targets.add(addr); return f'sub_{addr:08X}'

# kind: (endereço de referência, tamanho, extrator(a, w) -> (corpo, spec) ou None)
def alloc_then_init(a, w):
    body = (f'alloc_then_init(ctx, base, {-s16(w[4])}, {s16(w[6])}, {sym(bl_target(a + 32, w[8]))}, {s16(w[10])}, '
            f'{s16(w[12])}, {s16(w[14]) // 4}, {sym(bl_target(a + 80, w[20]))})')
    return body, 'r3=obj callret=ptr'

def ctor_vec_rand(a, w):
    ref = words(0x8226BCB8, 268)
    if w[12] != ref[12] or w[20] != ref[20] or w[28] != ref[28] or s16(w[14]) != 0 or s16(w[25]) != 1: return None
    hi_lo = lambda i, j: ((s16(w[i]) << 16) + s16(w[j])) & 0xFFFFFFFF
    mult = ((s16(w[30]) << 16) | (w[32] & 0xFFFF)) & 0xFFFFFFFF
    add = ((s16(w[49]) << 16) + s16(w[50])) & 0xFFFFFFFF
    p = (f'{{{sym(bl_target(a + 24, w[6]))}, 0x{hi_lo(8, 15):08X}, 0x{hi_lo(9, 17):08X}, 0x{hi_lo(29, 38):08X}, '
         f'0x{hi_lo(31, 44):08X}, 0x{hi_lo(13, 18):08X}, 0x{hi_lo(16, 27):08X}, 0x{hi_lo(23, 47):08X}, {mult}u, {add}u, '
         f'{s16(w[10]) + s16(w[11])}, {{{s16(w[19])}, {s16(w[21])}, {s16(w[22])}, {s16(w[24])}, {s16(w[26])}, {s16(w[36])}, {s16(w[40])}}}, '
         f'{s16(w[34])}, {s16(w[35])}, {s16(w[37])}, {s16(w[41])}, {{{s16(w[39])}, {s16(w[45])}, {s16(w[46])}}}, {s16(w[43])}, '
         f'{s16(w[42])}, {s16(w[60])}}}')
    return f'static constexpr CtorVecRand p = {p}; ctor_vec_rand(ctx, base, {-s16(w[4])}, p)', 'r3=ptr:16'

def fir16(a, w):
    if s16(w[11]) != 0 or s16(w[17]) != 0: return None
    hi_lo = lambda i, j: ((s16(w[i]) << 16) + s16(w[j])) & 0xFFFFFFFF
    coef = [19, 20, 21, 22, 24, 26, 28, 31, 33, 35, 37, 39, 42, 44, 46]
    ins = [12, 14, 15, 16, 23, 25, 27, 30, 32, 34, 36, 38, 41, 43, 45]
    p = (f'{{{s16(w[4])}, 0x{hi_lo(8, 9):08X}, {s16(w[6])}, {s16(w[62])}, '
         f'{{{", ".join(str(s16(w[i])) for i in coef)}}}, {{{", ".join(str(s16(w[i])) for i in ins)}}}, {s16(w[47])}}}')
    return f'static constexpr Fir16 p = {p}; fir16(ctx, base, p)', 'r3=ptr r4=ptr r5=ptr'

def dtor_body_free2(a, w):
    return (f'dtor_body_free(ctx, base, {-s16(w[4])}, {sym(bl_target(a + 28, w[7]))}, {sym(bl_target(a + 48, w[12]))})',
            'r3=ptr r4=range:0..1')

def ctor_base_vt_fields(a, w):
    hi_lo = lambda i, j: ((s16(w[i]) << 16) + s16(w[j])) & 0xFFFFFFFF
    body = (f'ctor_base_vt_fields(ctx, base, {-s16(w[3])}, {sym(bl_target(a + 24, w[6]))}, {s16(w[4])}, '
            f'0x{hi_lo(7, 9):08X}, {s16(w[12])}, 0x{hi_lo(8, 10):08X}, {s16(w[13])}, {s16(w[11])}, {s16(w[15])})')
    return body, 'r3=ptr'

def chain2x_global(a, w):
    hi_lo = lambda i, j: ((s16(w[i]) << 16) + s16(w[j])) & 0xFFFFFFFF
    body = (f'chain2x_global(ctx, base, {-s16(w[4])}, {s16(w[5])}, {s16(w[6])}, {sym(bl_target(a + 32, w[8]))}, '
            f'{sym(bl_target(a + 36, w[9]))}, {s16(w[11])}, {sym(bl_target(a + 52, w[13]))}, {sym(bl_target(a + 56, w[14]))}, '
            f'0x{hi_lo(15, 17):08X}, {sym(bl_target(a + 76, w[19]))})')
    return body, 'r3=ptr'

def bitset_find_resolve(a, w):
    if s16(w[16]) != -1: return None
    p = (f'{{{sym(bl_target(a + 76, w[19]))}, {sym(bl_target(a + 108, w[27]))}, {s16(w[7])}, {s16(w[23])}, {s16(w[24])}, '
         f'{rlw(w[5])}, {rlw(w[6])}, {rlw(w[9])}, {rlw(w[10])}, {rlw(w[22])}, {rlw(w[26])}}}')
    return f'static constexpr BitsetFind p = {p}; bitset_find_resolve(ctx, base, {-s16(w[4])}, p)', 'r3=ptr:8 r4=range:0..0x3FFF'

def collect_sort_apply(a, w):
    body = (f'collect_sort_apply(ctx, base, {-s16(w[4])}, {s16(w[5])}, {s16(w[12])}, {s16(w[10])}, {s16(w[14])}, '
            f'{s16(w[20])}, {s16(w[17])}, {sym(bl_target(a + 84, w[21]))}, {sym(bl_target(a + 100, w[25]))})')
    h, n = s16(w[5]), s16(w[12])
    return body, f'r3=ptr @r3.{h}=ptr @r3.{h}.{n}=ptr @r3.{h}.{n}.{n}=const:0'

def make_pair_result(a, w):
    body = (f'make_pair_result(ctx, base, {-s16(w[4])}, {s16(w[9])}, {s16(w[8])}, {sym(bl_target(a + 44, w[11]))}, '
            f'{s16(w[12])}, {s16(w[14])}, {sym(bl_target(a + 60, w[15]))}, {s16(w[17])})')
    return body, 'r3=ptr r4=ptr'

KINDS = {'alloc_then_init': (0x821F4C50, 116, alloc_then_init), 'ctor_vec_rand': (0x8226BCB8, 268, ctor_vec_rand),
         'fir16': (0x825047C0, 276, fir16), 'dtor_body_free2': (0x82102F88, 80, dtor_body_free2),
         'ctor_base_vt_fields': (0x82289650, 84, ctor_base_vt_fields), 'chain2x_global': (0x82386F00, 104, chain2x_global),
         'bitset_find_resolve': (0x82175C10, 136, bitset_find_resolve), 'collect_sort_apply': (0x8214C190, 128, collect_sort_apply),
         'make_pair_result': (0x82550860, 96, make_pair_result)}
refs = {k: shape(words(a, n)) for k, (a, n, _) in KINDS.items()}
done = set()
for f in (root / 'src/decomp').rglob('*.cpp'):
    if f.name == 'pattern_instances.cpp': continue
    done |= set(int(m, 16) for m in re.findall(r'NIER_DECOMP\(sub_([0-9A-F]{8})\)', f.read_text()))
out = []; total = 0
for fn in inv:
    a, n = fn['address'], fn['size']
    if a in done: continue
    for kind, (ra, rn, extract) in KINDS.items():
        if n != rn: continue
        w = words(a, n)
        if shape(w) != refs[kind]: continue
        r = extract(a, w)
        if r is None: break
        body, spec = r
        out.append(f'// sub_{a:08X} ({n} bytes) — {kind}\nNIER_DECOMP(sub_{a:08X}) {{ {body}; }}\n'
                   f'NIER_VERIFY_SPEC(sub_{a:08X}, 0x{a:08X}, "{spec}");\n')
        total += n
        break
hdr = ('// GERADO por tools/decomp/gen_pattern_instances.py — não editar à mão.\n'
       '// Instâncias dos padrões de pattern_template.h, provadas uma a uma pelo tools/verify.\n'
       '#include "pattern_template.h"\n#include "lifecycle_template.h"\n'
       '// Alvos chamados diretamente pelas instâncias.\n'
       + ''.join(f'DECLARE_REX_FUNC(sub_{t:08X});\n' for t in sorted(targets)) + '\n')
(root / 'src/decomp/game/pattern_instances.cpp').write_text(hdr + '\n'.join(out))
print(len(out), 'instâncias;', total, 'bytes')
