#!/usr/bin/env python3
"""Lista o índice de um CRI CPK (cabeçalho CPK, TOC, ITOC, ETOC, GTOC).

Lê só tabelas @UTF e os primeiros bytes de cada arquivo (tipo/compressão).
Não extrai nem grava dados do jogo no repositório.

Uso:
  python3 -I tools/assets/cpk_list.py layer1.cpk [--json saida.json] [--resumo]
"""
import argparse
import collections
import json
import os
import struct
import sys

TYPES = {0: '>B', 1: '>b', 2: '>H', 3: '>h', 4: '>I', 5: '>i', 6: '>Q', 7: '>q', 8: '>f'}


def utf_xor(data):
    """Tabelas cifradas do CRI: XOR com sequência multiplicativa."""
    out = bytearray(len(data))
    m, t = 0x5F, 0x15
    for i, b in enumerate(data):
        out[i] = b ^ (m & 0xFF)
        m = (m * t) & 0xFF
    return bytes(out)


def parse_utf(blob):
    if blob[:4] != b'@UTF':
        dec = utf_xor(blob)
        if dec[:4] != b'@UTF':
            raise ValueError('tabela @UTF não encontrada')
        blob = dec
    size = struct.unpack('>I', blob[4:8])[0]
    t = blob[8:8 + size]
    rows_off, str_off, data_off, name_off, ncol, row_len, nrows = struct.unpack('>IIIIHHI', t[:24])

    def string(o):
        e = t.index(b'\0', str_off + o)
        return t[str_off + o:e].decode('shift_jis', 'replace')

    def value(kind, pos):
        if kind in TYPES:
            fmt = TYPES[kind]
            n = struct.calcsize(fmt)
            return struct.unpack(fmt, t[pos:pos + n])[0], n
        if kind == 0xA:
            return string(struct.unpack('>I', t[pos:pos + 4])[0]), 4
        if kind == 0xB:
            o, n = struct.unpack('>II', t[pos:pos + 8])
            return t[data_off + o:data_off + o + n], 8
        raise ValueError('tipo @UTF %x' % kind)

    cols = []
    p = 24
    for _ in range(ncol):
        flags = t[p]
        name = string(struct.unpack('>I', t[p + 1:p + 5])[0])
        p += 5
        storage, kind = flags & 0xF0, flags & 0x0F
        const = None
        if storage == 0x30:
            const, n = value(kind, p)
            p += n
        cols.append((name, storage, kind, const))
    rows = []
    for r in range(nrows):
        p = rows_off + r * row_len
        row = {}
        for name, storage, kind, const in cols:
            if storage == 0x50:
                row[name], n = value(kind, p)
                p += n
            elif storage == 0x30:
                row[name] = const
            else:
                row[name] = None
        rows.append(row)
    return string(name_off), rows


def read_packet(f, offset, magic):
    f.seek(offset)
    head = f.read(16)
    if head[:4] != magic:
        raise ValueError('%r esperado em 0x%X, achado %r' % (magic, offset, head[:4]))
    size = struct.unpack('<Q', head[8:16])[0]
    return parse_utf(f.read(size))


MAGICS = [
    (b'CRILAYLA', 'crilayla'), (b'@UTF', 'utf'), (b'CPK ', 'cpk'), (b'AFS\0', 'afs'),
    (b'DDS ', 'dds'), (b'XPR2', 'xpr2'), (b'RIFF', 'riff'), (b'\x80\x00', 'adx'),
    (b'XWB', 'xwb'), (b'WBND', 'xwb'), (b'XGSF', 'xgs'), (b'SDBK', 'xsb'), (b'DAT\0', 'dat'),
    (b'WTB\0', 'wtb'), (b'WMB\0', 'wmb'), (b'WMB3', 'wmb3'), (b'MOT\0', 'mot'), (b'EFF\0', 'eff'),
    (b'BXM\0', 'bxm'), (b'XML\0', 'xml'), (b'MCD\0', 'mcd'), (b'SDDS', 'sdds'),
]


def sniff(head):
    for m, n in MAGICS:
        if head.startswith(m):
            return n
    return head[:4].hex()


class Window:
    """Arquivo visto a partir de um deslocamento (CPK dentro de CPK)."""
    def __init__(self, f, base):
        self.f, self.base = f, base
    def seek(self, o):
        self.f.seek(self.base + o)
    def read(self, n):
        return self.f.read(n)


def list_cpk(path, sniff_bytes=16, base=0):
    with open(path, 'rb') as raw:
        f = Window(raw, base)
        _, hdr = read_packet(f, 0, b'CPK ')
        h = hdr[0]
        info = {k: (v if not isinstance(v, bytes) else len(v)) for k, v in h.items()}
        entries = []
        toc_off = h.get('TocOffset') or 0
        content_off = h.get('ContentOffset') or 0
        if toc_off:
            _, toc = read_packet(f, toc_off, b'TOC ')
            # Regra do CRI: offsets do TOC são relativos ao menor entre TocOffset e ContentOffset.
            base = min(x for x in (toc_off, content_off) if x) if content_off else toc_off
            for r in toc:
                d = r.get('DirName') or ''
                name = (d + '/' if d else '') + (r.get('FileName') or '')
                entries.append({'id': r.get('ID'), 'name': name, 'offset': base + r['FileOffset'],
                                'size': r['FileSize'], 'extract': r.get('ExtractSize') or r['FileSize'],
                                'user': r.get('UserString')})
        if h.get('ItocOffset'):
            _, itoc = read_packet(f, h['ItocOffset'], b'ITOC')
            info['itoc_rows'] = len(itoc)
        if h.get('EtocOffset'):
            try:
                _, etoc = read_packet(f, h['EtocOffset'], b'ETOC')
                info['etoc_rows'] = len(etoc)
            except ValueError as e:
                info['etoc_error'] = str(e)
        if h.get('GtocOffset'):
            try:
                _, gtoc = read_packet(f, h['GtocOffset'], b'GTOC')
                info['gtoc_rows'] = len(gtoc)
            except ValueError as e:
                info['gtoc_error'] = str(e)
        for e in entries:
            f.seek(e['offset'])
            head = f.read(sniff_bytes)
            e['magic'] = sniff(head)
            e['compressed'] = head.startswith(b'CRILAYLA') or e['extract'] != e['size']
    return info, entries


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('cpk')
    ap.add_argument('--json', help='grava o índice (privado: fora do git)')
    ap.add_argument('--resumo', action='store_true')
    a = ap.parse_args()
    info, entries = list_cpk(a.cpk)
    print('arquivo', os.path.basename(a.cpk), 'tamanho', os.path.getsize(a.cpk))
    for k in ('Version', 'Revision', 'Align', 'Files', 'TocOffset', 'ContentOffset', 'ItocOffset',
              'EtocOffset', 'GtocOffset', 'EnabledPackedSize', 'EnabledDataSize', 'Sorted', 'CpkMode',
              'Tvers', 'itoc_rows', 'etoc_rows', 'gtoc_rows', 'etoc_error', 'gtoc_error'):
        if k in info:
            print(' ', k, info[k])
    print('  entradas', len(entries))
    by_ext = collections.defaultdict(lambda: [0, 0, 0, 0])
    by_magic = collections.Counter()
    for e in entries:
        ext = os.path.splitext(e['name'])[1].lower() or '(sem)'
        s = by_ext[ext]
        s[0] += 1; s[1] += e['size']; s[2] += e['extract']; s[3] += e['compressed']
        by_magic[(ext, e['magic'])] += 1
    print('  extensão  qtd  empacotado  extraído  comprimidos')
    for ext, (n, ps, xs, c) in sorted(by_ext.items(), key=lambda x: -x[1][2]):
        print('  %-8s %5d %11d %11d %5d' % (ext, n, ps, xs, c))
    if a.resumo:
        print('  (extensão, magia) mais comuns')
        for (ext, m), n in by_magic.most_common(60):
            print('   ', ext, m, n)
        dirs = collections.Counter(e['name'].split('/')[0] for e in entries)
        print('  diretórios de 1º nível')
        for d, n in dirs.most_common(40):
            print('   ', d, n)
    nested = [e for e in entries if e['magic'] == 'cpk']
    for e in nested:
        ni, ne = list_cpk(a.cpk, base=e['offset'])
        for x in ne:
            x['offset'] += e['offset']
            x['name'] = e['name'] + '!' + x['name']
        print('  aninhado', e['name'], 'entradas', len(ne), 'extraído', sum(x['extract'] for x in ne),
              'tipos', dict(collections.Counter(os.path.splitext(x['name'])[1].lower() for x in ne)))
        entries.extend(ne)
    if a.json:
        with open(a.json, 'w') as out:
            json.dump({'info': info, 'entries': entries}, out, ensure_ascii=False, indent=0, default=str)


if __name__ == '__main__':
    main()
