// Comparações de valores do sistema de script do NieR (tipo 1 = double, tipo 4 = inteiro).
#include <nier/decomp.h>
using namespace nier::guest;

namespace {
// Inteiro do valor (double truncado ou inteiro); false se não for numérico.
bool as_int(uint8_t* base, uint32_t v, uint32_t& out) {
  const uint32_t type = ld32(base, v + 0);
  if (type == 1) {
    const uint64_t bits = ld64(base, v + 8);
    double d; std::memcpy(&d, &bits, 8);
    out = fctiwz(d);
    return true;
  }
  if (type == 4) { out = ld32(base, v + 8); return true; }
  return false;
}
}  // namespace

// sub_82120FF0 (88 bytes) — valor >= r4 (comparação com sinal pelo idioma de carry).
NIER_DECOMP(sub_82120FF0) {
  uint32_t x;
  if (!as_int(base, ctx.r3.u32, x)) { ctx.r3.u64 = 0; return; }
  const uint64_t sign_y = (ctx.r4.u32 >> 31) & 1;
  const uint64_t sign_x = (x & 0x80000000u) ? ~0ull : 0;
  const uint64_t carry = static_cast<uint64_t>(x) >= ctx.r4.u64 ? 1 : 0;
  ctx.r3.u64 = (sign_y + sign_x + carry) & 0xFF;
}
NIER_VERIFY_SPEC(sub_82120FF0, 0x82120FF0, "r3=ptr @r3.0=range:0..5 @r3.8=int r4=int");

// sub_82121050 (76 bytes) — valor == r4 (32 bits).
NIER_DECOMP(sub_82121050) {
  uint32_t x;
  if (!as_int(base, ctx.r3.u32, x)) { ctx.r3.u64 = 0; return; }
  ctx.r3.u64 = static_cast<uint32_t>(ctx.r4.u64 - x) == 0 ? 1 : 0;
}
NIER_VERIFY_SPEC(sub_82121050, 0x82121050, "r3=ptr @r3.0=range:0..5 @r3.8=range:0..3 r4=range:0..3");

// sub_821210A0 (76 bytes) — valor != r4 (64 bits).
NIER_DECOMP(sub_821210A0) {
  uint32_t x;
  if (!as_int(base, ctx.r3.u32, x)) { ctx.r3.u64 = 0; return; }
  ctx.r3.u64 = (ctx.r4.u64 - x) != 0 ? 1 : 0;
}
NIER_VERIFY_SPEC(sub_821210A0, 0x821210A0, "r3=ptr @r3.0=range:0..5 @r3.8=range:0..3 r4=range:0..3");
