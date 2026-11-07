#include <nier/decomp.h>
#include <atomic>
using namespace nier::guest;

// Interlocked exchange of a guest big-endian 32-bit word. A single native
// atomic exchange replaces the reservation/retry loop, retaining the SDK's
// interrupt/global-lock protocol and the original zero-extended return value.
NIER_DECOMP(sub_825EB868) {
    std::atomic_thread_fence(std::memory_order_seq_cst);
    const uint64_t interrupt_state=REX_CHECK_GLOBAL_LOCK();
    std::atomic_thread_fence(std::memory_order_seq_cst);
    ctx.msr=(ctx.r13.u32&0x8020)|(ctx.msr&~0x8020);
    REX_ENTER_GLOBAL_LOCK();
    auto& word=*reinterpret_cast<uint32_t*>(base+ctx.r3.u32);
    const uint32_t previous=std::atomic_ref<uint32_t>(word).exchange(__builtin_bswap32(ctx.r4.u32),std::memory_order_seq_cst);
    ctx.reserved.u32=previous;
    ctx.cr0.lt=false;ctx.cr0.gt=false;ctx.cr0.eq=true;ctx.cr0.so=ctx.xer.so;
    std::atomic_thread_fence(std::memory_order_seq_cst);
    ctx.msr=(interrupt_state&0x8020)|(ctx.msr&~0x8020);
    REX_LEAVE_GLOBAL_LOCK();
    ctx.r3.u64=__builtin_bswap32(previous);
}
NIER_VERIFY_SPEC(sub_825EB868,0x825EB868,"r3=ptr:4 r4=int");
