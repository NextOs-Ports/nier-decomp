#!/usr/bin/env python3
"""Localiza instâncias dos templates de construtor/destrutor (src/decomp/game/lifecycle_template.h)
pela estrutura das instruções e gera src/decomp/game/lifecycle_instances.cpp.
Cada instância é provada pelo tools/verify. Uso: gen_lifecycle_instances.py <default.pe privado>"""
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

REF = {'dtor_vtable_delete': (0x824DEAB8, 76), 'ctor_base_field_vtable': (0x8226C400, 72),
       'dtor_allocator_delete': (0x8214E970, 108), 'forward_swapped_virtual': (0x821C7330, 28),
       'dtor_vtable_free': (0x82106118, 72), 'dtor_body_free': (0x82497AA8, 76),
       'forward_member_then_self': (0x8226AC18, 76), 'ctor_base_memset': (0x8226B490, 96),
       'member_pair_virtual_then': (0x8226BA98, 92), 'find_then_resolve': (0x82175360, 116),
       'dtor_two_members': (0x8226B9E8, 148), 'make_node': (0x821056B0, 176)}
# chain3_global e static_init (templates no cabeçalho) chamam inicialização real do CRT/runtime,
# que não roda com estado aleatório no comparador; ficam fora até haver prova específica.
refs = {k: shape(words(a, n)) for k, (a, n) in REF.items()}
done = set()
for f in (root / 'src/decomp').rglob('*.cpp'):
    if f.name == 'lifecycle_instances.cpp': continue
    done |= set(int(m, 16) for m in re.findall(r'NIER_DECOMP\(sub_([0-9A-F]{8})\)', f.read_text()))
out = []
for fn in inv:
    a, n = fn['address'], fn['size']
    if a in done: continue
    for kind, (ra, rn) in REF.items():
        if n != rn: continue
        w = words(a, n)
        if shape(w) != refs[kind]: continue
        frame = -s16(w[4] if kind in ('dtor_allocator_delete', 'dtor_body_free', 'forward_member_then_self',
                                      'find_then_resolve', 'make_node') else
                     (w[2] if kind in ('chain3_global', 'static_init') else w[3]))
        hi_lo = lambda i, j: ((s16(w[i]) << 16) + s16(w[j])) & 0xFFFFFFFF
        if kind == 'dtor_vtable_delete':
            vt = ((s16(w[4]) << 16) + s16(w[6])) & 0xFFFFFFFF
            args = f'{frame}, 0x{vt:08X}, {s16(w[11])}, 0x{bl_target(a + 48, w[12]):08X}, {s16(w[9])}'
            spec = 'r3=ptr r4=range:0..1'
        elif kind == 'ctor_base_field_vtable':
            vt = ((s16(w[7]) << 16) + s16(w[9])) & 0xFFFFFFFF
            args = f'{frame}, 0x{bl_target(a + 24, w[6]):08X}, {s16(w[4])}, {s16(w[10])}, 0x{vt:08X}, {s16(w[12])}'
            spec = 'r3=ptr'
        elif kind == 'forward_swapped_virtual':
            args = f'{s16(w[4]) // 4}'; spec = 'r3=ptr r4=obj'
        elif kind == 'dtor_vtable_free':
            args = f'{frame}, 0x{hi_lo(4, 6):08X}, 0x{bl_target(a + 44, w[11]):08X}, {s16(w[9])}'; spec = 'r3=ptr r4=range:0..1'
        elif kind == 'dtor_body_free':
            args = f'{frame}, 0x{bl_target(a + 28, w[7]):08X}, 0x{bl_target(a + 44, w[11]):08X}'; spec = 'r3=ptr r4=range:0..1'
        elif kind == 'forward_member_then_self':
            args = f'{frame}, {s16(w[8])}, 0x{bl_target(a + 36, w[9]):08X}, 0x{bl_target(a + 48, w[12]):08X}'
            spec = 'r3=ptr f1=f32'
        elif kind == 'ctor_base_memset':
            fa = ((s16(w[11]) << 16) + s16(w[15])) & 0xFFFFFFFF
            args = (f'{frame}, 0x{bl_target(a + 24, w[6]):08X}, {s16(w[4])}, {s16(w[7])}, {s16(w[9])}, {s16(w[8])}, '
                    f'0x{bl_target(a + 40, w[10]):08X}, 0x{hi_lo(12, 14):08X}, 0x{fa:08X}, {s16(w[16])}, {s16(w[17])}, {s16(w[18])}')
            spec = 'r3=ptr'
        elif kind == 'member_pair_virtual_then':
            if s16(w[6]) != s16(w[9]): break
            args = (f'{frame}, {s16(w[6])}, 0x{bl_target(a + 28, w[7]):08X}, 0x{bl_target(a + 40, w[10]):08X}, '
                    f'{s16(w[13]) // 4}, 0x{bl_target(a + 68, w[17]):08X}')
            spec = 'r3=obj'
        elif kind == 'find_then_resolve':
            rl = lambda x: f'{(x >> 11) & 31}, {(x >> 6) & 31}, {(x >> 1) & 31}'
            args = (f'{frame}, {s16(w[9])}, {s16(w[8])}, 0x{bl_target(a + 40, w[10]):08X}, 0x{bl_target(a + 76, w[19]):08X}, '
                    f'{rl(w[16])}, {rl(w[22])}')
            spec = 'r3=ptr'
        elif kind == 'dtor_two_members':
            args = (f'{frame}, {s16(w[5])}, 0x{hi_lo(6, 8):08X}, {s16(w[10])}, 0x{hi_lo(7, 9):08X}, {s16(w[12])}, '
                    f'{s16(w[16]) // 4}, {s16(w[15])}, {s16(w[20])}, 0x{hi_lo(19, 21):08X}, {s16(w[23])}, '
                    f'{s16(w[27]) // 4}, {s16(w[26])}, 0x{bl_target(a + 124, w[31]):08X}')
            spec = 'r3=ptr @r3.0=ptr'
        elif kind == 'make_node':
            fa = ((s16(w[16]) << 16) + s16(w[25])) & 0xFFFFFFFF
            args = (f'{frame}, {s16(w[6])}, {s16(w[8])}, {s16(w[7])}, {s16(w[10]) // 4}, 0x{hi_lo(17, 22):08X}, '
                    f'0x{fa:08X}, 0x{bl_target(a + 140, w[35]):08X}')
            spec = 'r3=ptr'
        elif kind == 'chain3_global':
            args = (f'{frame}, {s16(w[4])}, {s16(w[3])}, 0x{bl_target(a + 20, w[5]):08X}, 0x{bl_target(a + 24, w[6]):08X}, '
                    f'0x{hi_lo(7, 9):08X}, 0x{bl_target(a + 40, w[10]):08X}')
            spec = 'r3=ptr'
        elif kind == 'static_init':
            args = (f'{frame}, 0x{hi_lo(3, 4):08X}, 0x{bl_target(a + 20, w[5]):08X}, 0x{hi_lo(6, 7):08X}, '
                    f'0x{bl_target(a + 32, w[8]):08X}')
            spec = ''
        else:
            args = (f'{frame}, 0x{bl_target(a + 28, w[7]):08X}, 0x{bl_target(a + 48, w[12]):08X}, {s16(w[17]) // 4}, '
                    f'{s16(w[11])}, {s16(w[15])}')
            spec = 'r3=ptr r4=range:0..1'
        out.append(f'// sub_{a:08X} ({n} bytes) — {kind}\n'
                   f'NIER_DECOMP(sub_{a:08X}) {{ {kind}(ctx, base, {args}); }}\n'
                   f'NIER_VERIFY_SPEC(sub_{a:08X}, 0x{a:08X}, "{spec}");\n')
        break
hdr = ('// GERADO por tools/decomp/gen_lifecycle_instances.py — não editar à mão.\n'
       '// Instâncias dos templates de construtor/destrutor, provadas uma a uma pelo tools/verify.\n'
       '#include "lifecycle_template.h"\n\n')
(root / 'src/decomp/game/lifecycle_instances.cpp').write_text(hdr + '\n'.join(out))
print(len(out), 'instâncias')
