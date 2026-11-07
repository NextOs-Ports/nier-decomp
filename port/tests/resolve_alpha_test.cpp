// Exercise the production blit, keep-copy and whole-FBO alias with 256 alpha
// values. This TU includes renderer internals rather than duplicating GLSL.
#ifndef NIER_RENDERER_TEST_SOURCE
#define NIER_RENDERER_TEST_SOURCE "../render/renderer.cpp"
#endif
#include NIER_RENDERER_TEST_SOURCE
int main() {
  using namespace nier::render;
  if(!CreatePbuffer() || !eglMakeCurrent(g_eglDisplay,g_eglSurface,g_eglSurface,g_eglContext))return 1;
  CreateBlit();
  GLint linked=0;glGetProgramiv(g_blitProgram,GL_LINK_STATUS,&linked);if(!linked)return 4;
  g_config.renderWidth=1280;g_config.renderHeight=720;
  CreateTarget(g_targets[256],256,8,false);
  std::vector<uint8_t> source(256*8*4),out(source.size());
  for(unsigned i=0;i<256*8;++i){source[i*4]=i&255;source[i*4+1]=(i*7)&255;source[i*4+2]=17;source[i*4+3]=i&255;}
  glBindTexture(GL_TEXTURE_2D,g_targets[256].texture);
  glTexSubImage2D(GL_TEXTURE_2D,0,0,0,256,8,GL_RGBA,GL_UNSIGNED_BYTE,source.data());InvalidateGl();
  Frame frame;frame.blob.resize(nier::d3d::kStateSize+24);
  auto put=[&](unsigned off,uint32_t v){v=__builtin_bswap32(v);std::memcpy(frame.blob.data()+off,&v,4);};
  put(nier::d3d::kSurfaceInfo-nier::d3d::kStateBegin,256);
  const auto fetch=nier::d3d::kStateSize;
  put(fetch,2);put(fetch+4,0x3000|nier::xenos::k8888);put(fetch+8,255|(7<<13));put(fetch+12,0x688<<1);put(fetch+20,1<<9);
  Cmd cmd;cmd.state=0;cmd.fetch=fetch;cmd.kind=Kind::Resolve;
  uint64_t keepKey=TexKey(nier::xenos::DecodeFetch(frame.blob.data()+fetch));g_frameSampled.insert(keepKey);
  glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_FALSE);glEnable(GL_BLEND);glBlendFunc(GL_ZERO,GL_ZERO);
  ReplayResolve(frame,cmd,false,true);
  glBindFramebuffer(GL_FRAMEBUFFER,g_resolved[keepKey].fbo);
  glReadPixels(0,0,256,8,GL_RGBA,GL_UNSIGNED_BYTE,out.data());
  if(out!=source){std::fprintf(stderr,"FAIL keep blit alpha first=%u middle=%u last=%u\n",out[3],out[515],out[1023]);return 5;}
  put(fetch+4,0x4000|nier::xenos::k8888);
  uint64_t aliasKey=TexKey(nier::xenos::DecodeFetch(frame.blob.data()+fetch));
  ReplayResolve(frame,cmd,false,false);
  glBindFramebuffer(GL_FRAMEBUFFER,g_resolved[aliasKey].fbo);
  glReadPixels(0,0,256,8,GL_RGBA,GL_UNSIGNED_BYTE,out.data());if(out!=source)return 6;
  // Presentation variant uses the production shader with flip=1 and opaque alpha.
  glBindFramebuffer(GL_FRAMEBUFFER,g_targets[256].fbo);
  glBindTexture(GL_TEXTURE_2D,g_resolved[aliasKey].texture);
  glUniform1f(g_blitFlip,1.0f);glDrawArrays(GL_TRIANGLE_STRIP,0,4);
  glReadPixels(0,0,256,8,GL_RGBA,GL_UNSIGNED_BYTE,out.data());
  for(unsigned i=0;i<256*8;++i){source[i*4+3]=255;if(std::memcmp(&out[4*i],&source[4*i],4))return 7;}
  for(unsigned size:{1u,2u,4u}) {
    CreateTarget(g_targets[256],size,size,false);
    std::vector<uint8_t> tiny(size*size*4),actual(tiny.size());
    for(unsigned i=0;i<size*size;++i){tiny[4*i]=17;tiny[4*i+1]=93;tiny[4*i+2]=147;tiny[4*i+3]=13+i*7;}
    glBindTexture(GL_TEXTURE_2D,g_targets[256].texture);
    glTexSubImage2D(GL_TEXTURE_2D,0,0,0,size,size,GL_RGBA,GL_UNSIGNED_BYTE,tiny.data());InvalidateGl();
    put(fetch+4,(0x8000+size*0x1000)|nier::xenos::k8888);put(fetch+8,(size-1)|((size-1)<<13));
    auto key=TexKey(nier::xenos::DecodeFetch(frame.blob.data()+fetch));g_frameSampled.insert(key);
    ReplayResolve(frame,cmd,false,true);
    if(!g_resolved[key].fbo)return 9;
    glBindFramebuffer(GL_FRAMEBUFFER,g_resolved[key].fbo);glReadPixels(0,0,size,size,GL_RGBA,GL_UNSIGNED_BYTE,actual.data());
    if(tiny!=actual)return 10;
    auto putf=[&](unsigned off,float f){uint32_t bits;std::memcpy(&bits,&f,4);put(off-nier::d3d::kStateBegin,bits);};
    putf(nier::d3d::kViewport,size*0.5f);putf(nier::d3d::kViewport+4,size*0.5f);
    putf(nier::d3d::kViewport+8,size*0.5f);putf(nier::d3d::kViewport+12,size*0.5f);
    GuestShader vs,ps;Cmd draw;draw.kind=Kind::Draw;draw.state=0;draw.vs=&vs;draw.ps=&ps;
    frame.cmds={draw,cmd};g_frames.store(1);PlanDirectPasses(frame);
    if(!g_resolveDirect[1]||g_redirects[0].skip||g_redirects[0].w!=int(size)||g_redirects[0].h!=int(size))return 11;
  }
  GLenum error=glGetError();
  eglMakeCurrent(g_eglDisplay,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
  eglDestroyContext(g_eglDisplay,g_eglContext);eglDestroySurface(g_eglDisplay,g_eglSurface);eglTerminate(g_eglDisplay);
  std::printf("%s 256 RGBA values: keep blit, FBO alias, opaque presentation, 1x1/2x2/4x4 resolve/planning; GL 0x%x\n",error?"FAIL":"PASS",error);
  return error?8:0;
}
