// Acessores e construtores pequenos, reconstruídos do NieR.
#include <nier/decomp.h>
using namespace nier::guest;

// sub_82115740 (24 bytes) — construtor da base com vtable 0x820D7FF0; +4 = 0.
NIER_DECOMP(sub_82115740) {
  st32(base, ctx.r3.u32 + 0, 0x820D7FF0);
  st32(base, ctx.r3.u32 + 4, 0);
}
NIER_VERIFY_SPEC(sub_82115740, 0x82115740, "r3=ptr");

// sub_82115758 (24 bytes) — +4 = r4.
NIER_DECOMP(sub_82115758) { st32(base, ctx.r3.u32 + 4, ctx.r4.u32); }
NIER_VERIFY_SPEC(sub_82115758, 0x82115758, "r3=ptr");

// sub_82116390 (36 bytes) — endereço do item `index` (328 bytes, a partir de +2360),
// se index < quantidade em +23352; senão 0.
NIER_DECOMP(sub_82116390) {
  if (static_cast<int32_t>(ctx.r4.u32) >= static_cast<int32_t>(ld32(base, ctx.r3.u32 + 23352))) { ctx.r3.u64 = 0; return; }
  ctx.r3.u64 = ctx.r4.u64 * 328 + ctx.r3.u64 + 2360;
}
NIER_VERIFY_SPEC(sub_82116390, 0x82116390, "r3=ptr r4=range:0..40 @r3.23352=range:0..40");

// sub_8211C3F0 (48 bytes) — se não estiver travado (+5584), grava o par +4984/+4988 e devolve 1.
NIER_DECOMP(sub_8211C3F0) {
  const uint32_t self = ctx.r3.u32;
  if (ld8(base, self + 5584) != 0) { ctx.r3.u64 = 0; return; }
  st32(base, self + 4984, ctx.r4.u32);
  st32(base, self + 4988, ctx.r5.u32);
  ctx.r3.u64 = 1;
}
NIER_VERIFY_SPEC(sub_8211C3F0, 0x8211C3F0, "r3=ptr");

// sub_8211C420 (32 bytes) — endereço do item `index` (308 bytes, a partir de +48),
// se index < quantidade em +4976 (sem sinal); senão 0.
NIER_DECOMP(sub_8211C420) {
  if (ld32(base, ctx.r3.u32 + 4976) <= ctx.r4.u32) { ctx.r3.u64 = 0; return; }
  ctx.r3.u64 = ctx.r4.u64 * 308 + ctx.r3.u64 + 48;
}
NIER_VERIFY_SPEC(sub_8211C420, 0x8211C420, "r3=ptr r4=range:0..20 @r3.4976=range:0..20");

// sub_8211EAC8 (24 bytes) — verdadeiro se o ponteiro em +34456 não for nulo.
NIER_DECOMP(sub_8211EAC8) { ctx.r3.u64 = ld32(base, ctx.r3.u32 + 34456) != 0 ? 1 : 0; }
NIER_VERIFY_SPEC(sub_8211EAC8, 0x8211EAC8, "r3=ptr");
