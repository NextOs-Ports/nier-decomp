// Reutilização de blocos de shader por versão e máscara do estado de textura.
#include <nier/decomp.h>
#include <atomic>
using namespace nier::guest;
DECLARE_REX_FUNC(sub_82578568);

namespace {
uint32_t next_version(PPCContext& ctx, uint8_t* base) {
    auto& word = *reinterpret_cast<uint32_t*>(base + 0x85F4C81C);
    uint32_t next;
    do {
        bool exchanged;
        do {
            std::atomic_thread_fence(std::memory_order_seq_cst);
            const uint64_t interrupts = REX_CHECK_GLOBAL_LOCK();
            std::atomic_thread_fence(std::memory_order_seq_cst);
            ctx.msr = (ctx.r13.u32 & 0x8020) | (ctx.msr & ~0x8020);
            REX_ENTER_GLOBAL_LOCK();
            const uint32_t previous = std::atomic_ref<uint32_t>(word).load(std::memory_order_relaxed);
            next = __builtin_bswap32(previous) + 1;
            uint32_t expected = previous;
            exchanged = std::atomic_ref<uint32_t>(word).compare_exchange_strong(
                expected, __builtin_bswap32(next), std::memory_order_seq_cst);
            ctx.reserved.u32 = previous;
            ctx.cr0.lt = false; ctx.cr0.gt = false;
            ctx.cr0.eq = exchanged; ctx.cr0.so = ctx.xer.so;
            std::atomic_thread_fence(std::memory_order_seq_cst);
            ctx.msr = (interrupts & 0x8020) | (ctx.msr & ~0x8020);
            REX_LEAVE_GLOBAL_LOCK();
        } while (!exchanged);
    } while (next + 1u < 2u); // zero e -1 são sentinelas, inclusive no wrap.
    return next;
}
}

NIER_DECOMP(sub_82579100) {
    const uint64_t device_arg = ctx.r3.u64, shader_arg = ctx.r4.u64;
    const uint64_t layout_arg = ctx.r5.u64, stage_arg = ctx.r6.u64;
    const uint32_t device = ctx.r3.u32, shader = ctx.r4.u32, layout = ctx.r5.u32;
    const uint32_t stage = ctx.r6.u32, record = shader + stage * 416;
    GuestFrame frame(ctx, base, 128);
    bool reusable = ld32(base, record + 40) == ld32(base, layout + 48);
    if (reusable) {
        const uint64_t changed0 = ld64(base, record + 48) ^ ld64(base, device + 12704);
        const uint64_t changed1 = ld64(base, record + 56) ^ ld64(base, device + 12712);
        reusable = !((changed0 & ld64(base, layout + 32)) | (changed1 & ld64(base, layout + 40)));
    }
    if (!reusable) {
        const uint32_t last_use = ld32(base, record + 64);
        if (last_use) {
            const uint32_t retired_ptr = ld32(base, device + 10896);
            const uint32_t current = ld32(base, device + 10908);
            const uint32_t retired = ld32(base, retired_ptr);
            if (current - last_use < current - retired) {
                ctx.r3.u64 = 0;
                return;
            }
        }
        const uint32_t offset = ld32(base, shader + (stage + 112u) * 8u);
        const uint32_t pool = ld32(base, shader + 32);
        const uint32_t block = ld32(base, shader + offset + 872);
        ctx.r3.u64 = shader_arg;
        ctx.r4.u64 = uint64_t(block) + pool;
        ctx.r5.u64 = layout_arg;
        ctx.r6.u64 = device_arg + 12704;
        ctx.r7.u64 = stage_arg;
        call_guest(ctx, base, sub_82578568);
        if (!ld32(base, layout + 48)) st32(base, layout + 48, next_version(ctx, base));
        st32(base, record + 40, ld32(base, layout + 48));
        st64(base, record + 48, ld64(base, device + 12704));
        st64(base, record + 56, ld64(base, device + 12712));
    }
    const uint64_t state0 = ld64(base, device + 12704), state1 = ld64(base, device + 12712);
    st64(base, device + 12008, state0);
    st64(base, device + 12016, state1);
    ctx.r3.u64 = 1;
}
NIER_VERIFY_SPEC(sub_82579100, 0x82579100,
    "r3=const:0x40010000 r4=const:0x40020000 r5=const:0x40030000 r6=const:0 "
    "@r4.40=range:0..1 @r5.48=range:0..1 @r4.64=range:0..3 "
    "@r3.10896=const:0x40040000 @r3.10908=range:0..5 @r3.0x30000=range:0..3 "
    "@r4.896=const:0 @r4.900=const:0");
