#!/usr/bin/env python3
"""Lista as bibliotecas importadas e estáticas (versão do XDK) de um XEX2.

Lê só o cabeçalho em claro do XEX; não decifra nem descomprime a imagem.
Uso: python3 -I tools/render/xex_libs.py caminho/default.xex
"""
import struct
import sys


def ver(x):
    return '%d.%d.%d.%d' % (x >> 28, (x >> 24) & 15, (x >> 8) & 0xFFFF, x & 0xFF)


def main(path):
    d = open(path, 'rb').read(0x20000)
    if d[:4] != b'XEX2':
        sys.exit('não é XEX2')
    count = struct.unpack('>I', d[20:24])[0]
    headers = dict(struct.unpack('>II', d[24 + 8 * i:32 + 8 * i]) for i in range(count))
    if 0x00010201 in headers:
        print('base', hex(headers[0x00010201]))
    if 0x00010100 in headers:
        print('entrada', hex(headers[0x00010100]))
    v = headers.get(0x000103FF)
    if v is not None:
        strsz, cnt = struct.unpack('>II', d[v + 4:v + 12])
        names = [x.decode() for x in d[v + 12:v + 12 + strsz].split(b'\0') if x]
        p = v + 12 + strsz
        for _ in range(cnt):
            size = struct.unpack('>I', d[p:p + 4])[0]
            version, minimum = struct.unpack('>II', d[p + 0x1C:p + 0x24])
            index, imports = struct.unpack('>HH', d[p + 0x24:p + 0x28])
            print('import', names[index], ver(version), 'mín', ver(minimum), 'registros', imports)
            p += size
    v = headers.get(0x000200FF)
    if v is not None:
        size = struct.unpack('>I', d[v:v + 4])[0]
        for o in range(v + 4, v + size, 16):
            name = d[o:o + 8].rstrip(b'\0').decode()
            a, b, c, e = struct.unpack('>4H', d[o + 8:o + 16])
            print('estática', name, f'{a}.{b}.{c}.{e & 0xFF}')


if __name__ == '__main__':
    main(sys.argv[1])
