// Synthetic colored resolved levels through production ReplayDraw/TextureFor.
#include "../render/renderer.cpp"
int main(){
 using namespace nier::render;using namespace nier::d3d;
 setenv("NIER_BUFFER_REFERENCE","1",1);
 if(!CreatePbuffer()||!eglMakeCurrent(g_eglDisplay,g_eglSurface,g_eglSurface,g_eglContext))return 1;
 CreateBlit();g_config.renderWidth=640;g_config.renderHeight=360;
 for(unsigned level=0;level<=8;++level){nier::xenos::TextureFetch t;t.base=0x1000;t.width=t.height=512>>level;
  auto& target=g_resolved[TexKey(t)];CreateTarget(target,std::max(1u,t.width/2),std::max(1u,t.height/2),false);
  target.from3D=true;glClearColor(level/8.f,0.25f,0.5f,1);glClear(GL_COLOR_BUFFER_BIT);
 }
 GuestShader vs,ps;vs.key=123;ps.key=456;vs.ok=ps.ok=true;ps.pixel=true;ps.textureMask=2;vs.constMap={0};vs.inputs.resize(2);
 vs.source=R"(#version 100
precision highp float;uniform vec4 VC[1];attribute vec4 a0;attribute vec4 a1;varying vec4 v0;
void main(){gl_Position=a0;v0.xy=a1.xy;
vec4 va = VC[0].yyyx;
vec4 vb = VC[0].yyyx;
vec4 vr = max(va, vb);
v0.zw = vr.zw;
})";
 ps.source=R"(#version 100
precision mediump float;uniform sampler2D T1;uniform vec2 nx_size1;varying vec4 v0;
vec4 nx_sample1(vec2 uv){vec4 v=texture2D(T1,uv);return v;}
void main(){vec4 r[1];r[0] = v0;
// NIER_SET_LOD r[0].w
// NIER_LOD_SAMPLE 1
gl_FragColor=nx_sample1(r[0].xy);
})";
 Frame f;f.blob.resize(kStateSize);auto put=[&](unsigned off,uint32_t v){v=__builtin_bswap32(v);std::memcpy(f.blob.data()+off-kStateBegin,&v,4);};
 auto putf=[&](unsigned off,float v){uint32_t u;std::memcpy(&u,&v,4);put(off,u);};
 put(kSurfaceInfo,1280);put(kColorInfo,0);put(kBlendControl,0x10001);put(0x28DC,15);
 putf(kViewport,640);putf(kViewport+4,640);putf(kViewport+8,360);putf(kViewport+12,360);
 put(kFetchConstants+24,2);put(kFetchConstants+28,0x1000|6);put(kFetchConstants+32,511|(511<<13));put(kFetchConstants+36,0x688<<1);put(kFetchConstants+44,1<<9);
 const float positions[]={-1,-1,0,1, 1,-1,0,1, -1,1,0,1, 1,1,0,1};
 const float uv[]={0,0,0,0, 1,0,0,0, 0,1,0,0, 1,1,0,0};const uint16_t indices[]={0,1,2,2,1,3};
 Cmd c;c.kind=Kind::Draw;c.state=0;c.up=true;c.vs=&vs;c.ps=&ps;c.inputCount=2;c.vertexCount=4;c.indexCount=6;c.mode=GL_TRIANGLES;
 c.inputs[0]=Append(f,positions,sizeof positions);c.inputs[1]=Append(f,uv,sizeof uv);c.indices=Append(f,indices,sizeof indices);
 for(unsigned level=0;level<=8;++level){putf(kVertexConstants,float(level));ReplayDraw(f,c,{});uint8_t pixel[4];glReadPixels(320,180,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
  int expected=int(std::lround(level*255.0/8));if(std::abs(int(pixel[0])-expected)>1||std::abs(int(pixel[1])-64)>1||std::abs(int(pixel[2])-128)>1||pixel[3]!=255){std::fprintf(stderr,"FAIL LOD%u rgba %u %u %u %u expected red%d\n",level,pixel[0],pixel[1],pixel[2],pixel[3],expected);return 2;}
 }
 GLenum error=glGetError();std::printf("%s 9 distinct resolved levels through production sampler binding, GL%x\n",error?"FAIL":"PASS",error);return error?3:0;
}
