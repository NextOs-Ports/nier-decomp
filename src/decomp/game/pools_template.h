// Template de pool do NieR (lista livre + lista em uso), compartilhado pelas instâncias.
#pragma once
#include <nier/decomp.h>

namespace nier::pools {
using namespace nier::guest;
struct Link { uint32_t prev, next, used; };
// Campos do pool: último em uso, primeiro em uso, primeiro livre, quantidade.
struct PoolFields { uint32_t tail = 8, head = 12, free = 20, count = 24; };
// O compilador emitiu duas ordens para a atualização final; a ordem importa quando
// ponteiros se sobrepõem, então cada função usa a sua.
enum class CountOrder { BeforeHead, AfterHead };

// Tira o primeiro nó livre, marca em uso e o põe à frente da lista em uso.
// Sem nó livre, devolve o primeiro em uso.
inline uint32_t pool_take(uint8_t* base, uint32_t pool, const Link& l, const PoolFields& f = {},
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

// Monta a lista em uso a partir de um array de n nós (n >= 1): o primeiro vira o
// último em uso e o último vira o primeiro, com as ligações a/b de cada nó.
inline void build_used(uint8_t* base, uint32_t pool, uint32_t arr, int32_t n, uint32_t link_a, uint32_t link_b,
                const PoolFields& f) {
  const uint32_t first = ld32(base, arr);
  st32(base, pool + f.tail, first);
  st32(base, first + link_b, 0);
  if (n <= 1) {
    st32(base, ld32(base, pool + f.tail) + link_a, 0);
    st32(base, pool + f.head, ld32(base, pool + f.tail));
    return;
  }
  st32(base, first + link_a, ld32(base, arr + 4));
  for (int32_t k = 1; k + 1 < n; ++k) {
    const uint32_t node = ld32(base, arr + static_cast<uint32_t>(k) * 4);
    st32(base, node + link_b, ld32(base, arr + static_cast<uint32_t>(k - 1) * 4));
    st32(base, node + link_a, ld32(base, arr + static_cast<uint32_t>(k + 1) * 4));
  }
  const uint32_t last = ld32(base, arr + static_cast<uint32_t>(n - 1) * 4);
  const uint32_t before_last = ld32(base, arr + static_cast<uint32_t>(n - 2) * 4);
  st32(base, last + link_a, 0);
  st32(base, last + link_b, before_last);
  st32(base, pool + f.head, last);
  st32(base, pool + f.count, static_cast<uint32_t>(n));
}

// Devolve o nó *pnode à lista livre: tira da lista em uso (atualizando cabeça/cauda),
// avança *pnode para o anterior, limpa "em uso", empilha na lista livre e decrementa.
inline void pool_release(uint8_t* base, uint32_t pool, uint32_t pnode, const Link& l, const PoolFields& f = {}) {
  const uint32_t node = ld32(base, pnode);
  if (node == 0 || ld8(base, node + l.used) == 0) return;
  const uint32_t prev = ld32(base, node + l.prev);
  st32(base, pnode, prev);
  const uint32_t next = ld32(base, node + l.next);
  bool fix_head;
  if (next != 0) {
    st32(base, next + l.prev, prev);
    fix_head = prev == 0;
    if (!fix_head) st32(base, prev + l.next, next);
  } else {
    st32(base, pool + f.tail, prev);
    if (prev == 0) {
      fix_head = true;
    } else {
      st32(base, prev + l.next, 0);
      st32(base, prev + l.next, next);
      fix_head = false;
    }
  }
  if (fix_head) {
    st32(base, pool + f.head, next);
    if (next != 0) st32(base, next + l.prev, 0);
  }
  if (ld8(base, node + l.used) != 0) {
    st8(base, node + l.used, 0);
    const uint32_t free_head = ld32(base, pool + f.free);
    if (free_head == 0) {
      st32(base, node + l.next, 0);
    } else {
      st32(base, free_head + l.prev, node);
      st32(base, node + l.next, ld32(base, pool + f.free));
    }
    st32(base, pool + f.free, node);
    st32(base, node + l.prev, 0);
  }
  st32(base, pool + f.count, ld32(base, pool + f.count) - 1);
}
}  // namespace nier::pools
using namespace nier::pools;
using namespace nier::guest;
