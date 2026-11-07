#include <nier/decomp.h>
using namespace nier::guest;

// memchr-equivalent with a byte-valued target. Full guest pointer is returned.
NIER_DECOMP(sub_825DA010) {
    const uint64_t start=ctx.r3.u64;const int32_t target=ctx.r4.s32;
    for(uint32_t i=0;i<ctx.r5.u32;++i)
        if(ld8(base,uint32_t(start)+i)==target) {ctx.r3.u64=start+i;return;}
    ctx.r3.u64=0;
}
NIER_VERIFY_SPEC(sub_825DA010,0x825DA010,"r3=bptr r4=u8 r5=range:0..512");

// memcmp's result magnitude is observable: unequal 8-byte groups return +/-1,
// while the final byte tail returns the actual unsigned-byte difference.
NIER_DECOMP(sub_825DAB20) {
    uint32_t a=ctx.r3.u32,b=ctx.r4.u32,n=ctx.r5.u32;
    while(n>=8) {
        const uint64_t x=ld64(base,a),y=ld64(base,b);
        if(x!=y) {ctx.r3.s64=x<y?-1:1;return;}
        a+=8;b+=8;n-=8;
    }
    while(n--) {
        const int diff=int(ld8(base,a++))-int(ld8(base,b++));
        if(diff) {ctx.r3.s64=diff;return;}
    }
    ctx.r3.u64=0;
}
NIER_VERIFY_SPEC(sub_825DAB20,0x825DAB20,"r3=bptr r4=bptr r5=range:0..512 @r3.0=range:0..3 @r3.4=range:0..3 @r4.0=range:0..3 @r4.4=range:0..3");
