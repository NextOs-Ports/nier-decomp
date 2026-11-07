// Shaders do jogo em tempo de execução: lê o objeto shader do D3D (layout 9328), corrige os vfetch do
// microcódigo pela declaração de vértice atual (como o D3D faz no desenho), traduz para ESSL 100 e guarda
// em cache por hash. O tradutor Xenos -> ESSL é uma dependência PRIVADA fixada (docs/RENDER.md §8);
// sem ela (NIER_HAVE_XENOS_TRANSLATOR indefinido) Available() é false e nenhum desenho 3D acontece.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nier::render {

using VirtualReader = const uint8_t* (*)(uint32_t guestAddress, uint32_t size);

struct VertexInput {
  uint32_t slot = 0;    // fetch constant de vértice (0..95)
  uint32_t offset = 0;  // palavras
  uint32_t stride = 0;  // palavras
  uint32_t format = 0;  // VertexFormat da Xenos
  bool isSigned = false, isInteger = false;
  int32_t expAdjust = 0;
};

struct GuestShader {
  bool ok = false, pixel = false;
  uint64_t key = 0;
  std::string source;   // ESSL 100 completo (com #version/precisão)
  std::string error;
  std::vector<VertexInput> inputs;  // VS: atributo a<i> = inputs[i]
  std::vector<float> literals;      // vec4 que ocupam c[256-n .. 255]
  uint32_t textureMask = 0;
  uint32_t booleanMask[8] = {};
  bool relativeConstants = false;
  // Compactação para a Mali-450 (256 vec4 por estágio, contando os uniformes extras): o array VC/PC
  // declarado tem constMap.size() posições; posição j recebe a constante original constMap[j].
  std::vector<uint16_t> constMap;
};

bool TranslatorAvailable();
// device = endereço virtual do device; devolve nulo se não houver shader ou se a leitura falhar.
const GuestShader* VertexShaderFor(uint32_t device, VirtualReader read);
const GuestShader* PixelShaderFor(uint32_t device, VirtualReader read);
void SetShaderDumpDir(const char* dir);  // grava <key>_{vs,ps}.glsl ao traduzir (cache privado)

}  // namespace nier::render
