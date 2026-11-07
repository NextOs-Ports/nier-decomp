// Export the original, unpatched image using the external ReXGlue loader.
// No encryption key or commercial bytes are embedded in this tool.
#include <rex/runtime.h>
#include <rex/kernel/init.h>
#include <rex/system/xex_module.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <filesystem>

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "Usage: nier_dump_pe input.xex output.pe\n";
    return 2;
  }
  const auto input = std::filesystem::canonical(argv[1]);
  std::ifstream stream(input, std::ios::binary);
  const std::vector<char> bytes((std::istreambuf_iterator<char>(stream)), {});
  if (bytes.size() < 24) return 3;
  rex::Runtime runtime(input.parent_path().string());
  if (runtime.Setup({.kernel_init = rex::kernel::InitializeKernel, .tool_mode = true}) != 0) return 4;
  rex::runtime::XexModule module(runtime.function_dispatcher(), runtime.kernel_state());
  if (!module.Load(input.filename().string(), input.string(), bytes.data(), bytes.size())) return 5;
  // Save BEFORE LoadContinue patches imported variable slots. File offset equals RVA.
  std::ofstream out(argv[2], std::ios::binary);
  out.write(reinterpret_cast<const char*>(runtime.memory()->TranslateVirtual(module.base_address())),
            module.xex_security_info()->image_size);
  out.close();
  if (!out) return 6;
  std::cout << "image_base=" << module.base_address() << " entry=" << module.entry_point()
            << " image_size=" << module.xex_security_info()->image_size
            << " key=" << (module.is_dev_kit() ? "devkit" : "retail") << '\n';
  return 0;
}
