#include <nier/decomp.h>
#include <sys/mman.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>
using namespace nier::guest;
extern "C" void __imp__sub_825EBA18(PPCContext&,uint8_t*);
extern "C" void decomp_sub_825EBA18(PPCContext&,uint8_t*);
namespace nierverify {int add(const char*,uint32_t,PPCFunc*,PPCFunc*,const char*,const char*){return 0;}}
struct Encoder {
    std::vector<bool> bits;
    void put(unsigned value,unsigned count) {while(count--)bits.push_back((value>>count)&1);}
    void literal(uint8_t byte) {put(0,1);put(byte,8);}
    void match(unsigned distance,unsigned length) {
        put(1,1);put(distance,13);unsigned n=length-3;
        if(n<3) {put(n,2);return;}put(3,2);n-=3;
        if(n<7) {put(n,3);return;}put(7,3);n-=7;
        if(n<31) {put(n,5);return;}put(31,5);n-=31;
        while(n>=255) {put(255,8);n-=255;}put(n,8);
    }
    std::vector<uint8_t> bytes() {
        while(bits.size()%8)bits.push_back(false);
        std::vector<uint8_t> result(bits.size()/8+4,0);
        for(unsigned i=0;i<bits.size();++i)if(bits[i])result[result.size()-1-i/8]|=1u<<(7-i%8);
        return result;
    }
};
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
    unsigned tested=0,failed=0,blocked=0;
    auto check=[&](Encoder& encoder,unsigned output_size,unsigned placement) {
        const auto encoded=encoder.bytes();std::vector<uint8_t> before(0x10000,0x5a),expected(0x10000);
        const uint32_t source=arena+(placement==0?0x8000:0x1000),destination=arena+(placement==1?0x8000:0x1000);
        std::memcpy(before.data()+source-arena,encoded.data(),encoded.size());
        PPCContext a{},b{};a.r1.u64=b.r1.u64=sp;a.r3.u64=b.r3.u64=0x123456789abcdef0ull;
        a.r4.u64=b.r4.u64=source;a.r5.u64=b.r5.u64=encoded.size();a.r6.u64=b.r6.u64=destination;
        a.r7.u64=b.r7.u64=output_size;a.f1.f64=b.f1.f64=1.25;
        std::memcpy(base+arena,before.data(),before.size());__imp__sub_825EBA18(a,base);std::memcpy(expected.data(),base+arena,expected.size());
        std::memcpy(base+arena,before.data(),before.size());decomp_sub_825EBA18(b,base);++tested;
        if(!a.r3.u64 && output_size)++blocked;
        if(a.r3.u64!=b.r3.u64 || a.f1.u64!=b.f1.u64 || a.r1.u64!=b.r1.u64 ||
           (placement<2 && a.r3.u64!=output_size) || std::memcmp(expected.data(),base+arena,expected.size())) {
          ++failed;if(failed<=20)std::printf("FAIL case=%u size=%u place=%u r3=%llu/%llu data=%d\n",tested,output_size,placement,
            (unsigned long long)a.r3.u64,(unsigned long long)b.r3.u64,std::memcmp(expected.data(),base+arena,expected.size()));
        }
    };
    for(unsigned count=0;count<128;++count)for(unsigned placement=0;placement<3;++placement) {
        Encoder e;for(unsigned i=0;i<count;++i)e.literal(uint8_t(i*17+count));check(e,count,placement);
    }
    for(unsigned seeds:{3u,4u,17u,8194u})for(unsigned length:{3u,4u,5u,6u,12u,13u,43u,44u,298u,299u,553u,554u,808u,809u,1800u})
      for(unsigned tail=0;tail<8;++tail)for(unsigned placement=0;placement<3;++placement) {
        Encoder e;for(unsigned i=0;i<seeds;++i)e.literal(uint8_t(i*13+tail));e.match(seeds-3,length);
        for(unsigned i=0;i<tail;++i)e.literal(uint8_t(i*7));check(e,seeds+length+tail,placement);
      }
    std::mt19937 random(0x825eba18);
    for(unsigned trial=0;trial<100;++trial) {
        Encoder e;unsigned seeds=3+random()%64,length=3+random()%2048;
        for(unsigned i=0;i<seeds;++i)e.literal(random());e.match(random()%(seeds-2),length);check(e,seeds+length,trial%2);
    }
    std::printf("Backward LZ: %u cases, %u failed, %u overlap guards returned zero\n",tested,failed,blocked);return failed?1:0;
}
