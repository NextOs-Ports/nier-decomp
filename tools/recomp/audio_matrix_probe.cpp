#include <nier/decomp.h>
#include <sys/mman.h>
#include <array>
#include <algorithm>
#include <cfenv>
#include <chrono>
#include <cstdio>
#include <cstring>
using namespace nier::guest;
#define PAIR(n) extern "C" void __imp__sub_##n(PPCContext&,uint8_t*); extern "C" void decomp_sub_##n(PPCContext&,uint8_t*);
PAIR(824B8498) PAIR(824B9BF8) PAIR(824B85A0) PAIR(824B9D38) PAIR(824B8778) PAIR(824B9F68) PAIR(824BFB60)
namespace nierverify {int add(const char*,uint32_t,PPCFunc*,PPCFunc*,const char*,const char*){return 0;}}
struct Function {const char* name;PPCFunc* original;PPCFunc* native;unsigned inputs,outputs;bool pcm;};
#define ENTRY(n,i,o,p) Function{#n,__imp__sub_##n,decomp_sub_##n,i,o,p}
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
    auto check=[&](const Function& function,int mode) {
        std::array<uint8_t,0x4000> before{},expected{};std::memcpy(before.data(),base+arena,before.size());
        PPCContext a{},b{};a.r1.u64=b.r1.u64=sp;a.r3.u64=b.r3.u64=0x1234567840000100ull;
        a.f1.f64=b.f1.f64=1.25;
        std::fesetround(mode);function.original(a,base);std::memcpy(expected.data(),base+arena,expected.size());
        std::memcpy(base+arena,before.data(),before.size());std::fesetround(mode);function.native(b,base);++tested;
        if(a.r3.u64!=b.r3.u64 || a.f1.u64!=b.f1.u64 || a.r1.u64!=b.r1.u64 ||
           std::memcmp(expected.data(),base+arena,expected.size())) {
          ++failed;if(failed<=20)std::printf("FAIL %s case=%u mode=%d f1=%016llx/%016llx data=%d\n",
            function.name,tested,mode,(unsigned long long)a.f1.u64,(unsigned long long)b.f1.u64,
            std::memcmp(expected.data(),base+arena,expected.size()));
        }
    };
    const Function matrix[]{ENTRY(824B8498,1,2,false),ENTRY(824B9BF8,1,2,true),ENTRY(824B85A0,1,6,false),
        ENTRY(824B9D38,1,6,true),ENTRY(824B8778,2,6,false),ENTRY(824B9F68,2,6,true)};
    const uint32_t pattern[]{0,0x80000000,0x3f800000,0xbf800000,0x3e000000,0xbe000000,1,0x80000001,0x00800000,0x80800000};
    const int16_t pcm[]{0,-32768,32767,-1,1,1234,-1234};
    auto initialize=[&](const Function& function,unsigned count,bool ramp,bool accumulate,unsigned phase) {
        std::memset(base+arena,0,0x4000);
        st32(base,p,arena+0x1000);st32(base,p+4,arena+0x2000);st32(base,p+8,arena+0x3000);
        st32(base,p+12,ramp?arena+0x3100:0);st32(base,p+20,count);st32(base,p+32,accumulate);
        for(unsigned i=0;i<256;++i) {
            if(function.pcm)st16(base,arena+0x1000+i*2,uint16_t(pcm[(i+phase)%std::size(pcm)]));
            else st32(base,arena+0x1000+i*4,pattern[(i+phase)%std::size(pattern)]);
        }
        for(unsigned i=0;i<768;++i)stf(base,arena+0x2000+i*4,float(int(i%3)-1)*.25f);
        for(unsigned i=0;i<12;++i) {stf(base,arena+0x3000+i*4,float(int((i+phase)%5)-2)*.125f);
            stf(base,arena+0x3100+i*4,float(int((i+phase)%3)-1)*.015625f);}
    };
    for(const auto& function:matrix)for(int mode:{FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO})
      for(unsigned count:{0u,1u,2u,15u,16u})for(bool ramp:{false,true})for(bool accumulate:{false,true})for(unsigned phase=0;phase<3;++phase) {
        initialize(function,count,ramp,accumulate,phase);check(function,mode);
      }
    const Function filter=ENTRY(824BFB60,2,2,false);
    for(int mode:{FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO})for(unsigned count:{0u,1u,2u,15u,16u})
      for(unsigned output=0;output<5;++output)for(unsigned phase=0;phase<3;++phase) {
        initialize(filter,count,false,false,phase);st32(base,p+12,arena+0x3010);st32(base,p+16,count);st32(base,p+24,output);
        stf(base,p+28,.25f);stf(base,p+32,.5f);check(filter,mode);
      }
    std::fesetround(FE_TONEAREST);
    std::printf("Audio matrix/filter: %u cases, %u failed\n",tested,failed);
    if(failed)return 1;
    for(const auto& function:matrix)for(bool tiny:{false,true}) {
        auto bench=[&](PPCFunc* fn) {
            initialize(function,128,true,false,0);PPCContext ctx{};ctx.r1.u64=sp;ctx.r3.u64=p;
            if(!tiny && !function.pcm)for(unsigned i=0;i<256;++i)stf(base,arena+0x1000+i*4,float(int(i%7)-3)*.125f);
            auto start=std::chrono::steady_clock::now();for(unsigned i=0;i<10000;++i)fn(ctx,base);
            return std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count()/10000;
        };
        std::array<double,5> original{},native{};
        for(unsigned i=0;i<5;++i) {original[i]=bench(function.original);native[i]=bench(function.native);}
        std::sort(original.begin(),original.end());std::sort(native.begin(),native.end());
        std::printf("BENCH %s %s 128 samples/ramp/overwrite: original %.3f us native %.3f us ratio %.2fx (host median 5x10000 calls)\n",
            function.name,tiny?"including-subnormals":"normalized",original[2],native[2],original[2]/native[2]);
        if(function.pcm)break;
    }
    return 0;
}
