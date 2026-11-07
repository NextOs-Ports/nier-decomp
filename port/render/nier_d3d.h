// NieR (XDK 2.0.9328): endereços do Direct3D e campos da estrutura do device.
// Fonte: docs/D3D_MAP.md (análise estática da PE ff218ae3…, ver confiança em cada linha lá).
#pragma once

#include <cstdint>
#include <cstring>

namespace nier::d3d {

// ---- funções (guest) ----
constexpr uint32_t kDrawVertices = 0x82571508;         // (dev, prim, startVertex, vertexCount)
constexpr uint32_t kDrawIndexedVertices = 0x825718F8;  // (dev, prim, baseVertex, startIndex, indexCount)
constexpr uint32_t kDrawVerticesUP = 0x82571070;       // (dev, prim, vertexCount, ?, stride=r6) — ordem a confirmar
constexpr uint32_t kResolve = 0x82585888;              // (dev, flags, rect, destTex, destPoint, level, slice, color, z=f1, …)
constexpr uint32_t kSwap = 0x8257AB38;                 // Present: único caminho até VdSwap
constexpr uint32_t kSetVertexDeclaration = 0x825708F8; // (dev, decl): grava +0x2ED8, dirty de vfetch
constexpr uint32_t kSetPresentInterval = 0x8256BB90;   // SetRenderState(94): o jogo escolhe 1/2/4 em 0x825A6F48

// ---- device ----
constexpr uint32_t kStateBegin = 0x480;   // início do espelho de registradores copiado por comando
constexpr uint32_t kStateEnd = 0x3600;    // inclui shaders (0x3244/0x3248), IB (0x3144), RTs e strides
constexpr uint32_t kStateSize = kStateEnd - kStateBegin;

constexpr uint32_t kFetchConstants = 0x480;   // textura t: +24t; vértice v: +8v; stream s -> v = 95 - s
constexpr uint32_t kVertexConstants = 0x780;  // 256 vec4 (reg 0x4000)
constexpr uint32_t kPixelConstants = 0x1780;  // 256 vec4 (reg 0x4400)
constexpr uint32_t kBoolConstants = 0x2780;   // reg 0x4900
constexpr uint32_t kSurfaceInfo = 0x2880;     // RB_SURFACE_INFO (bloco 0x2000)
constexpr uint32_t kColorInfo = 0x2884;       // RB_COLOR_INFO
constexpr uint32_t kViewport = 0x2908;        // PA_CL_VPORT_XSCALE, XOFFSET, YSCALE, YOFFSET, ZSCALE, ZOFFSET
constexpr uint32_t kDepthControl = 0x2934;    // RB_DEPTHCONTROL (bloco 0x2200)
constexpr uint32_t kBlendControl = 0x2938;    // RB_BLENDCONTROL0
constexpr uint32_t kColorControl = 0x293C;    // RB_COLORCONTROL
constexpr uint32_t kModeControl = 0x2948;     // PA_SU_SC_MODE_CNTL (cull/face)
constexpr uint32_t kIndexBuffer = 0x3144;     // objeto IB atual (20871: 0x320C)
constexpr uint32_t kRenderTarget0 = 0x3148;   // RT0..3 em +4i (20871: 0x3210)
constexpr uint32_t kDepthStencil = 0x3158;    // (20871: 0x3220)
constexpr uint32_t kStreamStride0 = 0x31A0;   // stride/4 do stream 0 (20871: 0x3268)
constexpr uint32_t kPixelShader = 0x3244;     // (20871: 0x330C)
constexpr uint32_t kVertexShader = 0x3248;    // (20871: 0x3310)
constexpr uint32_t kPresentInterval = 0x35EC; // render state 94
constexpr uint32_t kVertexDeclaration = 0x2ED8; // (DMC3/20871: 0x2FD0)

// Objeto shader (igual ao DMC3): VS cabeçalho em +0x368, microcódigo em +0x20; PS cabeçalho +0x28,
// microcódigo +0x18. Seção do programa = cabeçalho + BE32(cabeçalho+0x18): {offset, size}.
constexpr uint32_t kVsHeader = 0x368, kVsUcode = 0x20, kPsHeader = 0x28, kPsUcode = 0x18;

inline uint32_t BE32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return __builtin_bswap32(v); }
inline float BEF(const uint8_t* p) { uint32_t v = BE32(p); float f; std::memcpy(&f, &v, 4); return f; }

// Campo do device dentro de um estado copiado (que começa em kStateBegin).
inline const uint8_t* Field(const uint8_t* state, uint32_t offset) { return state + (offset - kStateBegin); }

}  // namespace nier::d3d
