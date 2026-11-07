// Pedido assíncrono (estado +8, resultado +100..+108), reconstruído do NieR.
#include <nier/decomp.h>
using namespace nier::guest;

// sub_8213CA50 (160 bytes) — inicia um pedido se o objeto estiver livre (estado 0)
// e o identificador não for negativo: limpa campos, grava parâmetros e passa ao estado 4.
NIER_DECOMP(sub_8213CA50) {
  const uint32_t self = ctx.r3.u32;
  if (static_cast<int32_t>(ctx.r4.u32) < 0 || ld32(base, self + 8) != 0) { ctx.r3.u64 = 0; return; }
  st32(base, self + 8, 0);
  st32(base, self + 56, ctx.r4.u32);
  st32(base, self + 64, ctx.r5.u32);
  for (uint32_t off : {60u, 68u, 76u, 80u, 84u, 88u, 36u, 12u, 1372u, 16u}) st32(base, self + off, 0);
  for (uint32_t off = 113; off <= 119; ++off) st8(base, self + off, 0);
  st32(base, self + 72, ctx.r5.u32);
  st8(base, self + 5, static_cast<uint8_t>(ctx.r6.u32));
  st8(base, self + 112, static_cast<uint8_t>(ctx.r7.u32));
  st32(base, self + 8, 4);
  st32(base, self + 100, 1);
  st32(base, self + 104, 0);
  st32(base, self + 108, 0);
  ctx.r3.u64 = 1;
}
NIER_VERIFY_SPEC(sub_8213CA50, 0x8213CA50, "r3=ptr r4=range:0xFFFFFFFF..3 @r3.8=range:0..1");

// sub_8213CAF0 (48 bytes) — conclui o pedido: estado 6, resultado zerado, flag +115.
NIER_DECOMP(sub_8213CAF0) {
  const uint32_t self = ctx.r3.u32;
  st32(base, self + 100, 0);
  st8(base, self + 115, 1);
  st32(base, self + 8, 6);
  st32(base, self + 104, 0);
  st32(base, self + 108, 0);
  ctx.r3.u64 = 1;
}
NIER_VERIFY_SPEC(sub_8213CAF0, 0x8213CAF0, "r3=ptr");
