#!/usr/bin/env python3
"""Localiza o D3D do XDK (9328) dentro da PE decifrada do NieR.

Âncoras (docs/RENDER.md §4): chamadas aos thunks Vd* do kernel, cabeçalhos PM4 tipo 3 montados
em registrador, deslocamentos fixos na estrutura do device e o grafo de chamadas.

Uso:
  python3 tools/render/find_d3d.py --pe nier-data/pe/default.pe --info nier-data/pe/xex-info.json \
      [--json saida.json] [--callers 0x8256BB90] [--offset 0x35EC]
"""
import argparse
import bisect
import collections
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from capstone import ppc as cppc  # noqa: E402
from xex_image import Image  # noqa: E402

PM4 = {0x22: 'DRAW_INDX', 0x36: 'DRAW_INDX_2', 0x27: 'IM_LOAD', 0x2B: 'IM_LOAD_IMMEDIATE',
       0x2F: 'LOAD_ALU_CONSTANT', 0x2D: 'SET_CONSTANT', 0x46: 'EVENT_WRITE', 0x58: 'EVENT_WRITE_SHD',
       0x59: 'EVENT_WRITE_EXT', 0x3C: 'WAIT_REG_MEM', 0x54: 'INTERRUPT', 0x3A: 'REG_RMW',
       0x10: 'NOP', 0x3F: 'INDIRECT_BUFFER', 0x48: 'ME_INIT', 0x25: 'VIZ_QUERY',
       0x4A: 'SET_SHADER_CONSTANTS', 0x2E: 'LOAD_CONSTANT_CONTEXT', 0x5E: 'COND_WRITE',
       0x3E: 'MEM_WRITE', 0x41: 'INVALIDATE_STATE', 0x5B: 'SET_BIN_SELECT', 0x50: 'SET_BIN_MASK_LO',
       0x51: 'SET_BIN_MASK_HI', 0x4C: 'SET_BIN_MASK', 0x4D: 'SET_BIN_SELECT_LO', 0x4E: 'SET_BIN_SELECT_HI',
       0x37: 'IM_LOAD_ALT', 0x2C: 'IM_LOAD_IMM2'}


class Scan:
    def __init__(self, img):
        self.img = img
        self.ins = []          # (addr, mnemonic, ops) de todo o código executável
        targets = set(img.functions)
        for s in img.text:
            for i in img.md.disasm(img.bytes(s['address'], s['virtual_size']), s['address']):
                ops = []
                if i.id:
                    for o in i.operands:
                        if o.type == cppc.PPC_OP_IMM:
                            ops.append(('i', o.imm))
                        elif o.type == cppc.PPC_OP_REG:
                            ops.append(('r', o.reg))
                        elif o.type == cppc.PPC_OP_MEM:
                            ops.append(('m', o.mem.base, o.mem.disp))
                m = i.mnemonic if i.id else '.data'
                self.ins.append((i.address, m, ops))
                if m == 'bl' and ops and ops[0][0] == 'i':
                    targets.add(ops[0][1] & 0xFFFFFFFF)
        self.starts = sorted(targets)
        self.func = {}
        self.calls = collections.defaultdict(set)
        self.callers = collections.defaultdict(set)
        self.pm4 = collections.defaultdict(list)
        self.stores = collections.defaultdict(collections.Counter)
        self.loads = collections.defaultdict(collections.Counter)
        self.size = collections.Counter()
        hi = {}
        cur = None
        for a, m, ops in self.ins:
            f = self.owner(a)
            if f != cur:
                cur, hi = f, {}
            self.size[f] += 4
            if m in ('bl', 'b') and ops and ops[0][0] == 'i':
                t = ops[0][1] & 0xFFFFFFFF
                if m == 'bl' or self.owner(t) != f:
                    self.calls[f].add(t)
                    self.callers[t].add(f)
            elif m == 'lis' and len(ops) == 2:
                hi[ops[0][1]] = (ops[1][1] & 0xFFFF) << 16
            elif m in ('ori', 'addi') and len(ops) == 3 and ops[1][0] == 'r' and ops[1][1] in hi:
                lo = ops[2][1] if m == 'addi' else ops[2][1] & 0xFFFF
                v = (hi[ops[1][1]] + lo) & 0xFFFFFFFF
                if (v >> 30) == 3 and (v & 0xC000) == 0 and ((v >> 8) & 0x7F) in PM4:
                    self.pm4[f].append(PM4[(v >> 8) & 0x7F])
            for o in ops:
                if o[0] == 'm':
                    (self.stores if m.startswith('st') else self.loads)[f][o[2] & 0xFFFF] += 1

    def owner(self, a):
        i = bisect.bisect_right(self.starts, a) - 1
        return self.starts[i] if i >= 0 else None


def load_scan(img, cache):
    import pickle
    if cache and os.path.exists(cache):
        with open(cache, 'rb') as f:
            sha, data = pickle.load(f)
        if sha == img.info['pe_sha256']:
            s = Scan.__new__(Scan)
            s.__dict__.update(data)
            s.img = img
            s.calls = collections.defaultdict(set, s.calls)
            s.callers = collections.defaultdict(set, s.callers)
            s.pm4 = collections.defaultdict(list, s.pm4)
            s.stores = collections.defaultdict(collections.Counter, s.stores)
            s.loads = collections.defaultdict(collections.Counter, s.loads)
            return s
    s = Scan(img)
    if cache:
        data = {k: v for k, v in s.__dict__.items() if k != 'img'}
        for k in ('calls', 'callers', 'pm4', 'stores', 'loads'):
            data[k] = dict(data[k])
        with open(cache, 'wb') as f:
            pickle.dump((img.info['pe_sha256'], data), f)
        s = load_scan(img, cache)
    return s


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--pe', required=True)
    ap.add_argument('--info', required=True)
    ap.add_argument('--json')
    ap.add_argument('--cache', help='pickle da varredura (privado), reaproveitado se o SHA da PE bater')
    ap.add_argument('--callers', action='append', default=[])
    ap.add_argument('--offset', action='append', default=[])
    a = ap.parse_args()
    img = Image(a.pe, a.info)
    s = load_scan(img, a.cache)
    print('funções (pdata ∪ alvos de bl):', len(s.starts), 'instruções:', len(s.ins))
    vd = {t: n.split(':')[1] for t, n in img.thunks.items() if ':Vd' in n}
    vd_callers = collections.defaultdict(set)
    for t, n in vd.items():
        for c in s.callers.get(t, ()):
            vd_callers[c].add(n)
    print('\nchamadores de Vd*:')
    for c in sorted(vd_callers):
        print('  0x%08X  %5d B  %s  pm4=%s' % (c, s.size[c], ','.join(sorted(vd_callers[c])),
                                              dict(collections.Counter(s.pm4[c]))))
    print('\nfunções com PM4 tipo 3:')
    for f in sorted(s.pm4):
        print('  0x%08X  %5d B  %s  chamadores=%d' % (f, s.size[f], dict(collections.Counter(s.pm4[f])),
                                                     len(s.callers.get(f, ()))))
    for x in a.callers:
        t = int(x, 16)
        print('\nchamadores de 0x%08X:' % t, ' '.join('0x%08X' % c for c in sorted(s.callers.get(t, ()))))
    for x in a.offset:
        o = int(x, 16)
        print('\nacessos ao deslocamento 0x%X:' % o)
        for f in s.starts:
            if s.stores[f][o] or s.loads[f][o]:
                print('  0x%08X  st=%d ld=%d  %d B' % (f, s.stores[f][o], s.loads[f][o], s.size[f]))
    if a.json:
        out = {'pe_sha256': img.info['pe_sha256'],
               'vd_callers': {'0x%08X' % k: sorted(v) for k, v in vd_callers.items()},
               'pm4': {'0x%08X' % k: v for k, v in s.pm4.items()}}
        json.dump(out, open(a.json, 'w'), indent=1)


if __name__ == '__main__':
    main()
