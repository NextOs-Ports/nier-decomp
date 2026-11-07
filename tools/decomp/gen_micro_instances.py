#!/usr/bin/env python3
"""Reconstrói as funções minúsculas (≤ 24 bytes, código reto, sem chamadas com retorno):
acessores, constantes, encaminhadores (`b alvo`, `bctr`). Cada instrução vira uma linha
nativa sobre os registradores do contexto; o comparador prova cada instância.
Uso: gen_micro_instances.py <default.pe privado>"""
import json, pathlib, re, struct, sys

root = pathlib.Path(__file__).resolve().parents[2]
pe = open(sys.argv[1], 'rb').read()
inv = json.load(open(root / 'progress/functions.json'))['functions']
gen = pathlib.Path('/mnt/ARQUIVOS/HOME-FELIPE/ports_compile/nier-data/generated/default')
B = 0x82000000
def words(a, n): return struct.unpack_from('>%dI' % (n // 4), pe, a - B)
def s16(w): w &= 0xFFFF; return w - 0x10000 if w & 0x8000 else w
def b_target(pc, w):
    li = w & 0x03FFFFFC
    if li & 0x02000000: li -= 0x04000000
    return (pc + li) & 0xFFFFFFFF
# Funções presentes no código gerado (as demais são entradas de tabela que o recompilador não emite).
generated = set()
for f in gen.glob('nier_recomp.*.cpp'):
    generated |= set(int(m, 16) for m in re.findall(r'DEFINE_REX_FUNC\(sub_([0-9A-F]{8})\)', f.read_text()))

targets = set()
def sym(addr):
    targets.add(addr); return f'sub_{addr:08X}'

def decode(a, w):
    """Devolve lista de linhas C++ ou None se alguma instrução não for suportada."""
    lines = []; max_off = 0
    n = len(w)
    for i, x in enumerate(w):
        op = x >> 26; rd = (x >> 21) & 31; ra = (x >> 16) & 31; rb = (x >> 11) & 31; imm = s16(x)
        last = i == n - 1
        R = lambda r: f'ctx.r{r}'
        F = lambda r: f'ctx.f{r}'
        ea = lambda: (f'{R(ra)}.u32 + {imm}' if ra else f'{imm & 0xFFFFFFFF}u')
        if op == 14: lines.append(f'{R(rd)}.s64 = {R(ra)}.s64 + {imm};' if ra else f'{R(rd)}.s64 = {imm};')
        elif op == 15: lines.append(f'{R(rd)}.s64 = {R(ra)}.s64 + {imm * 65536};' if ra else f'{R(rd)}.s64 = {imm * 65536};')
        elif op == 24: lines.append(f'{R(ra)}.u64 = {R(rd)}.u64 | {x & 0xFFFF}u;')
        elif op == 28: lines.append(f'{R(ra)}.u64 = {R(rd)}.u64 & {x & 0xFFFF}u; ctx.cr0.compare<int32_t>({R(ra)}.s32, 0, ctx.xer);')
        elif op == 32: lines.append(f'{R(rd)}.u64 = ld32(base, {ea()});'); max_off = max(max_off, abs(imm))
        elif op == 34: lines.append(f'{R(rd)}.u64 = ld8(base, {ea()});'); max_off = max(max_off, abs(imm))
        elif op == 40: lines.append(f'{R(rd)}.u64 = ld16(base, {ea()});'); max_off = max(max_off, abs(imm))
        elif op == 42: lines.append(f'{R(rd)}.s64 = static_cast<int16_t>(ld16(base, {ea()}));'); max_off = max(max_off, abs(imm))
        elif op == 36: lines.append(f'st32(base, {ea()}, {R(rd)}.u32);'); max_off = max(max_off, abs(imm))
        elif op == 38: lines.append(f'st8(base, {ea()}, {R(rd)}.u8);'); max_off = max(max_off, abs(imm))
        elif op == 44: lines.append(f'st16(base, {ea()}, {R(rd)}.u16);'); max_off = max(max_off, abs(imm))
        elif op == 48: lines.append(f'{F(rd)}.f64 = ldf(base, {ea()});'); max_off = max(max_off, abs(imm))
        elif op == 52: lines.append(f'stf(base, {ea()}, static_cast<float>({F(rd)}.f64));'); max_off = max(max_off, abs(imm))
        elif op == 50: lines.append(f'{F(rd)}.u64 = ld64(base, {ea()});'); max_off = max(max_off, abs(imm))
        elif op == 54: lines.append(f'st64(base, {ea()}, {F(rd)}.u64);'); max_off = max(max_off, abs(imm))
        elif op == 12: lines.append(f'ctx.xer.ca = {R(ra)}.u32 + {imm & 0xFFFFFFFF}u < {R(ra)}.u32; {R(rd)}.s64 = {R(ra)}.s64 + {imm};')
        elif op == 21:
            sh, mb, me = (x >> 11) & 31, (x >> 6) & 31, (x >> 1) & 31
            if x & 1: return None
            lines.append(f'{R(ra)}.u64 = rlwinm({R(rd)}.u32, {sh}, {mb}, {me});')
        elif op == 18:
            if x & 1 or not last or b_target(a + 4 * i, x) not in generated: return None
            lines.append(f'{sym(b_target(a + 4 * i, x))}(ctx, base);')
        elif op == 19 and ((x >> 1) & 0x3FF) == 16 and (x >> 21) & 31 == 20:
            if not last: return None
            lines.append('return;')
        elif op == 19 and ((x >> 1) & 0x3FF) == 528 and (x >> 21) & 31 == 20:
            if not last: return None
            lines.append('call_indirect(ctx, base, ctx.ctr.u32);')
        elif op == 31:
            xo = (x >> 1) & 0x3FF
            if x & 1: return None
            if xo == 467 and ((x >> 11) & 0x3FF) == 0x120: lines.append(f'ctx.ctr.u64 = {R(rd)}.u64;')
            elif xo == 444 and rd == rb: lines.append(f'{R(ra)}.u64 = {R(rd)}.u64;')
            elif xo == 444: lines.append(f'{R(ra)}.u64 = {R(rd)}.u64 | {R(rb)}.u64;')
            elif xo == 28: lines.append(f'{R(ra)}.u64 = {R(rd)}.u64 & {R(rb)}.u64;')
            elif xo == 316: lines.append(f'{R(ra)}.u64 = {R(rd)}.u64 ^ {R(rb)}.u64;')
            elif xo == 266: lines.append(f'{R(rd)}.u64 = {R(ra)}.u64 + {R(rb)}.u64;')
            elif xo == 40: lines.append(f'{R(rd)}.u64 = {R(rb)}.u64 - {R(ra)}.u64;')
            elif xo == 136: lines.append(f'{R(rd)}.u64 = ~{R(ra)}.u64 + {R(rb)}.u64 + ctx.xer.ca;')
            elif xo == 26: lines.append(f'{R(ra)}.u64 = {R(rd)}.u32 ? __builtin_clz({R(rd)}.u32) : 32;')
            elif xo == 954: lines.append(f'{R(ra)}.s64 = {R(rd)}.s8;')
            elif xo == 922: lines.append(f'{R(ra)}.s64 = {R(rd)}.s16;')
            elif xo == 986: lines.append(f'{R(ra)}.s64 = {R(rd)}.s32;')
            elif xo == 23: lines.append(f'{R(rd)}.u64 = ld32(base, {R(ra)}.u32 + {R(rb)}.u32);')
            elif xo == 87: lines.append(f'{R(rd)}.u64 = ld8(base, {R(ra)}.u32 + {R(rb)}.u32);')
            elif xo == 279: lines.append(f'{R(rd)}.u64 = ld16(base, {R(ra)}.u32 + {R(rb)}.u32);')
            elif xo == 151: lines.append(f'st32(base, {R(ra)}.u32 + {R(rb)}.u32, {R(rd)}.u32);')
            elif xo == 215: lines.append(f'st8(base, {R(ra)}.u32 + {R(rb)}.u32, {R(rd)}.u8);')
            elif xo == 407: lines.append(f'st16(base, {R(ra)}.u32 + {R(rb)}.u32, {R(rd)}.u16);')
            elif xo == 535: lines.append(f'{F(rd)}.f64 = ldf(base, {R(ra)}.u32 + {R(rb)}.u32);')
            elif xo == 663: lines.append(f'stf(base, {R(ra)}.u32 + {R(rb)}.u32, static_cast<float>({F(rd)}.f64));')
            elif xo == 24: lines.append(f'{R(ra)}.u64 = slw({R(rd)}.u32, {R(rb)}.u64);')
            elif xo == 536: lines.append(f'{R(ra)}.u64 = srw({R(rd)}.u32, {R(rb)}.u64);')
            elif xo == 792: lines.append(f'{R(ra)}.s64 = static_cast<int32_t>(sraw({R(rd)}.u32, {R(rb)}.u64));')
            else: return None
        elif op == 63 and ((x >> 1) & 0x3FF) == 72 and not (x & 1): lines.append(f'{F(rd)}.u64 = {F(rb)}.u64;')   # fmr
        elif op == 63 and ((x >> 1) & 0x3FF) == 40 and not (x & 1): lines.append(f'{F(rd)}.f64 = -{F(rb)}.f64;')   # fneg
        else: return None
    if lines and lines[-1] == 'return;': lines.pop()
    return lines, max_off

done = set()
for f in (root / 'src/decomp').rglob('*.cpp'):
    if f.name == 'micro_instances.cpp': continue
    done |= set(int(m, 16) for m in re.findall(r'NIER_DECOMP\(sub_([0-9A-F]{8})\)', f.read_text()))
out = []; total = 0
for fn in inv:
    a, n = fn['address'], fn['size']
    if a in done or n > 24 or a not in generated: continue
    w = words(a, n)
    r = decode(a, w)
    if r is None: continue
    lines, max_off = r
    if max_off > 0x8000: continue
    body = ' '.join(lines) if lines else ''
    uses_ctr = any('ctr' in l for l in lines)
    spec = 'r3=ptr' if max_off <= 0x4000 else 'r3=const:0x40010000'
    if any('ld64' in l or 'st64' in l for l in lines): spec += ':8'
    if uses_ctr: spec = 'r3=obj'
    out.append(f'// sub_{a:08X} ({n} bytes)\nNIER_DECOMP(sub_{a:08X}) {{ {body} }}\nNIER_VERIFY_SPEC(sub_{a:08X}, 0x{a:08X}, "{spec}");\n')
    total += n
hdr = ('// GERADO por tools/decomp/gen_micro_instances.py — não editar à mão.\n'
       '// Funções minúsculas (acessores, constantes, encaminhadores), provadas uma a uma pelo tools/verify.\n'
       '#include <nier/decomp.h>\nusing namespace nier::guest;\n'
       + ''.join(f'DECLARE_REX_FUNC(sub_{t:08X});\n' for t in sorted(targets)) + '\n')
(root / 'src/decomp/game/micro_instances.cpp').write_text(hdr + '\n'.join(out))
print(len(out), 'instâncias;', total, 'bytes')
