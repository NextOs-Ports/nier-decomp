// Sample-addressing/quantization helpers in the linked audio middleware.
#include <nier/decomp.h>
#include <algorithm>
using namespace nier::guest;
NIER_DECOMP(sub_824C1B30) {ctx.r3.s64=int16_t(ld16(base,ctx.r3.u32+ctx.r6.u32*2));}
NIER_VERIFY_SPEC(sub_824C1B30,0x824C1B30,"r3=const:0x40010000 r6=range:0..32");
NIER_DECOMP(sub_824C1B40) {ctx.r3.s64=int32_t(ld32(base,ctx.r3.u32+ctx.r6.u32*3))>>8;}
NIER_VERIFY_SPEC(sub_824C1B40,0x824C1B40,"r3=const:0x40010000 r6=range:0..32");
NIER_DECOMP(sub_824C1B58) {
    const uint32_t p=ctx.r3.u32+ctx.r6.u32*3;
    const uint32_t packed=(uint32_t(int32_t(int8_t(ld8(base,p+2))))<<16)|ld16(base,p);
    ctx.r3.s64=int32_t(packed)>>4;
}
NIER_VERIFY_SPEC(sub_824C1B58,0x824C1B58,"r3=const:0x40010000 r6=range:0..32");

NIER_DECOMP(sub_824C1BD8) {st16(base,ctx.r4.u32+ctx.r6.u32*2,ctx.r3.u16);}
NIER_VERIFY_SPEC(sub_824C1BD8,0x824C1BD8,"r3=int r4=const:0x40010000 r6=range:0..32");
NIER_DECOMP(sub_824C1BE8) {
    const uint32_t p=ctx.r4.u32+ctx.r6.u32*3,value=ctx.r3.u32;
    st8(base,p,uint8_t(value>>16));st8(base,p+1,uint8_t(value>>8));st8(base,p+2,uint8_t(value));
}
NIER_VERIFY_SPEC(sub_824C1BE8,0x824C1BE8,"r3=int r4=const:0x40010000 r6=range:0..32");
NIER_DECOMP(sub_824C1C10) {
    const uint32_t p=ctx.r4.u32+ctx.r6.u32*3,shifted=ctx.r3.u32<<4;
    st16(base,p,uint16_t(shifted>>16));st8(base,p+2,uint8_t(shifted>>8));
}
NIER_VERIFY_SPEC(sub_824C1C10,0x824C1C10,"r3=int r4=const:0x40010000 r6=range:0..32");

NIER_DECOMP(sub_824C1C38) {
    ctx.fpscr.disableFlushMode();
    const uint32_t depth=ld16(base,ctx.r5.u32+110),scale=slw(1,depth-1);
    const float numerator=float(ctx.r3.s32),denominator=float(int32_t(scale));
    stf(base,ctx.r4.u32+ctx.r6.u32*4,float(double(numerator)/double(denominator)));
}
NIER_VERIFY_SPEC(sub_824C1C38,0x824C1C38,
 "r3=int r4=const:0x40010000 r5=const:0x40020000 r6=range:0..32 @r5.108=range:0..33");

NIER_DECOMP(sub_824C1C80) {
    ctx.fpscr.disableFlushMode();
    const uint32_t scale=slw(1,ctx.r5.u32-1);
    const float scaled=float(double(ldf(base,ctx.r3.u32+ctx.r6.u32*4))*double(float(int32_t(scale))));
    const float half=ldf(base,0x82039c48),zero=ldf(base,0x820454e0);
    // PPC bge here tests !LT, so unordered (NaN) takes this branch too.
    if(!(scaled<zero)) {
        const uint32_t value=fctiwz(float(double(scaled)+double(half)));
        const uint64_t limit=uint64_t(scale)-1;
        ctx.r3.u64=int32_t(value)>int32_t(limit)?limit:value;
    } else {
        const uint32_t value=fctiwz(float(double(scaled)-double(half)));
        const uint64_t limit=uint64_t(0)-scale;
        ctx.r3.u64=int32_t(value)<int32_t(limit)?limit:value;
    }
}
NIER_VERIFY_SPEC(sub_824C1C80,0x824C1C80,
 "r3=const:0x40010000 r5=range:0..33 r6=range:0..32");

// Interleave per-channel decoded float buffers, normalizing by the bit depth.
NIER_DECOMP(sub_824C1A78) {
    ctx.fpscr.disableFlushMode();
    const uint32_t p=ctx.r3.u32,frames=ctx.r5.u16,output=ctx.r4.u32;
    const float scale=float(int32_t(slw(1,uint32_t(ld16(base,p+110))-1)));
    const float inverse=float(double(ldf(base,0x820007a4))/double(scale));
    uint32_t written=0;
    for(uint32_t frame=0;frame<frames;++frame)for(uint32_t channel=0;channel<ld16(base,p+34);++channel) {
        const uint32_t channels=ld32(base,p+320),source=ld32(base,channels+channel*1776+60);
        stf(base,output+4*written++,float(double(ldf(base,source+frame*4))*double(inverse)));
    }
    ctx.r3.u64=0;
}
NIER_VERIFY_SPEC(sub_824C1A78,0x824C1A78,
 "r3=const:0x40010000 r4=const:0x40050000 r5=range:0..16 @r3.32=const:3 @r3.108=range:1..31 @r3.320=const:0x40020000 "
 "@r3.0x1003c=const:0x40030000 @r3.0x1072c=const:0x40031000 @r3.0x10e1c=const:0x40032000");

// Quarter-step gain table with the linked routine's asymmetric limits and
// observable pointer return on the nonpositive branch.
NIER_DECOMP(sub_824D64F8) {
    ctx.fpscr.disableFlushMode();int32_t step=ctx.r3.s32;
    if(step<=0) {
        if(step < -72)step=-71;
        const float divisor=float(int32_t(slw(1,uint32_t(-step)>>2)));
        const uint64_t address=uint64_t(int64_t(int32_t(0x82071e00)))-uint32_t(uint32_t(step)*4);
        ctx.r3.u64=address;ctx.f1.f64=float(double(ldf(base,uint32_t(address)))/double(divisor));
    } else {
        if(step>=62) {step=62;ctx.r3.u64=62;}
        const float multiplier=float(int32_t(slw(1,uint32_t(step)>>2)));
        ctx.f1.f64=float(double(multiplier)*double(ldf(base,0x82071f20+uint32_t(step)*4-4)));
    }
}
NIER_VERIFY_SPEC(sub_824D64F8,0x824D64F8,"r3=range:0xffffff9c..80");

NIER_DECOMP(sub_824DB878) {
    const int32_t count=ctx.r4.s32;const uint32_t multiplier=uint32_t(int32_t(ctx.r5.s16));
    for(int32_t i=0;i<count;++i) {const uint32_t p=ctx.r3.u32+uint32_t(i)*4;st32(base,p,ld32(base,p)*multiplier);}
}
NIER_VERIFY_SPEC(sub_824DB878,0x824DB878,"r3=const:0x40010000 r4=range:0xffffffff..32 r5=int");
