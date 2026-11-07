#include <nier/decomp.h>
#include <bit>
using namespace nier::guest;
// Rank in the source mask at the target mask's lowest set bit, or -1 when
// there is no intersection. Preserve r3; the result is a BE signed halfword.
NIER_DECOMP(sub_824C4258) {
    const uint32_t source=ctx.r3.u32,target=ctx.r4.u32;
    int rank=-1;
    if(source&target) {
        const unsigned bit=std::countr_zero(target);
        const uint32_t prefix=0xffffffffu>>(31-bit);
        rank=std::popcount(source&prefix)-1;
    }
    st16(base,ctx.r5.u32,uint16_t(rank));
}
NIER_VERIFY_SPEC(sub_824C4258,0x824C4258,"r3=int r4=int r5=ptr:2");

NIER_DECOMP(sub_824CF0B8) {
    const unsigned tag=ctx.r3.u16;
    if(tag<352 || tag>359)ctx.r3.u64=0;
    else ctx.r3.u64=tag==352?1:(tag==353 || tag==357)?2:3;
}
NIER_VERIFY_SPEC(sub_824CF0B8,0x824CF0B8,"r3=range:340..370");
NIER_DECOMP(sub_824CF110) {const unsigned tag=ctx.r3.u16;ctx.r3.u64=tag>=357 && tag<=359;}
NIER_VERIFY_SPEC(sub_824CF110,0x824CF110,"r3=range:340..370");
