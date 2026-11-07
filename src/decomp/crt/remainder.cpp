// Exact binary remainder using integer significands, independent of host libm.
#include <nier/decomp.h>
#include <bit>
using namespace nier::guest;
namespace {
constexpr uint64_t fraction_mask=0x000fffffffffffffull,hidden=1ull<<52;
struct Binary {uint64_t significand;int exponent;};
Binary normalized(uint64_t magnitude) {
    const int exponent=int(magnitude>>52);
    if(exponent)return {(magnitude&fraction_mask)|hidden,exponent};
    const unsigned shift=std::countl_zero(magnitude)-11;
    return {magnitude<<shift,1-int(shift)};
}
}
NIER_DECOMP(sub_825D9258) {
    const uint64_t input=ctx.f1.u64,other=ctx.f2.u64,sign=input&0x8000000000000000ull;
    const uint64_t a=input&0x7fffffffffffffffull,b=other&0x7fffffffffffffffull;
    constexpr uint64_t infinity=0x7ff0000000000000ull;
    st64(base,ctx.r1.u32+16,input);st64(base,ctx.r1.u32+24,other);
    if(a>=infinity || b>infinity || !b) {ctx.f1.u64=ld64(base,0x8278e8c8);return;}
    if(b==infinity || a<b)return;
    Binary numerator=normalized(a),denominator=normalized(b);
    while(true) {
        if(numerator.significand>=denominator.significand)
            numerator.significand-=denominator.significand;
        if(!numerator.significand) {ctx.f1.u64=sign;return;}
        if(numerator.exponent==denominator.exponent)break;
        numerator.significand<<=1;--numerator.exponent;
    }
    const unsigned shift=std::countl_zero(numerator.significand)-11;
    numerator.significand<<=shift;numerator.exponent-=int(shift);
    const uint64_t magnitude=numerator.exponent>0?
        (uint64_t(numerator.exponent)<<52)|(numerator.significand&fraction_mask):
        numerator.significand>>(1-numerator.exponent);
    ctx.f1.u64=sign|magnitude;
}
NIER_VERIFY_SPEC(sub_825D9258,0x825D9258,"f1=f32 f2=f32");
