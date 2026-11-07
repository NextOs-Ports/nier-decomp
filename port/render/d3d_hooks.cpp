// Ganchos do Direct3D do NieR (endereços em nier_d3d.h / docs/D3D_MAP.md) sobre o código recompilado
// pelo ReXGlue. Cada gancho grava o comando para o renderizador GLES2 e, por padrão, ainda chama o D3D
// original (o rexgpu-null mantém fences/vblank). Com Config::bypassGuestPM4 os desenhos não geram PM4
// (modelo DMC3), depois de provado que nada no jogo espera por eles.
#include "nier_d3d.h"
#include "renderer.h"

#include <rex/hook.h>
#include <rex/ppc/func.h>

REX_EXTERN(__imp__sub_82571508);  // DrawVertices
REX_EXTERN(__imp__sub_825718F8);  // DrawIndexedVertices
REX_EXTERN(__imp__sub_82571070);  // DrawVerticesUP
REX_EXTERN(__imp__sub_82585888);  // Resolve
REX_EXTERN(__imp__sub_8257AB38);  // Swap
REX_EXTERN(__imp__sub_8256BB90);  // SetRenderState(94) = intervalo de apresentação

namespace {
bool Bypass() { return nier::render::Enabled() && nier::render::Settings().bypassGuestPM4; }
}

REX_HOOK_RAW(sub_82571508) {
  if (nier::render::Enabled())
    nier::render::RecordDraw(ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, 0, ctx.r6.u32, false);
  if (Bypass()) return;
  __imp__sub_82571508(ctx, base);
}

REX_HOOK_RAW(sub_825718F8) {
  if (nier::render::Enabled())
    nier::render::RecordDraw(ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32, true);
  if (Bypass()) return;
  __imp__sub_825718F8(ctx, base);
}

// DrawVerticesUP: stride em r6 (medido: grava r6>>2 em +0x31A0). O ponteiro dos dados ainda não foi
// fixado (r7 no DMC3); até a captura confirmar, o comando só é gravado quando r7 aponta para memória.
REX_HOOK_RAW(sub_82571070) {
  if (nier::render::Enabled() && ctx.r7.u32)
    nier::render::RecordDrawUP(ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, base + ctx.r7.u32, ctx.r6.u32);
  if (Bypass()) return;
  __imp__sub_82571070(ctx, base);
}

// Resolve(dev, flags, srcRect, destTex, destPoint, level, slice, clearColor*, clearZ=f1, …)
REX_HOOK_RAW(sub_82585888) {
  if (nier::render::Enabled()) {
    int32_t rect[4];
    bool hasRect = ctx.r5.u32 != 0;
    if (hasRect)
      for (int i = 0; i < 4; ++i) rect[i] = int32_t(nier::d3d::BE32(base + ctx.r5.u32 + 4 * i));
    uint32_t argb = 0;
    if (ctx.r10.u32) {  // D3DVECTOR4 RGBA em float
      auto c = [&](int i) { float v = nier::d3d::BEF(base + ctx.r10.u32 + 4 * i); return uint32_t(v <= 0 ? 0 : v >= 1 ? 255 : v * 255 + 0.5f); };
      argb = c(3) << 24 | c(0) << 16 | c(1) << 8 | c(2);
    }
    nier::render::RecordResolve(ctx.r3.u32, ctx.r4.u32, hasRect ? rect : nullptr, ctx.r6.u32, argb);
  }
  __imp__sub_82585888(ctx, base);
}

REX_HOOK_RAW(sub_8257AB38) {
  __imp__sub_8257AB38(ctx, base);
  nier::render::Present();
}

REX_HOOK_RAW(sub_8256BB90) {
  nier::render::RecordPresentInterval(ctx.r4.u32);
  __imp__sub_8256BB90(ctx, base);
}
