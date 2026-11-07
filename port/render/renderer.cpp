// Renderizador GLES2 do NieR. Ver renderer.h e docs/RENDER.md.
//
// Thread do jogo (ganchos): copia o estado do device, traduz/acha os shaders (cache por hash), converte os
// streams de vértice usados pelo VS para float4 e os índices para lista de triângulos de 16 bits.
// Thread de render (única dona do contexto GL): programas, texturas (decodificação Xenos + mipmap em POT
// estática), estados fixos, desenho nos alvos internos (>= 640 de largura) e apresentação letterbox 16:9.
//
// Convenção de imagem: o VS traduzido inverte y, então cada alvo guarda a linha 0 do guest em y = 0 do GL
// (de cabeça para baixo no sentido GL). Viewport e Resolve usam o y do guest direto; a apresentação desvira.
#include "renderer.h"

#include "nier_d3d.h"
#include "xenos_texture.h"

#include <SDL3/SDL.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace nier::render {
namespace {
using Clock = std::chrono::steady_clock;
using namespace nier::d3d;

Config g_config;
GuestMemory g_mem;
std::atomic<bool> g_enabled{false};
SDL_Window* g_window = nullptr;
SDL_GLContext g_context = nullptr;
// Alternativa sem janela (PC): quando o SDL não tem GL (SDL estático do SDK host), EGL + pbuffer 1280x720.
EGLDisplay g_eglDisplay = EGL_NO_DISPLAY;
EGLSurface g_eglSurface = EGL_NO_SURFACE;
EGLContext g_eglContext = EGL_NO_CONTEXT;
bool UsingPbuffer() { return g_eglContext != EGL_NO_CONTEXT; }
bool CreatePbuffer() {
  g_eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (g_eglDisplay == EGL_NO_DISPLAY || !eglInitialize(g_eglDisplay, nullptr, nullptr)) return false;
  eglBindAPI(EGL_OPENGL_ES_API);
  const EGLint attrs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                          EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE};
  EGLConfig config; EGLint n = 0;
  if (!eglChooseConfig(g_eglDisplay, attrs, &config, 1, &n) || n < 1) return false;
  const EGLint size[] = {EGL_WIDTH, 1280, EGL_HEIGHT, 720, EGL_NONE};
  g_eglSurface = eglCreatePbufferSurface(g_eglDisplay, config, size);
  const EGLint ctx[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
  g_eglContext = eglCreateContext(g_eglDisplay, config, EGL_NO_CONTEXT, ctx);
  return g_eglSurface != EGL_NO_SURFACE && g_eglContext != EGL_NO_CONTEXT;
}
constexpr uint32_t kMaxInputs = 16;

// ---------------------------------------------------------------- memória guest
const uint8_t* Virtual(uint32_t address, uint32_t size) { return g_mem.virtualRead ? g_mem.virtualRead(address, size) : nullptr; }
const uint8_t* Physical(uint32_t address, uint32_t size) {
  address &= 0x1FFFFFFF;
  return g_mem.physicalBase && uint64_t(address) + size <= 0x20000000ull ? g_mem.physicalBase + address : nullptr;
}

// Palavra de 32 bits como a GPU a vê, com o endian da fetch constant (0 nenhum, 1 8in16, 2 8in32, 3 16in32).
inline uint32_t GpuWord(const uint8_t* p, uint32_t endian) {
  uint32_t w; std::memcpy(&w, p, 4);
  switch (endian) {
    case 1: return ((w & 0x00FF00FFu) << 8) | ((w >> 8) & 0x00FF00FFu);
    case 2: return __builtin_bswap32(w);
    case 3: return (w << 16) | (w >> 16);
    default: return w;
  }
}
float Half(uint16_t h) {
  uint32_t s = uint32_t(h >> 15) << 31, e = (h >> 10) & 31, m = h & 1023, f;
  if (!e) { if (!m) f = s; else { e = 127 - 14; while (!(m & 1024)) { m <<= 1; --e; } m &= 1023; f = s | e << 23 | m << 13; } }
  else if (e == 31) f = s | 0x7F800000u | m << 13;
  else f = s | (e + 112) << 23 | m << 13;
  float v; std::memcpy(&v, &f, 4); return v;
}
float Component(uint32_t raw, int bits, const VertexInput& in, float scale) {
  float v;
  if (in.isSigned) {
    int32_t x = int32_t(raw << (32 - bits)) >> (32 - bits);
    v = in.isInteger ? float(x) : std::max(float(x) / float((1u << (bits - 1)) - 1), -1.0f);
  } else {
    v = in.isInteger ? float(raw) : float(raw) / float((bits == 32 ? 0xFFFFFFFFu : (1u << bits) - 1));
  }
  return v * scale;
}
// Converte `count` vértices a partir de `first` para float4 (x, y, z, w; ausentes = 0,0,0,1).
bool ConvertInput(const VertexInput& in, const uint8_t* base, uint32_t endian, uint32_t first, uint32_t count, float* out) {
  float scale = in.expAdjust ? std::ldexp(1.0f, in.expAdjust) : 1.0f;
  for (uint32_t v = 0; v < count; ++v) {
    const uint8_t* p = base + size_t((first + v) * in.stride + in.offset) * 4;
    float* d = out + size_t(v) * 4;
    d[0] = d[1] = d[2] = 0; d[3] = 1;
    auto f32 = [&](int i) { uint32_t w = GpuWord(p + 4 * i, endian); float x; std::memcpy(&x, &w, 4); return x * scale; };
    switch (in.format) {
      case 6: { uint32_t w = GpuWord(p, endian); for (int c = 0; c < 4; ++c) d[c] = Component((w >> (8 * c)) & 0xFF, 8, in, scale); break; }
      case 7: { uint32_t w = GpuWord(p, endian); d[0] = Component(w & 1023, 10, in, scale); d[1] = Component((w >> 10) & 1023, 10, in, scale); d[2] = Component((w >> 20) & 1023, 10, in, scale); d[3] = Component(w >> 30, 2, in, scale); break; }
      case 16: { uint32_t w = GpuWord(p, endian); d[0] = Component(w & 2047, 11, in, scale); d[1] = Component((w >> 11) & 2047, 11, in, scale); d[2] = Component(w >> 22, 10, in, scale); break; }
      case 17: { uint32_t w = GpuWord(p, endian); d[0] = Component(w & 1023, 10, in, scale); d[1] = Component((w >> 10) & 2047, 11, in, scale); d[2] = Component(w >> 21, 11, in, scale); break; }
      case 25: { uint32_t w = GpuWord(p, endian); d[0] = Component(w & 0xFFFF, 16, in, scale); d[1] = Component(w >> 16, 16, in, scale); break; }
      case 26: { uint32_t a = GpuWord(p, endian), b = GpuWord(p + 4, endian); d[0] = Component(a & 0xFFFF, 16, in, scale); d[1] = Component(a >> 16, 16, in, scale); d[2] = Component(b & 0xFFFF, 16, in, scale); d[3] = Component(b >> 16, 16, in, scale); break; }
      case 31: { uint32_t w = GpuWord(p, endian); d[0] = Half(uint16_t(w)); d[1] = Half(uint16_t(w >> 16)); break; }
      case 32: { uint32_t a = GpuWord(p, endian), b = GpuWord(p + 4, endian); d[0] = Half(uint16_t(a)); d[1] = Half(uint16_t(a >> 16)); d[2] = Half(uint16_t(b)); d[3] = Half(uint16_t(b >> 16)); break; }
      case 33: d[0] = Component(GpuWord(p, endian), 32, in, scale); break;
      case 34: d[0] = Component(GpuWord(p, endian), 32, in, scale); d[1] = Component(GpuWord(p + 4, endian), 32, in, scale); break;
      case 35: for (int c = 0; c < 4; ++c) d[c] = Component(GpuWord(p + 4 * c, endian), 32, in, scale); break;
      case 36: d[0] = f32(0); break;
      case 37: d[0] = f32(0); d[1] = f32(1); break;
      case 38: for (int c = 0; c < 4; ++c) d[c] = f32(c); break;
      case 57: d[0] = f32(0); d[1] = f32(1); d[2] = f32(2); break;
      default: return false;
    }
  }
  return true;
}

// Primitivas do guest -> lista de triângulos (índices relativos ao 1º vértice convertido).
bool Triangulate(uint32_t primitive, const std::vector<uint32_t>& src, std::vector<uint16_t>& dst) {
  dst.clear();
  auto add = [&](uint32_t a, uint32_t b, uint32_t c) { if (a != b && b != c && a != c) { dst.push_back(uint16_t(a)); dst.push_back(uint16_t(b)); dst.push_back(uint16_t(c)); } };
  switch (primitive) {
    case 4: for (size_t i = 0; i + 2 < src.size(); i += 3) add(src[i], src[i + 1], src[i + 2]); return true;
    case 5: for (size_t i = 2; i < src.size(); ++i) add(src[0], src[i - 1], src[i]); return true;
    case 6: { size_t start = 0; for (size_t i = 0; i < src.size(); ++i) { if (src[i] == 0xFFFFFFFFu) { start = i + 1; continue; } if (i < start + 2) continue; if ((i - start) & 1) add(src[i - 1], src[i - 2], src[i]); else add(src[i - 2], src[i - 1], src[i]); } return true; }
    case 13: for (size_t i = 0; i + 3 < src.size(); i += 4) { add(src[i], src[i + 1], src[i + 2]); add(src[i], src[i + 2], src[i + 3]); } return true;
    case 8: for (size_t i = 0; i + 2 < src.size(); i += 3) { add(src[i], src[i + 1], src[i + 2]); /* rect list: 4º vértice implícito */ } return true;
    default: return false;
  }
}

// ---------------------------------------------------------------- fila de quadro
enum class Kind : uint8_t { Draw, Resolve, Skip };
struct Cmd {
  Kind kind;
  const GuestShader* vs = nullptr;
  const GuestShader* ps = nullptr;
  const char* skipReason = nullptr;
  size_t state = 0, indices = SIZE_MAX, fetch = SIZE_MAX;
  size_t inputs[kMaxInputs] = {};
  uint32_t inputCount = 0, vertexCount = 0, indexCount = 0;
  uint32_t flags = 0, argb = 0;
  int32_t rect[4] = {};
  bool hasRect = false;
};
struct Frame {
  std::vector<uint8_t> blob;
  std::vector<Cmd> cmds;
  uint32_t presentInterval = 1;
  void Clear() { blob.clear(); cmds.clear(); }
};
size_t Append(Frame& f, const void* p, size_t n) {
  size_t at = (f.blob.size() + 15) & ~size_t(15);
  f.blob.resize(at + n);
  if (p) std::memcpy(f.blob.data() + at, p, n);
  return at;
}

Frame g_building, g_inflight;
std::mutex g_queueMutex;
std::condition_variable g_queueCv;
bool g_hasInflight = false, g_quit = false;
std::thread g_thread;
std::atomic<uint32_t> g_interval{1};
std::atomic<uint64_t> g_frames{0}, g_draws{0}, g_skipped{0}, g_recordedBytes{0};
std::atomic<double> g_lastFrameMs{0};
std::vector<uint32_t> g_srcIndices;
std::vector<uint16_t> g_triIndices;

// Teto do quadro em construção: se o jogo desenha muito sem Present (ex.: tela de carregamento), o quadro
// pendente é descartado em vez de crescer sem limite (cada realocação copiaria centenas de MB).
std::atomic<uint64_t> g_droppedFrames{0};
void GuardBuilding() {
  if (g_building.blob.size() < (192u << 20) && g_building.cmds.size() < 100000) return;
  if (g_droppedFrames.fetch_add(1) < 5)
    std::fprintf(stderr, "[nier-gl] quadro sem Present descartado: %zu comandos, %zu MB\n", g_building.cmds.size(), g_building.blob.size() >> 20);
  g_building.Clear();
}

void RecordSkip(const char* reason) {
  Cmd c{Kind::Skip};
  c.skipReason = reason;
  g_building.cmds.push_back(c);
}

// ---------------------------------------------------------------- GL (thread de render)
struct Target { GLuint texture = 0, fbo = 0, depth = 0; int w = 0, h = 0; uint64_t clearedFrame = ~0ull; };
std::map<uint64_t, Target> g_targets;
std::map<uint64_t, Target> g_resolved;
uint64_t g_mainTarget = 0;
// Resolve preguiçoso (Mali: cada glCopyTexSubImage esvazia o tile e custa ms): a cópia só acontece se a
// textura destino for amostrada ou antes de o alvo de origem ser alterado; sem uso no quadro, é descartada.
struct PendingCopy { uint64_t src, dst; int x0, y0, w, h; };
std::vector<PendingCopy> g_pendingCopies;
std::set<uint64_t> g_frameSampled, g_everSampled;  // texturas amostradas no quadro (olhando à frente) e alguma vez
uint32_t g_copiesDone = 0, g_copiesSkipped = 0;
void DoCopy(const PendingCopy& p) {
  auto s = g_targets.find(p.src); auto d = g_resolved.find(p.dst);
  if (s == g_targets.end() || d == g_resolved.end()) return;
  GLint fbo = 0, tex = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex);
  glBindFramebuffer(GL_FRAMEBUFFER, s->second.fbo);
  glBindTexture(GL_TEXTURE_2D, d->second.texture);
  glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, p.x0, p.y0, std::min(p.w, s->second.w - p.x0), std::min(p.h, s->second.h - p.y0));
  glBindTexture(GL_TEXTURE_2D, GLuint(tex));
  glBindFramebuffer(GL_FRAMEBUFFER, GLuint(fbo));
  ++g_copiesDone;
}
void FlushCopiesFrom(uint64_t src) {  // antes de alterar o alvo de origem
  for (size_t i = 0; i < g_pendingCopies.size();) {
    if (g_pendingCopies[i].src == src) { DoCopy(g_pendingCopies[i]); g_pendingCopies.erase(g_pendingCopies.begin() + i); }
    else ++i;
  }
}
void FlushCopyTo(uint64_t dst) {  // antes de amostrar a textura destino
  for (size_t i = 0; i < g_pendingCopies.size(); ++i)
    if (g_pendingCopies[i].dst == dst) { DoCopy(g_pendingCopies[i]); g_pendingCopies.erase(g_pendingCopies.begin() + i); return; }
}
GLuint g_blitProgram = 0, g_blitBuffer = 0, g_vertexBuffer = 0, g_indexBuffer = 0;
std::map<std::string, uint32_t> g_skipReasons;
bool g_depth24 = false;

void Skip(const char* reason) {
  ++g_skipped;
  if (++g_skipReasons[reason] == 1) std::fprintf(stderr, "[nier-gl] desenho pulado: %s\n", reason);
}

GLuint CompileShader(GLenum type, const char* source, const char* name) {
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &source, nullptr);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[4096]; glGetShaderInfoLog(s, sizeof log, nullptr, log);
    std::fprintf(stderr, "[nier-gl] compilação %s falhou: %s\n", name, log);
    glDeleteShader(s); return 0;
  }
  return s;
}

void CreateBlit() {
  const char* vs = "attribute vec2 p; varying vec2 uv; void main(){ uv = vec2(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5); gl_Position = vec4(p, 0.0, 1.0); }";
  const char* fs = "precision mediump float; varying vec2 uv; uniform sampler2D t; void main(){ gl_FragColor = vec4(texture2D(t, uv).rgb, 1.0); }";
  GLuint v = CompileShader(GL_VERTEX_SHADER, vs, "blit"), f = CompileShader(GL_FRAGMENT_SHADER, fs, "blit");
  g_blitProgram = glCreateProgram();
  glAttachShader(g_blitProgram, v); glAttachShader(g_blitProgram, f);
  glBindAttribLocation(g_blitProgram, 0, "p");
  glLinkProgram(g_blitProgram);
  glDeleteShader(v); glDeleteShader(f);
  const float quad[] = {-1, -1, 1, -1, -1, 1, 1, 1};
  glGenBuffers(1, &g_blitBuffer);
  glBindBuffer(GL_ARRAY_BUFFER, g_blitBuffer);
  glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
  glGenBuffers(1, &g_vertexBuffer);
  glGenBuffers(1, &g_indexBuffer);
  const char* ext = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
  g_depth24 = ext && std::strstr(ext, "GL_OES_depth24");
}

void CreateTarget(Target& t, int w, int h, bool withDepth) {
  if (t.texture) glDeleteTextures(1, &t.texture);
  if (t.fbo) glDeleteFramebuffers(1, &t.fbo);
  if (t.depth) glDeleteRenderbuffers(1, &t.depth);
  t = Target{};
  t.w = w; t.h = h;
  glGenTextures(1, &t.texture);
  glBindTexture(GL_TEXTURE_2D, t.texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glGenFramebuffers(1, &t.fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.texture, 0);
  if (withDepth) {
    glGenRenderbuffers(1, &t.depth);
    glBindRenderbuffer(GL_RENDERBUFFER, t.depth);
    glRenderbufferStorage(GL_RENDERBUFFER, g_depth24 ? GL_DEPTH_COMPONENT24_OES : GL_DEPTH_COMPONENT16, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, t.depth);
  }
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) std::fprintf(stderr, "[nier-gl] FBO incompleto %dx%d\n", w, h);
  glDepthMask(GL_TRUE);
  glClearColor(0, 0, 0, 1);
  glClearDepthf(1.0f);
  glClear(GL_COLOR_BUFFER_BIT | (withDepth ? GL_DEPTH_BUFFER_BIT : 0));
}

float Scale() { return float(g_config.renderWidth) / 1280.0f; }

// Alvo de cor atual do guest (chave RB_SURFACE_INFO/RB_COLOR_INFO), na resolução interna.
Target& CurrentTarget(const uint8_t* state) {
  uint32_t surface = BE32(Field(state, kSurfaceInfo)), color = BE32(Field(state, kColorInfo));
  uint64_t key = uint64_t(color & 0xF0FFF) << 16 | (surface & 0x3FFF);
  uint32_t guestPitch = std::max<uint32_t>(surface & 0x3FFF, 1);
  int w = std::max(1, int(guestPitch * Scale() + 0.5f)), h = std::max(1, int(720 * Scale() + 0.5f));
  if (!g_pendingCopies.empty()) FlushCopiesFrom(key);
  Target& t = g_targets[key];
  if (t.w != w || t.h != h) CreateTarget(t, w, h, true);
  if ((key & 0x3FFF) > (g_mainTarget & 0x3FFF)) g_mainTarget = key;
  glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
  // Profundidade: o ClearF do 9328 ainda não foi localizado; limpa na 1ª vez do alvo em cada quadro.
  if (t.clearedFrame != g_frames.load()) {
    t.clearedFrame = g_frames.load();
    glDisable(GL_SCISSOR_TEST);
    glDepthMask(GL_TRUE);
    glClearDepthf(1.0f);
    glClear(GL_DEPTH_BUFFER_BIT);
  }
  return t;
}

// ---- texturas
struct Texture { GLuint id = 0; uint64_t signature = 0; uint32_t uploads = 0; uint64_t checkedFrame = ~0ull; GLenum glFormat = 0; };
std::unordered_map<uint64_t, Texture> g_textures;
std::vector<uint8_t> g_pixels;
GLuint g_white = 0;

// Conteúdo inteiro até 2 MB (atlas de fonte e texturas escritas pela CPU mudam em poucos bytes);
// acima disso, 64 amostras. Pendente para a Mali: trocar por write watch da memória física (ReXGlue).
uint64_t Signature(uint32_t base, uint32_t span) {
  uint64_t h = 1469598103934665603ull ^ span;
  if (span <= (2u << 20)) {
    if (const uint8_t* p = Physical(base, span)) {
      size_t words = span / 8;
      for (size_t i = 0; i < words; ++i) { uint64_t w; std::memcpy(&w, p + 8 * i, 8); h = (h ^ w) * 0x100000001B3ull; h ^= h >> 29; }
      return h;
    }
  }
  for (uint32_t i = 0; i < 64; ++i) {
    uint32_t off = span > 8 ? uint32_t(uint64_t(span - 8) * i / 63) : 0;
    uint64_t w = 0;
    if (const uint8_t* p = Physical(base + off, 8)) std::memcpy(&w, p, 8);
    h = (h ^ w) * 1099511628211ull;
  }
  return h;
}

GLuint White() {
  if (!g_white) {
    uint32_t px = 0xFFFFFFFF;
    glGenTextures(1, &g_white);
    glBindTexture(GL_TEXTURE_2D, g_white);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  }
  return g_white;
}

double g_pResolve = 0; uint32_t g_resolves = 0, g_resolveCopies = 0;
double g_pTarget = 0, g_pState = 0, g_pBuffers = 0, g_pDraw = 0, g_pFinish = 0;
bool GlProfile() { static bool on = std::getenv("NIER_GL_PROFILE") != nullptr; return on; }
double Ms(Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); }
double g_textureMs = 0, g_sigMs = 0, g_decodeMs = 0, g_uploadMs = 0, g_replayMs = 0, g_swapMs = 0;
std::atomic<uint64_t> g_gameWaitNs{0};  // tempo da thread de render em decodificação/envio de texturas (por janela de log)
GLuint TextureForImpl(const xenos::TextureFetch& t);
GLuint TextureFor(const xenos::TextureFetch& t) {
  auto t0 = Clock::now();
  GLuint id = TextureForImpl(t);
  g_textureMs += std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
  return id;
}
GLuint TextureForImpl(const xenos::TextureFetch& t) {
  uint64_t key = uint64_t(t.base) << 32 | uint64_t(t.width) << 16 | t.height;
  if (auto r = g_resolved.find(key); r != g_resolved.end()) {
    g_everSampled.insert(key);
    if (!g_pendingCopies.empty()) FlushCopyTo(key);
    return r->second.texture;
  }
  if (!xenos::Supported(t.format) || t.dimension != 1) { Skip("formato/dimensão de textura não suportado (branco)"); return White(); }
  Texture& tex = g_textures[key ^ uint64_t(t.format) << 58];
  uint64_t frame = g_frames.load();
  if (tex.id && tex.checkedFrame == frame) return tex.id;
  tex.checkedFrame = frame;
  uint32_t bpe = xenos::BytesPerElement(t.format);
  bool block = xenos::Block(t.format);
  uint32_t bw = block ? (t.width + 3) / 4 : t.width, bh = block ? (t.height + 3) / 4 : t.height;
  uint32_t pitchE = std::max(block ? t.pitch / 4 : t.pitch, bw);
  uint32_t span = ((pitchE + 31) & ~31u) * ((bh + 31) & ~31u) * bpe;
  auto ts = Clock::now();
  uint64_t sig = Signature(t.base, span);
  auto td = Clock::now();
  g_sigMs += std::chrono::duration<double, std::milli>(td - ts).count();
  if (tex.id && sig == tex.signature) return tex.id;
  // k8 com swizzle identidade (planos de vídeo): caminho rápido direto para LUMINANCE_ALPHA.
  td = Clock::now();
  static std::vector<uint8_t> lumAlpha;
  bool la = t.format == xenos::k8 && t.swizzle == 0x688 && xenos::DecodeK8LumAlpha(Physical, t, lumAlpha);
  if (!la && !xenos::DecodeLevel0(Physical, t, g_pixels)) { Skip("decodificação de textura (branco)"); return tex.id ? tex.id : White(); }
  if (const char* dump = std::getenv("NIER_DUMP_TEXTURES"); dump && !la && tex.uploads < 3) {  // diagnóstico
    char name[160];
    std::snprintf(name, sizeof name, "%s/tex_%08X_%ux%u_f%u_u%u.ppm", dump, t.base, t.width, t.height, t.format, tex.uploads);
    if (FILE* f = std::fopen(name, "wb")) {
      std::fprintf(f, "P6\n%u %u\n255\n", t.width, t.height);
      for (size_t i = 0; i < size_t(t.width) * t.height; ++i) std::fwrite(&g_pixels[i * 4], 1, 3, f);
      std::fclose(f);
    }
    std::string alpha = std::string(name) + ".alfa.pgm";
    if (FILE* f = std::fopen(alpha.c_str(), "wb")) {
      std::fprintf(f, "P5\n%u %u\n255\n", t.width, t.height);
      for (size_t i = 0; i < size_t(t.width) * t.height; ++i) std::fwrite(&g_pixels[i * 4 + 3], 1, 1, f);
      std::fclose(f);
    }
  }
  auto tu = Clock::now();
  g_decodeMs += std::chrono::duration<double, std::milli>(tu - td).count();
  bool fresh = !tex.id;
  if (!tex.id) glGenTextures(1, &tex.id);
  glBindTexture(GL_TEXTURE_2D, tex.id);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  const uint8_t* upload = la ? lumAlpha.data() : g_pixels.data();
  GLenum glFormat = la ? GL_LUMINANCE_ALPHA : GL_RGBA;
  if (fresh || tex.glFormat != glFormat) glTexImage2D(GL_TEXTURE_2D, 0, GLint(glFormat), GLsizei(t.width), GLsizei(t.height), 0, glFormat, GL_UNSIGNED_BYTE, upload);
  else glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, GLsizei(t.width), GLsizei(t.height), glFormat, GL_UNSIGNED_BYTE, upload);
  tex.glFormat = glFormat;
  ++tex.uploads;
  g_uploadMs += std::chrono::duration<double, std::milli>(Clock::now() - tu).count();
  // Mali-450: textura minificada sem mipmap satura a banda e segura a CPU (medido no Rayman).
  bool pot = (t.width & (t.width - 1)) == 0 && (t.height & (t.height - 1)) == 0;
  if (g_config.mipmaps && pot && tex.uploads == 1 && (t.width > 16 || t.height > 16)) {
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
  } else {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  }
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  const GLint wrap[] = {GL_REPEAT, GL_MIRRORED_REPEAT, GL_CLAMP_TO_EDGE};
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (pot || t.clampX >= 2) ? wrap[std::min(t.clampX, 2u)] : GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (pot || t.clampY >= 2) ? wrap[std::min(t.clampY, 2u)] : GL_CLAMP_TO_EDGE);
  tex.signature = sig;
  return tex.id;
}

// ---- programas
struct Program {
  GLuint id = 0;
  GLint vc = -1, pc = -1, alphaFunc = -1, alphaRef = -1, wideScale = -1;
  std::vector<std::pair<int, GLint>> bools;    // (índice, local)
  std::vector<std::pair<int, GLint>> samplers; // (fetch slot, local nx_size) — unidade = ordem
  int vcCount = 256, pcCount = 256;
};
std::unordered_map<uint64_t, Program> g_programs;

Program* ProgramFor(const GuestShader* vs, const GuestShader* ps) {
  uint64_t key = vs->key * 0x9E3779B97F4A7C15ull ^ ps->key;
  auto it = g_programs.find(key);
  if (it != g_programs.end()) return it->second.id ? &it->second : nullptr;
  Program& p = g_programs[key];
  char vn[40], pn[40];
  std::snprintf(vn, sizeof vn, "%016llX_vs", (unsigned long long)vs->key);
  std::snprintf(pn, sizeof pn, "%016llX_ps", (unsigned long long)ps->key);
  GLuint v = CompileShader(GL_VERTEX_SHADER, vs->source.c_str(), vn), f = CompileShader(GL_FRAGMENT_SHADER, ps->source.c_str(), pn);
  if (!v || !f) { if (v) glDeleteShader(v); if (f) glDeleteShader(f); return nullptr; }
  p.id = glCreateProgram();
  glAttachShader(p.id, v); glAttachShader(p.id, f);
  for (uint32_t i = 0; i < vs->inputs.size() && i < kMaxInputs; ++i) glBindAttribLocation(p.id, i, ("a" + std::to_string(i)).c_str());
  glLinkProgram(p.id);
  glDeleteShader(v); glDeleteShader(f);
  GLint ok = 0;
  glGetProgramiv(p.id, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[4096]; glGetProgramInfoLog(p.id, sizeof log, nullptr, log);
    std::fprintf(stderr, "[nier-gl] link %s/%s falhou: %s\n", vn, pn, log);
    glDeleteProgram(p.id); p.id = 0; return nullptr;
  }
  glUseProgram(p.id);
  p.vc = glGetUniformLocation(p.id, "VC");
  p.pc = glGetUniformLocation(p.id, "PC");
  p.alphaFunc = glGetUniformLocation(p.id, "alphaFunc");
  p.alphaRef = glGetUniformLocation(p.id, "alphaRef");
  p.wideScale = glGetUniformLocation(p.id, "wideScale");
  GLint count = 0;
  glGetProgramiv(p.id, GL_ACTIVE_UNIFORMS, &count);
  for (GLint i = 0; i < count; ++i) {
    char name[128]; GLint size; GLenum type;
    glGetActiveUniform(p.id, GLuint(i), sizeof name, nullptr, &size, &type, name);
    if (!std::strncmp(name, "VC", 2)) p.vcCount = size;
    if (!std::strncmp(name, "PC", 2)) p.pcCount = size;
  }
  for (int b = 0; b < 256; ++b)
    if ((vs->booleanMask[b >> 5] | ps->booleanMask[b >> 5]) & (1u << (b & 31))) {
      GLint loc = glGetUniformLocation(p.id, ("B" + std::to_string(b)).c_str());
      if (loc >= 0) p.bools.push_back({b, loc});
    }
  int unit = 0;
  for (int t = 0; t < 32; ++t)
    if (ps->textureMask & (1u << t)) {
      std::string n = std::to_string(t);
      GLint loc = glGetUniformLocation(p.id, ("T" + n).c_str());
      if (loc >= 0) glUniform1i(loc, unit);
      GLint depth = glGetUniformLocation(p.id, ("nx_depth" + n).c_str());
      if (depth >= 0) glUniform1i(depth, 0);
      p.samplers.push_back({t, glGetUniformLocation(p.id, ("nx_size" + n).c_str())});
      ++unit;
    }
  if (p.wideScale >= 0) glUniform1f(p.wideScale, 1.0f);
  return &p;
}

std::vector<float> g_constants;
// Envia as constantes na ordem compactada (GuestShader::constMap); literais por índice original.
void UploadConstants(GLint location, int count, const uint8_t* bank, const GuestShader* s) {
  if (location < 0) return;
  size_t n = std::min<size_t>(size_t(count), s->constMap.size());
  g_constants.resize(n * 4);
  size_t literals = s->literals.size() / 4;
  for (size_t j = 0; j < n; ++j) {
    size_t reg = s->constMap[j];
    if (reg >= 256 - literals) std::memcpy(&g_constants[j * 4], &s->literals[(reg - (256 - literals)) * 4], 16);
    else for (int c = 0; c < 4; ++c) g_constants[j * 4 + c] = BEF(bank + reg * 16 + 4 * c);
  }
  glUniform4fv(location, GLsizei(n), g_constants.data());
}

// ---- estados fixos
GLenum BlendFactor(uint32_t v) {
  static const GLenum m[] = {GL_ZERO, GL_ONE, GL_ONE, GL_ONE, GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR, GL_SRC_ALPHA,
                             GL_ONE_MINUS_SRC_ALPHA, GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA,
                             GL_CONSTANT_COLOR, GL_ONE_MINUS_CONSTANT_COLOR, GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA,
                             GL_SRC_ALPHA_SATURATE};
  return v < 17 ? m[v] : GL_ONE;
}
GLenum BlendOp(uint32_t v) {
  static const GLenum m[] = {GL_FUNC_ADD, GL_FUNC_SUBTRACT, GL_FUNC_ADD, GL_FUNC_ADD, GL_FUNC_REVERSE_SUBTRACT};
  return v < 5 ? m[v] : GL_FUNC_ADD;
}
void ApplyFixedState(const uint8_t* state) {
  float xs = std::fabs(BEF(Field(state, kViewport))), xo = BEF(Field(state, kViewport + 4));
  float ys = std::fabs(BEF(Field(state, kViewport + 8))), yo = BEF(Field(state, kViewport + 12));
  float zs = BEF(Field(state, kViewport + 16)), zo = BEF(Field(state, kViewport + 20));
  float s = Scale();
  glViewport(GLint(std::lround((xo - xs) * s)), GLint(std::lround((yo - ys) * s)), GLsizei(std::lround(2 * xs * s)), GLsizei(std::lround(2 * ys * s)));
  float n = zo, f = zo + zs;
  if (zs == 0 && zo == 0) { n = 0; f = 1; }
  glDepthRangef(std::clamp(n, 0.0f, 1.0f), std::clamp(f, 0.0f, 1.0f));
  uint32_t blend = BE32(Field(state, kBlendControl));
  if (blend == 0x00010001) glDisable(GL_BLEND);
  else {
    glEnable(GL_BLEND);
    glBlendFuncSeparate(BlendFactor(blend & 31), BlendFactor((blend >> 8) & 31), BlendFactor((blend >> 16) & 31), BlendFactor((blend >> 24) & 31));
    glBlendEquationSeparate(BlendOp((blend >> 5) & 7), BlendOp((blend >> 21) & 7));
  }
  uint32_t depth = BE32(Field(state, kDepthControl));
  if (depth & 2) { glEnable(GL_DEPTH_TEST); glDepthFunc(GL_NEVER + ((depth >> 4) & 7)); } else glDisable(GL_DEPTH_TEST);
  glDepthMask((depth & 4) ? GL_TRUE : GL_FALSE);
  uint32_t mode = BE32(Field(state, kModeControl));
  uint32_t cull = mode & 3;
  if (g_config.cull && (cull == 1 || cull == 2)) {
    glEnable(GL_CULL_FACE);
    glCullFace(cull == 1 ? GL_FRONT : GL_BACK);
    glFrontFace((mode & 4) ? GL_CCW : GL_CW);  // o VS inverte y: a ordem dos vértices na tela também inverte
  } else glDisable(GL_CULL_FACE);
  uint32_t mask = BE32(Field(state, 0x28DC)) & 15;  // RB_COLOR_MASK do alvo 0
  glColorMask(mask & 1, (mask >> 1) & 1, (mask >> 2) & 1, (mask >> 3) & 1);
}

void ReplayDraw(const Frame& f, const Cmd& c) {
  const uint8_t* state = f.blob.data() + c.state;
  auto t0 = Clock::now();
  CurrentTarget(state);
  auto t1 = Clock::now();
  g_pTarget += Ms(t0, t1);
  ApplyFixedState(state);
  Program* p = ProgramFor(c.vs, c.ps);
  if (!p) { Skip("programa não compilou/linkou"); return; }
  glUseProgram(p->id);
  UploadConstants(p->vc, p->vcCount, Field(state, kVertexConstants), c.vs);
  UploadConstants(p->pc, p->pcCount, Field(state, kPixelConstants), c.ps);
  for (auto [b, loc] : p->bools) glUniform1i(loc, (BE32(Field(state, kBoolConstants + 4 * (b >> 5))) >> (b & 31)) & 1);
  uint32_t colorControl = BE32(Field(state, kColorControl));
  if (p->alphaFunc >= 0) glUniform1i(p->alphaFunc, (colorControl & 8) ? int(colorControl & 7) : 7);
  if (p->alphaRef >= 0) glUniform1f(p->alphaRef, BEF(Field(state, 0x2904)));
  int unit = 0;
  for (auto [slot, sizeLoc] : p->samplers) {
    const uint8_t* fetch = Field(state, kFetchConstants + 24 * slot);
    auto t = xenos::DecodeFetch(fetch);
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, (BE32(fetch) & 3) == 2 ? TextureFor(t) : White());
    if (sizeLoc >= 0) glUniform2f(sizeLoc, float(t.width), float(t.height));
    ++unit;
  }
  glActiveTexture(GL_TEXTURE0);
  auto t2 = Clock::now();
  g_pState += Ms(t1, t2);
  glBindBuffer(GL_ARRAY_BUFFER, g_vertexBuffer);
  size_t bytes = size_t(c.vertexCount) * 16;
  glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(bytes * c.inputCount), nullptr, GL_STREAM_DRAW);
  for (uint32_t i = 0; i < c.inputCount; ++i) {
    glBufferSubData(GL_ARRAY_BUFFER, GLintptr(bytes * i), GLsizeiptr(bytes), f.blob.data() + c.inputs[i]);
    glEnableVertexAttribArray(i);
    glVertexAttribPointer(i, 4, GL_FLOAT, GL_FALSE, 16, reinterpret_cast<void*>(bytes * i));
  }
  for (uint32_t i = c.inputCount; i < kMaxInputs; ++i) glDisableVertexAttribArray(i);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_indexBuffer);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(c.indexCount * 2), f.blob.data() + c.indices, GL_STREAM_DRAW);
  auto t3 = Clock::now();
  g_pBuffers += Ms(t2, t3);
  glDrawElements(GL_TRIANGLES, GLsizei(c.indexCount), GL_UNSIGNED_SHORT, nullptr);
  g_pDraw += Ms(t3, Clock::now());
}

uint64_t TargetKeyOf(const uint8_t* state) {
  uint32_t surface = BE32(Field(state, kSurfaceInfo)), color = BE32(Field(state, kColorInfo));
  return uint64_t(color & 0xF0FFF) << 16 | (surface & 0x3FFF);
}
void ReplayResolve(const Frame& f, const Cmd& c) {
  const uint8_t* state = f.blob.data() + c.state;
  uint64_t srcKey = TargetKeyOf(state);
  bool clears = c.flags & 0x300;
  float s = Scale();
  if (c.fetch != SIZE_MAX) {
    auto t = xenos::DecodeFetch(f.blob.data() + c.fetch);
    static bool noCopy = std::getenv("NIER_NO_RESOLVE_COPY") != nullptr;  // diagnóstico
    if (t.width >= 8 && t.height >= 8 && !noCopy) {
      ++g_resolveCopies;
      uint64_t key = uint64_t(t.base) << 32 | uint64_t(t.width) << 16 | t.height;
      Target& dst = g_resolved[key];
      int w = std::max(1, int(t.width * s)), h = std::max(1, int(t.height * s));
      if (dst.w != w || dst.h != h) CreateTarget(dst, w, h, false);
      int x0 = c.hasRect ? int(c.rect[0] * s) : 0, y0 = c.hasRect ? int(c.rect[1] * s) : 0;
      // substitui pendência anterior do mesmo destino (a antiga nunca foi usada)
      for (size_t i = 0; i < g_pendingCopies.size(); ++i)
        if (g_pendingCopies[i].dst == key) { g_pendingCopies.erase(g_pendingCopies.begin() + i); ++g_copiesSkipped; break; }
      if (g_frameSampled.count(key) || g_everSampled.count(key)) {
        g_pendingCopies.push_back({srcKey, key, x0, y0, w, h});
        FlushCopiesFrom(srcKey);  // cópia já (semântica do Resolve), só para destinos usados
      } else {
        ++g_copiesSkipped;      // ninguém amostra este destino: a cópia é pulada
      }
    }
  }
  // Resolve com limpeza (D3DRESOLVE_CLEARRENDERTARGET 0x100 / CLEARDEPTHSTENCIL 0x200, a confirmar).
  if (clears) {
    Target& src = CurrentTarget(state);
    glBindFramebuffer(GL_FRAMEBUFFER, src.fbo);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(1, 1, 1, 1);
    glDepthMask(GL_TRUE);
    glClearColor(((c.argb >> 16) & 255) / 255.f, ((c.argb >> 8) & 255) / 255.f, (c.argb & 255) / 255.f, (c.argb >> 24) / 255.f);
    glClearDepthf(1.0f);
    glClear(((c.flags & 0x100) ? GL_COLOR_BUFFER_BIT : 0) | ((c.flags & 0x200) ? GL_DEPTH_BUFFER_BIT : 0));
  }
}

void PresentToWindow() {
  auto it = g_targets.find(g_mainTarget);
  int ww = 1280, wh = 720;
  if (!UsingPbuffer()) SDL_GetWindowSizeInPixels(g_window, &ww, &wh);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, ww, wh);
  glDisable(GL_SCISSOR_TEST); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_BLEND);
  glColorMask(1, 1, 1, 1);
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  if (it != g_targets.end()) {
    int pw = std::min(ww, wh * 16 / 9), ph = pw * 9 / 16;  // letterbox 16:9, sem esticar
    glViewport((ww - pw) / 2, (wh - ph) / 2, pw, ph);
    glUseProgram(g_blitProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, it->second.texture);
    glBindBuffer(GL_ARRAY_BUFFER, g_blitBuffer);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    for (uint32_t i = 1; i < kMaxInputs; ++i) glDisableVertexAttribArray(i);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  }
  // NIER_GLES2_CAPTURE=<arquivo.ppm>: grava o quadro apresentado (a cada 3 s) para prova automatizada.
  if (const char* capture = std::getenv("NIER_GLES2_CAPTURE")) {
    static Clock::time_point lastShot{};
    if (Clock::now() - lastShot > std::chrono::seconds(3) && ww > 0 && wh > 0) {
      lastShot = Clock::now();
      std::vector<uint8_t> px(size_t(ww) * wh * 4);
      glPixelStorei(GL_PACK_ALIGNMENT, 1);
      glReadPixels(0, 0, ww, wh, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
      static unsigned shot = 0;
      std::string target = capture;
      if (target.size() < 4 || target.compare(target.size() - 4, 4, ".ppm") != 0) {  // diretório: numeradas
        char name[48]; std::snprintf(name, sizeof name, "/quadro_%03u_%06llu.ppm", shot++, (unsigned long long)g_frames.load());
        target += name;
      }
      std::string tmp = target + ".tmp";
      if (FILE* f = std::fopen(tmp.c_str(), "wb")) {
        std::fprintf(f, "P6\n%d %d\n255\n", ww, wh);
        for (int y = wh - 1; y >= 0; --y) for (int x = 0; x < ww; ++x) std::fwrite(&px[(size_t(y) * ww + x) * 4], 1, 3, f);
        std::fclose(f);
        std::rename(tmp.c_str(), target.c_str());
      }
    }
  }
  if (UsingPbuffer()) eglSwapBuffers(g_eglDisplay, g_eglSurface);
  else SDL_GL_SwapWindow(g_window);
}

void RenderThread() {
  if (UsingPbuffer()) {
    if (!eglMakeCurrent(g_eglDisplay, g_eglSurface, g_eglSurface, g_eglContext)) { std::fprintf(stderr, "[nier-gl] eglMakeCurrent falhou\n"); return; }
  } else {
    if (!SDL_GL_MakeCurrent(g_window, g_context)) { std::fprintf(stderr, "[nier-gl] contexto: %s\n", SDL_GetError()); return; }
    SDL_GL_SetSwapInterval(1);
  }
  CreateBlit();
  GLint vsUniforms = 0, fsUniforms = 0;
  glGetIntegerv(GL_MAX_VERTEX_UNIFORM_VECTORS, &vsUniforms);
  glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_VECTORS, &fsUniforms);
  std::fprintf(stderr, "[nier-gl] GLES2 na thread de render: %s / %s; interno %dx%d; uniformes VS %d FS %d; depth24 %d\n",
               reinterpret_cast<const char*>(glGetString(GL_RENDERER)), reinterpret_cast<const char*>(glGetString(GL_VERSION)),
               g_config.renderWidth, g_config.renderHeight, vsUniforms, fsUniforms, int(g_depth24));
  Frame work;
  auto last = Clock::now();
  for (;;) {
    {
      std::unique_lock lock(g_queueMutex);
      g_queueCv.wait(lock, [] { return g_hasInflight || g_quit; });
      if (g_quit) break;
      work.blob.swap(g_inflight.blob);
      work.cmds.swap(g_inflight.cmds);
      work.presentInterval = g_inflight.presentInterval;
    }
    g_mainTarget = 0;
    uint32_t frameDraws = 0;
    g_frameSampled.clear();
    for (const Cmd& c : work.cmds) {
      if (c.kind != Kind::Draw || !c.ps) continue;
      const uint8_t* st = work.blob.data() + c.state;
      for (int t = 0; t < 32; ++t)
        if (c.ps->textureMask & (1u << t)) {
          auto tf = xenos::DecodeFetch(Field(st, kFetchConstants + 24 * t));
          g_frameSampled.insert(uint64_t(tf.base) << 32 | uint64_t(tf.width) << 16 | tf.height);
        }
    }
    auto tr = Clock::now();
    for (const Cmd& c : work.cmds) {
      if (c.kind == Kind::Resolve) { auto tr0 = Clock::now(); ReplayResolve(work, c); ++g_resolves; g_pResolve += Ms(tr0, Clock::now()); }
      else if (c.kind == Kind::Skip) Skip(c.skipReason);
      else { ++g_draws; ++frameDraws; ReplayDraw(work, c); }
    }
    g_copiesSkipped += uint32_t(g_pendingCopies.size());
    g_pendingCopies.clear();
    if (GlProfile()) { auto tf = Clock::now(); glFinish(); g_pFinish += Ms(tf, Clock::now()); }
    auto tp = Clock::now();
    g_replayMs += std::chrono::duration<double, std::milli>(tp - tr).count();
    PresentToWindow();
    auto now = Clock::now();
    g_swapMs += std::chrono::duration<double, std::milli>(now - tp).count();
    g_lastFrameMs = std::chrono::duration<double, std::milli>(now - last).count();
    last = now;
    uint64_t n = ++g_frames;
    if (n % 120 == 0)
    {
      std::fprintf(stderr, "[nier-gl] %llu quadros, %.2f ms, %u draws no quadro, %llu pulados (total), intervalo do jogo %u, programas %zu, texturas %zu, texturas %.2f ms/quadro (hash %.2f decod %.2f envio %.2f), replay %.2f swap %.2f espera do jogo %.2f ms/quadro, GL 0x%x\n",
                   (unsigned long long)n, g_lastFrameMs.load(), frameDraws, (unsigned long long)g_skipped.load(), work.presentInterval,
                   g_programs.size(), g_textures.size(), g_textureMs / 120.0, g_sigMs / 120.0, g_decodeMs / 120.0, g_uploadMs / 120.0, g_replayMs / 120.0, g_swapMs / 120.0, g_gameWaitNs.exchange(0) / 120e6, glGetError());
      if (GlProfile())
        std::fprintf(stderr, "[nier-perfil] ms/quadro: alvo+limpeza %.2f estado+programa+texturas %.2f buffers %.2f desenho %.2f resolve %.2f (%.1f/quadro, cópias pedidas %.1f feitas %.1f evitadas %.1f) glFinish(GPU) %.2f\n",
                     g_pTarget / 120, g_pState / 120, g_pBuffers / 120, g_pDraw / 120, g_pResolve / 120, g_resolves / 120.0, g_resolveCopies / 120.0, g_copiesDone / 120.0, g_copiesSkipped / 120.0, g_pFinish / 120);
      g_copiesDone = g_copiesSkipped = 0;
      g_pTarget = g_pState = g_pBuffers = g_pDraw = g_pFinish = g_pResolve = 0; g_resolves = g_resolveCopies = 0;
      g_textureMs = g_sigMs = g_decodeMs = g_uploadMs = g_replayMs = g_swapMs = 0;
    }
    {
      std::lock_guard lock(g_queueMutex);
      work.Clear();
      g_inflight.blob.swap(work.blob);
      g_inflight.cmds.swap(work.cmds);
      g_hasInflight = false;
    }
    g_queueCv.notify_all();
  }
  SDL_GL_MakeCurrent(g_window, nullptr);
}

// Converte os inputs do VS para float4 no blob. Vértices [first, first+count).
bool SnapshotInputs(Cmd& c, const uint8_t* state, int64_t first, uint32_t count, const uint8_t* upData, uint32_t upStride) {
  c.inputCount = uint32_t(std::min<size_t>(c.vs->inputs.size(), kMaxInputs));
  c.vertexCount = count;
  for (uint32_t i = 0; i < c.inputCount; ++i) {
    VertexInput in = c.vs->inputs[i];
    const uint8_t* base;
    uint32_t endian;
    if (upData && in.slot == 95) {          // BeginVertices: stream 0 vem da memória da CPU, 8in32
      in.stride = upStride / 4;
      base = upData; endian = 2;
    } else {
      const uint8_t* vf = Field(state, kFetchConstants + in.slot * 8);
      uint32_t address = BE32(vf) & 0x1FFFFFFC, size = ((BE32(vf + 4) >> 2) & 0xFFFFFF) * 4;
      endian = BE32(vf + 4) & 3;
      // Faixa em 64 bits e com sinal: baseVertex negativo ou grande não pode dar a volta (SIGSEGV em 07/11).
      int64_t start = first * int64_t(in.stride) * 4 + int64_t(in.offset) * 4;
      int elementBytes = (in.format == 57) ? 12 : (in.format == 38 || in.format == 35) ? 16 : (in.format == 26 || in.format == 32 || in.format == 34 || in.format == 37) ? 8 : 4;
      int64_t end = (first + int64_t(count) - 1) * int64_t(in.stride) * 4 + int64_t(in.offset) * 4 + elementBytes;
      if (!size || first < 0 || start < 0 || end > int64_t(size)) { RecordSkip("stream de vértice fora do buffer"); return false; }
      base = Physical(address, size);
      if (!base) { RecordSkip("stream de vértice sem memória"); return false; }
      base += size_t(first) * in.stride * 4;
    }
    c.inputs[i] = Append(g_building, nullptr, size_t(count) * 16);
    if (!ConvertInput(in, base, endian, 0, count, reinterpret_cast<float*>(g_building.blob.data() + c.inputs[i]))) {
      RecordSkip("formato de vfetch não suportado");
      return false;
    }
  }
  return true;
}

// NIER_RENDER_TRACE=N: descreve os N primeiros desenhos (diagnóstico).
int TraceBudget() { static int n = std::getenv("NIER_RENDER_TRACE") ? std::atoi(std::getenv("NIER_RENDER_TRACE")) : 0; return n; }
std::atomic<int> g_traced{0};
std::atomic<uint64_t> g_presented{0};
bool TraceThisFrame() {
  static long long f = std::getenv("NIER_RENDER_TRACE_FRAME") ? std::atoll(std::getenv("NIER_RENDER_TRACE_FRAME")) : -1;
  return f >= 0 && g_presented.load() == uint64_t(f);
}
void TraceDraw(const Cmd& c, const uint8_t* state, uint32_t primitive, uint32_t count, bool indexed) {
  if (!TraceThisFrame() && g_traced.fetch_add(1) >= TraceBudget()) return;
  float vp[6]; for (int i = 0; i < 6; ++i) vp[i] = BEF(Field(state, kViewport + 4 * i));
  std::fprintf(stderr, "[nier-trace] draw prim %u n %u idx %d vs %016llX ps %016llX surf %08X color %08X vport %.1f %.1f %.1f %.1f %.3f %.3f blend %08X depth %08X mode %08X vte %08X cc %08X mask %08X\n",
               primitive, count, int(indexed), (unsigned long long)c.vs->key, (unsigned long long)c.ps->key,
               BE32(Field(state, kSurfaceInfo)), BE32(Field(state, kColorInfo)), vp[0], vp[1], vp[2], vp[3], vp[4], vp[5],
               BE32(Field(state, kBlendControl)), BE32(Field(state, kDepthControl)), BE32(Field(state, kModeControl)),
               BE32(Field(state, 0x294C)), BE32(Field(state, kColorControl)), BE32(Field(state, 0x28DC)));
  for (uint32_t i = 0; i < c.inputCount; ++i) {
    const auto& in = c.vs->inputs[i];
    const float* v = reinterpret_cast<const float*>(g_building.blob.data() + c.inputs[i]);
    std::fprintf(stderr, "[nier-trace]   a%u slot %u fmt %u off %u stride %u v0 (%g %g %g %g) v1 (%g %g %g %g)\n", i, in.slot, in.format, in.offset, in.stride,
                 v[0], v[1], v[2], v[3], c.vertexCount > 1 ? v[4] : 0, c.vertexCount > 1 ? v[5] : 0, c.vertexCount > 1 ? v[6] : 0, c.vertexCount > 1 ? v[7] : 0);
  }
  for (int r = 0; r < 4; ++r) {
    const uint8_t* k = Field(state, kVertexConstants + 16 * r);
    std::fprintf(stderr, "[nier-trace]   VC%d %g %g %g %g\n", r, BEF(k), BEF(k + 4), BEF(k + 8), BEF(k + 12));
  }
  for (int t = 0; t < 32; ++t) if (c.ps->textureMask & (1u << t)) {
    const uint8_t* f = Field(state, kFetchConstants + 24 * t);
    auto tf = xenos::DecodeFetch(f);
    std::fprintf(stderr, "[nier-trace]   T%d words %08X %08X %08X %08X %08X %08X fmt %u %ux%u base %08X tiled %d endian %u dim %u pitch %u\n", t,
                 BE32(f), BE32(f + 4), BE32(f + 8), BE32(f + 12), BE32(f + 16), BE32(f + 20), tf.format, tf.width, tf.height, tf.base, int(tf.tiled), tf.endian, tf.dimension, tf.pitch);
  }
}

// Estado do device copiado NO BeginVertices: até o jogo fechar o bloco ele já pode ter trocado textura e
// constantes para o desenho seguinte.
struct PendingUP { uint32_t device = 0, primitive = 0, count = 0, stride = 0, data = 0; const GuestShader* vs = nullptr; const GuestShader* ps = nullptr; std::vector<uint8_t> state; } g_pendingUP;
void RecordDrawUPImpl(uint32_t device, uint32_t primitive, uint32_t vertexCount, const uint8_t* data, uint32_t stride, const PendingUP* frozen);
void FlushPendingUP() {
  if (!g_pendingUP.data) return;
  uint32_t address = g_pendingUP.data;
  g_pendingUP.data = 0;
  if (const uint8_t* data = Virtual(address, g_pendingUP.count * g_pendingUP.stride))
    RecordDrawUPImpl(g_pendingUP.device, g_pendingUP.primitive, g_pendingUP.count, data, g_pendingUP.stride, &g_pendingUP);
}

bool PrepareShaders(Cmd& c, uint32_t device) {
  if (!TranslatorAvailable()) { RecordSkip("tradutor de shaders ausente (dependência privada)"); return false; }
  c.vs = VertexShaderFor(device, Virtual);
  c.ps = PixelShaderFor(device, Virtual);
  if (!c.vs || !c.ps) { RecordSkip("shader do device ilegível"); return false; }
  if (!c.vs->ok || !c.ps->ok) { RecordSkip("shader não traduzido"); return false; }
  return true;
}
}  // namespace

bool Init(const Config& config, const GuestMemory& memory) {
  g_config = config;
  g_config.renderWidth = std::max(g_config.renderWidth, 640);  // regra: interno nunca abaixo de 640
  g_config.renderHeight = std::max(g_config.renderHeight, g_config.renderWidth * 9 / 16);
  g_mem = memory;
  if (g_config.shaderDir) SetShaderDumpDir(g_config.shaderDir);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);  // profundidade fica nos FBOs internos
  g_window = std::getenv("NIER_RENDER_PBUFFER") ? nullptr : SDL_CreateWindow("NieR", 1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
  if (!g_window || !(g_context = SDL_GL_CreateContext(g_window))) {
    if (g_window) { std::fprintf(stderr, "[nier-gl] janela/contexto SDL: %s; tentando EGL pbuffer\n", SDL_GetError()); SDL_DestroyWindow(g_window); g_window = nullptr; }
    if (!CreatePbuffer()) { std::fprintf(stderr, "[nier-gl] EGL pbuffer falhou (0x%x)\n", eglGetError()); return false; }
    std::fprintf(stderr, "[nier-gl] sem janela: EGL pbuffer 1280x720\n");
  } else {
    SDL_GL_MakeCurrent(g_window, nullptr);  // a thread de render assume o contexto
  }
  g_quit = false;
  g_thread = std::thread(RenderThread);
  g_enabled = true;
  std::fprintf(stderr, "[nier-gl] renderizador ligado; tradutor %s\n", TranslatorAvailable() ? "presente" : "AUSENTE");
  return true;
}

void Shutdown() {
  if (!g_enabled.exchange(false)) return;
  { std::lock_guard lock(g_queueMutex); g_quit = true; }
  g_queueCv.notify_all();
  if (g_thread.joinable()) g_thread.join();
  if (g_context) SDL_GL_DestroyContext(g_context);
  if (g_window) SDL_DestroyWindow(g_window);
  g_context = nullptr; g_window = nullptr;
}

bool Enabled() { return g_enabled.load(std::memory_order_relaxed); }
const Config& Settings() { return g_config; }

void RecordDraw(uint32_t device, uint32_t primitive, uint32_t baseVertex, uint32_t startIndex, uint32_t count, bool indexed) {
  static uint64_t calls = 0;
  if (++calls % 5000 == 0)
    std::fprintf(stderr, "[nier-jogo] %llu desenhos gravados; quadro atual %zu comandos %zu MB; apresentados %llu\n", (unsigned long long)calls,
                 g_building.cmds.size(), g_building.blob.size() >> 20, (unsigned long long)g_presented.load());
  FlushPendingUP();
  GuardBuilding();
  const uint8_t* state = Virtual(device + kStateBegin, kStateSize);
  if (!state || !count) return;
  Cmd c{Kind::Draw};
  if (!PrepareShaders(c, device)) return;
  c.state = Append(g_building, state, kStateSize);
  const uint8_t* copy = g_building.blob.data() + c.state;
  g_srcIndices.clear();
  int64_t first = 0;
  uint32_t vertices = 0;
  if (indexed) {
    // Objeto IB (+0x3144): palavra 0 bit 31 = índices de 32 bits; +0x18 = endereço dos dados.
    uint32_t ib = BE32(Field(copy, kIndexBuffer));
    const uint8_t* object = ib ? Virtual(ib, 0x20) : nullptr;
    if (!object) { RecordSkip("sem index buffer"); return; }
    bool is32 = BE32(object) & 0x80000000u;
    uint32_t address = BE32(object + 0x18), bytes = is32 ? 4 : 2;
    uint32_t physical = (address & 0x1FFFFFFF) + (address >= 0xE0000000 ? 0x1000 : 0);  // heap 0xE0000000
    const uint8_t* src = Physical(physical + startIndex * bytes, count * bytes);
    if (!src) { RecordSkip("index buffer ilegível"); return; }
    uint32_t lo = 0xFFFFFFFF, hi = 0;
    for (uint32_t i = 0; i < count; ++i) {
      uint32_t v = is32 ? BE32(src + 4 * i) : uint32_t(src[2 * i]) << 8 | src[2 * i + 1];
      if ((!is32 && v == 0xFFFF) || v == 0xFFFFFFFF) { g_srcIndices.push_back(0xFFFFFFFFu); continue; }
      g_srcIndices.push_back(v);
      lo = std::min(lo, v); hi = std::max(hi, v);
    }
    if (lo > hi) return;
    if (hi - lo >= 65535) { RecordSkip("faixa de índices > 65535"); return; }
    for (auto& v : g_srcIndices) if (v != 0xFFFFFFFFu) v -= lo;
    first = int64_t(int32_t(baseVertex)) + lo; vertices = hi - lo + 1;
  } else {
    first = int64_t(int32_t(baseVertex)); vertices = count;
    for (uint32_t i = 0; i < count; ++i) g_srcIndices.push_back(i);
  }
  if (!Triangulate(primitive, g_srcIndices, g_triIndices)) { RecordSkip("tipo de primitiva não suportado"); return; }
  if (g_triIndices.empty()) return;
  static int trace3d = std::getenv("NIER_RENDER_TRACE_3D") ? std::atoi(std::getenv("NIER_RENDER_TRACE_3D")) : 0;
  if (trace3d > 0 && indexed) {
    --trace3d;
    std::fprintf(stderr, "[nier-3d] prim %u n %u idx %d base %u start %u -> first %lld vertices %u inputs %zu vs %016llX ok %d ps ok %d\n",
                 primitive, count, int(indexed), baseVertex, startIndex, (long long)first, vertices, c.vs->inputs.size(),
                 (unsigned long long)c.vs->key, int(c.vs->ok), int(c.ps->ok));
    for (const auto& in : c.vs->inputs) std::fprintf(stderr, "[nier-3d]   slot %u fmt %u off %u stride %u\n", in.slot, in.format, in.offset, in.stride);
  }
  if (!SnapshotInputs(c, copy, first, vertices, nullptr, 0)) return;
  TraceDraw(c, copy, primitive, count, indexed);
  c.indexCount = uint32_t(g_triIndices.size());
  c.indices = Append(g_building, g_triIndices.data(), g_triIndices.size() * 2);
  g_recordedBytes = g_building.blob.size();
  g_building.cmds.push_back(c);
}

void RecordBeginVertices(uint32_t device, uint32_t primitive, uint32_t vertexCount, uint32_t stride, uint32_t guestData) {
  FlushPendingUP();
  GuardBuilding();
  if (!guestData || !stride || !vertexCount || vertexCount > 65535) return;
  const uint8_t* state = Virtual(device + kStateBegin, kStateSize);
  if (!state) return;
  Cmd probe{Kind::Draw};
  if (!PrepareShaders(probe, device)) return;  // shaders também no momento do Begin
  g_pendingUP.device = device; g_pendingUP.primitive = primitive; g_pendingUP.count = vertexCount;
  g_pendingUP.stride = stride; g_pendingUP.data = guestData; g_pendingUP.vs = probe.vs; g_pendingUP.ps = probe.ps;
  g_pendingUP.state.assign(state, state + kStateSize);
}

void RecordDrawUP(uint32_t device, uint32_t primitive, uint32_t vertexCount, const uint8_t* data, uint32_t stride) {
  FlushPendingUP();
  RecordDrawUPImpl(device, primitive, vertexCount, data, stride, nullptr);
}

namespace {
void RecordDrawUPImpl(uint32_t device, uint32_t primitive, uint32_t vertexCount, const uint8_t* data, uint32_t stride, const PendingUP* frozen) {
  const uint8_t* state = frozen ? frozen->state.data() : Virtual(device + kStateBegin, kStateSize);
  if (!state || !data || !stride || !vertexCount || vertexCount > 65535) return;
  Cmd c{Kind::Draw};
  if (frozen) { c.vs = frozen->vs; c.ps = frozen->ps; }
  else if (!PrepareShaders(c, device)) return;
  c.state = Append(g_building, state, kStateSize);
  g_srcIndices.clear();
  for (uint32_t i = 0; i < vertexCount; ++i) g_srcIndices.push_back(i);
  if (!Triangulate(primitive, g_srcIndices, g_triIndices)) { RecordSkip("tipo de primitiva não suportado (UP)"); return; }
  if (g_triIndices.empty()) return;
  if (!SnapshotInputs(c, g_building.blob.data() + c.state, 0, vertexCount, data, stride)) return;
  TraceDraw(c, g_building.blob.data() + c.state, primitive, vertexCount, false);
  c.indexCount = uint32_t(g_triIndices.size());
  c.indices = Append(g_building, g_triIndices.data(), g_triIndices.size() * 2);
  g_building.cmds.push_back(c);
}

}  // namespace

void RecordResolve(uint32_t device, uint32_t flags, const int32_t* rect, uint32_t destTexture, uint32_t clearColorArgb) {
  FlushPendingUP();
  const uint8_t* state = Virtual(device + kStateBegin, kStateSize);
  if (!state || (flags & 0x70)) return;  // só cor; resolve de profundidade não é copiado
  Cmd c{Kind::Resolve};
  c.flags = flags; c.argb = clearColorArgb;
  if (rect) { std::memcpy(c.rect, rect, sizeof c.rect); c.hasRect = true; }
  c.state = Append(g_building, state, kStateSize);
  if (destTexture) {
    // Textura D3D: fetch constant em +0x18 (layout do 20871; conferir no 9328 pela captura).
    if (const uint8_t* fetch = Virtual(destTexture + 0x18, 24)) c.fetch = Append(g_building, fetch, 24);
  }
  g_building.cmds.push_back(c);
}

void RecordPresentInterval(uint32_t interval) { g_interval = interval; }

void DriveTestInput(uint64_t frame);  // test_input.cpp

void Present() {
  if (!g_enabled) return;
  FlushPendingUP();
  DriveTestInput(++g_presented);
  auto waitStart = Clock::now();
  std::unique_lock lock(g_queueMutex);
  g_queueCv.wait(lock, [] { return !g_hasInflight || g_quit; });
  g_gameWaitNs += uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - waitStart).count());
  g_inflight.blob.swap(g_building.blob);
  g_inflight.cmds.swap(g_building.cmds);
  g_inflight.presentInterval = g_interval;
  g_building.Clear();
  g_hasInflight = true;
  lock.unlock();
  g_queueCv.notify_all();
}

Stats ReadStats() {
  return {g_frames.load(), g_draws.load(), g_skipped.load(), g_recordedBytes.load(), g_lastFrameMs.load(), g_interval.load()};
}

}  // namespace nier::render
