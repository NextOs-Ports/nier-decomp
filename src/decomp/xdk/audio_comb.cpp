// Filtros do reverb do XAudio (XDK 9328) usados pelo NieR, reconstruídos nativos.
// Semântica de ponto flutuante igual à da tradução (oráculo do comparador): cada
// operação simples em double com arredondamento a float, fmadds/fnmsubs = fma em double
// arredondado a float. NaN sinalizante passa pelas mesmas conversões float→double, logo
// é silenciado nos dois lados do mesmo jeito. Só o sinal/carga de um NaN propagado pode
// diferir (o compilador escolhe o operando NaN que sobrevive numa multiplicação); o jogo
// nunca lê esses bits — especificação `nan=payload` no comparador.
#include <nier/decomp.h>
#include <cmath>
using namespace nier::guest;

namespace {
inline double rf(double v) { return static_cast<double>(static_cast<float>(v)); }
inline double fmadds(double a, double b, double c) { return rf(std::fma(a, b, c)); }
inline double fnmsubs(double a, double b, double c) { return rf(-std::fma(a, b, -c)); }   // -(a*b - c)
}

// sub_824F5E40 (172 bytes) — um passo do filtro comb com realimentação (reverb):
// lê a amostra atrasada do anel (+4, máscara +20, atraso +12), atualiza o estado do
// passa-baixas (+152/+156, coeficientes +116/+120/+124) e do passa-tudo de 2ª ordem
// (+84/+88, coeficientes +52/+56), escreve ganho*(saída) + entrada (f1) na posição atual.
// Devolve em f1 a amostra atrasada lida (efeito colateral do original).
NIER_DECOMP(sub_824F5E40) {
  const uint32_t s = ctx.r3.u32;
  const uint32_t index = ld32(base, s + 16), mask = ld32(base, s + 20), ring = ld32(base, s + 4);
  const uint32_t read_pos = ((index - ld32(base, s + 12)) + 1) & mask;
  const double input = ctx.f1.f64;
  const double lp_prev2 = ldf(base, s + 156), lp_prev1 = ldf(base, s + 152);
  const double lp_b0 = ldf(base, s + 116), lp_b1 = ldf(base, s + 120), lp_b2 = ldf(base, s + 124);
  const double ap_prev1 = ldf(base, s + 88), ap_prev0 = ldf(base, s + 84);
  const double ap_a2 = ldf(base, s + 56), ap_a1 = ldf(base, s + 52);
  const double gain = ldf(base, s + 40);
  const double delayed = ldf(base, ring + read_pos * 4);
  const double acc0 = rf(lp_b2 * lp_prev2);
  const double acc1 = fmadds(lp_b0, delayed, acc0);
  stf(base, s + 156, static_cast<float>(lp_prev1));
  stf(base, s + 152, static_cast<float>(delayed));
  stf(base, s + 88, static_cast<float>(ap_prev0));
  const double acc2 = fmadds(lp_b1, lp_prev1, acc1);
  const double acc3 = fnmsubs(ap_a2, ap_prev1, acc2);
  const double out = fnmsubs(ap_a1, ap_prev0, acc3);
  stf(base, s + 84, static_cast<float>(out));
  const double scaled = rf(rf(gain * ldf(base, s + 28)) * out);
  const uint32_t write_pos = (index + 1) & mask;
  st32(base, s + 16, write_pos);
  stf(base, ring + write_pos * 4, static_cast<float>(rf(scaled + input)));
  ctx.f1.f64 = delayed;
}
NIER_VERIFY_SPEC(sub_824F5E40, 0x824F5E40, "r3=ptr @r3.4=ptr @r3.12=range:0..1023 @r3.20=range:0..1023 f1=f32 nan=payload");
