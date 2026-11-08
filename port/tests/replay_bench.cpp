// nier_replay_bench (C1-K1): reproduz em laço UM quadro gravado com NIER_FRAME_DUMP=<dir> pelo jogo e mede, por modo
// do A/B (os mesmos g_abMode do renderizador), parede, CPU desta thread e GPU (glFinish por quadro, só no banco).
// Uso: nier_replay_bench <dir>/frame.bin   [NIER_BENCH_MODES=0,9,10,...] [NIER_BENCH_SECONDS=8]
// Sem o jogo e sem o runtime: o mesmo código de replay (ReplayFrameCore) do renderizador, incluído aqui.
#ifndef NIER_RENDERER_TEST_SOURCE
#define NIER_RENDERER_TEST_SOURCE "../render/renderer.cpp"
#endif
#include NIER_RENDERER_TEST_SOURCE
#include <sys/mman.h>
int main(int argc, char** argv) {
  using namespace nier::render;
  const char* path = argc > 1 ? argv[1] : "frame.bin";
  void* mem = mmap(nullptr, 0x20000000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
  if (mem == MAP_FAILED) { std::fprintf(stderr, "[nier-bench] mmap falhou\n"); return 1; }
  g_mem.physicalBase = static_cast<uint8_t*>(mem);
  if (!CreatePbuffer() || !eglMakeCurrent(g_eglDisplay, g_eglSurface, g_eglSurface, g_eglContext)) { std::fprintf(stderr, "[nier-bench] EGL falhou\n"); return 2; }
  Frame frame;
  if (!LoadFrameDump(path, frame)) { std::fprintf(stderr, "[nier-bench] dump ilegível: %s\n", path); return 3; }
  RenderInitGl();
  std::vector<int> modes;
  if (const char* m = std::getenv("NIER_BENCH_MODES")) { for (const char* p = m; *p;) { modes.push_back(std::atoi(p)); while (*p && *p != ',') ++p; if (*p) ++p; } }
  else for (int k = 0; k < 13; ++k) modes.push_back(k);
  modes.push_back(0);  // base de novo no fim: deriva/aquecimento
  const double seconds = std::getenv("NIER_BENCH_SECONDS") ? std::atof(std::getenv("NIER_BENCH_SECONDS")) : 8.0;
  auto cpuNow = [] { timespec ts; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts); return double(ts.tv_sec) * 1e3 + ts.tv_nsec / 1e6; };
  auto one = [&] {
    ReplayFrameCore(frame);
    for (auto& [k, t] : g_targets) if (t.clearedFrame == g_frames.load()) DiscardDepth(t.fbo, t.depth);
    PresentToWindow(); InvalidateGl(); g_depthKeep.clear();
    ++g_frames;
    if (g_frames.load() % 60 == 0) { TrimBufferObjects(); TrimClusters(); }
  };
  for (int k = 0; k < 5; ++k) { one(); glFinish(); }  // LEVE_OK: banco de medição fora do jogo; aquece shaders/texturas/agrupamentos
  if (const char* ppm = std::getenv("NIER_BENCH_PPM")) {  // imagem do quadro reproduzido (conferência)
    std::vector<uint8_t> px(1280 * 720 * 4);
    glBindFramebuffer(GL_FRAMEBUFFER, 0); glReadPixels(0, 0, 1280, 720, GL_RGBA, GL_UNSIGNED_BYTE, px.data());  // LEVE_OK: banco
    if (FILE* o = std::fopen(ppm, "wb")) { std::fprintf(o, "P6\n1280 720\n255\n"); for (int y = 719; y >= 0; --y) for (int x = 0; x < 1280; ++x) std::fwrite(&px[(size_t(y) * 1280 + x) * 4], 1, 3, o); std::fclose(o); }
    InvalidateGl();
  }
  for (int mode : modes) {
    g_abMode = mode;
    for (int k = 0; k < 3; ++k) { one(); glFinish(); }  // LEVE_OK: banco
    int frames = 0; double cpu0 = cpuNow(), cpuSum = 0, gpuSum = 0;
    auto t0 = Clock::now();
    while (std::chrono::duration<double>(Clock::now() - t0).count() < seconds) {
      double c0 = cpuNow(); auto w0 = Clock::now();
      one();
      double c1 = cpuNow(); auto w1 = Clock::now();
      glFinish();  // LEVE_OK: banco — espera da GPU por quadro
      auto w2 = Clock::now();
      cpuSum += c1 - c0; gpuSum += std::chrono::duration<double, std::milli>(w2 - w1).count(); ++frames;
      (void)w0;
    }
    double wall = std::chrono::duration<double, std::milli>(Clock::now() - t0).count() / std::max(frames, 1);
    std::fprintf(stderr, "[nier-bench] modo %2d %-24s %4d quadros: %.1f ms/quadro (CPU do replay %.1f, espera da GPU no fim %.1f) => %.1f quadros/s; CPU total da thread %.1f\n",
                 mode, kAbNames[mode], frames, wall, cpuSum / std::max(frames, 1), gpuSum / std::max(frames, 1), 1000.0 / wall, (cpuNow() - cpu0) / std::max(frames, 1));
  }
  return 0;
}
