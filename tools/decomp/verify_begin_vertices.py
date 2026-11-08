#!/usr/bin/env python3
"""Prova BeginVertices isolando a alocação GPU; restaura o link normal ao terminar."""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess

ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('build',type=Path)
ap.add_argument('pe',type=Path)
ap.add_argument('output',type=Path)
ap.add_argument('--trials',type=int,default=2000)
a=ap.parse_args()
if a.trials<500:ap.error('a prova exige ao menos 500 tentativas')
root=Path(__file__).resolve().parents[2]
build=a.build.resolve()
old_flags=next(line.split('=',1)[1] for line in (build/'CMakeCache.txt').read_text().splitlines()
               if line.startswith('CMAKE_EXE_LINKER_FLAGS:STRING='))
if '--wrap=sub_82573670' in old_flags:ap.error('restaure primeiro o link normal deste build')
commands=subprocess.check_output(['ninja','-C',str(build),'-t','commands'],text=True)
command=next(line for line in commands.splitlines() if ' -c ' in line and '/src/decomp/d3d/draw_packets.cpp' in line)
args=shlex.split(command)
if '-MD' in args:args.remove('-MD')
for option in ('-MT','-MF'):
    if option in args:
        i=args.index(option);del args[i:i+2]
obj=build/'nierverify_gpu_allocator.o'
args[args.index('-o')+1]=str(obj)
args[args.index('-c')+1]=str(root/'tools/decomp/fixtures/gpu_allocator.cpp')
subprocess.run(args,cwd=build,check=True)
log=a.output.resolve().with_suffix('.build.log')
def configure(flags):
    subprocess.run(['cmake','-S',str(root/'tools/verify'),'-B',str(build),
                    '-DCMAKE_EXE_LINKER_FLAGS='+flags],check=True,stdout=stream,stderr=subprocess.STDOUT)
    subprocess.run(['cmake','--build',str(build),'-j','3'],check=True,stdout=stream,stderr=subprocess.STDOUT)
with log.open('w') as stream:
    try:
        configure(old_flags+' -Wl,--wrap=sub_82573670 '+shlex.quote(str(obj)))
        env=dict(os.environ,NIER_PE=str(a.pe.resolve()),NIER_OUT=str(a.output.resolve()),
                 NIER_TRIALS=str(a.trials),NIER_TIME_SLACK='1.0',NIER_TIME_ABS='0')
        result=subprocess.run([str(build/'nierverify'),'sub_82571070'],env=env,check=False)
        data=json.loads(a.output.read_text())
        data['dependency_fixture']='GPU allocator shared by oracle/native; full r3-r5 logged in scratch; production allocator unchanged'
        for row in data['results']:
            file=row['file'];start=file.find('src/decomp/')
            if start>=0:row['file']=file[start:]
        a.output.write_text(json.dumps(data,indent=1)+'\n')
    finally:configure(old_flags)
raise SystemExit(result.returncode)
