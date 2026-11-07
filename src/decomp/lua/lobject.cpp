// lobject/lgc do Lua 5.1 embutido no NieR, reconstruídos e verificados.
#include <nier/decomp.h>
using namespace nier::guest;

namespace {
constexpr uint32_t kLog2Table = 0x8200DE28;  // log_2[256] do Lua
}

// sub_821307D0 (48 bytes) — luaO_int2fb: inteiro para "float byte" (eeeeexxx).
NIER_DECOMP(sub_821307D0) {
  if (ctx.r3.u32 < 8) return;               // expoente 0: o próprio valor
  uint32_t x = ctx.r3.u32, e = 0;
  while (x >= 8) { x = (x + 1) >> 1; ++e; }
  ctx.r3.u64 = (e << 3) | x;
}
NIER_VERIFY_SPEC(sub_821307D0, 0x821307D0, "r3=int");

// sub_82130800 (152 bytes) — luaO_log2: piso do log2 (−1 para zero), pela tabela de 256 entradas.
NIER_DECOMP(sub_82130800) {
  const uint32_t x = ctx.r3.u32;
  if (x >= 0x1000000) { ctx.r3.u64 = ld8(base, kLog2Table + (x >> 24) - 1) + 24; return; }
  if (x >= 0x10000)   { ctx.r3.u64 = ld8(base, kLog2Table + ((x >> 16) & 0xFF) - 1) + 16; return; }
  if (x >= 256)       { ctx.r3.u64 = ld8(base, kLog2Table + ((x >> 8) & 0xFF) - 1) + 8; return; }
  if (x != 0)         { ctx.r3.u64 = ld8(base, kLog2Table + x - 1); return; }
  ctx.r3.u64 = ~0ull;
}
NIER_VERIFY_SPEC(sub_82130800, 0x82130800, "r3=int");

// sub_821308A0 (88 bytes) — luaO_rawequalObj: mesmo tipo e mesmo valor
// (nil sempre igual; número compara double; demais comparam a palavra do valor).
NIER_DECOMP(sub_821308A0) {
  const uint32_t a = ctx.r3.u32, b = ctx.r4.u32;
  const uint32_t ta = ld32(base, a), tb = ld32(base, b);
  if (static_cast<int32_t>(ta) != static_cast<int32_t>(tb)) { ctx.r3.u64 = 0; return; }
  if (ta == 0) { ctx.r3.u64 = 1; return; }
  if (ta == 3) {
    const uint64_t x = ld64(base, a + 8), y = ld64(base, b + 8);
    double dx, dy; std::memcpy(&dx, &x, 8); std::memcpy(&dy, &y, 8);
    ctx.r3.u64 = dx == dy ? 1 : 0;
    return;
  }
  ctx.r3.u64 = ld32(base, a + 8) == ld32(base, b + 8) ? 1 : 0;
}
NIER_VERIFY_SPEC(sub_821308A0, 0x821308A0, "r3=ptr r4=ptr @r3.0=range:0..5 @r4.0=range:0..5 @r3.8=range:0..2 @r4.8=range:0..2");

// sub_8212C550 (36 bytes) — luaC_link: liga o objeto à lista de GC (g->rootgc em +12)
// com o tipo dado e marcação zerada.
NIER_DECOMP(sub_8212C550) {
  const uint32_t L = ctx.r3.u32, o = ctx.r4.u32;
  st32(base, o + 0, ld32(base, ld32(base, L + 16) + 12));
  st32(base, ld32(base, L + 16) + 12, o);
  st8(base, o + 5, 0);
  st8(base, o + 4, static_cast<uint8_t>(ctx.r5.u32));
}
NIER_VERIFY_SPEC(sub_8212C550, 0x8212C550, "r3=ptr r4=ptr @r3.16=ptr r5=u8");

// sub_82128AB8 (80 bytes) — lua_sethook(L, func, mask, count): sem função ou máscara 0
// desliga o hook. Grava contador base e atual e liga "permitir hook".
NIER_DECOMP(sub_82128AB8) {
  const uint32_t L = ctx.r3.u32;
  uint32_t func = ctx.r4.u32;
  uint32_t mask = ctx.r5.u32;
  if (func == 0 || static_cast<int32_t>(mask) == 0) { func = 0; mask = 0; }
  st32(base, L + 60, func);
  st32(base, L + 52, ctx.r6.u32);
  st32(base, L + 56, ctx.r6.u32);
  st8(base, L + 48, static_cast<uint8_t>(mask));
  st8(base, L + 50, 0);
  ctx.r3.u64 = 1;
}
NIER_VERIFY_SPEC(sub_82128AB8, 0x82128AB8, "r3=ptr r5=range:0..15");
