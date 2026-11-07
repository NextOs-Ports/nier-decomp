// Autoteste do renderizador sem o jogo: device sintético com back buffer 1280x720, Resolve com limpeza
// vermelha, Present. Com NIER_GLES2_CAPTURE grava o quadro; o teste confere o pixel central.
// Encerra sozinho em poucos segundos (sem jogo envolvido).
#include "nier_d3d.h"
#include "renderer.h"

#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

int main() {
  if (!SDL_Init(SDL_INIT_VIDEO)) { std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 2; }
  static std::vector<uint8_t> virt(0x100000, 0), phys(0x100000, 0);
  nier::render::GuestMemory mem{[](uint32_t a, uint32_t n) -> const uint8_t* { return uint64_t(a) + n <= virt.size() ? virt.data() + a : nullptr; }, phys.data()};
  nier::render::Config cfg;
  cfg.renderWidth = 854; cfg.renderHeight = 480;
  if (!nier::render::Init(cfg, mem)) return 3;
  const uint32_t device = 0x10000;
  auto put = [&](uint32_t off, uint32_t v) { v = __builtin_bswap32(v); std::memcpy(&virt[device + off], &v, 4); };
  put(nier::d3d::kSurfaceInfo, 1280);   // pitch do alvo
  put(nier::d3d::kColorInfo, 0);
  uint32_t red[4];
  float rgba[4] = {1, 0, 0, 1};
  std::memcpy(red, rgba, sizeof red);
  for (int frame = 0; frame < 240; ++frame) {
    nier::render::RecordResolve(device, 0x100, nullptr, 0, 0xFFFF0000u);
    nier::render::Present();
    SDL_Event e;
    while (SDL_PollEvent(&e)) {}
    SDL_Delay(16);
  }
  auto st = nier::render::ReadStats();
  nier::render::Shutdown();
  SDL_Quit();
  std::printf("quadros %llu, draws %llu, último %.2f ms\n", (unsigned long long)st.frames, (unsigned long long)st.draws, st.lastFrameMs);
  return st.frames > 0 ? 0 : 1;
}
