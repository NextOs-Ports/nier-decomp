#include <nier/decomp.h>
#include <sys/mman.h>
#include <array>
#include <cfenv>
#include <cstdio>
#include <cstring>
using namespace nier::guest;
#define PAIR(n) extern "C" void __imp__sub_##n(PPCContext&,uint8_t*); extern "C" void decomp_sub_##n(PPCContext&,uint8_t*);
PAIR(824BECB0) PAIR(824BEDD8) PAIR(824BEF58) PAIR(824BF0E8) PAIR(824BF2D0) PAIR(824BF4A0) PAIR(824BF630) PAIR(824BF7C0)
PAIR(824BF930) PAIR(824BFC50)
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
    unsigned tested=0,failed=0;
    auto check=[&](const Function& function,uint64_t r3,uint64_t r4,uint64_t r5,int mode) {
        std::array<uint8_t,0x4000> before{},expected{};std::memcpy(before.data(),base+arena,before.size());
        PPCContext a{},b{};a.r1.u64=b.r1.u64=sp;a.r3.u64=b.r3.u64=r3;a.r4.u64=b.r4.u64=r4;a.r5.u64=b.r5.u64=r5;
        a.f1.f64=b.f1.f64=1.25;
        std::fesetround(mode);function.original(a,base);std::memcpy(expected.data(),base+arena,expected.size());
        std::memcpy(base+arena,before.data(),before.size());std::fesetround(mode);function.native(b,base);++tested;
        if(a.r3.u64!=b.r3.u64 || a.f1.u64!=b.f1.u64 || a.r1.u64!=b.r1.u64 ||std::memcmp(expected.data(),base+arena,expected.size())) {
          ++failed;if(failed<=30)std::printf("FAIL %s case=%u mode=%d r3=%016llx/%016llx count=%llu data=%d\n",
            function.name,tested,mode,(unsigned long long)a.r3.u64,(unsigned long long)b.r3.u64,
            (unsigned long long)r5,std::memcmp(expected.data(),base+arena,expected.size()));
        }
    };
    const Function functions[]{ENTRY(824BECB0),ENTRY(824BEDD8),ENTRY(824BEF58),ENTRY(824BF0E8),ENTRY(824BF2D0),ENTRY(824BF4A0),ENTRY(824BF630),ENTRY(824BF7C0)};
    const uint32_t values[]{0,0x80000000,1,0x80000001,0x007fffff,0x807fffff,0x00800000,0x80800000,
        0x3f000000,0xbf000000,0x3f7ffffe,0x3f7fffff,0x3f800000,0xbf800000,0x3f800001,0xbf800001,
        0x7f800000,0xff800000,0x7f812345,0xff812345,0x7fcabcde,0xffcabcde,0x7f7fffff,0xff7fffff};
    for(const auto& function:functions)for(int mode:{FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO})
      for(unsigned count=0;count<=9;++count)for(unsigned alignment=0;alignment<4;++alignment)for(bool inplace:{false,true})for(unsigned phase=0;phase<std::size(values);++phase) {
        std::memset(base+arena,0xa5,0x4000);const uint32_t source=arena+0x1000+alignment;
        for(unsigned i=0;i<16;++i)st32(base,source+i*4,values[(phase+i)%std::size(values)]);
        const uint32_t destination=inplace?source:arena+0x3000+alignment;
        check(function,0x1234567800000000ull|destination,source,count,mode);
      }
    for(uint64_t high:{0ull,0x1234567800000000ull})for(uint32_t kind:{0u,1u,2u,3u,4u,5u,6u,7u,8u,0xffffffffu}) {
        check(ENTRY(824BF930),high|kind,0,0,FE_TONEAREST);check(ENTRY(824BFC50),high|kind,0,0,FE_TONEAREST);
    }
    std::fesetround(FE_TONEAREST);std::printf("Audio output: %u cases, %u failed (formats, clamps, tails, NaNs, exact in-place, alignments, four FP modes)\n",tested,failed);return failed?1:0;
}
