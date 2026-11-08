// Distância para ordenação e fator de visibilidade das partículas de um emissor.
#include "particle_math.h"
#pragma clang fp contract(off)
using namespace nier::guest;
using namespace nier::particle;
DECLARE_REX_FUNC(sub_82101328);
DECLARE_REX_FUNC(sub_82156960);

NIER_DECOMP(sub_821569E8) {
    const uint64_t emitter_arg = ctx.r3.u64;
    const uint32_t emitter = ctx.r3.u32;
    const uint32_t definition = ld32(base, emitter + 36);
    if (int32_t(ld32(base, definition + 312)) <= 0 && !ld32(base, definition + 404)) return;
    GuestFrame frame(ctx, base, 288);
    const uint32_t behavior = ld32(base, emitter + 40);
    const double one = scalar(ctx, base, 0x820007A4);
    const double direction = ld32(base, behavior + 4) ? one : scalar(ctx, base, 0x820007C0);
    const Vector camera = load(base, 0x84694DD0);
    const bool world_space = ld32(base, behavior + 12) != 0;
    uint32_t node = ld32(base, emitter + 51552);
    while (node) {
        const uint32_t current = node;
        node = ld32(base, node + 144);
        Vector position;
        if (world_space) position = load(base, current + 48);
        else {
            ctx.r3.u64 = frame.slot(112); ctx.r4.u64 = emitter_arg + 160; ctx.r5.u64 = current;
            call_guest(ctx, base, sub_82101328);
            position = load(base, frame.slot(160));
        }
        ctx.fpscr.enableFlushMode();
        const Vector displacement = zero_w(simde_mm_sub_ps(position, camera));
        const Vector squared = simde_mm_dp_ps(displacement, displacement, 0xEF);
        const double distance = mul(ctx, component<0>(ctx, squared), direction);
        store(base, current + 80, replace<3>(ctx, load(base, current + 80), distance));
    }
    const uint32_t current_definition = ld32(base, emitter + 36);
    uint32_t limit = ld32(base, current_definition + 312);
    if (int32_t(limit) > 0) {
        const uint32_t count = ld32(base, emitter + 51568);
        if (int32_t(limit) > int32_t(count) / 2) limit = count;
        ctx.r3.u64 = emitter_arg + 352; ctx.r4.u64 = limit;
        call_guest(ctx, base, sub_82156960);
    }
    const uint32_t fade_definition = ld32(base, emitter + 36);
    node = ld32(base, emitter + 51552);
    if (ld32(base, fade_definition + 404)) {
        const double near_distance = scalar(ctx, base, fade_definition + 316);
        const double near_squared = mul(ctx, near_distance, near_distance);
        const double far_distance = scalar(ctx, base, fade_definition + 320);
        ctx.fpscr.disableFlushMode();
        const double span = float(std::fma(far_distance, far_distance, -near_squared));
        const double inverse_span = float(one / span);
        const double zero = scalar(ctx, base, 0x820454E0);
        while (node) {
            const uint32_t current = node;
            node = ld32(base, node + 144);
            const Vector value = load(base, current + 80);
            const double distance = component<3>(ctx, value);
            ctx.fpscr.disableFlushMode();
            const double difference = float(std::fma(distance, direction, -near_squared));
            double fade = mul(ctx, difference, inverse_span);
            if (fade < zero) fade = zero;
            else if (fade > one) fade = one;
            store(base, current + 80, replace<3>(ctx, value, fade));
        }
    } else {
        const double visible = scalar(ctx, base, 0x820007BC);
        while (node) {
            const uint32_t current = node;
            node = ld32(base, node + 144);
            store(base, current + 80, replace<3>(ctx, load(base, current + 80), visible));
        }
    }
}
NIER_VERIFY_SPEC(sub_821569E8, 0x821569E8,
    "r3=const:0x40000000 @r3.36=const:0x40010000 @r3.40=const:0x40020000 "
    "@r3.0xc960=const:0x40030000 @r3.0xc970=const:2 "
    "@r3.0x30090=const:0x40030100 @r3.0x30190=const:0 "
    "@r3.0x10138=const:0 @r3.0x10194=const:1 "
    "@r3.0x20004=range:0..1 @r3.0x2000c=range:0..1");
