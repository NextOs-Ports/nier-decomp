#include <nier/decomp.h>
#include <sys/mman.h>
#include <array>
#include <cstdio>
#include <cstring>
using namespace nier::guest;
#define PAIR(n) extern "C" void __imp__sub_##n(PPCContext&,uint8_t*); extern "C" void decomp_sub_##n(PPCContext&,uint8_t*);
PAIR(825E7B88) PAIR(825E82A0)
namespace nierverify {int add(const char*,uint32_t,PPCFunc*,PPCFunc*,const char*,const char*){return 0;}}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    auto allocation=mmap(nullptr,(1ull<<32)+0x10000,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE,-1,0);
    if(allocation==MAP_FAILED)return 3;
    auto* base=reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(allocation)+0xffff)&~uintptr_t(0xffff));
    constexpr uint32_t image=0x82000000,bytes=0x4090000,sp=0x70004000,arena=0x40000000;
    if(mprotect(base+image,bytes,PROT_READ|PROT_WRITE)||mprotect(base+0x70000000,0x10000,PROT_READ|PROT_WRITE)||
       mprotect(base+arena,0x10000,PROT_READ|PROT_WRITE))return 4;
    FILE* file=std::fopen(argv[1],"rb");if(!file)return 5;
    if(std::fread(base+image,1,bytes,file)!=bytes)return 6;std::fclose(file);mprotect(base+image,bytes,PROT_READ);
    using Wide=unsigned __int128;const Wide one=Wide(1)<<95;
    const Wide patterns[]{0,one,(Wide(1)<<96)-(Wide(1)<<16),one|(Wide(1)<<42),one|(Wide(1)<<42)|(Wide(1)<<16),
        one|(Wide(1)<<43)|(Wide(1)<<42),one|(Wide(1)<<71),one|(Wide(1)<<71)|(Wide(1)<<16),
        one|(Wide(1)<<72)|(Wide(1)<<71),Wide(1)<<16,one-(Wide(1)<<16)};
    PPCFunc* const original[]{__imp__sub_825E7B88,__imp__sub_825E82A0};
    PPCFunc* const native[]{decomp_sub_825E7B88,decomp_sub_825E82A0};
    unsigned tested=0,failed=0;
    for(unsigned fn=0;fn<2;++fn)for(unsigned exponent=0;exponent<32768;++exponent)
      for(unsigned sign=0;sign<2;++sign)for(unsigned pattern=0;pattern<std::size(patterns);++pattern) {
        const Wide significand=patterns[pattern];st16(base,arena,exponent|(sign<<15));
        st64(base,arena+2,uint64_t(significand>>32));st16(base,arena+10,uint16_t(significand>>16));
        PPCContext a{},b{};a.r1.u64=b.r1.u64=sp;a.r3.u64=b.r3.u64=arena;a.r4.u64=b.r4.u64=arena+0x100;
        a.f1.f64=b.f1.f64=1.25;
        std::array<uint8_t,32> expected{};std::memset(base+arena+0x100,0xa5,32);
        original[fn](a,base);std::memcpy(expected.data(),base+arena+0x100,32);
        std::memset(base+arena+0x100,0xa5,32);native[fn](b,base);++tested;
        if(a.r3.u64!=b.r3.u64 || a.f1.u64!=b.f1.u64 || a.r1.u64!=b.r1.u64 ||
           std::memcmp(expected.data(),base+arena+0x100,32)) {
          uint64_t expected_word;std::memcpy(&expected_word,expected.data(),8);
          ++failed;if(failed<=20)std::printf("FAIL fn=%u exp=%u sign=%u pattern=%u r3=%llu/%llu output=%016llx/%016llx\n",
            fn,exponent,sign,pattern,(unsigned long long)a.r3.u64,(unsigned long long)b.r3.u64,
            (unsigned long long)__builtin_bswap64(expected_word),
            (unsigned long long)ld64(base,arena+0x100));
        }
      }
    std::printf("Extended conversion: %u cases, %u failed (all 32768 exponents, both signs, 11 significands, two widths)\n",tested,failed);
    return failed?1:0;
}
