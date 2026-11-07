#include <rex/ppc.h>
#include <rex/ppc/context.h>
#include <sys/mman.h>
#include <array>
#include <cfenv>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>
#define PAIR(n) extern "C" void __imp__sub_##n(PPCContext&,uint8_t*); extern "C" void decomp_sub_##n(PPCContext&,uint8_t*);
PAIR(825DE3D0) PAIR(825D9258) PAIR(825DBF00) PAIR(825E9678) PAIR(825DCC78) PAIR(825D4FC8) PAIR(825E5270)
namespace nierverify {int add(const char*,uint32_t,PPCFunc*,PPCFunc*,const char*,const char*){return 0;}}
struct Function {const char* name;PPCFunc* original;PPCFunc* native;};
#define ENTRY(n) Function{#n,__imp__sub_##n,decomp_sub_##n}
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
    unsigned tested=0,failed=0;std::array<uint8_t,0x400> initial{};
    auto compare=[&](Function function,uint64_t r3,uint64_t r4,uint64_t r5,uint64_t f1,uint64_t f2,int mode=FE_TONEAREST) {
        PPCContext a{},b{};a.r1.u64=b.r1.u64=sp;a.r13.u64=b.r13.u64=0x50000100;
        a.r3.u64=b.r3.u64=r3;a.r4.u64=b.r4.u64=r4;a.r5.u64=b.r5.u64=r5;
        a.f1.u64=b.f1.u64=f1;a.f2.u64=b.f2.u64=f2;
        std::array<uint8_t,64> original_stack{};std::array<uint8_t,0x400> original_data{};
        auto reset=[&]{std::memset(base+sp,0xa5,64);std::memcpy(base+arena,initial.data(),initial.size());
          std::fesetround(mode);std::feclearexcept(FE_ALL_EXCEPT);};
        reset();function.original(a,base);std::memcpy(original_stack.data(),base+sp,64);std::memcpy(original_data.data(),base+arena,0x400);
        reset();function.native(b,base);++tested;
        if(a.r3.u64!=b.r3.u64 || a.f1.u64!=b.f1.u64 || a.r1.u64!=b.r1.u64 ||
           std::memcmp(original_stack.data(),base+sp,64)||std::memcmp(original_data.data(),base+arena,0x400)) {
          ++failed;if(failed<=20)std::printf("FAIL %s case=%u mode=%d f1=%016llx f2=%016llx result=%016llx/%016llx r3=%016llx/%016llx\n",
            function.name,tested,mode,(unsigned long long)f1,(unsigned long long)f2,(unsigned long long)a.f1.u64,(unsigned long long)b.f1.u64,
            (unsigned long long)a.r3.u64,(unsigned long long)b.r3.u64);
        }
    };
    const uint64_t fp[]{0,0x8000000000000000ull,1,0x8000000000000001ull,0x000fffffffffffffull,0x800fffffffffffffull,
      0x0010000000000000ull,0x8010000000000000ull,0x3ff0000000000000ull,0xbff123456789abcdull,
      0x4008000000000000ull,0x4031000000000000ull,0x7fefffffffffffffull,0xffefffffffffffffull,
      0x7ff0000000000000ull,0xfff0000000000000ull,0x7ff8000012345678ull,0xfff8000012345678ull,
      0x7ff0000000000001ull,0xfff0000000000001ull};
    for(int mode:{FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO})for(uint64_t x:fp) {
        compare(ENTRY(825DE3D0),0,0,0,x,0,mode);
        for(uint64_t y:fp)compare(ENTRY(825D9258),0x123456789abcdef0ull,0,0,x,y,mode);
    }
    std::mt19937_64 random(0x825d9258);
    for(unsigned i=0;i<4000;++i)compare(ENTRY(825D9258),0,0,0,random(),random());
    for(const char* str:{"","123","1.","1.0","1.000","1.0100","0.000e+00","1.2300E-17","1.000e-9",".000","-12.34000e+300","0.00000100"}) {
        initial.fill(0xa5);std::strcpy(reinterpret_cast<char*>(initial.data()+0x100),str);
        compare(ENTRY(825DBF00),0x1234567840000100ull,0,0,0,0);
    }
    for(unsigned ch:{0u,1u,65u,127u,128u,255u})for(unsigned count:{0u,1u,2u})for(bool output:{false,true})for(bool source:{false,true}) {
        initial.fill(0xa5);initial[0x200]=ch;
        compare(ENTRY(825E9678),output?arena+0x100:0,source?arena+0x200:0,count,0,0);
    }
    for(int ch=-2;ch<=257;++ch)for(uint64_t mask:{0ull,1ull,4ull,8ull,0xffffffffffffffffull})
        compare(ENTRY(825DCC78),uint32_t(ch),mask,0,0,0);
    for(unsigned error=0;error<512;++error)compare(ENTRY(825D4FC8),error,0,0,0,0);
    for(unsigned i=0;i<45;++i) {uint32_t error;std::memcpy(&error,base+0x8278e350+i*8,4);
        compare(ENTRY(825D4FC8),__builtin_bswap32(error),0,0,0,0);}
    for(unsigned mask=0;mask<64;++mask)compare(ENTRY(825E5270),mask,0,0,0,0);
    std::fesetround(FE_TONEAREST);std::printf("CRT contracts: %u cases, %u failed (IEEE remainder, classifications, locale, strings, null outputs)\n",tested,failed);
    return failed?1:0;
}
