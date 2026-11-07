// Pequenas funções de dados, reconstruídas do NieR.
#include <nier/decomp.h>
using namespace nier::guest;

// sub_82103638 (24 bytes) — init(self, value): +4 = value, +0 = 0; devolve 1.
NIER_DECOMP(sub_82103638) {
  const uint32_t self = ctx.r3.u32;
  st32(base, self + 4, ctx.r4.u32);
  st32(base, self + 0, 0);
  ctx.r3.u64 = 1;
}
NIER_VERIFY_SPEC(sub_82103638, 0x82103638, "r3=ptr");

// sub_82103BF8 (68 bytes) — preenche uma tabela de 9 limites padrão.
NIER_DECOMP(sub_82103BF8) {
  static constexpr uint32_t kDefaults[9] = {64, 16, 32, 8192, 4096, 128, 256, 32, 128};
  const uint32_t self = ctx.r3.u32;
  for (uint32_t i = 0; i < 9; ++i) st32(base, self + i * 4, kDefaults[i]);
}
NIER_VERIFY_SPEC(sub_82103BF8, 0x82103BF8, "r3=ptr");

// sub_82104480 (68 bytes) — guarda o registro atual (9 palavras em +4224) no histórico
// de 8 posições em +4260 (36 bytes cada), na posição do contador +4548, e avança o contador.
NIER_DECOMP(sub_82104480) {
  const uint32_t self = ctx.r3.u32;
  const int32_t slot = static_cast<int32_t>(ld32(base, self + 4548));
  if (slot >= 0 && slot < 8) {
    const uint32_t dst = self + 4260 + static_cast<uint32_t>(slot) * 36;
    for (uint32_t i = 0; i < 9; ++i) st32(base, dst + i * 4, ld32(base, self + 4224 + i * 4));
  }
  st32(base, self + 4548, ld32(base, self + 4548) + 1);
}
NIER_VERIFY_SPEC(sub_82104480, 0x82104480, "r3=ptr @r3.4548=range:0..9");
