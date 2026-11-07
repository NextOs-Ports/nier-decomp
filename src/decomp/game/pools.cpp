// Pools de objetos com lista livre e lista em uso, reconstruídos do NieR.
// Pool: +8 último em uso, +12 primeiro em uso, +20 primeiro livre, +24 quantidade em uso.
// Cada nó guarda anterior/próximo/em-uso num deslocamento próprio do tipo.
#include "pools_template.h"


// sub_821171A0 (128 bytes) — pool de nós de 348 bytes.
NIER_DECOMP(sub_821171A0) { ctx.r3.u64 = pool_take(base, ctx.r3.u32, {340, 344, 348}); }
NIER_VERIFY_SPEC(sub_821171A0, 0x821171A0, "r3=ptr @r3.20=ptr @r3.12=ptr");

// sub_8211DB70 (128 bytes) — pool de nós de 276 bytes.
NIER_DECOMP(sub_8211DB70) { ctx.r3.u64 = pool_take(base, ctx.r3.u32, {268, 272, 276}); }
NIER_VERIFY_SPEC(sub_8211DB70, 0x8211DB70, "r3=ptr @r3.20=ptr @r3.12=ptr");

// sub_8211F540 (128 bytes) — pool de nós de 400 bytes.
NIER_DECOMP(sub_8211F540) { ctx.r3.u64 = pool_take(base, ctx.r3.u32, {392, 396, 400}); }
NIER_VERIFY_SPEC(sub_8211F540, 0x8211F540, "r3=ptr @r3.20=ptr @r3.12=ptr");

// sub_82115370 (60 bytes) — procura na lista em uso (+28, próximo em +156) o nó com
// identificador +16 igual ao do pool em +8. Identificador negativo: nenhum.
NIER_DECOMP(sub_82115370) {
  const uint32_t self = ctx.r3.u32;
  const int32_t key = static_cast<int32_t>(ld32(base, self + 8));
  ctx.r3.u64 = 0;
  if (key < 0) return;
  for (uint32_t node = ld32(base, self + 28); node != 0;) {
    const uint32_t next = ld32(base, node + 156);
    if (static_cast<int32_t>(ld32(base, node + 16)) == key) { ctx.r3.u64 = node; return; }
    node = next;
  }
}
NIER_VERIFY_SPEC(sub_82115370, 0x82115370, "r3=ptr @r3.8=range:0..3 @r3.28=ptr");

// sub_82147860 (128 bytes) — pool dentro do objeto (campos +768..+784), nós com ligação +12/+16/+20.
NIER_DECOMP(sub_82147860) { ctx.r3.u64 = pool_take(base, ctx.r3.u32, {12, 16, 20}, {768, 772, 780, 784}); }
NIER_VERIFY_SPEC(sub_82147860, 0x82147860, "r3=ptr @r3.780=ptr @r3.772=ptr");

// sub_82147A08 (128 bytes) — pool com campos +896..+912, nós com ligação +96/+100/+104.
NIER_DECOMP(sub_82147A08) { ctx.r3.u64 = pool_take(base, ctx.r3.u32, {96, 100, 104}, {896, 900, 908, 912}); }
NIER_VERIFY_SPEC(sub_82147A08, 0x82147A08, "r3=ptr @r3.908=ptr @r3.900=ptr");

// sub_8214B4E8 (128 bytes) — pool com campos +4304..+4320, nós com ligação +4288/+4292/+4296.
NIER_DECOMP(sub_8214B4E8) { ctx.r3.u64 = pool_take(base, ctx.r3.u32, {4288, 4292, 4296}, {4304, 4308, 4316, 4320}); }
NIER_VERIFY_SPEC(sub_8214B4E8, 0x8214B4E8, "r3=ptr @r3.4316=ptr @r3.4308=ptr");

// sub_821476F8 (164 bytes) — pool de 510 nós (campos em +0x78000), contagem atualizada depois da cabeça.
NIER_DECOMP(sub_821476F8) {
  ctx.r3.u64 = pool_take(base, ctx.r3.u32, {944, 948, 952}, {0x78000, 0x78004, 0x7800C, 0x78010}, CountOrder::AfterHead);
}
NIER_VERIFY_SPEC(sub_821476F8, 0x821476F8, "r3=const:0x40000000 @r3.0x7800C=ptr @r3.0x78004=ptr");

// sub_82147B38 (164 bytes) — pool de 248 nós (campos em +0x1C520), contagem depois da cabeça.
NIER_DECOMP(sub_82147B38) {
  ctx.r3.u64 = pool_take(base, ctx.r3.u32, {448, 452, 456}, {0x1C520, 0x1C524, 0x1C52C, 0x1C530}, CountOrder::AfterHead);
}
NIER_VERIFY_SPEC(sub_82147B38, 0x82147B38, "r3=const:0x40000000 @r3.0x1C52C=ptr @r3.0x1C524=ptr");

// Instâncias do template de pool: montar a lista em uso a partir de um array.
// sub_8214B350 (156 bytes) — ligações +4288/+4292, campos +4304/+4308/+4320.
NIER_DECOMP(sub_8214B350) { build_used(base, ctx.r3.u32, ctx.r4.u32, static_cast<int32_t>(ctx.r5.u32), 4288, 4292, {4304, 4308, 0, 4320}); }
NIER_VERIFY_SPEC(sub_8214B350, 0x8214B350, "r3=ptr r4=ptr r5=range:1..6 @r4.0=ptr @r4.4=ptr @r4.8=ptr @r4.12=ptr @r4.16=ptr @r4.20=ptr");
// sub_82153758 (156 bytes) — ligações +144/+148, campos +5120/+5124/+5136.
NIER_DECOMP(sub_82153758) { build_used(base, ctx.r3.u32, ctx.r4.u32, static_cast<int32_t>(ctx.r5.u32), 144, 148, {5120, 5124, 0, 5136}); }
NIER_VERIFY_SPEC(sub_82153758, 0x82153758, "r3=ptr r4=ptr r5=range:1..6 @r4.0=ptr @r4.4=ptr @r4.8=ptr @r4.12=ptr @r4.16=ptr @r4.20=ptr");
// sub_821548F8 (156 bytes) — ligações +144/+148, campos +20480/+20484/+20496.
NIER_DECOMP(sub_821548F8) { build_used(base, ctx.r3.u32, ctx.r4.u32, static_cast<int32_t>(ctx.r5.u32), 144, 148, {20480, 20484, 0, 20496}); }
NIER_VERIFY_SPEC(sub_821548F8, 0x821548F8, "r3=ptr r4=ptr r5=range:1..6 @r4.0=ptr @r4.4=ptr @r4.8=ptr @r4.12=ptr @r4.16=ptr @r4.20=ptr");
// sub_82156D88 (156 bytes) — ligações +176/+180, campos +192/+196/+208.
NIER_DECOMP(sub_82156D88) { build_used(base, ctx.r3.u32, ctx.r4.u32, static_cast<int32_t>(ctx.r5.u32), 176, 180, {192, 196, 0, 208}); }
NIER_VERIFY_SPEC(sub_82156D88, 0x82156D88, "r3=ptr r4=ptr r5=range:1..6 @r4.0=ptr @r4.4=ptr @r4.8=ptr @r4.12=ptr @r4.16=ptr @r4.20=ptr");
// sub_82158440 (156 bytes) — ligações +176/+180, campos +768/+772/+784.
NIER_DECOMP(sub_82158440) { build_used(base, ctx.r3.u32, ctx.r4.u32, static_cast<int32_t>(ctx.r5.u32), 176, 180, {768, 772, 0, 784}); }
NIER_VERIFY_SPEC(sub_82158440, 0x82158440, "r3=ptr r4=ptr r5=range:1..6 @r4.0=ptr @r4.4=ptr @r4.8=ptr @r4.12=ptr @r4.16=ptr @r4.20=ptr");

// Instâncias do template de pool: tirar um nó livre.
// sub_821538D0 (128 bytes)
NIER_DECOMP(sub_821538D0) { ctx.r3.u64 = pool_take(base, ctx.r3.u32, {144, 148, 152}, {5120, 5124, 5132, 5136}); }
NIER_VERIFY_SPEC(sub_821538D0, 0x821538D0, "r3=ptr @r3.5132=ptr @r3.5124=ptr");
// sub_82154A70 (128 bytes)
NIER_DECOMP(sub_82154A70) { ctx.r3.u64 = pool_take(base, ctx.r3.u32, {144, 148, 152}, {20480, 20484, 20492, 20496}); }
NIER_VERIFY_SPEC(sub_82154A70, 0x82154A70, "r3=ptr @r3.20492=ptr @r3.20484=ptr");
// sub_82155C60 (164 bytes) — contagem depois da cabeça.
NIER_DECOMP(sub_82155C60) { ctx.r3.u64 = pool_take(base, ctx.r3.u32, {144, 148, 152}, {0xC800, 0xC804, 0xC80C, 0xC810}, CountOrder::AfterHead); }
NIER_VERIFY_SPEC(sub_82155C60, 0x82155C60, "r3=ptr @r3.0xC80C=ptr @r3.0xC804=ptr");
// sub_82156F20 (128 bytes)
NIER_DECOMP(sub_82156F20) { ctx.r3.u64 = pool_take(base, ctx.r3.u32, {176, 180, 184}, {192, 196, 204, 208}); }
NIER_VERIFY_SPEC(sub_82156F20, 0x82156F20, "r3=ptr @r3.204=ptr @r3.196=ptr");
// sub_82158608 (128 bytes)
NIER_DECOMP(sub_82158608) { ctx.r3.u64 = pool_take(base, ctx.r3.u32, {176, 180, 184}, {768, 772, 780, 784}); }
NIER_VERIFY_SPEC(sub_82158608, 0x82158608, "r3=ptr @r3.780=ptr @r3.772=ptr");
