// Read-only GLES2 comparison fixture. Actual game shader text is supplied
// privately at runtime; no generated game shaders are embedded in this source.
#include "../render/renderer.cpp"
#include <fstream>
#include <sstream>
static std::string ReadText(const char* path){std::ifstream f(path);std::ostringstream s;s<<f.rdbuf();return s.str();}
int main(int argc,char**argv){
 using namespace nier::render;using namespace nier::d3d;
 if(argc<3){std::fprintf(stderr,"usage: tarjas_driver_probe actual.vert actual.frag [reference]\n");return 1;}
 if(argc>3)setenv("NIER_BUFFER_REFERENCE","1",1);
 if(!CreatePbuffer()||!eglMakeCurrent(g_eglDisplay,g_eglSurface,g_eglSurface,g_eglContext)){std::fprintf(stderr,"EGL failed %x\n",eglGetError());return 2;}
 std::printf("PROVIDER %s / %s / %s\n",glGetString(GL_VENDOR),glGetString(GL_RENDERER),glGetString(GL_VERSION));
 if(!std::strstr(reinterpret_cast<const char*>(glGetString(GL_VERSION)),"OpenGL ES 2.")){std::fprintf(stderr,"REJECTED: this probe requires a real GLES2 provider\n");return 5;}
 CreateBlit();InitStreamBuffers();g_config.renderWidth=640;g_config.renderHeight=360;
 std::vector<uint8_t> memory(0x8000);g_mem.physicalBase=memory.data();memory[0x1083]=255;
 nier::xenos::TextureFetch fetch;fetch.base=0x1000;fetch.width=fetch.height=1;fetch.pitch=32;fetch.dimension=1;fetch.format=6;fetch.endian=2;fetch.swizzle=0x53;fetch.tiled=fetch.packedMips=true;
 std::vector<uint8_t> rgba;nier::xenos::DecodeLevel0(Physical,fetch,rgba);std::printf("DECODED texture %u %u %u %u\n",rgba[0],rgba[1],rgba[2],rgba[3]);
 uint8_t packed[]={0x4c,255,255,255};VertexInput colorInput;colorInput.format=6;colorInput.stride=1;float color[4];buffers::ConvertInput(colorInput,packed,2,0,1,color);
 std::printf("DECODED color %g %g %g %g\n",color[0],color[1],color[2],color[3]);
 GuestShader vs,ps;vs.key=0xe51e77673993e73full;ps.key=0x1bed176693a4988full;vs.ok=ps.ok=true;ps.pixel=true;vs.inputs.resize(3);vs.constMap={0};vs.source=ReadText(argv[1]);ps.source=ReadText(argv[2]);ps.textureMask=1;
 if(vs.source.empty()||ps.source.empty())return 3;
 const auto actual=ps.source;
 Frame f;f.blob.resize(kStateSize);auto put=[&](unsigned off,uint32_t v){v=__builtin_bswap32(v);std::memcpy(f.blob.data()+off-kStateBegin,&v,4);};auto putf=[&](unsigned off,float v){uint32_t u;std::memcpy(&u,&v,4);put(off,u);};
 put(kSurfaceInfo,1280);put(kColorInfo,0);put(kBlendControl,0x10706);put(0x28DC,15);put(kDepthControl,0);put(kModeControl,0);
 putf(kViewport,640);putf(kViewport+4,640);putf(kViewport+8,360);putf(kViewport+12,360);
 putf(kVertexConstants,1);putf(kVertexConstants+4,1);putf(kVertexConstants+8,0);putf(kVertexConstants+12,0);
 put(kFetchConstants,0x80404802);put(kFetchConstants+4,0x1086);put(kFetchConstants+8,0);put(kFetchConstants+12,0xa6);put(kFetchConstants+16,3);put(kFetchConstants+20,0xa01);
 const float pos[]={-1,-1,0,1, 1,-1,0,1, -1,1,0,1, 1,1,0,1};const float uv[]={0,0,0,1, 1,0,0,1, 0,1,0,1, 1,1,0,1};float colors[16];for(unsigned i=0;i<4;++i)std::memcpy(colors+4*i,color,16);const uint16_t indices[]={0,1,2,2,1,3};
 Cmd c;c.kind=Kind::Draw;c.state=0;c.up=true;c.vs=&vs;c.ps=&ps;c.inputCount=3;c.vertexCount=4;c.indexCount=6;c.mode=GL_TRIANGLES;
 c.inputs[0]=Append(f,pos,sizeof pos);c.inputs[1]=Append(f,uv,sizeof uv);c.inputs[2]=Append(f,colors,sizeof colors);c.indices=Append(f,indices,sizeof indices);
 unsigned failures=0;
 for(unsigned mode=0;mode<4;++mode){
  ps.key=0x100+mode;ps.source=actual;ps.textureMask=1;
  if(mode==1){ActiveUnit(0);auto id=TextureFor(fetch,false);glBindTexture(GL_TEXTURE_2D,id);uint8_t black[]={0,0,0,255};glTexSubImage2D(GL_TEXTURE_2D,0,0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,black);g_textures[TexKey(fetch)^(uint64_t(6)<<58)].nextCheck=~0ull;InvalidateGl();}
  if(mode>=2){ps.textureMask=0;ps.source=mode==2?"#version 100\nprecision mediump float;varying vec4 v1;void main(){gl_FragColor=vec4(0.0,0.0,0.0,v1.a);}":"#version 100\nprecision mediump float;void main(){gl_FragColor=vec4(0.0,0.0,0.0,76.0/255.0);}";}
  f.cmds={c};PrepareFrameStreams(f);auto& draw=f.cmds[0];const uint8_t* state=f.blob.data();auto& target=CurrentTarget(state);
  glColorMask(1,1,1,1);glClearColor(80.f/255,120.f/255,160.f/255,0);glClear(GL_COLOR_BUFFER_BIT);g_gl.fixedValid=false;
  uint8_t before[4],after[4];glReadPixels(320,180,1,1,GL_RGBA,GL_UNSIGNED_BYTE,before);
  ReplayDraw(f,draw,{});glReadPixels(320,180,1,1,GL_RGBA,GL_UNSIGNED_BYTE,after);
  GLint src=0,dst=0,eq=0;glGetIntegerv(GL_BLEND_SRC_RGB,&src);glGetIntegerv(GL_BLEND_DST_RGB,&dst);glGetIntegerv(GL_BLEND_EQUATION_RGB,&eq);
  bool ok=true;for(unsigned k=0;k<3;++k)ok&=std::abs(int(after[k])-int(std::lround(before[k]*(179.0/255))))<=1;ok&=std::abs(int(after[3])-76)<=1;GLenum error=glGetError();ok&=!error;failures+=!ok;
  std::printf("CASE%u %s %s before %u %u %u %u after %u %u %u %u blend %x/%x/%x GL%x\n",mode,ReferenceBuffers()?"reference":"stream",ok?"PASS":"FAIL",before[0],before[1],before[2],before[3],after[0],after[1],after[2],after[3],src,dst,eq,error);++g_frames;
 }
 std::printf("RESULT failures=%u\n",failures);return failures?4:0;
}
