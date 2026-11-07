#include <nier/decomp.h>
#include <sys/mman.h>
#include <array>
#include <cfenv>
#include <cstdio>
#include <cstring>
using namespace nier::guest;
#define PAIR(n) extern "C" void __imp__sub_##n(PPCContext&,uint8_t*); extern "C" void decomp_sub_##n(PPCContext&,uint8_t*);
PAIR(824C1B30) PAIR(824C1B40) PAIR(824C1B58) PAIR(824C1BD8) PAIR(824C1BE8) PAIR(824C1C10)
PAIR(824C1C38) PAIR(824C1C80) PAIR(824C1A78) PAIR(824D64F8) PAIR(824DB878)
namespace nierverify {int add(const char*,uint32_t,PPCFunc*,PPCFunc*,const char*,const char*){return 0;}}
struct Function {const char* name;PPCFunc* original;PPCFunc* native;};
#define ENTRY(n) Function{#n,__imp__sub_##n,decomp_sub_##n}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    auto allocation=mmap(nullptr,(1ull<<32)+0x10000,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE,-1,0);
    if(allocation==MAP_FAILED)return 3;
    auto* base=reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(allocation)+0xffff)&~uintptr_t(0xffff));
    constexpr uint32_t image=0x82000000,bytes=0x4090000,sp=0x70004000,arena=0x40000000,p=arena+0x100;
    if(mprotect(base+image,bytes,PROT_READ|PROT_WRITE)||mprotect(base+0x70000000,0x10000,PROT_READ|PROT_WRITE)||
       mprotect(base+arena,0x10000,PROT_READ|PROT_WRITE))return 4;
    FILE* file=std::fopen(argv[1],"rb");if(!file)return 5;
    if(std::fread(base+image,1,bytes,file)!=bytes)return 6;std::fclose(file);mprotect(base+image,bytes,PROT_READ);
    unsigned tested=0,failed=0;
    auto check=[&](const Function& function,uint64_t r3,uint64_t r4=0,uint64_t r5=0,uint64_t r6=0,int mode=FE_TONEAREST) {
        std::array<uint8_t,0x5000> before{},expected{};std::memcpy(before.data(),base+arena,before.size());
        PPCContext a{},b{};a.r1.u64=b.r1.u64=sp;a.r3.u64=b.r3.u64=r3;a.r4.u64=b.r4.u64=r4;
        a.r5.u64=b.r5.u64=r5;a.r6.u64=b.r6.u64=r6;a.f1.f64=b.f1.f64=1.25;
        std::fesetround(mode);function.original(a,base);std::memcpy(expected.data(),base+arena,expected.size());
        std::memcpy(base+arena,before.data(),before.size());std::fesetround(mode);function.native(b,base);++tested;
        if(a.r3.u64!=b.r3.u64 || a.f1.u64!=b.f1.u64 || a.r1.u64!=b.r1.u64 ||
           std::memcmp(expected.data(),base+arena,expected.size())) {
          ++failed;if(failed<=30)std::printf("FAIL %s case=%u mode=%d r3=%016llx/%016llx f1=%016llx/%016llx data=%d\n",
            function.name,tested,mode,(unsigned long long)a.r3.u64,(unsigned long long)b.r3.u64,
            (unsigned long long)a.f1.u64,(unsigned long long)b.f1.u64,std::memcmp(expected.data(),base+arena,expected.size()));
        }
    };
    constexpr uint64_t high=0x1234567800000000ull;
    const uint32_t bits[]{0,1,0xffffffffu,0x7fffffffu,0x80000000u,0x7fffu,0x8000u,0x3f800000u,0xbf800000u,
        0x3f000000u,0x3fc00000u,0xbfc00000u,0x00800000u,0x7f800000u,0xff800000u,0x7fa12345u};
    for(uint32_t value:bits)for(unsigned index:{0u,1u,3u,16u}) {
        for(unsigned i=0;i<256;i+=4)st32(base,p+i,value);
        for(const auto& function:{ENTRY(824C1B30),ENTRY(824C1B40),ENTRY(824C1B58)})check(function,high|p,0,0,index);
        for(const auto& function:{ENTRY(824C1BD8),ENTRY(824C1BE8),ENTRY(824C1C10)})check(function,high|value,p,0,index);
    }
    for(int mode:{FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO})for(uint32_t value:bits)for(unsigned depth:{0u,1u,8u,16u,20u,24u,31u,32u,33u,64u}) {
        st16(base,p+110,depth);st32(base,arena+0x2000,value);
        check(ENTRY(824C1C38),high|value,arena+0x3000,p,0,mode);
        check(ENTRY(824C1C80),high|(arena+0x2000),0,depth,0,mode);
    }
    for(int mode:{FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO})for(int step=-100;step<=100;++step)
        check(ENTRY(824D64F8),high|uint32_t(step),0,0,0,mode);
    for(int count:{-1,0,1,16})for(uint32_t factor:bits)check(ENTRY(824DB878),high|p,uint32_t(count),factor);
    for(int mode:{FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO})for(unsigned channels:{0u,1u,3u})for(unsigned frames:{0u,1u,4u})for(unsigned depth:{1u,16u,24u,32u}) {
        std::memset(base+arena,0,0x5000);st16(base,p+34,channels);st16(base,p+110,depth);st32(base,p+320,arena+0x1000);
        for(unsigned channel=0;channel<3;++channel) {
            st32(base,arena+0x1000+channel*1776+60,arena+0x3000+channel*64);
            for(unsigned i=0;i<4;++i)stf(base,arena+0x3000+channel*64+i*4,float(int(i)-2)*1024);
        }
        check(ENTRY(824C1A78),high|p,arena+0x4000,frames,0,mode);
    }
    std::fesetround(FE_TONEAREST);std::printf("Audio samples: %u cases, %u failed\n",tested,failed);return failed?1:0;
}
