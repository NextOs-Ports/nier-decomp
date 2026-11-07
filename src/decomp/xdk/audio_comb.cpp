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
DECLARE_REX_FUNC(sub_824F5E40);

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

// sub_824F5EF0 (860 bytes) — um passo do reverb completo: pré-atraso com ganho de entrada,
// cinco passa-tudo em cascata (anéis em +1648/+1684/+1720/+1756/+1792), nove filtros comb
// (sub_824F5E40, estados a cada 180 bytes a partir de +28) e a matriz de mistura para as
// quatro saídas (out0..out3 em r6..r9; out2 = -out1, out3 = -out0).
namespace {
struct Ring { uint32_t buf, delay, index, mask; };
inline Ring ring(const uint8_t* base, uint32_t at) {   // +0 buf, +8 delay, +12 index, +16 mask
  return {ld32(base, at + 4), ld32(base, at + 12), ld32(base, at + 16), ld32(base, at + 20)};
}
inline double mul(double a, double b) { return rf(a * b); }
inline double fmsubs(double a, double b, double c) { return rf(std::fma(a, b, -c)); }   // a*b - c
constexpr uint32_t kPreGain = 0x8276F8B8;
constexpr uint32_t kMixA = 0x820216F8;
constexpr uint32_t kMixB = 0x8200EE60;
constexpr uint32_t kMixC = 0x8201FD9C;
constexpr uint32_t kMixD = 0x8203B0E0;
constexpr uint32_t kMixE = 0x8201AD84;
constexpr uint32_t kMixF = 0x820210C0;
constexpr uint32_t kMixG = 0x82089664;
constexpr uint32_t kMixH = 0x82019C30;
}

NIER_DECOMP(sub_824F5EF0) {
  const uint32_t st = ctx.r4.u32;
  const uint32_t out0 = ctx.r6.u32, out1 = ctx.r7.u32, out2 = ctx.r8.u32, out3 = ctx.r9.u32;
  const double input = ctx.f1.f64;
  // pré-atraso (+1828): escreve entrada*ganho na posição seguinte, lê a amostra atrasada
  const Ring pre = ring(base, st + 1828);
  const double pre_gain = mul(ldf(base, st + 1860), ldf(base, kPreGain));
  const uint32_t pre_w = (pre.index + 1) & pre.mask, pre_r = (pre.index - pre.delay + 1) & pre.mask;
  const double x0 = ldf(base, pre.buf + pre_r * 4);
  st32(base, st + 1844, pre_w);
  stf(base, pre.buf + pre_w * 4, static_cast<float>(mul(pre_gain, input)));
  // passa-tudo 1 (+1648): grava coef*atrasado + x0
  const Ring r1 = ring(base, st + 1648);
  const double k1 = ldf(base, st + 1676);
  const uint32_t w1 = (r1.index + 1) & r1.mask, rd1 = (r1.index - r1.delay + 1) & r1.mask;
  const double d1 = ldf(base, r1.buf + rd1 * 4);
  st32(base, st + 1664, w1);
  stf(base, r1.buf + w1 * 4, static_cast<float>(fmadds(k1, d1, x0)));
  // passa-tudo 2 (+1684): x1 = g1*d1 - k1*x0; grava k2*d2 + x1
  const Ring r2 = ring(base, st + 1684);
  const double g1 = ldf(base, st + 1680), k2 = ldf(base, st + 1712);
  const uint32_t w2 = (r2.index + 1) & r2.mask, rd2 = (r2.index - r2.delay + 1) & r2.mask;
  const double x1 = fmsubs(g1, d1, mul(k1, x0));
  const double d2 = ldf(base, r2.buf + rd2 * 4);
  st32(base, st + 1700, w2);
  stf(base, r2.buf + w2 * 4, static_cast<float>(fmadds(k2, d2, x1)));
  // passa-tudo 3 (+1720)
  const Ring r3 = ring(base, st + 1720);
  const double g2 = ldf(base, st + 1716), k3 = ldf(base, st + 1748);
  const double x2 = fmsubs(g2, d2, mul(k2, x1));
  const uint32_t w3 = (r3.index + 1) & r3.mask, rd3 = (r3.index - r3.delay + 1) & r3.mask;
  const double d3 = ldf(base, r3.buf + rd3 * 4);
  st32(base, st + 1736, w3);
  stf(base, r3.buf + w3 * 4, static_cast<float>(fmadds(k3, d3, x2)));
  // passa-tudo 4 (+1756)
  const Ring r4 = ring(base, st + 1756);
  const double g3 = ldf(base, st + 1752), k4 = ldf(base, st + 1784);
  const uint32_t w4 = (r4.index + 1) & r4.mask, rd4 = (r4.index - r4.delay + 1) & r4.mask;
  const double x3 = fmsubs(g3, d3, mul(k3, x2));
  const double d4 = ldf(base, r4.buf + rd4 * 4);
  st32(base, st + 1772, w4);
  stf(base, r4.buf + w4 * 4, static_cast<float>(fmadds(k4, d4, x3)));
  // passa-tudo 5 (+1792)
  const Ring r5 = ring(base, st + 1792);
  const double g4 = ldf(base, st + 1788), k5 = ldf(base, st + 1820);
  const double x4 = fmsubs(g4, d4, mul(k4, x3));
  const uint32_t w5 = (r5.index + 1) & r5.mask, rd5 = (r5.index - r5.delay + 1) & r5.mask;
  const double d5 = ldf(base, r5.buf + rd5 * 4);
  st32(base, st + 1808, w5);
  stf(base, r5.buf + w5 * 4, static_cast<float>(fmadds(k5, d5, x4)));
  const double g5 = ldf(base, st + 1824);
  const double comb_in = fmsubs(g5, d5, mul(k5, x4));
  // nove combs em paralelo, mesma entrada; cada um devolve a amostra atrasada lida
  double tap[9];
  for (int k = 0; k < 9; ++k) {
    ctx.r3.u64 = st + 28 + 180 * k;
    ctx.f1.f64 = comb_in;
    sub_824F5E40(ctx, base);
    tap[k] = ctx.f1.f64;
  }
  const double t0 = tap[0], t1 = tap[1], t2 = tap[2], t3 = tap[3], t4 = tap[4], t5 = tap[5], t6 = tap[6], t7 = tap[7], t8 = tap[8];
  const double mA = ldf(base, kMixA), mB = ldf(base, kMixB), mC = ldf(base, kMixC), mD = ldf(base, kMixD);
  const double mE = ldf(base, kMixE), mF = ldf(base, kMixF), mG = ldf(base, kMixG), mH = ldf(base, kMixH);
  const double a3 = mul(t1, mA), a2 = mul(t1, mB);
  const double b6 = fmsubs(t0, mC, a3);
  const double b5 = fmadds(t6, mD, a2);
  const double b4 = rf(b6 + t2);
  const double b3 = fmadds(t8, mE, b5);
  const double b2 = fnmsubs(t3, mB, b4);
  const double b0 = fmadds(t5, mF, b3);
  const double c12 = fmadds(t5, mD, b2);
  const double c6 = fmadds(t3, mA, b0);
  const double m5 = fmadds(t7, mE, c12);
  const double m4 = fmadds(t4, mC, c6);
  const double m3 = fmadds(t6, mF, m5);
  const double m2 = fmadds(t7, mG, m4);
  const double m1 = fmadds(t8, mG, m3);
  const double c0 = fmadds(t0, mH, m2);
  const double left = fmadds(t4, mH, m1);
  stf(base, out0, static_cast<float>(left));
  const double right = rf(c0 + t2);
  stf(base, out1, static_cast<float>(right));
  stf(base, out2, static_cast<float>(-right));
  stf(base, out3, -ldf(base, out0));
  ctx.f1.f64 = m1;
  ctx.r3.s64 = static_cast<int32_t>(0x82020000u);
}
NIER_VERIFY_SPEC(sub_824F5EF0, 0x824F5EF0, "r4=ptr r6=ptr r7=ptr r8=ptr r9=ptr f1=f32 nan=payload @r4.1832=ptr @r4.1840=range:0..1023 @r4.1848=range:0..1023 @r4.1652=ptr @r4.1660=range:0..1023 @r4.1668=range:0..1023 @r4.1688=ptr @r4.1696=range:0..1023 @r4.1704=range:0..1023 @r4.1724=ptr @r4.1732=range:0..1023 @r4.1740=range:0..1023 @r4.1760=ptr @r4.1768=range:0..1023 @r4.1776=range:0..1023 @r4.1796=ptr @r4.1804=range:0..1023 @r4.1812=range:0..1023 @r4.32=ptr @r4.40=range:0..1023 @r4.48=range:0..1023 @r4.212=ptr @r4.220=range:0..1023 @r4.228=range:0..1023 @r4.392=ptr @r4.400=range:0..1023 @r4.408=range:0..1023 @r4.572=ptr @r4.580=range:0..1023 @r4.588=range:0..1023 @r4.752=ptr @r4.760=range:0..1023 @r4.768=range:0..1023 @r4.932=ptr @r4.940=range:0..1023 @r4.948=range:0..1023 @r4.1112=ptr @r4.1120=range:0..1023 @r4.1128=range:0..1023 @r4.1292=ptr @r4.1300=range:0..1023 @r4.1308=range:0..1023 @r4.1472=ptr @r4.1480=range:0..1023 @r4.1488=range:0..1023");
