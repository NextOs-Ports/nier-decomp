#include <nier/decomp.h>
#include <sys/mman.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <random>
using namespace nier::guest;
#define PAIR(n) extern "C" void __imp__sub_##n(PPCContext&,uint8_t*); extern "C" void decomp_sub_##n(PPCContext&,uint8_t*);
PAIR(824D13D8) PAIR(824CA558) PAIR(824CA4F8) PAIR(824C4E90) PAIR(824C4EE0) PAIR(824C4FA0) PAIR(824CA9E8)
namespace nierverify {int add(const char*,uint32_t,PPCFunc*,PPCFunc*,const char*,const char*){return 0;}}
struct Function {const char* name;PPCFunc* original;PPCFunc* native;};
#define ENTRY(n) Function{#n,__imp__sub_##n,decomp_sub_##n}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    auto allocation=mmap(nullptr,(1ull<<32)+0x10000,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE,-1,0);
    if(allocation==MAP_FAILED)return 3;
    auto* base=reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(allocation)+0xffff)&~uintptr_t(0xffff));
    constexpr uint32_t image=0x82000000,bytes=0x4090000,sp=0x70004000,arena=0x40000000,p=arena+0x100;
    if(mprotect(base+image,bytes,PROT_READ|PROT_WRITE)||mprotect(base+0x70000000,0x10000,PROT_READ|PROT_WRITE)||mprotect(base+arena,0x10000,PROT_READ|PROT_WRITE))return 4;
    FILE* file=std::fopen(argv[1],"rb");if(!file)return 5;
    if(std::fread(base+image,1,bytes,file)!=bytes)return 6;std::fclose(file);mprotect(base+image,bytes,PROT_READ);
    unsigned tested=0,failed=0;
    auto check=[&](const Function& function,uint64_t r3,uint64_t r4=0,uint64_t r5=0) {
        std::array<uint8_t,0x4000> before{},expected{};std::memcpy(before.data(),base+arena,before.size());
        PPCContext a{},b{};a.r1.u64=b.r1.u64=sp;a.r3.u64=b.r3.u64=r3;a.r4.u64=b.r4.u64=r4;a.r5.u64=b.r5.u64=r5;
        a.f1.f64=b.f1.f64=1.25;
        function.original(a,base);std::memcpy(expected.data(),base+arena,expected.size());std::memcpy(base+arena,before.data(),before.size());function.native(b,base);++tested;
        if(a.r3.u64!=b.r3.u64 || a.f1.u64!=b.f1.u64 || a.r1.u64!=b.r1.u64 ||std::memcmp(expected.data(),base+arena,expected.size())) {
          ++failed;if(failed<=30)std::printf("FAIL %s case=%u r3=%016llx/%016llx f1=%016llx/%016llx data=%d\n",function.name,tested,
            (unsigned long long)a.r3.u64,(unsigned long long)b.r3.u64,(unsigned long long)a.f1.u64,(unsigned long long)b.f1.u64,
            std::memcmp(expected.data(),base+arena,expected.size()));
        }
    };
    constexpr uint64_t high=0x1234567800000000ull;
    for(unsigned nodes=0;nodes<3;++nodes)for(unsigned payload:{0u,0xffffffffu}) {
        std::memset(base+arena,0xa5,0x4000);st32(base,p,arena+0x900);st32(base,p+36,nodes?arena+0x200:0);
        st32(base,p+40,nodes==2?arena+0x300:nodes?arena+0x200:0);st32(base,p+44,nodes);
        st32(base,arena+0x200,payload);st32(base,arena+0x204,nodes==2?arena+0x300:0);check(ENTRY(824D13D8),high|p);
    }
    std::mt19937 random(0x824ca558);
    for(unsigned trial=0;trial<2000;++trial) {
        std::memset(base+arena,0xa5,0x4000);st32(base,p+4,arena+0x1000);st32(base,p+8,arena+0x1100);
        st32(base,p+12,0);st32(base,p+16,0);st32(base,p+20,trial%64);st32(base,p+24,0xffffffff);st32(base,p+28,trial%2);
        st32(base,arena+0x1000,random());st32(base,arena+0x1004,random());st32(base,arena+0x1100,random());
        check(ENTRY(824CA558),high|p,random());
    }
    for(unsigned trial=0;trial<1000;++trial) {
        for(unsigned i=0;i<0x4000;i+=4)st32(base,arena+i,random());
        st32(base,p+8,arena+0x1000);st32(base,arena+0x1000,arena+0x2000);
        st32(base,arena+0x203c,trial%5);st32(base,arena+0x20d4,trial%2);st32(base,arena+0x225c,(trial/2)%2);
        st32(base,arena+0x1000+704,(trial/3)%2);st32(base,p+76,(trial/5)%2);
        check(ENTRY(824C4E90),high|p);check(ENTRY(824C4EE0),high|p);
        check(ENTRY(824C4FA0),high|p,high|random());
    }
    for(unsigned step=0;step<=80;++step) {
        st32(base,p+4,arena+0x1000);st32(base,p+64,80);st32(base,arena+0x1000,80-step);stf(base,arena+0x2000,-.75f);
        check(ENTRY(824CA4F8),high|(arena+0x2000),p,0);
    }
    for(unsigned count=0;count<=4;++count)for(int time=-1;time<=4*1048576;time+=65537) {
        st32(base,p+48,arena+0x1000);st32(base,p+52,count);
        for(unsigned i=0;i<4;++i) {
            const double data[]{double(i),double(i)*.25-.5,.125*double(i)-.125};
            for(unsigned k=0;k<3;++k) {uint64_t bits;std::memcpy(&bits,&data[k],8);st64(base,arena+0x1000+i*24+k*8,bits);}
        }
        check(ENTRY(824CA9E8),high|p,high|uint32_t(time));
    }
    std::printf("Audio buffers: %u cases, %u failed\n",tested,failed);return failed?1:0;
}
