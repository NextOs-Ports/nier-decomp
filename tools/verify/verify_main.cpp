// nierverify — prova de equivalência das funções reconstruídas do NieR.
//
// Para cada função registrada, executa a original traduzida (`__imp__sub_X`) e a
// reconstruída (`decomp_sub_X`) a partir do MESMO estado inicial aleatório e compara:
//   - registradores que a ABI do Xbox 360 preserva ou devolve (r1, r2, r13-r31,
//     f14-f31, r3, f1);
//   - toda a memória tocada: imagem do jogo (páginas sujas por proteção de escrita),
//     área de dados de teste, área do r13 e a pilha acima do r1 de entrada;
//   - falha de acesso: as duas falham ou nenhuma falha.
// Funções chamadas por ambas executam o mesmo código traduzido, então o oráculo é exato.
#include <rex/ppc.h>
#include <rex/ppc/context.h>
#include <setjmp.h>
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <random>
#include <string>
#include <vector>

extern PPCFuncMapping PPCFuncMappings[];

namespace nierverify {
struct Entry { const char* name; uint32_t addr; PPCFunc* orig; PPCFunc* decomp; const char* spec; const char* file; };
std::vector<Entry>& registry() { static std::vector<Entry> r; return r; }
int add(const char* n, uint32_t a, PPCFunc* o, PPCFunc* d, const char* s, const char* f) {
  registry().push_back({n, a, o, d, s, f}); return 0;
}
}  // namespace nierverify

namespace {
constexpr uint64_t kImageBase = 0x82000000ull, kImageSize = 0x4090000ull;
constexpr uint64_t kCodeBase = 0x82100000ull, kCodeSize = 0x52AA08ull, kThunk = 0x10000ull;
constexpr uint32_t kScratch = 0x40000000u, kScratchSize = 1u << 20;
constexpr uint32_t kTls = 0x50000000u, kTlsSize = 1u << 16;
constexpr uint32_t kStack = 0x70000000u, kStackSize = 1u << 20;
constexpr uint32_t kSp = kStack + kStackSize - 0x8000u;
constexpr size_t kPage = 4096;

uint8_t* g_base;

// Chamadas virtuais/indiretas: endereços falsos na reserva de thunks do ReXGlue
// apontam para um stub do host que registra destino e argumentos e devolve um valor
// determinístico. Assim a função testada pode chamar métodos de objetos de teste.
constexpr uint32_t kFakeFn = 0x82100000u + 0x52AA08u + 0x100u;  // dentro da reserva de thunks
constexpr int kFakeCount = 64;
struct Call { uint32_t target; uint64_t r[8]; uint64_t f[4]; };
std::vector<Call> g_calls;
int g_callargs = 6;
std::vector<uint8_t> g_pristine;           // imagem original
std::vector<uint8_t> g_dirty;              // páginas da imagem tocadas
sigjmp_buf g_jmp; volatile int g_armed; volatile uintptr_t g_fault_addr;

void on_segv(int, siginfo_t* si, void*) {
  uintptr_t a = (uintptr_t)si->si_addr;
  uintptr_t lo = (uintptr_t)g_base + kImageBase, hi = lo + kImageSize;
  if (a >= lo && a < hi) {
    size_t pg = (a - lo) / kPage;
    if (!g_dirty[pg]) {   // primeira escrita na página: liberar e marcar
      g_dirty[pg] = 1;
      mprotect((void*)(lo + pg * kPage), kPage, PROT_READ | PROT_WRITE);
      return;
    }
  }
  g_fault_addr = a;
  if (g_armed) siglongjmp(g_jmp, 1);
  _exit(99);
}

void fake_target(PPCContext& ctx, uint8_t*) {
  Call c{(uint32_t)ctx.ctr.u32, {}, {}};
  PPCRegister* g[] = {&ctx.r3,&ctx.r4,&ctx.r5,&ctx.r6,&ctx.r7,&ctx.r8,&ctx.r9,&ctx.r10};
  PPCRegister* f[] = {&ctx.f1,&ctx.f2,&ctx.f3,&ctx.f4};
  for (int i = 0; i < 8; ++i) c.r[i] = g[i]->u32;
  for (int i = 0; i < 4; ++i) c.f[i] = f[i]->u64;
  uint64_t h = 0x9E3779B97F4A7C15ull * (c.target + 1);
  for (int i = 0; i < g_callargs; ++i) h = (h ^ c.r[i]) * 0x100000001B3ull;
  g_calls.push_back(c);
  ctx.r3.u64 = (h >> 7) & ((h & 1) ? 0xFFFFFFFFull : 0x3ull);   // ora booleano, ora valor cheio
  ctx.f1.f64 = (double)(int64_t)(h % 2001) / 8.0 - 125.0;
}

void map_rw(uint64_t guest, uint64_t size) {
  if (mprotect(g_base + guest, size, PROT_READ | PROT_WRITE)) { perror("mprotect"); exit(2); }
}

struct State {
  PPCContext ctx;
  std::vector<uint8_t> scratch, tls, stack;
  std::map<size_t, std::vector<uint8_t>> image_pages;  // página -> conteúdo
  bool faulted = false; uintptr_t fault = 0;
  std::vector<Call> calls;
};

void image_reset() {
  for (size_t pg = 0; pg < g_dirty.size(); ++pg)
    if (g_dirty[pg]) {
      std::memcpy(g_base + kImageBase + pg * kPage, g_pristine.data() + pg * kPage, kPage);
      g_dirty[pg] = 0;
    }
  mprotect(g_base + kImageBase, kImageSize, PROT_READ);
}

void load_inputs(const std::vector<uint8_t>& s, const std::vector<uint8_t>& t, const std::vector<uint8_t>& st) {
  std::memcpy(g_base + kScratch, s.data(), s.size());
  std::memcpy(g_base + kTls, t.data(), t.size());
  std::memcpy(g_base + kStack, st.data(), st.size());
}

State run(PPCFunc* fn, const PPCContext& in, const std::vector<uint8_t>& s,
          const std::vector<uint8_t>& t, const std::vector<uint8_t>& st) {
  image_reset();
  load_inputs(s, t, st);
  g_calls.clear();
  State out;
  static PPCContext ctx;          // fora da pilha: sobrevive ao siglongjmp
  ctx = in;
  g_armed = 1;
  if (sigsetjmp(g_jmp, 1) == 0) {
    fn(ctx, g_base);
  } else {
    out.faulted = true; out.fault = g_fault_addr - (uintptr_t)g_base;
  }
  g_armed = 0;
  out.ctx = ctx;
  out.calls = g_calls;
  out.scratch.assign(g_base + kScratch, g_base + kScratch + kScratchSize);
  out.tls.assign(g_base + kTls, g_base + kTls + kTlsSize);
  out.stack.assign(g_base + kStack, g_base + kStack + kStackSize);
  for (size_t pg = 0; pg < g_dirty.size(); ++pg)
    if (g_dirty[pg]) out.image_pages[pg].assign(g_base + kImageBase + pg * kPage,
                                                g_base + kImageBase + (pg + 1) * kPage);
  return out;
}

// ---- geração de entradas -------------------------------------------------
struct Arg { std::string kind; uint64_t lo = 0, hi = 0; };
std::map<std::string, Arg> parse_spec(const char* spec) {
  std::map<std::string, Arg> m;
  std::string s = spec ? spec : "";
  size_t i = 0;
  while (i < s.size()) {
    while (i < s.size() && s[i] == ' ') ++i;
    size_t j = s.find(' ', i); if (j == std::string::npos) j = s.size();
    std::string tok = s.substr(i, j - i); i = j;
    size_t eq = tok.find('='); if (eq == std::string::npos) continue;
    Arg a; std::string v = tok.substr(eq + 1);
    size_t c = v.find(':');
    a.kind = v.substr(0, c);
    if (!a.kind.empty() && isdigit((unsigned char)a.kind[0])) { a.lo = a.hi = strtoull(a.kind.c_str(), nullptr, 0); a.kind = "num"; }
    if (c != std::string::npos) {
      std::string r = v.substr(c + 1); size_t d = r.find("..");
      a.lo = strtoull(r.substr(0, d).c_str(), nullptr, 0);
      a.hi = d == std::string::npos ? a.lo : strtoull(r.substr(d + 2).c_str(), nullptr, 0);
    }
    m[tok.substr(0, eq)] = a;
  }
  return m;
}

PPCRegister* gpr(PPCContext& c, int n) {
  PPCRegister* t[] = {&c.r0,&c.r1,&c.r2,&c.r3,&c.r4,&c.r5,&c.r6,&c.r7,&c.r8,&c.r9,&c.r10,&c.r11,&c.r12,&c.r13,&c.r14,&c.r15,
                      &c.r16,&c.r17,&c.r18,&c.r19,&c.r20,&c.r21,&c.r22,&c.r23,&c.r24,&c.r25,&c.r26,&c.r27,&c.r28,&c.r29,&c.r30,&c.r31};
  return t[n];
}
PPCRegister* fpr(PPCContext& c, int n) {
  PPCRegister* t[] = {&c.f0,&c.f1,&c.f2,&c.f3,&c.f4,&c.f5,&c.f6,&c.f7,&c.f8,&c.f9,&c.f10,&c.f11,&c.f12,&c.f13,&c.f14,&c.f15,
                      &c.f16,&c.f17,&c.f18,&c.f19,&c.f20,&c.f21,&c.f22,&c.f23,&c.f24,&c.f25,&c.f26,&c.f27,&c.f28,&c.f29,&c.f30,&c.f31};
  return t[n];
}

uint32_t scratch_ptr(std::mt19937_64& rng, uint32_t align) {
  uint32_t off = (uint32_t)(rng() % (kScratchSize - 0x10000)) & ~(align - 1);
  return kScratch + 0x8000 + (off % (kScratchSize - 0x18000));
}

uint64_t gen_value(const Arg& a, std::mt19937_64& rng, bool& is_float, double& fv) {
  is_float = false;
  if (a.kind == "ptr")   return scratch_ptr(rng, a.lo ? (uint32_t)a.lo : 4);
  if (a.kind == "obj")   return kScratch + (uint32_t)(rng() % 64) * 64;
  if (a.kind == "bptr")  return scratch_ptr(rng, 1);
  if (a.kind == "int")   return (uint32_t)rng();
  if (a.kind == "u8")    return rng() & 0xFF;
  if (a.kind == "u16")   return rng() & 0xFFFF;
  if (a.kind == "range") return a.lo + rng() % (a.hi - a.lo + 1);
  if (a.kind == "const") return a.lo;
  if (a.kind == "image") return a.lo + ((rng() % ((a.hi - a.lo) / 4 + 1)) * 4);
  if (a.kind == "f" || a.kind == "f32") {
    is_float = true;
    std::uniform_real_distribution<double> d(a.lo ? -(double)a.lo : -1000.0, a.hi ? (double)a.hi : 1000.0);
    fv = d(rng); if (a.kind == "f32") fv = (float)fv;
    return 0;
  }
  return (uint32_t)rng();
}

void fill_scratch(std::vector<uint8_t>& s, std::mt19937_64& rng) {
  // Mistura de bytes aleatórios, zeros, floats plausíveis e ponteiros válidos para a própria área.
  for (size_t i = 0; i < s.size(); i += 4) {
    uint32_t v; int k = rng() % 8;
    if (k < 2) v = 0;
    else if (k < 4) v = scratch_ptr(rng, 16);
    else if (k == 4) { float f = (float)((int)(rng() % 2000) - 1000) / 7.0f; std::memcpy(&v, &f, 4); }
    else if (k == 5) v = rng() % 64;
    else if (k == 6 && (rng() & 1)) v = kScratch + (uint32_t)(rng() % 64) * 64;   // objeto com vtable falsa
    else v = (uint32_t)rng();
    v = __builtin_bswap32(v); std::memcpy(&s[i], &v, 4);
  }
}

void fill_vtables(std::vector<uint8_t>& s) {
  // 64 objetos de 64 bytes no início da área: a palavra 0 de cada um aponta para uma
  // vtable em 0x40001000 + k*256, cujas entradas são endereços falsos registrados.
  for (int k = 0; k < 64; ++k) {
    uint32_t vt = __builtin_bswap32(kScratch + 0x1000 + k * 256);
    std::memcpy(&s[k * 64], &vt, 4);
    for (int e = 0; e < 64; ++e) {
      uint32_t fn = __builtin_bswap32(kFakeFn + 4 * ((k + e) % kFakeCount));
      std::memcpy(&s[0x1000 + k * 256 + e * 4], &fn, 4);
    }
  }
}

std::string diff(const State& a, const State& b, int callargs) {
  char buf[256];
  if (a.faulted != b.faulted) {
    snprintf(buf, sizeof buf, "falha de acesso só em %s (guest 0x%08" PRIxPTR ")", a.faulted ? "original" : "reconstruída",
             (uintptr_t)(a.faulted ? a.fault : b.fault)); return buf;
  }
  if (a.faulted) return "";   // ambas falharam: entrada inválida, não conta como prova nem como erro
  auto chk = [&](const char* n, uint64_t x, uint64_t y) -> std::string {
    if (x == y) return ""; snprintf(buf, sizeof buf, "%s: original 0x%" PRIx64 " reconstruída 0x%" PRIx64, n, x, y); return buf; };
  if (a.calls.size() != b.calls.size()) {
    snprintf(buf, sizeof buf, "chamadas: original %zu reconstruída %zu", a.calls.size(), b.calls.size()); return buf; }
  for (size_t i = 0; i < a.calls.size(); ++i) {
    const Call &x = a.calls[i], &y = b.calls[i];
    if (x.target != y.target) { snprintf(buf, sizeof buf, "chamada %zu: destino 0x%08x vs 0x%08x", i, x.target, y.target); return buf; }
    for (int k = 0; k < callargs; ++k) if (x.r[k] != y.r[k]) {
      snprintf(buf, sizeof buf, "chamada %zu (0x%08x): r%d original 0x%" PRIx64 " reconstruída 0x%" PRIx64, i, x.target, k + 3, x.r[k], y.r[k]); return buf; }
  }
  PPCContext ca = a.ctx, cb = b.ctx;
  std::string r;
  const int keep[] = {1, 2, 3, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31};
  for (int n : keep) { std::string nm = "r" + std::to_string(n); if (!(r = chk(nm.c_str(), gpr(ca, n)->u64, gpr(cb, n)->u64)).empty()) return r; }
  const int fkeep[] = {1, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31};
  for (int n : fkeep) { std::string nm = "f" + std::to_string(n); if (!(r = chk(nm.c_str(), fpr(ca, n)->u64, fpr(cb, n)->u64)).empty()) return r; }
  auto cmpmem = [&](const char* n, const std::vector<uint8_t>& x, const std::vector<uint8_t>& y, uint32_t g, size_t from) -> std::string {
    for (size_t i = from; i < x.size(); ++i) if (x[i] != y[i]) {
      snprintf(buf, sizeof buf, "%s: byte 0x%08zx original %02x reconstruída %02x", n, g + i, x[i], y[i]); return buf; }
    return ""; };
  if (!(r = cmpmem("dados", a.scratch, b.scratch, kScratch, 0)).empty()) return r;
  if (!(r = cmpmem("r13", a.tls, b.tls, kTls, 0)).empty()) return r;
  if (!(r = cmpmem("pilha", a.stack, b.stack, kStack, kSp - kStack)).empty()) return r;
  std::map<size_t, int> pages; for (auto& p : a.image_pages) pages[p.first] |= 1; for (auto& p : b.image_pages) pages[p.first] |= 2;
  for (auto& [pg, who] : pages) {
    const uint8_t* pa = who & 1 ? a.image_pages.at(pg).data() : g_pristine.data() + pg * kPage;
    const uint8_t* pb = who & 2 ? b.image_pages.at(pg).data() : g_pristine.data() + pg * kPage;
    for (size_t i = 0; i < kPage; ++i) if (pa[i] != pb[i]) {
      snprintf(buf, sizeof buf, "imagem: byte 0x%08llx original %02x reconstruída %02x",
               (unsigned long long)(kImageBase + pg * kPage + i), pa[i], pb[i]); return buf; }
  }
  return "";
}
}  // namespace

// Alvo indireto fora do código traduzido: tratar como falha de acesso controlada,
// no lugar do resolvedor do runtime (que aborta o processo).
static void bad_indirect(PPCContext& ctx, uint8_t*) {
  g_fault_addr = (uintptr_t)g_base + ctx.last_indirect_target;
  if (g_armed) siglongjmp(g_jmp, 1);
  _exit(98);
}
namespace rex::runtime {
PPCFunc* ResolveIndirectFunction(uint32_t) { return bad_indirect; }
}

int main(int argc, char** argv) {
  const char* pe = getenv("NIER_PE");
  int trials = getenv("NIER_TRIALS") ? atoi(getenv("NIER_TRIALS")) : 2000;
  const char* only = argc > 1 ? argv[1] : nullptr;
  if (!pe) { fprintf(stderr, "defina NIER_PE=<default.pe privado>\n"); return 2; }
  FILE* f = fopen(pe, "rb"); if (!f) { perror(pe); return 2; }
  g_pristine.resize(kImageSize);
  size_t got = fread(g_pristine.data(), 1, kImageSize, f); fclose(f);
  if (got != kImageSize) { fprintf(stderr, "PE com tamanho %zu, esperado %llu\n", got, (unsigned long long)kImageSize); return 2; }

  // 4 GB de endereço do guest, alinhados a 64 KB, sem acesso por padrão.
  void* m = mmap(nullptr, (1ull << 32) + 0x10000, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
  if (m == MAP_FAILED) { perror("mmap"); return 2; }
  g_base = (uint8_t*)(((uintptr_t)m + 0xFFFF) & ~(uintptr_t)0xFFFF);
  map_rw(kImageBase, kImageSize);
  std::memcpy(g_base + kImageBase, g_pristine.data(), kImageSize);
  uint64_t ft = kImageBase + kImageSize, ftsz = (kCodeSize + kThunk) * 2;
  map_rw(ft, (ftsz + 0xFFFF) & ~0xFFFFull);
  size_t nfn = 0;
  for (PPCFuncMapping* p = PPCFuncMappings; p->host; ++p, ++nfn)
    *(PPCFunc**)(g_base + ft + (p->guest - kCodeBase) * 2) = p->host;
  for (int i = 0; i < kFakeCount; ++i)
    *(PPCFunc**)(g_base + ft + (kFakeFn + 4 * i - kCodeBase) * 2) = fake_target;
  map_rw(kScratch, kScratchSize); map_rw(kTls, kTlsSize); map_rw(kStack, kStackSize);
  g_dirty.assign(kImageSize / kPage, 0);
  mprotect(g_base + kImageBase, kImageSize, PROT_READ);

  struct sigaction sa{}; sa.sa_sigaction = on_segv; sa.sa_flags = SA_SIGINFO | SA_NODEFER;
  sigaction(SIGSEGV, &sa, nullptr); sigaction(SIGBUS, &sa, nullptr);

  FILE* out = fopen(getenv("NIER_OUT") ? getenv("NIER_OUT") : "/dev/stdout", "w");
  if (!out) { perror("NIER_OUT"); return 2; }
  fprintf(out, "{\"tool\":\"nierverify\",\"functions_in_table\":%zu,\"trials\":%d,\"results\":[\n", nfn, trials);
  int bad = 0; bool first = true;
  for (auto& e : nierverify::registry()) {
    if (only && strcmp(only, e.name)) continue;
    auto spec = parse_spec(e.spec);
    int callargs = spec.count("callargs") ? (int)spec["callargs"].lo : 6;
    g_callargs = callargs;
    std::mt19937_64 rng(0x4E4945520000ull ^ e.addr);
    int ok = 0, both_fault = 0; std::string err;
    std::vector<uint8_t> s(kScratchSize), t(kTlsSize), st(kStackSize);
    for (int i = 0; i < trials && err.empty(); ++i) {
      fill_scratch(s, rng); fill_vtables(s); fill_scratch(t, rng); fill_scratch(st, rng);
      PPCContext in{};
      for (int n = 0; n < 32; ++n) gpr(in, n)->u64 = rng();
      for (int n = 0; n < 32; ++n) fpr(in, n)->f64 = (double)((int64_t)(rng() % 20000) - 10000) / 16.0;
      in.r1.u64 = kSp; in.r13.u64 = kTls + 0x100; in.lr = 0x82000000;
      // Registradores sem especificação recebem ponteiro válido ou inteiro pequeno.
      for (int n = 3; n <= 10; ++n) {
        std::string k = "r" + std::to_string(n);
        if (spec.count(k)) { bool fl; double fv; gpr(in, n)->u64 = gen_value(spec[k], rng, fl, fv); }
        else gpr(in, n)->u64 = (rng() & 1) ? scratch_ptr(rng, 16) : rng() % 256;
      }
      for (int n = 1; n <= 8; ++n) {
        std::string k = "f" + std::to_string(n);
        if (spec.count(k)) { bool fl; double fv = 0; gen_value(spec[k], rng, fl, fv); fpr(in, n)->f64 = fv; }
      }
      // Caminhos de ponteiros: "@r3.0.10896=ptr" grava, em (ld32(r3+0))+10896, um valor gerado.
      // Cada passo intermediário recebe um ponteiro válido para a área de teste.
      for (auto& [key, arg] : spec) {
        if (key.empty() || key[0] != '@') continue;
        std::vector<std::string> parts; size_t q = 1, dot;
        while ((dot = key.find('.', q)) != std::string::npos) { parts.push_back(key.substr(q, dot - q)); q = dot + 1; }
        parts.push_back(key.substr(q));
        uint32_t cur = parts[0] == "r13" ? kTls + 0x100 : gpr(in, atoi(parts[0].c_str() + 1))->u32;
        auto in_area = [&](uint32_t a, std::vector<uint8_t>*& v, uint32_t& off) {
          if (a >= kScratch && a + 4 <= kScratch + kScratchSize) { v = &s; off = a - kScratch; return true; }
          if (a >= kTls && a + 4 <= kTls + kTlsSize) { v = &t; off = a - kTls; return true; }
          return false; };
        bool okp = true;
        for (size_t k = 1; k < parts.size() && okp; ++k) {
          uint32_t addr = cur + (uint32_t)strtoul(parts[k].c_str(), nullptr, 0);
          std::vector<uint8_t>* v; uint32_t off;
          if (!in_area(addr, v, off)) { okp = false; break; }
          uint32_t val;
          if (k + 1 < parts.size()) val = scratch_ptr(rng, 16);
          else { bool fl; double fv; val = (uint32_t)gen_value(arg, rng, fl, fv); }
          uint32_t be = __builtin_bswap32(val); std::memcpy(&(*v)[off], &be, 4);
          cur = val;
        }
      }
      State a = run(e.orig, in, s, t, st);
      State b = run(e.decomp, in, s, t, st);
      err = diff(a, b, callargs);
      if (err.empty()) { if (a.faulted) ++both_fault; else ++ok; }
      else err = "tentativa " + std::to_string(i) + ": " + err;
    }
    const int min_ok = trials < 2000 ? trials / 4 : 500;
    bool pass = err.empty() && ok >= min_ok;
    if (!pass) ++bad;
    fprintf(out, "%s{\"name\":\"%s\",\"address\":\"0x%08X\",\"file\":\"%s\",\"pass\":%s,\"equal\":%d,\"both_faulted\":%d,\"error\":\"%s\"}",
           first ? "" : ",\n", e.name, e.addr, e.file, pass ? "true" : "false", ok, both_fault,
           err.empty() && !pass ? "poucas tentativas executaram sem falha de acesso" : err.c_str());
    first = false; fflush(out);
  }
  fprintf(out, "\n],\"failed\":%d}\n", bad);
  fclose(out);
  return bad ? 1 : 0;
}
