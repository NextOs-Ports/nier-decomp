// Inicializações pequenas, reconstruídas do NieR.
#include <nier/decomp.h>
using namespace nier::guest;

// sub_8213B0F0 (28 bytes) — grava (a, b, c) em +8/+12/+16 e zera +0/+4.
NIER_DECOMP(sub_8213B0F0) {
  const uint32_t self = ctx.r3.u32;
  st32(base, self + 8, ctx.r4.u32);
  st32(base, self + 12, ctx.r5.u32);
  st32(base, self + 16, ctx.r6.u32);
  st32(base, self + 0, 0);
  st32(base, self + 4, 0);
}
NIER_VERIFY_SPEC(sub_8213B0F0, 0x8213B0F0, "r3=ptr");

// sub_8213B260 (28 bytes) — construtor: vtable 0x8200E968, +8 e +12 zerados.
NIER_DECOMP(sub_8213B260) {
  const uint32_t self = ctx.r3.u32;
  st32(base, self + 8, 0);
  st32(base, self + 0, 0x8200E968);
  st32(base, self + 12, 0);
}
NIER_VERIFY_SPEC(sub_8213B260, 0x8213B260, "r3=ptr");

// sub_8213B280 (32 bytes) — tipo 42 com a descrição 0x8200E860; devolve 1.
NIER_DECOMP(sub_8213B280) {
  const uint32_t self = ctx.r3.u32;
  st32(base, self + 12, 42);
  st32(base, self + 8, 0x8200E860);
  ctx.r3.u64 = 1;
}
NIER_VERIFY_SPEC(sub_8213B280, 0x8213B280, "r3=ptr");
