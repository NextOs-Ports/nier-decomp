#!/usr/bin/env python3
"""Export private ReXGlue/PData metadata without publishing translated code."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import struct
import tomllib


def translated_functions(directory):
    mapping = {name: int(address, 16) for address, name in re.findall(
        r'\{ 0x([0-9A-Fa-f]+), (\w+) \}', (directory/'nier_init.cpp').read_text())}
    functions = {}
    switches = []
    for source in sorted(directory.glob('nier_recomp.*.cpp')):
        current = None
        pc = None
        for line in source.read_text().splitlines():
            match = re.match(r'DEFINE_REX_FUNC\((\w+)\)', line)
            if match:
                name = match[1]
                pc = mapping.get(name)
                current = dict(name=name, address=pc, instructions=[], switches=[])
                if pc is not None:
                    functions[pc] = current
                continue
            if pc is None:
                continue
            match = re.match(r'\s*loc_([0-9A-Fa-f]+):', line)
            if match:
                pc = int(match[1], 16)
            if line.startswith('\t// '):
                current['instructions'].append(pc)
                pc += 4
            if line.startswith('\tswitch ('):
                current['switches'].append(pc-4)
                switches.append(pc-4)
    return mapping, functions, switches


def export(pe, info, generated, hints):
    data = pe.read_bytes()
    if hashlib.sha256(data).hexdigest() != info['pe_sha256']:
        raise ValueError('PE hash differs from metadata')
    base = info['load_address']
    ranges = [dict(address=s['address'], size=s['virtual_size'], name=s['name'])
              for s in info['sections'] if s['executable']]
    pdata = {}
    for s in info['sections']:
        if s['name'] == '.pdata':
            for offset in range(s['rva'], s['rva']+s['virtual_size'], 8):
                address, word = struct.unpack_from('>II', data, offset)
                size = ((word >> 8) & 0x3fffff)*4
                if size and any(r['address'] <= address and address+size <= r['address']+r['size'] for r in ranges):
                    pdata[address] = size
    mapping, translated, switches = translated_functions(generated)
    names = {v:k for k,v in mapping.items()}
    starts = sorted(set(names) | set(pdata))
    output, issues = [], []
    for index, address in enumerate(starts):
        containing = next((r for r in ranges if r['address'] <= address < r['address']+r['size']), None)
        if containing is None:
            issues.append(dict(address=address, issue='outside executable sections'))
            continue
        next_start = starts[index+1] if index+1 < len(starts) else containing['address']+containing['size']
        cap = min(next_start, containing['address']+containing['size'])-address
        fn = translated.get(address, {})
        insns = fn.get('instructions', [])
        explicit = hints.get('functions', {}).get(f'0x{address:08X}', {}).get('size', 0)
        size = pdata.get(address) or explicit or (max(insns)+4-address if insns else 16)
        origin = 'pdata' if address in pdata else 'explicit-switch-extent' if explicit else 'translated-instruction-addresses' if insns else 'import-thunk'
        if size > cap:
            issues.append(dict(address=address, issue='overlap with next function', original_size=size, clipped_size=cap))
            size = cap
            origin += ':split-at-next-entry'
        if size <= 0:
            raise ValueError('Nonpositive function extent')
        name = names.get(address, f'sub_{address:08X}')
        library = None
        evidence = None
        if name.startswith('__imp__'):
            imported = name.removeprefix('__imp__')
            library = 'XAudio' if imported.startswith('XAudio') else 'D3D/XDK-video' if imported.startswith('Vd') else 'XDK'
            evidence = 'loader import ordinal/name'
        elif name.startswith(('__save', '__rest')):
            library, evidence = 'CRT', 'ReXGlue save/restore signature'
        elif not name.startswith('sub_'):
            library, evidence = 'recognized-library', 'ReXGlue signature name; family requires review'
        output.append(dict(address=address, size=size, name=name,
                           status='recompiled' if address in names else 'discovered',
                           original_extent_before_partition=pdata.get(address) or explicit or (max(insns)+4-address if insns else 16),
                           boundary_origin=origin, library=library,
                           library_evidence=evidence))
    return dict(schema_version=1, baseline=dict(title='NieR (Xbox 360, 2010)',
                version='USA/Europe base XEX, no title update',
                executable_sha256=info['xex_sha256'], inventory_complete=False,
                inventory_evidence=None, code_ranges=ranges), functions=output,
                analysis=dict(pe_sha256=info['pe_sha256'], translated_entries=len(names),
                              emitted_switches=len(switches), pdata_entries=len(pdata),
                              boundary_issues=issues,
                              library_classification='Only named signatures/imports classified; no address-neighborhood guessing',
                              completeness='Partial pending review of uncovered code and inferred/split extents'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pe',type=Path,required=True)
    parser.add_argument('--info',type=Path,required=True)
    parser.add_argument('--generated',type=Path,required=True)
    parser.add_argument('--hints',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args = parser.parse_args()
    document = export(args.pe,json.loads(args.info.read_text()),args.generated,
                      tomllib.loads(args.hints.read_text()))
    args.output.write_text(json.dumps(document,indent=2)+'\n')
    print(json.dumps(dict(functions=len(document['functions']),
                          original_bytes=sum(f['size'] for f in document['functions']),
                          statuses=dict(Counter(f['status'] for f in document['functions'])),
                          boundaries=dict(Counter(f['boundary_origin'] for f in document['functions'])),
                          boundary_issues=len(document['analysis']['boundary_issues']),
                          emitted_switches=document['analysis']['emitted_switches'])))

if __name__ == '__main__':
    main()
