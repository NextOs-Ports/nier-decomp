#include <rex/runtime.h>
#include <rex/kernel/init.h>
#include <rex/system/xam/content_manager.h>
#include <rex/audio/conversion.h>
#include <rex/audio/downmix.h>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

int main(int argc,char** argv) {
    if (argc!=4) return 2;
    alignas(16) std::array<float,6*256> guest{};
    std::array<float,2*256> stereo{};
    for (unsigned i=0;i<256;++i) {
        guest[i]=std::bit_cast<float>(rex::byte_swap(std::bit_cast<uint32_t>(0.25f)));
        guest[256+i]=std::bit_cast<float>(rex::byte_swap(std::bit_cast<uint32_t>(-0.125f)));
    }
    rex::audio::StereoFold fold;
    fold.scale=1.0f;
    rex::audio::conversion::sequential_6_BE_to_interleaved_2_LE(stereo.data(),guest.data(),256,fold,1.0f);
    for (unsigned i=0;i<256;++i)
        if (stereo[2*i]!=0.25f || stereo[2*i+1]!=-0.125f) return 3;
    std::puts("PASS audio BE-planar 5.1 -> LE-interleaved stereo, front gain exactly 1.0");
    auto root=std::filesystem::absolute(argv[1]);
    std::filesystem::create_directories(root);
    rex::Runtime runtime(std::filesystem::absolute(argv[3]),root);
    if (runtime.Setup({.kernel_init=rex::kernel::InitializeKernel,.tool_mode=true})!=0) return 4;
    if (runtime.LoadXexImage("game:\\default.xex")!=0) return 13;
    auto* content=runtime.kernel_state()->content_manager();
    rex::system::xam::XCONTENT_AGGREGATE_DATA data{};
    data.title_id=0x535107E8;
    data.xuid=0x0000000000000001ULL;
    data.content_type=rex::system::XContentType::kSavedGame;
    data.set_file_name("NierRuntimeProbe");
    data.set_display_name(u"Synthetic runtime persistence probe");
    const bool create=std::string(argv[2])=="create";
    uint32_t license=0;
    if (create) {
        if (content->ContentExists(1,data)) return 5; // Never overwrite an existing fixture.
        if (content->CreateContent("probe",1,data)!=0) return 6;
    } else if (content->OpenContent("probe",1,data,license)!=0) return 7;
    auto package=content->ResolvePackage("probecheck",1,data);
    if (!package) return 8;
    const auto path=package->package_path()/"roundtrip.txt";
    if (create) {
        std::ofstream output(path,std::ios::binary);
        output << "nier-runtime-persistence-v1\n";
        output.close();
        if (!output) return 9;
    }
    std::ifstream input(path,std::ios::binary);
    std::string value;
    std::getline(input,value);
    if (value!="nier-runtime-persistence-v1") return 10;
    if (!runtime.file_system()->ResolvePath("probe:\\roundtrip.txt")) return 11;
    if (content->CloseContent("probe")!=0) return 12;
    std::printf("PASS XContent %s and VFS payload: %s\n",create?"create":"reopen",path.c_str());
    return 0;
}
