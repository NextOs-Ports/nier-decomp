#include <nier/decomp.h>
#include <sys/mman.h>
#include <array>
#include <cfenv>
#include <cstdio>
#include <cstring>
#include <vector>
using namespace nier::guest;
#define PAIR(n) extern "C" void __imp__sub_##n(PPCContext&,uint8_t*); extern "C" void decomp_sub_##n(PPCContext&,uint8_t*);
PAIR(824BFAB0) PAIR(824B54A0) PAIR(824B6948) PAIR(824BFC88)
namespace nierverify {int add(const char*,uint32_t,PPCFunc*,PPCFunc*,const char*,const char*){return 0;}}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    auto allocation=mmap(nullptr,(1ull<<32)+0x10000,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE,-1,0);
    if(allocation==MAP_FAILED)return 3;
    auto* base=reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(allocation)+0xffff)&~uintptr_t(0xffff));
    constexpr uint32_t image=0x82000000,image_bytes=0x4090000,data=0x40000000,object=data+0x100,stack=0x70000000;
    if(mprotect(base+image,image_bytes,PROT_READ|PROT_WRITE)||mprotect(base+data,0x10000,PROT_READ|PROT_WRITE)||mprotect(base+stack,0x10000,PROT_READ|PROT_WRITE))return 4;
    FILE* file=std::fopen(argv[1],"rb");if(!file)return 5;
    if(std::fread(base+image,1,image_bytes,file)!=image_bytes)return 6;std::fclose(file);mprotect(base+image,image_bytes,PROT_READ);
    unsigned tested=0,failed=0;
    auto check=[&](const char* name,PPCFunc* oracle,PPCFunc* native,uint64_t r3,uint64_t r4,int rounding=FE_TONEAREST) {
        std::vector<uint8_t> input(base+data,base+data+0x10000),expected(0x10000);
        PPCContext a{},b{};a.r1.u64=b.r1.u64=stack+0x4000;a.r3.u64=b.r3.u64=r3;a.r4.u64=b.r4.u64=r4;
        a.f1.f64=b.f1.f64=1.25;
        std::fesetround(rounding);oracle(a,base);std::memcpy(expected.data(),base+data,expected.size());
        std::memcpy(base+data,input.data(),input.size());std::fesetround(rounding);native(b,base);++tested;
        if(a.r3.u64!=b.r3.u64||a.f1.u64!=b.f1.u64||a.r1.u64!=b.r1.u64||std::memcmp(expected.data(),base+data,expected.size())) {
            ++failed;std::printf("FAIL %s rounding=%d r3=%llx/%llx\n",name,rounding,(unsigned long long)a.r3.u64,(unsigned long long)b.r3.u64);
        }
    };
    for(unsigned nodes=0;nodes<3;++nodes)for(unsigned empty_payload=0;empty_payload<2;++empty_payload) {
        std::memset(base+data,0xa5,0x10000);
        st32(base,object,data+0x600);st32(base,object+204,nodes?data+0x200:0);
        st32(base,object+208,nodes==2?data+0x240:nodes?data+0x200:0);st32(base,object+212,nodes);
        st32(base,data+0x200,empty_payload?0:0xabcdef01);st32(base,data+0x204,nodes==2?data+0x240:0);
        st32(base,data+0x248,data+0x200);
        check("queue pop",__imp__sub_824B54A0,decomp_sub_824B54A0,object,0);
    }
    for(uint16_t tag:{0u,1u,2u,0x165u,0x166u,0xfffeu})for(unsigned channels=0;channels<=10;++channels)for(unsigned allow=0;allow<2;++allow) {
        std::memset(base+data,0,0x10000);st16(base,object,tag);st16(base,object+2,channels);
        st16(base,object+8,3);st32(base,object+20,0x63f);
        st16(base,object+30,0x0102);st16(base,object+50,0x0408);st16(base,object+70,0x1020);
        check("channel mask",__imp__sub_824B6948,decomp_sub_824B6948,object,allow);
    }
    for(unsigned channels=0;channels<4;++channels)for(unsigned bits:{0u,8u,16u,24u,32u,64u})
        check("converter selector",__imp__sub_824BFC88,decomp_sub_824BFC88,channels,bits);
    for(int rounding:{FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO})for(unsigned count:{0u,1u,4u})for(unsigned mode=0;mode<4;++mode) {
        std::memset(base+data,0,0x10000);
        st32(base,object,data+0x1000);st32(base,object+4,data+0x2000);st32(base,object+8,data+0x3000);st32(base,object+12,data+0x3004);
        st32(base,object+16,count);st32(base,object+24,mode);stf(base,object+28,.25f);stf(base,object+32,.5f);
        stf(base,data+0x1000,0);stf(base,data+0x1004,-1);stf(base,data+0x1008,1);stf(base,data+0x100c,.125f);
        check("state-variable filter",__imp__sub_824BFAB0,decomp_sub_824BFAB0,object,0,rounding);
    }
    std::fesetround(FE_TONEAREST);std::printf("Audio structures: %u cases, %u failed\n",tested,failed);return failed?1:0;
}
