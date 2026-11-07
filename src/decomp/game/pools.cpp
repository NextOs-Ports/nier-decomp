// Pools de objetos com lista livre e lista em uso, reconstruídos do NieR.
// Pool: +8 último em uso, +12 primeiro em uso, +20 primeiro livre, +24 quantidade em uso.
// Cada nó guarda anterior/próximo/em-uso num deslocamento próprio do tipo.
#include <nier/decomp.h>
using namespace nier::guest;

namespace {
struct Link { uint32_t prev, next, used; };
// Campos do pool: último em uso, primeiro em uso, primeiro livre, quantidade.
struct PoolFields { uint32_t tail = 8, head = 12, free = 20, count = 24; };
// O compilador emitiu duas ordens para a atualização final; a ordem importa quando
// ponteiros se sobrepõem, então cada função usa a sua.
enum class CountOrder { BeforeHead, AfterHead };

// Tira o primeiro nó livre, marca em uso e o põe à frente da lista em uso.
// Sem nó livre, devolve o primeiro em uso.
uint32_t pool_take(uint8_t* base, uint32_t pool, const Link& l, const PoolFields& f = {},
                   CountOrder order = CountOrder::BeforeHead) {
  const uint32_t node = ld32(base, pool + f.free);
  if (node == 0) return ld32(base, pool + f.head);
  const uint32_t next_free = ld32(base, node + l.next);
  st32(base, pool + f.free, next_free);
  if (next_free != 0) st32(base, next_free + l.prev, 0);
  st8(base, node + l.used, 1);
  const uint32_t head = ld32(base, pool + f.head);
  st32(base, node + l.prev, 0);
  st32(base, node + l.next, head);
  const uint32_t head_now = ld32(base, pool + f.head);
  if (head_now != 0) {
    st32(base, head_now + l.prev, node);
  } else if (order == CountOrder::BeforeHead) {
    const uint32_t count = ld32(base, pool + f.count) + 1;
    st32(base, pool + f.tail, node);
    st32(base, pool + f.head, node);
    st32(base, pool + f.count, count);
    return node;
  } else {
    st32(base, pool + f.tail, node);
  }
  if (order == CountOrder::BeforeHead) {
    const uint32_t count = ld32(base, pool + f.count) + 1;
    st32(base, pool + f.head, node);
    st32(base, pool + f.count, count);
  } else {
    st32(base, pool + f.head, node);
    st32(base, pool + f.count, ld32(base, pool + f.count) + 1);
  }
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

// sub_82147860 (128 bytes) — pool dentro do objeto (campos +768..+784), nós com ligação +12/+16/+20.
NIER_DECOMP(sub_82147860) { ctx.r3.u64 = pool_take(base, ctx.r3.u32, {12, 16, 20}, {768, 772, 780, 784}); }
NIER_VERIFY_SPEC(sub_82147860, 0x82147860, "r3=ptr @r3.780=ptr @r3.772=ptr");

// sub_82147A08 (128 bytes) — pool com campos +896..+912, nós com ligação +96/+100/+104.
NIER_DECOMP(sub_82147A08) { ctx.r3.u64 = pool_take(base, ctx.r3.u32, {96, 100, 104}, {896, 900, 908, 912}); }
NIER_VERIFY_SPEC(sub_82147A08, 0x82147A08, "r3=ptr @r3.908=ptr @r3.900=ptr");

// sub_8214B4E8 (128 bytes) — pool com campos +4304..+4320, nós com ligação +4288/+4292/+4296.
NIER_DECOMP(sub_8214B4E8) { ctx.r3.u64 = pool_take(base, ctx.r3.u32, {4288, 4292, 4296}, {4304, 4308, 4316, 4320}); }
NIER_VERIFY_SPEC(sub_8214B4E8, 0x8214B4E8, "r3=ptr @r3.4316=ptr @r3.4308=ptr");

// sub_821476F8 (164 bytes) — pool de 510 nós (campos em +0x78000), contagem atualizada depois da cabeça.
NIER_DECOMP(sub_821476F8) {
  ctx.r3.u64 = pool_take(base, ctx.r3.u32, {944, 948, 952}, {0x78000, 0x78004, 0x7800C, 0x78010}, CountOrder::AfterHead);
}
NIER_VERIFY_SPEC(sub_821476F8, 0x821476F8, "r3=const:0x40000000 @r3.0x7800C=ptr @r3.0x78004=ptr");

// sub_82147B38 (164 bytes) — pool de 248 nós (campos em +0x1C520), contagem depois da cabeça.
NIER_DECOMP(sub_82147B38) {
  ctx.r3.u64 = pool_take(base, ctx.r3.u32, {448, 452, 456}, {0x1C520, 0x1C524, 0x1C52C, 0x1C530}, CountOrder::AfterHead);
}
NIER_VERIFY_SPEC(sub_82147B38, 0x82147B38, "r3=const:0x40000000 @r3.0x1C52C=ptr @r3.0x1C524=ptr");
