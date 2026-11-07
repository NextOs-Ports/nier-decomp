// API do Lua 5.1 embutido no NieR, reconstruída e verificada.
// lua_State: +8 top, +12 base, +16 l_G, +64 env. TValue: 16 bytes (+0 tipo, +8 valor).
// Tipos: 0 nil, 1 boolean, 2 lightuserdata, 3 number, 4 string...
#include <nier/decomp.h>
using namespace nier::guest;

namespace {
constexpr uint32_t kTop = 8, kBase = 12, kGlobal = 16;
constexpr int32_t kRegistryIndex = -10000, kEnvironIndex = -10001;

// Empilha um TValue com o tipo e devolve o endereço do slot (antes de avançar o topo).
uint32_t push_slot(uint8_t* base, uint32_t L, uint32_t type) {
  const uint32_t slot = ld32(base, L + kTop);
  st32(base, slot + 0, type);
  return slot;
}
void advance_top(uint8_t* base, uint32_t L) { st32(base, L + kTop, ld32(base, L + kTop) + 16); }
}  // namespace

// sub_82121D00 (104 bytes) — index2adr(L, idx): endereço do valor no índice.
// Positivo/negativo comum: relativo ao topo/base; -10000 registro; -10001 ambiente;
// abaixo disso, upvalues da função C atual (zero se fora do limite).
NIER_DECOMP(sub_82121D00) {
  const uint32_t L = ctx.r3.u32;
  const int32_t idx = static_cast<int32_t>(ctx.r4.u32);
  if (idx > kRegistryIndex) {
    ctx.r3.u64 = static_cast<uint64_t>(ld32(base, L + kTop)) + static_cast<uint32_t>(idx << 4);
    return;
  }
  if (idx == kEnvironIndex) { ctx.r3.u64 = ctx.r3.u64 + 64; return; }
  if (idx == kRegistryIndex) { ctx.r3.u64 = static_cast<uint64_t>(ld32(base, L + kGlobal)) + 48; return; }
  const int32_t up = kEnvironIndex - idx;
  const uint32_t func = ld32(base, ld32(base, L + kBase) - 8);
  if (up > static_cast<int32_t>(ld8(base, func + 7))) { ctx.r3.u64 = 0; return; }
  ctx.r3.u64 = static_cast<uint64_t>(static_cast<uint32_t>(up << 4)) + func;
}
NIER_VERIFY_SPEC(sub_82121D00, 0x82121D00, "r3=ptr r4=range:0xFFFFD8E8..0xFFFFD8F8 @r3.12=ptr");

// sub_82121E60 (88 bytes) — lua_xmove(from, to, n): move n valores do topo de `from` para `to`.
NIER_DECOMP(sub_82121E60) {
  const uint32_t from = ctx.r3.u32, to = ctx.r4.u32;
  const int32_t n = static_cast<int32_t>(ctx.r5.u32);
  st32(base, from + kTop, ld32(base, from + kTop) - static_cast<uint32_t>(n << 4));
  for (int32_t i = 0; i < n; ++i) {
    const uint32_t src = ld32(base, from + kTop) + static_cast<uint32_t>(i) * 16;
    const uint32_t dst = ld32(base, to + kTop);
    st32(base, dst + 0, ld32(base, src + 0));
    st64(base, dst + 8, ld64(base, src + 8));
    st32(base, to + kTop, ld32(base, to + kTop) + 16);
  }
}
NIER_VERIFY_SPEC(sub_82121E60, 0x82121E60, "r3=ptr r4=ptr r5=range:0..6 @r3.8=ptr:16 @r4.8=ptr:16");

// sub_82121F38 (96 bytes) — lua_settop(L, idx): idx >= 0 preenche com nil até base+idx;
// negativo recua o topo.
NIER_DECOMP(sub_82121F38) {
  const uint32_t L = ctx.r3.u32;
  const int32_t idx = static_cast<int32_t>(ctx.r4.u32);
  if (idx < 0) {
    st32(base, L + kTop, static_cast<uint32_t>((idx + 1) << 4) + ld32(base, L + kTop));
    return;
  }
  const uint32_t span = static_cast<uint32_t>(idx << 4);
  while (ld32(base, L + kTop) < ld32(base, L + kBase) + span) {
    st32(base, ld32(base, L + kTop), 0);
    st32(base, L + kTop, ld32(base, L + kTop) + 16);
  }
  st32(base, L + kTop, ld32(base, L + kBase) + span);
}
NIER_VERIFY_SPEC(sub_82121F38, 0x82121F38, "r3=ptr r4=range:0xFFFFFFFA..8 @r3.12=ptr:16 @r3.8=ptr:16");

// sub_82122200 (32 bytes) — lua_typename(L, t): nome do tipo; -1 é "no value".
NIER_DECOMP(sub_82122200) {
  const int32_t t = static_cast<int32_t>(ctx.r4.u32);
  if (t == -1) { ctx.r3.u64 = 0x8200CAC8; return; }
  ctx.r3.u64 = ld32(base, 0x8200E57C + static_cast<uint32_t>(t << 2));
}
NIER_VERIFY_SPEC(sub_82122200, 0x82122200, "r4=range:0xFFFFFFFF..8");

// sub_82122988 (32 bytes) — lua_pushnil.
NIER_DECOMP(sub_82122988) { push_slot(base, ctx.r3.u32, 0); advance_top(base, ctx.r3.u32); }
NIER_VERIFY_SPEC(sub_82122988, 0x82122988, "r3=ptr @r3.8=ptr:16");

// sub_821229A8 (32 bytes) — lua_pushnumber(L, n).
NIER_DECOMP(sub_821229A8) {
  const uint32_t slot = ld32(base, ctx.r3.u32 + kTop);
  uint64_t bits; std::memcpy(&bits, &ctx.f1.f64, 8);
  st64(base, slot + 8, bits);
  st32(base, slot + 0, 3);
  advance_top(base, ctx.r3.u32);
}
NIER_VERIFY_SPEC(sub_821229A8, 0x821229A8, "r3=ptr @r3.8=ptr:16 f1=f");

// sub_82122C58 (40 bytes) — lua_pushboolean(L, b).
NIER_DECOMP(sub_82122C58) {
  const uint32_t slot = push_slot(base, ctx.r3.u32, 1);
  st32(base, slot + 8, ctx.r4.u64 != 0 ? 1 : 0);
  advance_top(base, ctx.r3.u32);
}
NIER_VERIFY_SPEC(sub_82122C58, 0x82122C58, "r3=ptr @r3.8=ptr:16 r4=range:0..2");

// sub_82122C80 (32 bytes) — lua_pushlightuserdata(L, p).
NIER_DECOMP(sub_82122C80) {
  const uint32_t slot = push_slot(base, ctx.r3.u32, 2);
  st32(base, slot + 8, ctx.r4.u32);
  advance_top(base, ctx.r3.u32);
}
NIER_VERIFY_SPEC(sub_82122C80, 0x82122C80, "r3=ptr @r3.8=ptr:16");

// sub_821211A0 (24 bytes) — endereço do elemento `i` (80 bytes) a partir de +8.
NIER_DECOMP(sub_821211A0) {
  ctx.r3.u64 = ctx.r3.u64 + static_cast<uint32_t>(ctx.r4.u32 * 80u) + 8;
}
NIER_VERIFY_SPEC(sub_821211A0, 0x821211A0, "r4=range:0..100");
