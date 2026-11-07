// Ganchos do Direct3D do NieR (endereços em nier_d3d.h / docs/D3D_MAP.md) sobre o código recompilado
// pelo ReXGlue. Cada gancho grava o comando para o renderizador GLES2 e, por padrão, ainda chama o D3D
// original (o rexgpu-null mantém fences/vblank). NIER_BYPASS_PM4=1 deixa de gerar PM4 nos desenhos
// (modelo DMC3), depois de provado que nada no jogo espera por eles.
//
// O renderizador liga sozinho no primeiro gancho quando NIER_NATIVE_RENDER está definido:
//   NIER_RENDER_WIDTH (>= 640, padrão 854), NIER_GLES2_SHADERS (dump ESSL privado), NIER_NO_CULL=1.
#include "nier_d3d.h"
#include "renderer.h"

#include <rex/hook.h>
#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#include <cstdio>
#include <cstdlib>
#include <mutex>

REX_EXTERN(__imp__sub_82571508);  // DrawVertices
REX_EXTERN(__imp__sub_825718F8);  // DrawIndexedVertices
REX_EXTERN(__imp__sub_82571070);  // DrawVerticesUP
REX_EXTERN(__imp__sub_82585888);  // Resolve
REX_EXTERN(__imp__sub_8257AB38);  // Swap
REX_EXTERN(__imp__sub_8256BB90);  // SetRenderState(94) = intervalo de apresentação

namespace {
const uint8_t* ReadVirtual(uint32_t address, uint32_t size) {
  if (!address || uint64_t(address) + size > 0x100000000ull) return nullptr;
  return rex::system::kernel_memory()->TranslateVirtual<const uint8_t*>(address);
}

bool Ready() {
  static std::once_flag once;
  static bool on = false;
  std::call_once(once, [] {
    if (!std::getenv("NIER_NATIVE_RENDER")) return;
    nier::render::Config c;
    if (const char* w = std::getenv("NIER_RENDER_WIDTH")) { c.renderWidth = std::atoi(w); c.renderHeight = c.renderWidth * 9 / 16; }
    c.shaderDir = std::getenv("NIER_GLES2_SHADERS");
    c.bypassGuestPM4 = std::getenv("NIER_BYPASS_PM4") && std::atoi(std::getenv("NIER_BYPASS_PM4"));
    c.cull = !(std::getenv("NIER_NO_CULL") && std::atoi(std::getenv("NIER_NO_CULL")));
    nier::render::GuestMemory m{ReadVirtual, rex::system::kernel_memory()->physical_membase()};
    on = nier::render::Init(c, m);
  });
  return on;
}
bool Bypass() { return Ready() && nier::render::Settings().bypassGuestPM4; }
}  // namespace

REX_HOOK_RAW(sub_82571508) {
  if (Ready()) nier::render::RecordDraw(ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, 0, ctx.r6.u32, false);
  if (Bypass()) return;
  __imp__sub_82571508(ctx, base);
}

REX_HOOK_RAW(sub_825718F8) {
  if (Ready()) nier::render::RecordDraw(ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32, true);
  if (Bypass()) return;
  __imp__sub_825718F8(ctx, base);
}

// BeginVertices(dev, prim, vertexCount, stride): devolve em r3 o espaço onde o jogo escreve os vértices
// (o rastro de 07/11 mostrou r7 sem dados). Grava o desenho depois que o jogo preencher o bloco.
REX_HOOK_RAW(sub_82571070) {
  uint32_t device = ctx.r3.u32, prim = ctx.r4.u32, count = ctx.r5.u32, stride = ctx.r6.u32;
  __imp__sub_82571070(ctx, base);
  if (Ready()) nier::render::RecordBeginVertices(device, prim, count, stride, ctx.r3.u32);
}

// Resolve(dev, flags, srcRect, destTex, destPoint, level, slice, clearColor*, clearZ=f1, …)
REX_HOOK_RAW(sub_82585888) {
  if (Ready()) {
    int32_t rect[4];
    const uint8_t* r = ctx.r5.u32 ? ReadVirtual(ctx.r5.u32, 16) : nullptr;
    if (r) for (int i = 0; i < 4; ++i) rect[i] = int32_t(nier::d3d::BE32(r + 4 * i));
    uint32_t argb = 0;
    if (const uint8_t* cc = ctx.r10.u32 ? ReadVirtual(ctx.r10.u32, 16) : nullptr) {  // D3DVECTOR4 RGBA
      auto c = [&](int i) { float v = nier::d3d::BEF(cc + 4 * i); return uint32_t(v <= 0 ? 0 : v >= 1 ? 255 : v * 255 + 0.5f); };
      argb = c(3) << 24 | c(0) << 16 | c(1) << 8 | c(2);
    }
    nier::render::RecordResolve(ctx.r3.u32, ctx.r4.u32, r ? rect : nullptr, ctx.r6.u32, argb, ctx.r8.u32);
  }
  __imp__sub_82585888(ctx, base);
}

REX_HOOK_RAW(sub_8257AB38) {
  __imp__sub_8257AB38(ctx, base);
  if (Ready()) nier::render::Present();
}

REX_HOOK_RAW(sub_8256BB90) {
  nier::render::RecordPresentInterval(ctx.r4.u32);
  __imp__sub_8256BB90(ctx, base);
}
