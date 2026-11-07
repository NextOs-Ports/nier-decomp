#include <nier/decomp.h>
using namespace nier::guest;

// Aggregate XMA stream speaker bytes, use explicit masks for extended formats,
// or choose the original default channel mask table when requested.
NIER_DECOMP(sub_824B6948) {
    const uint32_t format=ctx.r3.u32;const uint16_t tag=ld16(base,format);
    uint32_t mask=0;
    if(tag==0x165) {
        const uint16_t streams=ld16(base,format+8);
        for(uint32_t i=0;i<streams;++i) {
            const uint16_t packed=ld16(base,format+30+i*20);
            mask|=(packed&255)|(packed>>8);
        }
    } else if(tag==0x166 || tag==0xfffe) mask=ld32(base,format+20);
    else {
        const uint16_t channels=ld16(base,format+2);
        if(channels==1)mask=4;
        else if(channels==2)mask=3;
        else if(ctx.r4.u32 && channels>=1 && channels<=8)
            mask=ld32(base,0x8276F59C+4*(channels-1));
    }
    ctx.r3.u64=mask;
}
NIER_VERIFY_SPEC(sub_824B6948,0x824B6948,
 "r3=ptr:4 r4=range:0..1 @r3.0=range:0x01650000..0x0166ffff @r3.8=range:0..0x00080000");

// Select one of six existing PCM conversion entrypoints (mono/stereo x 8/16/32).
// Keep the image table live rather than copying its guest addresses into source.
NIER_DECOMP(sub_824BFC88) {
    const uint32_t channels=ctx.r3.u32,bits=ctx.r4.u32;
    const int index=bits==8?0:bits==16?1:bits==32?2:-1;
    ctx.r3.u64=(channels==1 || channels==2) && index>=0
        ? ld32(base,0x8276F5EC+4*((channels-1)*3+unsigned(index))) : 0;
}
NIER_VERIFY_SPEC(sub_824BFC88,0x824BFC88,"r3=range:0..3 r4=range:0..33");

// Initialize the three supplied descriptor words and the original default float.
NIER_DECOMP(sub_824BFD88) {
    const uint32_t object=ctx.r3.u32;
    st32(base,object,ctx.r4.u32);st32(base,object+4,ctx.r5.u32);st32(base,object+8,ctx.r6.u32);
    st32(base,object+12,0);st32(base,object+20,0);
    ctx.fpscr.disableFlushMode();st32(base,object+16,ld32(base,0x820454E0));
}
NIER_VERIFY_SPEC(sub_824BFD88,0x824BFD88,"r3=ptr:4 r4=int r5=int r6=int");
