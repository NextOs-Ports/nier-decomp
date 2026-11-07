#include <nier/decomp.h>
#include <sys/mman.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <random>
using namespace nier::guest;
extern "C" void __imp__sub_825E9AA8(PPCContext&,uint8_t*);
extern "C" void decomp_sub_825E9AA8(PPCContext&,uint8_t*);
namespace nierverify {int add(const char*,uint32_t,PPCFunc*,PPCFunc*,const char*,const char*){return 0;}}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    auto allocation=mmap(nullptr,(1ull<<32)+0x10000,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE,-1,0);
    if(allocation==MAP_FAILED)return 3;
    auto* base=reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(allocation)+0xffff)&~uintptr_t(0xffff));
    constexpr uint32_t image=0x82000000,bytes=0x4090000,sp=0x70004000,arena=0x40000000;
    if(mprotect(base+image,bytes,PROT_READ|PROT_WRITE)||mprotect(base+0x70000000,0x10000,PROT_READ|PROT_WRITE)||mprotect(base+arena,0x10000,PROT_READ|PROT_WRITE))return 4;
    FILE* file=std::fopen(argv[1],"rb");if(!file)return 5;
    if(std::fread(base+image,1,bytes,file)!=bytes)return 6;std::fclose(file);mprotect(base+image,bytes,PROT_READ);
    std::mt19937 random(0x825e9aa8);unsigned tested=0,failed=0;
    for(unsigned count=1;count<=24;++count)for(unsigned trial=0;trial<1000;++trial) {
        for(unsigned i=0;i<count;++i)st8(base,arena+i,trial<500?random()%10:random());
        st8(base,arena,1+random()%9);
        PPCContext a{},b{};a.r1.u64=b.r1.u64=sp;a.r3.u64=b.r3.u64=0x1234567840000000ull;
        a.r4.u64=b.r4.u64=count;a.r5.u64=b.r5.u64=arena+0x100;a.f1.f64=b.f1.f64=1.25;
        std::array<uint8_t,64> expected{};std::memset(base+arena+0x100,0xa5,64);
        __imp__sub_825E9AA8(a,base);std::memcpy(expected.data(),base+arena+0x100,64);
        std::memset(base+arena+0x100,0xa5,64);decomp_sub_825E9AA8(b,base);++tested;
        if(a.r3.u64!=b.r3.u64 || a.f1.u64!=b.f1.u64 || a.r1.u64!=b.r1.u64 ||std::memcmp(expected.data(),base+arena+0x100,64)) {
          ++failed;if(failed<=20)std::printf("FAIL count=%u trial=%u r3=%016llx/%016llx data=%d\n",count,trial,
            (unsigned long long)a.r3.u64,(unsigned long long)b.r3.u64,std::memcmp(expected.data(),base+arena+0x100,64));
        }
    }
    std::printf("Decimal accumulator: %u cases, %u failed (1..24 numeric/raw bytes, nonzero, extended96 and volatile return)\n",tested,failed);
    return failed?1:0;
}
