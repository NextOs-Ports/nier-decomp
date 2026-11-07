// Audio object construction/reset leaves; unwritten padding stays untouched.
#include <nier/decomp.h>
#include <initializer_list>
using namespace nier::guest;
namespace {
void clear_words(uint8_t* base,uint32_t p,std::initializer_list<unsigned> offsets) {
    for(unsigned offset:offsets)st32(base,p+offset,0);
}
void single_bucket(uint8_t* base,uint32_t p) {
    st32(base,p,p+12);st32(base,p+4,p+8);
    clear_words(base,p,{8,16,24,28,32});st32(base,p+36,1);
}
void reset_buffer(uint8_t* base,uint32_t p,uint32_t owner) {
    clear_words(base,p,{32,36,40,44,48});
    if(ld32(base,owner+704))clear_words(base,p,{20,24,28,32,68,76,80});
    st32(base,p+84,0x8211ced0);
}
}
NIER_DECOMP(sub_824DBA10) {
    const uint32_t p=ctx.r3.u32;
    st32(base,p,0x82086eb8);st32(base,p+4,ctx.r4.u32);clear_words(base,p,{8,12});
    single_bucket(base,p+16);single_bucket(base,p+56);st32(base,p+96,1);
    for(unsigned i=0;i<4;++i)st32(base,p+100+i*4,ld32(base,0x82086ee8+i*4));
    st32(base,p+116,1);
}
NIER_VERIFY_SPEC(sub_824DBA10,0x824DBA10,"r3=ptr:4 r4=int");

NIER_DECOMP(sub_824DEF60) {
    ctx.fpscr.disableFlushMode();const uint32_t p=ctx.r3.u32;
    st32(base,p,0x8208722c);st32(base,p+4,ctx.r5.u32);st32(base,p+24,ctx.r4.u32);st32(base,p+28,ctx.r6.u32);
    clear_words(base,p,{8,12,16,20,32});const uint32_t gain=ld32(base,0x820007a4);
    for(unsigned i=0;i<8;++i) {st32(base,p+36+i*4,i);st32(base,p+68+i*4,i);st32(base,p+100+i*4,gain);}
}
NIER_VERIFY_SPEC(sub_824DEF60,0x824DEF60,"r3=ptr:4 r4=int r5=int r6=int");

NIER_DECOMP(sub_824DF650) {
    ctx.fpscr.disableFlushMode();const uint32_t p=ctx.r3.u32;
    st32(base,p,0x82087238);st32(base,p+4,ctx.r5.u32);st32(base,p+24,ctx.r4.u32);
    clear_words(base,p,{8,12,16,20,136,148,152,156,160});
    st32(base,p+132,8);st32(base,p+140,128);st32(base,p+144,ld32(base,0x8200c2bc));
    for(unsigned i=0;i<8;++i)st32(base,p+68+i*4,p+164+i*516);
}
NIER_VERIFY_SPEC(sub_824DF650,0x824DF650,"r3=ptr:4 r4=int r5=int");

NIER_DECOMP(sub_824D1388) {
    const uint32_t p=ctx.r3.u32;
    st32(base,p,p+24);st32(base,p+4,p+8);st32(base,p+28,p+12);
    clear_words(base,p,{8,16,36,40,44});st32(base,p+48,2);
}
NIER_VERIFY_SPEC(sub_824D1388,0x824D1388,"r3=ptr:4");

NIER_DECOMP(sub_824C50B0) {
    const uint32_t p=ctx.r3.u32,owner=ctx.r4.u32;
    clear_words(base,p,{0,4,12,16,20,24,28,76});st32(base,p+8,owner);st32(base,p+52,15);
    st32(base,p+56,ld32(base,ld32(base,owner)+212));st32(base,p+60,1);reset_buffer(base,p,owner);
}
NIER_VERIFY_SPEC(sub_824C50B0,0x824C50B0,
 "r3=const:0x40010000 r4=const:0x40020000 @r4.0=const:0x40030000 @r4.704=range:0..1");
NIER_DECOMP(sub_824C4AE8) {const uint32_t p=ctx.r3.u32;reset_buffer(base,p,ld32(base,p+8));}
NIER_VERIFY_SPEC(sub_824C4AE8,0x824C4AE8,
 "r3=const:0x40010000 @r3.8=const:0x40020000 @r3.0x102c0=range:0..1");
NIER_DECOMP(sub_824CA758) {for(unsigned i=0;i<6;++i)st64(base,ctx.r3.u32+i*8,0);}
NIER_VERIFY_SPEC(sub_824CA758,0x824CA758,"r3=ptr:4");

NIER_DECOMP(sub_824B6150) {
    const uint32_t p=ctx.r3.u32;st32(base,p,0x8206a64c);st32(base,p+4,0x8206a620);
    st32(base,p+12,ctx.r4.u32);st32(base,p+16,ctx.r5.u32);
    clear_words(base,p,{8,20,24,28,32,36,40,44,48,56,60,64,68});
}
NIER_VERIFY_SPEC(sub_824B6150,0x824B6150,"r3=ptr:4 r4=int r5=int");
NIER_DECOMP(sub_824B4EC8) {
    const uint32_t p=ctx.r3.u32;
    st32(base,p,0x8206a534);st32(base,p+4,ctx.r7.u32);st32(base,p+8,ctx.r6.u32);
    st32(base,p+12,ctx.r4.u32);st32(base,p+16,ctx.r5.u32);st32(base,p+20,0);
}
NIER_VERIFY_SPEC(sub_824B4EC8,0x824B4EC8,"r3=ptr:4 r4=int r5=int r6=int r7=int");
NIER_DECOMP(sub_824B7EE8) {
    const uint32_t p=ctx.r3.u32;st32(base,p+4,ctx.r4.u32);clear_words(base,p,{8,12,24});
    st32(base,p+16,7);st32(base,p+20,1);st32(base,p+28,1);
}
NIER_VERIFY_SPEC(sub_824B7EE8,0x824B7EE8,"r3=ptr:4 r4=int");
