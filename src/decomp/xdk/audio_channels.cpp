// Buffers por canal do mixer do XAudio (XDK 9328), reconstruídos nativos.
#include <nier/decomp.h>
using namespace nier::guest;

namespace {
constexpr uint32_t kZero = 0x820454E0;     // float 0.0 na imagem
constexpr uint32_t kSilence = 0x820872A8;  // float gravado na 1ª amostra de cada canal
}

// sub_824E0038 (144 bytes) — zera os buffers dos canais (até 16, quantidade em +64):
// amostra 0 = kSilence, as demais (quantidade em +72) = 0.0, e limpa a flag +80+i.
NIER_DECOMP(sub_824E0038) {
  const uint32_t self = ctx.r3.u32;
  uint32_t count = ld32(base, self + 64);
  if (count == 0) return;
  const float zero = ldf(base, kZero), first = ldf(base, kSilence);
  for (uint32_t i = 0; i < count; ++i, count = ld32(base, self + 64)) {
    if (i >= 16) continue;
    const uint32_t buf = ld32(base, self + i * 4);
    stf(base, buf, first);
    uint32_t p = buf;
    for (uint32_t k = 1; k < ld32(base, self + 72); ++k) { p += 4; stf(base, p, zero); }
    st8(base, self + 80 + i, 0);
  }
}
NIER_VERIFY_SPEC(sub_824E0038, 0x824E0038, "r3=ptr @r3.64=range:0..20 @r3.72=range:0..256 "
                 "@r3.0=ptr @r3.4=ptr @r3.8=ptr @r3.12=ptr @r3.16=ptr @r3.20=ptr @r3.24=ptr @r3.28=ptr "
                 "@r3.32=ptr @r3.36=ptr @r3.40=ptr @r3.44=ptr @r3.48=ptr @r3.52=ptr @r3.56=ptr @r3.60=ptr");

// sub_824E00C8 (188 bytes) — soma `count` amostras float de src (r4) em dst (r3), em
// blocos de 8: o original arredonda a quantidade para cima até o múltiplo de 8 e soma
// essas amostras extras também (reproduzido). Quatro das oito somas têm os operandos
// trocados no original; a soma é comutativa (só a carga de um NaN mudaria: nan=payload).
NIER_DECOMP(sub_824E00C8) {
  const uint32_t dst = ctx.r3.u32, src = ctx.r4.u32, count = ctx.r5.u32;
  if (count == 0) return;
  const uint32_t n = (((count - 1) >> 3) + 1) * 8;
  for (uint32_t i = 0; i < n; ++i)
    stf(base, dst + i * 4, static_cast<float>(static_cast<double>(ldf(base, dst + i * 4)) + static_cast<double>(ldf(base, src + i * 4))));
  ctx.r3.u64 = dst + n * 4;   // o original devolve o ponteiro avançado (efeito colateral)
}
NIER_VERIFY_SPEC(sub_824E00C8, 0x824E00C8, "r3=ptr r4=ptr r5=range:0..512 nan=payload");
