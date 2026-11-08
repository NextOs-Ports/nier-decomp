// Validate production HDR8 upload/sampling against the encoded-space curve.
// CPU test separately reports approximation loss against original half data.
// The raw texture is an external private fixture, never embedded in the repo.
#include "../render/renderer.cpp"
#include <fstream>
int main(int argc,char**argv){
 using namespace nier::render;using namespace nier::d3d;
 if(argc!=2)return 1;setenv("NIER_BUFFER_REFERENCE","1",1);
 std::ifstream file(argv[1],std::ios::binary);std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),{});if(bytes.size()!=256*256*8)return 2;
 std::vector<uint8_t> memory(bytes.size()+8192);std::memcpy(memory.data()+4096,bytes.data(),bytes.size());g_mem.physicalBase=memory.data();
 nier::xenos::TextureFetch t;t.base=0xE0000000;t.width=t.height=t.pitch=256;t.format=29;t.dimension=1;t.endian=1;t.tiled=t.packedMips=true;t.swizzle=0x53;
 std::vector<float> original;if(!ReadHdrHalf(Physical,t,original))return 3;Hdr8Image packed;if(!PackHdr8(256,256,original,packed))return 4;
 if(!CreatePbuffer()||!eglMakeCurrent(g_eglDisplay,g_eglSurface,g_eglSurface,g_eglContext))return 5;
 std::printf("PROVIDER %s / %s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));if(!std::strstr(reinterpret_cast<const char*>(glGetString(GL_VERSION)),"OpenGL ES 2."))return 6;
 CreateBlit();g_config.renderWidth=640;g_config.renderHeight=360;
 GuestShader vs,ps;vs.key=999;ps.key=1000;vs.ok=ps.ok=true;ps.pixel=true;vs.inputs.resize(2);ps.textureMask=1;ps.constMap={0,1};
 vs.source="#version 100\nprecision highp float;attribute vec4 a0;attribute vec4 a1;varying vec4 v0;void main(){gl_Position=a0;v0=a1;}";
 ps.source="#version 100\nprecision mediump float;precision mediump int;precision mediump sampler2D;uniform vec4 PC[2];uniform sampler2D T0;varying vec4 v0;vec4 nx_sample0(vec2 uv){vec4 v=texture2D(T0,uv);return v;}void main(){vec4 v=nx_sample0(v0.xy);bvec4 good=lessThanEqual(abs(v-PC[0]),PC[1]);gl_FragColor=all(good)?vec4(0,1,0,1):vec4(1,0,0,1);}";
 Frame f;f.blob.resize(kStateSize);auto put=[&](unsigned off,uint32_t v){v=__builtin_bswap32(v);std::memcpy(f.blob.data()+off-kStateBegin,&v,4);};auto putf=[&](unsigned off,float v){uint32_t u;std::memcpy(&u,&v,4);put(off,u);};
 put(kSurfaceInfo,1280);put(kColorInfo,0);put(kBlendControl,0x10001);put(0x28DC,15);putf(kViewport,640);putf(kViewport+4,640);putf(kViewport+8,360);putf(kViewport+12,360);
 put(kFetchConstants,0x82004956);put(kFetchConstants+4,t.base|0x5D);put(kFetchConstants+8,255|(255<<13));put(kFetchConstants+12,0x2800A7);put(kFetchConstants+20,0xA01);
 const float positions[]={-1,-1,0,1, 1,-1,0,1, -1,1,0,1, 1,1,0,1};float uv[16]{};const uint16_t indices[]={0,1,2,2,1,3};
 Cmd c;c.kind=Kind::Draw;c.state=0;c.up=true;c.vs=&vs;c.ps=&ps;c.inputCount=2;c.vertexCount=4;c.indexCount=6;c.mode=GL_TRIANGLES;
 c.inputs[0]=Append(f,positions,sizeof positions);c.inputs[1]=Append(f,uv,sizeof uv);c.indices=Append(f,indices,sizeof indices);
 std::vector<unsigned> samples={0,1,17,500,8192,32768,65535};
 for(unsigned channel=0;channel<3;++channel){unsigned max=0,small=0;float err=1e9;for(unsigned i=0;i<65536;++i){if(original[i*4+channel]>original[max*4+channel])max=i;float e=std::fabs(original[i*4+channel]-0.02f);if(e<err){err=e;small=i;}}samples.push_back(max);samples.push_back(small);}
 unsigned failed=0,checks=0;
 for(unsigned index:samples)for(unsigned filter:{0u,1u}){
  unsigned x=index%256,y=index/256;float u=(x+0.5f+filter*0.5f)/256,v=(y+0.5f)/256;for(unsigned i=0;i<4;++i){uv[4*i]=u;uv[4*i+1]=v;uv[4*i+2]=uv[4*i+3]=0;}std::memcpy(f.blob.data()+c.inputs[1],uv,sizeof uv);
  for(unsigned ch=0;ch<4;++ch){double encoded=packed.levels[0].pixels[index*4+ch]/255.0;if(filter)encoded=(encoded+packed.levels[0].pixels[(y*256+std::min(x+1,255u))*4+ch]/255.0)*0.5;float expected=float(encoded*encoded*packed.gain[ch]+packed.bias[ch]);float eps=float((filter?0.5/255.0:0.0)+encoded/1024.0);float tolerance=packed.gain[ch]*(2*float(encoded)*eps+eps*eps)+std::fabs(expected)*0.003f+0.0001f;putf(kPixelConstants+ch*4,expected);putf(kPixelConstants+16+ch*4,tolerance);}
  ReplayDraw(f,c,{});uint8_t pixel[4];glReadPixels(320,180,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);++checks;
  if(pixel[0]||pixel[1]!=255||pixel[3]!=255){++failed;std::fprintf(stderr,"FAIL pixel%u filter%u result%u/%u/%u/%u\n",index,filter,pixel[0],pixel[1],pixel[2],pixel[3]);}++g_frames;
 }
 auto error=glGetError();std::printf("%s %u HDR8 texel/native-bilinear comparisons; expected encoded-space curve, tolerance=native8bitfilter+mediump; GL%x\n",failed||error?"FAIL":"PASS",checks,error);return failed||error?7:0;
}
