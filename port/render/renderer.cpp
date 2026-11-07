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

#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <regex>
#include <set>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace nier::render {
namespace {
using namespace std::string_literals;
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
GLenum g_triMode = GL_TRIANGLES;  // modo GL escolhido pelo último Triangulate (tira ou lista, o mais barato)
bool Triangulate(uint32_t primitive, const std::vector<uint32_t>& src, std::vector<uint16_t>& dst) {
  dst.clear();
  g_triMode = GL_TRIANGLES;
  auto add = [&](uint32_t a, uint32_t b, uint32_t c) { if (a != b && b != c && a != c) { dst.push_back(uint16_t(a)); dst.push_back(uint16_t(b)); dst.push_back(uint16_t(c)); } };
  switch (primitive) {
    case 4: for (size_t i = 0; i + 2 < src.size(); i += 3) add(src[i], src[i + 1], src[i + 2]); return true;
    case 5: for (size_t i = 2; i < src.size(); ++i) add(src[0], src[i - 1], src[i]); return true;
    case 6: {
      // Tira com reinício (0xFFFFFFFF): sai como UMA tira GL; a emenda usa vértices repetidos (degenerados) e
      // mantém a paridade do enrolamento (número par de índices entre sub-tiras).
      static bool asList = std::getenv("NIER_STRIP_AS_LIST") != nullptr;  // A/B
      // custo: tira = soma(len) + 3 por emenda; lista = 3*(len-2) por sub-tira. Tiras curtas (quads) saem em lista.
      size_t stripCost = 0, listCost = 0, subs = 0;
      for (size_t i = 0; i < src.size();) { size_t j = i; while (j < src.size() && src[j] != 0xFFFFFFFFu) ++j; if (j - i >= 3) { stripCost += j - i + (subs ? 3 : 0); listCost += 3 * (j - i - 2); ++subs; } i = j + 1; }
      if (asList || listCost <= stripCost) { size_t start = 0; for (size_t i = 0; i < src.size(); ++i) { if (src[i] == 0xFFFFFFFFu) { start = i + 1; continue; } if (i < start + 2) continue; if ((i - start) & 1) add(src[i - 1], src[i - 2], src[i]); else add(src[i - 2], src[i - 1], src[i]); } return true; }
      bool first = true;
      for (size_t i = 0; i < src.size();) {
        size_t j = i; while (j < src.size() && src[j] != 0xFFFFFFFFu) ++j;
        if (j - i >= 3) {
          if (!first) { dst.push_back(dst.back()); dst.push_back(uint16_t(src[i])); if (dst.size() & 1) dst.push_back(uint16_t(src[i])); }
          for (size_t k = i; k < j; ++k) dst.push_back(uint16_t(src[k]));
          first = false;
        }
        i = j + 1;
      }
      g_triMode = GL_TRIANGLE_STRIP;
      return true;
    }
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
  int skinJob = -1;  // índice em Frame::skinJobs (skinning na CPU iniciado na captura)
  uint8_t prim = 0;         // primitiva Xenos original (perfil)
  GLenum mode = GL_TRIANGLES;  // tiras da Xenos viram GL_TRIANGLE_STRIP (reinício = triângulos degenerados): 1/3 dos índices
};
struct SkinJob;
struct Frame {
  std::vector<uint8_t> blob;
  std::vector<Cmd> cmds;
  std::vector<std::shared_ptr<SkinJob>> skinJobs;  // jobs do skinning deste quadro (donos compartilhados com o pool)
  uint32_t presentInterval = 1;
  size_t bufferBytes = 0; // conservative accounting, including references shared by draws
  void Clear() { blob.clear(); cmds.clear(); skinJobs.clear(); bufferBytes = 0; }
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
  // Resolve repetido da mesma tela para um 2º destino: este destino aponta para o outro só neste quadro
  // (sharedFrom/sharedFrame); mantém os próprios recursos (nada recriado por quadro na Mali).
  uint64_t sharedFrom = 0, sharedFrame = ~0ull;
  uint64_t lastCopy = 0;  // destino que já recebeu cópia deste conteúdo (Resolve repetido referencia, não copia)
  uint64_t whiteFrame = ~0ull;  // quadro em que foi limpo em branco (recepção de sombra pulada)
};
std::map<uint64_t, Target> g_targets;
std::map<uint64_t, Target> g_resolved;
uint64_t g_mainTarget = 0;
// Resolve preguiçoso (Mali: cada glCopyTexSubImage esvazia o tile e custa ms): a cópia só acontece se a
// textura destino for amostrada ou antes de o alvo de origem ser alterado; sem uso no quadro, é descartada.
GLuint g_blitProgram = 0, g_blitBuffer = 0, g_vertexBuffer = 0, g_indexBuffer = 0;
GLint g_blitRect = -1, g_blitFlip = -1;
bool GlProfile() { static bool on = std::getenv("NIER_GL_PROFILE") != nullptr; return on; }
// Cache de estado GL na CPU (como o glCache do Rayman): nenhum glGet e nenhuma chamada redundante no quadro.
// Quem muda estado fora deste cache (CreateTarget, blit, apresentação) chama InvalidateGl().
struct GlCache {
  GLuint fbo = ~0u, program = ~0u;
  GLuint tex[16]; int unit = -1;
  bool fixedValid = false;
  GLint vp[4] = {}; float dr[2] = {}; uint32_t blend = 0, depth = 0, mode = 0, mask = 0;
  uint32_t attribs = ~0u;  // bits dos atributos ligados (~0 = desconhecido)
  GlCache() { for (auto& t : tex) t = ~0u; }
};
GlCache g_gl;
GLint g_vsUniformVectors = 256;  // medido no init (Mali-450: 256)
void InvalidateGl() { g_gl = GlCache{}; }
// Medição do "desenho" (CPU dentro de glDrawElements no driver Mali): o que cada janela enviou
uint64_t g_vec4Sent = 0, g_vtVec4Sent = 0, g_indicesDrawn = 0, g_programsLinked = 0, g_programSwitches = 0;
uint64_t g_idxByClass[5] = {}, g_drawsByClass[5] = {}, g_skippedPrepass = 0, g_skippedShadow = 0, g_skippedReceive = 0;
bool g_mali = false;
struct TopDraw { uint32_t indices, vertices; uint8_t prim, cls; uint32_t vs, ps; int vsOps; };
std::vector<TopDraw> g_topDraws;  // só no perfil: desenhos da janela, para o top 10 por índices
uint8_t g_drawClassCurrent = 0;
uint64_t g_culledDraws = 0, g_culledIndices = 0, g_cullEval = 0, g_cullNoPattern = 0, g_cullNoData = 0;
struct Aabb { float lo[3], hi[3]; bool ok; };
std::unordered_map<uint64_t, Aabb> g_aabb;  // serial do atributo de posição -> AABB
bool g_skipPrepass = false, g_skipShadow = false, g_sortByProgram = false;
uint32_t g_fboBinds = 0;  // por janela de perfil (troca de alvo = recarga/gravação de tiles na Mali)
void BindFbo(GLuint id) { if (g_gl.fbo != id) { g_gl.fbo = id; ++g_fboBinds; glBindFramebuffer(GL_FRAMEBUFFER, id); } }
// GL_EXT_discard_framebuffer: ao fechar um passe, a profundidade/estêncil não precisa voltar para a memória.
PFNGLDISCARDFRAMEBUFFEREXTPROC g_discard = nullptr;
std::set<GLuint> g_depthKeep;  // texturas de profundidade com alias neste quadro (não descartar)
uint32_t g_discards = 0;
void DiscardDepth(GLuint fbo, GLuint depth) {
  if (!g_discard || !fbo || !depth || g_depthKeep.count(depth)) return;
  BindFbo(fbo);
  static const GLenum att[] = {GL_DEPTH_ATTACHMENT, GL_STENCIL_ATTACHMENT};
  g_discard(GL_FRAMEBUFFER, 2, att);
  ++g_discards;
}
void UseProgram(GLuint id) { if (g_gl.program != id) { g_gl.program = id; ++g_programSwitches; glUseProgram(id); } }
void ActiveUnit(int unit) { if (g_gl.unit != unit) { g_gl.unit = unit; glActiveTexture(GL_TEXTURE0 + unit); } }
void BindTexture(int unit, GLuint id) { if (g_gl.tex[unit] != id) { ActiveUnit(unit); g_gl.tex[unit] = id; glBindTexture(GL_TEXTURE_2D, id); } }
void SetAttribs(uint32_t count) {
  uint32_t want = count >= 32 ? ~0u : (1u << count) - 1;
  if (g_gl.attribs == want) return;
  for (uint32_t i = 0; i < kMaxInputs; ++i) {
    bool on = (want >> i) & 1, was = g_gl.attribs != ~0u && ((g_gl.attribs >> i) & 1);
    if (g_gl.attribs == ~0u || on != was) { if (on) glEnableVertexAttribArray(i); else glDisableVertexAttribArray(i); }
  }
  g_gl.attribs = want;
}
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
  BindFbo(dst.fbo);
  glViewport(0, 0, dst.w, dst.h);
  glDisable(GL_SCISSOR_TEST); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_BLEND);
  glDepthMask(GL_FALSE);
  glColorMask(1, 1, 1, 1);
  g_gl.fixedValid = false;
  UseProgram(g_blitProgram);
  BindTexture(0, src.texture);
  glUniform4f(g_blitRect, float(p.w) / src.w, float(p.h) / src.h, float(p.x0) / src.w, float(p.y0) / src.h);
  glUniform1f(g_blitFlip, 0.0f);
  glBindBuffer(GL_ARRAY_BUFFER, g_blitBuffer);
  SetAttribs(1);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
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
// Profundidade como textura (GL_OES_depth_texture): o Resolve de profundidade do jogo (flags & 4) vira alias da
// textura de profundidade do alvo, amostrada pelos PS de formato 22 (nuvens/partículas suaves, sombra). Sem a
// extensão, branco (sem sombra) como antes.
bool g_depthTexture = false;
std::map<uint64_t, GLuint> g_depthAlias;  // chave da textura fmt 22 -> textura de profundidade GL

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
  const char* vs = "attribute vec2 p; varying vec2 uv; uniform vec4 r; uniform mediump float flip; void main(){ vec2 q = p * 0.5 + 0.5; q.y = mix(q.y, 1.0 - q.y, flip); uv = q * r.xy + r.zw; gl_Position = vec4(p, 0.0, 1.0); }";
  // Resolve preserves HDR metadata in alpha. Only presentation (flip=1) is opaque.
  const char* fs = "precision mediump float; varying vec2 uv; uniform sampler2D t; uniform mediump float flip; void main(){ gl_FragColor = texture2D(t, uv); gl_FragColor.a = mix(gl_FragColor.a, 1.0, flip); }";
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
  g_depthTexture = ext && std::strstr(ext, "GL_OES_depth_texture") && !std::getenv("NIER_NO_DEPTH_TEXTURE");
  if (ext && std::strstr(ext, "GL_EXT_discard_framebuffer") && !std::getenv("NIER_NO_DISCARD"))
    g_discard = reinterpret_cast<PFNGLDISCARDFRAMEBUFFEREXTPROC>(eglGetProcAddress("glDiscardFramebufferEXT"));  // LEVE_OK: inicialização
  std::fprintf(stderr, "[nier-gl] profundidade: depth24 %d, textura de profundidade %d, discard %d\n", int(g_depth24), int(g_depthTexture), int(g_discard != nullptr));
}

void ReleaseTarget(Target& t) {
  if (t.texture) glDeleteTextures(1, &t.texture);
  if (t.fbo) glDeleteFramebuffers(1, &t.fbo);
  if (t.depth) { if (g_depthTexture) glDeleteTextures(1, &t.depth); else glDeleteRenderbuffers(1, &t.depth); }
  t = Target{};
}
uint32_t g_targetCreates = 0;  // alvos (re)criados na janela: na Mali cada criação custa ms
void CreateTarget(Target& t, int w, int h, bool withDepth) {
  ++g_targetCreates;
  ReleaseTarget(t);
  InvalidateGl();  // liga textura/FBO/renderbuffer fora do cache
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
  static bool reportedStorage = false;
  if (!reportedStorage) {
    reportedStorage = true;
    GLint bits[4]{};
    glGetIntegerv(GL_RED_BITS, &bits[0]); // LEVE_OK: actual RGBA texture storage, once at first target creation
    glGetIntegerv(GL_GREEN_BITS, &bits[1]); // LEVE_OK: actual RGBA texture storage, once at first target creation
    glGetIntegerv(GL_BLUE_BITS, &bits[2]); // LEVE_OK: actual RGBA texture storage, once at first target creation
    glGetIntegerv(GL_ALPHA_BITS, &bits[3]); // LEVE_OK: actual RGBA texture storage, once at first target creation
    std::fprintf(stderr, "[nier-gl] alvo RGBA/UNSIGNED_BYTE: bits efetivos %d/%d/%d/%d%s\n", bits[0], bits[1], bits[2], bits[3],
                 bits[0]>=8 && bits[1]>=8 && bits[2]>=8 && bits[3]>=8 ? "" : " INSUFICIENTES para HDR alpha");
  }
  if (withDepth && g_depthTexture) {
    glGenTextures(1, &t.depth);
    glBindTexture(GL_TEXTURE_2D, t.depth);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, w, h, 0, GL_DEPTH_COMPONENT, g_depth24 ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, t.depth, 0);
  } else if (withDepth) {
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
  g_gl.fixedValid = false;
}
// FBO próprio do destino de Resolve (passe desenhado direto, sem cópia): tamanho do destino na escala interna.
Target& DirectTarget(uint64_t key, int w, int h) {
  Target& t = g_resolved[key];
  if (t.w != w || t.h != h || !t.depth) CreateTarget(t, w, h, true);
  t.sharedFrom = 0;
  BindFbo(t.fbo);
  ClearOncePerFrame(t);
  return t;
}
Target& CurrentTarget(const uint8_t* state, bool forDraw = true) {
  uint32_t surface = BE32(Field(state, kSurfaceInfo)), color = BE32(Field(state, kColorInfo));
  uint64_t key = uint64_t(color & 0xF0FFF) << 16 | (surface & 0x3FFF);
  uint32_t guestPitch = std::max<uint32_t>(surface & 0x3FFF, 1);
  // altura: 720 do guest, ou o que o viewport pedir (mapa de sombra 1024x1024 desenha até y = 1024)
  float vpBottom = BEF(Field(state, kViewport + 12)) + std::fabs(BEF(Field(state, kViewport + 8)));
  int guestH = std::clamp(int(std::lround(vpBottom)), 720, 2048);
  int w = std::max(1, int(guestPitch * Scale() + 0.5f)), h = std::max(1, int(guestH * Scale() + 0.5f));
  if (!g_pendingCopies.empty()) FlushCopiesFrom(key);
  Target& t = g_targets[key];
  if (t.w != w || (forDraw ? t.h < h : t.h != h && t.h < h)) CreateTarget(t, w, std::max(h, t.h), true);
  if (forDraw) { t.holder = 0; t.lastCopy = 0; }  // o conteúdo mais novo volta a ser a textura do próprio alvo
  if ((key & 0x3FFF) > (g_mainTarget & 0x3FFF)) g_mainTarget = key;
  BindFbo(t.fbo);
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
    g_gl.tex[std::max(g_gl.unit, 0)] = g_white;
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
// C1-H2 frameskip na fila de replay (lógica do NieR é por Δt real: o jogo não pode esperar o GL).
// NIER_FRAMESKIP=auto: se a thread GL ainda não pegou o quadro anterior, o novo o substitui (o antigo é pulado) em
// vez de o jogo esperar; no máximo 3 pulos seguidos (stride ≤ 4). 2|3|4 = 1 desenhado a cada N, pulos espalhados.
// 1 = sem pulo. Pular é seguro para a lógica: o renderizador não devolve nada à memória do guest; alvos/Resolve
// ficam do último quadro desenhado.
std::atomic<bool> g_frameskipAuto{false};
std::atomic<uint64_t> g_fsUpdates{0}, g_fsDropped{0}, g_fsMaxBurst{0};
std::atomic<uint64_t> g_triHits{0}, g_triMisses{0}, g_recordNs{0};  // perfil da thread de captura
std::atomic<uint64_t> g_recPhaseNs[7];  // RecordDraw por etapa: shaders, estado, índices, vértices, IB, skin, resto
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
    if (r->second.sharedFrom && r->second.sharedFrame == g_frames.load())
      if (auto h = g_resolved.find(r->second.sharedFrom); h != g_resolved.end() && h->second.texture) return h->second.texture;
    return r->second.texture ? r->second.texture : White();
  }
  if (t.format == xenos::k248) {
    if (auto d = g_depthAlias.find(key); d != g_depthAlias.end()) return d->second;
    // Sem textura de profundidade: branco = "sem sombra" nos PS que comparam. Não é desenho pulado.
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
  if (t.tiled) span = std::max(4096u, span);  // tiled tail may use another bank in the same 4 KiB page
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
  if (GlProfile()) {  // o que foi decodificado na janela: 1ª vez x redecodificação, por formato/tamanho
    static std::map<std::string, std::pair<uint32_t, double>> hist; static uint64_t lastFrame = 0; static uint32_t nFirst = 0, nRe = 0;
    static Clock::time_point lastTime = Clock::now();
    char k[48]; std::snprintf(k, sizeof k, "f%u %ux%u%s", t.format, t.width, t.height, fresh ? "" : " re");
    auto& e = hist[k]; ++e.first; e.second += std::chrono::duration<double, std::milli>(tu - td).count();
    if (fresh) ++nFirst; else ++nRe;
    if (frame >= lastFrame + 120 || Clock::now() - lastTime >= std::chrono::seconds(5)) {
      lastFrame = frame; lastTime = Clock::now();
      std::string line;
      std::vector<std::pair<double, std::string>> top;
      for (auto& [n, v] : hist) top.push_back({v.second, n + " x" + std::to_string(v.first)});
      std::sort(top.rbegin(), top.rend());
      for (size_t i = 0; i < top.size() && i < 6; ++i) { char b[96]; std::snprintf(b, sizeof b, " | %s %.1fms", top[i].second.c_str(), top[i].first); line += b; }
      std::fprintf(stderr, "[nier-perfil] decod. na janela: %u primeiras, %u redecod.%s\n", nFirst, nRe, line.c_str());
      hist.clear(); nFirst = nRe = 0;
    }
  }
  if (!tex.id) glGenTextures(1, &tex.id);
  ActiveUnit(std::max(g_gl.unit, 0));
  glBindTexture(GL_TEXTURE_2D, tex.id);
  g_gl.tex[g_gl.unit] = tex.id;
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
struct CullInfo { bool ok = false; int row[4] = {-1, -1, -1, -1}; int posInput = -1; };
struct Program {
  GLuint id = 0;
  GLint vc = -1, pc = -1, alphaFunc = -1, alphaRef = -1, wideScale = -1;
  std::vector<std::pair<int, GLint>> bools;    // (índice, local)
  std::vector<std::pair<int, GLint>> samplers; // (fetch slot, local nx_size) — unidade = ordem
  int vcCount = 256, pcCount = 256;
  std::vector<float> lastVc, lastPc;  // último banco enviado: glUniform4fv só nas faixas que mudaram
  bool vcSeq = false, pcSeq = false;  // VC[i] está em vc + i (confirmado no link): permite enviar faixas
  std::vector<int8_t> lastBools;      // -1 = nunca enviado
  int lastAlphaFunc = -2; float lastAlphaRef = -1e30f;
  std::vector<std::pair<float, float>> lastSizes;
  // Textura no VS (paleta de ossos 1024x1 float4): janela de texels em uniformes VT<t>[k], VT<t>_p = (largura, base)
  struct Vtf { int slot; GLint loc, params; int k; };
  std::vector<Vtf> vtf;
  int vsOps = 0;      // operações (vr/sr) no VS final, para o perfil
  bool identityUploaded = false;  // skinning na CPU: paleta de identidade já enviada
  bool dynIndex = false;          // VS final ainda indexa uniforme com expressão não constante (VC[k+addr], VT[...])
  CullInfo cull;      // MVP e atributo de posição reconhecidos (descarte por AABB)
};
// Substitui o stub "nx_sample<t> devolve 0" do tradutor por busca num array de uniformes (a Mali-450 não tem
// textura no vértice). k texels cabem no orçamento de uniformes do VS depois das constantes VC.
// Skinning na CPU (C1-G6, padrão): a paleta vista pelo VS é a identidade (linha r = e_r), o índice é só a linha
// (mod 4) e a CPU entrega posição/direções já transformadas pela matriz misturada dos ossos. NIER_GPU_SKIN=1 volta
// à janela de uniformes por malha (índice dinâmico, caro na Mali).
bool CpuSkin() { static bool on = std::getenv("NIER_GPU_SKIN") == nullptr; return on; }
std::string EmulateVertexTextures(const std::string& src, const GuestShader* vs, std::vector<std::pair<int, int>>& slots) {
  std::string out = src;
  if (CpuSkin()) {
    for (int t = 0; t < 32; ++t) {
      if (!(vs->textureMask & (1u << t))) continue;
      std::string n = std::to_string(t);
      std::string stub = "vec4 nx_sample" + n + "(vec2 uv) { return vec4(0.0); }\n";
      size_t at = out.find(stub);
      if (at == std::string::npos) continue;
      // índices de osso e pesos viram constantes no programa (ver SkinConst em ProgramFor): uv é dobrado pelo
      // compilador e o acesso vira VT<n>[const] — sem índice dinâmico de uniforme (GP da Mali, não software).
      // C1-G7: a paleta é a identidade e a linha é (texel mod 4) → linha calculada em ALU, sem uniforme nenhum
      // (nada de índice de uniforme no VS: na MaliGP2 isso cai em vértice por software no driver).
      std::string repl = "vec4 nx_sample" + n + "(vec2 uv) { float k = floor(mod(uv.x * 1024.0 + 0.5, 4.0)); "
        "return vec4(equal(vec4(k), vec4(0.0, 1.0, 2.0, 3.0))); }\n";
      out.replace(at, stub.size(), repl);
      slots.push_back({t, 0});  // k = 0: sem uniformes VT (loc = -1); o slot ainda diz de onde a CPU lê a paleta
    }
    return out;
  }
  int budget = int(g_vsUniformVectors) - int(vs->constMap.size()) - 24;
  static int capK = std::getenv("NIER_VT_K") ? std::atoi(std::getenv("NIER_VT_K")) : 1024;  // emular a Mali no PC: NIER_VT_K=220
  int k = std::clamp(std::min(budget, capK), 32, 1024);  // Mali-450: ~220 (55 ossos); host: 1024
  for (int t = 0; t < 32; ++t) {
    if (!(vs->textureMask & (1u << t))) continue;
    std::string n = std::to_string(t);
    std::string stub = "vec4 nx_sample" + n + "(vec2 uv) { return vec4(0.0); }\n";
    size_t at = out.find(stub);
    if (at == std::string::npos) continue;
    std::string repl = "uniform vec4 VT" + n + "[" + std::to_string(k) + "];\nuniform vec2 VT" + n + "_p;\n"
      "vec4 nx_sample" + n + "(vec2 uv) { return VT" + n + "[int(clamp(uv.x * VT" + n + "_p.x + 0.001 - VT" + n + "_p.y, 0.0, " + std::to_string(k - 1) + ".0))]; }\n";
    out.replace(at, stub.size(), repl);
    slots.push_back({t, k});
  }
  return out;
}
std::unordered_map<uint64_t, Program> g_programs;

// Variante sem alpha test (alphaTest = false): o bloco "bool pass … if (!pass) discard;" sai do PS. Na Mali-450 um
// `discard` presente no shader desliga o early-Z/descarte de fragmentos escondidos mesmo que nunca execute.
std::string StripAlphaTest(const std::string& src) {
  size_t a = src.find("    bool pass = alphaFunc == 7");
  size_t b = src.find("    if (!pass) discard;\n");
  if (a == std::string::npos || b == std::string::npos || b < a) return src;
  return src.substr(0, a) + src.substr(b + std::strlen("    if (!pass) discard;\n"));
}
// r[N] (índices sempre estáticos no tradutor) vira variáveis r0..rN: compiladores antigos (MaliGP2/Mali-450)
// tratam arrays como memória e não cabem no limite de 512 palavras; variáveis soltas são alocadas em registros.
std::string ScalarizeRegisters(const std::string& src) {
  static bool keep = std::getenv("NIER_KEEP_REG_ARRAY") != nullptr;
  if (keep) return src;
  const std::string decl = "    vec4 r[";
  size_t a = src.find(decl);
  if (a == std::string::npos) return src;
  size_t b = src.find("];\n", a);
  int n = std::atoi(src.c_str() + a + decl.size());
  if (b == std::string::npos || n <= 0 || n > 64) return src;
  size_t loopEnd = src.find("\n", b + 3);  // linha "for (int i = 0; i < n; i++) r[i] = vec4(0.0);"
  if (loopEnd == std::string::npos) return src;
  std::string out = src.substr(0, a) + "    vec4 ";
  for (int i = 0; i < n; ++i) out += "r" + std::to_string(i) + " = vec4(0.0)" + (i + 1 < n ? ", " : ";\n");
  std::string rest = src.substr(loopEnd + 1);
  std::string res; res.reserve(rest.size());
  for (size_t i = 0; i < rest.size();) {
    if (rest[i] == 'r' && i + 1 < rest.size() && rest[i + 1] == '[' && (i == 0 || !std::isalnum(uint8_t(rest[i - 1])) && rest[i - 1] != '_')) {
      size_t j = i + 2; while (j < rest.size() && std::isdigit(uint8_t(rest[j]))) ++j;
      if (j < rest.size() && rest[j] == ']' && j > i + 2) { res += "r" + rest.substr(i + 2, j - i - 2); i = j + 1; continue; }
    }
    res += rest[i++];
  }
  return out + res;
}
// Saídas v<i> do VS que o PS não lê: as atribuições saem do VS (o compilador descarta o cálculo morto; a Mali GP
// deixa de escrever varyings inúteis).
std::string StripUnusedVaryings(const std::string& src, uint8_t psInputMask) {
  static bool keep = std::getenv("NIER_KEEP_VARYINGS") != nullptr;
  if (keep || psInputMask == 0xFF) return src;
  std::string out; out.reserve(src.size());
  size_t at = 0;
  while (at < src.size()) {
    size_t end = src.find('\n', at); if (end == std::string::npos) end = src.size();
    std::string_view line(src.data() + at, end - at);
    bool drop = false;
    size_t p = line.find_first_not_of(' ');
    if (p != std::string_view::npos && p + 1 < line.size() && line[p] == 'v' && std::isdigit(uint8_t(line[p + 1])) && (p + 2 == line.size() || line[p + 2] == '.' || line[p + 2] == ' ')) {
      int v = line[p + 1] - '0';
      size_t eq = line.find(" = ", p);
      if (v < 8 && eq != std::string_view::npos && !((psInputMask >> v) & 1)) drop = true;
    }
    if (!drop) { out.append(line); out.push_back('\n'); }
    at = end + 1;
  }
  return out;
}
// Descarte por volume no CPU: reconhece no VS traduzido "opos.c = dot(VC[k].s, r[n].s)" nas 4 componentes e
// "r[n].xyz = a<m>.xyz, w = 1" (posição = atributo m); as 4 linhas VC[k] (índices compactados) são a MVP.
CullInfo ParseCull(const std::string& src) {
  CullInfo ci;
  static const std::regex opos(R"(vec4 va = VC\[(\d+)\]\.(\w+);\n\s*vec4 vb = r\[(\d+)\]\.(\w+);\n\s*vec4 vr = vec4\(dot\(va, vb\)\);\n\s*opos\.([xyzw]) = vr\.)");
  int reg = -1;
  for (auto it = std::sregex_iterator(src.begin(), src.end(), opos); it != std::sregex_iterator(); ++it) {
    const auto& m = *it;
    if (m[2].str() != m[4].str()) return ci;  // swizzles diferentes: o dot não é o produto direto
    int r = std::stoi(m[3].str());
    if (reg >= 0 && r != reg) return ci;
    reg = r;
    ci.row["xyzw"s.find(m[5].str()[0])] = std::stoi(m[1].str());
  }
  if (reg < 0 || ci.row[0] < 0 || ci.row[1] < 0 || ci.row[2] < 0 || ci.row[3] < 0) return ci;
  std::regex pos("r\\[" + std::to_string(reg) + "\\]\\.x = a(\\d+)\\.x;\\n\\s*r\\[" + std::to_string(reg) + "\\]\\.y = a(\\d+)\\.y;\\n\\s*r\\[" + std::to_string(reg) + "\\]\\.z = a(\\d+)\\.z;\\n\\s*r\\[" + std::to_string(reg) + "\\]\\.w = 1\\.0;");
  std::smatch m;
  if (!std::regex_search(src, m, pos) || m[1].str() != m[2].str() || m[1].str() != m[3].str()) return ci;
  ci.posInput = std::stoi(m[1].str());
  ci.ok = true;
  return ci;
}
// C1-G8: VS com paleta de ossos em constantes (VC[B+addr], VC[B+1+addr], VC[B+2+addr], addr = 3 * índice do osso).
// Com o skinning na CPU o VS lê as linhas da identidade como literais (e0/e1/e2): nenhum índice dinâmico de uniforme.
// A saída do VS fica (Σ pesos do shader) × posição pré-misturada = a mesma de antes quando os pesos somam 1.
struct RelSkinInfo { bool ok = false; int base = 0; int bones = 0; };
const RelSkinInfo& RelSkinFor(const GuestShader* vs) {
  static std::unordered_map<uint64_t, RelSkinInfo> cache;  // uma análise por VS (não é por quadro)
  static std::mutex mx; std::lock_guard<std::mutex> lk(mx);  // thread do jogo (captura) e thread GL (referências estáveis)
  auto [it, inserted] = cache.try_emplace(vs->key);
  RelSkinInfo& r = it->second;
  static bool off = std::getenv("NIER_NO_REL_SKIN") != nullptr;  // A/B: volta ao VC[k+addr]
  if (!inserted || off || !vs->relativeConstants || !CpuSkin()) return r;
  static const std::regex refRe(R"(VC\[nx_clamp_int\((\d+) \+ addr, 0, \d+\)\])");
  static const std::regex saRe(R"(float sa = ([^,]+), sb = [^;]+;\s*\n\s*addr = )");
  std::set<int> ks; size_t refs = 0;
  for (std::sregex_iterator m(vs->source.begin(), vs->source.end(), refRe), e; m != e; ++m) { ks.insert(std::stoi((*m)[1])); ++refs; }
  size_t addrUses = 0;
  for (size_t at = 0; (at = vs->source.find("+ addr", at)) != std::string::npos; at += 6) ++addrUses;
  if (ks.size() != 3 || *ks.rbegin() - *ks.begin() != 2 || addrUses != refs) return r;
  std::set<std::string> srcs;
  for (std::sregex_iterator m(vs->source.begin(), vs->source.end(), saRe), e; m != e; ++m) srcs.insert((*m)[1]);
  if (srcs.empty() || srcs.size() > 4) return r;
  r.ok = true; r.base = *ks.begin(); r.bones = int(srcs.size());
  return r;
}
std::string RelSkinSource(const std::string& src, int base) {
  static const std::regex refRe(R"(VC\[nx_clamp_int\((\d+) \+ addr, 0, \d+\)\])");
  static const char* rows[3] = {"vec4(1.0, 0.0, 0.0, 0.0)", "vec4(0.0, 1.0, 0.0, 0.0)", "vec4(0.0, 0.0, 1.0, 0.0)"};
  std::string out; size_t last = 0;
  for (std::sregex_iterator m(src.begin(), src.end(), refRe), e; m != e; ++m) {
    out.append(src, last, size_t(m->position()) - last);
    out += rows[std::clamp(std::stoi((*m)[1]) - base, 0, 2)];
    last = size_t(m->position() + m->length());
  }
  out.append(src, last, std::string::npos);
  return out;
}
Program* ProgramFor(const GuestShader* vs, const GuestShader* ps, bool flat = false, bool alphaTest = true, int skinConst = 0, int relBase = -1) {
  uint64_t key = (vs->key * 0x9E3779B97F4A7C15ull ^ ps->key) + (flat ? 1 : 0) + (alphaTest ? 0 : 2) + (uint64_t(skinConst) << 3) + (relBase >= 0 ? 4 : 0);
  auto it = g_programs.find(key);
  if (it != g_programs.end()) return it->second.id ? &it->second : nullptr;
  Program& p = g_programs[key];
  char vn[40], pn[40];
  std::snprintf(vn, sizeof vn, "%016llX_vs", (unsigned long long)vs->key);
  std::snprintf(pn, sizeof pn, "%016llX_ps%s", (unsigned long long)ps->key, alphaTest ? "" : "_na");
  static const char* flatPs = "#version 100\nprecision mediump float;\nvoid main(){ gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0); }\n";
  std::string psSource = ScalarizeRegisters(alphaTest ? ps->source : StripAlphaTest(ps->source));
  std::vector<std::pair<int, int>> vtfSlots;
  static bool noVtf = std::getenv("NIER_NO_VTF") != nullptr;  // A/B na Mali: textura no VS devolve 0 (sem índice dinâmico de uniformes)
  std::string vsBase = relBase >= 0 ? RelSkinSource(vs->source, relBase) : vs->source;
  std::string vsSource = ScalarizeRegisters(StripUnusedVaryings((vs->textureMask && !noVtf) ? EmulateVertexTextures(vsBase, vs, vtfSlots) : vsBase, flat ? 0xFF : ps->inputMask));
  if (skinConst > 0) {
    // skinning na CPU: atributo de índices de osso = 0, pesos = (1,0,0,0); o resto do VS fica igual (paleta identidade)
    int idxIn = (skinConst - 1) >> 8, wIn = (skinConst - 1) & 0xFF;
    auto constify = [&](int i, const char* value) {
      std::string decl = "attribute vec4 a" + std::to_string(i) + ";\n";
      size_t at = vsSource.find(decl);
      if (at != std::string::npos) vsSource.replace(at, decl.size(), "const vec4 a" + std::to_string(i) + " = " + value + ";\n");
    };
    constify(idxIn, "vec4(0.0)");
    constify(wIn, "vec4(1.0, 0.0, 0.0, 0.0)");
  }
  p.dynIndex = vsSource.find("VC[nx_clamp_int(") != std::string::npos || vsSource.find("VT0[int(") != std::string::npos ||
               vsSource.find("[int(clamp(uv.x") != std::string::npos;
  p.cull = ParseCull(vs->source);
  for (size_t at = 0; (at = vsSource.find(" vr = ", at)) != std::string::npos; at += 6) ++p.vsOps;
  for (size_t at = 0; (at = vsSource.find(" sr = ", at)) != std::string::npos; at += 6) ++p.vsOps;
  GLuint v = CompileShader(GL_VERTEX_SHADER, vsSource.c_str(), vn), f = CompileShader(GL_FRAGMENT_SHADER, flat ? flatPs : psSource.c_str(), pn);
  if (!v || !f) { if (v) glDeleteShader(v); if (f) glDeleteShader(f); return nullptr; }
  p.id = glCreateProgram();
  glAttachShader(p.id, v); glAttachShader(p.id, f);
  for (uint32_t i = 0; i < vs->inputs.size() && i < kMaxInputs; ++i) glBindAttribLocation(p.id, i, ("a" + std::to_string(i)).c_str());
  ++g_programsLinked;
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
  g_gl.program = p.id;
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
  for (auto [t, k] : vtfSlots) {
    std::string n = std::to_string(t);
    p.vtf.push_back({t, glGetUniformLocation(p.id, ("VT" + n).c_str()), glGetUniformLocation(p.id, ("VT" + n + "_p").c_str()), k});
  }
  // VC[i] em vc + i? (envio por faixa); conferido uma vez no link
  if (p.vc >= 0 && p.vcCount > 1) p.vcSeq = glGetUniformLocation(p.id, ("VC[" + std::to_string(p.vcCount - 1) + "]").c_str()) == p.vc + p.vcCount - 1 && glGetUniformLocation(p.id, "VC[1]") == p.vc + 1;
  if (p.pc >= 0 && p.pcCount > 1) p.pcSeq = glGetUniformLocation(p.id, ("PC[" + std::to_string(p.pcCount - 1) + "]").c_str()) == p.pc + p.pcCount - 1 && glGetUniformLocation(p.id, "PC[1]") == p.pc + 1;
  return &p;
}

std::vector<float> g_constants;
// Envia as constantes na ordem compactada (GuestShader::constMap); literais por índice original.
void UploadConstants(GLint location, int count, const uint8_t* bank, const GuestShader* s, std::vector<float>& last, bool sequential) {
  if (location < 0) return;
  size_t n = std::min<size_t>(size_t(count), s->constMap.size());
  g_constants.resize(n * 4);
  size_t literals = s->literals.size() / 4;
  for (size_t j = 0; j < n; ++j) {
    size_t reg = s->constMap[j];
    if (reg >= 256 - literals) std::memcpy(&g_constants[j * 4], &s->literals[(reg - (256 - literals)) * 4], 16);
    else for (int c = 0; c < 4; ++c) g_constants[j * 4 + c] = BEF(bank + reg * 16 + 4 * c);
  }
  if (last.size() == g_constants.size()) {
    // só a faixa [primeiro, último] vec4 que mudou (matrizes por objeto mudam; luzes/material ficam)
    size_t first = 0, lastIdx = n;
    while (first < n && std::memcmp(&last[first * 4], &g_constants[first * 4], 16) == 0) ++first;
    if (first == n) return;
    while (lastIdx > first && std::memcmp(&last[(lastIdx - 1) * 4], &g_constants[(lastIdx - 1) * 4], 16) == 0) --lastIdx;
    std::memcpy(&last[first * 4], &g_constants[first * 4], (lastIdx - first) * 16);
    if (sequential) { g_vec4Sent += lastIdx - first; glUniform4fv(location + GLint(first), GLsizei(lastIdx - first), &g_constants[first * 4]); return; }
    g_vec4Sent += n;
    glUniform4fv(location, GLsizei(n), g_constants.data());
    return;
  }
  last = g_constants;
  g_vec4Sent += n;
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
  GLint vp[4] = {GLint(std::lround((xo - xs) * s)), GLint(std::lround((yo - ys) * s)), GLint(std::lround(2 * xs * s)), GLint(std::lround(2 * ys * s))};
  float n = zo, f = zo + zs;
  if (zs == 0 && zo == 0) { n = 0; f = 1; }
  float dr[2] = {std::clamp(n, 0.0f, 1.0f), std::clamp(f, 0.0f, 1.0f)};
  uint32_t blend = BE32(Field(state, kBlendControl));
  uint32_t depth = BE32(Field(state, kDepthControl)) & 0x76;  // habilita (2), escreve (4), função (bits 4-6)
  uint32_t mode = BE32(Field(state, kModeControl)) & 7;       // cull (2 bits) + frente CCW (bit 2)
  uint32_t mask = BE32(Field(state, 0x28DC)) & 15;            // RB_COLOR_MASK do alvo 0
  const bool v = g_gl.fixedValid;
  if (!v || std::memcmp(vp, g_gl.vp, sizeof vp)) { std::memcpy(g_gl.vp, vp, sizeof vp); glViewport(vp[0], vp[1], vp[2], vp[3]); }
  if (!v || dr[0] != g_gl.dr[0] || dr[1] != g_gl.dr[1]) { g_gl.dr[0] = dr[0]; g_gl.dr[1] = dr[1]; glDepthRangef(dr[0], dr[1]); }
  if (!v || blend != g_gl.blend) {
    g_gl.blend = blend;
    if (blend == 0x00010001) glDisable(GL_BLEND);
    else {
      glEnable(GL_BLEND);
      glBlendFuncSeparate(BlendFactor(blend & 31), BlendFactor((blend >> 8) & 31), BlendFactor((blend >> 16) & 31), BlendFactor((blend >> 24) & 31));
      glBlendEquationSeparate(BlendOp((blend >> 5) & 7), BlendOp((blend >> 21) & 7));
    }
  }
  if (!v || depth != g_gl.depth) {
    g_gl.depth = depth;
    uint32_t func = (depth >> 4) & 7;
    if (g_skipPrepass && func == 2) func = 3;  // sem pré-passe de profundidade: EQUAL vira LEQUAL
    if (depth & 2) { glEnable(GL_DEPTH_TEST); glDepthFunc(GL_NEVER + func); } else glDisable(GL_DEPTH_TEST);
    glDepthMask((depth & 4) ? GL_TRUE : GL_FALSE);
  }
  if (!v || mode != g_gl.mode) {
    g_gl.mode = mode;
    uint32_t cull = mode & 3;
    if (g_config.cull && (cull == 1 || cull == 2)) {
      glEnable(GL_CULL_FACE);
      glCullFace(cull == 1 ? GL_FRONT : GL_BACK);
      glFrontFace((mode & 4) ? GL_CCW : GL_CW);  // o VS inverte y: a ordem dos vértices na tela também inverte
    } else glDisable(GL_CULL_FACE);
  }
  if (!v || mask != g_gl.mask) { g_gl.mask = mask; glColorMask(mask & 1, (mask >> 1) & 1, (mask >> 2) & 1, (mask >> 3) & 1); }
  g_gl.fixedValid = true;
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
// Paleta de ossos para o VS: lê a textura float4 1024x1 (fmt 38) da memória do guest e envia só a janela de
// texels [4*osso_min, 4*osso_max+4) que o desenho usa (ossos = atributo inteiro 8_8_8_8 do VS).
std::unordered_map<uint64_t, std::pair<int, int>> g_boneRange;  // serial do atributo -> (min, max)
// Remapeamento de ossos por malha (Mali: ~55 ossos cabem): os índices únicos viram slots 0..n-1, o atributo é
// reescrito uma vez por malha (payload próprio, VBO em cache pelo X1) e só os ossos usados vão para VT<t>.
struct BoneRemap { std::vector<int> bones; uint8_t slotOf[256]; buffers::Ref payload; };
std::unordered_map<uint64_t, BoneRemap> g_boneRemap;  // serial do atributo original -> remapeamento
buffers::Ref g_drawRemap[kMaxInputs];                 // por desenho: payload remapeado a ligar no lugar do original
const BoneRemap* BoneRemapFor(const buffers::Ref& data, uint32_t vertexCount) {
  if (!data || data->layout.type != GL_UNSIGNED_BYTE || data->layout.components != 4 || data->layout.normalized) return nullptr;
  auto it = g_boneRemap.find(data->serial);
  if (it != g_boneRemap.end()) return &it->second;
  BoneRemap r{};
  bool used[256] = {};
  size_t n = std::min<size_t>(size_t(vertexCount) * 4, data->bytes.size());
  for (size_t i = 0; i < n; ++i) used[data->bytes[i]] = true;
  for (int b = 0; b < 256; ++b) if (used[b]) { r.slotOf[b] = uint8_t(r.bones.size()); r.bones.push_back(b); }
  auto pl = std::make_shared<buffers::Payload>();
  pl->serial = data->serial ^ 0x426F6E65A5A5A5A5ull; pl->sample = data->sample; pl->input = data->input; pl->endian = data->endian;
  pl->count = data->count; pl->layout = data->layout; pl->raw = data->raw; pl->bytes = data->bytes;
  pl->cacheable.store(true);
  for (size_t i = 0; i < pl->bytes.size(); ++i) pl->bytes[i] = r.slotOf[pl->bytes[i]];
  r.payload = pl;
  return &(g_boneRemap[data->serial] = std::move(r));
}
// Conversões estáticas por malha (float4 por vértice) em cache por serial: índices de osso, pesos, posição, direções.
// Usado pelas threads do skinning (trava; o dono do vetor é o shared_ptr devolvido, o teto pode limpar o mapa).
std::mutex g_floatMx;
std::unordered_map<uint64_t, std::shared_ptr<const std::vector<float>>> g_floatCache;
std::atomic<uint64_t> g_convNs{0}, g_convMisses{0}, g_convVertices{0};  // perfil: conversões float4 que erraram o cache
std::shared_ptr<const std::vector<float>> FloatsFor(const buffers::Ref& data, uint32_t count) {
  if (!data) return nullptr;
  {
    std::lock_guard<std::mutex> lk(g_floatMx);
    auto it = g_floatCache.find(data->serial);
    if (it != g_floatCache.end()) return it->second;
  }
  auto t0 = Clock::now();
  auto v = std::make_shared<std::vector<float>>(size_t(count) * 4);
  if (!ConvertInput(data->input, data->raw.data(), data->endian, 0, count, v->data())) return nullptr;
  g_convNs += uint64_t(Ms(t0, Clock::now()) * 1e6); ++g_convMisses; g_convVertices += count;
  std::lock_guard<std::mutex> lk(g_floatMx);
  if (g_floatCache.size() > 4096) g_floatCache.clear();  // teto: serial novo a cada quadro não cresce sem fim
  g_floatCache[data->serial] = v;
  return v;
}
// VBOs transitórios para os streams deformados (anel por quadro × atributo)
GLuint g_transient[3][kMaxInputs] = {};
const std::vector<float>* g_skinOut[kMaxInputs] = {};  // stream deformado deste desenho (dono: o job do worker)
// C1-H4: skinning na CPU começa NA CAPTURA (thread do jogo): ao gravar o desenho, a paleta é copiada naquele instante
// e o job vai para um pool de NIER_SKIN_THREADS (padrão 2) threads; malha+paleta iguais no mesmo quadro (mapa de sombra,
// passe reduzido, passe principal) reaproveitam o mesmo job. A thread GL só encontra pronto e envia o VBO.
struct SkinJob {
  Cmd cmd;                     // cópia do comando (refs dos vértices)
  int kind = 0;                // 1 = paleta em textura (VTF), 2 = paleta em constantes (VC)
  std::vector<uint8_t> pal;    // cópia da paleta no instante da captura (VTF: textura 1D float4; VC: banco de 256 vec4)
  uint32_t palWidth = 0, palEndian = 0;
  int relBase = 0, relBones = 0;
  std::vector<std::pair<int, std::vector<float>>> outs; size_t nOuts = 0;
  double ms = 0; uint32_t vertices = 0; bool ok = false;
  std::atomic<bool> done{false};
  std::vector<float>& Out(int input, size_t floats) {
    if (nOuts == outs.size()) outs.emplace_back();
    auto& o = outs[nOuts++]; o.first = input; o.second.resize(floats); return o.second;
  }
};
bool g_transientUsed[kMaxInputs] = {};
uint64_t g_skinnedVertices = 0, g_skinnedDraws = 0;
uint64_t g_relDraws = 0, g_relVertices = 0, g_relIndices = 0;  // desenhos com VS de constante relativa (VC[k + addr]): índice dinâmico de uniforme
double g_skinMs = 0, g_skinWaitMs = 0;
// Entradas do skinning: índices (fmt 6 inteiro), pesos (fmt 6 normalizado com soma ≈ 1), posições (57/38), direções
// (16/7/17). Devolve skinConst = (idx<<8 | w) + 1 para a variante do programa, ou 0.
// Detecção na thread GL sem tocar o cache de conversões (só o worker usa): converte só o 1º vértice.
bool FirstVertex(const buffers::Ref& data, float out[4]) {
  if (!data) return false;
  return ConvertInput(data->input, data->raw.data(), data->endian, 0, 1, out);
}
int SkinInputs(const Cmd& c, int& idxIn, int& wIn, std::vector<int>* posIn, std::vector<int>* dirIn) {
  idxIn = -1; wIn = -1;
  if (!c.vs || !c.vs->textureMask || !CpuSkin()) return 0;
  for (uint32_t i = 0; i < c.inputCount && i < c.vs->inputs.size(); ++i) {
    const auto& in = c.vs->inputs[i];
    if (in.format == 6 && in.isInteger) { if (idxIn < 0) idxIn = int(i); }
    else if (in.format == 6 && !in.isInteger) {
      float v[4]; if (wIn < 0 && c.vertexCount && FirstVertex(c.vertexData[i], v)) { float sum = v[0] + v[1] + v[2] + v[3]; if (std::fabs(sum - 1.0f) < 0.03f) wIn = int(i); }
    } else if (in.format == 57 || in.format == 38) { if (posIn) posIn->push_back(int(i)); }
    else if (in.format == 16 || in.format == 7 || in.format == 17) { if (dirIn) dirIn->push_back(int(i)); }
  }
  if (idxIn < 0 || wIn < 0) return 0;
  return ((idxIn << 8) | wIn) + 1;
}
bool CpuSkinCompute(SkinJob& j) {
  const Cmd& c = j.cmd;
  int idxIn = -1, wIn = -1; std::vector<int> posIn, dirIn;
  if (!SkinInputs(c, idxIn, wIn, &posIn, &dirIn) || posIn.empty()) return false;
  struct { uint32_t width, endian; } t{j.palWidth, j.palEndian};
  const uint8_t* pal = j.pal.data();
  auto idx = FloatsFor(c.vertexData[idxIn], c.vertexCount);
  auto wgt = FloatsFor(c.vertexData[wIn], c.vertexCount);
  if (!idx || !wgt) return false;
  auto t0 = Clock::now();
  // matrizes 3x4 dos ossos usados (linha c = texel 4b+c, bytes 8in32)
  thread_local std::vector<float> rows; rows.assign(size_t(t.width) * 4, 0.0f);
  thread_local std::vector<uint8_t> have; have.assign(256, 0);
  const uint32_t n = c.vertexCount;
  for (uint32_t v = 0; v < n * 4; ++v) { int b = int((*idx)[v]); if (b >= 0 && b < 256 && !have[b]) { have[b] = 1; for (int k = 0; k < 12 && (4 * b + 2) < int(t.width); ++k) { uint8_t e[4]; std::memcpy(e, pal + (4 * b) * 16 + 4 * k, 4); xenos::Swap(e, 4, t.endian); std::memcpy(&rows[size_t(4 * b) * 4 + k], e, 4); } } }
  // Uma mistura de matriz por vértice (Σ w_b M_b, 3 linhas) aplicada a todos os streams (posição com w=1, direções
  // com w=0). Vetores de 4 floats do GCC: NEON no A53, SSE no PC.
  typedef float v4f __attribute__((vector_size(16)));
  std::vector<std::shared_ptr<const std::vector<float>>> srcs; std::vector<int> outs; std::vector<bool> points;
  for (int i : posIn) { auto src = FloatsFor(c.vertexData[i], n); if (!src) return false; srcs.push_back(src); outs.push_back(i); points.push_back(true); }
  for (int i : dirIn) { auto src = FloatsFor(c.vertexData[i], n); if (!src) continue; srcs.push_back(src); outs.push_back(i); points.push_back(false); }
  std::vector<float*> dst; for (int i : outs) dst.push_back(j.Out(i, size_t(n) * 4).data());
  const float* W = wgt->data(); const float* I = idx->data();
  // colunas da 3x4 de cada osso (lane 3 = 0): saída = C0*x + C1*y + C2*z (+ C3 se ponto), 4 FMAs, sem soma horizontal
  thread_local v4f cols[256][4];
  for (int b = 0; b < 256; ++b) if (have[b] && (4 * b + 2) < int(t.width)) { const float* m = &rows[size_t(4 * b) * 4]; for (int k = 0; k < 4; ++k) cols[b][k] = v4f{m[k], m[4 + k], m[8 + k], 0.0f}; }
  else if (have[b]) for (int k = 0; k < 4; ++k) cols[b][k] = v4f{0, 0, 0, 0};
  const size_t ns = srcs.size(); const float* sp[kMaxInputs]; bool pt[kMaxInputs];
  for (size_t sIdx = 0; sIdx < ns; ++sIdx) { sp[sIdx] = srcs[sIdx]->data(); pt[sIdx] = points[sIdx]; }
  for (uint32_t v = 0; v < n; ++v) {
    v4f C0 = {0, 0, 0, 0}, C1 = C0, C2 = C0, C3 = C0;
    for (int k = 0; k < 4; ++k) {
      float w = W[size_t(v) * 4 + k]; if (w == 0.0f) continue;
      int b = int(I[size_t(v) * 4 + k]); if (b < 0 || b > 255) continue;
      const v4f* m = cols[b];
      C0 += w * m[0]; C1 += w * m[1]; C2 += w * m[2]; C3 += w * m[3];
    }
    for (size_t sIdx = 0; sIdx < ns; ++sIdx) {
      const float* q = sp[sIdx] + size_t(v) * 4;
      v4f o = C0 * q[0] + C1 * q[1] + C2 * q[2];
      if (pt[sIdx]) o += C3;
      o[3] = q[3];
      std::memcpy(dst[sIdx] + size_t(v) * 4, &o, 16);
    }
  }
  (void)t0; j.vertices = n;
  if (GlProfile() && !outs.empty()) {  // diagnóstico: caixa da posição antes/depois (1ª vez por malha)
    thread_local std::set<std::pair<uint64_t, uint32_t>> seen;
    if (seen.insert({c.vs->key, n}).second && seen.size() < 200) {
      float lo[2][3] = {{1e30f, 1e30f, 1e30f}, {1e30f, 1e30f, 1e30f}}, hi[2][3] = {{-1e30f, -1e30f, -1e30f}, {-1e30f, -1e30f, -1e30f}};
      int maxBone = 0;
      for (uint32_t v = 0; v < n; ++v) {
        for (int a = 0; a < 3; ++a) {
          float in = (*srcs[0])[size_t(v) * 4 + a], out = dst[0][size_t(v) * 4 + a];
          lo[0][a] = std::min(lo[0][a], in); hi[0][a] = std::max(hi[0][a], in); lo[1][a] = std::min(lo[1][a], out); hi[1][a] = std::max(hi[1][a], out);
        }
        for (int k = 0; k < 4; ++k) if (W[size_t(v) * 4 + k] != 0.0f) maxBone = std::max(maxBone, int(I[size_t(v) * 4 + k]));
      }
      const float* m0 = &rows[0];
      std::fprintf(stderr, "[nier-osso-caixa] vs %016llX vtx %u maxOsso %d antes (%.2f,%.2f,%.2f)-(%.2f,%.2f,%.2f) depois (%.2f,%.2f,%.2f)-(%.2f,%.2f,%.2f) osso0 (%.3g %.3g %.3g %.3g | %.3g %.3g %.3g %.3g | %.3g %.3g %.3g %.3g)\n",
                   (unsigned long long)c.vs->key, n, maxBone, lo[0][0], lo[0][1], lo[0][2], hi[0][0], hi[0][1], hi[0][2], lo[1][0], lo[1][1], lo[1][2], hi[1][0], hi[1][1], hi[1][2],
                   m0[0], m0[1], m0[2], m0[3], m0[4], m0[5], m0[6], m0[7], m0[8], m0[9], m0[10], m0[11]);
    }
  }
  return true;
}
// C1-G8: entradas do skinning de paleta em constantes. Índices = fmt 6 inteiro; pesos = fmt 6 normalizado com soma ≈ 1
// (obrigatório se o VS mistura mais de um osso); posições 57/38; direções 16/7/17.
bool RelSkinInputs(const Cmd& c, const RelSkinInfo& ri, int& idxIn, int& wIn, std::vector<int>& posIn, std::vector<int>& dirIn) {
  idxIn = -1; wIn = -1; posIn.clear(); dirIn.clear();
  if (!ri.ok) return false;
  for (uint32_t i = 0; i < c.inputCount && i < c.vs->inputs.size(); ++i) {
    const auto& in = c.vs->inputs[i];
    float v[4];
    if (!c.vertexCount || !FirstVertex(c.vertexData[i], v)) continue;
    if (in.format == 6 && in.isInteger) { if (idxIn < 0) idxIn = int(i); }
    else if (in.format == 6) { if (wIn < 0) { float sum = v[0] + v[1] + v[2] + v[3]; if (std::fabs(sum - 1.0f) < 0.03f) wIn = int(i); } }
    else if (in.format == 57 || in.format == 38) posIn.push_back(int(i));
    else if (in.format == 16 || in.format == 7 || in.format == 17) dirIn.push_back(int(i));
  }
  return idxIn >= 0 && !posIn.empty() && (ri.bones == 1 || wIn >= 0);
}
bool RelSkinCompute(SkinJob& j) {
  const Cmd& c = j.cmd; const uint8_t* bank = j.pal.data();
  RelSkinInfo ri; ri.ok = true; ri.base = j.relBase; ri.bones = j.relBones;
  int idxIn, wIn; std::vector<int> posIn, dirIn;
  if (!RelSkinInputs(c, ri, idxIn, wIn, posIn, dirIn)) return false;
  auto t0 = Clock::now();
  typedef float v4f __attribute__((vector_size(16)));
  const uint32_t n = c.vertexCount;
  auto Ip = FloatsFor(c.vertexData[idxIn], n), Wp = wIn >= 0 ? FloatsFor(c.vertexData[wIn], n) : nullptr;
  if (!Ip || (wIn >= 0 && !Wp)) return false;
  const float* I = Ip->data(); const float* W = Wp ? Wp->data() : nullptr;
  // linhas dos ossos (constante B + 3b + k, big-endian), lidas uma vez por osso usado
  thread_local v4f rows[256][3]; thread_local uint8_t have[256];
  std::memset(have, 0, sizeof have);
  auto bone = [&](int b) -> const v4f* {
    if (!have[b]) {
      have[b] = 1;
      for (int k = 0; k < 3; ++k) { int reg = std::clamp(ri.base + 3 * b + k, 0, 255); for (int e = 0; e < 4; ++e) rows[b][k][e] = BEF(bank + reg * 16 + 4 * e); }
    }
    return rows[b];
  };
  std::vector<std::shared_ptr<const std::vector<float>>> holders; std::vector<const float*> srcs; std::vector<int> outs; std::vector<bool> points;
  for (int i : posIn) { auto h = FloatsFor(c.vertexData[i], n); if (!h) return false; holders.push_back(h); srcs.push_back(h->data()); outs.push_back(i); points.push_back(true); }
  for (int i : dirIn) { auto h = FloatsFor(c.vertexData[i], n); if (!h) continue; holders.push_back(h); srcs.push_back(h->data()); outs.push_back(i); points.push_back(false); }
  std::vector<float*> dst; for (int i : outs) dst.push_back(j.Out(i, size_t(n) * 4).data());
  // colunas por osso (ver CpuSkinCompute): 4 FMAs por stream, sem soma horizontal
  thread_local v4f cols[256][4]; thread_local uint8_t haveCols[256];
  std::memset(haveCols, 0, sizeof haveCols);
  auto boneCols = [&](int b) -> const v4f* {
    if (!haveCols[b]) { haveCols[b] = 1; const v4f* m = bone(b); for (int k = 0; k < 4; ++k) cols[b][k] = v4f{m[0][k], m[1][k], m[2][k], 0.0f}; }
    return cols[b];
  };
  const size_t ns = srcs.size(); bool pt[kMaxInputs];
  for (size_t sIdx = 0; sIdx < ns; ++sIdx) pt[sIdx] = points[sIdx];
  for (uint32_t v = 0; v < n; ++v) {
    v4f C0 = {0, 0, 0, 0}, C1 = C0, C2 = C0, C3 = C0; float wsum = 0;
    for (int k = 0; k < ri.bones; ++k) {
      float w = W ? W[size_t(v) * 4 + k] : 1.0f; if (w == 0.0f) continue;
      int b = int(I[size_t(v) * 4 + k]); if (b < 0 || b > 255) continue;
      const v4f* m = boneCols(b);
      C0 += w * m[0]; C1 += w * m[1]; C2 += w * m[2]; C3 += w * m[3]; wsum += w;
    }
    if (wsum > 0.0f && wsum != 1.0f) { float iw = 1.0f / wsum; C0 *= iw; C1 *= iw; C2 *= iw; C3 *= iw; }
    for (size_t sIdx = 0; sIdx < ns; ++sIdx) {
      const float* q = srcs[sIdx] + size_t(v) * 4;
      v4f o = C0 * q[0] + C1 * q[1] + C2 * q[2];
      if (pt[sIdx]) o += C3;
      o[3] = q[3];
      std::memcpy(dst[sIdx] + size_t(v) * 4, &o, 16);
    }
  }
  (void)t0; j.vertices = n;
  return true;
}
// Pool de threads do skinning
std::mutex g_skinMx; std::condition_variable g_skinCv, g_skinDoneCv;
std::deque<std::shared_ptr<SkinJob>> g_skinQueue;
void SkinWorker() {
  // prioridade abaixo da thread GL e do jogo (NIER_SKIN_NICE, padrão 5): a thread GL não pode perder núcleo para o skin
  static int nice = std::getenv("NIER_SKIN_NICE") ? std::atoi(std::getenv("NIER_SKIN_NICE")) : 5;
  setpriority(PRIO_PROCESS, pid_t(syscall(SYS_gettid)), nice);
  for (;;) {
    std::shared_ptr<SkinJob> j;
    {
      std::unique_lock<std::mutex> lk(g_skinMx);
      g_skinCv.wait(lk, [] { return !g_skinQueue.empty(); });
      j = std::move(g_skinQueue.front()); g_skinQueue.pop_front();
    }
    auto t0 = Clock::now();
    j->ok = j->kind == 1 ? CpuSkinCompute(*j) : RelSkinCompute(*j);
    j->ms = Ms(t0, Clock::now());
    { std::lock_guard<std::mutex> lk(g_skinMx); j->done.store(true, std::memory_order_release); }
    g_skinDoneCv.notify_all();
  }
}
void WaitSkinJob(SkinJob& j) {
  if (j.done.load(std::memory_order_acquire)) return;
  auto t0 = Clock::now();
  std::unique_lock<std::mutex> lk(g_skinMx);
  g_skinDoneCv.wait(lk, [&] { return j.done.load(std::memory_order_acquire); });
  g_skinWaitMs += Ms(t0, Clock::now());
}
// No desenho (thread GL): liga os streams deformados do job deste comando (idempotente).
void ConsumeSkinJob(const Frame& f, const Cmd& c) {
  if (c.skinJob < 0 || size_t(c.skinJob) >= f.skinJobs.size()) return;
  SkinJob& j = *f.skinJobs[size_t(c.skinJob)];
  WaitSkinJob(j);
  if (!j.ok) return;
  for (size_t k = 0; k < j.nOuts; ++k) { g_skinOut[j.outs[k].first] = &j.outs[k].second; g_transientUsed[j.outs[k].first] = true; }
  if (j.vertices) { g_skinMs += j.ms; g_skinnedVertices += j.vertices; ++g_skinnedDraws; j.vertices = 0; }
}
void UploadVertexTextures(const Frame& f, const Cmd& c, Program& p, const uint8_t* state) {
  for (auto& r : g_drawRemap) r.reset();
  for (auto& u : g_transientUsed) u = false;
  if (CpuSkin()) {
    ConsumeSkinJob(f, c);  // sem job (ossos/pesos não reconhecidos): identidade em ALU, vértice sem deformar
    return;
  }
  // 1) remapeamento compacto (ossos únicos da malha cabem em k/4 slots)
  if (p.vtf.size() == 1) {
    const auto& vt = p.vtf[0];
    for (uint32_t i = 0; i < c.inputCount && i < c.vs->inputs.size(); ++i) {
      const auto& in = c.vs->inputs[i];
      if (in.format != 6 || !in.isInteger) continue;
      const BoneRemap* rm = BoneRemapFor(c.vertexData[i], c.vertexCount);
      if (!rm) break;
      int nb = int(rm->bones.size());
      if (GlProfile()) { static std::set<uint64_t> seen; if (seen.insert(c.vertexData[i]->serial).second) std::fprintf(stderr, "[nier-perfil] malha com ossos: %d únicos (%s em %d texels)\n", nb, 4 * nb <= vt.k ? "cabem" : "NÃO cabem", vt.k); }
      if (4 * nb > vt.k) break;  // não cabe: cai na janela min..max abaixo
      const uint8_t* fetch = Field(state, kFetchConstants + 24 * vt.slot);
      auto t = xenos::DecodeFetch(fetch);
      if (t.format != xenos::k32323232Float || t.height != 1 || t.tiled) break;
      static std::vector<float> vals; vals.resize(size_t(nb) * 16);
      bool ok = true;
      for (int j = 0; j < nb && ok; ++j) {
        const uint8_t* src = Physical(t.base + uint32_t(rm->bones[j]) * 64, 64);
        if (!src) { ok = false; break; }
        for (int k = 0; k < 16; ++k) { uint8_t e[4]; std::memcpy(e, src + 4 * k, 4); xenos::Swap(e, 4, t.endian); std::memcpy(&vals[size_t(j) * 16 + k], e, 4); }
      }
      if (!ok) break;
      if (vt.loc >= 0) { g_vtVec4Sent += uint64_t(nb) * 4; glUniform4fv(vt.loc, nb * 4, vals.data()); }
      if (vt.params >= 0) glUniform2f(vt.params, float(t.width), 0.0f);
      g_drawRemap[i] = rm->payload;
      return;
    }
  }
  int bmin = 0, bmax = -1;
  for (uint32_t i = 0; i < c.inputCount && i < c.vs->inputs.size(); ++i) {
    const auto& in = c.vs->inputs[i];
    if (in.format != 6 || !in.isInteger) continue;
    int lo = 1 << 30, hi = -1;
    uint64_t serial = c.vertexData[i] ? c.vertexData[i]->serial : 0;
    auto cached = serial ? g_boneRange.find(serial) : g_boneRange.end();
    if (cached != g_boneRange.end()) { lo = cached->second.first; hi = cached->second.second; }
    else {
      const float* v = nullptr; static std::vector<float> tmp;
      if (c.vertexData[i]) {
        tmp.resize(size_t(c.vertexCount) * 4);
        ConvertInput(c.vertexData[i]->input, c.vertexData[i]->raw.data(), c.vertexData[i]->endian, 0, c.vertexCount, tmp.data());
        v = tmp.data();
      } else if (c.inputs[i] != 0) v = reinterpret_cast<const float*>(f.blob.data() + c.inputs[i]);
      if (!v) continue;
      for (size_t k = 0, n = size_t(c.vertexCount) * 4; k < n; ++k) { int b = int(v[k]); lo = std::min(lo, b); hi = std::max(hi, b); }
      if (serial) g_boneRange[serial] = {lo, hi};
    }
    if (hi < 0) continue;
    if (bmax < 0) { bmin = lo; bmax = hi; } else { bmin = std::min(bmin, lo); bmax = std::max(bmax, hi); }
  }
  for (const auto& vt : p.vtf) {
    const uint8_t* fetch = Field(state, kFetchConstants + 24 * vt.slot);
    auto t = xenos::DecodeFetch(fetch);
    static std::map<uint32_t, bool> warned;
    if (t.format != xenos::k32323232Float || t.height != 1 || t.tiled) {
      if (!warned[t.format]) { warned[t.format] = true; std::fprintf(stderr, "[nier-gl] textura no VS fmt %u %ux%u tiled %d: só float4 1024x1 linear é emulada\n", t.format, t.width, t.height, int(t.tiled)); }
      if (vt.params >= 0) glUniform2f(vt.params, float(t.width), 0.0f);
      continue;
    }
    int base = bmax >= 0 ? std::clamp(4 * bmin, 0, int(t.width)) : 0;
    int n = bmax >= 0 ? std::min(vt.k, std::max(0, 4 * (bmax - bmin + 1))) : std::min(vt.k, int(t.width));
    n = std::min(n, int(t.width) - base);
    if (bmax >= 0 && 4 * (bmax - bmin + 1) > vt.k) { static bool once = false; if (!once) { once = true; std::fprintf(stderr, "[nier-gl] textura no VS: %d ossos não cabem em %d texels\n", bmax - bmin + 1, vt.k); } }
    const uint8_t* src = Physical(t.base + uint32_t(base) * 16, uint32_t(n) * 16);
    if (!src || n <= 0) continue;
    static std::vector<float> vals; vals.resize(size_t(n) * 4);
    for (int k = 0; k < n * 4; ++k) {
      uint8_t e[4]; std::memcpy(e, src + 4 * k, 4); xenos::Swap(e, 4, t.endian);
      std::memcpy(&vals[k], e, 4);
    }
    if (vt.loc >= 0) { g_vtVec4Sent += uint64_t(n); glUniform4fv(vt.loc, n, vals.data()); }
    if (vt.params >= 0) glUniform2f(vt.params, float(t.width), float(base));
  }
}
// Descarte no CPU: AABB do VBO de posição (1x por serial) × MVP do desenho (constantes do estado). Fora do
// frustum (os 8 cantos atrás do mesmo plano) ou < 2 px na tela (todos os cantos à frente) = não desenha.
bool CullDraw(const Frame& f, const Cmd& c) {
  static bool off = std::getenv("NIER_NO_CULL_AABB") != nullptr;
  if (off || !c.vs || !c.ps) return false;
  static std::unordered_map<uint64_t, CullInfo> infos;  // por VS (o alpha test do PS não muda o VS)
  auto ci = infos.find(c.vs->key);
  if (ci == infos.end()) ci = infos.emplace(c.vs->key, ParseCull(c.vs->source)).first;
  const CullInfo& cull = ci->second;
  if (!cull.ok) { ++g_cullNoPattern; return false; }
  int pi = cull.posInput;
  if (pi < 0 || uint32_t(pi) >= c.inputCount || !c.vertexData[pi]) { ++g_cullNoData; return false; }
  ++g_cullEval;
  const auto& data = c.vertexData[pi];
  auto it = g_aabb.find(data->serial);
  if (it == g_aabb.end()) {
    Aabb a{{1e30f, 1e30f, 1e30f}, {-1e30f, -1e30f, -1e30f}, false};
    static std::vector<float> tmp; tmp.resize(size_t(c.vertexCount) * 4);
    if (ConvertInput(data->input, data->raw.data(), data->endian, 0, c.vertexCount, tmp.data()) && c.vertexCount > 0) {
      for (uint32_t v = 0; v < c.vertexCount; ++v) for (int k = 0; k < 3; ++k) { float x = tmp[v * 4 + k]; a.lo[k] = std::min(a.lo[k], x); a.hi[k] = std::max(a.hi[k], x); }
      a.ok = std::isfinite(a.lo[0]) && std::isfinite(a.hi[0]) && std::isfinite(a.lo[1]) && std::isfinite(a.hi[1]) && std::isfinite(a.lo[2]) && std::isfinite(a.hi[2]);
    }
    it = g_aabb.emplace(data->serial, a).first;
  }
  const Aabb& a = it->second;
  if (!a.ok) return false;
  const uint8_t* state = f.blob.data() + c.state;
  float m[4][4];
  size_t literals = c.vs->literals.size() / 4;
  for (int r = 0; r < 4; ++r) {
    int k = cull.row[r];
    if (k < 0 || size_t(k) >= c.vs->constMap.size()) return false;
    size_t reg = c.vs->constMap[k];
    if (reg >= 256 - literals) return false;
    for (int cc = 0; cc < 4; ++cc) m[r][cc] = BEF(Field(state, kVertexConstants + reg * 16 + 4 * cc));
  }
  int outside[6] = {};  // -x +x -y +y -z +z (clip D3D: -w<=x<=w, -w<=y<=w, 0<=z<=w)
  float sx0 = 1e30f, sx1 = -1e30f, sy0 = 1e30f, sy1 = -1e30f; bool allFront = true;
  for (int i = 0; i < 8; ++i) {
    float v[4] = {(i & 1) ? a.hi[0] : a.lo[0], (i & 2) ? a.hi[1] : a.lo[1], (i & 4) ? a.hi[2] : a.lo[2], 1.0f};
    float cl[4];
    for (int r = 0; r < 4; ++r) cl[r] = m[r][0] * v[0] + m[r][1] * v[1] + m[r][2] * v[2] + m[r][3] * v[3];
    float w = cl[3];
    if (cl[0] < -w) ++outside[0]; if (cl[0] > w) ++outside[1];
    if (cl[1] < -w) ++outside[2]; if (cl[1] > w) ++outside[3];
    if (cl[2] < 0) ++outside[4]; if (cl[2] > w) ++outside[5];
    if (w <= 1e-6f) allFront = false;
    else { float x = cl[0] / w, y = cl[1] / w; sx0 = std::min(sx0, x); sx1 = std::max(sx1, x); sy0 = std::min(sy0, y); sy1 = std::max(sy1, y); }
  }
  for (int k = 0; k < 6; ++k) if (outside[k] == 8) return true;
  if (allFront) {
    float px = (sx1 - sx0) * 0.5f * float(g_config.renderWidth), py = (sy1 - sy0) * 0.5f * float(g_config.renderHeight);
    if (px < 2.0f && py < 2.0f) return true;
    // LOD por área (só com autorização do dono): NIER_CULL_AREA=<fração da tela>, ex. 0.01 = desenhos de cenário
    // cuja caixa projetada cobre menos de 1 % da tela não são desenhados. Padrão 0 = desligado.
    static float areaFrac = std::getenv("NIER_CULL_AREA") ? float(std::atof(std::getenv("NIER_CULL_AREA"))) : 0.0f;
    if (areaFrac > 0 && px * py < areaFrac * float(g_config.renderWidth) * float(g_config.renderHeight)) return true;
  }
  return false;
}
struct Redirect { uint64_t key = 0; int w = 0, h = 0; bool passStart = false; bool skip = false; };
uint32_t g_bloomSkips = 0;
void ReplayDraw(const Frame& f, const Cmd& c, const Redirect& rd) {
  const uint8_t* state = f.blob.data() + c.state;
  auto t0 = Clock::now();
  for (auto& r : g_drawRemap) r.reset();  // só UploadVertexTextures liga um remapeamento para ESTE desenho
  for (auto& u : g_transientUsed) u = false;  // idem para os streams deformados na CPU
  if (g_floatCache.size() > 4096) g_floatCache.clear();  // teto: serial novo a cada quadro não pode crescer a memória sem fim
  if (rd.skip) return;  // nível de bloom mantido do quadro anterior
  Target& drawTarget = rd.key ? DirectTarget(rd.key, rd.w, rd.h) : CurrentTarget(state);
  if (rd.key) { ++g_directDraws; if (rd.passStart) drawTarget.from3D = false; if (!c.up) drawTarget.from3D = true; }
  if (!c.up) drawTarget.drew3DFrame = g_frames.load();
  auto t1 = Clock::now();
  g_pTarget += Ms(t0, t1);
  ApplyFixedState(state);
  static bool flat3d = std::getenv("NIER_FLAT_3D") != nullptr;  // diagnóstico: 3D em vermelho sólido
  static bool keepAlphaTest = std::getenv("NIER_ALPHA_TEST_SEMPRE") != nullptr;  // diagnóstico: variante única
  uint32_t colorControl = BE32(Field(state, kColorControl));
  int skinIdx = -1, skinW = -1;
  int skinConst = SkinInputs(c, skinIdx, skinW, nullptr, nullptr);
  const RelSkinInfo& relInfo = RelSkinFor(c.vs);
  int relBase = -1;
  if (relInfo.ok) { int ri, rw; std::vector<int> rp, rd; if (RelSkinInputs(c, relInfo, ri, rw, rp, rd)) relBase = relInfo.base; }
  // diagnóstico NIER_DIAG_VS=<hex dos 6 primeiros dígitos do VS>: só esse VS, em vermelho, sem profundidade nem cull
  static const char* diagVs = std::getenv("NIER_DIAG_VS");
  bool diag = false;
  if (diagVs) { char b[20]; std::snprintf(b, sizeof b, "%016llX", (unsigned long long)c.vs->key); diag = std::strncmp(b, diagVs, std::strlen(diagVs)) == 0; if (!diag && !c.up) return; }
  static bool diagFlat = !std::getenv("NIER_DIAG_PS"), diagDepth = std::getenv("NIER_DIAG_DEPTH") != nullptr;
  Program* p = ProgramFor(c.vs, c.ps, (flat3d || (diag && diagFlat)) && !c.up, keepAlphaTest || (colorControl & 8), skinConst, relBase);
  if (diag) { if (!diagDepth) glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_BLEND); glColorMask(1, 1, 1, 1); g_gl.fixedValid = false; }
  if (!p) { Skip("programa não compilou/linkou"); return; }
  UseProgram(p->id);
  if (relBase >= 0) ConsumeSkinJob(f, c);
  UploadConstants(p->vc, p->vcCount, Field(state, kVertexConstants), c.vs, p->lastVc, p->vcSeq);
  UploadConstants(p->pc, p->pcCount, Field(state, kPixelConstants), c.ps, p->lastPc, p->pcSeq);
  if (p->lastBools.size() != p->bools.size()) p->lastBools.assign(p->bools.size(), -1);
  for (size_t i = 0; i < p->bools.size(); ++i) {
    auto [b, loc] = p->bools[i];
    int8_t v = int8_t((BE32(Field(state, kBoolConstants + 4 * (b >> 5))) >> (b & 31)) & 1);
    if (p->lastBools[i] != v) { p->lastBools[i] = v; glUniform1i(loc, v); }
  }
  if (p->alphaFunc >= 0) { int af = (colorControl & 8) ? int(colorControl & 7) : 7; if (af != p->lastAlphaFunc) { p->lastAlphaFunc = af; glUniform1i(p->alphaFunc, af); } }
  if (p->alphaRef >= 0) { float ar = BEF(Field(state, 0x2904)); if (ar != p->lastAlphaRef) { p->lastAlphaRef = ar; glUniform1f(p->alphaRef, ar); } }
  if (p->lastSizes.size() != p->samplers.size()) p->lastSizes.assign(p->samplers.size(), {-1.f, -1.f});
  if (!p->vtf.empty()) UploadVertexTextures(f, c, *p, state);
  int unit = 0;
  for (auto [slot, sizeLoc] : p->samplers) {
    const uint8_t* fetch = Field(state, kFetchConstants + 24 * slot);
    auto t = xenos::DecodeFetch(fetch);
    // Heurística (07/11): a tela resolvida alimenta desenhos não-UP e também UP quando a origem recebeu
    // desenho 3D no quadro (composição da cena). Tela de UI pura vem da memória do guest (menus sem faixas).
    static bool resolvedInUi = std::getenv("NIER_RESOLVED_IN_UI") != nullptr;
    ActiveUnit(unit);  // TextureFor pode enviar a textura: liga na unidade certa
    GLuint id = (BE32(fetch) & 3) == 2 ? TextureFor(t, !c.up || resolvedInUi) : White();
    BindTexture(unit, id);
    if (sizeLoc >= 0) {
      std::pair<float, float> sz{float(t.width), float(t.height)};
      if (p->lastSizes[unit] != sz) { p->lastSizes[unit] = sz; glUniform2f(sizeLoc, sz.first, sz.second); }
    }
    ++unit;
  }
  auto t2 = Clock::now();
  g_pState += Ms(t1, t2);
  size_t indexOffset = 0;
  if (!ReferenceBuffers() && !g_bufferAuditReference) {
    const auto& stream = g_streamSlots[g_frames.load() % g_streamSlots.size()];
    bool streamBound = false;
    for (uint32_t i = 0; i < c.inputCount; ++i) {
      if (g_transientUsed[i]) {  // stream deformado na CPU neste desenho (float4)
        GLuint& vbo = g_transient[g_frames.load() % 3][i];
        if (!vbo) glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo); streamBound = false;
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(g_skinOut[i]->size() * 4), g_skinOut[i]->data(), GL_STREAM_DRAW);
        glVertexAttribPointer(i, 4, GL_FLOAT, GL_FALSE, 16, nullptr);
        continue;
      }
      const auto& data = g_drawRemap[i] ? g_drawRemap[i] : c.vertexData[i];
      auto layout = data ? data->layout : buffers::Layout{};
      size_t offset = 0;
      if (g_drawRemap[i] || (c.residentInputs & (1u << i))) { BindSnapshot(GL_ARRAY_BUFFER, data); streamBound = false; }
      else {
        if (!streamBound) glBindBuffer(GL_ARRAY_BUFFER, stream.vertex);
        streamBound = true; offset = c.streamInputs[i];
      }
      glVertexAttribPointer(i, layout.components, layout.type, layout.normalized ? GL_TRUE : GL_FALSE,
                            layout.bytes, reinterpret_cast<void*>(offset));
    }
    SetAttribs(c.inputCount);  // cache de estado do C1 (só liga/desliga o que mudou)
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
      glVertexAttribPointer(i, 4, GL_FLOAT, GL_FALSE, 16, reinterpret_cast<void*>(bytes * i));
    }
    SetAttribs(c.inputCount);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_indexBuffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(c.indexCount * 2), c.indexData ? c.indexData->raw.data() : f.blob.data() + c.indices, GL_STREAM_DRAW);
  }
  auto t3 = Clock::now();
  g_pBuffers += Ms(t2, t3);
  g_indicesDrawn += c.indexCount;
  if (GlProfile() && c.vs->textureMask) {  // diagnóstico: desenhos com ossos (VTF), caminho e 1º vértice
    static std::set<std::pair<uint64_t, uint32_t>> seenSkin;
    if (seenSkin.insert({c.vs->key, c.vertexCount}).second && seenSkin.size() < 400) {
      std::string d; char b[128];
      for (uint32_t i = 0; i < c.inputCount && i < c.vs->inputs.size(); ++i) {
        const auto& in = c.vs->inputs[i]; float fv[4];
        std::snprintf(b, sizeof b, " a%u:f%u%s", i, in.format, in.isInteger ? "i" : ""); d += b;
        if (c.vertexCount && FirstVertex(c.vertexData[i], fv)) { std::snprintf(b, sizeof b, "(%.3g,%.3g,%.3g,%.3g)", fv[0], fv[1], fv[2], fv[3]); d += b; }
      }
      int job = c.skinJob;
      auto tf = xenos::DecodeFetch(Field(state, kFetchConstants + 24 * __builtin_ctz(c.vs->textureMask)));
      std::fprintf(stderr, "[nier-osso] vs %016llX ps %016llX vtx %u idx %u tex %u fmt %u w %u skinConst %d job %d ok %d%s\n", (unsigned long long)c.vs->key, (unsigned long long)c.ps->key, c.vertexCount, c.indexCount, c.vs->textureMask, unsigned(tf.format), tf.width, skinConst, job, job >= 0 && size_t(job) < f.skinJobs.size() ? int(f.skinJobs[size_t(job)]->done.load() && f.skinJobs[size_t(job)]->ok) : -1, d.c_str());
    }
  }
  if (p->dynIndex && GlProfile()) {  // diagnóstico (perfil): uma linha por VS com índice dinâmico, formatos dos atributos
    static std::set<uint64_t> seen;
    if (seen.insert(c.vs->key).second) {
      std::string d; char b[96];
      for (uint32_t i = 0; i < c.inputCount && i < c.vs->inputs.size(); ++i) {
        const auto& in = c.vs->inputs[i]; float fv[4]; std::vector<float> tmp; const std::vector<float>* v = nullptr; if (c.vertexCount && FirstVertex(c.vertexData[i], fv)) { tmp.assign(fv, fv + 4); v = &tmp; }
        std::snprintf(b, sizeof b, " a%u:f%u%s%s", i, in.format, in.isInteger ? "i" : "", in.isSigned ? "s" : ""); d += b;
        if (v && c.vertexCount) { std::snprintf(b, sizeof b, "(%.3g,%.3g,%.3g,%.3g)", (*v)[0], (*v)[1], (*v)[2], (*v)[3]); d += b; }
      }
      std::fprintf(stderr, "[nier-dyn] vs %016llX vtx %u idx %u%s\n", (unsigned long long)c.vs->key, c.vertexCount, c.indexCount, d.c_str());
    }
  }
  if (p->dynIndex) { ++g_relDraws; g_relVertices += c.vertexCount; g_relIndices += c.indexCount; }
  if (GlProfile()) g_topDraws.push_back({c.indexCount, c.vertexCount, c.prim, g_drawClassCurrent, uint32_t(c.vs->key >> 40), uint32_t(c.ps->key >> 40), p->vsOps});
  glDrawElements(c.mode, GLsizei(c.indexCount), GL_UNSIGNED_SHORT, reinterpret_cast<void*>(indexOffset));
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
// Resolve de tela cheia seguido de mais desenho no MESMO alvo (nuvens/partículas sobre a cena resolvida): o alvo
// precisa manter o conteúdo, então esse Resolve copia por blit (1 passe) em vez de trocar o FBO e limpar.
std::vector<uint8_t> g_resolveKeep;
// C1-G3 (desempenho do desenho na Mali): classe de cada desenho e ordem de execução do quadro.
// 0 cor, 1 sombra (alvo só com Resolve de profundidade), 2 pré-passe de profundidade (máscara de cor 0 +
// escrita de profundidade num alvo de cor), 3 pós (UP / passe direto).
std::vector<uint8_t> g_drawClass;
std::vector<size_t> g_order;

void PlanDirectPasses(const Frame& f) {
  g_redirects.assign(f.cmds.size(), Redirect{});
  g_resolveDirect.assign(f.cmds.size(), 0);
  g_resolveKeep.assign(f.cmds.size(), 0);
  for (size_t i = 0; i < f.cmds.size(); ++i) {
    const Cmd& c = f.cmds[i];
    if (c.kind != Kind::Resolve || (c.flags & 4) || (c.flags & 0x300)) continue;
    uint64_t srcKey = TargetKeyOf(f.blob.data() + c.state);
    for (size_t j = i + 1; j < f.cmds.size(); ++j) {
      const Cmd& n = f.cmds[j];
      if (n.kind == Kind::Skip) continue;
      if (TargetKeyOf(f.blob.data() + n.state) != srcKey) continue;
      if (n.kind == Kind::Resolve && ((n.flags & 4) || !(n.flags & 0x300))) continue;  // profundidade ou 2º destino da mesma cena
      static bool noKeep = std::getenv("NIER_NO_KEEP") != nullptr;  // A/B
      if (n.kind == Kind::Draw && !noKeep) g_resolveKeep[i] = 1;  // continua desenhando por cima: manter conteúdo
      break;
    }
  }
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
    if (c.flags & 4) continue;  // Resolve de profundidade: não fecha o passe nem redireciona
    if (c.fetch != SIZE_MAX && !draws.empty()) {
      auto t = xenos::DecodeFetch(f.blob.data() + c.fetch);
      int x0, y0, w, h; ResolveRect(c, t, x0, y0, w, h);
      uint32_t guestPitch = std::max<uint32_t>(BE32(Field(state, kSurfaceInfo)) & 0x3FFF, 1);
      bool smaller = t.width < guestPitch || t.height < 720;
      bool ok = x0 == 0 && y0 == 0 && w == int(t.width) && h == int(t.height) && smaller && t.width >= 1 && t.height >= 1;
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
        // This chain also drives luminance adaptation: every level must be current.
        const bool skipThisFrame = false;
        for (size_t k = 0; k < draws.size(); ++k) g_redirects[draws[k]] = Redirect{key, tw, th, k == 0, skipThisFrame};
        g_resolveDirect[i] = 1;
        ++g_directPasses;
        if (skipThisFrame) ++g_bloomSkips;
      }
    }
    draws.clear();
  }
  // ---- classes de desenho e ordem de execução
  g_drawClass.assign(f.cmds.size(), 0);
  std::map<uint64_t, uint8_t> keyKind;  // bit 1 = Resolve de cor, bit 2 = Resolve de profundidade
  for (const Cmd& c : f.cmds)
    if (c.kind == Kind::Resolve && c.fetch != SIZE_MAX) keyKind[TargetKeyOf(f.blob.data() + c.state)] |= (c.flags & 4) ? 2 : 1;
  for (size_t i = 0; i < f.cmds.size(); ++i) {
    const Cmd& c = f.cmds[i];
    if (c.kind != Kind::Draw) continue;
    const uint8_t* st = f.blob.data() + c.state;
    uint64_t key = TargetKeyOf(st);
    uint32_t mask = BE32(Field(st, 0x28DC)) & 15, depth = BE32(Field(st, kDepthControl));
    uint8_t cls = 0;
    bool samplesShadowMap = false;  // PS amostra o mapa de sombra (fmt 22, 1024x1024): passe de recepção de sombra
    if (c.ps) for (int t = 0; t < 32 && !samplesShadowMap; ++t)
      if (c.ps->textureMask & (1u << t)) { auto tf = xenos::DecodeFetch(Field(st, kFetchConstants + 24 * t)); samplesShadowMap = tf.format == xenos::k248 && tf.width == tf.height && tf.width >= 1024; }
    if (!c.up && samplesShadowMap) cls = 4;
    else if (c.up || g_redirects[i].key) cls = 3;
    else if (keyKind.count(key) && keyKind[key] == 2) cls = 1;
    else if (mask == 0 && (depth & 4)) cls = 2;
    g_drawClass[i] = cls;
  }
  g_order.clear(); g_order.reserve(f.cmds.size());
  for (size_t i = 0; i < f.cmds.size(); ++i) g_order.push_back(i);
  if (g_sortByProgram) {
    // corridas de desenhos opacos consecutivos (sem mistura, teste e escrita de profundidade, sem alpha test, mesmo
    // alvo, classe cor) ordenadas por programa: a ordem entre opacos só muda empates de profundidade
    auto opaque = [&](size_t i) {
      const Cmd& c = f.cmds[i];
      if (c.kind != Kind::Draw || g_drawClass[i] != 0) return false;
      const uint8_t* st = f.blob.data() + c.state;
      uint32_t depth = BE32(Field(st, kDepthControl));
      return BE32(Field(st, kBlendControl)) == 0x00010001 && (depth & 6) == 6 && !(BE32(Field(st, kColorControl)) & 8);
    };
    for (size_t i = 0; i < g_order.size();) {
      if (!opaque(i)) { ++i; continue; }
      uint64_t key = TargetKeyOf(f.blob.data() + f.cmds[i].state);
      size_t j = i + 1;
      while (j < g_order.size() && opaque(j) && TargetKeyOf(f.blob.data() + f.cmds[j].state) == key) ++j;
      if (j - i > 2) std::stable_sort(g_order.begin() + i, g_order.begin() + j, [&](size_t a, size_t b) {
        const Cmd& ca = f.cmds[a]; const Cmd& cb = f.cmds[b];
        if (ca.vs != cb.vs) return ca.vs < cb.vs;
        return ca.ps < cb.ps;
      });
      i = j;
    }
  }
}
void ReplayResolve(const Frame& f, const Cmd& c, bool direct, bool keep = false) {
  const uint8_t* state = f.blob.data() + c.state;
  uint64_t srcKey = TargetKeyOf(state);
  bool clears = c.flags & 0x300;
  float s = Scale();
  if ((c.flags & 4) && c.fetch != SIZE_MAX) {
    // Resolve de profundidade: a textura fmt 22 passa a ser a textura de profundidade do FBO que tem a cena
    // (o próprio alvo, ou o destino do alias se a cor já foi resolvida antes).
    auto t = xenos::DecodeFetch(f.blob.data() + c.fetch);
    if (g_depthTexture) {
      auto srcIt = g_targets.find(srcKey);
      if (srcIt != g_targets.end()) {
        Target& src = srcIt->second;
        GLuint depth = src.holder ? g_resolved[src.holder].depth : src.depth;
        if (depth) { g_depthAlias[TexKey(t)] = depth; g_depthKeep.insert(depth); }
      }
    }
  } else if (c.fetch != SIZE_MAX) {
    auto t = xenos::DecodeFetch(f.blob.data() + c.fetch);
    static bool noCopy = std::getenv("NIER_NO_RESOLVE_COPY") != nullptr;  // diagnóstico
    uint64_t key = TexKey(t);
    if (direct) {
      // o passe já foi desenhado no FBO do destino (PlanDirectPasses); o alvo não recebe o conteúdo
      Target& src = g_targets[srcKey];
      src.holder = key;
      if (auto d = g_resolved.find(key); d != g_resolved.end()) DiscardDepth(d->second.fbo, d->second.depth);
    } else if (t.width >= 1 && t.height >= 1 && !noCopy) {
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
      if (full && !src.holder && !keep) {
        // Tela cheia: o destino fica com a própria textura do alvo (sem cópia); o alvo segue com a textura antiga
        // do destino (ping-pong) e o próximo desenho nele limpa/sobrescreve como antes.
        FlushCopiesFrom(srcKey);
        Target& dst = g_resolved[key];
        // destino com FBO completo (cor+profundidade) para a troca ser de FBO inteiro: zero chamadas GL
        if (dst.w != dw || dst.h != dh || !dst.texture || !dst.depth) CreateTarget(dst, dw, dh, true);
        dst.sharedFrom = 0;
        std::swap(src.texture, dst.texture);
        std::swap(src.fbo, dst.fbo);
        std::swap(src.depth, dst.depth);
        dst.from3D = src.drew3DFrame == g_frames.load();
        src.holder = key;
        static bool noClear = std::getenv("NIER_NO_CLEAR_ALIAS") != nullptr;  // A/B: comportamento antigo (conteúdo antigo do ping-pong)
        if (!noClear) src.clearedFrame = ~0ull;  // o próximo desenho no alvo limpa: entrar sem limpar recarrega os tiles
        DiscardDepth(dst.fbo, dst.depth);
        ++g_aliases;
      } else if (src.holder && src.holder != key) {
        // conteúdo está numa textura resolvida (alias/passe direto anterior)
        Target& holder = g_resolved[src.holder];
        Target& dst = g_resolved[key];
        if (full && holder.w == dw && holder.h == dh && holder.texture) {
          // mesma tela inteira para um segundo destino: aponta para o outro destino neste quadro (sem cópia)
          dst.sharedFrom = src.holder; dst.sharedFrame = g_frames.load(); dst.from3D = holder.from3D;
          ++g_aliases;
        } else {
          dst.sharedFrom = 0;
          if (dst.w != dw || dst.h != dh) CreateTarget(dst, dw, dh, false);
          if (g_frameSampled.count(key) || g_everSampled.count(key)) DoCopy(PendingCopy{src.holder, key, int(x0 * s), int(y0 * s), dw, dh, true});
          else ++g_copiesSkipped;
        }
      } else if (!src.holder) {
        Target& dst = g_resolved[key];
        dst.sharedFrom = 0;
        if (full && src.lastCopy && src.lastCopy != key && g_resolved.count(src.lastCopy) && g_resolved[src.lastCopy].texture) {
          // mesma cena já copiada para outro destino neste passe: referencia, sem 2ª cópia
          dst.sharedFrom = src.lastCopy; dst.sharedFrame = g_frames.load(); dst.from3D = g_resolved[src.lastCopy].from3D;
          if (dst.w != dw || dst.h != dh) CreateTarget(dst, dw, dh, false);
          ++g_aliases;
        } else {
          if (dst.w != dw || dst.h != dh) CreateTarget(dst, dw, dh, false);
          // Retângulo parcial ou passe que continua: copia só se o destino for amostrado (neste quadro ou alguma vez).
          if (g_frameSampled.count(key) || g_everSampled.count(key)) {
            g_pendingCopies.push_back({srcKey, key, int(x0 * s), int(y0 * s), dw, dh, false});
            FlushCopiesFrom(srcKey);  // cópia já (semântica do Resolve), só para destinos usados
            if (full) src.lastCopy = key;
          } else {
            ++g_copiesSkipped;      // ninguém amostra este destino: a cópia é pulada
          }
        }
      }
    }
  }
  // Resolve com limpeza (D3DRESOLVE_CLEARRENDERTARGET 0x100 / CLEARDEPTHSTENCIL 0x200, a confirmar).
  if (clears) {
    Target& src = CurrentTarget(state, false);
    BindFbo(src.fbo);
    g_gl.fixedValid = false;
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
      else if (c.kind == Kind::Resolve) ReplayResolve(work, c, planned && g_resolveDirect[i] != 0, planned && g_resolveKeep[i] != 0);
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
  g_vsUniformVectors = vsUniforms > 0 ? vsUniforms : 256;
  {
    const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));  // LEVE_OK: inicialização
    bool mali = renderer && std::strstr(renderer, "Mali");
    g_mali = mali;
    // C1-G3: na Mali (tiler) o pré-passe de profundidade só dobra o custo de vértice; mapa de sombra desligado
    // (efeito pode reduzir, regra do dono); desenhos opacos ordenados por programa. NIER_PREPASS=1 / NIER_SHADOW=1 /
    // NIER_NO_SORT=1 invertem; no PC NIER_MALI_LIKE=1 aplica os mesmos padrões para prova.
    bool maliLike = mali || std::getenv("NIER_MALI_LIKE");
    g_skipPrepass = maliLike && !std::getenv("NIER_PREPASS");
    g_skipShadow = maliLike && !std::getenv("NIER_SHADOW");
    g_sortByProgram = maliLike && !std::getenv("NIER_NO_SORT");
    g_frameskipAuto = false;  // H3 no .164: auto derrubou os desenhados/s (CPU disputada); padrão 1, NIER_FRAMESKIP=auto|2|3|4 liga
    // Ordem do dono (07/11): na Mali a resolução interna padrão é 640x360 (nunca abaixo de 640 de largura);
    // NIER_RENDER_WIDTH continua valendo quando dado. glDiscardFramebufferEXT na Mali só com NIER_DISCARD=1
    // (F13: quadrados pretos e retângulo de quadro antigo = conteúdo indefinido pelo driver).
    if (mali && !std::getenv("NIER_RENDER_WIDTH")) { g_config.renderWidth = 640; g_config.renderHeight = 360; }
    g_config.renderWidth = std::max(g_config.renderWidth, 640);
    g_config.renderHeight = std::max(g_config.renderHeight, g_config.renderWidth * 9 / 16);
    if (mali && !std::getenv("NIER_DISCARD")) g_discard = nullptr;
  }
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
      work.skinJobs.swap(g_inflight.skinJobs); g_inflight.skinJobs.clear();
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
    for (size_t oi = 0; oi < g_order.size(); ++oi) {
      size_t i = g_order[oi];
      const Cmd& c = work.cmds[i];
      if (c.kind == Kind::Resolve) { auto tr0 = Clock::now(); ReplayResolve(work, c, g_resolveDirect[i] != 0, g_resolveKeep[i] != 0); ++g_resolves; g_pResolve += Ms(tr0, Clock::now()); }
      else if (c.kind == Kind::Skip) Skip(c.skipReason);
      else {
        uint8_t cls = g_drawClass[i];
        if (cls == 2 && g_skipPrepass) { ++g_skippedPrepass; continue; }
        if (cls == 1 && g_skipShadow) { ++g_skippedShadow; continue; }
        if (cls == 4 && g_skipShadow) {
          // recepção de sombra sem mapa de sombra: o destino (máscara) fica branco = tudo iluminado
          ++g_skippedReceive;
          const Redirect& rd = g_redirects[i];
          if (rd.key) { Target& t = DirectTarget(rd.key, rd.w, rd.h); if (t.whiteFrame != g_frames.load()) { t.whiteFrame = g_frames.load(); glColorMask(1, 1, 1, 1); glClearColor(1, 1, 1, 1); glClear(GL_COLOR_BUFFER_BIT); g_gl.fixedValid = false; } }
          continue;
        }
        if (cls == 0 && !c.up && CullDraw(work, c)) { ++g_culledDraws; g_culledIndices += c.indexCount; continue; }
        g_idxByClass[cls] += c.indexCount; ++g_drawsByClass[cls];
        g_drawClassCurrent = cls;
        ++g_draws; ++frameDraws; ReplayDraw(work, c, g_redirects[i]);
      }
    }
    g_copiesSkipped += uint32_t(g_pendingCopies.size());
    g_pendingCopies.clear();
    AuditBufferImage(work);
    if (GlProfile()) { auto tf = Clock::now(); glFinish(); g_pFinish += Ms(tf, Clock::now()); }  // LEVE_OK: perfil opt-in (NIER_GL_PROFILE) para medir a GPU
    auto tp = Clock::now();
    g_replayMs += std::chrono::duration<double, std::milli>(tp - tr).count();
    for (auto& [k, t] : g_targets) if (t.clearedFrame == g_frames.load()) DiscardDepth(t.fbo, t.depth);
    PresentToWindow();
    InvalidateGl();
    g_depthKeep.clear();  // apresentação (e auditoria opt-in) mexem no estado fora do cache
    auto now = Clock::now();
    g_swapMs += std::chrono::duration<double, std::milli>(now - tp).count();
    g_lastFrameMs = std::chrono::duration<double, std::milli>(now - last).count();
    last = now;
    uint64_t n = ++g_frames;
    // Janela de log: 120 quadros ou 5 s, o que vier primeiro (a 1 fps no 3D, 120 quadros misturavam tudo);
    // médias divididas pelo número real de quadros da janela.
    static Clock::time_point windowStart = Clock::now(); static uint64_t windowFirst = 0;
    const uint64_t W = n - windowFirst;
    if (W >= 120 || (now - windowStart >= std::chrono::seconds(5) && W > 0))
    {
      std::fprintf(stderr, "[nier-gl] %llu quadros (janela %llu), %.2f ms, %u draws no quadro, %llu pulados (total), intervalo do jogo %u, programas %zu, texturas %zu, texturas %.2f ms/quadro (hash %.2f decod %.2f envio %.2f), replay %.2f swap %.2f espera do jogo %.2f ms/quadro, GL 0x%x\n",
                   (unsigned long long)n, (unsigned long long)W, g_lastFrameMs.load(), frameDraws, (unsigned long long)g_skipped.load(), work.presentInterval,
                   g_programs.size(), g_textures.size(), g_textureMs / double(W), g_sigMs / double(W), g_decodeMs / double(W), g_uploadMs / double(W), g_replayMs / double(W), g_swapMs / double(W), g_gameWaitNs.exchange(0) / (W * 1e6), GlProfile() ? glGetError() : 0u);  // LEVE_OK: glGetError só no perfil opt-in
      if (!g_skipReasons.empty()) {
        std::string line;
        for (auto& [r, n] : g_skipReasons) line += " | " + r + ": " + std::to_string(n);
        std::fprintf(stderr, "[nier-gl] motivos acumulados%s\n", line.c_str());
      }
      if (GlProfile())
        std::fprintf(stderr, "[nier-perfil] índices/quadro por passe: cor %.0f (%.1f desenhos), sombra %.0f (%.1f), pré-passe prof. %.0f (%.1f), pós %.0f (%.1f), recepção de sombra %.0f (%.1f); pulados na janela: pré-passe %llu, sombra %llu, recepção %llu; descartados por AABB %llu desenhos / %llu índices (avaliados %llu, sem padrão MVP %llu, sem dados %llu)\n",
                     g_idxByClass[0] / double(W), g_drawsByClass[0] / double(W), g_idxByClass[1] / double(W), g_drawsByClass[1] / double(W), g_idxByClass[2] / double(W), g_drawsByClass[2] / double(W), g_idxByClass[3] / double(W), g_drawsByClass[3] / double(W), g_idxByClass[4] / double(W), g_drawsByClass[4] / double(W), (unsigned long long)g_skippedPrepass, (unsigned long long)g_skippedShadow, (unsigned long long)g_skippedReceive, (unsigned long long)g_culledDraws, (unsigned long long)g_culledIndices, (unsigned long long)g_cullEval, (unsigned long long)g_cullNoPattern, (unsigned long long)g_cullNoData);
      if (GlProfile() && !g_topDraws.empty()) {
        std::vector<TopDraw> top = g_topDraws;
        std::partial_sort(top.begin(), top.begin() + std::min<size_t>(10, top.size()), top.end(), [](const TopDraw& a, const TopDraw& b) { return a.indices > b.indices; });
        static const char* cls[] = {"cor", "sombra", "pre-passe", "pos", "recepcao"};
        std::string line;
        for (size_t i = 0; i < top.size() && i < 10; ++i) { char b[112]; std::snprintf(b, sizeof b, " | %u idx %u vtx prim %u %s vs %06X (%d ops) ps %06X", top[i].indices, top[i].vertices, top[i].prim, cls[top[i].cls], top[i].vs, top[i].vsOps, top[i].ps); line += b; }
        std::fprintf(stderr, "[nier-perfil] top 10 desenhos da janela (%zu desenhos):%s\n", g_topDraws.size(), line.c_str());
        std::partial_sort(top.begin(), top.begin() + std::min<size_t>(10, top.size()), top.end(), [](const TopDraw& a, const TopDraw& b) { return uint64_t(a.vertices) * a.vsOps > uint64_t(b.vertices) * b.vsOps; });
        line.clear();
        for (size_t i = 0; i < top.size() && i < 10; ++i) { char b[112]; std::snprintf(b, sizeof b, " | %u vtx x %d ops vs %06X %s", top[i].vertices, top[i].vsOps, top[i].vs, cls[top[i].cls]); line += b; }
        std::fprintf(stderr, "[nier-perfil] top 10 VS por vértices x operações:%s\n", line.c_str());
        g_topDraws.clear();
      }
      {
        static Clock::time_point fsLast = Clock::now();
        double secs = std::chrono::duration<double>(Clock::now() - fsLast).count(); fsLast = Clock::now();
        uint64_t up = g_fsUpdates.exchange(0), dr = g_fsDropped.exchange(0);
        uint64_t rec = g_recordNs.exchange(0), th = g_triHits.exchange(0), tm = g_triMisses.exchange(0);
        { uint64_t ph[7]; for (int k = 0; k < 7; ++k) ph[k] = g_recPhaseNs[k].exchange(0);
          double u = up ? up * 1e6 : 1.0;
          std::fprintf(stderr, "[nier-captura] ms/update: shaders %.2f estado %.2f índices %.2f vértices(SnapshotInputs) %.2f IB+trace %.2f skin(início) %.2f outros %.2f\n",
                       ph[0] / u, ph[1] / u, ph[2] / u, ph[3] / u, ph[4] / u, ph[5] / u, ph[6] / u); }
        std::fprintf(stderr, "[nier-frameskip] updates/s %.1f desenhados/s %.1f pulados %llu maior rajada %llu; captura RecordDraw %.2f ms/update, triangulação em cache %llu acertos / %llu erros\n",
                     secs > 0 ? up / secs : 0.0, secs > 0 ? W / secs : 0.0, (unsigned long long)dr, (unsigned long long)g_fsMaxBurst.exchange(0),
                     up ? rec / (up * 1e6) : 0.0, (unsigned long long)th, (unsigned long long)tm);
      }
      if (GlProfile())
        std::fprintf(stderr, "[nier-perfil] ms/quadro: alvo+limpeza %.2f estado+programa+texturas %.2f buffers %.2f desenho %.2f resolve %.2f (%.1f/quadro, cópias pedidas %.1f feitas %.1f evitadas %.1f, alias %.1f, passes diretos %.1f com %.1f desenhos; binds de FBO %.1f, discards %.1f, alvos criados %u; por quadro: vec4 VC/PC %.0f, vec4 ossos %.0f, skinning CPU %.0f vtx em %.1f desenhos %.2f ms (no worker; espera da thread GL %.2f ms; conversões %.2f ms, %.1f misses, %.0f vtx), VS com índice dinâmico de uniforme %.1f desenhos / %.0f vtx / %.0f idx, índices %.0f, trocas de programa %.1f, programas ligados %llu) glFinish(GPU) %.2f\n",
                     g_pTarget / double(W), g_pState / double(W), g_pBuffers / double(W), g_pDraw / double(W), g_pResolve / double(W), g_resolves / double(W), g_resolveCopies / double(W), g_copiesDone / double(W), g_copiesSkipped / double(W), g_aliases / double(W), g_directPasses / double(W), g_directDraws / double(W), g_fboBinds / double(W), g_discards / double(W), g_targetCreates, g_vec4Sent / double(W), g_vtVec4Sent / double(W), g_skinnedVertices / double(W), g_skinnedDraws / double(W), g_skinMs / double(W), g_skinWaitMs / double(W), g_convNs.exchange(0) / (W * 1e6), g_convMisses.exchange(0) / double(W), g_convVertices.exchange(0) / double(W), g_relDraws / double(W), g_relVertices / double(W), g_relIndices / double(W), g_indicesDrawn / double(W), g_programSwitches / double(W), (unsigned long long)g_programsLinked, g_pFinish / double(W));
      g_copiesDone = g_copiesSkipped = 0; g_aliases = g_directPasses = g_directDraws = 0; g_fboBinds = g_discards = 0; g_targetCreates = 0; for (auto& v : g_idxByClass) v = 0; for (auto& v : g_drawsByClass) v = 0; g_skippedPrepass = g_skippedShadow = g_skippedReceive = 0; g_culledDraws = g_culledIndices = g_cullEval = g_cullNoPattern = g_cullNoData = 0; g_vec4Sent = g_vtVec4Sent = g_indicesDrawn = g_programsLinked = g_programSwitches = 0; g_skinnedVertices = g_skinnedDraws = 0; g_skinMs = 0; g_skinWaitMs = 0; g_relDraws = g_relVertices = g_relIndices = 0;
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
      windowStart = now; windowFirst = n;
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
  for (int t = 0; t < 32; ++t) if ((c.ps->textureMask | c.vs->textureMask) & (1u << t)) {
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

// C1-H3: cache de triangulação na thread de captura. Malhas estáticas mandam o mesmo IB todo quadro: a chave é
// (endereço físico, contagem, 16/32 bits, primitiva) e a validação é memcmp dos bytes brutos (muito mais barato que
// trocar bytes + triangular ~550 mil índices/quadro no A53). NIER_NO_TRI_CACHE=1 desliga (A/B).
struct TriCacheEntry { std::vector<uint8_t> raw; std::vector<uint16_t> tri; uint32_t lo = 0, hi = 0; GLenum mode = GL_TRIANGLES; };
std::unordered_map<uint64_t, TriCacheEntry> g_triCache;
// C1-H4: inicia o skinning do desenho recém-gravado (thread do jogo). st = estado copiado deste comando.
uint64_t Fnv(const uint8_t* p, size_t n, uint64_t h = 1469598103934665603ull) {
  for (size_t i = 0; i + 8 <= n; i += 8) { uint64_t w; std::memcpy(&w, p + i, 8); h = (h ^ w) * 1099511628211ull; }
  return h;
}
std::unordered_map<uint64_t, int> g_skinDedupe; uint64_t g_skinDedupeFrame = ~0ull;
void StartSkinJob(Cmd& c, const uint8_t* st) {
  c.skinJob = -1;
  if (!CpuSkin() || c.up || !c.vs || !c.ps) return;
  static bool noVtf = std::getenv("NIER_NO_VTF") != nullptr;
  static int threads = [] {
    int n = std::getenv("NIER_SKIN_THREADS") ? std::atoi(std::getenv("NIER_SKIN_THREADS")) : 1;
    n = std::clamp(n, 1, 4);
    for (int i = 0; i < n; ++i) std::thread(SkinWorker).detach();
    return n;
  }();
  (void)threads;
  auto job = std::make_shared<SkinJob>();
  if (c.vs->textureMask && !noVtf && __builtin_popcount(c.vs->textureMask) == 1) {
    int a, b; if (!SkinInputs(c, a, b, nullptr, nullptr)) return;
    auto t = xenos::DecodeFetch(Field(st, kFetchConstants + 24 * __builtin_ctz(c.vs->textureMask)));
    if (t.format != xenos::k32323232Float || t.height != 1 || t.tiled) return;
    const uint8_t* pal = Physical(t.base, t.width * 16);
    if (!pal) return;
    job->kind = 1; job->pal.assign(pal, pal + size_t(t.width) * 16); job->palWidth = t.width; job->palEndian = t.endian;
  } else if (c.vs->relativeConstants) {
    const RelSkinInfo& ri = RelSkinFor(c.vs);
    int a, b; std::vector<int> pi, di;
    if (!ri.ok || !RelSkinInputs(c, ri, a, b, pi, di)) return;
    const uint8_t* bank = Field(st, kVertexConstants);
    job->kind = 2; job->pal.assign(bank, bank + 256 * 16); job->relBase = ri.base; job->relBones = ri.bones;
  } else return;
  // mesma malha + mesma paleta no mesmo quadro: um job só
  if (g_skinDedupeFrame != g_presented.load()) { g_skinDedupe.clear(); g_skinDedupeFrame = g_presented.load(); }
  uint64_t key = Fnv(job->pal.data(), job->pal.size(), uint64_t(job->kind) * 0x9E3779B97F4A7C15ull ^ c.vs->key);
  for (uint32_t i = 0; i < c.inputCount; ++i) key = (key ^ (c.vertexData[i] ? c.vertexData[i]->serial : 0)) * 1099511628211ull;
  key = (key ^ c.vertexCount) * 1099511628211ull;
  auto hit = g_skinDedupe.find(key);
  if (hit != g_skinDedupe.end() && size_t(hit->second) < g_building.skinJobs.size()) { c.skinJob = hit->second; return; }
  job->cmd = c;
  c.skinJob = int(g_building.skinJobs.size());
  g_building.skinJobs.push_back(job);
  g_skinDedupe[key] = c.skinJob;
  { std::lock_guard<std::mutex> lk(g_skinMx); g_skinQueue.push_back(job); }
  g_skinCv.notify_one();
}
struct RecordTimer { Clock::time_point t0 = Clock::now(); ~RecordTimer() { g_recordNs += uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count()); } };
void RecordDraw(uint32_t device, uint32_t primitive, uint32_t baseVertex, uint32_t startIndex, uint32_t count, bool indexed) {
  RecordTimer recordTimer;
  auto ph = Clock::now();
  auto mark = [&](int k) { auto t = Clock::now(); g_recPhaseNs[k] += uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(t - ph).count()); ph = t; };
  static uint64_t calls = 0;
  if (++calls % 5000 == 0)
    std::fprintf(stderr, "[nier-jogo] %llu desenhos gravados; quadro atual %zu comandos %zu MB; apresentados %llu\n", (unsigned long long)calls,
                 g_building.cmds.size(), g_building.blob.size() >> 20, (unsigned long long)g_presented.load());
  FlushPendingUP();
  GuardBuilding();
  const uint8_t* state = Virtual(device + kStateBegin, kStateSize);
  if (!state || !count) return;
  Cmd c{Kind::Draw};
  mark(6);
  if (!PrepareShaders(c, device)) return;
  mark(0);
  c.state = Append(g_building, state, kStateSize);
  mark(1);
  const uint8_t* copy = g_building.blob.data() + c.state;
  g_srcIndices.clear();
  int64_t first = 0;
  uint32_t vertices = 0;
  const std::vector<uint16_t>* triCached = nullptr;
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
    static bool noTriCache = std::getenv("NIER_NO_TRI_CACHE") != nullptr;
    uint64_t triKey = (c.indexAddress * 0x9E3779B97F4A7C15ull) ^ (uint64_t(count) << 8) ^ (uint64_t(primitive) << 1) ^ uint64_t(is32);
    if (!noTriCache) {
      auto hit = g_triCache.find(triKey);
      if (hit != g_triCache.end() && hit->second.raw.size() == size_t(count) * bytes && std::memcmp(hit->second.raw.data(), src, hit->second.raw.size()) == 0) {
        ++g_triHits;
        triCached = &hit->second.tri; g_triMode = hit->second.mode;
        first = int64_t(int32_t(baseVertex)) + hit->second.lo; vertices = hit->second.hi - hit->second.lo + 1;
      }
    }
  }
  if (triCached) {
    // índices já triangulados (acerto do cache)
  } else if (indexed) {
    uint32_t ib = BE32(Field(copy, kIndexBuffer));
    const uint8_t* object = Virtual(ib, 0x20);
    bool is32 = BE32(object) & 0x80000000u;
    uint32_t address = BE32(object + 0x18), bytes = is32 ? 4 : 2;
    uint32_t physical = (address & 0x1FFFFFFF) + (address >= 0xE0000000 ? 0x1000 : 0);
    const uint8_t* src = Physical(physical + startIndex * bytes, count * bytes);
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
    if (!Triangulate(primitive, g_srcIndices, g_triIndices)) { RecordSkip("tipo de primitiva não suportado"); return; }
    static bool noTriCache = std::getenv("NIER_NO_TRI_CACHE") != nullptr;
    if (!noTriCache) {
      if (g_triCache.size() > 8192) g_triCache.clear();
      uint64_t triKey = (c.indexAddress * 0x9E3779B97F4A7C15ull) ^ (uint64_t(count) << 8) ^ (uint64_t(primitive) << 1) ^ uint64_t(is32);
      auto& e = g_triCache[triKey];
      e.raw.assign(src, src + size_t(count) * bytes); e.tri = g_triIndices; e.lo = lo; e.hi = hi; e.mode = g_triMode;
      ++g_triMisses;
    }
    triCached = &g_triIndices;
  } else {
    first = int64_t(int32_t(baseVertex)); vertices = count;
    for (uint32_t i = 0; i < count; ++i) g_srcIndices.push_back(i);
    if (!Triangulate(primitive, g_srcIndices, g_triIndices)) { RecordSkip("tipo de primitiva não suportado"); return; }
    triCached = &g_triIndices;
  }
  const std::vector<uint16_t>& tri = *triCached;
  if (tri.empty()) return;
  static int trace3d = std::getenv("NIER_RENDER_TRACE_3D") ? std::atoi(std::getenv("NIER_RENDER_TRACE_3D")) : 0;
  if (trace3d > 0 && indexed) {
    --trace3d;
    std::fprintf(stderr, "[nier-3d] prim %u n %u idx %d base %u start %u -> first %lld vertices %u inputs %zu vs %016llX ok %d ps ok %d\n",
                 primitive, count, int(indexed), baseVertex, startIndex, (long long)first, vertices, c.vs->inputs.size(),
                 (unsigned long long)c.vs->key, int(c.vs->ok), int(c.ps->ok));
    for (const auto& in : c.vs->inputs) std::fprintf(stderr, "[nier-3d]   slot %u fmt %u off %u stride %u\n", in.slot, in.format, in.offset, in.stride);
  }
  mark(2);
  if (!SnapshotInputs(c, copy, first, vertices, nullptr, 0)) return;
  mark(3);
  TraceDraw(c, g_building.blob.data() + c.state, primitive, count, indexed);
  c.indexCount = uint32_t(tri.size());
  c.prim = uint8_t(primitive);
  c.mode = g_triMode;
  if (ReferenceBuffers() || c.up) c.indices = Append(g_building, tri.data(), tri.size() * 2);
  else {
    c.indexData = g_bufferSnapshots.Indices(c.indexAddress, tri.data(), c.indexCount, g_bufferRecordingFrame);
    g_building.bufferBytes += c.indexData->raw.size() + c.indexData->bytes.size();
  }
  mark(4);
  StartSkinJob(c, g_building.blob.data() + c.state);
  mark(5);
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
  c.prim = uint8_t(primitive);
  c.mode = g_triMode;
  if (ReferenceBuffers() || c.up) c.indices = Append(g_building, g_triIndices.data(), g_triIndices.size() * 2);
  else {
    c.indexData = g_bufferSnapshots.Indices(c.indexAddress, g_triIndices.data(), c.indexCount, g_bufferRecordingFrame);
    g_building.bufferBytes += c.indexData->raw.size() + c.indexData->bytes.size();
  }
  g_building.cmds.push_back(c);
}

}  // namespace

void RecordResolve(uint32_t device, uint32_t flags, const int32_t* rect, uint32_t destTexture, uint32_t clearColorArgb, uint32_t level) {
  FlushPendingUP();
  const uint8_t* state = Virtual(device + kStateBegin, kStateSize);
  if (!state) return;
  if ((flags & 4) && !destTexture && !(flags & 0x300)) return;  // D3DRESOLVE_DEPTHSTENCIL = 4 sem destino nem limpeza
  Cmd c{Kind::Resolve};
  c.flags = flags; c.argb = clearColorArgb;
  if (rect) { std::memcpy(c.rect, rect, sizeof c.rect); c.hasRect = true; }
  c.state = Append(g_building, state, kStateSize);
  if (destTexture) {
    // Textura D3D do 9328: fetch constant em +0x1C (o 20871 usa +0x18). Conferido no rastro de 07/11:
    // objeto+0x1C = 8A000002 (tipo textura), +0x24 = 0059E4FF (1280x720).
    if (const uint8_t* fetch = Virtual(destTexture + 0x1C, 24)) c.fetch = Append(g_building, fetch, 24);
    // Nível de mip > 0: o destino é o subrecurso daquele nível. A cópia do fetch passa a ter largura/altura do
    // nível, e a chave (endereço, largura, altura) separa o alvo do nível 0. Antes o nível 1/2 da cadeia de bloom
    // era gravado no canto da textura do nível 0 e aparecia como uma cópia da cena num quarto da tela.
    if (level > 0 && level < 13 && c.fetch != SIZE_MAX) {
      uint8_t* w = g_building.blob.data() + c.fetch + 8;
      uint32_t w2 = BE32(w), width = (w2 & 0x1FFF) + 1, height = ((w2 >> 13) & 0x1FFF) + 1;
      width = std::max(1u, width >> level); height = std::max(1u, height >> level);
      w2 = (w2 & ~0x3FFFFFFu) | (width - 1) | (height - 1) << 13;
      w[0] = uint8_t(w2 >> 24); w[1] = uint8_t(w2 >> 16); w[2] = uint8_t(w2 >> 8); w[3] = uint8_t(w2);
      static uint32_t logged = 0;
      if (logged++ < 4) std::fprintf(stderr, "[nier-gl] Resolve no nível de mip %u: destino %ux%u próprio\n", level, width, height);
    }
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
  ++g_fsUpdates;
  static int fsMode = [] {  // 0 = auto
    const char* e = std::getenv("NIER_FRAMESKIP");
    if (!e) return -1;  // decide pelo GPU (g_frameskipAuto) quando a thread GL já iniciou
    if (!std::strcmp(e, "auto")) return 0;
    return std::clamp(std::atoi(e), 1, 4);
  }();
  int mode = fsMode >= 0 ? fsMode : (g_frameskipAuto.load() ? 0 : 1);
  static uint64_t burst = 0;
  auto noteDrop = [&] { ++g_fsDropped; if (++burst > g_fsMaxBurst.load()) g_fsMaxBurst = burst; };
  if (mode >= 2 && (g_presented.load() % uint64_t(mode)) != 0 && g_traceFrame.load() < 0) {
    g_building.Clear(); noteDrop();  // fixo: 1 desenhado a cada N, pulos espalhados; o jogo não espera
    return;
  }
  auto waitStart = Clock::now();
  std::unique_lock lock(g_queueMutex);
  if (mode == 0 && g_hasInflight && !g_quit && burst < 3 && g_traceFrame.load() < 0) {
    // auto: a thread GL está ocupada; o quadro novo toma o lugar do que ainda não começou (esse é pulado)
    g_inflight.blob.swap(g_building.blob);
    g_inflight.cmds.swap(g_building.cmds);
    g_inflight.skinJobs.swap(g_building.skinJobs);
    g_inflight.presentInterval = g_interval;
    g_building.Clear();
    noteDrop();
    return;
  }
  g_queueCv.wait(lock, [] { return !g_hasInflight || g_quit; });
  burst = 0;
  g_gameWaitNs += uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - waitStart).count());
  g_inflight.blob.swap(g_building.blob);
  g_inflight.cmds.swap(g_building.cmds);
  g_inflight.skinJobs.swap(g_building.skinJobs);
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
