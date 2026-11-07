#include <nier/decomp.h>
using namespace nier::guest;

// Thirteen intrusive hash buckets. Preserve unspecified payload/padding fields.
NIER_DECOMP(sub_824B2680) {
    const uint32_t object=ctx.r3.u32;
    st32(base,object,ctx.r4.u32);st32(base,object+4,ctx.r5.u32);
    for(unsigned i=0;i<13;++i) {
        const uint32_t bucket=object+8+i*40;
        st32(base,bucket,bucket+12);st32(base,bucket+4,bucket+8);
        for(unsigned offset:{8u,16u,24u,28u,32u})st32(base,bucket+offset,0);
        st32(base,bucket+36,1);
    }
    for(unsigned offset=528;offset<=552;offset+=4)st32(base,object+offset,0);
}
NIER_VERIFY_SPEC(sub_824B2680,0x824B2680,"r3=ptr:4 r4=int r5=int");

// Pop a queued payload and return its node to the manager's free list.
NIER_DECOMP(sub_824B54A0) {
    const uint32_t object=ctx.r3.u32,node=ld32(base,object+204);
    if(!node) {ctx.r3.u64=0;return;}
    const uint32_t next=ld32(base,node+4),payload=ld32(base,node);
    st32(base,object+204,next);
    if(next)st32(base,next+8,0);else st32(base,object+208,0);
    st32(base,node+4,ld32(base,object));st32(base,object,node);
    st32(base,object+212,ld32(base,object+212)-1);ctx.r3.u64=payload;
}
NIER_VERIFY_SPEC(sub_824B54A0,0x824B54A0,
 "r3=const:0x40010000 @r3.204=const:0x40020000 @r3.0x10004=const:0x40030000 @r3.212=range:1..16");

// Lookup requires a present key, as does the original routine (a missing key
// eventually dereferences a null payload). Keys select one of thirteen chains.
NIER_DECOMP(sub_824B25A0) {
    const int32_t key=ctx.r4.s32;
    uint32_t node=ld32(base,ctx.r3.u32+32+uint32_t((key%13)*40));
    for(;;) {
        const uint32_t payload=node?ld32(base,node):0;
        const uint32_t next=node?ld32(base,node+4):0;
        if(ld32(base,payload+48)==uint32_t(key)) {ctx.r3.u64=payload;return;}
        node=next;
    }
}
// All thirteen buckets, both chain positions, keys guaranteed present.
NIER_VERIFY_SPEC(sub_824B25A0,0x824B25A0,
    "r3=const:0x40010000 r4=range:0..25 @r3.0x20=const:0x40020000 @r3.0x10000=const:0x40030000 "
    "@r3.0x10004=const:0x40020010 @r3.0x10010=const:0x40030080 @r3.0x20030=const:0x0 @r3.0x200b0=const:0xd "
    "@r3.0x48=const:0x40020020 @r3.0x10020=const:0x40030100 @r3.0x10024=const:0x40020030 @r3.0x10030=const:0x40030180 "
    "@r3.0x20130=const:0x1 @r3.0x201b0=const:0xe @r3.0x70=const:0x40020040 @r3.0x10040=const:0x40030200 "
    "@r3.0x10044=const:0x40020050 @r3.0x10050=const:0x40030280 @r3.0x20230=const:0x2 @r3.0x202b0=const:0xf "
    "@r3.0x98=const:0x40020060 @r3.0x10060=const:0x40030300 @r3.0x10064=const:0x40020070 @r3.0x10070=const:0x40030380 "
    "@r3.0x20330=const:0x3 @r3.0x203b0=const:0x10 @r3.0xc0=const:0x40020080 @r3.0x10080=const:0x40030400 "
    "@r3.0x10084=const:0x40020090 @r3.0x10090=const:0x40030480 @r3.0x20430=const:0x4 @r3.0x204b0=const:0x11 "
    "@r3.0xe8=const:0x400200a0 @r3.0x100a0=const:0x40030500 @r3.0x100a4=const:0x400200b0 @r3.0x100b0=const:0x40030580 "
    "@r3.0x20530=const:0x5 @r3.0x205b0=const:0x12 @r3.0x110=const:0x400200c0 @r3.0x100c0=const:0x40030600 "
    "@r3.0x100c4=const:0x400200d0 @r3.0x100d0=const:0x40030680 @r3.0x20630=const:0x6 @r3.0x206b0=const:0x13 "
    "@r3.0x138=const:0x400200e0 @r3.0x100e0=const:0x40030700 @r3.0x100e4=const:0x400200f0 @r3.0x100f0=const:0x40030780 "
    "@r3.0x20730=const:0x7 @r3.0x207b0=const:0x14 @r3.0x160=const:0x40020100 @r3.0x10100=const:0x40030800 "
    "@r3.0x10104=const:0x40020110 @r3.0x10110=const:0x40030880 @r3.0x20830=const:0x8 @r3.0x208b0=const:0x15 "
    "@r3.0x188=const:0x40020120 @r3.0x10120=const:0x40030900 @r3.0x10124=const:0x40020130 @r3.0x10130=const:0x40030980 "
    "@r3.0x20930=const:0x9 @r3.0x209b0=const:0x16 @r3.0x1b0=const:0x40020140 @r3.0x10140=const:0x40030a00 "
    "@r3.0x10144=const:0x40020150 @r3.0x10150=const:0x40030a80 @r3.0x20a30=const:0xa @r3.0x20ab0=const:0x17 "
    "@r3.0x1d8=const:0x40020160 @r3.0x10160=const:0x40030b00 @r3.0x10164=const:0x40020170 @r3.0x10170=const:0x40030b80 "
    "@r3.0x20b30=const:0xb @r3.0x20bb0=const:0x18 @r3.0x200=const:0x40020180 @r3.0x10180=const:0x40030c00 "
    "@r3.0x10184=const:0x40020190 @r3.0x10190=const:0x40030c80 @r3.0x20c30=const:0xc @r3.0x20cb0=const:0x19 "
);

// Force mode 2, or request mode 1 only while no earlier request is pending.
NIER_DECOMP(sub_824B2848) {
    if(ctx.r4.u32)st32(base,ctx.r3.u32+572,2);
    else if(!ld32(base,ctx.r3.u32+572))st32(base,ctx.r3.u32+572,1);
}
NIER_VERIFY_SPEC(sub_824B2848,0x824B2848,"r3=ptr r4=range:0..1 @r3.572=range:0..2");

// Fetch the payload and mark the object as accessed/ready.
NIER_DECOMP(sub_824B5DD0) {
    const uint32_t object=ctx.r3.u32,payload=ld32(base,object+20);
    st32(base,object+60,1);ctx.r3.u64=payload;
}
NIER_VERIFY_SPEC(sub_824B5DD0,0x824B5DD0,"r3=ptr");
