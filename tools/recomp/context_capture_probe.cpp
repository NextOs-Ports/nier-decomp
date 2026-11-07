#include <nier/decomp.h>
#include <sys/mman.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <random>
using namespace nier::guest;
extern "C" void __imp__sub_825D5DE0(PPCContext&,uint8_t*);
extern "C" void decomp_sub_825D5DE0(PPCContext&,uint8_t*);
namespace nierverify {int add(const char*,uint32_t,PPCFunc*,PPCFunc*,const char*,const char*){return 0;}}
static void capture_hook(PPCContext& ctx,uint8_t* base) {
    const uint32_t p=ctx.r3.u32;
    st64(base,p,ctx.r4.u64);st64(base,p+8,ctx.r5.u64);st64(base,p+16,ctx.r0.u64);
    st32(base,p+24,ctx.cr0.raw());ctx.r3.u64=0xabcdef0123456789ull;ctx.f1.f64=3.5;
}
namespace rex::runtime {PPCFunc* ResolveIndirectFunction(uint32_t address) {return address==0x1000?capture_hook:nullptr;}}
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
    mprotect(base+0x86010000,0x10000,PROT_READ|PROT_WRITE);
    std::mt19937_64 random(0x825d5de0);unsigned tested=0,failed=0;
    for(unsigned trial=0;trial<256;++trial)for(unsigned alignment=0;alignment<16;++alignment)for(bool hook:{false,true}) {
        PPCContext a{},b{};
        std::array<PPCRegister,32> integers{},floats{};std::array<PPCVRegister,64> vectors{};std::array<PPCCRRegister,8> cr{};
        for(auto& r:integers)r.u64=random();for(auto& f:floats)f.u64=random();
        for(auto& v:vectors)for(auto& byte:v.u8)byte=uint8_t(random());for(auto& field:cr)field.set_raw(random());
        std::memcpy(&a.r3,integers.data(),sizeof integers);std::memcpy(&a.f0,floats.data(),sizeof floats);
        std::memcpy(&a.v64,vectors.data(),sizeof vectors);std::memcpy(&a.cr0,cr.data(),sizeof cr);
        a.lr=random();a.xer.so=random()&1;a.r1.u64=sp;a.r3.u64=0x1234567840000100ull+alignment;b=a;
        st32(base,0x86018f04,hook?0x1000:0);
        std::array<uint8_t,0x1000> expected{};std::memset(base+arena,0xa5,expected.size());
        __imp__sub_825D5DE0(a,base);std::memcpy(expected.data(),base+arena,expected.size());
        std::memset(base+arena,0xa5,expected.size());decomp_sub_825D5DE0(b,base);++tested;
        if(a.r3.u64!=b.r3.u64 || a.f1.u64!=b.f1.u64 || a.r1.u64!=b.r1.u64 ||
           std::memcmp(expected.data(),base+arena,expected.size()) || std::memcmp(&a.r13,&b.r13,19*sizeof(PPCRegister)) ||
           std::memcmp(&a.f14,&b.f14,18*sizeof(PPCRegister)) || std::memcmp(&a.v64,&b.v64,64*sizeof(PPCVRegister))) {
          ++failed;if(failed<=20)std::printf("FAIL trial=%u align=%u hook=%d\n",trial,alignment,hook);
        }
    }
    std::printf("Context capture: %u cases, %u failed (all alignments, random GPR/FPR/VMX128/CR/LR, custom hook)\n",tested,failed);
    return failed?1:0;
}
