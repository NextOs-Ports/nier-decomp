// Tests actual GLES2 attribute fetch plus immutable cache invalidation/eviction.
#include "../render/vertex_cache.h"
#include <EGL/egl.h>
#include <cassert>
#include <cstdio>
#include <random>
using namespace nier::render;
using namespace nier::render::buffers;
GLuint Shader(GLenum type, const char* source) {
  GLuint id = glCreateShader(type); glShaderSource(id, 1, &source, nullptr); glCompileShader(id);
  GLint ok; glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
  if (!ok) { char log[2048]; glGetShaderInfoLog(id, sizeof log, nullptr, log); std::fprintf(stderr,"%s\n",log); std::abort(); }
  return id;
}
int main() {
  // Same-frame repetition must NOT promote; two distinct frames may promote.
  Cache cache(1 << 20);
  std::vector<uint8_t> source(1024 * 16, 0);
  VertexInput in{}; in.format = 38; in.stride = 4;
  auto old = cache.Vertex(in, source.data(), 0, 1024, false);
  auto same = cache.Vertex(in, source.data(), 0, 1024, false);
  assert(old == same && cache.hits == 1 && !old->cacheable.load());
  assert(cache.Vertex(in, source.data(), 0, 1024, false, 1) == old && old->cacheable.load());
  auto hash = Sample(source.data(), 16, 1024, 16);
  source[13] = 1;
  assert(Sample(source.data(), 16, 1024, 16) != hash);
  auto changed = cache.Vertex(in, source.data(), 0, 1024, false, 2);
  assert(changed != old && old->raw[13] == 0 && changed->raw[13] == 1 && !changed->cacheable.load());
  in.expAdjust = 2;
  assert(cache.Vertex(in, source.data(), 0, 1024, false) != changed);
  Cache tiny(256);
  for (unsigned i = 0; i < 64; ++i) { source[i * 16] = uint8_t(i); tiny.Vertex(in, source.data() + i * 16, 0, 1, false); }
  assert(tiny.resident_bytes() <= 256 && old->raw[13] == 0);
  uint16_t indices[] = {0, 1, 2};
  auto ib = cache.Indices(0x1234, indices, 3);
  assert(cache.Indices(0x1234, indices, 3) == ib);
  indices[1] = 2;
  assert(cache.Indices(0x1234, indices, 3) != ib && ib->bytes[2] == 1);

  EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  assert(eglInitialize(display, nullptr, nullptr)); eglBindAPI(EGL_OPENGL_ES_API);
  EGLint cfgAttrs[] = {EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,
    EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
  EGLConfig cfg; EGLint n; assert(eglChooseConfig(display,cfgAttrs,&cfg,1,&n) && n);
  EGLint size[] = {EGL_WIDTH,32,EGL_HEIGHT,32,EGL_NONE};
  auto surface = eglCreatePbufferSurface(display,cfg,size);
  EGLint ctxAttrs[] = {EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
  auto context = eglCreateContext(display,cfg,EGL_NO_CONTEXT,ctxAttrs);
  assert(eglMakeCurrent(display,surface,surface,context));
  const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
  if (!version || std::strncmp(version, "OpenGL ES 2.", 12)) {
    std::fprintf(stderr, "Rejected non-GLES2 context: %s\n", version ? version : "null");
    eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
    eglDestroyContext(display,context);eglDestroySurface(display,surface);eglTerminate(display);return 2;
  }
  std::string ext = std::string(" ") + reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS)) + " ";
  bool half = ext.find(" GL_OES_vertex_half_float ") != std::string::npos;
  std::printf("provider=%s version=%s half=%d\n",glGetString(GL_RENDERER),glGetString(GL_VERSION),half);
  GLuint vs = Shader(GL_VERTEX_SHADER,"#version 100\nprecision mediump float; attribute vec2 pos; attribute vec4 a; varying mediump vec4 v; uniform mediump float scale; void main(){gl_Position=vec4(pos,0.,1.);gl_PointSize=1.;v=clamp(a*scale+0.5,0.,1.);}");
  GLuint fs = Shader(GL_FRAGMENT_SHADER,"#version 100\nprecision mediump float; varying mediump vec4 v; void main(){gl_FragColor=v;}");
  GLuint program = glCreateProgram(); glAttachShader(program,vs);glAttachShader(program,fs);
  glBindAttribLocation(program,0,"pos"); glBindAttribLocation(program,1,"a"); glLinkProgram(program);
  GLint linked; glGetProgramiv(program,GL_LINK_STATUS,&linked); assert(linked); glUseProgram(program);
  glViewport(0,0,32,32); glDisable(GL_DITHER); glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST);
  GLuint vbo[3], ebo; glGenBuffers(3,vbo); glGenBuffers(1,&ebo);
  std::vector<float> positions(2048);
  std::vector<uint16_t> sequence(1024);
  for (int i=0;i<1024;++i) {positions[i*2]=float(i%32+0.5f)/16-1;positions[i*2+1]=float(i/32+0.5f)/16-1;sequence[i]=uint16_t(i);}
  glBindBuffer(GL_ARRAY_BUFFER,vbo[0]); glBufferData(GL_ARRAY_BUFFER,positions.size()*4,positions.data(),GL_STATIC_DRAW);
  glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,8,nullptr);glEnableVertexAttribArray(0);glEnableVertexAttribArray(1);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ebo);glBufferData(GL_ELEMENT_ARRAY_BUFFER,sequence.size()*2,sequence.data(),GL_STATIC_DRAW);
  std::vector<float> reference(4096);
  std::vector<uint8_t> expected(4096), actual(4096);
  std::mt19937 rng(0x4e494552);
  int cases=0; uint64_t nativeBytes=0, referenceBytes=0;
  for (unsigned format : {6u,7u,16u,17u,25u,26u,31u,32u,33u,34u,35u,36u,37u,38u,57u})
  for (unsigned endian=0;endian<4;++endian)
  for (unsigned flags=0;flags<4;++flags)
  for (int exponent : {0,2}) {
    in={};in.format=format;in.stride=5;in.offset=1;in.isSigned=flags&1;in.isInteger=flags&2;in.expAdjust=exponent;
    source.resize(1024*20+16);
    for(size_t i=0;i<source.size();i+=4) {
      uint32_t word=rng();
      if(format>=36) {float f=float(int(word%65536)-32768)/256;std::memcpy(&word,&f,4);}
      if(format==31 || format==32) word &= 0xfbfffbffu; // finite halves, including subnormal/signed zero
      word=GpuWord(reinterpret_cast<uint8_t*>(&word),endian);std::memcpy(source.data()+i,&word,4);
    }
    auto packed=cache.Vertex(in,source.data(),endian,1024,half);
    assert(packed && ConvertInput(in,source.data(),endian,0,1024,reference.data()));
    glUniform1f(glGetUniformLocation(program,"scale"),in.isInteger ? 0.0001f : 0.25f);
    for (int pass=0;pass<2;++pass) {
      glBindBuffer(GL_ARRAY_BUFFER,vbo[pass+1]);
      auto layout=pass ? packed->layout : Layout{};
      glBufferData(GL_ARRAY_BUFFER,pass ? packed->bytes.size() : reference.size()*4,pass ? static_cast<const void*>(packed->bytes.data()) : reference.data(),GL_STATIC_DRAW);
      glVertexAttribPointer(1,layout.components,layout.type,layout.normalized,layout.bytes,nullptr);
      glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);glDrawElements(GL_POINTS,1024,GL_UNSIGNED_SHORT,nullptr);
      glReadPixels(0,0,32,32,GL_RGBA,GL_UNSIGNED_BYTE,(pass ? actual : expected).data());
      assert(glGetError()==GL_NO_ERROR);
    }
    if(expected!=actual) {
      size_t diffs=0;for(size_t i=0;i<actual.size();++i) diffs+=actual[i]!=expected[i];
      std::fprintf(stderr,"FAIL fmt=%u endian=%u flags=%u exp=%d type=%x diffbytes=%zu\n",format,endian,flags,exponent,packed->layout.type,diffs);return 1;
    }
    ++cases;nativeBytes+=packed->bytes.size();referenceBytes+=reference.size()*4;
  }
  std::printf("PASS %d GLES2 images pixel-exact (%d vertex fetches); sampled-mutation/promotion/descriptor/eviction/lifetime/IBO passed; bytes=%llu reference=%llu\n",cases,cases*1024,(unsigned long long)nativeBytes,(unsigned long long)referenceBytes);
  eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
  eglDestroyContext(display,context);eglDestroySurface(display,surface);eglTerminate(display);
}
