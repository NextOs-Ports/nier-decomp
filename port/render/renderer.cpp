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
#include "vertex_cache.h"

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

using buffers::ConvertInput;

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
  buffers::Ref vertexData[kMaxInputs], indexData;
  uint64_t indexAddress = 0;
  size_t streamInputs[kMaxInputs]{}, streamIndices = 0;
  uint32_t residentInputs = 0;
  bool residentIndices = false;
  uint32_t inputCount = 0, vertexCount = 0, indexCount = 0;
  uint32_t flags = 0, argb = 0;
  int32_t rect[4] = {};
  bool hasRect = false;
  bool up = false;  // BeginVertices (2D/UI); falso = DrawVertices/DrawIndexedVertices
};
struct Frame {
  std::vector<uint8_t> blob;
  std::vector<Cmd> cmds;
  uint32_t presentInterval = 1;
  size_t bufferBytes = 0; // conservative accounting, including references shared by draws
  void Clear() { blob.clear(); cmds.clear(); bufferBytes = 0; }
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
buffers::Cache g_bufferSnapshots;
uint64_t g_bufferRecordingFrame = 0; // recording thread, distinct Present boundaries
std::atomic<bool> g_vertexHalf{false};
bool ReferenceBuffers() { static bool value = std::getenv("NIER_BUFFER_REFERENCE") != nullptr; return value; }


// Teto do quadro em construção: se o jogo desenha muito sem Present (ex.: tela de carregamento), o quadro
// pendente é descartado em vez de crescer sem limite (cada realocação copiaria centenas de MB).
std::atomic<uint64_t> g_droppedFrames{0};
void GuardBuilding() {
  if (g_building.blob.size() + g_building.bufferBytes < (192u << 20) && g_building.cmds.size() < 100000) return;
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
struct Target {
  GLuint texture = 0, fbo = 0, depth = 0; int w = 0, h = 0; uint64_t clearedFrame = ~0ull; uint64_t drew3DFrame = ~0ull; bool from3D = false;
  // Resolve sem cópia (C1-G0): depois de um Resolve de tela cheia a textura do alvo passa ao destino (troca de
  // texturas entre FBOs, ping-pong) e `holder` guarda a chave em g_resolved onde está o conteúdo mais recente;
  // um desenho novo no alvo zera `holder`. Passes menores (bloom) desenham direto no FBO do destino.
  uint64_t holder = 0;
  bool shared = false;  // textura/FBO emprestados de outro destino (mesmo conteúdo): nunca apagar nem desenhar nela
};
std::map<uint64_t, Target> g_targets;
std::map<uint64_t, Target> g_resolved;
uint64_t g_mainTarget = 0;
// Resolve preguiçoso (Mali: cada glCopyTexSubImage esvazia o tile e custa ms): a cópia só acontece se a
// textura destino for amostrada ou antes de o alvo de origem ser alterado; sem uso no quadro, é descartada.
GLuint g_blitProgram = 0, g_blitBuffer = 0, g_vertexBuffer = 0, g_indexBuffer = 0;
GLint g_blitRect = -1, g_blitFlip = -1;
bool GlProfile() { static bool on = std::getenv("NIER_GL_PROFILE") != nullptr; return on; }
struct PendingCopy { uint64_t src, dst; int x0, y0, w, h; bool srcResolved; };
std::vector<PendingCopy> g_pendingCopies;
std::set<uint64_t> g_frameSampled, g_everSampled;
uint32_t g_copiesDone = 0, g_copiesSkipped = 0;
// Cópia parcial de Resolve = quad com a textura de origem desenhado no FBO do destino (blit pela GPU, como o
// Rayman no mesmo .164). Sem glGet (no Mali sincroniza o pipeline) e sem glCopyTexSubImage (grava e relê a
// tela). O desenho seguinte religa FBO/programa/estado por conta própria (ReplayDraw/CurrentTarget).
void DoCopy(const PendingCopy& p) {
  auto& srcMap = p.srcResolved ? g_resolved : g_targets;
  auto s = srcMap.find(p.src); auto d = g_resolved.find(p.dst);
  if (s == srcMap.end() || d == g_resolved.end() || !s->second.texture || !d->second.fbo) return;
  Target& src = s->second; Target& dst = d->second;
  glBindFramebuffer(GL_FRAMEBUFFER, dst.fbo);
  glViewport(0, 0, dst.w, dst.h);
  glDisable(GL_SCISSOR_TEST); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_BLEND);
  glDepthMask(GL_FALSE);
  glColorMask(1, 1, 1, 1);
  glUseProgram(g_blitProgram);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, src.texture);
  glUniform4f(g_blitRect, float(p.w) / src.w, float(p.h) / src.h, float(p.x0) / src.w, float(p.y0) / src.h);
  glUniform1f(g_blitFlip, 0.0f);
  glBindBuffer(GL_ARRAY_BUFFER, g_blitBuffer);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
  for (uint32_t i = 1; i < kMaxInputs; ++i) glDisableVertexAttribArray(i);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  dst.from3D = src.drew3DFrame == g_frames.load();
  ++g_copiesDone;
  if (GlProfile()) { static int n = 0; if (n++ < 12) std::fprintf(stderr, "[nier-perfil] cópia parcial: origem %dx%d%s janela %d,%d %dx%d -> destino %dx%d (chave %016llX)\n", src.w, src.h, p.srcResolved ? " (resolvida)" : "", p.x0, p.y0, p.w, p.h, dst.w, dst.h, (unsigned long long)p.dst); }
}
uint32_t g_aliases = 0, g_directDraws = 0, g_directPasses = 0;
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
std::map<std::string, uint32_t> g_skipReasons;
bool g_depth24 = false;

void Skip(const char* reason) {
  ++g_skipped;
  if (++g_skipReasons[reason] == 1) std::fprintf(stderr, "[nier-gl] desenho pulado: %s\n", reason);
}

GLuint CompileShader(GLenum type, const char* source, const char* name) {
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &source, nullptr);
  glCompileShader(s);  // LEVE_OK: compilar só na 1ª vez do programa (cache g_programs por chave VS/PS) e no init do blit
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
  // r = (escala.xy, deslocamento.xy) da janela de origem em UV; flip = 1 desvira (apresentação), 0 copia reto (Resolve).
  const char* vs = "attribute vec2 p; varying vec2 uv; uniform vec4 r; uniform float flip; void main(){ vec2 q = p * 0.5 + 0.5; q.y = mix(q.y, 1.0 - q.y, flip); uv = q * r.xy + r.zw; gl_Position = vec4(p, 0.0, 1.0); }";
  const char* fs = "precision mediump float; varying vec2 uv; uniform sampler2D t; void main(){ gl_FragColor = vec4(texture2D(t, uv).rgb, 1.0); }";
  GLuint v = CompileShader(GL_VERTEX_SHADER, vs, "blit"), f = CompileShader(GL_FRAGMENT_SHADER, fs, "blit");
  g_blitProgram = glCreateProgram();
  glAttachShader(g_blitProgram, v); glAttachShader(g_blitProgram, f);
  glBindAttribLocation(g_blitProgram, 0, "p");
  glLinkProgram(g_blitProgram);  // LEVE_OK: inicialização (CreateBlit, uma vez)
  glDeleteShader(v); glDeleteShader(f);
  g_blitRect = glGetUniformLocation(g_blitProgram, "r");
  g_blitFlip = glGetUniformLocation(g_blitProgram, "flip");
  const float quad[] = {-1, -1, 1, -1, -1, 1, 1, 1};
  glGenBuffers(1, &g_blitBuffer);
  glBindBuffer(GL_ARRAY_BUFFER, g_blitBuffer);
  glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
  glGenBuffers(1, &g_vertexBuffer);
  glGenBuffers(1, &g_indexBuffer);
  const char* ext = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
  g_depth24 = ext && std::strstr(ext, "GL_OES_depth24");
}

void ReleaseTarget(Target& t) {
  if (!t.shared) {
    if (t.texture) glDeleteTextures(1, &t.texture);
    if (t.fbo) glDeleteFramebuffers(1, &t.fbo);
    if (t.depth) glDeleteRenderbuffers(1, &t.depth);
  }
  t = Target{};
}
void CreateTarget(Target& t, int w, int h, bool withDepth) {
  ReleaseTarget(t);
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
// Chave de textura por endereço físico (o objeto pode trazer bits altos de mapeamento, ex. 0xF105…).
// Endereço ≥ 0xE0000000 (heap física de 4 KB) corresponde a físico + 0x1000 (o mesmo do index buffer).
uint32_t PhysicalOf(uint32_t base) { return (base & 0x1FFFFFFF) + (base >= 0xE0000000 ? 0x1000 : 0); }
uint64_t TexKey(const xenos::TextureFetch& t) { return uint64_t(PhysicalOf(t.base)) << 32 | uint64_t(t.width) << 16 | t.height; }

// Alvo de cor atual do guest (chave RB_SURFACE_INFO/RB_COLOR_INFO), na resolução interna.
// Limpeza única por quadro de um alvo (cor e profundidade), com o FBO já ligado.
void ClearOncePerFrame(Target& t) {
  if (t.clearedFrame == g_frames.load()) return;
  t.clearedFrame = g_frames.load();
  glDisable(GL_SCISSOR_TEST);
  glDepthMask(GL_TRUE);
  glColorMask(1, 1, 1, 1);
  glClearDepthf(1.0f);
  glClearColor(0, 0, 0, 1);
  glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
}
// FBO próprio do destino de Resolve (passe desenhado direto, sem cópia): tamanho do destino na escala interna.
Target& DirectTarget(uint64_t key, int w, int h) {
  Target& t = g_resolved[key];
  if (t.w != w || t.h != h || !t.depth || t.shared) CreateTarget(t, w, h, true);
  glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
  ClearOncePerFrame(t);
  return t;
}
Target& CurrentTarget(const uint8_t* state, bool forDraw = true) {
  uint32_t surface = BE32(Field(state, kSurfaceInfo)), color = BE32(Field(state, kColorInfo));
  uint64_t key = uint64_t(color & 0xF0FFF) << 16 | (surface & 0x3FFF);
  uint32_t guestPitch = std::max<uint32_t>(surface & 0x3FFF, 1);
  int w = std::max(1, int(guestPitch * Scale() + 0.5f)), h = std::max(1, int(720 * Scale() + 0.5f));
  if (!g_pendingCopies.empty()) FlushCopiesFrom(key);
  Target& t = g_targets[key];
  if (t.w != w || t.h != h) CreateTarget(t, w, h, true);
  if (forDraw) t.holder = 0;  // o conteúdo mais novo volta a ser a textura do próprio alvo
  if ((key & 0x3FFF) > (g_mainTarget & 0x3FFF)) g_mainTarget = key;
  glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
  // O ClearF do 9328 ainda não foi localizado e os Resolve do jogo não pedem limpeza (flags 0/4):
  // cor e profundidade são limpas na 1ª vez do alvo em cada quadro (sem isso, passes aditivos que
  // realimentam a tela resolvida saturam para branco).
  ClearOncePerFrame(t);
  return t;
}

// ---- texturas
// Conferência de mudança com recuo (regime ≤ 1 ms/quadro na Mali): textura que não muda é conferida a cada
// 2, 4, … 64 quadros; a que já mudou alguma vez, no máximo a cada 8 (texto novo aparece em < 0,3 s a 30 fps);
// a que muda sempre (planos de vídeo) volta a ser conferida todo quadro.
struct Texture { GLuint id = 0; uint64_t signature = 0; uint32_t uploads = 0; uint64_t checkedFrame = ~0ull; GLenum glFormat = 0; uint64_t nextCheck = 0; uint32_t stable = 0; };
std::unordered_map<uint64_t, Texture> g_textures;
std::vector<uint8_t> g_pixels;
GLuint g_white = 0;

// Conteúdo inteiro até 2 MB (atlas de fonte e texturas escritas pela CPU mudam em poucos bytes);
// acima disso, 64 amostras. Pendente para a Mali: trocar por write watch da memória física (ReXGlue).
uint64_t Signature(uint32_t base, uint32_t span, bool sampled = false) {
  uint64_t h = 1469598103934665603ull ^ span;
  if (!sampled && span <= (2u << 20)) {
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
double Ms(Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); }
double g_textureMs = 0, g_sigMs = 0, g_decodeMs = 0, g_uploadMs = 0, g_replayMs = 0, g_swapMs = 0;
std::atomic<uint64_t> g_gameWaitNs{0};  // tempo da thread de render em decodificação/envio de texturas (por janela de log)
GLuint TextureForImpl(const xenos::TextureFetch& t, bool allowResolved);
GLuint TextureFor(const xenos::TextureFetch& t, bool allowResolved = true) {
  auto t0 = Clock::now();
  GLuint id = TextureForImpl(t, allowResolved);
  g_textureMs += std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
  return id;
}
GLuint TextureForImpl(const xenos::TextureFetch& t, bool allowResolved) {
  uint64_t key = TexKey(t);
  if (auto r = g_resolved.find(key); r != g_resolved.end() && (allowResolved || r->second.from3D)) {
    g_everSampled.insert(key);
    if (!g_pendingCopies.empty()) FlushCopyTo(key);
    return r->second.texture;
  }
  if (t.format == xenos::k248) {
    // Profundidade resolvida (mapa de sombra 1024x1024 / cena 1280x720): o Resolve de profundidade não é
    // emulado; branco = "sem sombra" nos PS que comparam. Não é desenho pulado.
    static bool once = false;
    if (!once) { once = true; std::fprintf(stderr, "[nier-gl] textura de profundidade (fmt 22) %ux%u amostrada: branco (sem sombra)\n", t.width, t.height); }
    return White();
  }
  if (!xenos::Supported(t.format) || t.dimension != 1) {
    static std::map<uint32_t, uint32_t> seen;
    uint32_t k = t.format | t.dimension << 8;
    if (++seen[k] == 1) std::fprintf(stderr, "[nier-gl] textura sem suporte: formato %u dimensão %u (%ux%u)\n", t.format, t.dimension, t.width, t.height);
    Skip("formato/dimensão de textura não suportado (branco)"); return White();
  }
  Texture& tex = g_textures[key ^ uint64_t(t.format) << 58];
  uint64_t frame = g_frames.load();
  if (tex.id && (tex.checkedFrame == frame || frame < tex.nextCheck)) return tex.id;
  tex.checkedFrame = frame;
  uint32_t bpe = xenos::BytesPerElement(t.format);
  bool block = xenos::Block(t.format);
  uint32_t bw = block ? (t.width + 3) / 4 : t.width, bh = block ? (t.height + 3) / 4 : t.height;
  uint32_t pitchE = std::max(block ? t.pitch / 4 : t.pitch, bw);
  uint32_t span = ((pitchE + 31) & ~31u) * ((bh + 31) & ~31u) * bpe;
  auto ts = Clock::now();
  // Planos de vídeo (k8 reenviado): o quadro novo muda tudo, 64 amostras bastam e poupam ~1,4 MB de leitura.
  uint64_t sig = Signature(t.base, span, t.format == xenos::k8 && tex.uploads >= 2);
  auto td = Clock::now();
  g_sigMs += std::chrono::duration<double, std::milli>(td - ts).count();
  if (tex.id && sig == tex.signature) {
    tex.stable = std::min<uint32_t>(tex.stable + 1, tex.uploads > 1 ? 3 : 6);
    tex.nextCheck = frame + (1ull << tex.stable);
    return tex.id;
  }
  tex.stable = 0; tex.nextCheck = frame + 1;
  if (t.format == xenos::k8) { static int n = 0; if (n++ < 4) std::fprintf(stderr, "[nier-k8] %ux%u swizzle %03X endian %u tiled %d\n", t.width, t.height, t.swizzle, t.endian, int(t.tiled)); }
  // k8 com swizzle identidade (planos de vídeo): caminho rápido direto para LUMINANCE_ALPHA.
  td = Clock::now();
  static std::vector<uint8_t> lumAlpha;
  bool lum = t.format == xenos::k8 && t.swizzle == 0xA00 && xenos::DecodeK8Luminance(Physical, t, lumAlpha);
  bool la = !lum && t.format == xenos::k8 && t.swizzle == 0x688 && xenos::DecodeK8LumAlpha(Physical, t, lumAlpha);
  if ((la || lum) && std::getenv("NIER_K8_CHECK")) {  // diagnóstico: compara com o decodificador genérico
    static int checks = 0;
    if (checks++ < 40 && xenos::DecodeLevel0(Physical, t, g_pixels)) {
      size_t bad = 0;
      for (size_t i = 0, n = size_t(t.width) * t.height; i < n; ++i) bad += lumAlpha[(lum ? 1 : 2) * i] != g_pixels[4 * i];
      std::fprintf(stderr, "[nier-k8] %ux%u diferenças %zu\n", t.width, t.height, bad);
    }
  }
  if (!la && !lum && !xenos::DecodeLevel0(Physical, t, g_pixels)) { Skip("decodificação de textura (branco)"); return tex.id ? tex.id : White(); }
  if (const char* dump = std::getenv("NIER_DUMP_TEXTURES"); dump && !la && !lum && tex.uploads < 3) {  // diagnóstico
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
  const uint8_t* upload = (la || lum) ? lumAlpha.data() : g_pixels.data();
  GLenum glFormat = lum ? GL_LUMINANCE : la ? GL_LUMINANCE_ALPHA : GL_RGBA;
  if (fresh || tex.glFormat != glFormat) glTexImage2D(GL_TEXTURE_2D, 0, GLint(glFormat), GLsizei(t.width), GLsizei(t.height), 0, glFormat, GL_UNSIGNED_BYTE, upload);
  else glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, GLsizei(t.width), GLsizei(t.height), glFormat, GL_UNSIGNED_BYTE, upload);
  tex.glFormat = glFormat;
  ++tex.uploads;
  g_uploadMs += std::chrono::duration<double, std::milli>(Clock::now() - tu).count();
  // Mali-450: textura minificada sem mipmap satura a banda e segura a CPU (medido no Rayman).
  bool pot = (t.width & (t.width - 1)) == 0 && (t.height & (t.height - 1)) == 0;
  if (g_config.mipmaps && pot && tex.uploads == 1 && (t.width > 16 || t.height > 16)) {
    glGenerateMipmap(GL_TEXTURE_2D);  // LEVE_OK: só no 1º envio da textura (tex.uploads == 1), textura estática POT
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
  std::vector<float> lastVc, lastPc;  // último banco enviado: glUniform4fv só quando muda (estado ≤ 3 ms na Mali)
};
std::unordered_map<uint64_t, Program> g_programs;

Program* ProgramFor(const GuestShader* vs, const GuestShader* ps, bool flat = false) {
  uint64_t key = (vs->key * 0x9E3779B97F4A7C15ull ^ ps->key) + (flat ? 1 : 0);
  auto it = g_programs.find(key);
  if (it != g_programs.end()) return it->second.id ? &it->second : nullptr;
  Program& p = g_programs[key];
  char vn[40], pn[40];
  std::snprintf(vn, sizeof vn, "%016llX_vs", (unsigned long long)vs->key);
  std::snprintf(pn, sizeof pn, "%016llX_ps", (unsigned long long)ps->key);
  static const char* flatPs = "#version 100\nprecision mediump float;\nvoid main(){ gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0); }\n";
  GLuint v = CompileShader(GL_VERTEX_SHADER, vs->source.c_str(), vn), f = CompileShader(GL_FRAGMENT_SHADER, flat ? flatPs : ps->source.c_str(), pn);
  if (!v || !f) { if (v) glDeleteShader(v); if (f) glDeleteShader(f); return nullptr; }
  p.id = glCreateProgram();
  glAttachShader(p.id, v); glAttachShader(p.id, f);
  for (uint32_t i = 0; i < vs->inputs.size() && i < kMaxInputs; ++i) glBindAttribLocation(p.id, i, ("a" + std::to_string(i)).c_str());
  glLinkProgram(p.id);  // LEVE_OK: ligar só na 1ª vez do programa (cache g_programs)
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
void UploadConstants(GLint location, int count, const uint8_t* bank, const GuestShader* s, std::vector<float>& last) {
  if (location < 0) return;
  size_t n = std::min<size_t>(size_t(count), s->constMap.size());
  g_constants.resize(n * 4);
  size_t literals = s->literals.size() / 4;
  for (size_t j = 0; j < n; ++j) {
    size_t reg = s->constMap[j];
    if (reg >= 256 - literals) std::memcpy(&g_constants[j * 4], &s->literals[(reg - (256 - literals)) * 4], 16);
    else for (int c = 0; c < 4; ++c) g_constants[j * 4 + c] = BEF(bank + reg * 16 + 4 * c);
  }
  if (last.size() == g_constants.size() && std::memcmp(last.data(), g_constants.data(), g_constants.size() * 4) == 0) return;
  last = g_constants;
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

// GL objects belong exclusively to the render thread. A serial identifies an
// immutable snapshot, not a guest address that the CPU can overwrite later.
struct CachedBuffer { GLuint id; size_t bytes; std::weak_ptr<const buffers::Payload> data; uint64_t used; };
std::unordered_map<uint64_t, CachedBuffer> g_bufferObjects;
size_t g_bufferObjectBytes = 0;
uint64_t g_bufferHits = 0, g_bufferUploads = 0, g_bufferUploadBytes = 0;
struct StreamSlot { GLuint vertex = 0, index = 0; };
std::array<StreamSlot, 3> g_streamSlots;
std::vector<uint8_t> g_streamVertices, g_streamIndices;
uint64_t g_streamUploads = 0, g_streamBytes = 0;
void InitStreamBuffers() {
  for (auto& slot : g_streamSlots) {
    glGenBuffers(1, &slot.vertex); glGenBuffers(1, &slot.index);
  }
}
void PrepareFrameStreams(Frame& frame) {
  if (ReferenceBuffers()) return;
  auto started = Clock::now();
  size_t vertexBytes = 0, indexBytes = 0;
  auto aligned = [](size_t size) { return (size + 15) & ~size_t(15); };
  // Freeze promotion decisions for the entire frame before any draw is issued.
  for (auto& c : frame.cmds) {
    if (c.kind != Kind::Draw) continue;
    c.residentInputs = 0;
    for (uint32_t i = 0; i < c.inputCount; ++i) {
      const auto& data = c.vertexData[i];
      if (data && data->cacheable.load(std::memory_order_relaxed)) c.residentInputs |= 1u << i;
      else {
        c.streamInputs[i] = vertexBytes;
        vertexBytes += aligned(data ? data->bytes.size() : size_t(c.vertexCount) * 16);
      }
    }
    c.residentIndices = c.indexData && c.indexData->cacheable.load(std::memory_order_relaxed);
    if (!c.residentIndices) { c.streamIndices = indexBytes; indexBytes += aligned(size_t(c.indexCount) * 2); }
  }
  g_streamVertices.resize(vertexBytes); g_streamIndices.resize(indexBytes);
  for (const auto& c : frame.cmds) {
    if (c.kind != Kind::Draw) continue;
    for (uint32_t i = 0; i < c.inputCount; ++i) if (!(c.residentInputs & (1u << i))) {
      const auto& data = c.vertexData[i];
      std::memcpy(g_streamVertices.data() + c.streamInputs[i], data ? data->bytes.data() : frame.blob.data() + c.inputs[i],
                  data ? data->bytes.size() : size_t(c.vertexCount) * 16);
    }
    if (!c.residentIndices) std::memcpy(g_streamIndices.data() + c.streamIndices,
      c.indexData ? c.indexData->bytes.data() : frame.blob.data() + c.indices, size_t(c.indexCount) * 2);
  }
  auto& slot = g_streamSlots[g_frames.load() % g_streamSlots.size()];
  // One orphan+upload per nonempty VBO/IBO, BEFORE the first draw of this frame.
  // Never update an in-flight backing store with glBufferSubData on Mali-450.
  if (vertexBytes) {
    glBindBuffer(GL_ARRAY_BUFFER, slot.vertex);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(vertexBytes), g_streamVertices.data(), GL_STREAM_DRAW);
    ++g_streamUploads; g_streamBytes += vertexBytes;
  }
  if (indexBytes) {
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, slot.index);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(indexBytes), g_streamIndices.data(), GL_STREAM_DRAW);
    ++g_streamUploads; g_streamBytes += indexBytes;
  }
  g_pBuffers += Ms(started, Clock::now()); // include CPU staging AND both uploads in the budget
}
void BindSnapshot(GLenum target, const buffers::Ref& data) {
  auto it = g_bufferObjects.find(data->serial);
  if (it == g_bufferObjects.end()) {
    GLuint id; glGenBuffers(1, &id); glBindBuffer(target, id);
    glBufferData(target, GLsizeiptr(data->bytes.size()), data->bytes.data(), GL_STATIC_DRAW);
    it = g_bufferObjects.emplace(data->serial, CachedBuffer{id, data->bytes.size(), data, g_frames.load()}).first;
    g_bufferObjectBytes += data->bytes.size(); ++g_bufferUploads; g_bufferUploadBytes += data->bytes.size();
  } else { glBindBuffer(target, it->second.id); ++g_bufferHits; }
  it->second.used = g_frames.load();
}
void TrimBufferObjects(bool all = false) {
  for (auto it = g_bufferObjects.begin(); it != g_bufferObjects.end();) {
    if (all || (it->second.data.expired() && g_frames.load() - it->second.used >= 3)) {
      glDeleteBuffers(1, &it->second.id); g_bufferObjectBytes -= it->second.bytes;
      it = g_bufferObjects.erase(it);
    } else ++it;
  }
  // Evict only between frames: no attribute in an unfinished draw loses its VBO.
  if (g_bufferObjectBytes > (64u << 20)) {
    std::vector<std::pair<uint64_t, uint64_t>> order;
    for (auto& [key, value] : g_bufferObjects) order.push_back({value.used, key});
    std::sort(order.begin(), order.end());
    for (auto [age, key] : order) {
      if (g_bufferObjectBytes <= (64u << 20)) break;
      auto it = g_bufferObjects.find(key);
      glDeleteBuffers(1, &it->second.id); g_bufferObjectBytes -= it->second.bytes; g_bufferObjects.erase(it);
    }
  }
}

bool g_bufferAuditReference = false; // render-thread-only diagnostic
struct Redirect { uint64_t key = 0; int w = 0, h = 0; bool passStart = false; };
void ReplayDraw(const Frame& f, const Cmd& c, const Redirect& rd) {
  const uint8_t* state = f.blob.data() + c.state;
  auto t0 = Clock::now();
  Target& drawTarget = rd.key ? DirectTarget(rd.key, rd.w, rd.h) : CurrentTarget(state);
  if (rd.key) { ++g_directDraws; if (rd.passStart) drawTarget.from3D = false; if (!c.up) drawTarget.from3D = true; }
  if (!c.up) drawTarget.drew3DFrame = g_frames.load();
  auto t1 = Clock::now();
  g_pTarget += Ms(t0, t1);
  ApplyFixedState(state);
  static bool flat3d = std::getenv("NIER_FLAT_3D") != nullptr;  // diagnóstico: 3D em vermelho sólido
  Program* p = ProgramFor(c.vs, c.ps, flat3d && !c.up);
  if (!p) { Skip("programa não compilou/linkou"); return; }
  glUseProgram(p->id);
  UploadConstants(p->vc, p->vcCount, Field(state, kVertexConstants), c.vs, p->lastVc);
  UploadConstants(p->pc, p->pcCount, Field(state, kPixelConstants), c.ps, p->lastPc);
  for (auto [b, loc] : p->bools) glUniform1i(loc, (BE32(Field(state, kBoolConstants + 4 * (b >> 5))) >> (b & 31)) & 1);
  uint32_t colorControl = BE32(Field(state, kColorControl));
  if (p->alphaFunc >= 0) glUniform1i(p->alphaFunc, (colorControl & 8) ? int(colorControl & 7) : 7);
  if (p->alphaRef >= 0) glUniform1f(p->alphaRef, BEF(Field(state, 0x2904)));
  int unit = 0;
  for (auto [slot, sizeLoc] : p->samplers) {
    const uint8_t* fetch = Field(state, kFetchConstants + 24 * slot);
    auto t = xenos::DecodeFetch(fetch);
    glActiveTexture(GL_TEXTURE0 + unit);
    // Heurística (07/11): a tela resolvida alimenta desenhos não-UP e também UP quando a origem recebeu
    // desenho 3D no quadro (composição da cena). Tela de UI pura vem da memória do guest (menus sem faixas).
    static bool resolvedInUi = std::getenv("NIER_RESOLVED_IN_UI") != nullptr;
    glBindTexture(GL_TEXTURE_2D, (BE32(fetch) & 3) == 2 ? TextureFor(t, !c.up || resolvedInUi) : White());
    if (sizeLoc >= 0) glUniform2f(sizeLoc, float(t.width), float(t.height));
    ++unit;
  }
  glActiveTexture(GL_TEXTURE0);
  auto t2 = Clock::now();
  g_pState += Ms(t1, t2);
  size_t indexOffset = 0;
  if (!ReferenceBuffers() && !g_bufferAuditReference) {
    const auto& stream = g_streamSlots[g_frames.load() % g_streamSlots.size()];
    bool streamBound = false;
    for (uint32_t i = 0; i < c.inputCount; ++i) {
      const auto& data = c.vertexData[i];
      auto layout = data ? data->layout : buffers::Layout{};
      size_t offset = 0;
      if (c.residentInputs & (1u << i)) { BindSnapshot(GL_ARRAY_BUFFER, data); streamBound = false; }
      else {
        if (!streamBound) glBindBuffer(GL_ARRAY_BUFFER, stream.vertex);
        streamBound = true; offset = c.streamInputs[i];
      }
      glEnableVertexAttribArray(i);
      glVertexAttribPointer(i, layout.components, layout.type, layout.normalized ? GL_TRUE : GL_FALSE,
                            layout.bytes, reinterpret_cast<void*>(offset));
    }
    for (uint32_t i = c.inputCount; i < kMaxInputs; ++i) glDisableVertexAttribArray(i);
    if (c.residentIndices) BindSnapshot(GL_ELEMENT_ARRAY_BUFFER, c.indexData);
    else { glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, stream.index); indexOffset = c.streamIndices; }
  } else {
    glBindBuffer(GL_ARRAY_BUFFER, g_vertexBuffer);
    size_t bytes = size_t(c.vertexCount) * 16;
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(bytes * c.inputCount), nullptr, GL_STREAM_DRAW);
    for (uint32_t i = 0; i < c.inputCount; ++i) {
      const void* source = f.blob.data() + c.inputs[i];
      static std::vector<float> reference;
      if (c.vertexData[i]) {
        const auto& data = *c.vertexData[i]; reference.resize(size_t(c.vertexCount) * 4);
        ConvertInput(data.input, data.raw.data(), data.endian, 0, c.vertexCount, reference.data());
        source = reference.data();
      }
      glBufferSubData(GL_ARRAY_BUFFER, GLintptr(bytes * i), GLsizeiptr(bytes), source);
      glEnableVertexAttribArray(i);
      glVertexAttribPointer(i, 4, GL_FLOAT, GL_FALSE, 16, reinterpret_cast<void*>(bytes * i));
    }
    for (uint32_t i = c.inputCount; i < kMaxInputs; ++i) glDisableVertexAttribArray(i);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_indexBuffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(c.indexCount * 2), c.indexData ? c.indexData->raw.data() : f.blob.data() + c.indices, GL_STREAM_DRAW);
  }
  auto t3 = Clock::now();
  g_pBuffers += Ms(t2, t3);
  glDrawElements(GL_TRIANGLES, GLsizei(c.indexCount), GL_UNSIGNED_SHORT, reinterpret_cast<void*>(indexOffset));
  g_pDraw += Ms(t3, Clock::now());
}

uint64_t TargetKeyOf(const uint8_t* state) {
  uint32_t surface = BE32(Field(state, kSurfaceInfo)), color = BE32(Field(state, kColorInfo));
  return uint64_t(color & 0xF0FFF) << 16 | (surface & 0x3FFF);
}
// Janela do Resolve em unidades do guest: retângulo (left, top, right, bottom) ou o tamanho do destino.
void ResolveRect(const Cmd& c, const xenos::TextureFetch& t, int& x0, int& y0, int& w, int& h) {
  if (c.hasRect) { x0 = c.rect[0]; y0 = c.rect[1]; w = c.rect[2] - c.rect[0]; h = c.rect[3] - c.rect[1]; }
  else { x0 = 0; y0 = 0; w = int(t.width); h = int(t.height); }
}
// Pré-passagem do quadro (C1-G0): descobre os passes cujo Resolve cobre exatamente um destino menor que o alvo,
// a partir de (0,0), com todos os desenhos do passe dentro desse retângulo; esses desenhos vão direto ao FBO
// do destino e o Resolve vira apenas marcação. Dois Resolve seguidos do mesmo alvo sem desenho entre eles
// cancelam o redirecionamento (o segundo precisaria do conteúdo no alvo).
std::vector<Redirect> g_redirects;
std::vector<uint8_t> g_resolveDirect;
void PlanDirectPasses(const Frame& f) {
  g_redirects.assign(f.cmds.size(), Redirect{});
  g_resolveDirect.assign(f.cmds.size(), 0);
  static bool off = std::getenv("NIER_NO_DIRECT_RESOLVE") != nullptr;
  if (off) return;
  std::map<uint64_t, std::vector<size_t>> open;  // alvo -> desenhos desde o último Resolve desse alvo
  float s = Scale();
  for (size_t i = 0; i < f.cmds.size(); ++i) {
    const Cmd& c = f.cmds[i];
    if (c.kind == Kind::Draw) { open[TargetKeyOf(f.blob.data() + c.state)].push_back(i); continue; }
    if (c.kind != Kind::Resolve) continue;
    const uint8_t* state = f.blob.data() + c.state;
    uint64_t srcKey = TargetKeyOf(state);
    auto& draws = open[srcKey];
    if (c.fetch != SIZE_MAX && !draws.empty()) {
      auto t = xenos::DecodeFetch(f.blob.data() + c.fetch);
      int x0, y0, w, h; ResolveRect(c, t, x0, y0, w, h);
      uint32_t guestPitch = std::max<uint32_t>(BE32(Field(state, kSurfaceInfo)) & 0x3FFF, 1);
      bool smaller = t.width < guestPitch || t.height < 720;
      bool ok = x0 == 0 && y0 == 0 && w == int(t.width) && h == int(t.height) && smaller && t.width >= 8 && t.height >= 8;
      // próximo comando do mesmo alvo não pode ser outro Resolve sem desenho entre eles
      for (size_t j = i + 1; ok && j < f.cmds.size(); ++j) {
        const Cmd& n = f.cmds[j];
        if (n.kind == Kind::Skip) continue;
        uint64_t k = TargetKeyOf(f.blob.data() + n.state);
        if (k != srcKey) continue;
        if (n.kind == Kind::Resolve) ok = false;
        break;
      }
      for (size_t d : draws) {
        if (!ok) break;
        const uint8_t* st = f.blob.data() + f.cmds[d].state;
        float xs = std::fabs(BEF(Field(st, kViewport))), xo = BEF(Field(st, kViewport + 4));
        float ys = std::fabs(BEF(Field(st, kViewport + 8))), yo = BEF(Field(st, kViewport + 12));
        if (xo - xs < -0.5f || yo - ys < -0.5f || xo + xs > w + 0.5f || yo + ys > h + 0.5f) ok = false;
      }
      if (ok) {
        uint64_t key = TexKey(t);
        int tw = std::max(1, int(t.width * s)), th = std::max(1, int(t.height * s));
        for (size_t k = 0; k < draws.size(); ++k) g_redirects[draws[k]] = Redirect{key, tw, th, k == 0};
        g_resolveDirect[i] = 1;
        ++g_directPasses;
      }
    }
    draws.clear();
  }
}
void ReplayResolve(const Frame& f, const Cmd& c, bool direct) {
  const uint8_t* state = f.blob.data() + c.state;
  uint64_t srcKey = TargetKeyOf(state);
  bool clears = c.flags & 0x300;
  float s = Scale();
  if (c.fetch != SIZE_MAX) {
    auto t = xenos::DecodeFetch(f.blob.data() + c.fetch);
    static bool noCopy = std::getenv("NIER_NO_RESOLVE_COPY") != nullptr;  // diagnóstico
    uint64_t key = TexKey(t);
    if (direct) {
      // o passe já foi desenhado no FBO do destino (PlanDirectPasses); o alvo não recebe o conteúdo
      Target& src = g_targets[srcKey];
      src.holder = key;
    } else if (t.width >= 8 && t.height >= 8 && !noCopy) {
      ++g_resolveCopies;
      int x0, y0, w, h; ResolveRect(c, t, x0, y0, w, h);
      auto srcIt = g_targets.find(srcKey);
      if (srcIt == g_targets.end()) return;
      Target& src = srcIt->second;
      int dw = std::max(1, int(t.width * s)), dh = std::max(1, int(t.height * s));
      // substitui pendência anterior do mesmo destino (a antiga nunca foi usada)
      for (size_t i = 0; i < g_pendingCopies.size(); ++i)
        if (g_pendingCopies[i].dst == key) { g_pendingCopies.erase(g_pendingCopies.begin() + i); ++g_copiesSkipped; break; }
      bool full = x0 == 0 && y0 == 0 && w == int(t.width) && h == int(t.height) && dw == src.w && dh == src.h;
      if (full && !src.holder) {
        // Tela cheia: o destino fica com a própria textura do alvo (sem cópia); o alvo segue com a textura antiga
        // do destino (ping-pong) e o próximo desenho nele limpa/sobrescreve como antes.
        FlushCopiesFrom(srcKey);
        Target& dst = g_resolved[key];
        // destino com FBO completo (cor+profundidade) para a troca ser de FBO inteiro: zero chamadas GL
        if (dst.w != dw || dst.h != dh || !dst.texture || !dst.depth || dst.shared) CreateTarget(dst, dw, dh, true);
        std::swap(src.texture, dst.texture);
        std::swap(src.fbo, dst.fbo);
        std::swap(src.depth, dst.depth);
        // a textura que volta ao alvo vai ser desenhada: destinos que a emprestavam deixam de valer
        for (auto& [k, r] : g_resolved) if (r.shared && r.texture == src.texture) r = Target{};
        dst.from3D = src.drew3DFrame == g_frames.load();
        src.holder = key;
        ++g_aliases;
      } else if (src.holder && src.holder != key) {
        // conteúdo está numa textura resolvida (alias/passe direto anterior)
        Target& holder = g_resolved[src.holder];
        Target& dst = g_resolved[key];
        if (full && holder.w == dw && holder.h == dh && holder.texture) {
          // mesma tela inteira para um segundo destino: empresta a textura (sem cópia)
          ReleaseTarget(dst);
          dst = holder; dst.shared = true; dst.holder = 0;
          ++g_aliases;
        } else {
          if (dst.w != dw || dst.h != dh || dst.shared) CreateTarget(dst, dw, dh, false);
          if (g_frameSampled.count(key) || g_everSampled.count(key)) DoCopy(PendingCopy{src.holder, key, int(x0 * s), int(y0 * s), dw, dh, true});
          else ++g_copiesSkipped;
        }
      } else if (!src.holder) {
        Target& dst = g_resolved[key];
        if (dst.w != dw || dst.h != dh || dst.shared) CreateTarget(dst, dw, dh, false);
        // Retângulo parcial: copia só se o destino for amostrado (neste quadro ou alguma vez).
        if (g_frameSampled.count(key) || g_everSampled.count(key)) {
          g_pendingCopies.push_back({srcKey, key, int(x0 * s), int(y0 * s), dw, dh, false});
          FlushCopiesFrom(srcKey);  // cópia já (semântica do Resolve), só para destinos usados
        } else {
          ++g_copiesSkipped;      // ninguém amostra este destino: a cópia é pulada
        }
      }
    }
  }
  // Resolve com limpeza (D3DRESOLVE_CLEARRENDERTARGET 0x100 / CLEARDEPTHSTENCIL 0x200, a confirmar).
  if (clears) {
    Target& src = CurrentTarget(state, false);
    glBindFramebuffer(GL_FRAMEBUFFER, src.fbo);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(1, 1, 1, 1);
    glDepthMask(GL_TRUE);
    glClearColor(((c.argb >> 16) & 255) / 255.f, ((c.argb >> 8) & 255) / 255.f, (c.argb & 255) / 255.f, (c.argb >> 24) / 255.f);
    glClearDepthf(1.0f);
    glClear(((c.flags & 0x100) ? GL_COLOR_BUFFER_BIT : 0) | ((c.flags & 0x200) ? GL_DEPTH_BUFFER_BIT : 0));
  }
}

// Opt-in frozen-command image audit. Two reference replays must first agree;
// otherwise temporal framebuffer/texture dependencies make this test inconclusive.
void AuditBufferImage(const Frame& work) {
  static const char* output = std::getenv("NIER_BUFFER_AUDIT");
  if (!output || ReferenceBuffers()) return;
  static auto start = Clock::now(); static bool done = false;
  if (done || Ms(start, Clock::now()) < 70000) return;
  size_t sceneDraws = 0;
  for (const auto& c : work.cmds) if (c.kind == Kind::Draw && !c.up) ++sceneDraws;
  if (sceneDraws < 50) return;
  auto originalTarget = g_targets.find(g_mainTarget);
  if (originalTarget == g_targets.end()) return;
  auto logicalTarget = [&](const Target& stored) -> const Target& {
    if (stored.holder) {
      auto it = g_resolved.find(stored.holder);
      if (it != g_resolved.end() && it->second.texture) return it->second;
    }
    return stored;
  };
  const auto& target = logicalTarget(originalTarget->second);
  std::vector<uint8_t> originalMain(size_t(target.w) * target.h * 4), finalMain;
  glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);
  glReadPixels(0, 0, target.w, target.h, GL_RGBA, GL_UNSIGNED_BYTE, originalMain.data()); // LEVE_OK: diagnostic NIER_BUFFER_AUDIT only
  size_t litPixels = 0;
  for (size_t i = 0; i < originalMain.size(); i += 4)
    litPixels += std::max({originalMain[i], originalMain[i + 1], originalMain[i + 2]}) > 24;
  if (litPixels * 20 < originalMain.size() / 4) return; // wait past black fades/credits
  done = true;
  auto replay = [&](bool reference, const char* label) {
    g_bufferAuditReference = reference; g_mainTarget = 0;
    g_pendingCopies.clear();
    for (auto& [key, target] : g_targets) target.clearedFrame = ~0ull;
    for (size_t i = 0; i < work.cmds.size(); ++i) {  // mesmo plano de Resolve do quadro (g_redirects/g_resolveDirect)
      const Cmd& c = work.cmds[i];
      bool planned = i < g_redirects.size();
      if (c.kind == Kind::Draw) ReplayDraw(work, c, planned ? g_redirects[i] : Redirect{});
      else if (c.kind == Kind::Resolve) ReplayResolve(work, c, planned && g_resolveDirect[i] != 0);
    }
    g_bufferAuditReference = false;
    std::vector<uint8_t> pixels;
    auto readTargets = [&](const auto& targets, const char* kind) {
      for (const auto& [key, stored] : targets) {
        const auto& target = logicalTarget(stored);
        if (!target.fbo || target.w < 1 || target.h < 1) continue;
        std::vector<uint8_t> rgba(size_t(target.w) * target.h * 4);
        glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);
        glReadPixels(0, 0, target.w, target.h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data()); // LEVE_OK: diagnostic NIER_BUFFER_AUDIT only
        if (kind[0] == 'c' && key == g_mainTarget) finalMain = rgba;
        const uint64_t header[] = {key, uint64_t(target.w), uint64_t(target.h)};
        const auto* raw = reinterpret_cast<const uint8_t*>(header);
        pixels.insert(pixels.end(), raw, raw + sizeof header);
        pixels.insert(pixels.end(), rgba.begin(), rgba.end());
        if (label) {
          char suffix[128]; std::snprintf(suffix, sizeof suffix, "/buffer-%s-%s-%016llx.ppm", label, kind, (unsigned long long)key);
          std::string name = std::string(output) + suffix;
          if (FILE* file = std::fopen(name.c_str(), "wb")) {
            std::fprintf(file, "P6\n%d %d\n255\n", target.w, target.h);
            for (int y = target.h - 1; y >= 0; --y) for (int x = 0; x < target.w; ++x)
              std::fwrite(rgba.data() + (size_t(y) * target.w + x) * 4, 1, 3, file);
            std::fclose(file);
          }
        }
      }
    };
    readTargets(g_targets, "color"); readTargets(g_resolved, "resolve");
    return pixels;
  };
  auto ref0 = replay(true, nullptr), ref1 = replay(true, "reference"), candidate = replay(false, "candidate"), ref2 = replay(true, nullptr);
  size_t differences = 0;
  if (ref1.size() == candidate.size())
    for (size_t i = 0; i < ref1.size(); ++i) differences += ref1[i] != candidate[i];
  bool stable = !ref1.empty() && ref0 == ref1 && ref1 == ref2 && originalMain == finalMain;
  bool equal = stable && ref1 == candidate;
  std::fprintf(stderr, "[nier-buffer-audit] %s frame %llu scene_draws %zu targets %zu bytes %zu differences %zu stable_reference %d original_lit_pixels %zu stable_replays %d original_match %d GL 0x%x\n",
               equal ? "PASS" : stable ? "FAIL" : "INCONCLUSIVE", (unsigned long long)g_frames.load(), sceneDraws,
               g_targets.size() + g_resolved.size(), ref1.size(), differences, int(stable), litPixels, int(ref0 == ref1 && ref1 == ref2), int(originalMain == finalMain), glGetError()); // LEVE_OK: diagnostic NIER_BUFFER_AUDIT only

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
    GLuint shown = it->second.texture;
    if (it->second.holder) if (auto h = g_resolved.find(it->second.holder); h != g_resolved.end() && h->second.texture) shown = h->second.texture;
    glBindTexture(GL_TEXTURE_2D, shown);
    glUniform4f(g_blitRect, 1, 1, 0, 0);
    glUniform1f(g_blitFlip, 1.0f);
    glBindBuffer(GL_ARRAY_BUFFER, g_blitBuffer);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    for (uint32_t i = 1; i < kMaxInputs; ++i) glDisableVertexAttribArray(i);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  }
  // NIER_GLES2_CAPTURE=<arquivo.ppm>: grava o quadro apresentado (a cada 3 s) para prova automatizada.
  if (const char* capture = std::getenv("NIER_GLES2_CAPTURE")) {
    // NIER_GLES2_CAPTURE_EVERY=<s> (padrão 3), contado a partir do 1º quadro apresentado.
    static const int every = std::getenv("NIER_GLES2_CAPTURE_EVERY") ? std::max(1, std::atoi(std::getenv("NIER_GLES2_CAPTURE_EVERY"))) : 3;
    static Clock::time_point lastShot = every > 3 ? Clock::now() : Clock::time_point{};
    if (Clock::now() - lastShot > std::chrono::seconds(every) && ww > 0 && wh > 0) {
      lastShot = Clock::now();
      std::vector<uint8_t> px(size_t(ww) * wh * 4);
      glPixelStorei(GL_PACK_ALIGNMENT, 1);
      glReadPixels(0, 0, ww, wh, GL_RGBA, GL_UNSIGNED_BYTE, px.data());  // LEVE_OK: captura de prova opt-in (NIER_GLES2_CAPTURE), nunca em entrega
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
  InitStreamBuffers();
  const char* extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
  std::string extensionList = std::string(" ") + (extensions ? extensions : "") + " ";
  g_vertexHalf.store(extensionList.find(" GL_OES_vertex_half_float ") != std::string::npos);
  std::fprintf(stderr, "[nier-buffers] cache %s; vertex_half_float %d; CPU/GL cache budget 64 MiB each\n",
               ReferenceBuffers() ? "reference/off" : "on", int(g_vertexHalf.load()));
  GLint vsUniforms = 0, fsUniforms = 0;
  glGetIntegerv(GL_MAX_VERTEX_UNIFORM_VECTORS, &vsUniforms);  // LEVE_OK: inicialização da thread de render, uma vez
  glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_VECTORS, &fsUniforms);  // LEVE_OK: inicialização da thread de render, uma vez
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
          g_frameSampled.insert(TexKey(tf));
        }
    }
    auto tr = Clock::now();
    PrepareFrameStreams(work);
    PlanDirectPasses(work);
    for (size_t i = 0; i < work.cmds.size(); ++i) {
      const Cmd& c = work.cmds[i];
      if (c.kind == Kind::Resolve) { auto tr0 = Clock::now(); ReplayResolve(work, c, g_resolveDirect[i] != 0); ++g_resolves; g_pResolve += Ms(tr0, Clock::now()); }
      else if (c.kind == Kind::Skip) Skip(c.skipReason);
      else { ++g_draws; ++frameDraws; ReplayDraw(work, c, g_redirects[i]); }
    }
    g_copiesSkipped += uint32_t(g_pendingCopies.size());
    g_pendingCopies.clear();
    AuditBufferImage(work);
    if (GlProfile()) { auto tf = Clock::now(); glFinish(); g_pFinish += Ms(tf, Clock::now()); }  // LEVE_OK: perfil opt-in (NIER_GL_PROFILE) para medir a GPU
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
                   g_programs.size(), g_textures.size(), g_textureMs / 120.0, g_sigMs / 120.0, g_decodeMs / 120.0, g_uploadMs / 120.0, g_replayMs / 120.0, g_swapMs / 120.0, g_gameWaitNs.exchange(0) / 120e6, GlProfile() ? glGetError() : 0u);  // LEVE_OK: glGetError só no perfil opt-in
      if (!g_skipReasons.empty()) {
        std::string line;
        for (auto& [r, n] : g_skipReasons) line += " | " + r + ": " + std::to_string(n);
        std::fprintf(stderr, "[nier-gl] motivos acumulados%s\n", line.c_str());
      }
      if (GlProfile())
        std::fprintf(stderr, "[nier-perfil] ms/quadro: alvo+limpeza %.2f estado+programa+texturas %.2f buffers %.2f desenho %.2f resolve %.2f (%.1f/quadro, cópias pedidas %.1f feitas %.1f evitadas %.1f, alias %.1f, passes diretos %.1f com %.1f desenhos) glFinish(GPU) %.2f\n",
                     g_pTarget / 120, g_pState / 120, g_pBuffers / 120, g_pDraw / 120, g_pResolve / 120, g_resolves / 120.0, g_resolveCopies / 120.0, g_copiesDone / 120.0, g_copiesSkipped / 120.0, g_aliases / 120.0, g_directPasses / 120.0, g_directDraws / 120.0, g_pFinish / 120);
      g_copiesDone = g_copiesSkipped = 0; g_aliases = g_directPasses = g_directDraws = 0;
      std::fprintf(stderr, "[nier-buffers] frame %llu CPU hits %llu misses %llu; GL hits %llu uploads %llu bytes %llu resident %zu\n",
                   (unsigned long long)n, (unsigned long long)g_bufferSnapshots.hits.load(),
                   (unsigned long long)g_bufferSnapshots.misses.load(), (unsigned long long)g_bufferHits,
                   (unsigned long long)g_bufferUploads, (unsigned long long)g_bufferUploadBytes, g_bufferObjectBytes);
      static uint64_t lastMiss = 0, lastCreate = 0, lastStream = 0, lastBytes = 0;
      uint64_t misses = g_bufferSnapshots.misses.load();
      std::fprintf(stderr, "[nier-perfil] buffers/quadros: misses %.2f criacoes %.2f static_uploads %.2f stream_uploads %.2f stream_bytes %.0f\n",
        double(misses-lastMiss)/120, double(g_bufferUploads-lastCreate)/120, double(g_bufferUploads-lastCreate)/120,
        double(g_streamUploads-lastStream)/120, double(g_streamBytes-lastBytes)/120);
      lastMiss=misses;lastCreate=g_bufferUploads;lastStream=g_streamUploads;lastBytes=g_streamBytes;
      g_pTarget = g_pState = g_pBuffers = g_pDraw = g_pFinish = g_pResolve = 0; g_resolves = g_resolveCopies = 0;
      g_textureMs = g_sigMs = g_decodeMs = g_uploadMs = g_replayMs = g_swapMs = 0;
    }
    {
      std::lock_guard lock(g_queueMutex);
      work.Clear();
      TrimBufferObjects();
      g_inflight.blob.swap(work.blob);
      g_inflight.cmds.swap(work.cmds);
      g_hasInflight = false;
    }
    g_queueCv.notify_all();
  }
  TrimBufferObjects(true);
  for (auto& slot : g_streamSlots) { glDeleteBuffers(1, &slot.vertex); glDeleteBuffers(1, &slot.index); }
  SDL_GL_MakeCurrent(g_window, nullptr);
}

// Converte os inputs do VS para float4 no blob. Vértices [first, first+count).
bool SnapshotInputs(Cmd& c, const uint8_t* state, int64_t first, uint32_t count, const uint8_t* upData, uint32_t upStride) {
  c.inputCount = uint32_t(std::min<size_t>(c.vs->inputs.size(), kMaxInputs));
  c.vertexCount = count;
  // Cópia local das fetch constants de vértice: `state` pode apontar para dentro de g_building.blob, que é
  // realocado pelos Append abaixo (ponteiro pendurado -> SIGSEGV em laço no tratador do runtime, 07/11).
  uint8_t vfetch[96 * 8];
  std::memcpy(vfetch, Field(state, kFetchConstants), sizeof vfetch);
  for (uint32_t i = 0; i < c.inputCount; ++i) {
    VertexInput in = c.vs->inputs[i];
    const uint8_t* base;
    uint32_t endian;
    if (upData && in.slot == 95) {          // BeginVertices: stream 0 vem da memória da CPU, 8in32
      in.stride = upStride / 4;
      base = upData; endian = 2;
    } else {
      const uint8_t* vf = vfetch + std::min<uint32_t>(in.slot, 95) * 8;
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
    if (!ReferenceBuffers() && !c.up) {
      c.vertexData[i] = g_bufferSnapshots.Vertex(in, base, endian, count, g_vertexHalf.load(), g_bufferRecordingFrame);
      if (!c.vertexData[i]) { RecordSkip("formato de vfetch não suportado"); return false; }
      g_building.bufferBytes += c.vertexData[i]->raw.size() + c.vertexData[i]->bytes.size();
      continue;
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
std::atomic<long long> g_traceFrame{std::getenv("NIER_RENDER_TRACE_FRAME") ? std::atoll(std::getenv("NIER_RENDER_TRACE_FRAME")) : -1};
bool TraceThisFrame() {
  long long f = g_traceFrame.load();
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
    float decoded[8] = {};
    const float* v = reinterpret_cast<const float*>(g_building.blob.data() + c.inputs[i]);
    if (c.vertexData[i]) {
      const auto& data = *c.vertexData[i];
      ConvertInput(data.input, data.raw.data(), data.endian, 0, std::min(2u, c.vertexCount), decoded);
      v = decoded;
    }
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
  g_building.Clear(); g_inflight.Clear(); g_bufferSnapshots.Clear();
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
    c.indexAddress = uint64_t(physical) + uint64_t(startIndex) * bytes;
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
  TraceDraw(c, g_building.blob.data() + c.state, primitive, count, indexed);
  c.indexCount = uint32_t(g_triIndices.size());
  if (ReferenceBuffers() || c.up) c.indices = Append(g_building, g_triIndices.data(), g_triIndices.size() * 2);
  else {
    c.indexData = g_bufferSnapshots.Indices(c.indexAddress, g_triIndices.data(), c.indexCount, g_bufferRecordingFrame);
    g_building.bufferBytes += c.indexData->raw.size() + c.indexData->bytes.size();
  }
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
  c.up = true;
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
  if (ReferenceBuffers() || c.up) c.indices = Append(g_building, g_triIndices.data(), g_triIndices.size() * 2);
  else {
    c.indexData = g_bufferSnapshots.Indices(c.indexAddress, g_triIndices.data(), c.indexCount, g_bufferRecordingFrame);
    g_building.bufferBytes += c.indexData->raw.size() + c.indexData->bytes.size();
  }
  g_building.cmds.push_back(c);
}

}  // namespace

void RecordResolve(uint32_t device, uint32_t flags, const int32_t* rect, uint32_t destTexture, uint32_t clearColorArgb) {
  FlushPendingUP();
  const uint8_t* state = Virtual(device + kStateBegin, kStateSize);
  if (!state || (flags & 4)) return;  // D3DRESOLVE_DEPTHSTENCIL = 4 (rastro 07/11): profundidade não é copiada
  Cmd c{Kind::Resolve};
  c.flags = flags; c.argb = clearColorArgb;
  if (rect) { std::memcpy(c.rect, rect, sizeof c.rect); c.hasRect = true; }
  c.state = Append(g_building, state, kStateSize);
  if (destTexture) {
    // Textura D3D do 9328: fetch constant em +0x1C (o 20871 usa +0x18). Conferido no rastro de 07/11:
    // objeto+0x1C = 8A000002 (tipo textura), +0x24 = 0059E4FF (1280x720).
    if (const uint8_t* fetch = Virtual(destTexture + 0x1C, 24)) c.fetch = Append(g_building, fetch, 24);
    if (TraceThisFrame()) {
      const uint8_t* obj = Virtual(destTexture, 0x40);
      if (obj) {
        std::fprintf(stderr, "[nier-trace] resolve flags %08X dest %08X objeto:", flags, destTexture);
        for (int i = 0; i < 16; ++i) std::fprintf(stderr, " %08X", BE32(obj + 4 * i));
        auto t = xenos::DecodeFetch(obj + 0x1C);
        std::fprintf(stderr, " -> fetch+0x1C base %08X %ux%u fmt %u\n", t.base, t.width, t.height, t.format);
      }
    }
  }
  g_building.cmds.push_back(c);
}

void RecordPresentInterval(uint32_t interval) { g_interval = interval; }

void DriveTestInput(uint64_t frame);  // test_input.cpp

void Present() {
  if (!g_enabled) return;
  FlushPendingUP();
  static long long sceneDelay = std::getenv("NIER_RENDER_TRACE_SCENE") ? std::atoll(std::getenv("NIER_RENDER_TRACE_SCENE")) : -1;
  if (sceneDelay >= 0 && g_traceFrame.load() < 0 && g_building.cmds.size() > 60) {
    g_traceFrame = (long long)g_presented.load() + 1 + sceneDelay;
    std::fprintf(stderr, "[nier-trace] cena 3D no quadro %llu; rastro no quadro %lld\n", (unsigned long long)g_presented.load(), g_traceFrame.load());
  }
  ++g_bufferRecordingFrame;
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
