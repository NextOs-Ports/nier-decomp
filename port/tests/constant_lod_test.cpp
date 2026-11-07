#include "../render/constant_lod.h"
#include <cstdio>
using namespace nier::render;
int main(){
 std::string vs="vec4 va = VC[3].yyyx;\nvec4 vb = VC[3].yyyx;\nvec4 vr = max(va, vb);\nv0.zw = vr.zw;";
 std::string ps="r[0] = v0;\n// NIER_SET_LOD r[0].w\n// NIER_LOD_SAMPLE 1\nnx_sample1(uv);\nvec4 nx_sample1(vec2 uv){}\n";
 auto a=ParseConstantLod(vs,ps);if(a.index!=3||a.component!=0||a.samplers!=2)return 1;
 if(ParseConstantLod(vs,"r[0] = v0; r[0].w = 0.0;\n// NIER_SET_LOD r[0].w\n// NIER_LOD_SAMPLE 1").index>=0)return 2;
 if(ParseConstantLod(vs,ps+"// NIER_SET_LOD r[1].x\n").index>=0)return 3;
 if(ParseConstantLod(vs,ps+"if (p) {}").index>=0)return 4;
 if(ParseConstantLod("v0 = attribute;",ps).index>=0)return 5;
 if(ParseConstantLod(vs,"r[0] = v0;").index>=0)return 6;
 std::puts("PASS constant LOD uniform mapping and 5 conservative rejection cases");
}
