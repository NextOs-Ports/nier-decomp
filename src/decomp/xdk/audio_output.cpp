// Final float-to-PCM output conversion: clamp, scale, truncate, pack. The
// original unrolled loops and scalar tails share these numerical contracts.
#include <nier/decomp.h>
#include <bit>
#include <cmath>
using namespace nier::guest;
namespace {
enum class Format { F32,U8,I16,LE20,LE24,BE20,BE24,I32 };
double number(const uint8_t* base,uint32_t address) {return std::bit_cast<double>(ld64(base,address));}
template<Format F> void convert(PPCContext& ctx,uint8_t* base) {
    ctx.fpscr.disableFlushMode();
    const uint32_t destination=ctx.r3.u32,source=ctx.r4.u32,count=ctx.r5.u32;
    const double lower=ldf(base,0x820007c0);
    double upper,scale=1,replacement;
    if constexpr(F==Format::F32)upper=ldf(base,0x820007a4);
    else if constexpr(F==Format::U8) {upper=ldf(base,0x8206a6a0);scale=ldf(base,0x8202a168);}
    else if constexpr(F==Format::I16) {upper=ldf(base,0x8206a698);scale=ldf(base,0x8206a69c);}
    else if constexpr(F==Format::LE20 || F==Format::BE20) {upper=ldf(base,0x8206a6a4);scale=ldf(base,0x8206a6a8);}
    else if constexpr(F==Format::LE24 || F==Format::BE24) {upper=ldf(base,0x8206a6ac);scale=ldf(base,0x8206a6b0);}
    else {upper=number(base,0x8206a6b8);scale=number(base,0x8206a6c0);}
    replacement=upper;
    if constexpr(F==Format::I32)replacement=ldf(base,0x8206a6ac);
    for(uint32_t i=0;i<count;++i) {
        const uint32_t input_bits=ld32(base,source+i*4);
        double sample=double(std::bit_cast<float>(input_bits));
        if(sample<lower)sample=lower;else if(sample>upper)sample=replacement;
        if constexpr(F==Format::F32) {
            // lfs/stfs quiets an input signaling NaN without losing payload.
            if(std::isnan(sample))st32(base,destination+i*4,input_bits|0x00400000u);
            else stf(base,destination+i*4,float(sample));
        } else {
            double scaled;
            if constexpr(F==Format::U8)scaled=float(double(float(sample+double(ldf(base,0x820007a4))))*scale);
            else if constexpr(F==Format::I32)scaled=sample*scale;
            else scaled=float(sample*scale);
            uint32_t value=fctiwz(scaled);
            if constexpr(F==Format::U8)st8(base,destination+i,uint8_t(value));
            else if constexpr(F==Format::I16)st16(base,destination+i*2,uint16_t(value));
            else if constexpr(F==Format::LE20 || F==Format::LE24) {
                if constexpr(F==Format::LE20)value<<=4;
                const uint32_t p=destination+i*3;
                st8(base,p,uint8_t(value));st8(base,p+1,uint8_t(value>>8));st8(base,p+2,uint8_t(value>>16));
            } else {
                if constexpr(F==Format::BE20)value<<=12;
                if constexpr(F==Format::BE24)value<<=8;
                st32(base,destination+i*4,value);
            }
        }
    }
}
}
NIER_DECOMP(sub_824BECB0) {convert<Format::F32>(ctx,base);}
NIER_DECOMP(sub_824BEDD8) {convert<Format::I16>(ctx,base);}
NIER_DECOMP(sub_824BEF58) {convert<Format::U8>(ctx,base);}
NIER_DECOMP(sub_824BF0E8) {convert<Format::LE20>(ctx,base);}
NIER_DECOMP(sub_824BF2D0) {convert<Format::LE24>(ctx,base);}
NIER_DECOMP(sub_824BF4A0) {convert<Format::BE20>(ctx,base);}
NIER_DECOMP(sub_824BF630) {convert<Format::BE24>(ctx,base);}
NIER_DECOMP(sub_824BF7C0) {convert<Format::I32>(ctx,base);}
#define OUTPUT_SPEC(name) NIER_VERIFY_SPEC(sub_##name,0x##name,"r3=const:0x40010000 r4=const:0x40030000 r5=range:0..64")
OUTPUT_SPEC(824BECB0);OUTPUT_SPEC(824BEDD8);OUTPUT_SPEC(824BEF58);OUTPUT_SPEC(824BF0E8);
OUTPUT_SPEC(824BF2D0);OUTPUT_SPEC(824BF4A0);OUTPUT_SPEC(824BF630);OUTPUT_SPEC(824BF7C0);
#undef OUTPUT_SPEC
