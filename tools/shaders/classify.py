#!/usr/bin/env python3
"""Classifica VS/PS, valida ESSL 100 (glslangValidator) e mede uniformes contra a Mali-450.
Uso: python3 -I tools/shaders/classify.py <diretório de saída do nier_shaders>"""
import csv,collections,hashlib,subprocess,re,os,json,sys
os.chdir(sys.argv[1])
r=list(csv.DictReader(open('report.tsv'),delimiter='\t'))
by=collections.defaultdict(dict)
for x in r: by[hashlib.sha1(open(f"{x['hash']}_{x['tipo']}.ucode",'rb').read()).hexdigest()][x['tipo']]=x
final=[]
for k,v in by.items():
    ps,vs=v.get('ps'),v.get('vs')
    if ps and 'vfetch no shader de pixel' in ps['erro']: t=vs or ps; 
    elif ps and ps['traduziu']=='1': t=ps
    else: t=vs or ps
    if t is ps and ps and 'vfetch no shader de pixel' in ps['erro'] and not vs: t=dict(ps,tipo='vs?')
    final.append(t)
os.makedirs('glslang',exist_ok=True)
res=collections.Counter(); rows=[]
for t in final:
    ok=t['traduziu']=='1'; comp='-'
    tp=t['tipo'].rstrip('?')
    src=open(f"{t['hash']}_{tp}.glsl").read() if os.path.exists(f"{t['hash']}_{tp}.glsl") else ''
    if ok:
        fn=f"glslang/{t['hash']}.{'vert' if tp=='vs' else 'frag'}"; open(fn,'w').write(src)
        p=subprocess.run(['glslangValidator',fn],capture_output=True,text=True)
        comp='ok' if p.returncode==0 else 'erro'
        if comp=='erro':
            ls=[l for l in p.stdout.splitlines() if 'ERROR' in l]; t['erro']=(ls[0] if ls else p.stdout)[:120]
    body=src.split('void main()',1)[-1]
    idx=[int(m) for m in re.findall(r'\b[VP]C\[(\d+)\]',body)]
    dyn=bool(re.search(r'\b[VP]C\[[^\]0-9]',body))
    t.update(dict(compila=comp,uniform_max_idx=(max(idx)+1 if idx else 0),uniform_distintos=len(set(idx)),
        uniform_extra=len(re.findall(r'uniform\s+(?:\w+\s+)?(?:float|int|bool|vec[234]|mat4)\s+\w+\s*;',src)),indice_dinamico=dyn))
    res[(t['tipo'],ok,comp)]+=1; rows.append(t)
print('resultado (tipo, traduziu, glslang ESSL100):',dict(res))
for tp in ('vs','ps'):
    xs=[t for t in rows if t['tipo']==tp and t['traduziu']=='1']
    if not xs: continue
    over=[t for t in xs if t['uniform_max_idx']+t['uniform_extra']>256 or t['indice_dinamico']]
    print(tp,'traduzidos',len(xs),'| VC/PC[256] declarado: precisam compactar',len(over),
          '| maior índice',max(t['uniform_max_idx'] for t in xs),'| distintos máx',max(t['uniform_distintos'] for t in xs),
          '| índice dinâmico',sum(t['indice_dinamico'] for t in xs),
          '| instr estim máx',max(int(t['instr_estim']) for t in xs),'| varyings máx',max(int(t['varyings']) for t in xs),'| samplers máx',max(int(t['samplers']) for t in xs))
print(collections.Counter(t['erro'][:60] for t in rows if t['traduziu']=='0' or t['compila']=='erro').most_common(10))
json.dump(rows,open('final.json','w'),indent=0)
