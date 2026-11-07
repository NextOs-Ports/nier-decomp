// PCM reader helpers in the audio bridge adjacent to the XAudio callbacks.
// The exact middleware symbol names are not available; formats are identified
// from byte assembly, sign extension and the original normalization constants.
#include <nier/decomp.h>
using namespace nier::guest;
namespace {
void normalized(PPCContext& ctx,int32_t sample,float scale) {
    ctx.fpscr.disableFlushMode();
    const float converted=static_cast<float>(sample);
    ctx.f1.f64=static_cast<float>(converted*scale);
}
uint32_t packed24(const uint8_t* base,uint32_t address) {
    return uint32_t(ld8(base,address)) | uint32_t(ld8(base,address+1))<<8 | uint32_t(ld8(base,address+2))<<16;
}
}
// Signed 16-bit big-endian PCM.
NIER_DECOMP(sub_824BB5D0) {normalized(ctx,int16_t(ld16(base,ctx.r3.u32)),0x1p-15f);}
NIER_VERIFY_SPEC(sub_824BB5D0,0x824BB5D0,"r3=ptr:2");
// 20 meaningful bits in a packed little-endian 24-bit word. The aligned word
// remains in r3 in the original helper, in addition to the floating f1 result.
NIER_DECOMP(sub_824BB5F8) {
    const uint32_t aligned=packed24(base,ctx.r3.u32)<<8;
    ctx.r3.u64=aligned;normalized(ctx,int32_t(aligned)>>12,0x1p-19f);
}
NIER_VERIFY_SPEC(sub_824BB5F8,0x824BB5F8,"r3=bptr");
NIER_DECOMP(sub_824BB640) {normalized(ctx,int32_t(ld32(base,ctx.r3.u32))>>12,0x1p-19f);}
NIER_VERIFY_SPEC(sub_824BB640,0x824BB640,"r3=ptr:4");
NIER_DECOMP(sub_824BB670) {normalized(ctx,int32_t(ld32(base,ctx.r3.u32))>>8,0x1p-23f);}
NIER_VERIFY_SPEC(sub_824BB670,0x824BB670,"r3=ptr:4");
// Unsigned 8-bit PCM has its midpoint at 128.
NIER_DECOMP(sub_824BE428) {
    ctx.fpscr.disableFlushMode();
    const float centered=static_cast<float>(ld8(base,ctx.r3.u32))-128.0f;
    ctx.f1.f64=static_cast<float>(centered*0x1p-7f);
}
NIER_VERIFY_SPEC(sub_824BE428,0x824BE428,"r3=bptr");
NIER_DECOMP(sub_824BE458) {
    const uint32_t aligned=packed24(base,ctx.r3.u32)<<8;
    ctx.r3.u64=aligned;normalized(ctx,int32_t(aligned)>>8,0x1p-23f);
}
NIER_VERIFY_SPEC(sub_824BE458,0x824BE458,"r3=bptr");
NIER_DECOMP(sub_824BE4A0) {normalized(ctx,int32_t(ld32(base,ctx.r3.u32)),0x1p-31f);}
NIER_VERIFY_SPEC(sub_824BE4A0,0x824BE4A0,"r3=ptr:4");
