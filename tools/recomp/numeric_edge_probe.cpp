#include <rex/ppc.h>
#include <rex/ppc/context.h>
#include <sys/mman.h>
#include <array>
#include <bit>
#include <cfenv>
#include <cstdio>
#include <cstring>
#include <vector>
#define PAIR(n) extern "C" void __imp__sub_##n(PPCContext&,uint8_t*); extern "C" void decomp_sub_##n(PPCContext&,uint8_t*);
PAIR(825DAAF0) PAIR(825D91D8) PAIR(825D9218) PAIR(825E4B80) PAIR(825D93E0)
namespace nierverify {int add(const char*,uint32_t,PPCFunc*,PPCFunc*,const char*,const char*){return 0;}}
struct Function {const char* name;PPCFunc* original;PPCFunc* native;bool integer;};
#define ENTRY(n,i) {#n,__imp__sub_##n,decomp_sub_##n,i}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    auto allocation=mmap(nullptr,(1ull<<32)+0x10000,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE,-1,0);
    if(allocation==MAP_FAILED)return 3;
    auto* base=reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(allocation)+0xffff)&~uintptr_t(0xffff));
    constexpr uint32_t image=0x82000000,bytes=0x4090000,stack=0x70000000,sp=stack+0x4000;
    if(mprotect(base+image,bytes,PROT_READ|PROT_WRITE)||mprotect(base+stack,0x10000,PROT_READ|PROT_WRITE))return 4;
    FILE* file=std::fopen(argv[1],"rb");if(!file)return 5;
    if(std::fread(base+image,1,bytes,file)!=bytes)return 6;std::fclose(file);
    mprotect(base+image,bytes,PROT_READ);
    const Function functions[]{ENTRY(825DAAF0,true),ENTRY(825D91D8,false),ENTRY(825D9218,false),ENTRY(825E4B80,false),ENTRY(825D93E0,false)};
    const uint64_t floating[]{0,0x8000000000000000ull,1,0x8000000000000001ull,
      0x000fffffffffffffull,0x800fffffffffffffull,0x3fb999999999999aull,0xbfb999999999999aull,
      0x3fe0000000000000ull,0xbfe0000000000000ull,0x3ff0000000000000ull,0xbff0000000000000ull,
      0x3ff8000000000000ull,0xbff8000000000000ull,0x432fffffffffffffull,0xc32fffffffffffffull,
      0x4330000000000000ull,0xc330000000000000ull,0x43e0000000000000ull,0xc3e0000000000000ull,
      0x7fefffffffffffffull,0xffefffffffffffffull,0x7ff0000000000000ull,0xfff0000000000000ull,
      0x7ff8000012345678ull,0xfff8000012345678ull,0x7ff0000000000001ull,0xfff0000000000001ull};
    const uint64_t integers[]{0,1,2,3,0xffffffffull,0x100000000ull,0x1fffffffffffffull,
      0x20000000000000ull,0x20000000000001ull,0x7fffffffffffffffull,0x8000000000000000ull,
      0xffffffffffffffffull,0x123456789abcdef0ull,0x0000000100000001ull};
    unsigned tested=0,failed=0;
    for(const auto& function:functions)for(int mode:{FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO}) {
      const uint64_t* inputs=function.integer?integers:floating;
      const size_t count=function.integer?std::size(integers):std::size(floating);
      for(size_t i=0;i<count;++i) {
        PPCContext a{},b{};a.r1.u64=b.r1.u64=sp;a.r13.u64=b.r13.u64=0x50000100;
        a.r3.u64=b.r3.u64=function.integer?inputs[i]:0x123456789abcdef0ull;
        a.f1.u64=b.f1.u64=function.integer?0:inputs[i];
        std::array<uint8_t,64> original_stack{};
        std::memset(base+sp,0xa5,64);std::fesetround(mode);std::feclearexcept(FE_ALL_EXCEPT);
        function.original(a,base);std::memcpy(original_stack.data(),base+sp,64);
        std::memset(base+sp,0xa5,64);std::fesetround(mode);std::feclearexcept(FE_ALL_EXCEPT);
        function.native(b,base);
        ++tested;
        if(a.r3.u64!=b.r3.u64 || a.f1.u64!=b.f1.u64 || a.r1.u64!=b.r1.u64 ||
           std::memcmp(original_stack.data(),base+sp,64)) {
          ++failed;std::printf("FAIL %s mode=%d input=%016llx r3=%016llx/%016llx f1=%016llx/%016llx\n",
            function.name,mode,(unsigned long long)inputs[i],(unsigned long long)a.r3.u64,(unsigned long long)b.r3.u64,
            (unsigned long long)a.f1.u64,(unsigned long long)b.f1.u64);
        }
      }
    }
    std::fesetround(FE_TONEAREST);
    std::printf("numeric edges: %u cases, %u failed (four rounding modes, zeros, subnormals, infinity/NaN, integer limits)\n",tested,failed);
    return failed?1:0;
}
