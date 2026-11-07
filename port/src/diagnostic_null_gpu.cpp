// SPDX-License-Identifier: GPL-3.0-only
// Reused from rayman-origins-compact 80837358e9577d755297831e5301eb70ef82593c.
// Guest ring/fence/vblank processing, derived from the upstream Rayman null
// plugin. This backend creates no Vulkan provider and no GPU presenter.
#include <rex/graphics/command_processor.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <rex/graphics/graphics_system.h>
#include <rex/system/gpu_plugin.h>
namespace rex::graphics {
class NextOSCommandProcessor final : public CommandProcessor {
 public:
  using CommandProcessor::CommandProcessor;
  void IssueSwap(uint32_t, uint32_t, uint32_t) override {
    static std::atomic<unsigned> swaps{0};
    const auto n = ++swaps;
    static const bool trace = std::getenv("NIER_DIAG_SWAP_TRACE") != nullptr;
    const auto stamp = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (trace || n <= 3 || n % 300 == 0)
      std::fprintf(stderr, "NIER_DIAG swaps=%u monotonic=%.6f (no rendering)\n", n, stamp);
  }
 protected:
  bool SetupContext() override { return true; }
  void ShutdownContext() override {}
  Shader* LoadShader(xenos::ShaderType, uint32_t, const uint32_t*, uint32_t) override { return nullptr; }
  bool IssueDraw(xenos::PrimitiveType, uint32_t, IndexBufferInfo*, bool) override { return true; }
  bool IssueCopy() override { return true; }
};
class NextOSNullGraphics final : public GraphicsSystem {
 public:
  std::string name() const override { return "NEXTOS GL guest protocol"; }
  X_STATUS SetupPresentation(ui::WindowedAppContext*) override { return X_STATUS_SUCCESS; }
 protected:
  void CreateProvider(bool) override {}
  std::unique_ptr<CommandProcessor> CreateCommandProcessor() override {
    return std::make_unique<NextOSCommandProcessor>(this, kernel_state_);
  }
};
}
extern "C" REX_GPU_PLUGIN_EXPORT uint32_t rex_gpu_abi_version() {
  return rex::system::kGpuPluginAbiVersion;
}
extern "C" REX_GPU_PLUGIN_EXPORT rex::system::IGraphicsSystem* rex_gpu_create(
    uint32_t version, const rex::system::GpuCreateInfo* info) {
  if (version != rex::system::kGpuPluginAbiVersion || !info || info->struct_size < sizeof(*info)) return nullptr;
  return new rex::graphics::NextOSNullGraphics;
}
