// Validation contracts for PCM, ADPCM and the two XMA-format header layouts.
#include <nier/decomp.h>
#include <bit>
using namespace nier::guest;

NIER_DECOMP(sub_824B6668) {
    const uint32_t p=ctx.r3.u32,bits=ld16(base,p+14),channels=ld16(base,p+2),align=ld16(base,p+12);
    bool valid=bits==8 || bits==16 || bits==24 || bits==32;
    if(ld16(base,p)==0xfffe) {
        const uint32_t significant=ld16(base,p+18);
        valid &= (significant==0 || significant==8 || significant==16 || significant==20 || significant==24 || significant==32) && significant<=bits;
    }
    valid &= int32_t(channels*bits)/8==int32_t(align);
    valid &= ld32(base,p+4)*align==ld32(base,p+8);
    ctx.r3.u64=valid;
}
NIER_VERIFY_SPEC(sub_824B6668,0x824B6668,
 "r3=ptr:4 @r3.0=const:0xfffe0002 @r3.4=const:48000 @r3.8=const:192000 @r3.12=const:0x00040010 @r3.16=range:0..32");

NIER_DECOMP(sub_824B6738) {
    const uint32_t p=ctx.r3.u32,channels=ld16(base,p+2);
    if((channels!=1 && channels!=2) || ld16(base,p+14)!=4 || ld16(base,p+16)!=32) {ctx.r3.u64=0;return;}
    bool valid=ld16(base,p+20)==7;
    if(valid)for(uint32_t i=0;i<7;++i) {
        valid &= ld16(base,p+22+i*4)==ld16(base,0x8206a654+i*2);
        valid &= ld16(base,p+24+i*4)==ld16(base,0x8206a664+i*2);
    }
    const uint32_t samples=ld16(base,p+18),align=ld16(base,p+12);
    const int32_t payload_bits=int32_t((align-channels*7)*8);
    valid &= uint32_t(payload_bits/int32_t(channels*4)+2)==samples && samples!=0;
    ctx.r3.u64=valid;
}
NIER_VERIFY_SPEC(sub_824B6738,0x824B6738,
 "r3=ptr:4 @r3.0=range:0x00020001..0x00020002 @r3.12=const:0x01000004 @r3.16=const:0x002000f4 @r3.20=const:0x00070000");

NIER_DECOMP(sub_824B6A00) {
    const uint32_t p=ctx.r3.u32;
    if(ld16(base,p+16)<32) {ctx.r3.u64=0;return;}
    const uint32_t streams=ld16(base,p+8),rate=ld32(base,p+16);
    bool valid=ld16(base,p+2)==16 && ld8(base,p+11)==2 && streams!=0 && rate!=0;
    for(uint32_t i=0;i<streams;++i) {
        const uint32_t stream=p+16+i*20,channels=ld8(base,stream+13),mask=ld16(base,stream+14);
        const uint8_t packed=ld8(base,stream+12);
        valid &= ld32(base,stream)==rate && (packed&15)<=4 && (packed&240)<=48;
        valid &= channels==1 || channels==2;
        if(mask)valid &= std::popcount(mask)==channels;
    }
    ctx.r3.u64=valid;
}
NIER_VERIFY_SPEC(sub_824B6A00,0x824B6A00,
 "r3=ptr:4 @r3.0=const:0x01650010 @r3.8=const:0x00010002 @r3.16=range:0x00200000..0x00210000 @r3.28=range:0..0x00400000");

NIER_DECOMP(sub_824B6B30) {
    const uint32_t p=ctx.r3.u32,channels=ld16(base,p+2);
    bool valid=ld16(base,p+12)==channels*2 && ld16(base,p+14)==16;
    if(ld16(base,p+16)!=34) {ctx.r3.u64=0;return;}
    const uint32_t block_bytes=ld32(base,p+28),mask=ld32(base,p+20),total=ld32(base,p+24);
    valid &= ld8(base,p+49)>=3 && ld16(base,p+50)!=0 && block_bytes!=0 && block_bytes<=0x7ff800;
    if(mask)valid &= std::popcount(mask)==channels;
    valid &= ld16(base,p+18)==(channels+1)/2;
    valid &= ld32(base,p+32)+ld32(base,p+36)<=total;
    valid &= ld32(base,p+40)+ld32(base,p+44)<=total;
    ctx.r3.u64=valid;
}
NIER_VERIFY_SPEC(sub_824B6B30,0x824B6B30,
 "r3=ptr:4 @r3.0=const:0x01660002 @r3.12=const:0x00040010 @r3.16=const:0x00220001 @r3.20=const:3 "
 "@r3.24=range:0..10000 @r3.28=range:0..0x800000 @r3.32=const:0 @r3.36=range:0..12000 "
 "@r3.40=const:0 @r3.44=const:0 @r3.48=const:0x00030001");
