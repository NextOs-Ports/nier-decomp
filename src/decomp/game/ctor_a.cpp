// Construtores e cópias de pequenos objetos, reconstruídos do NieR.
#include <nier/decomp.h>
using namespace nier::guest;

// sub_82107050 (36 bytes) — construtor: vtable 0x820D7FF0; zera +4, +28, +32 e +36.
NIER_DECOMP(sub_82107050) {
  const uint32_t self = ctx.r3.u32;
  st32(base, self + 4, 0);
  st32(base, self + 0, 0x820D7FF0);
  st32(base, self + 28, 0);
  st32(base, self + 32, 0);
  st32(base, self + 36, 0);
}
NIER_VERIFY_SPEC(sub_82107050, 0x82107050, "r3=ptr");

// sub_821070A8 (72 bytes) — construtor: vtable 0x8200BE08; escala +16 com o padrão
// em 0x820454E0; zera +4, +8, +12, +20, +24 e +28.
NIER_DECOMP(sub_821070A8) {
  const uint32_t self = ctx.r3.u32;
  st32(base, self + 4, 0);
  st32(base, self + 0, 0x8200BE08);
  stf(base, self + 16, ldf(base, 0x820454E0));
  for (uint32_t off : {8u, 12u, 20u, 24u, 28u}) st32(base, self + off, 0);
}
NIER_VERIFY_SPEC(sub_821070A8, 0x821070A8, "r3=ptr");

// sub_821070F0 (80 bytes) — operador de atribuição: copia os campos +4..+36 de r4
// para r3, mantendo a vtable. O campo +16 é float.
NIER_DECOMP(sub_821070F0) {
  const uint32_t self = ctx.r3.u32, other = ctx.r4.u32;
  for (uint32_t off = 4; off <= 36; off += 4) {
    if (off == 16) stf(base, self + off, ldf(base, other + off));
    else st32(base, self + off, ld32(base, other + off));
  }
}
NIER_VERIFY_SPEC(sub_821070F0, 0x821070F0, "r3=ptr r4=ptr");

// sub_82107140 (36 bytes) — valor normalizado: inteiro +12 dividido pela escala float +16.
NIER_DECOMP(sub_82107140) {
  const uint32_t self = ctx.r3.u32;
  const float value = static_cast<float>(static_cast<int32_t>(ld32(base, self + 12)));
  ctx.f1.f64 = static_cast<float>(static_cast<double>(value) / static_cast<double>(ldf(base, self + 16)));
}
NIER_VERIFY_SPEC(sub_82107140, 0x82107140, "r3=ptr");

// sub_82107168 (28 bytes) — inteiro +12 como float.
NIER_DECOMP(sub_82107168) {
  ctx.f1.f64 = static_cast<float>(static_cast<int32_t>(ld32(base, ctx.r3.u32 + 12)));
}
NIER_VERIFY_SPEC(sub_82107168, 0x82107168, "r3=ptr");

// sub_82108DF8 (36 bytes) — copia 6 palavras de r4+28 para r3.
NIER_DECOMP(sub_82108DF8) {
  for (uint32_t i = 0; i < 6; ++i) st32(base, ctx.r3.u32 + i * 4, ld32(base, ctx.r4.u32 + 28 + i * 4));
}
NIER_VERIFY_SPEC(sub_82108DF8, 0x82108DF8, "r3=ptr r4=ptr");
