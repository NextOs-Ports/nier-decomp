// Utilitários de caminho, reconstruídos do NieR.
#include <nier/decomp.h>
using namespace nier::guest;

// sub_821125A8 (88 bytes) — basename: ponteiro para depois da última '/' ou '\\';
// o próprio texto se não houver separador.
NIER_DECOMP(sub_821125A8) {
  const uint32_t text = ctx.r3.u32;
  uint32_t end = text;
  while (ld8(base, end++) != 0) {}
  int32_t i = static_cast<int32_t>(end - text - 1);
  while (i > 0) {
    --i;
    const int8_t c = static_cast<int8_t>(ld8(base, text + static_cast<uint32_t>(i)));
    if (c == '/' || c == '\\') {
      ctx.r3.u64 = text + static_cast<uint32_t>(i) + 1;
      return;
    }
  }
}
NIER_VERIFY_SPEC(sub_821125A8, 0x821125A8, "r3=bptr");
