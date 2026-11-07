// Pequenas rotinas de vetores e intervalos, reconstruídas do NieR.
#include <nier/decomp.h>
using namespace nier::guest;

// sub_821459B8 (84 bytes) — verdadeiro se os 32 vetores (x, y, z) forem todos iguais ao primeiro.
NIER_DECOMP(sub_821459B8) {
  const uint32_t v = ctx.r3.u32;
  const float x = ldf(base, v), y = ldf(base, v + 4), z = ldf(base, v + 8);
  for (uint32_t i = 1; i < 32; ++i) {
    const uint32_t e = v + i * 12;
    if (!(x == ldf(base, e)) || !(y == ldf(base, e + 4)) || !(z == ldf(base, e + 8))) { ctx.r3.u64 = 0; return; }
  }
  ctx.r3.u64 = 1;
}
NIER_VERIFY_SPEC(sub_821459B8, 0x821459B8, "r3=ptr");

// sub_82145C38 (32 bytes) — intervalo [a, b] em +320/+324 e o inverso do comprimento em +328.
NIER_DECOMP(sub_82145C38) {
  const uint32_t self = ctx.r3.u32;
  const float length = static_cast<float>(ctx.f2.f64 - ctx.f1.f64);
  stf(base, self + 320, static_cast<float>(ctx.f1.f64));
  stf(base, self + 324, static_cast<float>(ctx.f2.f64));
  stf(base, self + 328, static_cast<float>(static_cast<double>(ldf(base, 0x820007A4)) / static_cast<double>(length)));
}
NIER_VERIFY_SPEC(sub_82145C38, 0x82145C38, "r3=ptr f1=f32 f2=f32");

// sub_82146220 (28 bytes) — item `i` (432 bytes) da tabela em +640; índice negativo dá +208.
NIER_DECOMP(sub_82146220) {
  if (static_cast<int32_t>(ctx.r4.u32) < 0) { ctx.r3.u64 = ctx.r3.u64 + 208; return; }
  ctx.r3.u64 = static_cast<uint64_t>(ld32(base, ctx.r3.u32 + 640)) + ctx.r4.u64 * 432;
}
NIER_VERIFY_SPEC(sub_82146220, 0x82146220, "r3=ptr r4=range:0xFFFFFFFE..20");

// sub_821461A8 (120 bytes) — pronto para agir: tem alvo (+780), valor +20 negativo
// e flag +16 ligada; então modo +936 == 1, ou com modo 0, campo +76 do alvo não nulo.
NIER_DECOMP(sub_821461A8) {
  const uint32_t self = ctx.r3.u32;
  const uint32_t target = ld32(base, self + 780);
  ctx.r3.u64 = 0;
  if (target == 0) return;
  const double value = ldf(base, self + 20);
  // bge: tomado quando não é "menor" (inclui NaN).
  const bool blocked = !(value < static_cast<double>(ldf(base, 0x820454E0))) || ld8(base, self + 16) == 0;
  if (blocked) return;
  const uint32_t mode = ld32(base, self + 936);
  if (mode == 0) { ctx.r3.u64 = ld32(base, target + 76) != 0 ? 1 : 0; return; }
  ctx.r3.u64 = mode == 1 ? 1 : 0;
}
NIER_VERIFY_SPEC(sub_821461A8, 0x821461A8, "r3=ptr @r3.780=ptr @r3.936=range:0..2");
