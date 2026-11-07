// Renderizador GLES2 do NieR (Mali-450): interface entre os ganchos do D3D (thread do jogo) e a thread
// de render, que é a única dona do contexto GL (contexto, desenho e troca de buffer na mesma thread).
//
// Fluxo: os ganchos chamam Record*() durante o tick do jogo; cada comando leva uma cópia do estado do
// device (kStateBegin..kStateEnd) e dos vértices/índices referenciados. Present() entrega o quadro à
// thread de render (buffer duplo; espera só se o quadro anterior ainda não foi desenhado).
#pragma once

#include <cstdint>

#include "shader_runtime.h"

namespace nier::render {

// Leitores de memória guest, fornecidos pelo runtime (X1): endereço virtual e físico (GPU).
struct GuestMemory {
  VirtualReader virtualRead = nullptr;  // Memory::TranslateVirtual (soma o deslocamento de cada heap)
  uint8_t* physicalBase = nullptr;      // Memory::physical_membase()
};

struct Config {
  int renderWidth = 854;   // resolução interna; nunca abaixo de 640 (regra da casa)
  int renderHeight = 480;
  bool mipmaps = true;     // mipmap em toda textura estática POT (lição do Rayman)
  const char* shaderDir = nullptr;  // cache ESSL por hash (tools/shaders/nier_shaders)
  bool bypassGuestPM4 = false;      // modelo DMC3: não chamar o D3D original nos desenhos
  bool cull = true;                 // NIER_NO_CULL=1 desliga (diagnóstico)
};

// Cria a janela/contexto GLES2 (thread que chamou) e libera o contexto para a thread de render.
bool Init(const Config& config, const GuestMemory& memory);
void Shutdown();
bool Enabled();
const Config& Settings();

enum class Primitive : uint32_t { PointList = 1, LineList = 2, LineStrip = 3, TriangleList = 4, TriangleFan = 5, TriangleStrip = 6, QuadList = 13 };

// Chamados pelos ganchos do D3D (thread do jogo). `device` é o endereço virtual do device.
void RecordDraw(uint32_t device, uint32_t primitive, uint32_t baseVertex, uint32_t startIndex, uint32_t count, bool indexed);
void RecordDrawUP(uint32_t device, uint32_t primitive, uint32_t vertexCount, const uint8_t* data, uint32_t stride);
// BeginVertices (0x82571070): o D3D devolve o espaço onde o jogo escreve os vértices DEPOIS do retorno.
// O desenho é gravado na próxima chamada ao renderizador (o jogo já fechou o bloco com EndVertices).
void RecordBeginVertices(uint32_t device, uint32_t primitive, uint32_t vertexCount, uint32_t stride, uint32_t guestData);
void RecordResolve(uint32_t device, uint32_t flags, const int32_t* rect, uint32_t destTexture, uint32_t clearColorArgb);
void RecordPresentInterval(uint32_t interval);  // render state 94: só informação (ver docs/D3D_MAP.md §4)
void Present();

struct Stats { uint64_t frames, draws, skipped, recordedBytes; double lastFrameMs; uint32_t presentInterval; };
Stats ReadStats();

}  // namespace nier::render
