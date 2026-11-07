// Numerical CRT helpers reconstructed at the level of their observable results.
#include <nier/decomp.h>
#include <bit>
#include <cmath>
using namespace nier::guest;

// Unsigned 64-bit -> binary64, truncating discarded mantissa bits. This helper
// also leaves the constructed bit pattern in r3 (not just the f1 result).
NIER_DECOMP(sub_825DAAF0) {
    const uint64_t value=ctx.r3.u64;uint64_t bits=0;
    if(value) {
        const unsigned leading=std::countl_zero(value);
        bits=(uint64_t(1086-leading)<<52) | (((value<<leading)>>11)&0x000fffffffffffffull);
    }
    ctx.r3.u64=bits;ctx.f1.u64=bits;
}
NIER_VERIFY_SPEC(sub_825DAAF0,0x825DAAF0,"r3=range:0..0xfffffffffffffffe");

namespace {
double rounded_zero() { volatile double zero=0.0; return zero-zero; }
bool preserve_rounding_input(uint64_t bits) {
    const uint64_t magnitude=bits&0x7fffffffffffffffull;
    // Inputs >= 2^52 are already integral; this also preserves infinities/NaNs.
    return magnitude==0 || magnitude>=0x4330000000000000ull;
}
}
// Ceil helper; a fractional input rounding to zero uses the sign produced by
// exact cancellation in the current rounding mode (negative under downward).
NIER_DECOMP(sub_825D91D8) {
    ctx.fpscr.disableFlushMode();
    if(preserve_rounding_input(ctx.f1.u64))return;
    const double value=std::ceil(ctx.f1.f64);
    ctx.f1.f64=value==0?rounded_zero():value;
}
NIER_VERIFY_SPEC(sub_825D91D8,0x825D91D8,"f1=f32");

NIER_DECOMP(sub_825D9218) {
    ctx.fpscr.disableFlushMode();
    if(preserve_rounding_input(ctx.f1.u64))return;
    const double value=std::floor(ctx.f1.f64);
    ctx.f1.f64=value==0?rounded_zero():value;
}
NIER_VERIFY_SPEC(sub_825D9218,0x825D9218,"f1=f32");

// Legacy rounding helper: ordered add/subtract 2^52 in the current rounding
// mode. Keep the signed-zero behavior of exact cancellation, not std::round.
NIER_DECOMP(sub_825E4B80) {
    ctx.fpscr.disableFlushMode();
    if(preserve_rounding_input(ctx.f1.u64))return;
    const double value=ctx.f1.f64;
    if(value>=0) {volatile double shifted=value+0x1p52;ctx.f1.f64=shifted-0x1p52;}
    else {volatile double shifted=value-0x1p52;ctx.f1.f64=shifted+0x1p52;}
}
NIER_VERIFY_SPEC(sub_825E4B80,0x825E4B80,"f1=f32");

// Classify via integer/binary64 round trips: 0 noninteger/out-of-range,
// 1 odd, 2 even. Use the SDK conversion primitive at the +2^63 boundary:
// its ARM saturation and x86 invalid-conversion result differ there.
NIER_DECOMP(sub_825D93E0) {
    ctx.fpscr.disableFlushMode();
    st64(base,ctx.r1.u32+16,ctx.f1.u64);
    const double value=ctx.f1.f64;
    if((ctx.f1.u64&0x7fffffffffffffffull)>=0x7ff0000000000000ull) {ctx.r3.u64=0;return;}
    auto integer_round_trip=[](double number) {
        if(number>0x1p63)return double(INT64_MAX);
        return double(simde_mm_cvtsd_si64(simde_mm_load_sd(&number)));
    };
    if(integer_round_trip(value)!=value) {ctx.r3.u64=0;return;}
    const double half=value*0.5;
    ctx.r3.u64=integer_round_trip(half)==half?2:1;
}
NIER_VERIFY_SPEC(sub_825D93E0,0x825D93E0,"f1=f32");
