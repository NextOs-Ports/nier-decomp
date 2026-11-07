// Predicados simples sobre objetos do jogo, reconstruídos do NieR (Xbox 360).
// Tipos e nomes dos campos ainda são provisórios: só o deslocamento é certo.
#include <nier/decomp.h>

using namespace nier::guest;

// sub_82102878 (64 bytes)
// Verdadeiro quando as duas flags de byte em +8 e +9 estão ligadas, o contador
// em +28 não é positivo e o estado em +20 é diferente de 2.
NIER_DECOMP(sub_82102878) {
  const uint32_t self = ctx.r3.u32;
  const bool ready = ld8(base, self + 8) != 0 && ld8(base, self + 9) != 0 &&
                     static_cast<int32_t>(ld32(base, self + 28)) <= 0 &&
                     static_cast<int32_t>(ld32(base, self + 20)) != 2;
  ctx.r3.u64 = ready ? 1 : 0;
}
NIER_VERIFY_SPEC(sub_82102878, 0x82102878, "r3=ptr");

// sub_821B6180 (64 bytes)
// Falso quando o modo em +2520 é 7, 11 ou 12; verdadeiro nos demais.
NIER_DECOMP(sub_821B6180) {
  const int32_t mode = static_cast<int32_t>(ld32(base, ctx.r3.u32 + 2520));
  ctx.r3.u64 = (mode == 7 || mode == 11 || mode == 12) ? 0 : 1;
}
NIER_VERIFY_SPEC(sub_821B6180, 0x821B6180, "r3=ptr");

// sub_823C6A50 (76 bytes)
// Lista de ponteiros com contagem em +32 e itens a partir de +36. Devolve o campo
// +16 do item `index`, ou 0 se o índice estiver fora da lista ou o item for nulo.
NIER_DECOMP(sub_823C6A50) {
  const uint32_t list = ctx.r3.u32;
  const int32_t index = static_cast<int32_t>(ctx.r4.u32);
  uint32_t result = 0;
  if (index >= 0 && index < static_cast<int32_t>(ld32(base, list + 32))) {
    const uint32_t item = ld32(base, list + static_cast<uint32_t>(index + 9) * 4);
    if (item != 0) result = ld32(base, item + 16);
  }
  ctx.r3.u64 = result;
}
NIER_VERIFY_SPEC(sub_823C6A50, 0x823C6A50, "r3=ptr r4=range:0..64");

// sub_82338BD0 (60 bytes)
// Encaminhador: se há um alvo em +4, chama o método virtual 1 dele com
// (alvo, campo +8, arg1, arg2, campo +12) e devolve o resultado; senão devolve 0.
NIER_DECOMP(sub_82338BD0) {
  const uint32_t self = ctx.r3.u32;
  const uint32_t target = ld32(base, self + 4);
  if (target == 0) {
    ctx.r3.u64 = 0;
    return;
  }
  const uint64_t arg1 = ctx.r4.u64, arg2 = ctx.r5.u64;
  ctx.r3.u64 = target;
  ctx.r4.u64 = ld32(base, self + 8);
  ctx.r5.u64 = arg1;
  ctx.r6.u64 = arg2;
  ctx.r7.u64 = ld32(base, self + 12);
  call_virtual(ctx, base, target, 1);
}
NIER_VERIFY_SPEC(sub_82338BD0, 0x82338BD0, "r3=ptr");
