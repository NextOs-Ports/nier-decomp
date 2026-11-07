// Reconstructed CRT classifications and error-code translation.
#include <nier/decomp.h>
using namespace nier::guest;

// 0 finite, 1 +infinity, 2 -infinity, 3 quiet NaN, 4 signaling NaN.
NIER_DECOMP(sub_825DE3D0) {
    const uint64_t bits=ctx.f1.u64,magnitude=bits&0x7fffffffffffffffull;
    st64(base,ctx.r1.u32+16,bits);
    if(magnitude==0x7ff0000000000000ull)ctx.r3.u64=(bits>>63)?2:1;
    else if(magnitude>0x7ff0000000000000ull)ctx.r3.u64=(bits&(1ull<<51))?3:4;
    else ctx.r3.u64=0;
}
NIER_VERIFY_SPEC(sub_825DE3D0,0x825DE3D0,"f1=f32");

// Ordered selection from a floating-point status mask.
NIER_DECOMP(sub_825E5270) {
    const uint32_t flags=ctx.r3.u32;
    ctx.r3.u64=(flags&32)?5:(flags&8)?1:(flags&4)?2:(flags&1)?3:(flags&2)?4:0;
}
NIER_VERIFY_SPEC(sub_825E5270,0x825E5270,"r3=int");

// Error translation consults the guest's 45-entry map, then range fallbacks.
NIER_DECOMP(sub_825D4FC8) {
    const uint32_t error=ctx.r3.u32;
    for(uint32_t i=0;i<45;++i) {
        const uint32_t row=0x8278e350+i*8;
        if(ld32(base,row)==error) {ctx.r3.u64=ld32(base,row+4);return;}
    }
    ctx.r3.u64=error>=19 && error<=36?13:error>=188 && error<=202?8:22;
}
NIER_VERIFY_SPEC(sub_825D4FC8,0x825D4FC8,"r3=range:0..512");

// Locale-backed ctype mask lookup accepts EOF (-1) and byte values.
NIER_DECOMP(sub_825DCC78) {
    const int32_t ch=ctx.r3.s32;
    if(ch < -1 || ch > 255) {ctx.r3.u64=0;return;}
    const uint32_t locale=ld32(base,0x8278e868),table=ld32(base,locale+200);
    ctx.r3.u64=ld16(base,table+uint32_t(ch)*2)&ctx.r4.u64;
}
NIER_VERIFY_SPEC(sub_825DCC78,0x825DCC78,"r3=range:0xfffffffe..257 r4=int");
