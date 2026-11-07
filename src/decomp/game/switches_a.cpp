// Interruptores de estado com notificação virtual, reconstruídos do NieR.
// Objeto: vtable em +0, ligado (byte) em +8. Método 3 = ao ligar, método 4 = ao desligar.
#include <nier/decomp.h>
using namespace nier::guest;

namespace {
constexpr uint32_t kOn = 8;
constexpr uint32_t kOnEnter = 3, kOnLeave = 4;
}

// sub_821028B8 (64 bytes) — como sub_82102878, com a 2ª flag em +10 e o contador em +32.
NIER_DECOMP(sub_821028B8) {
  const uint32_t self = ctx.r3.u32;
  const bool ready = ld8(base, self + 8) != 0 && ld8(base, self + 10) != 0 &&
                     static_cast<int32_t>(ld32(base, self + 32)) <= 0 &&
                     static_cast<int32_t>(ld32(base, self + 20)) != 2;
  ctx.r3.u64 = ready ? 1 : 0;
}
NIER_VERIFY_SPEC(sub_821028B8, 0x821028B8, "r3=ptr");

// sub_82102F28 (88 bytes) — set_on(self, on, arg): só age se o estado mudar;
// grava o novo estado e chama o método "ao ligar" ou "ao desligar" com (self, arg).
NIER_DECOMP(sub_82102F28) {
  const uint32_t self = ctx.r3.u32;
  const uint8_t on = static_cast<uint8_t>(ctx.r4.u32);
  ctx.r4.u64 = ctx.r5.u64;                 // arg segue em r4 para o método
  const bool was_on = ld8(base, self + kOn) != 0;
  if ((on != 0) == was_on) return;         // nada mudou: devolve self
  st8(base, self + kOn, on);
  call_virtual(ctx, base, self, on ? kOnEnter : kOnLeave);
}
NIER_VERIFY_SPEC(sub_82102F28, 0x82102F28, "r3=obj r4=u8");

// sub_82102FD8 (96 bytes) — mesmo interruptor, mas recebe o objeto em r5,
// zera o campo +20 e passa ao método o campo +24 como argumento.
NIER_DECOMP(sub_82102FD8) {
  const uint32_t self = ctx.r5.u32;
  const uint8_t on = static_cast<uint8_t>(ctx.r4.u32);
  st32(base, self + 20, 0);
  ctx.r3.u64 = self;
  ctx.r4.u64 = ld32(base, self + 24);
  const bool was_on = ld8(base, self + kOn) != 0;
  if ((on != 0) == was_on) return;
  st8(base, self + kOn, on ? 1 : 0);
  call_virtual(ctx, base, self, on ? kOnEnter : kOnLeave);
}
NIER_VERIFY_SPEC(sub_82102FD8, 0x82102FD8, "r5=obj r4=u8");
