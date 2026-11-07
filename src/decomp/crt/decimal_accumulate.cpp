// Accumulate numeric digit bytes in a 96-bit integer, then normalize the
// linked CRT's 12-byte extended format. Caller supplies a nonzero mantissa.
#include <nier/decomp.h>
using namespace nier::guest;
NIER_DECOMP(sub_825E9AA8) {
    using Wide=unsigned __int128;constexpr Wide mask=(Wide(1)<<96)-1;
    const uint32_t input=ctx.r3.u32,count=ctx.r4.u32,output=ctx.r5.u32;
    Wide value=0;uint16_t exponent=16462;
    st32(base,output,0);st32(base,output+4,0);st32(base,output+8,0);
    for(uint32_t i=0;i<count;++i) {
        // The last low-word doubling also remains in the volatile return r3.
        ctx.r3.u64=uint32_t(value)<<1;
        const uint32_t digit=uint32_t(int32_t(int8_t(ld8(base,input+i))));
        value=(value*10+digit)&mask;
    }
    // As in the original, an all-zero accumulator has no normalization exit;
    // zero input is outside this helper's terminating domain.
    while(uint32_t(value>>64)==0) {value=(value<<16)&mask;exponent=uint16_t(exponent-16);}
    while(!(value&(Wide(1)<<79))) {value=(value<<1)&mask;exponent=uint16_t(exponent-1);}
    st16(base,output,exponent);st16(base,output+2,uint16_t(value>>64));st64(base,output+4,uint64_t(value));
}
NIER_VERIFY_SPEC(sub_825E9AA8,0x825E9AA8,
 "r3=const:0x40010000 r4=range:1..24 r5=const:0x40050000 @r3.0=const:0x01000000");
