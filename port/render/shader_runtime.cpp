// Ver shader_runtime.h. Código deste projeto (MIT); só Translate() vem da dependência privada.
#include "shader_runtime.h"

#include "nier_d3d.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <memory>
#include <mutex>
#include <unordered_map>

#ifdef NIER_HAVE_XENOS_TRANSLATOR
#include "xenos_shader.h"
#endif

namespace nier::render {
namespace {
using namespace nier::d3d;

std::mutex g_mutex;
std::unordered_map<uint64_t, std::unique_ptr<GuestShader>> g_byKey;
struct FastKey { uint32_t object, decl, ucode; uint64_t strides, tag; bool operator==(const FastKey&) const = default; };
struct FastHash { size_t operator()(const FastKey& k) const { return std::hash<uint64_t>()(uint64_t(k.object) << 32 ^ k.decl ^ k.strides * 31 ^ k.tag * 131 ^ k.ucode); } };
std::unordered_map<FastKey, const GuestShader*, FastHash> g_fast;
std::string g_dumpDir;

uint32_t R32(VirtualReader read, uint32_t address) {
  const uint8_t* p = read(address, 4);
  return p ? BE32(p) : 0;
}

// Liga os vfetch do VS à declaração atual. Cabeçalho do shader: seção+0x1C = nº de vfetch; a partir de
// seção+0x28 uma palavra por vfetch (bits 0-11 instrução, 12-15 uso, 16-19 índice do uso). Declaração:
// +0x18 nº de elementos; elemento k em +0x34+12k = {stream<<16|offset, tipo, _, uso(+9), índice(+10)}.
// Tipo: bits 0-5 formato Xenos, 8 com sinal, 9 inteiro, 10-21 swizzle de destino. (Layout do XDK conferido
// no DMC3; no 9328 o shader segue o mesmo cabeçalho, ver docs/D3D_MAP.md.)
void BindVertexFetches(VirtualReader read, uint32_t decl, uint32_t section, const uint8_t* strides, std::vector<uint32_t>& words) {
  if (!decl) return;
  uint32_t elements = std::min(R32(read, decl + 0x18), 16u);
  uint32_t fetches = std::min(R32(read, section + 0x1C), 16u);
  for (uint32_t i = 0; i < fetches; ++i) {
    uint32_t entry = R32(read, section + 0x28 + 4 * i);
    uint32_t slot = entry & 0xFFF, usage = (entry >> 12) & 0xF, usageIndex = (entry >> 16) & 0xF;
    if (3 * slot + 2 >= words.size() || ((words[3 * slot + 1] >> 16) & 0x3F) != 0) continue;
    uint32_t element = 0;
    for (uint32_t k = 0; k < elements && !element; ++k) {
      const uint8_t* e = read(decl + 0x34 + 12 * k, 12);
      if (e && e[9] == usage && e[10] == usageIndex) element = decl + 0x34 + 12 * k;
    }
    if (!element) continue;
    uint32_t head = R32(read, element), type = R32(read, element + 4);
    uint32_t stream = head >> 16, offset = head & 0xFFFF;
    uint32_t stride = stream < 16 ? strides[stream] : 0;  // em palavras
    uint32_t fetchSlot = 95 - stream;
    uint32_t& w0 = words[3 * slot];
    uint32_t& w1 = words[3 * slot + 1];
    uint32_t& w2 = words[3 * slot + 2];
    uint32_t typeSwizzle = (type >> 10) & 0xFFF, format = type & 0x3F;
    // formatos de 16 bits chegam com as metades trocadas (buffers 8in32): x<->y, z<->w
    if (format == 25 || format == 26 || format == 31 || format == 32) {
      uint32_t swapped = 0;
      for (int c = 0; c < 4; ++c) { uint32_t sel = (typeSwizzle >> (3 * c)) & 7; if (sel < 4) sel ^= 1; swapped |= sel << (3 * c); }
      typeSwizzle = swapped;
    }
    uint32_t swizzle = 0;
    for (int c = 0; c < 4; ++c) {
      uint32_t sel = (w1 >> (3 * c)) & 7;
      if (sel < 4) sel = (typeSwizzle >> (3 * sel)) & 7;
      swizzle |= sel << (3 * c);
    }
    w0 = (w0 & ~(0x1Fu << 20) & ~(3u << 25)) | ((fetchSlot / 3) << 20) | ((fetchSlot % 3) << 25);
    w1 = (w1 & ~0x3FFFu & ~(0x3Fu << 16) & ~(1u << 30)) | swizzle | (((type >> 8) & 1) << 12) | (((type >> 9) & 1) << 13) | (format << 16);
    w2 = (w2 & 0x80000000u) | (stride & 0xFF) | ((offset / 4) << 8);
  }
}

// Reduz VC[256]/PC[256] ao que o shader usa. Sem índice relativo: array denso com as constantes lidas.
// Com índice relativo (a0): mantém 0..N-1 com N = 256 - uniformes extras - folga, literais viram const.
constexpr int kStageBudget = 256;   // GL_MAX_*_UNIFORM_VECTORS medido na Mali-450
void CompactConstants(GuestShader& s) {
  const std::string name = s.pixel ? "PC" : "VC";
  const std::string decl = "uniform vec4 " + name + "[256];";
  size_t at = s.source.find(decl);
  if (at == std::string::npos) return;
  static const std::regex extra(R"(uniform\s+(?:\w+\s+)?(?:float|int|bool|vec[234]|ivec[234]|bvec[234]|mat[234]|sampler2D)\s+\w+\s*;)");
  std::string withoutDecl = s.source;
  withoutDecl.erase(at, decl.size());
  int extras = int(std::distance(std::sregex_iterator(withoutDecl.begin(), withoutDecl.end(), extra), std::sregex_iterator()));
  const std::regex ref(name + R"(\[(\d+)\])");
  std::set<int> used;
  for (std::sregex_iterator it(withoutDecl.begin(), withoutDecl.end(), ref), e; it != e; ++it) used.insert(std::stoi((*it)[1]));
  int literalsFrom = 256 - int(s.literals.size() / 4);
  auto literal = [&](int reg) {
    const float* v = &s.literals[size_t(reg - literalsFrom) * 4];
    char b[160]; std::snprintf(b, sizeof b, "vec4(%.9g, %.9g, %.9g, %.9g)", v[0], v[1], v[2], v[3]);
    std::string out = b;
    for (size_t p = 0; (p = out.find("inf", p)) != std::string::npos; p += 6) out.replace(p, 3, "1e38");
    return out;
  };
  std::string body = withoutDecl, out;
  if (!s.relativeConstants) {
    std::map<int, int> slot;
    for (int r : used) if (r < literalsFrom) { int j = int(s.constMap.size()); slot[r] = j; s.constMap.push_back(uint16_t(r)); }
    std::sregex_iterator it(body.begin(), body.end(), ref), e;
    size_t last = 0;
    for (; it != e; ++it) {
      int r = std::stoi((*it)[1]);
      out.append(body, last, size_t(it->position()) - last);
      out += r >= literalsFrom ? literal(r) : name + "[" + std::to_string(slot[r]) + "]";
      last = size_t(it->position() + it->length());
    }
    out.append(body, last, std::string::npos);
    int count = std::max<int>(1, int(s.constMap.size()));
    if (s.constMap.empty()) s.constMap.push_back(0);
    out.insert(at, "uniform vec4 " + name + "[" + std::to_string(count) + "];");
  } else {
    int n = std::min(literalsFrom, kStageBudget - extras - 4);
    for (int r : used) if (r >= n && r < literalsFrom) { s.ok = false; s.error = "constante " + std::to_string(r) + " acima do orçamento da Mali com índice relativo"; return; }
    std::sregex_iterator it(body.begin(), body.end(), ref), e;
    size_t last = 0;
    for (; it != e; ++it) {
      int r = std::stoi((*it)[1]);
      out.append(body, last, size_t(it->position()) - last);
      out += r >= literalsFrom ? literal(r) : it->str();
      last = size_t(it->position() + it->length());
    }
    out.append(body, last, std::string::npos);
    out = std::regex_replace(out, std::regex(R"(nx_clamp_int\(([^,]+), 0, 255\))"), "nx_clamp_int($1, 0, " + std::to_string(n - 1) + ")");
    for (int r = 0; r < n; ++r) s.constMap.push_back(uint16_t(r));
    out.insert(at, "uniform vec4 " + name + "[" + std::to_string(n) + "];");
  }
  s.source = out;
}

const GuestShader* Load(uint32_t device, VirtualReader read, bool pixel) {
#ifndef NIER_HAVE_XENOS_TRANSLATOR
  (void)device; (void)read; (void)pixel;
  return nullptr;
#else
  const uint8_t* dev = read(device, kStateEnd);
  if (!dev) return nullptr;
  uint32_t object = BE32(dev + (pixel ? kPixelShader : kVertexShader));
  if (!object) return nullptr;
  uint32_t header = object + (pixel ? kPsHeader : kVsHeader);
  uint32_t ucode = R32(read, object + (pixel ? kPsUcode : kVsUcode));
  uint32_t section = header + R32(read, header + 0x18);
  uint32_t offset = R32(read, section), size = R32(read, section + 4);
  if (!ucode || !size || size > 0x10000 || (size & 3)) return nullptr;
  const uint8_t* code = read(ucode, offset + size);
  if (!code) return nullptr;
  uint32_t decl = pixel ? 0 : BE32(dev + kVertexDeclaration);
  uint64_t strides = 0;
  if (!pixel) std::memcpy(&strides, dev + kStreamStride0, 8);
  uint64_t tag = 0;
  std::memcpy(&tag, code + offset, 8);
  FastKey fk{object, decl, ucode, strides, tag};
  std::lock_guard lock(g_mutex);
  if (auto it = g_fast.find(fk); it != g_fast.end()) return it->second;

  std::vector<uint32_t> words(size / 4);
  for (size_t i = 0; i < words.size(); ++i) words[i] = BE32(code + offset + 4 * i);
  if (!pixel) BindVertexFetches(read, decl, section, dev + kStreamStride0, words);
  uint64_t key = pixel ? 0x9E3779B97F4A7C15ull : 1469598103934665603ull;
  for (uint32_t w : words) key = (key ^ w) * 1099511628211ull;
  for (uint32_t i = 0; i < offset / 4; ++i) key = (key ^ BE32(code + 4 * i)) * 1099511628211ull;  // literais
  auto& slot = g_byKey[key];
  if (!slot) {
    auto s = std::make_unique<GuestShader>();
    s->pixel = pixel;
    s->key = key;
    xenos::Shader t = xenos::Translate(words.data(), uint32_t(words.size()), pixel);
    s->ok = t.ok;
    s->error = t.error;
    s->textureMask = t.textureMask;
    std::memcpy(s->booleanMask, t.booleanMask, sizeof s->booleanMask);
    s->inputMask = t.inputMask;
    s->outputMask = t.outputMask;
    s->relativeConstants = t.relativeConstants;
    for (const auto& f : t.fetches)
      s->inputs.push_back({f.slot, f.offset, f.stride, f.format, f.isSigned, f.isInteger, f.expAdjust});
    for (uint32_t i = 0; i < offset / 4; ++i) {
      uint32_t w = BE32(code + 4 * i);
      float v; std::memcpy(&v, &w, 4);
      s->literals.push_back(v);
    }
    s->source = std::string("#version 100\n") +
                (pixel ? "precision mediump float;\nprecision mediump int;\nprecision mediump sampler2D;\n"
                       : "precision highp float;\nprecision highp int;\n") + t.body;
    if (s->ok && !std::getenv("NIER_NO_COMPACT")) CompactConstants(*s);
    if (s->constMap.empty()) for (int r = 0; r < 256; ++r) s->constMap.push_back(uint16_t(r));
    if (!s->ok) std::fprintf(stderr, "[nier-shader] %s %016llX não traduzido: %s\n", pixel ? "PS" : "VS", (unsigned long long)key, s->error.c_str());
    if (!g_dumpDir.empty()) {
      char name[64];
      std::snprintf(name, sizeof name, "/%016llX_%s.glsl", (unsigned long long)key, pixel ? "ps" : "vs");
      std::ofstream(g_dumpDir + name) << s->source;
    }
    slot = std::move(s);
  }
  g_fast[fk] = slot.get();
  return slot.get();
#endif
}
}  // namespace

bool TranslatorAvailable() {
#ifdef NIER_HAVE_XENOS_TRANSLATOR
  return true;
#else
  return false;
#endif
}
const GuestShader* VertexShaderFor(uint32_t device, VirtualReader read) { return Load(device, read, false); }
const GuestShader* PixelShaderFor(uint32_t device, VirtualReader read) { return Load(device, read, true); }
void SetShaderDumpDir(const char* dir) { g_dumpDir = dir ? dir : ""; }

}  // namespace nier::render
