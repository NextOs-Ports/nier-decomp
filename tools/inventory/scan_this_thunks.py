#!/usr/bin/env python3
"""Find data-referenced PPC this-adjustment thunks across every executable section."""
import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import re
import struct


def scan(pe, info, registered):
    if hashlib.sha256(pe).hexdigest() != info['pe_sha256']:
        raise ValueError('PE identity mismatch')
    base=info['load_address']
    code=[s for s in info['sections'] if s['executable']]
    def executable(address):
        return not address%4 and any(s['address']<=address<s['end'] for s in code)
    candidates={}
    for section in code:
        for offset in range(section['rva'],min(len(pe),section['rva']+section['virtual_size'])-7,4):
            first=struct.unpack_from('>I',pe,offset)[0]
            if first & 0xffff0000 != 0x38630000:  # addi r3,r3,simm16
                continue
            branch=struct.unpack_from('>I',pe,offset+4)[0]
            if branch & 0xfc000001 != 0x48000000: # unconditional b, never bl
                continue
            displacement=branch & 0x03fffffc
            if displacement & 0x02000000: displacement-=0x04000000
            address=base+offset
            target=(displacement if branch&2 else address+4+displacement)&0xffffffff
            if not executable(target):
                continue
            adjust=first&0xffff
            if adjust&0x8000:adjust-=0x10000
            candidates[address]=dict(address=address,size=8,adjust=adjust,target=target,
                target_registered=target in registered,registered=address in registered,
                section=section['name'],references=[])
    for section in info['sections']:
        if section['executable']:continue
        start=section['rva'];end=min(len(pe),start+section['virtual_size'])
        end-=max(0,end-start)%4
        if start>=end:continue
        for index,(value,) in enumerate(struct.iter_unpack('>I',pe[start:end])):
            if value in candidates:
                candidates[value]['references'].append(dict(address=base+start+index*4,section=section['name']))
    found=sorted((c for c in candidates.values() if c['references']),key=lambda c:c['address'])
    return dict(schema_version=1,pe_sha256=info['pe_sha256'],
        criteria='addi r3,r3,simm16; b executable_target, at least one aligned pointer from a non-executable PE section',
        sections_scanned=[s['name'] for s in code],pattern_matches=len(candidates),
        referenced_count=len(found),missing_count=sum(not c['registered'] for c in found),thunks=found)


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--pe',type=Path,required=True)
    ap.add_argument('--info',type=Path,required=True)
    ap.add_argument('--generated',type=Path,required=True)
    ap.add_argument('--output',type=Path,required=True)
    a=ap.parse_args()
    registered={int(x,16) for x in re.findall(r'\{ 0x([0-9a-fA-F]+),', (a.generated/'nier_init.cpp').read_text())}
    result=scan(a.pe.read_bytes(),json.loads(a.info.read_text()),registered)
    a.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k!='thunks'}))
    print('missing:', ', '.join(f"0x{t['address']:08X}" for t in result['thunks'] if not t['registered']))

if __name__=='__main__':main()
