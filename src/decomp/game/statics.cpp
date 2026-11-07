// Objetos estáticos locais com construção sob demanda, reconstruídos do NieR.
#include <nier/decomp.h>
using namespace nier::guest;

// sub_82149BE0 (100 bytes) — devolve em *r3 o objeto estático em 0x84694CB8,
// construindo-o na primeira chamada (flag em 0x826908A4): vtable 0x8200EE64, +4 = 0.
NIER_DECOMP(sub_82149BE0) {
  constexpr uint32_t kFirstCall = 0x826908A4, kInstance = 0x84694CB4, kStorage = 0x84694CB8;
  if (ld8(base, kFirstCall) == 0) { st32(base, ctx.r3.u32, ld32(base, kInstance)); return; }
  st8(base, kFirstCall, 0);
  const uint32_t obj = (kStorage + 7) & ~7u;
  st32(base, obj + 4, 0);
  st32(base, kInstance, obj);
  st32(base, obj + 0, 0x8200EE64);
  st32(base, ctx.r3.u32, obj);
}
NIER_VERIFY_SPEC(sub_82149BE0, 0x82149BE0, "r3=ptr");
