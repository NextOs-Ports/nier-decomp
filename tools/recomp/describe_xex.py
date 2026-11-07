#!/usr/bin/env python3
"""Describe an unpatched XEX PE memory image; never emits keys or game bytes."""
import argparse
import hashlib
import json
import struct
from pathlib import Path


def describe(xex, pe, key):
    def u32(buf, off, endian='>'):
        return struct.unpack_from(endian + 'I', buf, off)[0]
    def u16(buf, off, endian='>'):
        return struct.unpack_from(endian + 'H', buf, off)[0]
    if xex[:4] != b'XEX2' or pe[:2] != b'MZ':
        raise ValueError('Expected XEX2 and decrypted MZ image')
    opts = dict(struct.unpack_from('>II', xex, 24 + 8*i)
                for i in range(u32(xex, 20)))
    nt = u32(pe, 60, '<')
    if pe[nt:nt+4] != b'PE\0\0':
        raise ValueError('Invalid PE signature')
    optional = nt + 24
    base = u32(pe, optional + 28, '<')
    entry = base + u32(pe, optional + 16, '<')
    sections = []
    start = optional + u16(pe, nt + 20, '<')
    for i in range(u16(pe, nt + 6, '<')):
        off = start + i*40
        size, rva, raw_size, raw_offset = struct.unpack_from('<IIII', pe, off+8)
        flags = u32(pe, off+36, '<')
        sections.append(dict(name=pe[off:off+8].split(b'\0')[0].decode(),
                             address=base+rva, end=base+rva+size, rva=rva,
                             virtual_size=size, raw_size=raw_size, raw_offset=raw_offset,
                             flags=f'0x{flags:08x}', executable=bool(flags & 0x20000000),
                             readable=bool(flags & 0x40000000), writable=bool(flags & 0x80000000)))
    imports = []
    if 0x103ff in opts:
        off = opts[0x103ff]
        total, string_size, count = struct.unpack_from('>III', xex, off)
        names = []
        p = off+12
        for _ in range(count):
            end = xex.index(0, p)
            names.append(xex[p:end].decode())
            p = off+12 + ((end+1-(off+12)+3)//4)*4
        p = off+12+string_size
        while p < off+total:
            size = u32(xex, p)
            if size < 40 or p+size > off+total:
                raise ValueError('Invalid import library length')
            records = []
            for i in range(u16(xex, p+38)):
                address = u32(xex, p+40+4*i)
                if not address:
                    continue
                value = u32(pe, address-base)
                records.append(dict(address=address, ordinal=value & 0xffff, type=value >> 24))
            imports.append(dict(library=names[u16(xex,p+36)&255],
                                version=u32(xex,p+28), records=records))
            p += size
    fmt = opts[0x3ff]
    encryption, compression = struct.unpack_from('>HH', xex, fmt+4)
    return dict(schema_version=1, xex_sha256=hashlib.sha256(xex).hexdigest(),
                pe_sha256=hashlib.sha256(pe).hexdigest(), pe_size=len(pe),
                layout='memory image; file offset = RVA; dumped before import patching',
                load_address=base, entry=entry, xex_entry=opts.get(0x10100),
                encryption=encryption, key_family=key,
                compression={0:'none',1:'basic',2:'normal LZX',3:'delta'}.get(compression,compression),
                sections=sections, imports=imports)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('xex',type=Path)
    ap.add_argument('pe',type=Path)
    ap.add_argument('output',type=Path)
    ap.add_argument('--key-family',required=True,choices=['retail','devkit','none'])
    a = ap.parse_args()
    result = describe(a.xex.read_bytes(),a.pe.read_bytes(),a.key_family)
    if result['entry'] != result['xex_entry']:
        raise ValueError('PE and XEX entry points differ')
    a.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:result[k] for k in ['pe_sha256','pe_size','load_address','entry','compression']}))

if __name__ == '__main__':
    main()
