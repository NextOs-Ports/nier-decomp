// Atualização do descritor de textura do XDK. Palavras e máscaras nativas;
// conversão big-endian somente nas bordas da memória do guest.
#include <nier/decomp.h>
#include <algorithm>
using namespace nier::guest;
DECLARE_REX_FUNC(sub_82574DB8);

NIER_DECOMP(sub_8256F910) {
    const uint64_t device_arg = ctx.r3.u64;
    const uint32_t device = ctx.r3.u32, stage = ctx.r4.u32, texture = ctx.r5.u32;
    const uint64_t dirty = ctx.r6.u64;
    const uint32_t slot = device + (stage + 3180u) * 4u;
    const uint32_t descriptor = device + (stage + 48u) * 24u;
    const uint32_t previous = ld32(base, slot);
    GuestFrame frame(ctx, base, 192);

    if (texture) {
        const uint32_t t5 = ld32(base, texture + 48);
        const uint32_t t1 = ld32(base, texture + 32);
        const uint32_t t4 = ld32(base, texture + 44);
        const uint32_t t0 = ld32(base, texture + 28);
        const uint32_t t2 = ld32(base, texture + 36);
        const uint32_t old0 = ld32(base, descriptor);
        const uint32_t old3 = ld32(base, descriptor + 12);
        const uint32_t old5 = ld32(base, descriptor + 20);
        const uint32_t t3 = ld32(base, texture + 40);
        const uint32_t old4 = ld32(base, descriptor + 16);
        const uint32_t old1 = ld32(base, descriptor + 4);
        st32(base, descriptor + 8, t2);

        const uint32_t min_lod = ld8(base, device + stage + 12126);
        const uint32_t page1 = ((t1 >> 20) + 512u) & 0x1000u;
        const uint32_t page5 = ((t5 >> 20) + 512u) & 0x1000u;
        uint32_t word4 = (t4 & 0x3FCu) | (old4 & 0xFFFFFC03u);
        const uint32_t word1 = (((t1 & 0x1FFFFFFFu) + page1) & ~0x800u) | (old1 & 0x800u);
        st32(base, descriptor + 16, word4);
        st32(base, descriptor, (t0 & ~0x003FFC00u) | (old0 & 0x003FFC00u));
        st32(base, descriptor + 4, word1);
        st32(base, descriptor + 12, (t3 & ~0x7FF80000u) | (old3 & 0x7FF80000u));
        st32(base, descriptor + 20, ((t5 & 0x1FFFFE00u) + page5) | (old5 & 0x1FFu));
        ctx.r3.u64 = word1;
        ctx.r4.u64 = (t4 >> 2) & 15u;

        const uint32_t max_lod_texture = (ld32(base, texture + 44) >> 6) & 15u;
        const uint32_t max_lod = ld8(base, device + stage + 12152);
        word4 = (word4 & ~0x3Cu) | ((std::max((t4 >> 2) & 15u, min_lod) << 2) & 0x3Cu);
        st32(base, descriptor + 16, word4);
        const uint64_t prior_dirty = ld64(base, device + 24);
        word4 = (word4 & ~0x3C0u) | ((std::min(max_lod_texture, max_lod) << 6) & 0x3C0u);
        st32(base, descriptor + 16, word4);
        st64(base, device + 24, prior_dirty | dirty);
    } else {
        st32(base, descriptor, ld32(base, descriptor) & ~3u);
    }
    st32(base, slot, texture);
    if (!previous) return;
    const uint32_t token = ld32(base, device + 10908);
    if (token) {
        st32(base, previous + 8, token);
        return;
    }
    if (!(ld32(base, device + 10912) & ld32(base, previous))) return;
    uint32_t cursor = ld32(base, device + 13732);
    const uint32_t end = ld32(base, device + 13736);
    ctx.r3.u64 = cursor;
    if (cursor >= end) {
        ctx.r3.u64 = device_arg;
        call_guest(ctx, base, sub_82574DB8);
        cursor = ctx.r3.u32;
    }
    const uint32_t command = (ld32(base, frame.slot(80)) & 0x80000000u) | (previous >> 2);
    st32(base, frame.slot(80), command);
    st32(base, frame.slot(84), 0xFFFFFFFFu);
    st64(base, cursor, (uint64_t(command) << 32) | 0xFFFFFFFFu);
    st32(base, device + 13732, cursor + 8);
}
NIER_VERIFY_SPEC(sub_8256F910, 0x8256F910,
    "r3=const:0x40010000 r4=range:0..15 r5=ptr r6=int");
