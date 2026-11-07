// Backward LZ stream: reverse-byte bit reservoir, literals and distance/length
// copies. This routine assumes a valid stream and adequate caller buffers.
#include <nier/decomp.h>
using namespace nier::guest;
NIER_DECOMP(sub_825EBA18) {
    const uint32_t source=ctx.r4.u32,destination=ctx.r6.u32,end=destination+ctx.r7.u32;
    uint32_t input=source+ctx.r5.u32,output=end,reservoir=0;unsigned available=0;
    const bool protect_input=destination>=source;
    auto refill_byte=[&] {reservoir=(reservoir<<8)|ld8(base,--input);available+=8;};
    auto take=[&](unsigned count) {available-=count;return (reservoir>>available)&((1u<<count)-1);};
    while(output!=destination) {
        while(available<=24)refill_byte();
        if(!take(1)) {
            if(protect_input && output-1<input) {ctx.r3.u64=0;return;}
            st8(base,--output,uint8_t(take(8)));
        } else {
            const uint32_t distance=take(13);
            uint32_t length=take(2);
            if(length==3) {
                uint32_t extra=take(3);length+=extra;
                if(extra==7) {
                    extra=take(5);length+=extra;
                    if(extra==31)do {refill_byte();extra=take(8);length+=extra;}while(extra==255);
                }
            }
            length+=3;
            if(protect_input && output-length<input) {ctx.r3.u64=0;return;}
            uint32_t copy=output+distance+3;
            for(uint32_t i=0;i<length;++i) {const uint8_t value=ld8(base,--copy);st8(base,--output,value);}
        }
    }
    ctx.r3.u64=uint32_t(end-output);
}
NIER_VERIFY_SPEC(sub_825EBA18,0x825EBA18,
 "r4=const:0x40010000 r5=const:4 r6=const:0x40050000 r7=range:0..3 @r4.0=range:0..127");

// Extract the little-endian word at header+8; a short declared header writes
// UINT32_MAX. The original pointer in r3 remains unchanged.
NIER_DECOMP(sub_825EBC68) {st32(base,ctx.r5.u32,ctx.r4.u32<4?0xffffffffu:__builtin_bswap32(ld32(base,ctx.r3.u32+8)));}
NIER_VERIFY_SPEC(sub_825EBC68,0x825EBC68,"r3=ptr:4 r4=range:0..16 r5=ptr:4");
