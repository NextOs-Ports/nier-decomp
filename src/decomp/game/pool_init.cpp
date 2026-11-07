// Inicialização de pools com lista livre encadeada, reconstruída do NieR.
// Cada entrada guarda, no fim, o ponteiro para o próprio início e para a próxima.
#include <nier/decomp.h>
using namespace nier::guest;

// sub_821465C8 (280 bytes) — 510 entradas de 960 bytes.
NIER_DECOMP(sub_821465C8) {
  const uint32_t self = ctx.r3.u32;
  st32(base, self + 0x78000, 0);
  st32(base, self + 0x78004, 0);
  st32(base, self + 0x78010, 0);
  for (uint32_t k = 0; k < 510; ++k) {
    const uint32_t tail = self + 1908 + k * 960;
    st32(base, tail, tail - 1908);
    st32(base, tail - 4, tail + 12);
  }
  st32(base, self + 944, self + 960);
  st32(base, self + 948, 0);
  st32(base, self + 0x78008, self);
  st32(base, self + 0x77FF0, 0);
  st32(base, self + 0x77FF4, self + 0x77880);
  st32(base, self + 0x7800C, self + 0x77C40);
}
NIER_VERIFY_SPEC(sub_821465C8, 0x821465C8, "r3=const:0x40000000");

// sub_821466E0 (96 bytes) — 14 entradas de 128 bytes.
NIER_DECOMP(sub_821466E0) {
  const uint32_t self = ctx.r3.u32;
  st32(base, self + 2048, 0);
  st32(base, self + 2052, 0);
  st32(base, self + 2064, 0);
  for (uint32_t k = 0; k < 14; ++k) {
    const uint32_t tail = self + 244 + k * 128;
    st32(base, tail, tail - 244);
    st32(base, tail - 4, tail + 12);
  }
  st32(base, self + 112, self + 128);
  st32(base, self + 2056, self);
  st32(base, self + 116, 0);
  st32(base, self + 2032, 0);
  st32(base, self + 2036, self + 1792);
  st32(base, self + 2060, self + 1920);
}
NIER_VERIFY_SPEC(sub_821466E0, 0x821466E0, "r3=ptr");
