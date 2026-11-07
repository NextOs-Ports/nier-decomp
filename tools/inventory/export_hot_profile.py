#!/usr/bin/env python3
"""Aggregate perf script CPU-clock samples by guest address and thread."""
import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
import re

HEADER = re.compile(r'^(.*?)\s+(\d+)/(\d+)\s+([\d.]+):\s+(\d+)\s+cpu-clock:u:\s*$')
FRAME = re.compile(r'^\s+[0-9a-f]+\s+(.+?)\s+\((.+)\)$')


def export(stacks, inventory, process_id):
    names = {f['name']: f['address'] for f in inventory['functions']}
    guest = set(names.values())
    threads = {}
    all_own, all_inclusive = Counter(), Counter()
    total_weight = 0
    total_samples = 0
    def address(symbol, dso):
        if not dso.endswith('/nier'):
            return None
        original = symbol.removeprefix('__imp__')
        match = re.fullmatch(r'sub_([0-9A-Fa-f]{8})', original)
        candidate = int(match[1],16) if match else names.get(symbol, names.get(original))
        return candidate if candidate in guest else None
    def accept(header, frames):
        nonlocal total_weight, total_samples
        if header is None or int(header[2]) != process_id:
            return
        comm,pid,tid,stamp,weight=header.groups()
        tid,weight=int(tid),int(weight)
        thread=threads.setdefault(tid,dict(name=comm,samples=0,weight=0,own=Counter(),inclusive=Counter(),host=Counter(),first=float(stamp),last=float(stamp)))
        thread['samples']+=1;thread['weight']+=weight;thread['last']=float(stamp)
        total_weight+=weight;total_samples+=1
        resolved=[address(sym,dso) for sym,dso in frames]
        if resolved and resolved[0] is not None:
            thread['own'][resolved[0]]+=weight;all_own[resolved[0]]+=weight
        elif frames:
            thread['host'][frames[0][0]]+=weight
        for addr in set(resolved)-{None}:
            thread['inclusive'][addr]+=weight;all_inclusive[addr]+=weight
    header=None;frames=[]
    with stacks.open() as stream:
        for line in stream:
            match=HEADER.match(line)
            if match:
                accept(header,frames);header=match;frames=[]
            elif (match:=FRAME.match(line)):
                frames.append((match[1],match[2]))
        accept(header,frames)
    def rows(own,inclusive,denominator):
        ordered=sorted(set(own)|set(inclusive),key=lambda a:(-own[a],-inclusive[a],a))[:300]
        return [dict(address=a,name=f'sub_{a:08X}',own_seconds=own[a]/1e9,
                     inclusive_seconds=inclusive[a]/1e9,
                     own_percent=100*own[a]/denominator,
                     inclusive_percent=100*inclusive[a]/denominator) for a in ordered]
    if not total_samples:
        raise ValueError('No samples matched process id / perf-script field format')
    return dict(schema_version=1,executable_sha256=inventory['baseline']['executable_sha256'],
        source='perf cpu-clock:u, 199 Hz, DWARF callchains with 16384-byte stacks, 60-second null boot',
        process_id=process_id,process_samples=total_samples,sampled_cpu_seconds=total_weight/1e9,
        percentage_denominator='All sampled user CPU time of this process; per-thread lists use that thread. Inclusive percentages overlap.',
        limitations=['Boot/null GPU, not gameplay or Mali CPU ranking',
                     'Finite sampling and truncated/unwound stacks can miss functions',
                     'Unobserved entries are omitted, never padded with invented timing'],
        top300=rows(all_own,all_inclusive,total_weight),
        threads=[dict(tid=tid,name=t['name'],samples=t['samples'],sampled_cpu_seconds=t['weight']/1e9,
                      observed_seconds=t['last']-t['first'],
                      top300=rows(t['own'],t['inclusive'],t['weight']),
                      top_host_own=[dict(symbol=s,percent=100*w/t['weight']) for s,w in t['host'].most_common(10)])
                 for tid,t in sorted(threads.items(),key=lambda item:-item[1]['weight'])])


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--stacks',type=Path,required=True)
    ap.add_argument('--inventory',type=Path,required=True)
    ap.add_argument('--pid',type=int,required=True)
    ap.add_argument('--output',type=Path,required=True)
    a=ap.parse_args()
    result=export(a.stacks,json.loads(a.inventory.read_text()),a.pid)
    a.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(dict(samples=result['process_samples'],cpu_seconds=result['sampled_cpu_seconds'],
                         listed=len(result['top300']),threads=len(result['threads']),top=result['top300'][:5]),indent=2))

if __name__=='__main__':
    main()
