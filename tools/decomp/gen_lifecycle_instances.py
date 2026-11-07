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
       'dtor_allocator_delete': (0x8214E970, 108)}
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
        frame = -s16(w[4] if kind == 'dtor_allocator_delete' else w[3])
        if kind == 'dtor_vtable_delete':
            vt = ((s16(w[4]) << 16) + s16(w[6])) & 0xFFFFFFFF
            args = f'{frame}, 0x{vt:08X}, {s16(w[11])}, 0x{bl_target(a + 48, w[12]):08X}'
            spec = 'r3=ptr r4=range:0..1'
        elif kind == 'ctor_base_field_vtable':
            vt = ((s16(w[7]) << 16) + s16(w[9])) & 0xFFFFFFFF
            args = f'{frame}, 0x{bl_target(a + 24, w[6]):08X}, {s16(w[4])}, {s16(w[10])}, 0x{vt:08X}'
            spec = 'r3=ptr'
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
