// Lista paginada com cursor e tabela de entradas (nome, valor), reconstruída do NieR.
// Campos: entradas de 8 bytes a partir de +4, quantidade +4100, cursor +4104,
// primeira visível +4112, total +24624, colunas +24656, linhas por página +24664.
#include <nier/decomp.h>
#include <cmath>
using namespace nier::guest;

namespace {
constexpr uint32_t kEntries = 4, kCount = 4100, kCursor = 4104, kTop = 4112;
constexpr uint32_t kTotal = 24624, kColumns = 24656, kRows = 24664;

// Mantém a primeira linha visível de forma que o cursor apareça na página.
void scroll_to_cursor(uint8_t* base, uint32_t self, int32_t cursor, int32_t page) {
  const int32_t top = static_cast<int32_t>(ld32(base, self + kTop));
  if (cursor < top) st32(base, self + kTop, top - page);
  else if (cursor >= top + page) st32(base, self + kTop, top + page);
}
}  // namespace

// sub_8210C970 (28 bytes) — valor da entrada `index`, ou 0 se index >= quantidade.
NIER_DECOMP(sub_8210C970) {
  const uint32_t self = ctx.r3.u32;
  const int32_t index = static_cast<int32_t>(ctx.r4.u32);
  if (index >= static_cast<int32_t>(ld32(base, self + kCount))) { ctx.r3.u64 = 0; return; }
  ctx.r3.u64 = ld32(base, self + kEntries + static_cast<uint32_t>(index) * 8);
}
NIER_VERIFY_SPEC(sub_8210C970, 0x8210C970, "r3=ptr r4=range:0..600");

// sub_8210CA00 (60 bytes) — estado do cursor: 1 lista vazia, 0 cursor válido, 2 fora da lista.
NIER_DECOMP(sub_8210CA00) {
  const uint32_t self = ctx.r3.u32;
  const int32_t count = static_cast<int32_t>(ld32(base, self + kCount));
  if (count == 0) { ctx.r3.u64 = 1; return; }
  const int32_t cursor = static_cast<int32_t>(ld32(base, self + kCursor));
  ctx.r3.u64 = (cursor >= 0 && cursor < count) ? 0 : 2;
}
NIER_VERIFY_SPEC(sub_8210CA00, 0x8210CA00, "r3=ptr @r3.4100=range:0..8 @r3.4104=range:0xFFFFFFFE..0xFFFFFFFF");

// sub_8210CC08 (148 bytes) — ordena as entradas por nome (comparação de bytes como strcmp),
// trocando a entrada i com cada j > i cujo nome seja menor. Relê a quantidade a cada passo.
NIER_DECOMP(sub_8210CC08) {
  const uint32_t self = ctx.r3.u32;
  int32_t count = static_cast<int32_t>(ld32(base, self + kCount));
  if (count - 1 <= 0) return;
  for (int32_t i = 0;; ++i) {
    const uint32_t entry_i = self + kEntries + static_cast<uint32_t>(i) * 8;
    for (int32_t j = i + 1; j < count; ++j) {
      const uint32_t entry_j = self + kEntries + static_cast<uint32_t>(j) * 8;
      uint32_t a = ld32(base, entry_i), b = ld32(base, entry_j);
      int32_t diff;
      for (;;) {
        const uint8_t ca = ld8(base, a), cb = ld8(base, b);
        diff = static_cast<int32_t>(ca) - static_cast<int32_t>(cb);
        if (ca == 0 || diff != 0) break;
        ++a; ++b;
      }
      if (diff > 0) {
        const uint64_t tmp = ld64(base, entry_j);
        st64(base, entry_j, ld64(base, entry_i));
        st64(base, entry_i, tmp);
      }
      count = static_cast<int32_t>(ld32(base, self + kCount));
    }
    if (i + 1 >= count - 1) break;
  }
}
NIER_VERIFY_SPEC(sub_8210CC08, 0x8210CC08,
                 "r3=ptr @r3.4100=range:0..12 @r3.4=bptr @r3.12=bptr @r3.20=bptr @r3.28=bptr @r3.36=bptr @r3.44=bptr @r3.52=bptr @r3.60=bptr @r3.68=bptr @r3.76=bptr @r3.84=bptr @r3.92=bptr");

// sub_8210D408 (88 bytes) — copia a escala +4136 para +24660 e calcula a posição da
// barra de rolagem: (altura da tela − altura do item * fator) / linhas.
NIER_DECOMP(sub_8210D408) {
  const uint32_t self = ctx.r3.u32;
  const int32_t rows = static_cast<int32_t>(ld32(base, self + kRows));
  stf(base, self + 24660, ldf(base, self + 4136));
  const double item = ldf(base, self + 4128);
  const float screen = static_cast<float>(static_cast<int32_t>(ld32(base, 0x827CE7A0)));
  const double factor = ldf(base, 0x820007A0);
  const float span = static_cast<float>(-std::fma(item, factor, -static_cast<double>(screen)));
  stf(base, self + 24668, static_cast<float>(static_cast<double>(span) / static_cast<double>(static_cast<float>(rows))));
}
NIER_VERIFY_SPEC(sub_8210D408, 0x8210D408, "r3=ptr @r3.24664=range:1..20");

// sub_8210D460 (88 bytes) — cursor para o item anterior, com volta ao último.
NIER_DECOMP(sub_8210D460) {
  const uint32_t self = ctx.r3.u32;
  int32_t cursor = static_cast<int32_t>(ld32(base, self + kCursor));
  const int32_t page = static_cast<int32_t>(ld32(base, self + kRows) * ld32(base, self + kColumns));
  if (cursor <= 0) cursor = static_cast<int32_t>(ld32(base, self + kTotal));
  cursor -= 1;
  st32(base, self + kCursor, static_cast<uint32_t>(cursor));
  scroll_to_cursor(base, self, cursor, page);
}
NIER_VERIFY_SPEC(sub_8210D460, 0x8210D460, "r3=ptr @r3.4104=range:0..30 @r3.24624=range:1..30 @r3.4112=range:0..30 @r3.24656=range:1..4 @r3.24664=range:1..8");

// sub_8210D568 (84 bytes) — cursor uma linha acima (cursor − colunas), sem passar do início.
NIER_DECOMP(sub_8210D568) {
  const uint32_t self = ctx.r3.u32;
  const uint32_t columns = ld32(base, self + kColumns);
  const int32_t cursor = static_cast<int32_t>(ld32(base, self + kCursor) - columns);
  if (cursor < 0) return;
  const int32_t page = static_cast<int32_t>(ld32(base, self + kRows) * columns);
  st32(base, self + kCursor, static_cast<uint32_t>(cursor));
  scroll_to_cursor(base, self, cursor, page);
}
NIER_VERIFY_SPEC(sub_8210D568, 0x8210D568, "r3=ptr @r3.4104=range:0..30 @r3.4112=range:0..30 @r3.24656=range:1..4 @r3.24664=range:1..8");
