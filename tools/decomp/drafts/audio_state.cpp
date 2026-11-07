// UNVERIFIED DRAFT: paused for C6; excluded from build and verified totals.
// Format callback selection, bit-width state and per-channel parameters.
#include <nier/decomp.h>
#include <algorithm>
using namespace nier::guest;
NIER_DECOMP(sub_824C2518) {
    uint32_t reader=0x824c18e0,writer=0x824c1b80;
    switch(ctx.r5.s32) {
    case 0:reader=0x824c1c80;writer=0x824c1c38;break;
    case 61:reader=0x824c1b30;writer=0x824c1bd8;break;
    case 78:reader=0x824c1b58;writer=0x824c1c10;break;
    case 94:reader=0x824c1b40;writer=0x824c1be8;break;
    }
    st32(base,ctx.r4.u32,writer);st32(base,ctx.r3.u32,reader);
}
NIER_VERIFY_SPEC(sub_824C2518,0x824C2518,"r3=const:0x40010000 r4=const:0x40020000 r5=range:0..100");
NIER_DECOMP(sub_824C6990) {
    const uint32_t p=ctx.r3.u32;const int rate=ctx.r4.s32;
    const unsigned bits=int32_t(ld32(base,p+60))>=3?31:rate<15?13:rate<32?12:rate<40?11:rate<45?10:9;
    st32(base,p+52,bits);st32(base,p+56,(1u<<bits)-1);
}
NIER_VERIFY_SPEC(sub_824C6990,0x824C6990,"r3=ptr:4 r4=range:0..64 @r3.60=range:0..4");
NIER_DECOMP(sub_824C1880) {
    const uint32_t p=ctx.r5.u32;st32(base,p+48,0);
    if(ctx.r6.s32!=3 || !ld32(base,p+40) || !ctx.r3.u32 || ctx.r4.s32<=0)return;
    for(int32_t i=0;i<ctx.r4.s32;++i)if(ld32(base,ctx.r3.u32+uint32_t(i)*4)) {st32(base,p+48,1);break;}
}
NIER_VERIFY_SPEC(sub_824C1880,0x824C1880,
 "r3=const:0x40010000 r4=range:0xffffffff..16 r5=const:0x40020000 r6=range:0..4 @r5.40=range:0..1");
NIER_DECOMP(sub_824D1178) {
    const uint32_t p=ctx.r3.u32;uint32_t node;
    if(ld32(base,p+176)) {const uint32_t link=ld32(base,p+172);node=link?ld32(base,link):0;}
    else {node=ld32(base,p+184);if(!node)return;}
    st32(base,node+8,ld32(base,node+8)|64);
}
NIER_VERIFY_SPEC(sub_824D1178,0x824D1178,
 "r3=const:0x40010000 @r3.176=range:0..1 @r3.172=const:0x40020000 @r3.0x10000=const:0x40030000 @r3.184=const:0x40030000");
NIER_DECOMP(sub_824C1488) {
    const uint32_t p=ctx.r3.u32,a=ld32(base,p+252),b=ld32(base,p+256),limit=ld32(base,p+268),shift=ld32(base,p+456);
    if(ld32(base,p+444)) {
        st32(base,p+464,sraw(a,shift));st32(base,p+468,sraw(b,shift));
        st32(base,p+472,uint32_t(std::min(int32_t(limit),int32_t(ctx.r4.s16))));
    } else if(ld32(base,p+448)) {
        st32(base,p+464,slw(a,shift));st32(base,p+468,slw(b,shift));st32(base,p+472,limit);
    } else {st32(base,p+464,a);st32(base,p+468,b);st32(base,p+472,limit);}
}
NIER_VERIFY_SPEC(sub_824C1488,0x824C1488,
 "r3=ptr:4 r4=u16 @r3.444=range:0..1 @r3.448=range:0..1 @r3.456=range:0..63");
NIER_DECOMP(sub_824C1378) {
    const uint32_t p=ctx.r3.u32,dst=ctx.r4.u32,shift=ld32(base,p+456);
    const bool right=ld32(base,p+444)!=0,left=ld32(base,p+448)!=0;
    const uint32_t initial=uint32_t(int32_t(int16_t(ld16(base,dst+118))));
    if(!right && !left) {st16(base,dst+120,uint16_t(initial));st32(base,dst+140,initial);return;}
    const uint32_t transformed=right?sraw(initial,shift):slw(initial,shift);
    st16(base,dst+120,uint16_t(transformed));st32(base,dst+140,transformed);
    for(unsigned offset:{122u,124u,126u}) {
        const uint32_t value=uint32_t(int32_t(int16_t(ld16(base,dst+offset))));
        st16(base,dst+offset,uint16_t(right?sraw(value,shift):slw(value,shift)));
    }
    if(right)ctx.r3.u64=shift;
}
NIER_VERIFY_SPEC(sub_824C1378,0x824C1378,
 "r3=const:0x40010000 r4=const:0x40020000 @r3.444=range:0..1 @r3.448=range:0..1 @r3.456=range:0..63");
NIER_DECOMP(sub_824C3C50) {
    const uint32_t p=ctx.r3.u32;
    if(int32_t(ld32(base,p+60))<=2 || ld32(base,p+176)) {ctx.r3.u64=0;return;}
    const uint32_t records=ld32(base,p+320),channels=ld16(base,p+34),shift=ld32(base,p+456);
    const bool right=ld32(base,p+444)!=0,left=ld32(base,p+448)!=0;
    int32_t peak_a=0,peak_b=0;
    for(uint32_t channel=0;channel<channels;++channel) {
        const uint32_t sample=ld32(base,ld32(base,records+channel*1776+424)+8);
        uint32_t a=uint32_t(int32_t(int16_t(ld16(base,sample-2)))),b=uint32_t(int32_t(int16_t(ld16(base,sample))));
        if(right) {a=sraw(a,shift);b=sraw(b,shift);}else if(left) {a=slw(a,shift);b=slw(b,shift);}
        peak_a=std::max(peak_a,int32_t(a));peak_b=std::max(peak_b,int32_t(b));
    }
    ctx.r3.s64=int32_t(uint32_t(peak_a)+uint32_t(peak_b))/2;
}
NIER_VERIFY_SPEC(sub_824C3C50,0x824C3C50,
 "r3=const:0x40010000 @r3.32=const:3 @r3.60=range:0..4 @r3.176=range:0..1 @r3.320=const:0x40020000 "
 "@r3.444=range:0..1 @r3.448=range:0..1 @r3.456=range:0..63 "
 "@r3.0x101a8=const:0x40030000 @r3.0x10898=const:0x40030020 @r3.0x10f88=const:0x40030040 "
 "@r3.0x20008=const:0x40040002 @r3.0x20028=const:0x40040012 @r3.0x20048=const:0x40040022");
