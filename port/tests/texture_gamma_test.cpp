#include "../render/texture_gamma.h"
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <cstdio>
#include <cmath>
#include <vector>
using namespace nier::render;
int main(){
 uint8_t fetch[24]{};auto put=[&](unsigned n,uint32_t v){for(unsigned i=0;i<4;++i)fetch[4*n+i]=v>>(24-8*i);};
 put(0,63<<2);put(3,0x688<<1);if(TextureGammaChannels(fetch,1)!=7)return 1;
 put(3,(3|(4<<3)|(5<<6)|(0<<9))<<1);if(TextureGammaChannels(fetch,1)!=8)return 2;
 EGLDisplay d=eglGetDisplay(EGL_DEFAULT_DISPLAY);if(!eglInitialize(d,nullptr,nullptr))return 3;eglBindAPI(EGL_OPENGL_ES_API);
 EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE},n;EGLConfig cfg;
 eglChooseConfig(d,attrs,&cfg,1,&n);EGLint size[]={EGL_WIDTH,256,EGL_HEIGHT,1,EGL_NONE},ca[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
 auto surf=eglCreatePbufferSurface(d,cfg,size);auto ctx=eglCreateContext(d,cfg,EGL_NO_CONTEXT,ca);if(!eglMakeCurrent(d,surf,surf,ctx))return 4;
 auto compile=[](GLenum type,const std::string& s){GLuint sh=glCreateShader(type);const char* p=s.c_str();glShaderSource(sh,1,&p,nullptr);glCompileShader(sh);GLint ok;glGetShaderiv(sh,GL_COMPILE_STATUS,&ok);if(!ok){char b[2048];glGetShaderInfoLog(sh,sizeof b,nullptr,b);std::puts(b);return GLuint(0);}return sh;};
 auto vs=compile(GL_VERTEX_SHADER,"attribute vec2 p;varying vec2 uv;void main(){uv=p*0.5+0.5;gl_Position=vec4(p,0,1);}");
 std::string base="precision mediump float;uniform sampler2D T0;varying vec2 uv;vec4 nx_sample0(vec2 uv){vec4 v=texture2D(T0,uv);return v;}void main(){gl_FragColor=nx_sample0(uv);}";
 GLuint tex;glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
 const unsigned input[]={0,32,64,96,128,192,255},numerator[]={0,32,64,128,257,516,1023};std::vector<uint8_t> pixels(1024),out(1024);
 for(unsigned i=0;i<256;++i){for(unsigned c=0;c<3;++c)pixels[4*i+c]=input[i%7];pixels[4*i+3]=137;}
 glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,256,1,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
 float quad[]={-1,-1,1,-1,-1,1,1,1};glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,quad);glEnableVertexAttribArray(0);glViewport(0,0,256,1);
 for(uint64_t mask:{0ull,7ull}){
  auto fs=compile(GL_FRAGMENT_SHADER,TextureGammaVariant(base,1,mask));if(!fs)return 5;auto pr=glCreateProgram();glAttachShader(pr,vs);glAttachShader(pr,fs);glBindAttribLocation(pr,0,"p");glLinkProgram(pr);GLint ok;glGetProgramiv(pr,GL_LINK_STATUS,&ok);if(!ok)return 6;glUseProgram(pr);glDrawArrays(GL_TRIANGLE_STRIP,0,4);glReadPixels(0,0,256,1,GL_RGBA,GL_UNSIGNED_BYTE,out.data());
  for(unsigned i=0;i<256;++i){int expected=mask?int(std::lround(numerator[i%7]*255.0/1023.0)):input[i%7];for(unsigned c=0;c<3;++c)if(std::abs(int(out[4*i+c])-expected)>1)return 7;if(out[4*i+3]!=137)return 8;}
 }
 std::printf("PASS gamma fetch/swizzle, 7 independent PWL anchors, 256 GPU pixels gamma on/off, alpha preserved; GL%x\n",glGetError());
}
