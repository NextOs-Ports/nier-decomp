#!/usr/bin/env python3
"""Imagem PE decifrada do Xbox 360 (layout de memória: offset = VA - base) + análise básica PPC.

Funções vêm do .pdata; chamadas, constantes e deslocamentos vêm do disassembler capstone (PPC64 BE).
Instruções VMX128 que o capstone não conhece viram None e são ignoradas.
"""
import json
import os
import re
import struct

import capstone
from capstone import ppc as cppc

KERNEL_TABLE = os.environ.get(
    'XBOXKRNL_EXPORTS',
    '/mnt/ARQUIVOS/TRABALHO CLAUDE CODE/03-PORTS-E-RECEITAS/projetos/RAYMAN-ORIGINS-NEXTOS-GL-20261002/'
    'work-state/rexglue-source/src/kernel/xboxkrnl/export_table.inc')


def kernel_names(path=KERNEL_TABLE):
    names = {}
    try:
        for m in re.finditer(r'XE_EXPORT\(xboxkrnl,\s*0x([0-9A-Fa-f]+),\s*(\w+)', open(path).read()):
            names[int(m[1], 16)] = m[2]
    except OSError:
        pass
    return names


class Image:
    def __init__(self, pe, info):
        self.data = open(pe, 'rb').read()
        self.info = json.load(open(info))
        self.base = self.info['load_address']
        self.sections = self.info['sections']
        text = [s for s in self.sections if s['executable']]
        self.text = text
        self.functions = self._pdata()
        self.thunks = {}
        kn = kernel_names()
        for lib in self.info['imports']:
            for r in lib['records']:
                if r['type'] == 1:
                    name = kn.get(r['ordinal']) if lib['library'].startswith('xboxkrnl') else None
                    self.thunks[r['address']] = '%s:%s' % (lib['library'].split('.')[0], name or r['ordinal'])
        self.md = capstone.Cs(capstone.CS_ARCH_PPC, capstone.CS_MODE_64 | capstone.CS_MODE_BIG_ENDIAN)
        self.md.detail = True
        self.md.skipdata = True

    def u32(self, va):
        return struct.unpack('>I', self.data[va - self.base:va - self.base + 4])[0]

    def bytes(self, va, n):
        return self.data[va - self.base:va - self.base + n]

    def _pdata(self):
        p = next(s for s in self.sections if s['name'] == '.pdata')
        out = {}
        raw = self.data[p['rva']:p['rva'] + p['virtual_size']]
        for i in range(0, len(raw) - 7, 8):
            start, word = struct.unpack('>II', raw[i:i + 8])
            if not start:
                continue
            length = ((word >> 8) & 0x3FFFFF) * 4
            out[start] = length
        return dict(sorted(out.items()))

    def function_of(self, va):
        import bisect
        keys = getattr(self, '_keys', None)
        if keys is None:
            keys = self._keys = list(self.functions)
        i = bisect.bisect_right(keys, va) - 1
        if i >= 0 and va < keys[i] + self.functions[keys[i]]:
            return keys[i]
        return None

    def disasm(self, start, length):
        return list(self.md.disasm(self.bytes(start, length), start))


def facts(img, start, length):
    """Chamadas diretas, cabeçalhos PM4 tipo 3 e deslocamentos de memória de uma função."""
    calls, pm4, offsets, consts = [], [], {}, []
    hi = {}
    for ins in img.disasm(start, length):
        m = ins.mnemonic
        ops = ins.operands
        if m in ('bl', 'b') and ops and ops[0].type == cppc.PPC_OP_IMM:
            target = ops[0].imm & 0xFFFFFFFF
            if m == 'bl' or not (start <= target < start + length):
                calls.append(target)
        elif m == 'lis' and len(ops) == 2:
            hi[ops[0].reg] = (ops[1].imm & 0xFFFF) << 16
        elif m in ('ori', 'addi') and len(ops) == 3 and ops[1].reg in hi:
            v = (hi[ops[1].reg] + (ops[2].imm if m == 'addi' else ops[2].imm & 0xFFFF)) & 0xFFFFFFFF
            consts.append(v)
            if (v >> 30) == 3 and (v & 0xFF) == 0:
                pm4.append(((v >> 8) & 0x7F, (v >> 16) & 0x3FFF))
        if ops and m[0] in 'ls' and any(o.type == cppc.PPC_OP_MEM for o in ops):
            for o in ops:
                if o.type == cppc.PPC_OP_MEM:
                    key = ('st' if m.startswith('st') else 'ld', o.mem.disp & 0xFFFF)
                    offsets[key] = offsets.get(key, 0) + 1
    return calls, pm4, offsets, consts
