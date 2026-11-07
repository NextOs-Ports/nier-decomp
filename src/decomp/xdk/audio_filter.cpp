// State-variable audio filter: low/band integrators and selectable outputs.
#include <nier/decomp.h>
#include <cmath>
using namespace nier::guest;
NIER_DECOMP(sub_824BFAB0) {
    ctx.fpscr.disableFlushMode();
    const uint32_t object=ctx.r3.u32,source=ld32(base,object),destination=ld32(base,object+4);
    const uint32_t low_state=ld32(base,object+8),band_state=ld32(base,object+12);
    const uint32_t count=ld32(base,object+16),mode=ld32(base,object+24);
    const float frequency=ldf(base,object+28),damping=ldf(base,object+32);
    float low=ldf(base,low_state),band=ldf(base,band_state);
    for(uint32_t i=0;i<count;++i) {
        const float error=float(double(ldf(base,source+i*4))-double(low));
        const float high=float(-std::fma(double(damping),double(band),-double(error)));
        band=float(std::fma(double(high),double(frequency),double(band)));
        low=float(std::fma(double(band),double(frequency),double(low)));
        const float output=mode==0?low:mode==1?band:mode==2?high:float(double(low)+double(high));
        stf(base,destination+i*4,output);
    }
    stf(base,low_state,low);stf(base,band_state,band);
}
// Finite normalized samples/states, varying stable coefficients and all output modes.
NIER_VERIFY_SPEC(sub_824BFAB0,0x824BFAB0,
    "r3=const:0x40010000 @r3.0=const:0x40020000 @r3.4=const:0x40030000 @r3.8=const:0x40040000 "
    "@r3.12=const:0x40040004 @r3.16=range:0..32 @r3.24=range:0xffffffff..3 @r3.28=range:0x3d000000..0x3f000000 "
    "@r3.32=range:0x3e800000..0x3f800000 @r3.0x30000=range:0..0x3f800000 @r3.0x30004=range:0x80000000..0xbf800000 @r3.0x10000=range:0..0x3f800000 "
    "@r3.0x10004=range:0x80000000..0xbf800000 @r3.0x10008=range:0..0x3f800000 @r3.0x1000c=range:0x80000000..0xbf800000 @r3.0x10010=range:0..0x3f800000 "
    "@r3.0x10014=range:0x80000000..0xbf800000 @r3.0x10018=range:0..0x3f800000 @r3.0x1001c=range:0x80000000..0xbf800000 @r3.0x10020=range:0..0x3f800000 "
    "@r3.0x10024=range:0x80000000..0xbf800000 @r3.0x10028=range:0..0x3f800000 @r3.0x1002c=range:0x80000000..0xbf800000 @r3.0x10030=range:0..0x3f800000 "
    "@r3.0x10034=range:0x80000000..0xbf800000 @r3.0x10038=range:0..0x3f800000 @r3.0x1003c=range:0x80000000..0xbf800000 @r3.0x10040=range:0..0x3f800000 "
    "@r3.0x10044=range:0x80000000..0xbf800000 @r3.0x10048=range:0..0x3f800000 @r3.0x1004c=range:0x80000000..0xbf800000 @r3.0x10050=range:0..0x3f800000 "
    "@r3.0x10054=range:0x80000000..0xbf800000 @r3.0x10058=range:0..0x3f800000 @r3.0x1005c=range:0x80000000..0xbf800000 @r3.0x10060=range:0..0x3f800000 "
    "@r3.0x10064=range:0x80000000..0xbf800000 @r3.0x10068=range:0..0x3f800000 @r3.0x1006c=range:0x80000000..0xbf800000 @r3.0x10070=range:0..0x3f800000 "
    "@r3.0x10074=range:0x80000000..0xbf800000 @r3.0x10078=range:0..0x3f800000 @r3.0x1007c=range:0x80000000..0xbf800000 "
);
