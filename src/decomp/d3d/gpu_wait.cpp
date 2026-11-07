// Espera da CPU pela GPU no Direct3D do XDK 2.0.9328, reconstruída do NieR.
#include <nier/decomp.h>
using namespace nier::guest;

DECLARE_REX_FUNC(sub_8255D138);   // thread atual → identificador
DECLARE_REX_FUNC(sub_8257D028);   // tratamento de GPU travada
DECLARE_REX_FUNC(sub_82579C58);   // início da espera (abaixo)
DECLARE_REX_FUNC(sub_82579C88);   // fim da espera (abaixo)
DECLARE_REX_FUNC(sub_82579D50);   // uma volta da espera (abaixo)

// Estado de uma espera em andamento (24 bytes na pilha de quem espera).
struct GpuWait {
  static constexpr uint32_t device = 0;        // ponteiro para o device D3D
  static constexpr uint32_t kind = 4;          // motivo da espera (3 = anel de comandos)
  static constexpr uint32_t last_seen = 8;      // último valor lido do contador da GPU
  static constexpr uint32_t since = 12;         // instante (ms) do último progresso
  static constexpr uint32_t start_ms = 16;      // instante (ms) do início da espera
  static constexpr uint32_t start_tb = 20;      // relógio do console (mftb) no início
};
// Campos do device usados aqui.
struct Device {
  static constexpr uint32_t gpu_counter = 10896;  // ponteiro para o contador escrito pela GPU (leitura do anel)
  static constexpr uint32_t ring_write = 10908;   // posição de escrita no anel de comandos
  static constexpr uint32_t owner_thread = 10888; // thread dona do device
  static constexpr uint32_t owner_busy = 11008;   // dona está ocupada (não conta como travada)
  static constexpr uint32_t flags = 10941;        // bit 1: device perdido / desligado
  static constexpr uint32_t ring_lock = 13408;    // anel travado por outra operação
  static constexpr uint32_t wait_callback = 13644; // função chamada ao fim de cada espera (ou 0)
  static constexpr uint32_t tick_scale = 21800;   // float: ticks → unidade do callback
  static constexpr uint32_t ticks_other = 21808;  // u64: ticks esperados (outros motivos)
  static constexpr uint32_t ticks_ring = 21816;   // u64: ticks esperados pelo anel
};
constexpr uint32_t kPcrSlot = 0x100;        // r13+0x100: bloco por thread; +88 = milissegundos
constexpr uint32_t kTickToSeconds = 0x820007DC; // float constante: fator final do callback

DECLARE_REX_FUNC(sub_825744F8);   // tenta liberar espaço no anel (descarrega comandos)

// sub_82579C58 (48 bytes) — início de uma espera: guarda device, motivo, valor atual do
// contador da GPU, o relógio em ms (duas vezes) e o relógio do console.
NIER_DECOMP(sub_82579C58) {
  const uint32_t wait = ctx.r3.u32, device = ctx.r4.u32;
  st32(base, wait + GpuWait::device, device);
  st32(base, wait + GpuWait::kind, ctx.r5.u32);
  const uint32_t now_ms = ld32(base, ld32(base, ctx.r13.u32 + kPcrSlot) + 88);
  st32(base, wait + GpuWait::last_seen, ld32(base, ld32(base, device + Device::gpu_counter)));
  st32(base, wait + GpuWait::since, now_ms);
  st32(base, wait + GpuWait::start_ms, now_ms);
  st32(base, wait + GpuWait::start_tb, static_cast<uint32_t>(REX_QUERY_TIMEBASE()));
}
NIER_VERIFY_SPEC(sub_82579C58, 0x82579C58, "r3=ptr r4=ptr @r4.10896=ptr @r13.0x100=ptr");

// sub_82579C88 (200 bytes) — fim da espera: soma os ticks gastos ao acumulador do device
// (anel ou outros) e, se houver callback, chama callback(0, motivo, ticks*escala*fator).
NIER_DECOMP(sub_82579C88) {
  const uint32_t wait = ctx.r3.u32;
  const uint32_t kind = ld32(base, wait + GpuWait::kind);
  if (static_cast<int32_t>(kind) == 0) return;
  const uint32_t elapsed = static_cast<uint32_t>(REX_QUERY_TIMEBASE()) - ld32(base, wait + GpuWait::start_tb);
  const uint32_t device = ld32(base, wait + GpuWait::device);
  const uint32_t acc = static_cast<int32_t>(kind) == 3 ? Device::ticks_ring : Device::ticks_other;
  st64(base, device + acc, ld64(base, device + acc) + elapsed);
  const uint32_t callback = ld32(base, device + Device::wait_callback);
  if (callback == 0) return;
  const float scaled = ldf(base, device + Device::tick_scale) * static_cast<float>(static_cast<double>(elapsed));
  ctx.f1.f64 = scaled * ldf(base, kTickToSeconds);
  ctx.r4.u64 = ld32(base, wait + GpuWait::kind);
  ctx.r3.u64 = 0;
  GuestFrame f(ctx, base, 96);
  call_indirect(ctx, base, callback);
}
NIER_VERIFY_SPEC(sub_82579C88, 0x82579C88, "r3=ptr @r3.0=ptr @r3.4=range:0..4 @r13.0x100=ptr @r3.0.13644=const:0x8262AB08 callargs=2");

// sub_82573758 (204 bytes) — espera até caber `needed` bytes no anel de comandos:
// enquanto (escrita − needed) < (escrita − leitura), gira uma volta da espera pela GPU.
// Se o anel está exatamente no fim (needed == escrita): travado por outra operação → desiste;
// livre → antes tenta descarregar. Devolve o que a última chamada deixou em r3 (o chamador ignora).
NIER_DECOMP(sub_82573758) {
  const uint32_t device = ctx.r3.u32, needed = ctx.r4.u32;
  const uint64_t kind = ctx.r5.u64;
  const bool force = (ctx.r7.u32 & 0xFF) != 0;
  if (needed == 0) return;
  GuestFrame f(ctx, base, 144);
  auto fits = [&]() {
    const uint32_t write = ld32(base, device + Device::ring_write);
    const uint32_t read = ld32(base, ld32(base, device + Device::gpu_counter));
    return write - needed >= write - read;
  };
  if (fits()) return;
  if (!force && needed == ld32(base, device + Device::ring_write)) {
    if (ld32(base, device + Device::ring_lock) != 0) return;
    ctx.r3.u64 = device;
    sub_825744F8(ctx, base);
    if (fits()) return;
  }
  const uint32_t wait = f.slot(80);
  ctx.r3.u64 = wait; ctx.r4.u64 = device; ctx.r5.u64 = kind;
  sub_82579C58(ctx, base);
  while (!fits()) {
    ctx.r3.u64 = wait;
    sub_82579D50(ctx, base);
    if (static_cast<int32_t>(ctx.r3.u32) == 0) break;
  }
  ctx.r3.u64 = wait;
  sub_82579C88(ctx, base);
}
NIER_VERIFY_SPEC(sub_82573758, 0x82573758,
                 "r3=ptr r4=range:0..64 r7=range:0..1 @r3.10896=ptr @r3.10908=range:0..64 @r13.0x100=ptr "
                 "@r3.10941=range:0..0xFDFFFFFF @r3.13644=const:0x8262AB08");

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
