// XDK helpers identified from KPCR/KTHREAD layouts and their memory contracts.
#include <nier/decomp.h>
using namespace nier::guest;
namespace {
void set_last_error(PPCContext& ctx,uint8_t* base) {
    if(ld32(base,ctx.r13.u32+0x150)==0)
        st32(base,ld32(base,ctx.r13.u32+0x100)+0x160,ctx.r3.u32);
}
void get_last_error(PPCContext& ctx,uint8_t* base) {
    ctx.r3.u64=ld32(base,ctx.r13.u32+0x150) ? 0 : ld32(base,ld32(base,ctx.r13.u32+0x100)+0x160);
}
}

// KPCR.PRCB.dpc_active guards KTHREAD.last_error; writes are suppressed in DPCs.
NIER_DECOMP(sub_82561278) {set_last_error(ctx,base);}
NIER_VERIFY_SPEC(sub_82561278,0x82561278,"r3=int @r13.0x100=ptr @r13.0x150=range:0..1");
NIER_DECOMP(sub_825612C8) {get_last_error(ctx,base);}
NIER_VERIFY_SPEC(sub_825612C8,0x825612C8,"@r13.0x100=ptr @r13.0x150=range:0..1");
// Public tail-entry facades preserve the same contracts.
NIER_DECOMP(sub_825617E0) {set_last_error(ctx,base);}
NIER_VERIFY_SPEC(sub_825617E0,0x825617E0,"r3=int @r13.0x100=ptr @r13.0x150=range:0..1");
NIER_DECOMP(sub_8255D378) {get_last_error(ctx,base);}
NIER_VERIFY_SPEC(sub_8255D378,0x8255D378,"@r13.0x100=ptr @r13.0x150=range:0..1");

// Milliseconds -> negative 100 ns NT interval. UINT32_MAX means infinite and
// returns a null interval pointer; finite values retain the caller's pointer.
NIER_DECOMP(sub_82563038) {
    if(ctx.r4.u32==0xffffffffu) {ctx.r3.u64=0;return;}
    st64(base,ctx.r3.u32,uint64_t(-10000*int64_t(ctx.r4.u32)));
}
NIER_VERIFY_SPEC(sub_82563038,0x82563038,"r3=ptr:8 r4=range:0xffffff00..0xffffffff");

// Scalar byte strlen used by the XDK layer; result is explicitly zero-extended.
NIER_DECOMP(sub_82561250) {
    uint32_t count=0;while(ld8(base,ctx.r3.u32+count))++count;ctx.r3.u64=count;
}
NIER_VERIFY_SPEC(sub_82561250,0x82561250,"r3=bptr");

// Exception-code filter: EXCEPTION_POINTERS -> record -> code, excluding
// STATUS_NO_MEMORY (0xC0000017). The original routine's PDB name is unavailable.
NIER_DECOMP(sub_8256072C) {
    ctx.r3.u64=ld32(base,ld32(base,ctx.r3.u32))!=0xc0000017u;
}
NIER_VERIFY_SPEC(sub_8256072C,0x8256072C,"r3=ptr @r3.0.0=range:0xc0000010..0xc0000020");
