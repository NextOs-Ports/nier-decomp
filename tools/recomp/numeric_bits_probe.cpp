#include <rex/ppc.h>
#include <rex/ppc/context.h>
#include <sys/mman.h>
#include <array>
#include <cfenv>
#include <cstdio>
#include <cstring>
#include <vector>
#define PAIR(n) extern "C" void __imp__sub_##n(PPCContext&,uint8_t*); extern "C" void decomp_sub_##n(PPCContext&,uint8_t*);
PAIR(825E4B40) PAIR(825DE390) PAIR(825DE458) PAIR(825E79C0)
namespace nierverify {int add(const char*,uint32_t,PPCFunc*,PPCFunc*,const char*,const char*){return 0;}}
struct Function {const char* name;PPCFunc* original;PPCFunc* native;};
#define ENTRY(n) {#n,__imp__sub_##n,decomp_sub_##n}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    auto allocation=mmap(nullptr,(1ull<<32)+0x10000,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE,-1,0);
    if(allocation==MAP_FAILED)return 3;
    auto* base=reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(allocation)+0xffff)&~uintptr_t(0xffff));
    constexpr uint32_t image=0x82000000,bytes=0x4090000,stack=0x70000000,sp=stack+0x4000,arena=0x40000000;
    if(mprotect(base+image,bytes,PROT_READ|PROT_WRITE)||mprotect(base+stack,0x10000,PROT_READ|PROT_WRITE)||
       mprotect(base+arena,0x10000,PROT_READ|PROT_WRITE))return 4;
    FILE* file=std::fopen(argv[1],"rb");if(!file)return 5;
    if(std::fread(base+image,1,bytes,file)!=bytes)return 6;std::fclose(file);
    mprotect(base+image,bytes,PROT_READ);
    const Function functions[]{ENTRY(825E4B40),ENTRY(825DE390),ENTRY(825DE458),ENTRY(825E79C0)};
    std::vector<uint64_t> inputs{0,0x8000000000000000ull,0x000fffffffffffffull,0x800fffffffffffffull,
      0x0010000000000000ull,0x8010000000000000ull,0x3ff0000000000000ull,0xbff123456789abcdull,
      0x7fefffffffffffffull,0xffefffffffffffffull,0x7ff0000000000000ull,0xfff0000000000000ull,
      0x7ff8000012345678ull,0xfff8000012345678ull,0x7ff0000000000001ull,0xfff0000000000001ull};
    for(unsigned bit=0;bit<52;++bit) {inputs.push_back(1ull<<bit);inputs.push_back((1ull<<bit)|(1ull<<63));}
    const uint32_t exponents[]{0,1,0xffffffffu,1023,0xfffffc01u,0x7fffffffu,0x80000000u,0x0000ffffu};
    unsigned tested=0,failed=0;
    for(unsigned fn=0;fn<std::size(functions);++fn)for(int mode:{FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO}) {
      const auto& function=functions[fn];
      for(uint64_t input:inputs)for(unsigned variant=0;variant<(fn==1?std::size(exponents):2);++variant) {
        PPCContext a{},b{};a.r1.u64=b.r1.u64=sp;a.r13.u64=b.r13.u64=0x50000100;
        a.r3.u64=b.r3.u64=0x1234567840000100ull;
        a.r4.u64=b.r4.u64=fn==1?exponents[variant]:arena+0x200;
        a.f1.u64=b.f1.u64=input;a.f2.u64=b.f2.u64=variant?0xfff123456789abcdull:0x7ff0000000000001ull;
        std::array<uint8_t,64> original_stack{};std::array<uint8_t,0x400> original_data{};
        auto reset=[&]{std::memset(base+sp,0xa5,64);std::memset(base+arena,0x5a,0x400);
          const uint64_t be=__builtin_bswap64(input);std::memcpy(base+arena+0x200,&be,8);
          std::fesetround(mode);std::feclearexcept(FE_ALL_EXCEPT);};
        reset();function.original(a,base);std::memcpy(original_stack.data(),base+sp,64);std::memcpy(original_data.data(),base+arena,0x400);
        reset();function.native(b,base);++tested;
        if(a.r3.u64!=b.r3.u64 || a.f1.u64!=b.f1.u64 || a.r1.u64!=b.r1.u64 ||
           std::memcmp(original_stack.data(),base+sp,64)||std::memcmp(original_data.data(),base+arena,0x400)) {
          ++failed;std::printf("FAIL %s mode=%d input=%016llx variant=%u f1=%016llx/%016llx stack=%d data=%d\n",
            function.name,mode,(unsigned long long)input,variant,(unsigned long long)a.f1.u64,(unsigned long long)b.f1.u64,
            std::memcmp(original_stack.data(),base+sp,64),std::memcmp(original_data.data(),base+arena,0x400));
        }
      }
    }
    std::fesetround(FE_TONEAREST);
    std::printf("numeric bit edges: %u cases, %u failed (four rounding modes, all subnormal leading bits, signs, NaN payloads, exponent wrap)\n",tested,failed);
    return failed?1:0;
}
