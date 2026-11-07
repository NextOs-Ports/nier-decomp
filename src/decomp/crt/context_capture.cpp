// Save the linked CRT's jump-buffer layout, including its VMX128 registers.
#include <nier/decomp.h>
#include <array>
#include <cstddef>
using namespace nier::guest;
static_assert(offsetof(PPCContext,r31)-offsetof(PPCContext,r13)==18*sizeof(PPCRegister));
static_assert(offsetof(PPCContext,f31)-offsetof(PPCContext,f14)==17*sizeof(PPCRegister));
static_assert(offsetof(PPCContext,v127)-offsetof(PPCContext,v64)==63*sizeof(PPCVRegister));
static_assert(offsetof(PPCContext,cr7)-offsetof(PPCContext,cr0)==7*sizeof(PPCCRRegister));
NIER_DECOMP(sub_825D5DE0) {
    ctx.r4.s64=int32_t(0x86020000);
    const uint32_t hook=ld32(base,0x86018f04);ctx.r0.u64=hook;
    ctx.cr0.compare<int32_t>(int32_t(hook),0,ctx.xer);ctx.ctr.u64=hook;
    if(hook) {call_indirect(ctx,base,hook);return;}
    ctx.fpscr.disableFlushMode();const uint32_t p=ctx.r3.u32;
    std::array<PPCRegister,18> floats{};std::array<PPCRegister,19> integers{};
    std::array<PPCVRegister,64> vectors{};std::array<PPCCRRegister,8> conditions{};
    // The SDK also uses contiguous memcpy for these register groups; the
    // assertions above make this layout dependency explicit.
    std::memcpy(floats.data(),&ctx.f14,sizeof floats);std::memcpy(integers.data(),&ctx.r13,sizeof integers);
    std::memcpy(vectors.data(),&ctx.v64,sizeof vectors);std::memcpy(conditions.data(),&ctx.cr0,sizeof conditions);
    for(unsigned i=0;i<floats.size();++i)st64(base,p+i*8,floats[i].u64);
    st64(base,p+144,ctx.r1.u64);
    for(unsigned i=0;i<integers.size();++i)st64(base,p+152+i*8,integers[i].u64);
    uint32_t cr=0;
    for(const auto& field:conditions)cr=(cr<<4)|(bool(field.lt)<<3)|(bool(field.gt)<<2)|(bool(field.eq)<<1)|bool(field.so);
    st32(base,p+304,cr);st32(base,p+308,uint32_t(ctx.lr));st32(base,p+312,0);
    const unsigned stored=16-(p&15);
    for(unsigned i=0;i<vectors.size();++i)for(unsigned byte=0;byte<stored;++byte)
        st8(base,p+320+i*16+byte,vectors[i].u8[15-byte]);
    ctx.r3.u64=0;
}
NIER_VERIFY_SPEC(sub_825D5DE0,0x825D5DE0,"r3=const:0x40010000");
