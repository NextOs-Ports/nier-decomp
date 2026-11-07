#!/usr/bin/env python3
"""Symbolize private ARM/x86 sampler JSON on the PC; no perf required on target."""
import argparse,bisect,collections,hashlib,json,re,struct,subprocess
from pathlib import Path
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('profile',type=Path)
ap.add_argument('--elf',type=Path,action='append',required=True)
ap.add_argument('--inventory',type=Path,required=True)
ap.add_argument('--output',type=Path,required=True)
a=ap.parse_args();profile=json.loads(a.profile.read_text());
if not profile['samples']:ap.error('No CPU samples; inspect timer errors and collection window')
inventory=json.loads(a.inventory.read_text())
guest_names={f['name']:f['address'] for f in inventory['functions']}
class Elf:
    def __init__(self,path):
        self.path=path;self.loads=[];self.build_id=None
        with path.open('rb') as f:
            header=f.read(64)
            if header[:6]!=b'\x7fELF\x02\x01':raise ValueError('Expected little-endian ELF64')
            self.machine=struct.unpack_from('<H',header,18)[0]
            phoff=struct.unpack_from('<Q',header,32)[0];entry,count=struct.unpack_from('<HH',header,54)
            for i in range(count):
                f.seek(phoff+i*entry);ph=struct.unpack('<IIQQQQQQ',f.read(56))
                if ph[0]==1 and ph[1]&1:self.loads.append((ph[2],ph[3],ph[5]))
                if ph[0]==4:
                    f.seek(ph[2]);notes=f.read(ph[5]);at=0
                    while at+12<=len(notes):
                        namesz,descsz,kind=struct.unpack_from('<III',notes,at);at+=12
                        name=notes[at:at+namesz];at+=(namesz+3)&~3
                        data=notes[at:at+descsz];at+=(descsz+3)&~3
                        if kind==3 and name==b'GNU\0':self.build_id=data.hex()
        rows=subprocess.check_output(['llvm-nm','-C','-n','-S','--defined-only',str(path)],text=True)
        unique={}
        for line in rows.splitlines():
            parts=line.split(None,3)
            if len(parts)!=4 or parts[2] not in 'tTwW':continue
            address,size=int(parts[0],16),int(parts[1],16);name=parts[3]
            prior=unique.get(address)
            if prior is None or name.startswith('sub_'):unique[address]=(address,size,name)
        self.symbols=sorted(unique.values());self.starts=[s[0] for s in self.symbols]
    def symbol(self,pc,mapping):
        start,end,offset,name=mapping;vaddr=pc-start+offset
        for fileoff,virt,filesz in self.loads:
            if fileoff//4096*4096<=offset<(fileoff+filesz+4095)//4096*4096:
                vaddr=pc-start+(virt//4096*4096)+(offset-fileoff//4096*4096);break
        index=bisect.bisect_right(self.starts,vaddr)-1
        if index>=0:
            address,size,symbol=self.symbols[index]
            if size and vaddr<address+size:return symbol
        return f'{self.path.name}+0x{vaddr:x}'
elfs={p.name:Elf(p) for p in a.elf};maps=[]
for name,elf in elfs.items():
    expected=profile.get('build_ids',{}).get(name)
    if expected and expected!=elf.build_id:ap.error(f'ELF build ID mismatch: {name}; use the binary from this exact capture')
for line in profile['maps'].splitlines():
    parts=line.split(None,5)
    if len(parts)<5 or 'x' not in parts[1]:continue
    start,end=(int(x,16) for x in parts[0].split('-'));name=parts[5] if len(parts)>5 else '[anonymous]'
    maps.append((start,end,int(parts[2],16),name.removesuffix(' (deleted)')))
maps.sort();starts=[m[0] for m in maps]
guest=collections.Counter();host=collections.Counter();threads=collections.Counter();thread_guest=collections.defaultdict(collections.Counter)
for row in profile['pcs']:
    pc=int(row['pc'],16);count=row['count'];tid=row['tid'];threads[tid]+=count
    at=bisect.bisect_right(starts,pc)-1;mapping=maps[at] if at>=0 and pc<maps[at][1] else None
    if mapping:
        module=Path(mapping[3]).name;elf=elfs.get(module)
        symbol=elf.symbol(pc,mapping) if elf else f'{module}+0x{pc-mapping[0]+mapping[2]:x}'
    else:module='unmapped';symbol=f'0x{pc:x}'
    normalized=symbol.removeprefix('__imp__');match=re.fullmatch(r'sub_([0-9a-fA-F]{8})(?:\..*)?',normalized)
    address=int(match[1],16) if match else guest_names.get(normalized)
    if module=='nier' and address is not None:
        guest[address]+=count;thread_guest[tid][address]+=count
    else:host[(module,symbol)]+=count
n=profile['samples'];names={t['tid']:t['name'] for t in profile['threads']};thread_metadata={t['tid']:t for t in profile['threads']}
def guest_rows(counts,total,limit=30):
    return [{'address':address,'name':f'sub_{address:08X}','own_samples':count,'own_percent':100*count/total if total else 0} for address,count in counts.most_common(limit)]
r={'schema_version':2,'architecture':{183:'aarch64',62:'x86_64'}.get(next(iter(elfs.values())).machine,'unknown'),'source':profile['method'],'requested_hz_per_cpu_thread':profile['hz'],'source_profile':str(a.profile),
   'percentage_denominator':'delivered PC samples; blocked/short-lived threads may be absent',
   'captured_cpu_seconds_estimate':n/profile['hz'],'sample_coverage_percent':100*n/profile['hz']/profile['process_cpu_seconds'] if profile['process_cpu_seconds'] else None,
   'sample_count':n,'process_cpu_seconds':profile['process_cpu_seconds'],'wall_seconds':profile['wall_seconds'],
   'complete':profile['complete'],'table_dropped':profile['table_dropped'],'timer_overruns':profile['timer_overruns'],'timer_errors':profile['timer_errors'],
   'elf_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in a.elf},
   'executable_sha256':inventory['baseline']['executable_sha256'],'top30':guest_rows(guest,n),
   'runtime_top30':[{'module':module,'symbol':symbol,'own_samples':count,'own_percent':100*count/n if n else 0} for (module,symbol),count in host.most_common(30)],
   'threads':[{'tid':tid,'name':names.get(tid,''),'cpu_seconds_observed':thread_metadata.get(tid,{}).get('cpu_seconds_observed'),'samples':count,'cpu_sample_percent':100*count/n if n else 0,'top_guest':guest_rows(thread_guest[tid],count,5)} for tid,count in threads.most_common()],
   'blocked_threads':[t for t in profile['threads'] if t.get('sigprof_blocked_last_scan')],
   'limitations':['Own-PC sampling only, no inclusive stack cost.','CPU-clock signal delivery may coalesce; overruns and table drops are explicit.','Threads created during collection are registered within about 250 ms; shorter-lived threads can be missed.','Kernel CPU time is sampled at interrupted/resumed userspace PC; no kernel stacks.','This profile does not measure or certify FPS.']}
a.output.write_text(json.dumps(r,indent=2)+'\n')
print(json.dumps({'samples':n,'guest_samples':sum(guest.values()),'top30':r['top30'][:5],'runtime':r['runtime_top30'][:5]},indent=2))
