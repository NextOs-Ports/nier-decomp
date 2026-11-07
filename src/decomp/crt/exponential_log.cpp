// Linked CRT exp/log approximations with original guest coefficients and FMA
// order. Do not replace them with the host library's different approximations.
#include <nier/decomp.h>
#include <bit>
#include <cmath>
#include <limits>
using namespace nier::guest;
namespace {
double d(const uint8_t* base,uint32_t address) {return std::bit_cast<double>(ld64(base,address));}
int64_t rounded_integer(double value) {
    constexpr int64_t minimum=std::numeric_limits<int64_t>::min();
    if(std::isnan(value))return minimum;
    if(value>0x1p63)return std::numeric_limits<int64_t>::max();
    const double rounded=std::nearbyint(value);
    if(rounded>=0x1p63 || rounded<=-0x1p63)return minimum;
    return int64_t(rounded);
}
}
NIER_DECOMP(sub_825D9AC0) {
    ctx.fpscr.disableFlushMode();constexpr uint32_t table=0x8278e690;
    const double input=ctx.f1.f64,multiple=double(rounded_integer(d(base,table+32)*input));
    double reduced=-std::fma(d(base,table+40),multiple,-input);
    reduced=-std::fma(d(base,table+48),multiple,-reduced);
    const double square=reduced*reduced;
    double numerator=std::fma(d(base,table+72),square,d(base,table+64));
    double denominator=std::fma(d(base,table+96),square,d(base,table+88));
    numerator=std::fma(numerator,square,d(base,table+56));
    denominator=std::fma(denominator,square,d(base,table+80));
    numerator*=reduced;
    double result=numerator/(denominator-numerator)+double(ldf(base,table+108));
    const uint32_t adjustment=fctiwz(multiple)+1;
    if(adjustment) {
        uint64_t bits=std::bit_cast<uint64_t>(result);
        const uint16_t top=uint16_t((bits>>48)&0x800f)|uint16_t((uint32_t((bits>>52)&2047)+adjustment)<<4);
        result=std::bit_cast<double>((bits&0x0000ffffffffffffull)|(uint64_t(top)<<48));
    }
    if(input-d(base,table)>=0)result=d(base,table+16);
    if(d(base,table+8)-input>=0)result=ldf(base,table+104);
    ctx.f1.f64=result;
}
NIER_VERIFY_SPEC(sub_825D9AC0,0x825D9AC0,"f1=f32");

NIER_DECOMP(sub_825D4568) {
    ctx.fpscr.disableFlushMode();constexpr uint32_t table=0x820aa448;
    double input=ctx.f1.f64;uint64_t bits=ctx.f1.u64;
    st64(base,ctx.r1.u32+16,bits);
    const double one=d(base,0x8200ce10),zero=d(base,0x8200ca18);
    if(input==one) {ctx.f1.f64=zero;return;}
    if(((bits>>52)&2047)==2047) {
        if(input>d(base,table+136))return;
        ctx.f1.f64=-d(base,0x8278e8c8);return;
    }
    if(!(input>zero)) {ctx.f1.f64=input==zero?-d(base,0x8278e8c0):-d(base,0x8278e8c8);return;}
    int exponent;
    if(input<d(base,table+128)) {
        input*=d(base,table+120);bits=std::bit_cast<uint64_t>(input);
        st64(base,ctx.r1.u32+16,bits);exponent=int((bits>>52)&2047)-1075;
    } else exponent=int((bits>>52)&2047)-1022;
    const double mantissa=std::bit_cast<double>((bits&0x800fffffffffffffull)|(uint64_t(1022)<<52));
    const double half=d(base,0x8200f980);
    double numerator,denominator;
    if(mantissa>d(base,table)) {denominator=(mantissa+one)*half;numerator=(mantissa-half)-half;}
    else {--exponent;numerator=mantissa-half;denominator=(numerator+one)*half;}
    const double reduced=numerator/denominator,square=reduced*reduced;
    double p=-std::fma(square,d(base,table+112),-d(base,table+40));
    double q=square-d(base,table+104);
    p=std::fma(p,square,-d(base,table+96));q=std::fma(q,square,d(base,table+64));
    p*=square;q=std::fma(q,square,-d(base,table+88));
    const double correction=p/q+one;
    const double small=double(exponent)*d(base,table+80);
    const double value=std::fma(correction,reduced,-small);
    ctx.f1.f64=std::fma(double(exponent),d(base,table+8),value);
}
NIER_VERIFY_SPEC(sub_825D4568,0x825D4568,"f1=f32");
