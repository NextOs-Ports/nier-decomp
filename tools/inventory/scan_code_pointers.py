#!/usr/bin/env python3
"""Audit lis+addi/ori code pointers, separating entries from inline dispatch data."""
import argparse
from collections import Counter,defaultdict
import ctypes
import hashlib
import json
from pathlib import Path
import re
import struct


def scan(pe,info,registered,decoder,window):
    if hashlib.sha256(pe).hexdigest()!=info['pe_sha256']:raise ValueError('PE identity mismatch')
    base=info['load_address'];sections=[s for s in info['sections'] if s['executable']]
    library=ctypes.CDLL(str(decoder.resolve()));decode=library.nier_decode_word
    decode.restype=ctypes.c_char_p;decode.argtypes=[ctypes.c_uint32,ctypes.c_uint32,ctypes.POINTER(ctypes.c_uint32)]
    def word(address):return struct.unpack_from('>I',pe,address-base)[0]
    def executable(address):return address%4==0 and any(s['address']<=address<s['end'] for s in sections)
    def instruction(address):
        operands=(ctypes.c_uint32*8)();name=decode(word(address),address,operands)
        return (name.decode() if name else '',list(operands))
    refs=defaultdict(list)
    for section in sections:
        for address in range(section['address'],section['end']-4,4):
            first=word(address)
            if first>>26!=15 or (first>>16)&31:continue
            reg=(first>>21)&31;high=(first&65535)<<16
            if not any(s['address']-0x8000<=high<=s['end']+0x8000 for s in sections):continue
            for distance in range(1,window+1):
                at=address+distance*4
                if at>=section['end']:break
                value=word(at);op=value>>26;rd=(value>>21)&31;ra=(value>>16)&31
                name,operands=instruction(at)
                if not name:break
                target=None;destination=None
                if op==14 and ra==reg:
                    low=value&65535;low=low-65536 if low&32768 else low
                    target=(high+low)&0xffffffff;destination=rd
                elif op==24 and rd==reg:
                    target=high|(value&65535);destination=ra
                if target is not None:
                    if executable(target):refs[target].append(dict(lis=address,combine=at,register=destination))
                    if destination==reg:break
                    continue
                if name.startswith('b'):
                    if name in ('bl','bla','bctrl') and reg>=14:continue
                    break
                # Updated addressing writes RA; ordinary stores/FP/vector ops do not write GPR0.
                if (name.endswith('u') or name.endswith('ux')) and name.startswith(('st','l')) and ra==reg:break
                if name=='nop' or name.startswith(('st','cmp','cmpl','f','v','lv','lf','mt','dc','ic','sync','eieio','isync')):continue
                if operands[0]==reg:break
    details=[]
    for target,references in sorted(refs.items()):
        if target in registered:kind='registered_entry'
        elif target-base+32<=len(pe) and all(executable(word(target+i*4)) for i in range(8)):
            kind='inline_code_pointer_table'
        else:
            dispatch=[]
            for reference in references:
                names=[];ops=[]
                for step in range(1,7):
                    at=reference['combine']+step*4
                    if not executable(at):break
                    name,operands=instruction(at);names.append(name);ops.append(operands)
                    if name.startswith('b'):break
                reg=reference['register']
                dispatch.append('bctr' in names and any(n=='add' and o[0]==reg and reg in o[1:3] for n,o in zip(names,ops)))
            if all(dispatch):kind='computed_dispatch_anchor'
            elif all(instruction(target+i*4)[0] for i in range(3)):
                kind='unregistered_code_entry'
            else:kind='needs_review'
        details.append(dict(address=target,classification=kind,references=references))
    counts=Counter(d['classification'] for d in details)
    return dict(schema_version=1,pe_sha256=info['pe_sha256'],sections=[s['name'] for s in sections],
        instruction_window=window,criteria='lis followed by addi/ori while the high register is live; decoded GPR clobbers and branch boundaries checked',
        constructed_targets=len(details),counts=dict(counts),targets=details)


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--pe',type=Path,required=True);ap.add_argument('--info',type=Path,required=True)
    ap.add_argument('--generated',type=Path,required=True);ap.add_argument('--decoder',type=Path,required=True)
    ap.add_argument('--output',type=Path,required=True);ap.add_argument('--window',type=int,default=64)
    a=ap.parse_args();registered={int(x,16) for x in re.findall(r'\{ 0x([0-9A-Fa-f]+),',(a.generated/'nier_init.cpp').read_text())}
    result=scan(a.pe.read_bytes(),json.loads(a.info.read_text()),registered,a.decoder,a.window)
    a.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k!='targets'}))
    print('unresolved:',[hex(t['address']) for t in result['targets'] if t['classification'] in ['unregistered_code_entry','needs_review']])

if __name__=='__main__':main()
