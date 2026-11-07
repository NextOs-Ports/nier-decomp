// Cálculo de EDRAM do Xenos, reconstruído do NieR (XDK 2.0.9328).
#include <nier/decomp.h>
using namespace nier::guest;

// sub_82108EC8 (152 bytes) — tiles de EDRAM para uma superfície.
// r3 = largura, r4 = altura, r5 = formato, r6 = multiamostragem (0, 1 = 2x, 2 = 4x).
// A EDRAM é dividida em tiles de 80x16 pixels a 32 bits (5120 bytes). 2x dobra a
// altura, 4x dobra também a largura. Os formatos 21, 32 e 37 têm 64 bits por pixel.
NIER_DECOMP(sub_82108EC8) {
  uint32_t width = ctx.r3.u32, height = ctx.r4.u32;
  const uint32_t format = ctx.r5.u32 & 63;
  const int32_t msaa = static_cast<int32_t>(ctx.r6.u32);
  if (msaa >= 1) height <<= 1;
  if (msaa == 2) width <<= 1;
  const uint32_t width_tiles = (width + 79) / 80;
  const uint32_t padded_height = (height + 15) & ~15u;
  const uint32_t padded_width = width_tiles * 80;
  const uint32_t bytes_per_pixel = (format == 21 || format == 32 || format == 37) ? 8 : 4;
  ctx.r3.u64 = (padded_height * padded_width * bytes_per_pixel) / 5120;
}
NIER_VERIFY_SPEC(sub_82108EC8, 0x82108EC8, "r3=range:1..2048 r4=range:1..2048 r5=range:0..63 r6=range:0..2");
