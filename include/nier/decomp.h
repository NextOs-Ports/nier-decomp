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
}  // namespace nier::guest

#ifdef REX_CALL_INDIRECT_FUNC
namespace nier::guest {
// Chamada indireta (ponteiro de função ou método virtual), como `mtctr; bctrl`.
// Os argumentos já devem estar em ctx.r3.. / ctx.f1.. conforme a ABI do Xbox 360.
inline void call_indirect(PPCContext& __restrict ctx, uint8_t* base, uint32_t target) {
  ctx.ctr.u64 = target;
  REX_CALL_INDIRECT_FUNC(target);
}
// Método virtual `slot` do objeto em `obj` (vtable na palavra 0).
inline void call_virtual(PPCContext& __restrict ctx, uint8_t* base, uint32_t obj, uint32_t slot) {
  call_indirect(ctx, base, ld32(base, ld32(base, obj) + slot * 4));
}
}  // namespace nier::guest
#endif

#ifdef NIER_VERIFY
#define NIER_DECOMP(name) extern "C" void decomp_##name([[maybe_unused]] PPCContext& __restrict ctx, [[maybe_unused]] uint8_t* base)
#else
#define NIER_DECOMP(name) extern "C" void name([[maybe_unused]] PPCContext& __restrict ctx, [[maybe_unused]] uint8_t* base)
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
