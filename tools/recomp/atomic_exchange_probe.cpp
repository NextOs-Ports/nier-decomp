#include <rex/ppc.h>
#include <rex/ppc/context.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>
extern "C" void __imp__sub_825EB868(PPCContext&,uint8_t*);
extern "C" void decomp_sub_825EB868(PPCContext&,uint8_t*);
namespace nierverify {
int add(const char*,uint32_t,PPCFunc*,PPCFunc*,const char*,const char*) {return 0;}
}
static bool exercise(PPCFunc* function,const char* name) {
    constexpr unsigned workers=4,count=2000;
    alignas(16) std::array<uint32_t,16> memory{};
    std::vector<uint32_t> observed(workers*count+1);
    std::vector<std::jthread> threads;
    for(unsigned worker=0;worker<workers;++worker) {
        threads.emplace_back([&,worker] {
            PPCContext context{};context.r13.u64=0x50000100;
            for(unsigned i=0;i<count;++i) {
                const unsigned slot=worker*count+i;
                context.r3.u64=16;context.r4.u64=slot+1;
                function(context,reinterpret_cast<uint8_t*>(memory.data()));
                observed[slot]=context.r3.u32;
            }
        });
    }
    threads.clear(); // join before examining the last stored word
    observed.back()=__builtin_bswap32(memory[4]);
    std::sort(observed.begin(),observed.end());
    for(unsigned i=0;i<observed.size();++i)if(observed[i]!=i)return false;
    std::printf("PASS %s: 4 threads, 8000 exchanges, every token accounted for once\n",name);
    return true;
}
int main() {
    return exercise(__imp__sub_825EB868,"translated oracle") &&
           exercise(decomp_sub_825EB868,"native reconstruction") ? 0 : 1;
}
