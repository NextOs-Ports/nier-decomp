// Espera da CPU pela GPU no Direct3D do XDK 2.0.9328, reconstruída do NieR.
#include <nier/decomp.h>
using namespace nier::guest;

DECLARE_REX_FUNC(sub_8255D138);   // thread atual → identificador
DECLARE_REX_FUNC(sub_8257D028);   // tratamento de GPU travada

// Estado de uma espera em andamento.
struct GpuWait {
  static constexpr uint32_t device = 0;        // ponteiro para o device D3D
  static constexpr uint32_t last_seen = 8;      // último valor lido do contador da GPU
  static constexpr uint32_t since = 12;         // instante (ms) do último progresso
};
// Campos do device usados aqui.
struct Device {
  static constexpr uint32_t gpu_counter = 10896;  // ponteiro para o contador escrito pela GPU
  static constexpr uint32_t owner_thread = 10888; // thread dona do device
  static constexpr uint32_t owner_busy = 11008;   // dona está ocupada (não conta como travada)
  static constexpr uint32_t flags = 10941;        // bit 1: device perdido / desligado
};

// sub_82579D50 (216 bytes)
// Uma volta da espera ativa pela GPU. Pausa um pouco e decide se continua esperando:
//   - device desligado → 0 (parar);
//   - a GPU avançou o contador → reinicia o relógio de progresso;
//   - a própria thread dona está ocupada → também reinicia;
//   - 5 s sem progresso → chama o tratamento de GPU travada; se ele mandar parar, 0.
// Devolve 1 para continuar esperando.
// No console a pausa era 32 instruções `db16cyc` em prioridade baixa; no port cede o núcleo.
NIER_DECOMP(sub_82579D50) {
  const uint32_t wait = ctx.r3.u32;
  const uint32_t device = ld32(base, wait + GpuWait::device);
  for (int i = 0; i < 4; ++i) nier::host::spin_pause();

  if (ld8(base, device + Device::flags) & 0x2) {
    ctx.r3.u64 = 0;
    return;
  }
  const uint32_t counter = ld32(base, device + Device::gpu_counter);
  const uint32_t pcr = ld32(base, ctx.r13.u32 + 0x100);
  const uint32_t now_ms = ld32(base, pcr + 88);
  if (ld32(base, wait + GpuWait::last_seen) != ld32(base, counter)) {
    st32(base, wait + GpuWait::since, now_ms);
    st32(base, wait + GpuWait::last_seen, ld32(base, counter));
  }
  sub_8255D138(ctx, base);
  if (ld32(base, device + Device::owner_thread) == ctx.r3.u32 &&
      static_cast<int32_t>(ld32(base, device + Device::owner_busy)) != 0) {
    st32(base, wait + GpuWait::since, now_ms);
  }
  if (now_ms - ld32(base, wait + GpuWait::since) >= 5000) {
    ctx.r3.u64 = device;
    sub_8257D028(ctx, base);
    if (static_cast<int32_t>(ctx.r3.u32) != 0) {
      ctx.r3.u64 = 0;
      return;
    }
    st32(base, wait + GpuWait::since, now_ms);
  }
  ctx.r3.u64 = 1;
}
NIER_VERIFY_SPEC(sub_82579D50, 0x82579D50,
                 "r3=ptr @r3.0=ptr @r3.0.10896=ptr @r13.0x100=ptr @r3.0.10941=range:0..0xFDFFFFFF");
