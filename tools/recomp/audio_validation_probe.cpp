#include <nier/decomp.h>
#include <sys/mman.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <random>
using namespace nier::guest;
#define PAIR(n) extern "C" void __imp__sub_##n(PPCContext&,uint8_t*); extern "C" void decomp_sub_##n(PPCContext&,uint8_t*);
PAIR(824B6668) PAIR(824B6738) PAIR(824B6A00) PAIR(824B6B30) PAIR(824BBAF8) PAIR(824BEBF0)
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
    unsigned tested=0,failed=0;std::array<unsigned,4> accepted{},rejected{};
    auto check=[&](Function function,uint64_t r3,uint64_t r4=0,uint64_t r5=0,uint64_t r6=0) {
        std::array<uint8_t,0x1000> before{},expected{};std::memcpy(before.data(),base+arena,before.size());
        PPCContext a{},b{};a.r1.u64=b.r1.u64=sp;a.r3.u64=b.r3.u64=r3;a.r4.u64=b.r4.u64=r4;
        a.r5.u64=b.r5.u64=r5;a.r6.u64=b.r6.u64=r6;a.f1.f64=b.f1.f64=1.25;
        function.original(a,base);std::memcpy(expected.data(),base+arena,expected.size());
        std::memcpy(base+arena,before.data(),before.size());function.native(b,base);++tested;
        if(a.r3.u64!=b.r3.u64 || a.f1.u64!=b.f1.u64 || a.r1.u64!=b.r1.u64 ||
           std::memcmp(expected.data(),base+arena,expected.size())) {
          ++failed;if(failed<=20)std::printf("FAIL %s case=%u result=%016llx/%016llx args=%llu,%llu,%llu,%llu\n",
            function.name,tested,(unsigned long long)a.r3.u64,(unsigned long long)b.r3.u64,
            (unsigned long long)r3,(unsigned long long)r4,(unsigned long long)r5,(unsigned long long)r6);
        }
        return a.r3.u64;
    };
    const Function validators[]{ENTRY(824B6668),ENTRY(824B6738),ENTRY(824B6A00),ENTRY(824B6B30)};
    std::mt19937_64 random(0x824b6668);
    for(unsigned fn=0;fn<4;++fn) {
        std::memset(base+arena,0,0x1000);
        if(fn==0) {st16(base,p,0xfffe);st16(base,p+2,2);st32(base,p+4,48000);st32(base,p+8,192000);
            st16(base,p+12,4);st16(base,p+14,16);st16(base,p+16,22);st16(base,p+18,16);}
        if(fn==1) {st16(base,p,2);st16(base,p+2,2);st16(base,p+12,256);st16(base,p+14,4);st16(base,p+16,32);
            st16(base,p+18,244);st16(base,p+20,7);for(unsigned i=0;i<7;++i) {
                st16(base,p+22+i*4,ld16(base,0x8206a654+i*2));st16(base,p+24+i*4,ld16(base,0x8206a664+i*2));}}
        if(fn==2) {st16(base,p,0x165);st16(base,p+2,16);st16(base,p+8,2);st8(base,p+11,2);
            for(unsigned i=0;i<2;++i) {st32(base,p+16+i*20,0x00300000);st8(base,p+28+i*20,0x31);
                st8(base,p+29+i*20,2);st16(base,p+30+i*20,3);}}
        if(fn==3) {st16(base,p,0x166);st16(base,p+2,2);st16(base,p+12,4);st16(base,p+14,16);st16(base,p+16,34);
            st16(base,p+18,1);st32(base,p+20,3);st32(base,p+24,10000);st32(base,p+28,2048);st32(base,p+36,10000);
            st32(base,p+40,123);st32(base,p+44,456);st8(base,p+49,3);st16(base,p+50,10);}
        std::array<uint8_t,256> valid{};std::memcpy(valid.data(),base+p,valid.size());
        if(check(validators[fn],p)!=1) {std::printf("FAIL valid baseline %s\n",validators[fn].name);++failed;}
        for(unsigned offset=0;offset<64;++offset)for(unsigned value=0;value<256;++value) {
            std::memcpy(base+p,valid.data(),valid.size());st8(base,p+offset,value);
            if(fn==2 && ld16(base,p+8)>4)st16(base,p+8,ld16(base,p+8)%5);
            if(check(validators[fn],p))++accepted[fn];else ++rejected[fn];
        }
        for(unsigned trial=0;trial<1000;++trial) {
            for(unsigned i=0;i<64;i+=4)st32(base,p+i,random());
            if(fn==2)st16(base,p+8,random()%5);
            if(check(validators[fn],p))++accepted[fn];else ++rejected[fn];
        }
        std::printf("%s: accepted=%u rejected=%u\n",validators[fn].name,accepted[fn],rejected[fn]);
    }
    for(unsigned kind=0;kind<=8;++kind)for(unsigned in=0;in<=8;++in) {
        check(ENTRY(824BEBF0),kind,in);
        for(unsigned out=0;out<=8;++out)for(unsigned flags=0;flags<16;++flags)check(ENTRY(824BBAF8),kind,in,out,flags);
    }
    std::printf("Audio validation: %u cases, %u failed\n",tested,failed);return failed?1:0;
}
