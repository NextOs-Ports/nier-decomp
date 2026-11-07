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

def sub_virtual_then_set(a, w):
    hi_lo = lambda i, j: ((s16(w[i]) << 16) + s16(w[j])) & 0xFFFFFFFF
    body = (f'sub_virtual_then_set(ctx, base, {-s16(w[3])}, {s16(w[7])}, 0x{hi_lo(5, 9):08X}, {s16(w[8])}, '
            f'{s16(w[10]) // 4}, {s16(w[14])}, {s16(w[13])})')
    return body, f'r3=ptr @r3.{s16(w[7])}=const:0x40001000'

def ctor_base_vt_byte_float(a, w):
    hi_lo = lambda i, j: ((s16(w[i]) << 16) + s16(w[j])) & 0xFFFFFFFF
    body = (f'ctor_base_vt_byte_float(ctx, base, {-s16(w[3])}, {sym(bl_target(a + 24, w[6]))}, {s16(w[4])}, '
            f'{s16(w[11])}, {s16(w[9])}, 0x{hi_lo(8, 10):08X}, {s16(w[14])}, 0x{hi_lo(7, 13):08X}, {s16(w[15])})')
    return body, 'r3=ptr'

def singleton_register(a, w):
    hi_lo = lambda i, j: ((s16(w[i]) << 16) + s16(w[j])) & 0xFFFFFFFF
    if s16(w[6]) != 0: return None
    body = (f'singleton_register(ctx, base, {-s16(w[2])}, 0x{hi_lo(3, 5):08X}, 0x{hi_lo(4, 7):08X}, '
            f'{sym(bl_target(a + 48, w[12]))}, 0x{hi_lo(10, 11):08X}, 0x{hi_lo(14, 17):08X}, {s16(w[15])})')
    return body, f'r3=ptr @r3.0=ptr'

def angle_wrap(a, w):
    hi_lo = lambda i, j: ((s16(w[i]) << 16) + s16(w[j])) & 0xFFFFFFFF
    p = (f'{{0x{hi_lo(0, 4):08X}, 0x{hi_lo(2, 5):08X}, 0x{hi_lo(12, 13):08X}, 0x{hi_lo(21, 23):08X}, 0x{hi_lo(22, 25):08X}, '
         f'0x{hi_lo(28, 31):08X}, {s16(w[3])}}}')
    if hi_lo(21, 23) != hi_lo(29, 32) or hi_lo(22, 25) != hi_lo(30, 34): return None
    return f'static constexpr AngleWrap p = {p}; angle_wrap(ctx, base, p)', 'f1=f32:0..800'

def ctor_vt2_link(a, w):
    hi_lo = lambda i, j: ((s16(w[i]) << 16) + s16(w[j])) & 0xFFFFFFFF
    if bl_target(a + 56, w[14]) != bl_target(a + 88, w[22]): return None
    body = (f'ctor_vt2_link(ctx, base, {-s16(w[4])}, 0x{hi_lo(6, 8):08X}, {s16(w[10])}, 0x{hi_lo(7, 9):08X}, {s16(w[12])}, '
            f'{s16(w[11])}, {sym(bl_target(a + 56, w[14]))}, {s16(w[15])}, {s16(w[19])}, {s16(w[25])}, {s16(w[27]) // 4})')
    return body, 'r3=ptr r4=range:0..1 callargs=2'

def five_flag_calls(a, w):
    p = (f'{{{sym(bl_target(a + 40, w[10]))}, {sym(bl_target(a + 64, w[16]))}, {sym(bl_target(a + 100, w[25]))}, '
         f'{sym(bl_target(a + 124, w[31]))}, {sym(bl_target(a + 148, w[37]))}, {s16(w[8])}, {s16(w[11])}, {s16(w[17])}, '
         f'{s16(w[19])}, {s16(w[26])}, {s16(w[32])}}}')
    return f'static constexpr FiveFlags p = {p}; five_flag_calls(ctx, base, {-s16(w[4])}, p)', 'r3=ptr r4=range:0..6'

def effect_init(a, w):
    hi_lo = lambda i, j: ((s16(w[i]) << 16) + s16(w[j])) & 0xFFFFFFFF
    p = (f'{{{sym(bl_target(a + 84, w[21]))}, {sym(bl_target(a + 116, w[29]))}, 0x{hi_lo(9, 11):08X}, 0x{hi_lo(6, 12):08X}, '
         f'0x{hi_lo(7, 13):08X}, 0x{hi_lo(8, 16):08X}, {s16(w[19])}, {s16(w[17])}, {s16(w[15])}, {s16(w[14])}, {s16(w[22])}, '
         f'{s16(w[23])}, {s16(w[25])}, {s16(w[26])}, {s16(w[28])}, {s16(w[34])}, {s16(w[33])}, {s16(w[37]) // 4}, {s16(w[40])}, {s16(w[41])}}}')
    return f'static constexpr EffectInit p = {p}; effect_init(ctx, base, {-s16(w[5])}, p)', f'r3=ptr @r3.{s16(w[22])}=ptr callret=ptr'

def clone_if_type(a, w):
    ref = words(0x822D8268, 184)
    for i in (20, 22, 23, 24, 25, 26, 27, 28, 29, 30, 33, 34, 35, 36):
        if (w[i] & 0xFFFF) != (ref[i] & 0xFFFF): return None
    if bl_target(a + 36, w[9]) != bl_target(a + 44, w[11]): return None
    p = (f'{{{sym(bl_target(a + 36, w[9]))}, {sym(bl_target(a + 56, w[14]))}, {s16(w[5])}, {s16(w[7])}, {s16(w[12])}, '
         f'{s16(w[13])}, {s16(w[16])}, {s16(w[17])}, {s16(w[21])}, {s16(w[39])}, {s16(w[37])}, {s16(w[38])}}}')
    return f'static constexpr CloneIf p = {p}; clone_if_type(ctx, base, {-s16(w[4])}, p)', f'r3=ptr:16 r5=ptr @r5.{s16(w[5])}=range:{s16(w[7]) - 1}..{s16(w[7]) + 1} @r3.{s16(w[12])}=ptr callret=ptr'

def bounded_string_copy(a, w):
    body = (f'bounded_string_copy(ctx, base, {-s16(w[3])}, {s16(w[8])}, {s16(w[23])}, {sym(bl_target(a + 148, w[37]))}, {s16(w[39])})')
    return body, 'r3=ptr r4=bptr'

def make_node2(a, w):
    hi_lo = lambda i, j: ((s16(w[i]) << 16) + s16(w[j])) & 0xFFFFFFFF
    body = (f'make_node(ctx, base, {-s16(w[4])}, {s16(w[6])}, {s16(w[8])}, {s16(w[7])}, {s16(w[10]) // 4}, '
            f'0x{hi_lo(16, 21):08X}, 0x{hi_lo(17, 26):08X}, {sym(bl_target(a + 144, w[36]))})')
    return body, f'r3=ptr @r3.{s16(w[6])}=obj callret=ptr'

def vcall_tail(a, w):
    return f'vcall_tail(ctx, base, {s16(w[1]) // 4})', 'r3=obj'

def bitset_probe(a, w):
    p = f'{{{sym(bl_target(a + 68, w[17]))}, {s16(w[5])}, {s16(w[12])}, {s16(w[20])}, {rlw(w[3])}, {rlw(w[4])}, {rlw(w[6])}}}'
    return f'static constexpr BitsetProbe p = {p}; bitset_probe(ctx, base, {-s16(w[2])}, p)', 'r3=ptr:8 r4=range:0..0x3FFF'

def ctor_vt_init_opt(a, w):
    hi_lo = lambda i, j: ((s16(w[i]) << 16) + s16(w[j])) & 0xFFFFFFFF
    body = (f'ctor_vt_init_opt(ctx, base, {-s16(w[4])}, 0x{hi_lo(5, 7):08X}, {s16(w[9])}, {sym(bl_target(a + 40, w[10]))}, '
            f'{s16(w[15])}, {sym(bl_target(a + 64, w[16]))})')
    return body, 'r3=ptr r4=range:0..1'

def make_pair_result2(a, w):
    body = (f'make_pair_result2(ctx, base, {-s16(w[4])}, {sym(bl_target(a + 36, w[9]))}, {s16(w[15])}, {s16(w[14])}, '
            f'{sym(bl_target(a + 64, w[16]))}, {s16(w[18])}, {sym(bl_target(a + 76, w[19]))}, {s16(w[21])})')
    return body, 'r3=ptr r4=int r5=ptr'

def chain_flag_global(a, w):
    hi_lo = lambda i, j: ((s16(w[i]) << 16) + s16(w[j])) & 0xFFFFFFFF
    body = (f'chain_flag_global(ctx, base, {-s16(w[2])}, {s16(w[4])}, {s16(w[3])}, {sym(bl_target(a + 20, w[5]))}, '
            f'{sym(bl_target(a + 24, w[6]))}, 0x{(hi_lo(7, 9) + s16(w[11])) & 0xFFFFFFFF:08X})')
    return body, 'r3=ptr'

def ctor_base_vt_byte_word(a, w):
    hi_lo = lambda i, j: ((s16(w[i]) << 16) + s16(w[j])) & 0xFFFFFFFF
    body = (f'ctor_base_vt_byte_word(ctx, base, {-s16(w[3])}, {sym(bl_target(a + 24, w[6]))}, {s16(w[4])}, {s16(w[10])}, '
            f'{s16(w[8])}, 0x{hi_lo(7, 9):08X}, {s16(w[12])}, {s16(w[13])})')
    return body, 'r3=ptr'

def list_unlink(a, w):
    if s16(w[5]) != s16(w[13]) or s16(w[7]) != s16(w[15]): return None
    return (f'list_unlink(ctx, base, {s16(w[5])}, {s16(w[7])}, {s16(w[10])}, {s16(w[18])}, {s16(w[26])})',
            f'r3=ptr r4=ptr @r4.{s16(w[5])}=ptr @r4.{s16(w[7])}=ptr')

def seh_thunk(a, w):
    return f'seh_thunk(ctx, base, {-s16(w[5])}, {s16(w[1])}, {sym(bl_target(a + 28, w[7]))})', 'r3=ptr'

def list_push_back(a, w):
    p = (f'{{{sym(bl_target(a + 28, w[7]))}, {s16(w[11])}, {s16(w[12])}, {s16(w[13])}, {s16(w[14])}, {s16(w[18])}, '
         f'{s16(w[20])}, {s16(w[21])}}}')
    return f'static constexpr ListPush p = {p}; list_push_back(ctx, base, {-s16(w[4])}, p)', 'r3=ptr r4=int'

KINDS = {'alloc_then_init': (0x821F4C50, 116, alloc_then_init), 'ctor_vec_rand': (0x8226BCB8, 268, ctor_vec_rand),
         'fir16': (0x825047C0, 276, fir16), 'dtor_body_free2': (0x82102F88, 80, dtor_body_free2),
         'ctor_base_vt_fields': (0x82289650, 84, ctor_base_vt_fields), 'chain2x_global': (0x82386F00, 104, chain2x_global),
         'bitset_find_resolve': (0x82175C10, 136, bitset_find_resolve), 'collect_sort_apply': (0x8214C190, 128, collect_sort_apply),
         'make_pair_result': (0x82550860, 96, make_pair_result),
         'sub_virtual_then_set': (0x823F6318, 80, sub_virtual_then_set), 'ctor_base_vt_byte_float': (0x8228FB70, 84, ctor_base_vt_byte_float),
         'singleton_register': (0x82594690, 88, singleton_register), 'angle_wrap': (0x821B2C40, 148, angle_wrap),
         'ctor_vt2_link': (0x8227A730, 148, ctor_vt2_link), 'five_flag_calls': (0x82207AB0, 176, five_flag_calls),
         'effect_init': (0x82351228, 196, effect_init),
         # clone_if_type (0x822D8268, 184): a chamada base traduzida dispara SIGFPE com o comparador desarmado; fora até entender.
         'bounded_string_copy': (0x82100540, 180, bounded_string_copy), 'make_node2': (0x825A7690, 176, make_node2),
         'vcall_tail': (0x8213FFB8, 16, vcall_tail), 'bitset_probe': (0x82174710, 100, bitset_probe),
         'ctor_vt_init_opt': (0x824E02C8, 96, ctor_vt_init_opt), 'make_pair_result2': (0x82551108, 112, make_pair_result2),
         'chain_flag_global': (0x82383F08, 64, chain_flag_global), 'ctor_base_vt_byte_word': (0x8226B1B8, 76, ctor_base_vt_byte_word),
         'list_unlink': (0x821AE3F0, 120, list_unlink), 'seh_thunk': (0x825D19E0, 56, seh_thunk),
         'list_push_back': (0x82498B80, 124, list_push_back)}
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
