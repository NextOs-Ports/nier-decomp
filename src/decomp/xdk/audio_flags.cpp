#include <nier/decomp.h>
#include <bit>
using namespace nier::guest;
// Packed audio-format flags, including the two stack-passed arguments.
NIER_DECOMP(sub_824B1198) {
    const uint32_t count=ctx.r6.u32,category=ctx.r8.u32;
    const uint32_t logarithm=count<=1?0:std::bit_width(count)-1;
    uint32_t flags=0;
    if(category<3) {
        flags=ctx.r3.s32==1?1:32;
        if(ctx.r4.s32)flags|=2;
        if(ctx.r5.s32)flags=(flags|4)|((logarithm-(ctx.r7.s32?1:3))<<3);
    } else if(category==3) {
        if(ctx.r9.s32 && ld32(base,ctx.r1.u32+92)==100)flags=1;
        else {
            if(!ctx.r10.u32)flags=64;
            if(ld32(base,ctx.r1.u32+84))flags|=128;
        }
        flags|=logarithm<<3;if(flags&1)flags|=256;
    }
    ctx.r3.u64=uint16_t(flags);
}
NIER_VERIFY_SPEC(sub_824B1198,0x824B1198,
 "r3=range:0..2 r4=range:0..1 r5=range:0..1 r6=range:0..128 r7=range:0..1 r8=range:0..4 r9=range:0..1 r10=range:0..1 @r1.84=range:0..1 @r1.92=range:99..100");

// Compute two signed 16-bit parameters, optionally collapse them to their mean.
NIER_DECOMP(sub_824C6660) {
    const uint32_t p=ctx.r3.u32,a=ctx.r7.u32,b=ctx.r8.u32;
    const int lo=ctx.r5.s16,hi=ctx.r6.s16;
    st16(base,a,lo>=hi?0:(hi-lo)/2);st16(base,b,lo>=hi?hi:(hi+lo)/2);
    const bool first=ctx.r4.s32==1;
    if(ld32(base,p+140)==1 && !ld32(base,p+(first?148:156))) {
        const int16_t mean=(int(int16_t(ld16(base,a)))+int(int16_t(ld16(base,b))))/2;
        st16(base,a,mean);st16(base,b,mean);if(first)ctx.r3.s64=mean;
    }
}
NIER_VERIFY_SPEC(sub_824C6660,0x824C6660,
 "r3=const:0x40010000 r4=range:0..1 r5=u16 r6=u16 r7=const:0x40020000 r8=const:0x40020004 @r3.140=range:0..1 @r3.148=range:0..1 @r3.156=range:0..1");
NIER_DECOMP(sub_824C6730) {
    const uint32_t p=ctx.r3.u32,a=ctx.r8.u32,b=ctx.r9.u32,offset=ctx.r7.u32;
    const int lo=ctx.r5.s16,hi=ctx.r6.s16;
    st16(base,a,hi>=lo?offset:offset+uint32_t((lo-hi)/2));
    st16(base,b,hi>=lo?offset*2:offset+uint32_t((lo+hi)/2));
    const bool first=ctx.r4.s32==1;
    if(ld32(base,p+140)==1 && !ld32(base,p+(first?156:148))) {
        const int16_t mean=(int(int16_t(ld16(base,a)))+int(int16_t(ld16(base,b))))/2;
        st16(base,a,mean);st16(base,b,mean);if(first)ctx.r3.s64=mean;
    }
}
NIER_VERIFY_SPEC(sub_824C6730,0x824C6730,
 "r3=const:0x40010000 r4=range:0..1 r5=u16 r6=u16 r7=u16 r8=const:0x40020000 r9=const:0x40020004 @r3.140=range:0..1 @r3.148=range:0..1 @r3.156=range:0..1");
