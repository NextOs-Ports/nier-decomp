#include <nier/decomp.h>
#include <sys/mman.h>
#include <array>
#include <bit>
#include <cfenv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>
using namespace nier::guest;
#define PAIR(n) extern "C" void __imp__sub_##n(PPCContext&,uint8_t*); extern "C" void decomp_sub_##n(PPCContext&,uint8_t*);
PAIR(825D8E50) PAIR(825D8F30) PAIR(825D9010) PAIR(825D9108) PAIR(825D9AC0) PAIR(825D4568)
namespace nierverify {int add(const char*,uint32_t,PPCFunc*,PPCFunc*,const char*,const char*){return 0;}}
struct Function {const char* name;PPCFunc* original;PPCFunc* native;};
#define ENTRY(n) Function{#n,__imp__sub_##n,decomp_sub_##n}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    auto allocation=mmap(nullptr,(1ull<<32)+0x10000,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE,-1,0);
    if(allocation==MAP_FAILED)return 3;
    auto* base=reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(allocation)+0xffff)&~uintptr_t(0xffff));
    constexpr uint32_t image=0x82000000,bytes=0x4090000,sp=0x70004000;
    if(mprotect(base+image,bytes,PROT_READ|PROT_WRITE)||mprotect(base+0x70000000,0x10000,PROT_READ|PROT_WRITE))return 4;
    FILE* file=std::fopen(argv[1],"rb");if(!file)return 5;
    if(std::fread(base+image,1,bytes,file)!=bytes)return 6;std::fclose(file);mprotect(base+image,bytes,PROT_READ);
    std::vector<uint64_t> values{0,0x8000000000000000ull,1,0x8000000000000001ull,0x000fffffffffffffull,0x800fffffffffffffull,
        0x0010000000000000ull,0x8010000000000000ull,0x7ff0000000000000ull,0xfff0000000000000ull,
        0x7ff8000012345678ull,0xfff8000012345678ull,0x7ff0000000000001ull,0xfff0000000000001ull,
        0x7fefffffffffffffull,0xffefffffffffffffull};
    for(double v:{.5,1.,1.5707963267948966,3.141592653589793,220000000.,0x1p63,709.782712893385,708.396418532265})for(double x:{std::nextafter(v,0.),v,std::nextafter(v,INFINITY)})
        {values.push_back(std::bit_cast<uint64_t>(x));values.push_back(std::bit_cast<uint64_t>(-x));}
    for(unsigned bit=0;bit<52;++bit)for(uint64_t sign:{0ull,0x8000000000000000ull})values.push_back(sign|0x7ff0000000000000ull|(1ull<<bit));
    std::mt19937_64 random(0x825d8e50);for(unsigned i=0;i<1000;++i)values.push_back(random());
    const Function functions[]{ENTRY(825D8E50),ENTRY(825D8F30),ENTRY(825D9010),ENTRY(825D9108),ENTRY(825D9AC0),ENTRY(825D4568)};
    unsigned tested=0,failed=0;
    for(const auto& function:functions)for(int mode:{FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO})
      for(unsigned selector=0;selector<2;++selector)for(uint64_t input:values) {
        PPCContext a{},b{};a.r1.u64=b.r1.u64=sp;a.r3.u64=b.r3.u64=0x123456789abcdef0ull;a.r4.u64=b.r4.u64=selector;
        a.f1.u64=b.f1.u64=input;
        std::array<uint8_t,64> expected{};std::memset(base+sp,0xa5,64);
        std::fesetround(mode);function.original(a,base);std::memcpy(expected.data(),base+sp,64);
        std::memset(base+sp,0xa5,64);std::fesetround(mode);function.native(b,base);++tested;
        if(a.r3.u64!=b.r3.u64 || a.f1.u64!=b.f1.u64 || a.r1.u64!=b.r1.u64 ||std::memcmp(expected.data(),base+sp,64)) {
          ++failed;if(failed<=100)std::printf("FAIL %s mode=%d selector=%u input=%016llx f1=%016llx/%016llx\n",
            function.name,mode,selector,(unsigned long long)input,(unsigned long long)a.f1.u64,(unsigned long long)b.f1.u64);
        }
      }
    std::fesetround(FE_TONEAREST);std::printf("CRT approximations: %u cases, %u failed\n",tested,failed);return failed?1:0;
}
