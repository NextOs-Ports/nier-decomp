// Listas duplamente ligadas intrusivas (cabeça +0, cauda +4, quantidade +8),
// reconstruídas do NieR. O nó guarda anterior/próximo num deslocamento do tipo.
// A ordem das escritas é a do original (importa quando os ponteiros se sobrepõem).
#include <nier/decomp.h>
using namespace nier::guest;

namespace {
void push_back(uint8_t* base, uint32_t list, uint32_t node, uint32_t prev, uint32_t next) {
  if (node == 0) return;
  const uint32_t tail = ld32(base, list + 4);
  if (tail == 0) {
    st32(base, list + 0, node);
    st32(base, list + 4, node);
    st32(base, node + prev, 0);
    st32(base, node + next, 0);
    st32(base, list + 8, ld32(base, list + 8) + 1);
    return;
  }
  st32(base, tail + next, node);
  st32(base, node + prev, ld32(base, list + 4));
  st32(base, node + next, 0);
  const uint32_t count = ld32(base, list + 8) + 1;
  st32(base, list + 4, node);
  st32(base, list + 8, count);
}

// Remove o nó e devolve o seguinte.
uint32_t unlink(uint8_t* base, uint32_t list, uint32_t node, uint32_t prev_off, uint32_t next_off) {
  if (node == 0) return 0;
  const uint32_t prev = ld32(base, node + prev_off);
  const uint32_t next = ld32(base, node + next_off);
  bool fix_tail;
  if (prev != 0) {
    st32(base, prev + next_off, next);
    fix_tail = next == 0;
  } else {
    st32(base, list + 0, next);
    if (next == 0) fix_tail = true;
    else { st32(base, next + prev_off, 0); fix_tail = false; }
  }
  if (fix_tail) {
    st32(base, list + 4, prev);
    if (prev != 0) st32(base, prev + next_off, 0);
  } else {
    st32(base, next + prev_off, prev);
  }
  st32(base, node + prev_off, 0);
  st32(base, node + next_off, 0);
  st32(base, list + 8, ld32(base, list + 8) - 1);
  return next;
}
}  // namespace

// sub_82146418 (96 bytes) — insere no fim (ligação em +0/+4 do nó).
NIER_DECOMP(sub_82146418) { push_back(base, ctx.r3.u32, ctx.r4.u32, 0, 4); }
NIER_VERIFY_SPEC(sub_82146418, 0x82146418, "r3=ptr r4=ptr @r3.4=ptr");

// sub_82146478 (120 bytes) — remove (ligação em +0/+4 do nó); devolve o próximo.
NIER_DECOMP(sub_82146478) { ctx.r3.u64 = unlink(base, ctx.r3.u32, ctx.r4.u32, 0, 4); }
NIER_VERIFY_SPEC(sub_82146478, 0x82146478, "r3=ptr r4=ptr");

// sub_821464F0 (96 bytes) — insere no fim (ligação em +8/+12 do nó).
NIER_DECOMP(sub_821464F0) { push_back(base, ctx.r3.u32, ctx.r4.u32, 8, 12); }
NIER_VERIFY_SPEC(sub_821464F0, 0x821464F0, "r3=ptr r4=ptr @r3.4=ptr");

// sub_82146550 (120 bytes) — remove (ligação em +8/+12 do nó); devolve o próximo.
NIER_DECOMP(sub_82146550) { ctx.r3.u64 = unlink(base, ctx.r3.u32, ctx.r4.u32, 8, 12); }
NIER_VERIFY_SPEC(sub_82146550, 0x82146550, "r3=ptr r4=ptr");
