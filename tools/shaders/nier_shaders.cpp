// Extrai contêineres de shader Xenos (0x102A1100 = pixel, 0x102A1101 = vértice) de arquivos do jogo,
// traduz para GLSL ES 1.00 com o tradutor do DMC3 (compilado com NEXTOS_GLES2) e grava, por hash:
//   <out>/<HASH>_{vs,ps}.glsl   fonte ESSL 100 (privado: derivado do jogo)
//   <out>/<HASH>_{vs,ps}.ucode  microcódigo original (programa)
//   <out>/report.tsv            uma linha por shader com as métricas para a Mali-450
// Uso: nier_shaders <out> <arquivo>...   (cada arquivo é varrido inteiro; repetições são deduplicadas)
#include "xenos_shader.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <algorithm>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <vector>

namespace {
uint32_t BE(const uint8_t* p) { return uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

struct Result {
  bool pixel;
  uint32_t words, literals;
  bool ok;
  std::string error, source, origin;
  int uniformVec4, samplers, varyings, attributes, usedConstants;
  bool relative;
};

int CountMatches(const std::string& s, const std::regex& re) {
  return int(std::distance(std::sregex_iterator(s.begin(), s.end(), re), std::sregex_iterator()));
}
}  // namespace

// Tamanho do programa (em palavras) pelo fluxo de controle: os CF ficam no início, 2 por 3 palavras; o programa
// termina na maior posição address+count dos exec. Devolve 0 se não parece um programa Xenos.
uint32_t ProgramWords(const uint8_t* p, size_t avail) {
  // Regras: o 1º exec começa logo após o bloco de CF (address = nº de triplas de CF); cada exec seguinte
  // começa onde o anterior terminou; o END fecha o programa e é a última CF do bloco.
  if (avail < 24) return 0;
  uint32_t cfTriplets = 0, next = 0, execs = 0;
  for (uint32_t pair = 0; pair < 64; ++pair) {
    if (12 * (pair + 1) > avail) return 0;
    uint32_t w0 = BE(p + 12 * pair), w1 = BE(p + 12 * pair + 4), w2 = BE(p + 12 * pair + 8);
    uint64_t cf[2] = {uint64_t(w0) | uint64_t(w1 & 0xFFFF) << 32, uint64_t(w1 >> 16) | uint64_t(w2) << 16};
    for (int k = 0; k < 2; ++k) {
      uint32_t op = uint32_t(cf[k] >> 44) & 0xF;
      bool exec = (op >= 1 && op <= 6) || op == 13 || op == 14;
      if (exec) {
        uint32_t address = cf[k] & 0xFFF, count = (cf[k] >> 12) & 7;
        if (!execs) { if (address == 0 || address > 64) return 0; cfTriplets = address; next = address; }
        if (address != next || (count == 0 && op != 2)) return 0;
        next += count; ++execs;
        if (op == 2 || op == 4 || op == 6 || op == 14) {
          if (pair + 1 != cfTriplets) return 0;
          if (k == 0 && cf[1] != 0) return 0;
          if (next <= cfTriplets || 12ull * next > avail) return 0;
          return 3 * next;
        }
      } else if (op == 0) {
        if (cf[k] != 0) return 0;
      } else if (op == 12 || op == 15 || (op >= 7 && op <= 11)) {
        if (!execs && op != 12) return 0;
      } else return 0;
      if (execs && pair + 1 > cfTriplets) return 0;
    }
    if (!execs && pair > 4) return 0;
  }
  return 0;
}

int main(int argc, char** argv) {
  if (argc < 3) { std::fprintf(stderr, "uso: %s <saída> <arquivo>...\n", argv[0]); return 2; }
  std::string out = argv[1];
  // Arquivos depois de "--fisico" são partes físicas (*.EFP, *.MDP, PE): varridas atrás de programas
  // cujo tamanho bata com algum contêiner visto nas partes virtuais.
  int physicalFrom = argc;
  for (int a = 2; a < argc; ++a) if (std::string(argv[a]) == "--fisico") { physicalFrom = a; break; }
  std::set<uint32_t> sizes[2];
  std::map<uint64_t, Result> all;
  size_t containers = 0, invalid = 0;
  auto add = [&](const uint8_t* code, uint32_t size, uint32_t literals, bool pixel, const std::string& origin) {
    std::vector<uint32_t> words(size / 4);
    uint64_t hash = pixel ? 0x9E3779B97F4A7C15ull : 1469598103934665603ull;
    for (size_t w = 0; w < words.size(); ++w) { words[w] = BE(code + 4 * w); hash = (hash ^ words[w]) * 1099511628211ull; }
    if (all.count(hash)) return;
    Result r{};
    r.pixel = pixel; r.words = uint32_t(words.size()); r.literals = literals; r.origin = origin;
    xenos::Shader s = xenos::Translate(words.data(), uint32_t(words.size()), pixel);
    r.ok = s.ok; r.error = s.error; r.relative = s.relativeConstants;
    for (uint32_t m : s.constantMask) r.usedConstants += __builtin_popcount(m);
    static const char* header = "#version 100\nprecision mediump float;\nprecision mediump int;\nprecision mediump sampler2D;\n";
    r.source = header + s.body;
    static const std::regex arr(R"(uniform\s+(?:\w+\s+)?vec4\s+\w+\s*\[\s*(\d+)\s*\])"), one(R"(uniform\s+(?:\w+\s+)?(?:vec4|mat4|vec3|vec2|float|int|bool|uint)\s+\w+\s*;)"),
        sam(R"(uniform\s+(?:\w+\s+)?sampler\w*\s)"), vary(R"(\bvarying\s+)"), attr(R"(\battribute\s+)");
    for (std::sregex_iterator it(r.source.begin(), r.source.end(), arr), e; it != e; ++it) r.uniformVec4 += std::stoi((*it)[1]);
    r.uniformVec4 += CountMatches(r.source, one);
    r.samplers = CountMatches(r.source, sam);
    r.varyings = CountMatches(r.source, vary);
    r.attributes = CountMatches(r.source, attr);
    char name[64]; std::snprintf(name, sizeof name, "%016llX_%s", (unsigned long long)hash, pixel ? "ps" : "vs");
    std::ofstream(out + "/" + name + ".glsl") << r.source;
    std::ofstream(out + "/" + name + ".ucode", std::ios::binary).write(reinterpret_cast<const char*>(code), size);
    all[hash] = std::move(r);
  };
  size_t found = 0;
  for (int a = physicalFrom + 1; a < argc; ++a) {}
  for (int a = 2; a < physicalFrom; ++a) {
    std::ifstream f(argv[a], std::ios::binary);
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), {});
    for (size_t i = 0; i + 0x20 <= d.size(); i += 4) {
      uint32_t magic = BE(&d[i]);
      if ((magic & 0xFFFFFFFE) != 0x102A1100) continue;
      bool pixel = magic == 0x102A1100;
      uint32_t virtualSize = BE(&d[i + 4]), physicalSize = BE(&d[i + 8]), shaderOffset = BE(&d[i + 0x18]);
      if (!virtualSize || virtualSize > 0x100000 || physicalSize > 0x100000 || shaderOffset + 8 > virtualSize ||
          i + virtualSize + physicalSize > d.size()) { ++invalid; continue; }
      uint32_t physOff = BE(&d[i + shaderOffset]), size = BE(&d[i + shaderOffset + 4]);
      if (!size || size % 4 || physOff + size > physicalSize) { ++invalid; continue; }
      ++containers;
      sizes[pixel].insert(size);
    }
  }
  for (int a = physicalFrom + 1; a < argc; ++a) {
    std::ifstream f(argv[a], std::ios::binary);
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), {});
    for (size_t i = 0; i + 12 <= d.size(); i += 4) {
      uint32_t n = ProgramWords(&d[i], d.size() - i);
      if (!n) continue;
      for (int pixel = 0; pixel < 2; ++pixel) {
        if (!sizes[pixel].count(n * 4)) continue;
        char where[600]; std::snprintf(where, sizeof where, "%s+0x%zX", argv[a], i);
        add(&d[i], n * 4, 0, pixel, where);
        ++found;
      }
    }
  }
  std::ofstream rep(out + "/report.tsv");
  rep << "hash\ttipo\tpalavras\tinstr_estim\tliterais_vec4\ttraduziu\tuniform_vec4_decl\tconstantes_usadas\tindice_relativo\tsamplers\tvaryings\tatributos\torigem\terro\n";
  for (auto& [h, r] : all) {
    char hs[20]; std::snprintf(hs, sizeof hs, "%016llX", (unsigned long long)h);
    rep << hs << '\t' << (r.pixel ? "ps" : "vs") << '\t' << r.words << '\t' << r.words / 3 << '\t' << r.literals << '\t' << r.ok << '\t'
        << r.uniformVec4 << '\t' << r.usedConstants << '\t' << r.relative << '\t' << r.samplers << '\t' << r.varyings << '\t'
        << r.attributes << '\t' << r.origin << '\t' << r.error << '\n';
  }
  std::printf("contêineres %zu (inválidos %zu), tamanhos vs %zu ps %zu, programas achados %zu, shaders únicos %zu\n", containers, invalid, sizes[0].size(), sizes[1].size(), found, all.size());
}
