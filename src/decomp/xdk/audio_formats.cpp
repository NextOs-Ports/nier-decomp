#include <nier/decomp.h>
using namespace nier::guest;

// Convert the wave-style header into the engine's six-word parameter record.
// Keep reads/stores ordered and return ceil(bits_per_sample/8), as the original.
NIER_DECOMP(sub_824B23E0) {
    const uint32_t input=ctx.r3.u32,output=ctx.r4.u32;
    st32(base,output,ld32(base,input+4));
    st32(base,output+4,ld16(base,input+2));
    st32(base,output+8,ld32(base,input+16));
    st32(base,output+12,ld16(base,input+14));
    const uint32_t bytes=(uint32_t(ld16(base,input+14))+7)/8;
    st32(base,output+20,0);st32(base,output+16,bytes);ctx.r3.u64=bytes;
}
NIER_VERIFY_SPEC(sub_824B23E0,0x824B23E0,"r3=ptr:4 r4=ptr:4");

// Populate a zeroed 24-byte base header. Tag/rate-derived fields intentionally
// remain zero here; this is not a complete standalone WAVEFORMATEX constructor.
NIER_DECOMP(sub_824B2420) {
    const uint32_t input=ctx.r3.u32,output=ctx.r4.u32;
    for(unsigned offset=0;offset<24;offset+=4)st32(base,output+offset,0);
    st16(base,output,0);st16(base,output+2,uint16_t(ld32(base,input+4)));
    st32(base,output+4,ld32(base,input));st32(base,output+8,0);st16(base,output+12,0);
    st16(base,output+14,uint16_t(ld32(base,input+12)));st32(base,output+16,ld32(base,input+8));st16(base,output+20,0);
}
NIER_VERIFY_SPEC(sub_824B2420,0x824B2420,"r3=ptr:4 r4=ptr:4");

namespace {
uint32_t adpcm_samples(uint32_t block,uint32_t channels) {
    block&=0xffff;channels&=0xffff;
    if(block<7*channels)return 0;
    // Match the translated division-by-zero convention and 16-bit result.
    return uint16_t((channels?2*block/channels:0)-12);
}
}
// Microsoft ADPCM block geometry, with the multiply performed before division.
NIER_DECOMP(sub_824BFCF0) {ctx.r3.u64=adpcm_samples(ctx.r3.u32,ctx.r4.u32);}
NIER_VERIFY_SPEC(sub_824BFCF0,0x824BFCF0,"r3=u16 r4=range:0..8");

// Number of complete blocks needed for a sample count, plus optional padding.
NIER_DECOMP(sub_824BFD28) {
    const uint32_t per_block=adpcm_samples(ctx.r3.u32,ctx.r4.u32),samples=ctx.r5.u32;
    const uint32_t numerator=per_block+samples-1;
    const uint32_t blocks=per_block?numerator/per_block:0;
    if(ctx.r6.u32)st32(base,ctx.r6.u32,blocks*per_block-samples);
    ctx.r3.u64=blocks;
}
NIER_VERIFY_SPEC(sub_824BFD28,0x824BFD28,"r3=u16 r4=range:0..8 r5=int r6=ptr:4");
