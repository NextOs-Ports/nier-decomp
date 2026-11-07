// Acesso à thread atual pelo bloco do processador (r13), reconstruído do NieR.
#include <nier/decomp.h>
using namespace nier::guest;

// sub_8255D138 (12 bytes)
// Devolve o campo +0x14C da thread atual (r13 → PCR, +0x100 → KTHREAD).
// É usado pelo D3D para saber se a thread chamadora é a dona do device.
NIER_DECOMP(sub_8255D138) {
  const uint32_t thread = ld32(base, ctx.r13.u32 + 0x100);
  ctx.r3.u64 = ld32(base, thread + 0x14C);
}
NIER_VERIFY_SPEC(sub_8255D138, 0x8255D138, "@r13.0x100=ptr");
