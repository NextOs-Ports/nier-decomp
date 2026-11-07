// Funções reconstruídas do NieR (Xbox 360). Cada uma tem a mesma assinatura da
// função traduzida (`PPCContext&`, memória do console em `base`).
//
// No jogo, NIER_DECOMP(sub_X) define o símbolo forte `sub_X`, que substitui o
// alias fraco gerado pelo ReXGlue: a versão reconstruída roda no lugar da original.
// No comparador (NIER_VERIFY), o nome vira `decomp_sub_X` e a original continua
// acessível como `__imp__sub_X`, servindo de oráculo.
#pragma once
#include <cstdint>
#include <cstring>

#if __has_include("nier_pch.h")
#include "nier_pch.h"
#endif

namespace nier::guest {
// Acesso big-endian à memória do console. `base` é o início dos 4 GB do guest.
inline uint8_t  ld8 (const uint8_t* base, uint32_t a) { return base[a]; }
inline uint16_t ld16(const uint8_t* base, uint32_t a) { uint16_t v; std::memcpy(&v, base + a, 2); return __builtin_bswap16(v); }
inline uint32_t ld32(const uint8_t* base, uint32_t a) { uint32_t v; std::memcpy(&v, base + a, 4); return __builtin_bswap32(v); }
inline uint64_t ld64(const uint8_t* base, uint32_t a) { uint64_t v; std::memcpy(&v, base + a, 8); return __builtin_bswap64(v); }
inline float    ldf (const uint8_t* base, uint32_t a) { uint32_t v = ld32(base, a); float f; std::memcpy(&f, &v, 4); return f; }
inline void st8 (uint8_t* base, uint32_t a, uint8_t v)  { base[a] = v; }
inline void st16(uint8_t* base, uint32_t a, uint16_t v) { v = __builtin_bswap16(v); std::memcpy(base + a, &v, 2); }
inline void st32(uint8_t* base, uint32_t a, uint32_t v) { v = __builtin_bswap32(v); std::memcpy(base + a, &v, 4); }
inline void st64(uint8_t* base, uint32_t a, uint64_t v) { v = __builtin_bswap64(v); std::memcpy(base + a, &v, 8); }
inline void stf (uint8_t* base, uint32_t a, float f) { uint32_t v; std::memcpy(&v, &f, 4); st32(base, a, v); }
// Deslocamentos com quantidade em registrador, com a semântica do PowerPC:
// usam os 6 (32 bits) ou 7 (64 bits) bits baixos, e quantidade >= largura dá 0 (ou o sinal).
inline uint32_t slw(uint32_t x, uint64_t n) { n &= 63; return n >= 32 ? 0 : x << n; }
inline uint32_t srw(uint32_t x, uint64_t n) { n &= 63; return n >= 32 ? 0 : x >> n; }
inline uint32_t sraw(uint32_t x, uint64_t n) { n &= 63; return n >= 32 ? (static_cast<int32_t>(x) < 0 ? 0xFFFFFFFFu : 0u) : static_cast<uint32_t>(static_cast<int32_t>(x) >> n); }
// rlwinm: rotaciona 32 bits à esquerda e aplica a máscara dos bits mb..me (numeração IBM).
inline uint32_t rlwinm(uint32_t x, uint32_t sh, uint32_t mb, uint32_t me) {
  const uint32_t rot = sh ? ((x << sh) | (x >> (32 - sh))) : x;
  const uint32_t from = 0xFFFFFFFFu >> mb, to = 0xFFFFFFFFu << (31 - me);
  const uint32_t mask = mb <= me ? (from & to) : (from | to);
  return rot & mask;
}
// fctiwz: double → int32 truncando, com saturação (NaN e abaixo de INT_MIN dão 0x80000000).
inline uint32_t fctiwz(double v) {
  if (v != v) return 0x80000000u;
  if (v >= 2147483647.0) return 0x7FFFFFFFu;
  if (v <= -2147483649.0) return 0x80000000u;
  return static_cast<uint32_t>(static_cast<int32_t>(v));
}
inline uint64_t sld(uint64_t x, uint64_t n) { n &= 127; return n >= 64 ? 0 : x << n; }
inline uint64_t srd(uint64_t x, uint64_t n) { n &= 127; return n >= 64 ? 0 : x >> n; }
}  // namespace nier::guest

#ifdef REX_CALL_INDIRECT_FUNC
namespace nier::guest {
// Chamada indireta (ponteiro de função ou método virtual), como `mtctr; bctrl`.
// Os argumentos já devem estar em ctx.r3.. / ctx.f1.. conforme a ABI do Xbox 360.
inline void call_indirect(PPCContext& __restrict ctx, uint8_t* base, uint32_t target) {
  ctx.ctr.u64 = target;
  REX_CALL_INDIRECT_FUNC(target);
}
// Chamada a uma função do jogo pelo endereço (equivale ao `bl` direto, mas passa pela
// tabela de funções). Preferir a forma por símbolo abaixo no caminho quente.
inline void call_guest(PPCContext& __restrict ctx, uint8_t* base, uint32_t target) {
  call_indirect(ctx, base, target);
}
// Chamada direta por símbolo (`DECLARE_REX_FUNC(sub_X)` + `sub_X`): igual ao `bl` traduzido,
// sem tabela nem despacho indireto. No build do jogo, se sub_X também foi reconstruída, a
// chamada vai direto para a reconstrução.
inline void call_guest(PPCContext& __restrict ctx, uint8_t* base, PPCFunc* target) {
  target(ctx, base);
}
// Quadro de pilha do guest enquanto a função chama outras: reserva `size` bytes abaixo
// do r1 (com o encadeamento no topo, como `stwu`) e devolve o r1 no fim do escopo.
struct GuestFrame {
  PPCContext& ctx;
  uint8_t* base;
  uint32_t saved;
  GuestFrame(PPCContext& c, uint8_t* b, uint32_t size) : ctx(c), base(b), saved(c.r1.u32) {
    ctx.r1.u64 = saved - size;
    st32(base, ctx.r1.u32, saved);
  }
  ~GuestFrame() { ctx.r1.u64 = saved; }
  uint32_t slot(uint32_t off) const { return ctx.r1.u32 + off; }
};
// Método virtual `slot` do objeto em `obj` (vtable na palavra 0).
inline void call_virtual(PPCContext& __restrict ctx, uint8_t* base, uint32_t obj, uint32_t slot) {
  call_indirect(ctx, base, ld32(base, ld32(base, obj) + slot * 4));
}
}  // namespace nier::guest
#endif

// Pausa curta numa espera ativa do guest. No console era `db16cyc` (só atraso,
// sem efeito em memória ou registradores); no port cede o núcleo ao sistema.
#include <sched.h>
namespace nier::host {
inline void spin_pause() {
#ifndef NIER_VERIFY
  sched_yield();
#endif
}
}  // namespace nier::host

#ifdef NIER_VERIFY
#define NIER_DECOMP(name) extern "C" void decomp_##name([[maybe_unused]] PPCContext& __restrict ctx, [[maybe_unused]] uint8_t* base)
#else
// No jogo, só as reconstruções VERIFICADAS viram o símbolo forte `sub_X`: o cabeçalho gerado
// por tools/decomp/gen_overrides_header.py define `NIER_OVERRIDE_sub_X` como `sub_X`; sem a
// definição o nome fica inerte (`NIER_OVERRIDE_sub_X`) e a tradução continua valendo.
#if __has_include("nier_decomp_overrides.h")
#include "nier_decomp_overrides.h"
#endif
#define NIER_DECOMP_CAT2(a, b) a##b
#define NIER_DECOMP_SYMBOL(name) NIER_DECOMP_CAT2(NIER_OVERRIDE_, name)
#define NIER_DECOMP(name) extern "C" void NIER_DECOMP_SYMBOL(name)([[maybe_unused]] PPCContext& __restrict ctx, [[maybe_unused]] uint8_t* base)
#endif

// Registro no comparador. Fora do NIER_VERIFY não gera nada.
// spec: "r3=ptr r4=range:0..512 f1=f32 ..." (ver tools/verify/README.md).
#ifdef NIER_VERIFY
namespace nierverify { int add(const char*, uint32_t, PPCFunc*, PPCFunc*, const char*, const char*); }
#define NIER_VERIFY_SPEC(name, addr, spec)                                                \
  extern "C" void __imp__##name(PPCContext& __restrict, uint8_t*);                       \
  static int nierverify_reg_##name = nierverify::add(#name, addr, __imp__##name, decomp_##name, spec, __FILE__)
#else
#define NIER_VERIFY_SPEC(name, addr, spec) static_assert(true)
#endif
