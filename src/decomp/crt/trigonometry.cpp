// CRT range reduction and polynomial/rational evaluation. Coefficients remain
// in the guest image; ordered FMA operations match its approximation kernels.
#include <nier/decomp.h>
#include <bit>
#include <cmath>
#include <limits>
using namespace nier::guest;
namespace {
double coefficient(const uint8_t* base,uint32_t address) {return std::bit_cast<double>(ld64(base,address));}
int64_t integral(double value,bool truncate=false) {
    constexpr int64_t minimum=std::numeric_limits<int64_t>::min();
    if(std::isnan(value))return minimum;
    if(value>0x1p63)return std::numeric_limits<int64_t>::max();
    // Preserve the SDK's per-target conversion semantics (its ARM SIMDe
    // fallback uses round(), whereas x86 cvtsd follows MXCSR).
    const auto input=simde_mm_load_sd(&value);
    return truncate?simde_mm_cvttsd_si64(input):simde_mm_cvtsd_si64(input);
}
double sine_polynomial(const uint8_t* base,double remainder) {
    constexpr uint32_t table=0x8278e590;
    const double square=remainder*remainder;
    double value=coefficient(base,table+112);
    for(int offset=104;offset>=56;offset-=8)value=std::fma(value,square,coefficient(base,table+offset));
    return std::fma(value,square,coefficient(base,0x8200ce10))*remainder;
}
double inverse_rational(const uint8_t* base,double square,double reduced) {
    constexpr uint32_t table=0x8278e608;
    double numerator=coefficient(base,table+80);
    for(int offset=72;offset>=48;offset-=8)numerator=std::fma(numerator,square,coefficient(base,table+offset));
    double denominator=coefficient(base,table+120)+square;
    for(int offset=112;offset>=88;offset-=8)denominator=std::fma(denominator,square,coefficient(base,table+offset));
    return ((numerator*square)*reduced)/denominator+reduced;
}
void inverse(PPCContext& ctx,uint8_t* base,uint32_t selector,bool single) {
    ctx.fpscr.disableFlushMode();constexpr uint32_t table=0x8278e608;
    const uint64_t magnitude_bits=ctx.f1.u64&0x7fffffffffffffffull;
    if(magnitude_bits>0x7ff0000000000000ull) {
        // Preserve the variant's NaN sign (asin negative, acos positive).
        // Host FMA lowering/register allocation can otherwise change it.
        ctx.f1.u64=magnitude_bits|0x0008000000000000ull;
        if(!selector)ctx.f1.u64|=0x8000000000000000ull;
        return;
    }
    const double input=ctx.f1.f64,magnitude=std::fabs(input),threshold=ldf(base,table+12);
    double square,reduced;uint32_t index=selector;
    if(!(magnitude<=threshold)) {
        square=(double(ldf(base,table+4))-magnitude)*threshold;
        reduced=-(std::sqrt(square)*double(ldf(base,table+8)));index=1-selector;
    } else {
        square=magnitude*magnitude;reduced=magnitude;
        if(!selector && input==double(ldf(base,table)))return;
    }
    const double value=inverse_rational(base,square,reduced);
    const double offset=coefficient(base,table+16+index*8);
    if(!selector) {
        const double result=single?value+offset:offset+value;
        ctx.f1.f64=input>=0?result:-result;
    } else {
        const double alternate=coefficient(base,table+32+index*8)+value;
        ctx.f1.f64=input>=0?offset-value:alternate;
    }
}
}
NIER_DECOMP(sub_825D8E50) {
    ctx.fpscr.disableFlushMode();constexpr uint32_t table=0x8278e590;
    // fabs plus the original arithmetic quiets NaNs with a positive sign.
    // Express this explicitly: host FMA/multiply contraction may choose a
    // different NaN sign even though finite results remain identical.
    const uint64_t magnitude_bits=ctx.f1.u64&0x7fffffffffffffffull;
    if(magnitude_bits>0x7ff0000000000000ull) {ctx.f1.u64=magnitude_bits|0x0008000000000000ull;return;}
    const double input=ctx.f1.f64,magnitude=std::fabs(input);
    if(magnitude==0)return;
    const double multiple=double(integral(coefficient(base,table+8)*magnitude));
    const double first=-std::fma(coefficient(base,table+40),multiple,-magnitude);
    const double remainder=-std::fma(coefficient(base,table+48),multiple,-first);
    double value=sine_polynomial(base,remainder);
    if(uint64_t(integral(multiple,true))&1)value=-value;
    value*=double(ldf(base,table+(input>=0?28:32)));
    ctx.f1.f64=magnitude-coefficient(base,table+16)>=0?coefficient(base,0x8278e8c8):value;
}
NIER_VERIFY_SPEC(sub_825D8E50,0x825D8E50,"f1=f32");

NIER_DECOMP(sub_825D8F30) {
    ctx.fpscr.disableFlushMode();constexpr uint32_t table=0x8278e590;
    const uint64_t magnitude_bits=ctx.f1.u64&0x7fffffffffffffffull;
    if(magnitude_bits>0x7ff0000000000000ull) {ctx.f1.u64=magnitude_bits|0x0008000000000000ull;return;}
    const double magnitude=std::fabs(ctx.f1.f64),shifted=coefficient(base,table)+magnitude;
    const double multiple=double(integral(coefficient(base,table+8)*shifted));
    const double adjusted=multiple-double(ldf(base,table+36));
    const double first=-std::fma(coefficient(base,table+40),adjusted,-magnitude);
    const double remainder=-std::fma(coefficient(base,table+48),adjusted,-first);
    double value=sine_polynomial(base,remainder);
    if(uint64_t(integral(multiple,true))&1)value=-value;
    if(magnitude==double(ldf(base,table+24)))ctx.f1.f64=ldf(base,table+28);
    else ctx.f1.f64=shifted-coefficient(base,table+16)>=0?coefficient(base,0x8278e8c8):value;
}
NIER_VERIFY_SPEC(sub_825D8F30,0x825D8F30,"f1=f32");

NIER_DECOMP(sub_825D9010) {inverse(ctx,base,ctx.r4.u32,false);}
NIER_VERIFY_SPEC(sub_825D9010,0x825D9010,"f1=f:1..1 r4=range:0..1");
NIER_DECOMP(sub_825D9108) {inverse(ctx,base,0,true);}
NIER_VERIFY_SPEC(sub_825D9108,0x825D9108,"f1=f:1..1");
