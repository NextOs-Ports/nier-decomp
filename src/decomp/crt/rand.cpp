// Gerador aleatório do CRT da Microsoft (rand) usado pelo jogo, reconstruído do NieR.
#include <nier/decomp.h>
#include <cmath>
using namespace nier::guest;

namespace {
constexpr uint32_t kSeed = 0x826303A0;      // semente global do rand()
constexpr uint32_t kInvRandMaxA = 0x82000730; // float: 1/RAND_MAX (usado na chance)
constexpr uint32_t kInvRandMaxB = 0x8200072C; // float: 1/RAND_MAX (usado na faixa)

// rand() do MSVC: semente = semente*214013 + 2531011; devolve os bits 16..30.
uint32_t next_rand(uint8_t* base) {
  const uint32_t seed = ld32(base, kSeed) * 214013u + 2531011u;
  st32(base, kSeed, seed);
  return (seed >> 16) & 0x7FFF;
}
}  // namespace

// sub_82100000 (88 bytes) — chance(p): verdadeiro com probabilidade p (f1 entre 0 e 1).
NIER_DECOMP(sub_82100000) {
  const float r = static_cast<float>(next_rand(base));
  const float roll = r * ldf(base, kInvRandMaxA);
  // ble: verdadeiro também quando a comparação é desordenada (NaN).
  ctx.r3.u64 = !(static_cast<double>(roll) > ctx.f1.f64) ? 1 : 0;
}
NIER_VERIFY_SPEC(sub_82100000, 0x82100000, "f1=f32:2..2");

// sub_82100058 (80 bytes) — aleatório(min=f1, max=f2): valor uniforme entre min e max.
NIER_DECOMP(sub_82100058) {
  const float span = static_cast<float>(ctx.f2.f64 - ctx.f1.f64);
  const float r = static_cast<float>(next_rand(base));
  const float scaled = r * span;
  ctx.f1.f64 = static_cast<float>(std::fma(static_cast<double>(scaled), static_cast<double>(ldf(base, kInvRandMaxB)), ctx.f1.f64));
}
NIER_VERIFY_SPEC(sub_82100058, 0x82100058, "f1=f32 f2=f32");

// sub_82141D58 (84 bytes) — aleatório(−x, +x) com uma segunda semente (0x826908A0).
NIER_DECOMP(sub_82141D58) {
  constexpr uint32_t kSeed2 = 0x826908A0;
  const double lo = -ctx.f1.f64;
  const float span = static_cast<float>(ctx.f1.f64 - lo);
  const uint32_t seed = ld32(base, kSeed2) * 214013u + 2531011u;
  st32(base, kSeed2, seed);
  const float r = static_cast<float>((seed >> 16) & 0x7FFF);
  const float scaled = r * span;
  ctx.f1.f64 = static_cast<float>(std::fma(static_cast<double>(scaled), static_cast<double>(ldf(base, kInvRandMaxB)), lo));
}
NIER_VERIFY_SPEC(sub_82141D58, 0x82141D58, "f1=f32");
