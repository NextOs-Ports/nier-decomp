// Consultas por máscara de 4 entradas (bits 0..3), reconstruídas do NieR.
// Duas tabelas iguais em layout: floats a cada 24 bytes e bytes de flags a cada 24 bytes.
#include <nier/decomp.h>
using namespace nier::guest;

namespace {
constexpr uint32_t kZero = 0x820454E0;

// Valor float da primeira entrada ligada na máscara; padrão se nenhuma estiver ligada.
float first_value(uint8_t* base, uint32_t self, uint8_t mask, uint32_t table) {
  for (uint32_t i = 0; i < 4; ++i)
    if (mask & (1u << i)) return ldf(base, self + table + i * 24);
  return ldf(base, kZero);
}
// OU das flags de todas as entradas ligadas na máscara.
uint8_t merged_flags(uint8_t* base, uint32_t self, uint8_t mask, uint32_t table) {
  uint8_t flags = 0;
  for (uint32_t i = 0; i < 4; ++i)
    if (mask & (1u << i)) flags |= ld8(base, self + table + i * 24);
  return flags;
}
}  // namespace

// sub_8210EFA8 (24 bytes) — verdadeiro para os modos 0 e 2.
NIER_DECOMP(sub_8210EFA8) {
  const int32_t mode = static_cast<int32_t>(ctx.r4.u32);
  ctx.r3.u64 = (mode == 0 || mode == 2) ? 1 : 0;
}
NIER_VERIFY_SPEC(sub_8210EFA8, 0x8210EFA8, "r4=range:0..4");

// sub_8210F378 (88 bytes) — tabela 1: valor da primeira entrada da máscara.
NIER_DECOMP(sub_8210F378) { ctx.f1.f64 = first_value(base, ctx.r3.u32, static_cast<uint8_t>(ctx.r4.u32), 42540); }
NIER_VERIFY_SPEC(sub_8210F378, 0x8210F378, "r3=ptr r4=u8");

// sub_8210F3D0 (136 bytes) — tabela 1: flags combinadas das entradas da máscara.
NIER_DECOMP(sub_8210F3D0) { ctx.r3.u64 = merged_flags(base, ctx.r3.u32, static_cast<uint8_t>(ctx.r4.u32), 42560); }
NIER_VERIFY_SPEC(sub_8210F3D0, 0x8210F3D0, "r3=ptr r4=u8");

// sub_8210F588 (88 bytes) — tabela 2: valor da primeira entrada da máscara.
NIER_DECOMP(sub_8210F588) { ctx.f1.f64 = first_value(base, ctx.r3.u32, static_cast<uint8_t>(ctx.r4.u32), 42636); }
NIER_VERIFY_SPEC(sub_8210F588, 0x8210F588, "r3=ptr r4=u8");

// sub_8210F5E0 (136 bytes) — tabela 2: flags combinadas das entradas da máscara.
NIER_DECOMP(sub_8210F5E0) { ctx.r3.u64 = merged_flags(base, ctx.r3.u32, static_cast<uint8_t>(ctx.r4.u32), 42656); }
NIER_VERIFY_SPEC(sub_8210F5E0, 0x8210F5E0, "r3=ptr r4=u8");
