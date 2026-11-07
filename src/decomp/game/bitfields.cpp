// Testes de campo de bits ("todos ligados" ou "todos desligados"), reconstruídos do NieR.
// Campo = bits [lo, hi) do valor em r3. Devolve 1 se o campo inteiro for igual a `on`.
#include <nier/decomp.h>
using namespace nier::guest;

// sub_821174A0 (128 bytes) — campo num byte. r4 = on, r5 = lo, r6 = hi.
NIER_DECOMP(sub_821174A0) {
  const uint32_t lo = ctx.r5.u32, hi = ctx.r6.u32;
  uint32_t mask = 0, want = 0;
  if (lo != hi) {
    const uint64_t keep = ctx.r5.u64 - ctx.r6.u64 + 8;   // 8 − largura
    mask = slw(sraw(255, keep), ctx.r5.u64) & 0xFF;
    const uint32_t ones = static_cast<uint32_t>(-static_cast<int64_t>(ctx.r4.u64 & 0xFF)) & 0xFF;
    want = slw(srw(ones, keep & 0xFF), ctx.r5.u64);
  }
  const uint32_t diff = ((ld8(base, ctx.r3.u32) & (mask & 0xFF)) ^ (want & 0xFF)) & 0xFF;
  ctx.r3.u64 = diff == 0 ? 1 : 0;
}
NIER_VERIFY_SPEC(sub_821174A0, 0x821174A0, "r3=bptr r4=range:0..1 r5=range:0..8 r6=range:0..8");

// sub_82117520 (128 bytes) — campo num halfword.
NIER_DECOMP(sub_82117520) {
  const uint32_t lo = ctx.r5.u32, hi = ctx.r6.u32;
  uint32_t mask = 0, want = 0;
  if (lo != hi) {
    const uint64_t keep = ctx.r5.u64 - ctx.r6.u64 + 16;  // 16 − largura
    mask = slw(sraw(0xFFFF, keep), ctx.r5.u64) & 0xFFFF;
    const uint32_t ones = static_cast<uint32_t>(-static_cast<int64_t>(ctx.r4.u64 & 0xFF)) & 0xFFFF;
    want = slw(srw(ones, keep & 0xFFFF), ctx.r5.u64);
  }
  const uint32_t diff = ((ld16(base, ctx.r3.u32) & (mask & 0xFFFF)) ^ (want & 0xFFFF)) & 0xFFFF;
  ctx.r3.u64 = diff == 0 ? 1 : 0;
}
NIER_VERIFY_SPEC(sub_82117520, 0x82117520, "r3=ptr:2 r4=range:0..1 r5=range:0..16 r6=range:0..16");

// sub_82117890 (124 bytes) — campo num doubleword.
NIER_DECOMP(sub_82117890) {
  const uint32_t lo = ctx.r5.u32, hi = ctx.r6.u32;
  uint64_t mask = 0, want = 0;
  if (lo != hi) {
    const uint64_t keep = (ctx.r5.u64 - ctx.r6.u64 + 64) & 0xFFFFFFFFull;  // 64 − largura
    const uint64_t shift = ctx.r5.u64 & 0xFFFFFFFFull;
    mask = sld(srd(~0ull, keep), shift);
    const uint64_t ones = 0 - (ctx.r4.u64 & 0xFF);
    want = sld(srd(ones, keep), shift);
  }
  ctx.r3.u64 = ((ld64(base, ctx.r3.u32) & mask) ^ want) == 0 ? 1 : 0;
}
NIER_VERIFY_SPEC(sub_82117890, 0x82117890, "r3=ptr:8 r4=range:0..1 r5=range:0..64 r6=range:0..64");
