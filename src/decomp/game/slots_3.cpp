// Três slots de 64 bytes (por exemplo, um por controle/usuário) e a máquina de estados
// que os acompanha, reconstruídos do NieR.
// Slot i em self + i*64: +11080 double, +11088 palavra, +11092 palavra, +11096 dados,
// +11128 flags (bit 7, bit 6), +11132 estado (1 = ativo, 2 = outro estado).
#include <nier/decomp.h>
using namespace nier::guest;

namespace {
constexpr uint32_t kSlots = 3;
inline uint64_t slot_base(const PPCContext& ctx, uint32_t index_reg_value) {
  return ctx.r3.u64 + static_cast<uint32_t>(index_reg_value << 6);
}
inline uint32_t slot_state(uint8_t* base, uint64_t slot) { return ld32(base, static_cast<uint32_t>(slot + 11132)); }
inline bool slot_active(uint8_t* base, const PPCContext& ctx, uint32_t index) {
  return index < kSlots && slot_state(base, slot_base(ctx, index)) == 1;
}
// Máquina de estados em +11264 (estado), +11268 (passo), +11272 (próximo).
void set_phase(uint8_t* base, uint32_t self, uint32_t phase) {
  st32(base, self + 11264, phase);
  st32(base, self + 11268, 0);
  st32(base, self + 11272, 3);
}
}  // namespace

// sub_82140540 (44 bytes) — slot ativo (estado 1).
NIER_DECOMP(sub_82140540) { ctx.r3.u64 = slot_active(base, ctx, ctx.r4.u32) ? 1 : 0; }
NIER_VERIFY_SPEC(sub_82140540, 0x82140540, "r3=ptr r4=range:0..4 @r3.11132=range:0..2 @r3.11196=range:0..2");

// sub_82140570 (44 bytes) — slot no estado 2.
NIER_DECOMP(sub_82140570) {
  const uint32_t i = ctx.r4.u32;
  ctx.r3.u64 = (i < kSlots && slot_state(base, slot_base(ctx, i)) == 2) ? 1 : 0;
}
NIER_VERIFY_SPEC(sub_82140570, 0x82140570, "r3=ptr r4=range:0..4 @r3.11132=range:0..2 @r3.11196=range:0..2");

// sub_821405A0 (80 bytes) — endereço dos dados do slot ativo; senão o bloco vazio 0x820008B8.
NIER_DECOMP(sub_821405A0) {
  const uint32_t i = ctx.r4.u32;
  if (!slot_active(base, ctx, i)) { ctx.r3.s64 = static_cast<int32_t>(0x820008B8u); return; }
  ctx.r3.u64 = slot_base(ctx, i) + 11096;
}
NIER_VERIFY_SPEC(sub_821405A0, 0x821405A0, "r3=ptr r4=range:0..4 @r3.11132=range:0..2 @r3.11196=range:0..2");

// sub_821405F0 (76 bytes) — palavra +11092 do slot ativo, ou 0.
NIER_DECOMP(sub_821405F0) {
  const uint32_t i = ctx.r4.u32;
  ctx.r3.u64 = slot_active(base, ctx, i) ? ld32(base, static_cast<uint32_t>(slot_base(ctx, i) + 11092)) : 0;
}
NIER_VERIFY_SPEC(sub_821405F0, 0x821405F0, "r3=ptr r4=range:0..4 @r3.11132=range:0..2 @r3.11196=range:0..2");

// sub_821406C0 (80 bytes) — double +11080 do slot ativo; senão o padrão em 0x8200CA18.
NIER_DECOMP(sub_821406C0) {
  const uint32_t i = ctx.r4.u32;
  const uint32_t addr = slot_active(base, ctx, i) ? static_cast<uint32_t>(slot_base(ctx, i) + 11080) : 0x8200CA18u;
  ctx.f1.u64 = ld64(base, addr);
}
NIER_VERIFY_SPEC(sub_821406C0, 0x821406C0, "r3=ptr r4=range:0..4 @r3.11132=range:0..2 @r3.11196=range:0..2");

// sub_82140710 (76 bytes) — palavra +11088 do slot ativo, ou 0.
NIER_DECOMP(sub_82140710) {
  const uint32_t i = ctx.r4.u32;
  ctx.r3.u64 = slot_active(base, ctx, i) ? ld32(base, static_cast<uint32_t>(slot_base(ctx, i) + 11088)) : 0;
}
NIER_VERIFY_SPEC(sub_82140710, 0x82140710, "r3=ptr r4=range:0..4 @r3.11132=range:0..2 @r3.11196=range:0..2");

// sub_82140760 (80 bytes) — bit 7 das flags do slot ativo.
NIER_DECOMP(sub_82140760) {
  const uint32_t i = ctx.r4.u32;
  ctx.r3.u64 = slot_active(base, ctx, i) ? (ld8(base, static_cast<uint32_t>(slot_base(ctx, i) + 11128)) >> 7) : 0;
}
NIER_VERIFY_SPEC(sub_82140760, 0x82140760, "r3=ptr r4=range:0..4 @r3.11132=range:0..2 @r3.11196=range:0..2");

// sub_821407B0 (80 bytes) — bit 6 das flags do slot ativo.
NIER_DECOMP(sub_821407B0) {
  const uint32_t i = ctx.r4.u32;
  ctx.r3.u64 = slot_active(base, ctx, i) ? ((ld8(base, static_cast<uint32_t>(slot_base(ctx, i) + 11128)) >> 6) & 1) : 0;
}
NIER_VERIFY_SPEC(sub_821407B0, 0x821407B0, "r3=ptr r4=range:0..4 @r3.11132=range:0..2 @r3.11196=range:0..2");

// sub_82140640 (128 bytes) — copia os 4 halfwords +11088.. do slot ativo para r3;
// slot inativo dá 8 bytes zerados.
NIER_DECOMP(sub_82140640) {
  const uint32_t out = ctx.r3.u32;
  const uint32_t i = ctx.r5.u32;
  const uint64_t slot = ctx.r4.u64 + static_cast<uint32_t>(i << 6);
  const bool active = i < kSlots && ld32(base, static_cast<uint32_t>(slot + 11132)) == 1;
  if (!active) { st64(base, out, 0); return; }
  const uint32_t src = static_cast<uint32_t>(ctx.r4.u64 + static_cast<uint32_t>((i + 173) << 6));
  for (uint32_t k = 0; k < 4; ++k) st16(base, out + k * 2, ld16(base, src + k * 2));
}
NIER_VERIFY_SPEC(sub_82140640, 0x82140640, "r3=ptr r4=ptr r5=range:0..4 @r4.11132=range:0..2 @r4.11196=range:0..2");

// sub_82140210 (72 bytes) — fase 5 com (alvo, a, b); b vira 1 quando a é 0.
NIER_DECOMP(sub_82140210) {
  const uint32_t self = ctx.r3.u32;
  st32(base, self + 11280, ctx.r4.u32);
  st8(base, self + 11288, static_cast<uint8_t>(ctx.r5.u32));
  st8(base, self + 11289, static_cast<uint8_t>(ctx.r6.u32));
  if ((ctx.r5.u32 & 0xFF) == 0) st8(base, self + 11289, 1);
  set_phase(base, self, 5);
  ctx.r3.u64 = 1;
}
NIER_VERIFY_SPEC(sub_82140210, 0x82140210, "r3=ptr r5=range:0..1");

// sub_82140258 (72 bytes) — igual, fase 6.
NIER_DECOMP(sub_82140258) {
  const uint32_t self = ctx.r3.u32;
  st32(base, self + 11280, ctx.r4.u32);
  st8(base, self + 11288, static_cast<uint8_t>(ctx.r5.u32));
  st8(base, self + 11289, static_cast<uint8_t>(ctx.r6.u32));
  if ((ctx.r5.u32 & 0xFF) == 0) st8(base, self + 11289, 1);
  set_phase(base, self, 6);
  ctx.r3.u64 = 1;
}
NIER_VERIFY_SPEC(sub_82140258, 0x82140258, "r3=ptr r5=range:0..1");

// sub_82140308 (36 bytes) — fase 7.
NIER_DECOMP(sub_82140308) { set_phase(base, ctx.r3.u32, 7); ctx.r3.u64 = 1; }
NIER_VERIFY_SPEC(sub_82140308, 0x82140308, "r3=ptr");

// sub_82140330 (40 bytes) — fase 8 com o byte b.
NIER_DECOMP(sub_82140330) {
  st8(base, ctx.r3.u32 + 11289, static_cast<uint8_t>(ctx.r4.u32));
  set_phase(base, ctx.r3.u32, 8);
  ctx.r3.u64 = 1;
}
NIER_VERIFY_SPEC(sub_82140330, 0x82140330, "r3=ptr");
