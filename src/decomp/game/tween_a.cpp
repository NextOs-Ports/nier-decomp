// Estado e interpolação de valores (atual → alvo em um tempo), reconstruídos do NieR.
//
// Um "canal" de interpolação ocupa 24 bytes:
//   +0 valor inicial, +4 valor anterior, +8 alvo, +12 duração, +16 tempo decorrido,
//   +20 (byte) ativo.
// Duração 0 aplica o alvo na hora (inicial e anterior recebem o alvo).
#include <nier/decomp.h>
using namespace nier::guest;

namespace {
constexpr uint32_t kZero = 0x820454E0;   // float 0.0 usado como referência
constexpr uint32_t kState = 32, kFlags = 39;

void start_tween(uint8_t* base, uint32_t ch, double target, double duration) {
  const float zero = ldf(base, kZero);
  if (duration == static_cast<double>(zero)) {
    stf(base, ch + 0, static_cast<float>(target));
    stf(base, ch + 4, static_cast<float>(target));
  } else {
    stf(base, ch + 4, ldf(base, ch + 0));
  }
  stf(base, ch + 8, static_cast<float>(target));
  stf(base, ch + 12, static_cast<float>(duration));
  stf(base, ch + 16, zero);
  st8(base, ch + 20, 1);
}
}  // namespace

// sub_82113B10 (48 bytes) — se o estado é 3 ou 4, passa a 5 e devolve 1; senão 0.
NIER_DECOMP(sub_82113B10) {
  const uint32_t self = ctx.r3.u32;
  const int32_t state = static_cast<int32_t>(ld32(base, self + kState));
  if (state == 3 || state == 4) {
    st32(base, self + kState, 5);
    ctx.r3.u64 = 1;
  } else {
    ctx.r3.u64 = 0;
  }
}
NIER_VERIFY_SPEC(sub_82113B10, 0x82113B10, "r3=ptr @r3.32=range:0..8");

// sub_82113B40 (40 bytes) — estado acima de 3.
NIER_DECOMP(sub_82113B40) {
  ctx.r3.u64 = static_cast<int32_t>(ld32(base, ctx.r3.u32 + kState)) > 3 ? 1 : 0;
}
NIER_VERIFY_SPEC(sub_82113B40, 0x82113B40, "r3=ptr @r3.32=range:0..8");

// sub_82113B70 (32 bytes) — estado entre 3 e 7.
NIER_DECOMP(sub_82113B70) {
  const int32_t state = static_cast<int32_t>(ld32(base, ctx.r3.u32 + kState));
  ctx.r3.u64 = (state >= 3 && state <= 7) ? 1 : 0;
}
NIER_VERIFY_SPEC(sub_82113B70, 0x82113B70, "r3=ptr @r3.32=range:0..9");

// sub_82113BB0 (56 bytes) — liga ou desliga o bit 7 das flags +39.
NIER_DECOMP(sub_82113BB0) {
  const uint32_t self = ctx.r3.u32;
  const uint8_t flags = ld8(base, self + kFlags);
  st8(base, self + kFlags, static_cast<int32_t>(ctx.r4.u32) != 0 ? (flags | 0x80) : (flags & 0x7F));
}
NIER_VERIFY_SPEC(sub_82113BB0, 0x82113BB0, "r3=ptr r4=range:0..1");

// sub_82113BE8 (72 bytes) — inicia a interpolação do canal em +52; devolve 1.
NIER_DECOMP(sub_82113BE8) {
  start_tween(base, ctx.r3.u32 + 52, ctx.f1.f64, ctx.f2.f64);
  ctx.r3.u64 = 1;
}
NIER_VERIFY_SPEC(sub_82113BE8, 0x82113BE8, "r3=ptr f1=f32 f2=f32:0..3");

// sub_82113C30 (28 bytes) — grava o valor +48 e liga o bit 0 das flags; devolve 1.
NIER_DECOMP(sub_82113C30) {
  const uint32_t self = ctx.r3.u32;
  stf(base, self + 48, static_cast<float>(ctx.f1.f64));
  st8(base, self + kFlags, ld8(base, self + kFlags) | 0x01);
  ctx.r3.u64 = 1;
}
NIER_VERIFY_SPEC(sub_82113C30, 0x82113C30, "r3=ptr f1=f32");

// sub_82113C60 (32 bytes) — chama o método 45 do objeto em +16 com o recurso 0x85F11EC8.
NIER_DECOMP(sub_82113C60) {
  const uint32_t obj = ld32(base, ctx.r3.u32 + 16);
  ctx.r3.u64 = obj;
  ctx.r4.u64 = 0x85F11EC8;
  call_virtual(ctx, base, obj, 45);
}
NIER_VERIFY_SPEC(sub_82113C60, 0x82113C60, "r3=ptr @r3.16=obj");

// sub_82113C80 (32 bytes) — grava o par +40/+44 e liga o bit 1 das flags.
NIER_DECOMP(sub_82113C80) {
  const uint32_t self = ctx.r3.u32;
  const uint8_t flags = ld8(base, self + kFlags);
  stf(base, self + 40, static_cast<float>(ctx.f1.f64));
  stf(base, self + 44, static_cast<float>(ctx.f2.f64));
  st8(base, self + kFlags, flags | 0x02);
}
NIER_VERIFY_SPEC(sub_82113C80, 0x82113C80, "r3=ptr f1=f32 f2=f32");

// sub_82114230 (140 bytes) — termina no estado 7 com o canal em +52 indo a 0 no tempo f1.
// Tempo negativo zera tudo na hora e liga o bit 3 das flags; senão desliga o bit 3.
NIER_DECOMP(sub_82114230) {
  const uint32_t self = ctx.r3.u32;
  const float zero = ldf(base, kZero);
  if (ctx.f1.f64 < static_cast<double>(zero)) {
    for (uint32_t off : {56u, 60u, 64u, 68u, 52u}) stf(base, self + off, zero);
    st8(base, self + 72, 1);
    st32(base, self + kState, 7);
    st8(base, self + kFlags, ld8(base, self + kFlags) | 0x08);
    return;
  }
  start_tween(base, self + 52, zero, ctx.f1.f64);
  st32(base, self + kState, 7);
  st8(base, self + kFlags, ld8(base, self + kFlags) & ~0x08);
}
NIER_VERIFY_SPEC(sub_82114230, 0x82114230, "r3=ptr f1=f32:2..2");

// sub_821149C8 (240 bytes) — inicializa um controlador com 4 canais de cor/escala.
NIER_DECOMP(sub_821149C8) {
  const uint32_t self = ctx.r3.u32;
  const float zero = ldf(base, kZero);
  const float one = ldf(base, 0x820007C0);
  const float full = ldf(base, 0x820007A4);
  st32(base, self + 8, ctx.r4.u32);
  st32(base, self + 4, 0);
  stf(base, self + 20, zero);
  st32(base, self + 12, 0);
  st32(base, self + 16, 0xFFFFFFFF);
  st32(base, self + 24, 0);
  st32(base, self + 28, 0);
  for (uint32_t off : {32u, 36u, 40u, 44u}) stf(base, self + off, zero);
  stf(base, self + 48, one);
  st8(base, self + 52, 0);
  for (uint32_t k = 0; k < 4; ++k) {
    const uint32_t ch = self + 52 + k * 24;
    stf(base, ch + 4, full);
    stf(base, ch + 8, full);
    stf(base, ch + 12, full);
    stf(base, ch + 16, zero);
    stf(base, ch + 20, zero);
    st8(base, ch + 24, 1);
  }
}
NIER_VERIFY_SPEC(sub_821149C8, 0x821149C8, "r3=ptr");

// sub_82114AB8 (64 bytes) — inicia a interpolação do canal em +32.
NIER_DECOMP(sub_82114AB8) { start_tween(base, ctx.r3.u32 + 32, ctx.f1.f64, ctx.f2.f64); }
NIER_VERIFY_SPEC(sub_82114AB8, 0x82114AB8, "r3=ptr f1=f32 f2=f32:0..3");

// sub_82114AF8 (96 bytes) — inicia a interpolação do canal `index` (+56, a cada 24 bytes),
// se 0 <= index < quantidade em +24.
NIER_DECOMP(sub_82114AF8) {
  const uint32_t self = ctx.r3.u32;
  const int32_t index = static_cast<int32_t>(ctx.r4.u32);
  if (index < 0 || static_cast<int32_t>(ld32(base, self + 24)) <= index) return;
  start_tween(base, self + 56 + static_cast<uint32_t>(index) * 24, ctx.f1.f64, ctx.f2.f64);
}
NIER_VERIFY_SPEC(sub_82114AF8, 0x82114AF8, "r3=ptr r4=range:0xFFFFFFFF..6 @r3.24=range:0..6 f1=f32 f2=f32:0..3");

// sub_82114B58 (56 bytes) — valor inicial do canal `index`, ou 0.
NIER_DECOMP(sub_82114B58) {
  const uint32_t self = ctx.r3.u32;
  const int32_t index = static_cast<int32_t>(ctx.r4.u32);
  if (index < 0 || static_cast<int32_t>(ld32(base, self + 24)) <= index) { ctx.f1.f64 = ldf(base, kZero); return; }
  ctx.f1.f64 = ldf(base, self + 56 + static_cast<uint32_t>(index) * 24);
}
NIER_VERIFY_SPEC(sub_82114B58, 0x82114B58, "r3=ptr r4=range:0xFFFFFFFF..6 @r3.24=range:0..6");

// sub_82114B90 (56 bytes) — alvo do canal `index`, ou 0.
NIER_DECOMP(sub_82114B90) {
  const uint32_t self = ctx.r3.u32;
  const int32_t index = static_cast<int32_t>(ctx.r4.u32);
  if (index < 0 || static_cast<int32_t>(ld32(base, self + 24)) <= index) { ctx.f1.f64 = ldf(base, kZero); return; }
  ctx.f1.f64 = ldf(base, self + 64 + static_cast<uint32_t>(index) * 24);
}
NIER_VERIFY_SPEC(sub_82114B90, 0x82114B90, "r3=ptr r4=range:0xFFFFFFFF..6 @r3.24=range:0..6");

// sub_82114F10 (40 bytes) — construtor: vtable 0x8200C41C e três subobjetos
// (em +44, +208 e +372, 164 bytes cada) com a vtable base 0x820D7FF0.
NIER_DECOMP(sub_82114F10) {
  const uint32_t self = ctx.r3.u32;
  st32(base, self + 0, 0x8200C41C);
  for (uint32_t off : {372u, 208u, 44u}) st32(base, self + off, 0x820D7FF0);
}
NIER_VERIFY_SPEC(sub_82114F10, 0x82114F10, "r3=ptr");
