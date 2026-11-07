// Paginação de listas de menu (página acima/abaixo), reconstruída do NieR.
// A mesma lógica aparece em dois tipos de lista, com os campos em posições diferentes.
#include <nier/decomp.h>
using namespace nier::guest;

namespace {
constexpr uint32_t kDefaultScroll = 0x820454E0;  // float: valor inicial da animação de rolagem

struct ListLayout { uint32_t count, cursor, top, scroll, page; };
constexpr ListLayout kListA{4100, 4104, 4112, 4120, 20532};
constexpr ListLayout kListB{16388, 16392, 16400, 16408, 0x44034};

int32_t get(uint8_t* base, uint32_t self, uint32_t off) { return static_cast<int32_t>(ld32(base, self + off)); }

// Página acima: cursor e topo recuam uma página, sem passar do início.
void page_up(uint8_t* base, uint32_t self, const ListLayout& l) {
  const int32_t cursor = get(base, self, l.cursor);
  if (cursor == 0) return;
  int32_t target = cursor - get(base, self, l.page);
  if (target < 0) target = 0;
  st32(base, self + l.top, static_cast<uint32_t>(target));
  st32(base, self + l.cursor, static_cast<uint32_t>(target));
  stf(base, self + l.scroll, ldf(base, kDefaultScroll));
}

// Página abaixo: avança uma página; no fim da lista, vai ao último item e mostra a última página.
void page_down(uint8_t* base, uint32_t self, const ListLayout& l) {
  const int32_t count = get(base, self, l.count);
  const int32_t cursor = get(base, self, l.cursor);
  const int32_t last = count - 1;
  if (cursor == last) return;
  const int32_t page = get(base, self, l.page);
  const int32_t target = cursor + page;
  int32_t top;
  if (target >= count) {
    st32(base, self + l.cursor, static_cast<uint32_t>(last));
    top = count - page;
    if (top < 0) top = 0;
  } else {
    st32(base, self + l.cursor, static_cast<uint32_t>(target));
    const int32_t remaining = count - target;
    top = remaining > page ? target : target + (remaining - page);
  }
  st32(base, self + l.top, static_cast<uint32_t>(top));
  stf(base, self + l.scroll, ldf(base, kDefaultScroll));
}

// Estado do cursor: 1 lista vazia, 0 válido, 2 fora da lista.
uint32_t cursor_state(uint8_t* base, uint32_t self, const ListLayout& l) {
  const int32_t count = get(base, self, l.count);
  if (count == 0) return 1;
  const int32_t cursor = get(base, self, l.cursor);
  return (cursor >= 0 && cursor < count) ? 0 : 2;
}
}  // namespace

// sub_8210DF68 (56 bytes) — lista A: página acima.
NIER_DECOMP(sub_8210DF68) { page_up(base, ctx.r3.u32, kListA); }
NIER_VERIFY_SPEC(sub_8210DF68, 0x8210DF68, "r3=ptr @r3.4104=range:0..40 @r3.20532=range:1..10");

// sub_8210DFA0 (116 bytes) — lista A: página abaixo.
NIER_DECOMP(sub_8210DFA0) { page_down(base, ctx.r3.u32, kListA); }
NIER_VERIFY_SPEC(sub_8210DFA0, 0x8210DFA0, "r3=ptr @r3.4100=range:0..40 @r3.4104=range:0..40 @r3.20532=range:1..12");

// sub_8210E420 (60 bytes) — lista B: estado do cursor.
NIER_DECOMP(sub_8210E420) { ctx.r3.u64 = cursor_state(base, ctx.r3.u32, kListB); }
NIER_VERIFY_SPEC(sub_8210E420, 0x8210E420, "r3=ptr @r3.16388=range:0..8 @r3.16392=range:0xFFFFFFFE..0xFFFFFFFF");

// sub_8210E5E0 (60 bytes) — lista B: página acima.
NIER_DECOMP(sub_8210E5E0) { page_up(base, ctx.r3.u32, kListB); }
NIER_VERIFY_SPEC(sub_8210E5E0, 0x8210E5E0, "r3=const:0x40008000 @r3.16392=range:0..40 @r3.0x44034=range:1..10");

// sub_8210E620 (124 bytes) — lista B: página abaixo.
NIER_DECOMP(sub_8210E620) { page_down(base, ctx.r3.u32, kListB); }
NIER_VERIFY_SPEC(sub_8210E620, 0x8210E620, "r3=const:0x40008000 @r3.16388=range:0..40 @r3.16392=range:0..40 @r3.0x44034=range:1..12");
