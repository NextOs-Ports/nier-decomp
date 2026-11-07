// Auxiliares do Lua 5.1 embutido no NieR, reconstruídos e verificados.
#include <nier/decomp.h>
using namespace nier::guest;

// sub_82123928 (100 bytes) — luaL_findstring(name, list): índice do nome na lista
// terminada por nulo, ou -1.
NIER_DECOMP(sub_82123928) {
  const uint32_t name = ctx.r3.u32, list = ctx.r4.u32;
  for (uint32_t i = 0;; ++i) {
    uint32_t entry = ld32(base, list + i * 4);
    if (entry == 0) break;
    uint32_t a = entry, b = name;
    int32_t diff;
    for (;;) {
      const uint8_t ca = ld8(base, a), cb = ld8(base, b);
      diff = static_cast<int32_t>(ca) - static_cast<int32_t>(cb);
      if (ca == 0 || diff != 0) break;
      ++a; ++b;
    }
    if (diff == 0) { ctx.r3.u64 = i; return; }
  }
  ctx.r3.u64 = ~0ull;
}
NIER_VERIFY_SPEC(sub_82123928, 0x82123928, "r3=bptr r4=ptr @r4.0=bptr @r4.4=bptr @r4.8=bptr @r4.12=const:0");

// sub_82124390 (24 bytes) — inicia um buffer: +8 = dono, +0 = área interna (+12), +4 = 0.
NIER_DECOMP(sub_82124390) {
  const uint32_t buf = ctx.r4.u32;
  st32(base, buf + 8, ctx.r3.u32);
  st32(base, buf + 0, buf + 12);
  st32(base, buf + 4, 0);
}
NIER_VERIFY_SPEC(sub_82124390, 0x82124390, "r4=ptr");

// sub_82124610 (36 bytes) — leitor de string para lua_load (getS): entrega o bloco
// inteiro uma vez (tamanho em *size) e depois devolve nulo.
NIER_DECOMP(sub_82124610) {
  const uint32_t state = ctx.r4.u32, size_out = ctx.r5.u32;
  const uint32_t size = ld32(base, state + 4);
  if (size == 0) { ctx.r3.u64 = 0; return; }
  st32(base, size_out, size);
  ctx.r3.u64 = ld32(base, state + 0);
  st32(base, state + 4, 0);
}
NIER_VERIFY_SPEC(sub_82124610, 0x82124610, "r4=ptr r5=ptr");
