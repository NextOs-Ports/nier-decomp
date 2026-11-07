// Pools de objetos com lista livre e lista em uso, reconstruídos do NieR.
// Pool: +8 último em uso, +12 primeiro em uso, +20 primeiro livre, +24 quantidade em uso.
// Cada nó guarda anterior/próximo/em-uso num deslocamento próprio do tipo.
#include <nier/decomp.h>
using namespace nier::guest;

namespace {
struct Link { uint32_t prev, next, used; };

// Tira o primeiro nó livre, marca em uso e o põe à frente da lista em uso.
// Sem nó livre, devolve o primeiro em uso. A ordem de leituras e escritas é a do original.
uint32_t pool_take(uint8_t* base, uint32_t pool, const Link& l) {
  const uint32_t node = ld32(base, pool + 20);
  if (node == 0) return ld32(base, pool + 12);
  const uint32_t next_free = ld32(base, node + l.next);
  st32(base, pool + 20, next_free);
  if (next_free != 0) st32(base, next_free + l.prev, 0);
  st8(base, node + l.used, 1);
  const uint32_t head = ld32(base, pool + 12);
  st32(base, node + l.prev, 0);
  st32(base, node + l.next, head);
  const uint32_t head_now = ld32(base, pool + 12);
  if (head_now != 0) {
    st32(base, head_now + l.prev, node);
  } else {
    st32(base, pool + 8, node);
  }
  const uint32_t count = ld32(base, pool + 24) + 1;
  st32(base, pool + 12, node);
  st32(base, pool + 24, count);
  return node;
}
}  // namespace

// sub_821171A0 (128 bytes) — pool de nós de 348 bytes.
NIER_DECOMP(sub_821171A0) { ctx.r3.u64 = pool_take(base, ctx.r3.u32, {340, 344, 348}); }
NIER_VERIFY_SPEC(sub_821171A0, 0x821171A0, "r3=ptr @r3.20=ptr @r3.12=ptr");

// sub_8211DB70 (128 bytes) — pool de nós de 276 bytes.
NIER_DECOMP(sub_8211DB70) { ctx.r3.u64 = pool_take(base, ctx.r3.u32, {268, 272, 276}); }
NIER_VERIFY_SPEC(sub_8211DB70, 0x8211DB70, "r3=ptr @r3.20=ptr @r3.12=ptr");

// sub_8211F540 (128 bytes) — pool de nós de 400 bytes.
NIER_DECOMP(sub_8211F540) { ctx.r3.u64 = pool_take(base, ctx.r3.u32, {392, 396, 400}); }
NIER_VERIFY_SPEC(sub_8211F540, 0x8211F540, "r3=ptr @r3.20=ptr @r3.12=ptr");

// sub_82115370 (60 bytes) — procura na lista em uso (+28, próximo em +156) o nó com
// identificador +16 igual ao do pool em +8. Identificador negativo: nenhum.
NIER_DECOMP(sub_82115370) {
  const uint32_t self = ctx.r3.u32;
  const int32_t key = static_cast<int32_t>(ld32(base, self + 8));
  ctx.r3.u64 = 0;
  if (key < 0) return;
  for (uint32_t node = ld32(base, self + 28); node != 0;) {
    const uint32_t next = ld32(base, node + 156);
    if (static_cast<int32_t>(ld32(base, node + 16)) == key) { ctx.r3.u64 = node; return; }
    node = next;
  }
}
NIER_VERIFY_SPEC(sub_82115370, 0x82115370, "r3=ptr @r3.8=range:0..3 @r3.28=ptr");
