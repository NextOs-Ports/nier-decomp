// CRT's packed extended96 -> IEEE storage helpers. The format is a BE sign/
// exponent word followed by 80 significand bits (including the explicit bit).
#include <nier/decomp.h>
using namespace nier::guest;
namespace {
using Wide=unsigned __int128;
constexpr Wide mask96=(Wide(1)<<96)-1;
// The original increments only for guard=1 AND a nonzero lower remainder;
// exact ties are truncated. Keep the guard bit until final packing.
bool round_significand(Wide& significand,unsigned precision) {
    const unsigned guard=95-precision;
    const Wide lower=(Wide(1)<<guard)-1;
    if((significand&(Wide(1)<<guard)) && (significand&lower))
        significand+=Wide(1)<<(guard+1);
    const bool carry=(significand>>96)!=0;
    significand&=mask96&~lower;
    return carry;
}

void convert(PPCContext& ctx,uint8_t* base,uint32_t config) {
    const uint32_t input=ctx.r3.u32,output=ctx.r4.u32;
    // Retain the original live configuration tables (binary64 and binary32).
    const int maximum=int32_t(ld32(base,config)),minimum=int32_t(ld32(base,config+4));
    const unsigned precision=ld32(base,config+8),exponent_bits=ld32(base,config+12),width=ld32(base,config+16);
    const int bias=int32_t(ld32(base,config+20));
    const uint16_t header=ld16(base,input);
    const Wide original=(Wide(ld64(base,input+2))<<32)|(Wide(ld16(base,input+10))<<16);
    Wide significand=original;
    const int initial_exponent=int(header&0x7fff)-16383;
    int exponent=initial_exponent,encoded=0;uint32_t status=0;
    if((header&0x7fff)==0) {
        if(significand)status=2;
        significand=0;
    } else {
        if(round_significand(significand,precision))++exponent;
        if(exponent<minimum-int(precision)) {significand=0;status=2;}
        else if(exponent<=minimum) {
            significand=original>>(minimum-initial_exponent);
            round_significand(significand,precision);
            significand>>=exponent_bits+1;status=2;
        } else if(exponent>=maximum) {
            significand=(Wide(1)<<95)>>exponent_bits;
            encoded=maximum+bias;status=1;
        } else {
            significand&=~(Wide(1)<<95);
            significand>>=exponent_bits;
            encoded=exponent+bias;
        }
    }
    const uint32_t top=uint32_t(significand>>64)|(uint32_t(encoded)<<(31-exponent_bits))|((header&0x8000)?0x80000000u:0);
    if(width==64)st32(base,output+4,uint32_t(significand>>32));
    if(width==32 || width==64)st32(base,output,top);
    ctx.r3.u64=status;
}
}
NIER_DECOMP(sub_825E7B88) {convert(ctx,base,0x8278f0c4);}
NIER_VERIFY_SPEC(sub_825E7B88,0x825E7B88,"r3=const:0x40010000 r4=const:0x40050000");
NIER_DECOMP(sub_825E82A0) {convert(ctx,base,0x8278f0dc);}
NIER_VERIFY_SPEC(sub_825E82A0,0x825E82A0,"r3=const:0x40010000 r4=const:0x40050000");
