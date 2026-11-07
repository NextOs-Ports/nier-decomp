// Queue recycling, bit-buffer cursors and table/envelope interpolation.
#include <nier/decomp.h>
#include <bit>
using namespace nier::guest;
namespace {
uint32_t partial_bits(const uint8_t* base,uint32_t object) {
    const uint32_t format=ld32(base,ld32(base,object+8)),width=ld32(base,format+8);
    if(int32_t(ld32(base,format+60))<=2)return ld32(base,format+212)?(width+11)&7:0;
    return (width+(ld32(base,format+604)?17:6))&7;
}
double number(const uint8_t* base,uint32_t address) {return std::bit_cast<double>(ld64(base,address));}
}
NIER_DECOMP(sub_824D13D8) {
    const uint32_t p=ctx.r3.u32,node=ld32(base,p+36);
    if(!node) {ctx.r3.u64=0;return;}
    const uint32_t next=ld32(base,node+4),payload=ld32(base,node);
    st32(base,p+36,next);if(next)st32(base,next+8,0);else st32(base,p+40,0);
    st32(base,node+4,ld32(base,p));st32(base,p,node);st32(base,p+44,ld32(base,p+44)-1);
    ctx.r3.u64=payload;
}
NIER_VERIFY_SPEC(sub_824D13D8,0x824D13D8,
 "r3=const:0x40010000 @r3.36=const:0x40020000 @r3.0x10004=const:0x40030000 @r3.44=range:1..16");

NIER_DECOMP(sub_824CA558) {
    const uint32_t p=ctx.r3.u32,offset=ctx.r4.u32-ld32(base,p+12),shift=ld32(base,p+20);
    const uint32_t index=srw(offset&ld32(base,p+16),shift),fraction=offset&ld32(base,p+24);
    const uint32_t table=ld32(base,p+4),value=ld32(base,table+index*4);
    if(ld32(base,p+28)) {
        const int32_t slope=int32_t(ld32(base,ld32(base,p+8)+index*4));
        ctx.r3.u64=uint64_t(int64_t(slope)*int64_t(int32_t(fraction)))+value;
    } else {
        const uint32_t delta=ld32(base,table+index*4+4)-value,scale=slw(1,shift);
        const int32_t numerator=int32_t(uint32_t(uint64_t(delta)*fraction));
        const int32_t denominator=int32_t(scale);
        const uint32_t quotient=denominator && !(numerator==INT32_MIN && denominator==-1)?uint32_t(numerator/denominator):0;
        ctx.r3.u64=uint64_t(quotient)+value;
    }
}
NIER_VERIFY_SPEC(sub_824CA558,0x824CA558,
 "r3=const:0x40010000 r4=range:0..127 @r3.4=const:0x40020000 @r3.8=const:0x40030000 @r3.12=const:0 @r3.16=const:112 @r3.20=const:4 @r3.24=const:15 @r3.28=range:0..1");

NIER_DECOMP(sub_824CA4F8) {
    ctx.fpscr.disableFlushMode();
    const uint32_t table=ld32(base,ctx.r4.u32+4);
    int32_t step=int32_t(ld32(base,ctx.r4.u32+64)-ld32(base,table+ctx.r5.u32*4));
    if(step>=72)step=71;
    const float divisor=float(int32_t(slw(1,uint32_t(step>>2))));
    const float gain=float(double(ldf(base,0x82071e00+uint32_t(step)*4))/double(divisor));
    ctx.f1.f64=float(double(gain)*double(ldf(base,ctx.r3.u32)));
}
NIER_VERIFY_SPEC(sub_824CA4F8,0x824CA4F8,
 "r3=ptr:4 r4=const:0x40010000 r5=range:0..3 @r4.4=const:0x40020000 @r4.64=const:80 "
 "@r4.0x10000=range:0..80 @r4.0x10004=range:0..80 @r4.0x10008=range:0..80 @r4.0x1000c=range:0..80");

NIER_DECOMP(sub_824C4E90) {
    const uint32_t p=ctx.r3.u32;
    ctx.r3.u64=ld32(base,p+76) && (!ld32(base,ld32(base,p+8)+704) ||
        uint32_t(ld32(base,p+28)+ld32(base,p+32))>=uint32_t(ld32(base,p+24)+ld32(base,p+20)));
}
NIER_VERIFY_SPEC(sub_824C4E90,0x824C4E90,
 "r3=const:0x40010000 @r3.8=const:0x40020000 @r3.76=range:0..1 @r3.0x102c0=range:0..1");

NIER_DECOMP(sub_824C4EE0) {
    const uint32_t p=ctx.r3.u32,partial=partial_bits(base,p);
    uint64_t position=uint64_t(ld32(base,p+48))+ld32(base,p+40);
    if(partial) {
        const uint32_t first=ld32(base,p+20),span=ld32(base,p+24),used=ld32(base,p+28),saved=ld32(base,p+80);
        const uint32_t boundary=first-saved+span,threshold=(used-first-span+saved)<<3;
        const uint64_t candidate=position+partial;
        if(used>boundary && uint32_t(candidate)>=threshold)position=candidate;
    }
    ctx.r3.u64=position;
}
NIER_DECOMP(sub_824C4FA0) {
    const uint32_t p=ctx.r3.u32,count=ld32(base,p+32),bits=ld32(base,p+40);
    if(!partial_bits(base,p))return;
    const uint64_t end=uint64_t(count<<3)+bits+ld32(base,p+48),target=ctx.r4.u64;
    st32(base,p+80,ld32(base,p+24));if((end^target)&7)return;
    if(uint32_t(end)>uint32_t(target)) {
        const uint32_t distance=uint32_t(end-target)>>3;
        if(distance<=count)st32(base,p+32,count-distance);
        else {
            const uint32_t extra=distance-count,shift=extra<<3;
            st32(base,p+32,0);st32(base,p+28,ld32(base,p+28)-extra);
            st32(base,p+40,bits-shift);st32(base,p+36,srw(ld32(base,p+36),shift));
        }
    } else {st32(base,p+68,1);st32(base,p+72,uint32_t(target-end)>>3);}
}
static constexpr const char* cursor_spec=
 "r3=const:0x40010000 r4=range:0..4096 @r3.8=const:0x40020000 @r3.0x10000=const:0x40030000 "
 "@r3.0x2003c=range:0..4 @r3.0x20008=range:0..255 @r3.0x200d4=range:0..1 @r3.0x2025c=range:0..1";
NIER_VERIFY_SPEC(sub_824C4EE0,0x824C4EE0,cursor_spec);
NIER_VERIFY_SPEC(sub_824C4FA0,0x824C4FA0,cursor_spec);

NIER_DECOMP(sub_824CA9E8) {
    ctx.fpscr.disableFlushMode();const uint32_t p=ctx.r3.u32,rows=ld32(base,p+48);
    const int32_t segments=int32_t(ld32(base,p+52)-1),time=ctx.r4.s32;
    const double scale=number(base,0x8206d5e0),bias_scale=number(base,0x8206d5d8);
    for(int32_t i=0;i<segments;++i) {
        const uint32_t row=rows+uint32_t(i)*24;
        const int32_t begin=int32_t(fctiwz(number(base,row)*scale));
        if(time<begin || time>int32_t(fctiwz(number(base,row+24)*scale)))continue;
        const int32_t slope=int32_t(fctiwz(number(base,row+16)*scale)),delta=int32_t(uint32_t(time)-uint32_t(begin));
        const uint32_t bias=fctiwz(number(base,row+8)*bias_scale);
        const int64_t product=int64_t(slope)*delta;
        ctx.r3.u64=uint64_t(int64_t(int32_t(product>>20)))-bias;return;
    }
    ctx.r3.u64=0;
}
NIER_VERIFY_SPEC(sub_824CA9E8,0x824CA9E8,
    "r3=const:0x40010000 r4=range:0..3145728 @r3.48=const:0x40020000 @r3.52=range:0..4 "
    "@r3.0x10000=const:0x0 @r3.0x10004=const:0x0 @r3.0x10008=const:0x3fd00000 @r3.0x1000c=const:0x0 "
    "@r3.0x10010=const:0x3fc00000 @r3.0x10014=const:0x0 @r3.0x10018=const:0x3ff00000 @r3.0x1001c=const:0x0 "
    "@r3.0x10020=const:0x3fe00000 @r3.0x10024=const:0x0 @r3.0x10028=const:0x3fd00000 @r3.0x1002c=const:0x0 "
    "@r3.0x10030=const:0x40000000 @r3.0x10034=const:0x0 @r3.0x10038=const:0x3fe80000 @r3.0x1003c=const:0x0 "
    "@r3.0x10040=const:0x3fd80000 @r3.0x10044=const:0x0 @r3.0x10048=const:0x40080000 @r3.0x1004c=const:0x0 "
    "@r3.0x10050=const:0x3ff00000 @r3.0x10054=const:0x0 @r3.0x10058=const:0x3fe00000 @r3.0x1005c=const:0x0 "
);
