// Tabela de glifos e recorte de textura de um caractere (quentes no perfil do X1:
// sub_82142130 e as duas buscas sub_821418C0/sub_82141930), reconstruídas nativas.
#include <nier/decomp.h>
#include <cmath>
using namespace nier::guest;

DECLARE_REX_FUNC(sub_82592A40);   // largura/avanço a partir da entrada de 9 palavras
DECLARE_REX_FUNC(sub_82592A60);   // altura a partir da entrada de 9 palavras
DECLARE_REX_FUNC(sub_821418C0);
DECLARE_REX_FUNC(sub_82141930);

namespace {
// Índice da entrada de 36 bytes na tabela: ((byte1 << 9) + int16(baixo) + 15386) * 9 palavras.
// Copia a entrada para a pilha (como o original) e chama `measure` com ela.
inline void glyph_entry_measure(PPCContext& ctx, uint8_t* base, PPCFunc* measure) {
  const uint32_t table = ctx.r3.u32, key = ctx.r4.u32;
  GuestFrame f(ctx, base, 128);
  st32(base, f.slot(156), key);   // o original guarda o argumento na área do chamador
  const uint32_t idx = ((((key >> 16) & 0xFF) << 9) + static_cast<uint32_t>(static_cast<int16_t>(key & 0xFFFF)) + 15386u);
  const uint32_t src = table + idx * 9 * 4;
  const uint32_t dst = f.slot(80);
  for (uint32_t k = 0; k < 36; k += 4) st32(base, dst + k, ld32(base, src + k));
  ctx.r3.u64 = dst;
  measure(ctx, base);
}
inline double rf(double v) { return static_cast<double>(static_cast<float>(v)); }
inline double i2f(uint32_t v) { return rf(static_cast<double>(static_cast<int32_t>(v))); }
constexpr uint32_t kGlyphTable = 0x84694D00;   // tabela global de glifos
constexpr uint32_t kEmptyRect = 0x82690890;    // recorte vazio (4 palavras)
constexpr uint32_t kOne = 0x820007A4, kZero = 0x820007A0, kNegOne = 0x820007C0;
}

// sub_821418C0 (112 bytes) — avanço do glifo `r4` na tabela `r3`.
NIER_DECOMP(sub_821418C0) { glyph_entry_measure(ctx, base, sub_82592A40); }
NIER_VERIFY_SPEC(sub_821418C0, 0x821418C0, "r3=const:0x40070000 r4=range:0xC000..0xC3FF");
// sub_82141930 (112 bytes) — altura do glifo `r4` na tabela `r3`.
NIER_DECOMP(sub_82141930) { glyph_entry_measure(ctx, base, sub_82592A60); }
NIER_VERIFY_SPEC(sub_82141930, 0x82141930, "r3=const:0x40070000 r4=range:0xC000..0xC3FF");

// sub_82142130 (772 bytes) — recorte (u0, v0, u1, v1) de um caractere na folha de fonte:
// célula = código % colunas (+ deslocamento vertical opcional), escala pelo avanço/altura do
// glifo, fração da célula com ajuste de linha (floor/ceil) e espelhamento pelos bits 0x40/0x20.
NIER_DECOMP(sub_82142130) {
  const uint32_t out = ctx.r3.u32, obj = ctx.r4.u32, ch = ctx.r5.u32;
  if (ld8(base, obj + 22) == 0) {
    for (uint32_t k = 0; k < 16; k += 4) st32(base, out + k, ld32(base, kEmptyRect + k));
    return;
  }
  GuestFrame f(ctx, base, 176);
  const uint32_t code = ld16(base, ch + 128);
  const uint32_t font = ld32(base, obj + 40);
  const uint32_t mask = code * 2 - 1;
  const uint32_t cols = ld32(base, font + 48) + 1;
  const uint32_t layered = ld32(base, font + 60);
  if (cols == 0) __builtin_trap();                       // twllei: divisão por zero
  if ((cols & ~mask) == 0xFFFFFFFFu) __builtin_trap();  // twlgei: estouro da divisão
  uint32_t cell = code - static_cast<uint32_t>(static_cast<int32_t>(code) / static_cast<int32_t>(cols)) * cols;
  if (static_cast<int32_t>(layered) == 0)
    cell += fctiwz(rf(static_cast<double>(ldf(base, ch + 96)) / static_cast<double>(ldf(base, font + 44))));
  // medidas do glifo
  const uint32_t key = ld32(base, ch + 116);
  st32(base, f.slot(80), key);
  ctx.r4.u64 = key; ctx.r3.s64 = static_cast<int32_t>(kGlyphTable);
  sub_821418C0(ctx, base);
  const uint32_t advance = ctx.r3.u32;
  st32(base, f.slot(80), key);
  ctx.r4.u64 = key; ctx.r3.s64 = static_cast<int32_t>(kGlyphTable);
  sub_82141930(ctx, base);
  const uint32_t height = ctx.r3.u32;
  const double cell_f = i2f(cell), adv_f = i2f(advance), hgt_f = i2f(height);
  const double one = ldf(base, kOne), zero = ldf(base, kZero), neg_one = ldf(base, kNegOne);
  const double col_scaled = rf(std::fma(static_cast<double>(ldf(base, font + 36)), cell_f, static_cast<double>(ldf(base, font + 20))));
  const double row_scaled = rf(std::fma(static_cast<double>(ldf(base, font + 40)), cell_f, static_cast<double>(ldf(base, font + 24))));
  ctx.f1.f64 = row_scaled;
  double u = rf(rf(col_scaled + one) / adv_f);
  double v = rf(rf(row_scaled + one) / hgt_f);
  st32(base, f.slot(96), 0); stf(base, f.slot(96), static_cast<float>(u));
  stf(base, f.slot(100), static_cast<float>(v));
  auto wrap = [&](double& x, uint32_t slot) {
    if (x > zero) x = rf(x - i2f(fctiwz(x)));
    else if (x < neg_one) x = rf(x - i2f(fctiwz(x) - 1));
    else return;
    stf(base, f.slot(slot), static_cast<float>(x));
  };
  wrap(u, 96);
  wrap(v, 100);
  const uint32_t font2 = ld32(base, obj + 40);
  uint32_t cw = fctiwz(ldf(base, font2 + 28)), chh = fctiwz(ldf(base, font2 + 32));
  if (static_cast<int32_t>(cw) <= 0) cw = advance;
  if (static_cast<int32_t>(chh) <= 0) chh = height;
  const uint8_t flags = ld8(base, ch + 130);
  const double du = rf(i2f(cw - 2) / adv_f), dv = rf(i2f(chh - 2) / hgt_f);
  const double u1 = rf(du + u), v1 = rf(dv + v);
  stf(base, f.slot(104), static_cast<float>(u1));
  stf(base, f.slot(108), static_cast<float>(v1));
  if (flags & 0x40) { stf(base, f.slot(96), static_cast<float>(u1)); stf(base, f.slot(104), static_cast<float>(u)); }
  if (flags & 0x20) { stf(base, f.slot(100), static_cast<float>(v1)); stf(base, f.slot(108), static_cast<float>(v)); }
  for (uint32_t k = 0; k < 16; k += 4) st32(base, out + k, ld32(base, f.slot(96) + k));
  ctx.r3.u64 = out;
}
NIER_VERIFY_SPEC(sub_82142130, 0x82142130,
                 "r3=ptr r4=ptr r5=ptr @r4.20=range:0..0xFFFF @r4.40=ptr @r4.40.48=range:0..8 @r4.40.60=range:0..1 "
                 "@r5.128=range:0..0xFFFFFFFF @r5.116=range:0xC000..0xC3FF nan=payload");
