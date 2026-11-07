// Select existing audio processing entrypoints by representation/channels.
#include <nier/decomp.h>
using namespace nier::guest;
namespace {
void return_target(PPCContext& ctx,uint32_t address) {ctx.r3.s64=int32_t(address);}
}
NIER_DECOMP(sub_824BBAF8) {
    const uint32_t kind=ctx.r3.u32,in=ctx.r4.u32,out=ctx.r5.u32,flags=ctx.r6.u32;
    if(kind!=1 && kind!=7) {return_target(ctx,kind<7?0x824bb6a0:0);return;}
    // Columns: mono->stereo, mono->six, alternate mono->six, stereo->six,
    // six->stereo, general, intermediate, flagged.
    const uint32_t table[2][8]{
      {0x824b9bf8,0x824b9d38,0x824badb0,0x824b9f68,0x824ba308,0x824b99e0,0x824ba6a0,0x824bb1c8},
      {0x824b8498,0x824b85a0,0x824b9210,0x824b8778,0x824b8a60,0x824b82e0,0x824b8ca0,0x824b9600}};
    unsigned index=5;
    if(in==1 && out==2)index=0;
    else if(in==1 && out==6)index=(flags&4)?2:1;
    else if(in==2 && out==6)index=3;
    else if(in==6 && out==2)index=4;
    else if(flags&2) {
        if((flags&1) || ((in<=4 && in!=3) && (flags&4)))index=7;
        else if(in<=6)index=6;
    }
    return_target(ctx,table[kind==7][index]);
}
NIER_VERIFY_SPEC(sub_824BBAF8,0x824BBAF8,"r3=range:0..8 r4=range:0..8 r5=range:0..8 r6=range:0..15");

NIER_DECOMP(sub_824BEBF0) {
    const uint32_t kind=ctx.r3.u32,channels=ctx.r4.u32;
    if(kind!=1 && kind!=7) {return_target(ctx,kind<7?0x824be4c8:0);return;}
    const uint32_t table[2][4]{{0x824bdfa8,0x824bd228,0x824bd638,0x824bcb58},
                              {0x824bc168,0x824bc428,0x824bc718,0x824bbcc8}};
    return_target(ctx,table[kind==7][channels==1?0:channels==2?1:channels==6?2:3]);
}
NIER_VERIFY_SPEC(sub_824BEBF0,0x824BEBF0,"r3=range:0..8 r4=range:0..8");

// Output-conversion selector; its CTR zero tests use the low word.
NIER_DECOMP(sub_824BF930) {
    const uint32_t targets[]{0x824bef58,0x824bedd8,0x824bf0e8,0x824bf4a0,0x824bf2d0,0x824bf630,0x824bf7c0};
    const uint32_t kind=ctx.r3.u32;
    return_target(ctx,kind<=6?targets[kind]:0x824becb0);
}
NIER_VERIFY_SPEC(sub_824BF930,0x824BF930,"r3=range:0..8");
NIER_DECOMP(sub_824BFC50) {
    const uint32_t channels=ctx.r3.u32;
    return_target(ctx,channels==1?0x824bfab0:channels==2?0x824bfb60:0x824bf9c8);
}
NIER_VERIFY_SPEC(sub_824BFC50,0x824BFC50,"r3=range:0..4");
