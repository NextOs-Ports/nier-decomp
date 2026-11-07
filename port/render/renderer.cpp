// Renderizador GLES2 do NieR — esqueleto (Etapa E). Ver renderer.h e docs/RENDER.md.
//
// Estado atual: fila de quadro + thread de render dona do contexto, alvo interno >= 640 de largura,
// apresentação com letterbox 16:9, cache de texturas (decodificação Xenos + mipmap em POT estática),
// estados de blend/depth/cull/viewport lidos do device. O desenho 3D fica pendente até o tradutor de
// shaders em execução (VS dependem da declaração de vértice): cada draw pulado registra o motivo.
#include "renderer.h"

#include "nier_d3d.h"
#include "xenos_texture.h"

#include <SDL3/SDL.h>
#include <GLES2/gl2.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
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

// ---------------------------------------------------------------- memória guest
const uint8_t* Virtual(uint32_t address, uint32_t size) {
  return g_mem.virtualBase && uint64_t(address) + size <= 0x100000000ull ? g_mem.virtualBase + address : nullptr;
}
const uint8_t* Physical(uint32_t address, uint32_t size) {
  address &= 0x1FFFFFFF;
  return g_mem.physicalBase && uint64_t(address) + size <= 0x20000000ull ? g_mem.physicalBase + address : nullptr;
}

// ---------------------------------------------------------------- fila de quadro
enum class Kind : uint8_t { Draw, DrawUP, Resolve };
struct Cmd {
  Kind kind;
  uint32_t primitive = 0, baseVertex = 0, startIndex = 0, count = 0, stride = 0;
  bool indexed = false, index32 = false;
  uint32_t flags = 0, argb = 0;
  int32_t rect[4] = {};
  bool hasRect = false;
  size_t state = 0, vertices = SIZE_MAX, indices = SIZE_MAX, fetch = SIZE_MAX;
  uint32_t vertexBytes = 0, firstVertex = 0;
};
struct Frame {
  std::vector<uint8_t> blob;
  std::vector<Cmd> cmds;
  uint32_t presentInterval = 1;
  void Clear() { blob.clear(); cmds.clear(); }
};
size_t Append(Frame& f, const void* p, size_t n) {
  size_t at = f.blob.size();
  f.blob.insert(f.blob.end(), static_cast<const uint8_t*>(p), static_cast<const uint8_t*>(p) + n);
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

// Cópia do estado e do intervalo de vértices referenciado pelo stream 0 (os demais streams entram
// junto com a declaração de vértice, quando o tradutor em execução estiver ligado).
void SnapshotVertices(Frame& f, Cmd& c, const uint8_t* state, uint32_t first, uint32_t count) {
  const uint8_t* vf = Field(state, kFetchConstants + 95 * 8);
  uint32_t base = BE32(vf) & 0x1FFFFFFC, size = ((BE32(vf + 4) >> 2) & 0xFFFFFF) * 4;
  uint32_t stride = uint32_t(*Field(state, kStreamStride0)) * 4;
  c.stride = stride;
  if (!stride || !count || count > 65536 || uint64_t(first + count) * stride > size) return;
  if (const uint8_t* vb = Physical(base + first * stride, count * stride)) {
    c.vertexBytes = count * stride;
    c.firstVertex = first;
    c.vertices = Append(f, vb, c.vertexBytes);
  }
}

// ---------------------------------------------------------------- GL (thread de render)
struct Target { GLuint texture = 0, fbo = 0; int w = 0, h = 0; };
std::map<uint64_t, Target> g_targets;  // por RB_SURFACE_INFO/RB_COLOR_INFO
std::map<uint64_t, Target> g_resolved; // por endereço/tamanho da textura destino
uint64_t g_mainTarget = 0;
GLuint g_blitProgram = 0, g_blitBuffer = 0;
std::map<std::string, uint32_t> g_skipReasons;

void Skip(const char* reason) {
  ++g_skipped;
  if (++g_skipReasons[reason] == 1) std::fprintf(stderr, "[nier-gl] desenho pulado: %s\n", reason);
}

GLuint CompileShader(GLenum type, const char* source) {
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &source, nullptr);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) { char log[2048]; glGetShaderInfoLog(s, sizeof log, nullptr, log); std::fprintf(stderr, "[nier-gl] shader: %s\n", log); glDeleteShader(s); return 0; }
  return s;
}

void CreateBlit() {
  const char* vs = "attribute vec2 p; varying vec2 uv; void main(){ uv = p * 0.5 + 0.5; gl_Position = vec4(p, 0.0, 1.0); }";
  const char* fs = "precision mediump float; varying vec2 uv; uniform sampler2D t; void main(){ gl_FragColor = vec4(texture2D(t, uv).rgb, 1.0); }";
  GLuint v = CompileShader(GL_VERTEX_SHADER, vs), f = CompileShader(GL_FRAGMENT_SHADER, fs);
  g_blitProgram = glCreateProgram();
  glAttachShader(g_blitProgram, v); glAttachShader(g_blitProgram, f);
  glBindAttribLocation(g_blitProgram, 0, "p");
  glLinkProgram(g_blitProgram);
  glDeleteShader(v); glDeleteShader(f);
  const float quad[] = {-1, -1, 1, -1, -1, 1, 1, 1};
  glGenBuffers(1, &g_blitBuffer);
  glBindBuffer(GL_ARRAY_BUFFER, g_blitBuffer);
  glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
}

void CreateTarget(Target& t, int w, int h) {
  if (t.texture) glDeleteTextures(1, &t.texture);
  if (t.fbo) glDeleteFramebuffers(1, &t.fbo);
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
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) std::fprintf(stderr, "[nier-gl] FBO incompleto %dx%d\n", w, h);
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);
}

// Alvo de cor atual do guest. Todos os alvos são desenhados na resolução interna, proporcional à
// largura do alvo guest (o back buffer de 1280 vira g_config.renderWidth).
Target& CurrentTarget(const uint8_t* state) {
  uint32_t surface = BE32(Field(state, kSurfaceInfo)), color = BE32(Field(state, kColorInfo));
  uint64_t key = uint64_t(color & 0xF0FFF) << 16 | (surface & 0x3FFF);
  uint32_t guestPitch = std::max<uint32_t>(surface & 0x3FFF, 1);
  float scale = float(g_config.renderWidth) / 1280.0f;
  int w = std::max(1, int(guestPitch * scale + 0.5f)), h = std::max(1, int(720 * scale + 0.5f));
  Target& t = g_targets[key];
  if (t.w != w || t.h != h) CreateTarget(t, w, h);
  if ((key & 0x3FFF) > (g_mainTarget & 0x3FFF)) g_mainTarget = key;
  glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
  return t;
}

// ---- texturas: decodificação Xenos, reenvio só quando o conteúdo muda, mipmap em POT estática
struct Texture { GLuint id = 0; uint64_t signature = 0; uint32_t uploads = 0; uint64_t checkedFrame = ~0ull; };
std::map<uint64_t, Texture> g_textures;
std::vector<uint8_t> g_pixels;

uint64_t Signature(uint32_t base, uint32_t span) {
  uint64_t h = 1469598103934665603ull;
  for (uint32_t i = 0; i < 32; ++i) {
    uint32_t off = span > 8 ? uint32_t(uint64_t(span - 8) * i / 31) : 0;
    uint64_t w = 0;
    if (const uint8_t* p = Physical(base + off, 8)) std::memcpy(&w, p, 8);
    h = (h ^ w) * 1099511628211ull;
  }
  return h;
}

GLuint TextureFor(const xenos::TextureFetch& t) {
  uint64_t key = uint64_t(t.base) << 32 | uint64_t(t.width) << 16 | t.height;
  if (auto r = g_resolved.find(key); r != g_resolved.end()) return r->second.texture;
  if (!xenos::Supported(t.format)) { Skip("formato de textura não suportado"); return 0; }
  Texture& tex = g_textures[key ^ uint64_t(t.format) << 58];
  uint64_t frame = g_frames.load();
  if (tex.id && tex.checkedFrame == frame) return tex.id;
  tex.checkedFrame = frame;
  uint32_t span = t.width * t.height * 4;
  uint64_t sig = Signature(t.base, span);
  if (tex.id && sig == tex.signature) return tex.id;
  if (!xenos::DecodeLevel0(Physical, t, g_pixels)) { Skip("decodificação de textura"); return tex.id; }
  if (!tex.id) glGenTextures(1, &tex.id);
  glBindTexture(GL_TEXTURE_2D, tex.id);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, GLsizei(t.width), GLsizei(t.height), 0, GL_RGBA, GL_UNSIGNED_BYTE, g_pixels.data());
  ++tex.uploads;
  // Mali-450: textura minificada sem mipmap satura a banda e segura a CPU (medido no Rayman).
  // Só textura estática (1º envio) e POT; dinâmica (vídeo, reenviada) fica num nível só.
  bool pot = (t.width & (t.width - 1)) == 0 && (t.height & (t.height - 1)) == 0;
  if (g_config.mipmaps && pot && tex.uploads == 1 && (t.width > 16 || t.height > 16)) {
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
  } else {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  }
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  const GLint wrap[] = {GL_REPEAT, GL_MIRRORED_REPEAT, GL_CLAMP_TO_EDGE};
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap[std::min(t.clampX, 2u)]);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap[std::min(t.clampY, 2u)]);
  tex.signature = sig;
  return tex.id;
}

// ---- estados fixos lidos dos registradores do device
GLenum BlendFactor(uint32_t v) {
  static const GLenum m[] = {GL_ZERO, GL_ONE, GL_ONE, GL_ONE, GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR, GL_SRC_ALPHA,
                             GL_ONE_MINUS_SRC_ALPHA, GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA,
                             GL_CONSTANT_COLOR, GL_ONE_MINUS_CONSTANT_COLOR, GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA,
                             GL_SRC_ALPHA_SATURATE};
  return v < 17 ? m[v] : GL_ONE;
}
GLenum BlendOp(uint32_t v) {
  static const GLenum m[] = {GL_FUNC_ADD, GL_FUNC_SUBTRACT, GL_FUNC_ADD /* min: sem ext */, GL_FUNC_ADD /* max */, GL_FUNC_REVERSE_SUBTRACT};
  return v < 5 ? m[v] : GL_FUNC_ADD;
}
void ApplyFixedState(const uint8_t* state, const Target& target) {
  // viewport: PA_CL_VPORT_* (escala/offset em pixels do alvo guest), convertidos para a resolução interna
  float xs = std::fabs(BEF(Field(state, kViewport))), xo = BEF(Field(state, kViewport + 4));
  float ys = std::fabs(BEF(Field(state, kViewport + 8))), yo = BEF(Field(state, kViewport + 12));
  float scale = float(g_config.renderWidth) / 1280.0f;
  float x = (xo - xs) * scale, y = (yo - ys) * scale, w = 2 * xs * scale, h = 2 * ys * scale;
  glViewport(GLint(std::lround(x)), GLint(target.h - std::lround(y + h)), GLsizei(std::lround(w)), GLsizei(std::lround(h)));
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
  uint32_t cull = mode & 3;  // 1 = frente, 2 = trás
  if (cull == 1 || cull == 2) { glEnable(GL_CULL_FACE); glCullFace(cull == 1 ? GL_FRONT : GL_BACK); glFrontFace((mode & 4) ? GL_CW : GL_CCW); }
  else glDisable(GL_CULL_FACE);
}

void ReplayDraw(const Frame& f, const Cmd& c) {
  const uint8_t* state = f.blob.data() + c.state;
  Target& target = CurrentTarget(state);
  ApplyFixedState(state, target);
  // Texturas: as 16 primeiras fetch constants de textura que estiverem preenchidas.
  for (uint32_t slot = 0; slot < 16; ++slot) {
    const uint8_t* fetch = Field(state, kFetchConstants + 24 * slot);
    if ((BE32(fetch) & 3) != 2) continue;
    TextureFor(xenos::DecodeFetch(fetch));
  }
  // Programa: VS/PS do device (+0x3248/+0x3244). Tradução em execução + declaração pendentes.
  if (!BE32(Field(state, kVertexShader)) || !BE32(Field(state, kPixelShader))) { Skip("sem shader no device"); return; }
  Skip("tradutor de shaders em execução ainda não ligado (Etapa E pendente)");
}

void ReplayResolve(const Frame& f, const Cmd& c) {
  const uint8_t* state = f.blob.data() + c.state;
  Target& src = CurrentTarget(state);
  if (c.fetch != SIZE_MAX) {
    auto t = xenos::DecodeFetch(f.blob.data() + c.fetch);
    if (t.width >= 8 && t.height >= 8) {
      uint64_t key = uint64_t(t.base) << 32 | uint64_t(t.width) << 16 | t.height;
      Target& dst = g_resolved[key];
      float scale = float(g_config.renderWidth) / 1280.0f;
      int w = std::max(1, int(t.width * scale)), h = std::max(1, int(t.height * scale));
      if (dst.w != w || dst.h != h) CreateTarget(dst, w, h);
      glBindFramebuffer(GL_FRAMEBUFFER, src.fbo);
      glBindTexture(GL_TEXTURE_2D, dst.texture);
      glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, std::max(0, src.h - h), std::min(w, src.w), std::min(h, src.h));
    }
  }
  // Resolve com limpeza (D3DRESOLVE_CLEARRENDERTARGET, a confirmar: bit 8): limpa o alvo de origem.
  if (c.flags & 0x100) {
    glBindFramebuffer(GL_FRAMEBUFFER, src.fbo);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(((c.argb >> 16) & 255) / 255.f, ((c.argb >> 8) & 255) / 255.f, (c.argb & 255) / 255.f, (c.argb >> 24) / 255.f);
    glClear(GL_COLOR_BUFFER_BIT);
  }
}

void PresentToWindow() {
  auto it = g_targets.find(g_mainTarget);
  int ww = 0, wh = 0;
  SDL_GetWindowSizeInPixels(g_window, &ww, &wh);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, ww, wh);
  glDisable(GL_SCISSOR_TEST); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_BLEND);
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
      std::string tmp = std::string(capture) + ".tmp";
      if (FILE* f = std::fopen(tmp.c_str(), "wb")) {
        std::fprintf(f, "P6\n%d %d\n255\n", ww, wh);
        for (int y = wh - 1; y >= 0; --y) for (int x = 0; x < ww; ++x) std::fwrite(&px[(size_t(y) * ww + x) * 4], 1, 3, f);
        std::fclose(f);
        std::rename(tmp.c_str(), capture);
      }
    }
  }
  SDL_GL_SwapWindow(g_window);
}

void RenderThread() {
  if (!SDL_GL_MakeCurrent(g_window, g_context)) { std::fprintf(stderr, "[nier-gl] contexto: %s\n", SDL_GetError()); return; }
  SDL_GL_SetSwapInterval(1);
  CreateBlit();
  std::fprintf(stderr, "[nier-gl] GLES2 na thread de render: %s / %s; interno %dx%d\n",
               reinterpret_cast<const char*>(glGetString(GL_RENDERER)), reinterpret_cast<const char*>(glGetString(GL_VERSION)),
               g_config.renderWidth, g_config.renderHeight);
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
    for (const Cmd& c : work.cmds) {
      if (c.kind == Kind::Resolve) ReplayResolve(work, c);
      else { ++g_draws; ReplayDraw(work, c); }
    }
    PresentToWindow();
    auto now = Clock::now();
    g_lastFrameMs = std::chrono::duration<double, std::milli>(now - last).count();
    last = now;
    uint64_t n = ++g_frames;
    if (n % 120 == 0)
      std::fprintf(stderr, "[nier-gl] %llu quadros, %.2f ms, %llu draws, %llu pulados, intervalo do jogo %u, GL 0x%x\n",
                   (unsigned long long)n, g_lastFrameMs.load(), (unsigned long long)g_draws.load(),
                   (unsigned long long)g_skipped.load(), work.presentInterval, glGetError());
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
}  // namespace

bool Init(const Config& config, const GuestMemory& memory) {
  g_config = config;
  g_config.renderWidth = std::max(g_config.renderWidth, 640);  // regra: interno nunca abaixo de 640
  g_config.renderHeight = std::max(g_config.renderHeight, g_config.renderWidth * 9 / 16);
  g_mem = memory;
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);  // profundidade fica nos FBOs internos
  g_window = SDL_CreateWindow("NieR", 1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
  if (!g_window || !(g_context = SDL_GL_CreateContext(g_window))) {
    std::fprintf(stderr, "[nier-gl] janela/contexto: %s\n", SDL_GetError());
    return false;
  }
  SDL_GL_MakeCurrent(g_window, nullptr);  // a thread de render assume o contexto
  g_quit = false;
  g_thread = std::thread(RenderThread);
  g_enabled = true;
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
  const uint8_t* state = Virtual(device + kStateBegin, kStateSize);
  if (!state || !count) return;
  Cmd c{Kind::Draw};
  c.primitive = primitive; c.baseVertex = baseVertex; c.startIndex = startIndex; c.count = count; c.indexed = indexed;
  c.state = Append(g_building, state, kStateSize);
  uint32_t first = baseVertex, vertexCount = count;
  if (indexed) {
    // Objeto IB (+0x3144): palavra 0 bit 31 = índices de 32 bits; +0x18 = endereço dos dados.
    uint32_t ib = BE32(Field(state, kIndexBuffer));
    const uint8_t* object = ib ? Virtual(ib, 0x20) : nullptr;
    if (!object) return;
    c.index32 = BE32(object) & 0x80000000u;
    uint32_t address = BE32(object + 0x18), bytes = c.index32 ? 4 : 2;
    uint32_t physical = (address & 0x1FFFFFFF) + (address >= 0xE0000000 ? 0x1000 : 0);
    const uint8_t* src = Physical(physical + startIndex * bytes, count * bytes);
    if (!src) return;
    c.indices = Append(g_building, src, count * bytes);
    const uint8_t* copy = g_building.blob.data() + c.indices;
    uint32_t lo = 0xFFFFFFFF, hi = 0;
    for (uint32_t i = 0; i < count; ++i) {
      uint32_t v = c.index32 ? BE32(copy + 4 * i) : uint32_t(copy[2 * i]) << 8 | copy[2 * i + 1];
      if (v == 0xFFFF || v == 0xFFFFFFFF) continue;  // reinício de primitiva
      lo = std::min(lo, v); hi = std::max(hi, v);
    }
    if (lo > hi) return;
    first = baseVertex + lo; vertexCount = hi - lo + 1;
  }
  SnapshotVertices(g_building, c, g_building.blob.data() + c.state, first, vertexCount);
  g_recordedBytes += g_building.blob.size();
  g_building.cmds.push_back(c);
}

void RecordDrawUP(uint32_t device, uint32_t primitive, uint32_t vertexCount, const uint8_t* data, uint32_t stride) {
  const uint8_t* state = Virtual(device + kStateBegin, kStateSize);
  if (!state || !data || !stride || !vertexCount || vertexCount > 65536) return;
  Cmd c{Kind::DrawUP};
  c.primitive = primitive; c.count = vertexCount; c.stride = stride;
  c.state = Append(g_building, state, kStateSize);
  c.vertexBytes = vertexCount * stride;
  c.vertices = Append(g_building, data, c.vertexBytes);
  g_building.cmds.push_back(c);
}

void RecordResolve(uint32_t device, uint32_t flags, const int32_t* rect, uint32_t destTexture, uint32_t clearColorArgb) {
  const uint8_t* state = Virtual(device + kStateBegin, kStateSize);
  if (!state || (flags & 0x70)) return;  // só cor; profundidade (bits 4–6) fica na GPU nossa
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

void Present() {
  if (!g_enabled) return;
  std::unique_lock lock(g_queueMutex);
  g_queueCv.wait(lock, [] { return !g_hasInflight || g_quit; });
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
