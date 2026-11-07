// CRT bit-level floating-point contracts, including observable argument spills.
#include <nier/decomp.h>
#include <bit>
using namespace nier::guest;
namespace {
constexpr uint64_t sign_bit=0x8000000000000000ull;
constexpr uint64_t fraction_mask=0x000fffffffffffffull;
}

NIER_DECOMP(sub_825E4B40) {
    st64(base,ctx.r1.u32+16,ctx.f1.u64);
    st64(base,ctx.r1.u32+24,ctx.f2.u64);
    ctx.f1.u64=(ctx.f1.u64&~sign_bit)|(ctx.f2.u64&sign_bit);
}
NIER_VERIFY_SPEC(sub_825E4B40,0x825E4B40,"f1=f32 f2=f32");

// Replace the exponent field with (argument + 1022). This is not ldexp:
// the stored 16-bit field can wrap and an out-of-range argument affects sign.
NIER_DECOMP(sub_825DE390) {
    const uint64_t input=ctx.f1.u64;
    st64(base,ctx.r1.u32+16,input);
    const uint16_t top=uint16_t((input>>48)&0x800f) | uint16_t((ctx.r4.u32+1022u)<<4);
    ctx.f1.u64=(input&0x0000ffffffffffffull)|(uint64_t(top)<<48);
}
NIER_VERIFY_SPEC(sub_825DE390,0x825DE390,"f1=f32 r4=int");

// Split a double into a [0.5,1) mantissa and an integer exponent. The original
// also processes nonfinite encodings mechanically, unlike a host frexp call.
NIER_DECOMP(sub_825DE458) {
    ctx.fpscr.disableFlushMode();
    const uint64_t input=ctx.f1.u64,sign=input&sign_bit;
    uint64_t fraction=input&fraction_mask,spill=input;
    int32_t exponent=0;
    if(input&~sign_bit) {
        const unsigned encoded=unsigned((input>>52)&2047);
        if(encoded)exponent=int32_t(encoded)-1022;
        else {
            const unsigned shift=std::countl_zero(fraction)-11;
            fraction=(fraction<<shift)&fraction_mask;
            exponent=-1021-int32_t(shift);
            spill=sign|fraction;
        }
        ctx.f1.u64=sign|fraction|(uint64_t(1022)<<52);
    }
    st64(base,ctx.r1.u32+16,spill);
    st32(base,ctx.r4.u32,uint32_t(exponent));
}
NIER_VERIFY_SPEC(sub_825DE458,0x825DE458,"f1=f32 r4=ptr:4");

// Convert a BE binary64 object to a packed BE extended80 object (10 bytes),
// preserving sign and NaN payload rather than using host long double layout.
NIER_DECOMP(sub_825E79C0) {
    const uint64_t input=ld64(base,ctx.r4.u32);
    const uint16_t sign=uint16_t(input>>48)&0x8000;
    const unsigned encoded=unsigned((input>>52)&2047);
    const uint64_t fraction=input&fraction_mask;
    uint16_t exponent=0;uint64_t significand=0;
    if(encoded) {
        exponent=encoded==2047?32767:uint16_t(encoded+15360);
        significand=sign_bit|(fraction<<11);
    } else if(fraction) {
        const unsigned shift=std::countl_zero(fraction);
        significand=fraction<<shift;
        exponent=uint16_t(15372-shift);
    }
    st16(base,ctx.r3.u32,sign|exponent);
    st64(base,ctx.r3.u32+2,significand);
}
NIER_VERIFY_SPEC(sub_825E79C0,0x825E79C0,"r3=const:0x40010000 r4=const:0x40050000");

// Update selected bits of the linked CRT's global control word; return old.
NIER_DECOMP(sub_825DA3C8) {
    constexpr uint32_t address=0x8278e738;
    const uint32_t old=ld32(base,address),mask=ctx.r4.u32;
    st32(base,address,(old&~mask)|(ctx.r3.u32&mask));
    ctx.r3.u64=old;
}
NIER_VERIFY_SPEC(sub_825DA3C8,0x825DA3C8,"r3=int r4=int");
