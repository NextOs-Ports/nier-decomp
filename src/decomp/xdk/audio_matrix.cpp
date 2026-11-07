// Fixed-size channel mixing kernels: float/BE16 input, float BE output.
// Preserve each single-precision rounding step and optional coefficient ramp.
#include <nier/decomp.h>
#include <array>
#include <cmath>
using namespace nier::guest;
namespace {
// Keep the multiply in binary64: an optimizer may otherwise narrow it to
// float, whose subnormal-input slow path can dominate very quiet audio.
float product(float x,float y) {volatile double value=double(x)*double(y);return float(value);}
float sum(float x,float y) {return float(double(x)+double(y));}
float multiply_add(float x,float y,float z) {return float(std::fma(double(x),double(y),double(z)));}

template<unsigned Inputs,unsigned Outputs,bool PCM16>
void mix(PPCContext& ctx,uint8_t* base) {
    ctx.fpscr.disableFlushMode();
    const uint32_t p=ctx.r3.u32,source=ld32(base,p),destination=ld32(base,p+4);
    const uint32_t matrix=ld32(base,p+8),ramp=ld32(base,p+12),count=ld32(base,p+20);
    const bool accumulate=ld32(base,p+32)!=0;
    std::array<float,Inputs*Outputs> gain{},delta{};
    const float scale=PCM16?ldf(base,0x8206a674):1;
    for(unsigned i=0;i<gain.size();++i) {
        gain[i]=ldf(base,matrix+i*4);if constexpr(PCM16)gain[i]=product(gain[i],scale);
        if(ramp) {delta[i]=ldf(base,ramp+i*4);if constexpr(PCM16)delta[i]=product(delta[i],scale);}
    }
    if constexpr(Inputs==2)ctx.f1.f64=gain.back();
    const uint32_t frames=count/Inputs+(count%Inputs!=0);
    for(uint32_t frame=0;frame<frames;++frame) {
        std::array<float,Inputs> sample{};
        for(unsigned channel=0;channel<Inputs;++channel) {
            const uint32_t index=frame*Inputs+channel;
            if constexpr(PCM16)sample[channel]=float(int16_t(ld16(base,source+index*2)));
            else sample[channel]=ldf(base,source+index*4);
        }
        for(unsigned out=0;out<Outputs;++out) {
            const uint32_t dst=destination+4*(frame*Outputs+out);
            float value;
            if constexpr(Inputs==1) {
                if(accumulate) {
                    const float old=ldf(base,dst);
                    value=multiply_add(sample[0],gain[out],old);
                    // f1 is volatile but the verifier compares its observable
                    // value: the six-channel code uses it as a temporary.
                    if constexpr(Outputs==6)if((ramp && out==5)||(!ramp && out==3))ctx.f1.f64=old;
                } else {
                    value=product(sample[0],gain[out]);
                    if constexpr(Outputs==6)if(ramp && out==4)ctx.f1.f64=value;
                }
            } else {
                value=multiply_add(sample[1],gain[out*2+1],product(sample[0],gain[out*2]));
                if(accumulate)value=sum(value,ldf(base,dst));
            }
            stf(base,dst,value);
        }
        if(ramp)for(unsigned i=0;i<gain.size();++i)gain[i]=sum(delta[i],gain[i]);
        if constexpr(Inputs==2)ctx.f1.f64=gain.back();
    }
}
}
NIER_DECOMP(sub_824B8498) {mix<1,2,false>(ctx,base);}
NIER_DECOMP(sub_824B9BF8) {mix<1,2,true>(ctx,base);}
NIER_DECOMP(sub_824B85A0) {mix<1,6,false>(ctx,base);}
NIER_DECOMP(sub_824B9D38) {mix<1,6,true>(ctx,base);}
NIER_DECOMP(sub_824B8778) {mix<2,6,false>(ctx,base);}
NIER_DECOMP(sub_824B9F68) {mix<2,6,true>(ctx,base);}
// Finite samples, matrix/ramp and destination; explicit four-mode edge probe
// also covers no ramp, overwrite, PCM extremes, zero and odd counts.
static constexpr const char* matrix_spec=
    "r3=const:0x40010000 @r3.0=const:0x40020000 @r3.4=const:0x40030000 @r3.8=const:0x40040000 "
    "@r3.12=const:0x40040100 @r3.20=range:0..16 @r3.32=range:0..1 @r3.0x10000=range:0x3c000000..0x3f000000 "
    "@r3.0x10004=range:0x3c000000..0x3f000000 @r3.0x10008=range:0x3c000000..0x3f000000 @r3.0x1000c=range:0x3c000000..0x3f000000 @r3.0x10010=range:0x3c000000..0x3f000000 "
    "@r3.0x10014=range:0x3c000000..0x3f000000 @r3.0x10018=range:0x3c000000..0x3f000000 @r3.0x1001c=range:0x3c000000..0x3f000000 @r3.0x10020=range:0x3c000000..0x3f000000 "
    "@r3.0x10024=range:0x3c000000..0x3f000000 @r3.0x10028=range:0x3c000000..0x3f000000 @r3.0x1002c=range:0x3c000000..0x3f000000 @r3.0x10030=range:0x3c000000..0x3f000000 "
    "@r3.0x10034=range:0x3c000000..0x3f000000 @r3.0x10038=range:0x3c000000..0x3f000000 @r3.0x1003c=range:0x3c000000..0x3f000000 @r3.0x20000=range:0x3c000000..0x3f000000 "
    "@r3.0x20004=range:0x3c000000..0x3f000000 @r3.0x20008=range:0x3c000000..0x3f000000 @r3.0x2000c=range:0x3c000000..0x3f000000 @r3.0x20010=range:0x3c000000..0x3f000000 "
    "@r3.0x20014=range:0x3c000000..0x3f000000 @r3.0x20018=range:0x3c000000..0x3f000000 @r3.0x2001c=range:0x3c000000..0x3f000000 @r3.0x20020=range:0x3c000000..0x3f000000 "
    "@r3.0x20024=range:0x3c000000..0x3f000000 @r3.0x20028=range:0x3c000000..0x3f000000 @r3.0x2002c=range:0x3c000000..0x3f000000 @r3.0x20030=range:0x3c000000..0x3f000000 "
    "@r3.0x20034=range:0x3c000000..0x3f000000 @r3.0x20038=range:0x3c000000..0x3f000000 @r3.0x2003c=range:0x3c000000..0x3f000000 @r3.0x20040=range:0x3c000000..0x3f000000 "
    "@r3.0x20044=range:0x3c000000..0x3f000000 @r3.0x20048=range:0x3c000000..0x3f000000 @r3.0x2004c=range:0x3c000000..0x3f000000 @r3.0x20050=range:0x3c000000..0x3f000000 "
    "@r3.0x20054=range:0x3c000000..0x3f000000 @r3.0x20058=range:0x3c000000..0x3f000000 @r3.0x2005c=range:0x3c000000..0x3f000000 @r3.0x20060=range:0x3c000000..0x3f000000 "
    "@r3.0x20064=range:0x3c000000..0x3f000000 @r3.0x20068=range:0x3c000000..0x3f000000 @r3.0x2006c=range:0x3c000000..0x3f000000 @r3.0x20070=range:0x3c000000..0x3f000000 "
    "@r3.0x20074=range:0x3c000000..0x3f000000 @r3.0x20078=range:0x3c000000..0x3f000000 @r3.0x2007c=range:0x3c000000..0x3f000000 @r3.0x20080=range:0x3c000000..0x3f000000 "
    "@r3.0x20084=range:0x3c000000..0x3f000000 @r3.0x20088=range:0x3c000000..0x3f000000 @r3.0x2008c=range:0x3c000000..0x3f000000 @r3.0x20090=range:0x3c000000..0x3f000000 "
    "@r3.0x20094=range:0x3c000000..0x3f000000 @r3.0x20098=range:0x3c000000..0x3f000000 @r3.0x2009c=range:0x3c000000..0x3f000000 @r3.0x200a0=range:0x3c000000..0x3f000000 "
    "@r3.0x200a4=range:0x3c000000..0x3f000000 @r3.0x200a8=range:0x3c000000..0x3f000000 @r3.0x200ac=range:0x3c000000..0x3f000000 @r3.0x200b0=range:0x3c000000..0x3f000000 "
    "@r3.0x200b4=range:0x3c000000..0x3f000000 @r3.0x200b8=range:0x3c000000..0x3f000000 @r3.0x200bc=range:0x3c000000..0x3f000000 @r3.0x200c0=range:0x3c000000..0x3f000000 "
    "@r3.0x200c4=range:0x3c000000..0x3f000000 @r3.0x200c8=range:0x3c000000..0x3f000000 @r3.0x200cc=range:0x3c000000..0x3f000000 @r3.0x200d0=range:0x3c000000..0x3f000000 "
    "@r3.0x200d4=range:0x3c000000..0x3f000000 @r3.0x200d8=range:0x3c000000..0x3f000000 @r3.0x200dc=range:0x3c000000..0x3f000000 @r3.0x200e0=range:0x3c000000..0x3f000000 "
    "@r3.0x200e4=range:0x3c000000..0x3f000000 @r3.0x200e8=range:0x3c000000..0x3f000000 @r3.0x200ec=range:0x3c000000..0x3f000000 @r3.0x200f0=range:0x3c000000..0x3f000000 "
    "@r3.0x200f4=range:0x3c000000..0x3f000000 @r3.0x200f8=range:0x3c000000..0x3f000000 @r3.0x200fc=range:0x3c000000..0x3f000000 @r3.0x20100=range:0x3c000000..0x3f000000 "
    "@r3.0x20104=range:0x3c000000..0x3f000000 @r3.0x20108=range:0x3c000000..0x3f000000 @r3.0x2010c=range:0x3c000000..0x3f000000 @r3.0x20110=range:0x3c000000..0x3f000000 "
    "@r3.0x20114=range:0x3c000000..0x3f000000 @r3.0x20118=range:0x3c000000..0x3f000000 @r3.0x2011c=range:0x3c000000..0x3f000000 @r3.0x20120=range:0x3c000000..0x3f000000 "
    "@r3.0x20124=range:0x3c000000..0x3f000000 @r3.0x20128=range:0x3c000000..0x3f000000 @r3.0x2012c=range:0x3c000000..0x3f000000 @r3.0x20130=range:0x3c000000..0x3f000000 "
    "@r3.0x20134=range:0x3c000000..0x3f000000 @r3.0x20138=range:0x3c000000..0x3f000000 @r3.0x2013c=range:0x3c000000..0x3f000000 @r3.0x20140=range:0x3c000000..0x3f000000 "
    "@r3.0x20144=range:0x3c000000..0x3f000000 @r3.0x20148=range:0x3c000000..0x3f000000 @r3.0x2014c=range:0x3c000000..0x3f000000 @r3.0x20150=range:0x3c000000..0x3f000000 "
    "@r3.0x20154=range:0x3c000000..0x3f000000 @r3.0x20158=range:0x3c000000..0x3f000000 @r3.0x2015c=range:0x3c000000..0x3f000000 @r3.0x20160=range:0x3c000000..0x3f000000 "
    "@r3.0x20164=range:0x3c000000..0x3f000000 @r3.0x20168=range:0x3c000000..0x3f000000 @r3.0x2016c=range:0x3c000000..0x3f000000 @r3.0x20170=range:0x3c000000..0x3f000000 "
    "@r3.0x20174=range:0x3c000000..0x3f000000 @r3.0x20178=range:0x3c000000..0x3f000000 @r3.0x2017c=range:0x3c000000..0x3f000000 @r3.0x30000=range:0x3c000000..0x3f000000 "
    "@r3.0x30004=range:0x3c000000..0x3f000000 @r3.0x30008=range:0x3c000000..0x3f000000 @r3.0x3000c=range:0x3c000000..0x3f000000 @r3.0x30010=range:0x3c000000..0x3f000000 "
    "@r3.0x30014=range:0x3c000000..0x3f000000 @r3.0x30018=range:0x3c000000..0x3f000000 @r3.0x3001c=range:0x3c000000..0x3f000000 @r3.0x30020=range:0x3c000000..0x3f000000 "
    "@r3.0x30024=range:0x3c000000..0x3f000000 @r3.0x30028=range:0x3c000000..0x3f000000 @r3.0x3002c=range:0x3c000000..0x3f000000 @r3.0x30100=range:0x3c000000..0x3f000000 "
    "@r3.0x30104=range:0x3c000000..0x3f000000 @r3.0x30108=range:0x3c000000..0x3f000000 @r3.0x3010c=range:0x3c000000..0x3f000000 @r3.0x30110=range:0x3c000000..0x3f000000 "
    "@r3.0x30114=range:0x3c000000..0x3f000000 @r3.0x30118=range:0x3c000000..0x3f000000 @r3.0x3011c=range:0x3c000000..0x3f000000 @r3.0x30120=range:0x3c000000..0x3f000000 "
    "@r3.0x30124=range:0x3c000000..0x3f000000 @r3.0x30128=range:0x3c000000..0x3f000000 @r3.0x3012c=range:0x3c000000..0x3f000000 "
    ;
NIER_VERIFY_SPEC(sub_824B8498,0x824B8498,matrix_spec);
NIER_VERIFY_SPEC(sub_824B9BF8,0x824B9BF8,matrix_spec);
NIER_VERIFY_SPEC(sub_824B85A0,0x824B85A0,matrix_spec);
NIER_VERIFY_SPEC(sub_824B9D38,0x824B9D38,matrix_spec);
NIER_VERIFY_SPEC(sub_824B8778,0x824B8778,matrix_spec);
NIER_VERIFY_SPEC(sub_824B9F68,0x824B9F68,matrix_spec);
