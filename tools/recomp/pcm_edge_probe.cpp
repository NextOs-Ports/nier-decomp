#include <rex/ppc.h>
#include <rex/ppc/context.h>
#include <sys/mman.h>
#include <array>
#include <cfenv>
#include <cstdio>
#include <cstring>
#define PAIR(n) extern "C" void __imp__sub_##n(PPCContext&,uint8_t*); extern "C" void decomp_sub_##n(PPCContext&,uint8_t*);
PAIR(824BB5D0) PAIR(824BB5F8) PAIR(824BB640) PAIR(824BB670) PAIR(824BE428) PAIR(824BE458) PAIR(824BE4A0)
namespace nierverify {int add(const char*,uint32_t,PPCFunc*,PPCFunc*,const char*,const char*){return 0;}}
struct Function {const char* name;PPCFunc* original;PPCFunc* native;};
#define ENTRY(n) {#n,__imp__sub_##n,decomp_sub_##n}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    auto allocation=mmap(nullptr,(1ull<<32)+0x10000,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE,-1,0);
    if(allocation==MAP_FAILED)return 3;
    auto* base=reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(allocation)+0xffff)&~uintptr_t(0xffff));
    constexpr uint32_t image=0x82000000,bytes=0x4090000,stack=0x70000000,sp=stack+0x4000,data=0x40000000;
    if(mprotect(base+image,bytes,PROT_READ|PROT_WRITE)||mprotect(base+stack,0x10000,PROT_READ|PROT_WRITE)||mprotect(base+data,4096,PROT_READ|PROT_WRITE))return 4;
    FILE* file=std::fopen(argv[1],"rb");if(!file)return 5;
    if(std::fread(base+image,1,bytes,file)!=bytes)return 6;std::fclose(file);mprotect(base+image,bytes,PROT_READ);
    const Function functions[]{ENTRY(824BB5D0),ENTRY(824BB5F8),ENTRY(824BB640),ENTRY(824BB670),ENTRY(824BE428),ENTRY(824BE458),ENTRY(824BE4A0)};
    const uint32_t patterns[]{0,1,0xffffffff,0x80000000,0x7fffffff,0x8000ffff,0x7fff0000,
      0x00008000,0xffff7f00,0x0f000000,0x10000000,0x00000fff,0x00001000,0x000000ff,
      0x00000100,0x7f000000,0xff000000,0x01000000,0x12345678,0x87654321};
    unsigned tested=0,failed=0;
    for(const auto& function:functions)for(int mode:{FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO})for(uint32_t pattern:patterns) {
      uint32_t value=__builtin_bswap32(pattern);std::memcpy(base+data,&value,4);
      PPCContext a{},b{};a.r1.u64=b.r1.u64=sp;
      a.r3.u64=b.r3.u64=0xffffffff00000000ull|data;
      a.f1.u64=b.f1.u64=0x3ff8000000000000ull;
      std::fesetround(mode);function.original(a,base);
      std::fesetround(mode);function.native(b,base);++tested;
      uint32_t after;std::memcpy(&after,base+data,4);
      if(a.r3.u64!=b.r3.u64||a.f1.u64!=b.f1.u64||after!=value) {
        ++failed;std::printf("FAIL %s mode=%d bytes=%08x r3=%016llx/%016llx f1=%016llx/%016llx\n",function.name,mode,pattern,
          (unsigned long long)a.r3.u64,(unsigned long long)b.r3.u64,(unsigned long long)a.f1.u64,(unsigned long long)b.f1.u64);
      }
    }
    std::fesetround(FE_TONEAREST);std::printf("PCM edges: %u cases, %u failed\n",tested,failed);
    return failed?1:0;
}
