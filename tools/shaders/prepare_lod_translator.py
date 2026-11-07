#!/usr/bin/env python3
"""Copy the private translator and annotate register-LOD operands; never modify the shared input."""
import argparse, pathlib, shutil, hashlib
p=argparse.ArgumentParser();p.add_argument('source',type=pathlib.Path);p.add_argument('output',type=pathlib.Path);a=p.parse_args()
a.output.mkdir(parents=True,exist_ok=True)
for name in ['xenos_shader.h','depth_codec.h']:shutil.copy2(a.source/name,a.output/name)
s=(a.source/'xenos_shader.cpp').read_text()
if 'NIER_SET_LOD ' in s:
 out=a.output/'xenos_shader.cpp'
 if not out.exists() or out.read_text()!=s:out.write_text(s)
 print(hashlib.sha256(s.encode()).hexdigest());raise SystemExit(0)
anchor='                    Line("    vec4 tex = nx_sample" + std::to_string(slot) + "(" + coords + ");");'
if s.count(anchor)!=1:raise SystemExit('unexpected sampler emitter; refusing patch')
s=s.replace(anchor,'                    if (pixel && Bits(w1, 29, 1) && !Bits(w1, 28, 1)) Line("// NIER_LOD_SAMPLE " + std::to_string(slot));\n'+anchor)
anchor='                    // NIER: setTextureLod / setTextureGradientsHorz/Vert: sem efeito no GLES2 (LOD automático).'
if s.count(anchor)!=1:raise SystemExit('unexpected LOD emitter; refusing patch')
s=s.replace(anchor,'                    if (pixel && op == 24) Line("// NIER_SET_LOD " + Reg(src) + "." + XYZW[Bits(w0, 26, 2)]);\n'+anchor)
out=a.output/'xenos_shader.cpp'
if not out.exists() or out.read_text()!=s:out.write_text(s)
print(hashlib.sha256(s.encode()).hexdigest())
