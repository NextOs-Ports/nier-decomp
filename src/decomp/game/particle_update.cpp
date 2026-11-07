// Passo de uma partícula: rotação, forças, integração, eventos e resposta de colisão.
#include "particle_math.h"
#pragma clang fp contract(off)
using namespace nier::guest;
using namespace nier::particle;
DECLARE_REX_FUNC(sub_82214578);
DECLARE_REX_FUNC(sub_8217D098);
DECLARE_REX_FUNC(sub_82148408);
DECLARE_REX_FUNC(sub_82101328);
DECLARE_REX_FUNC(sub_821419A0);
DECLARE_REX_FUNC(sub_82170FF0);
DECLARE_REX_FUNC(sub_82170CA8);
DECLARE_REX_FUNC(sub_82149B88);
DECLARE_REX_FUNC(sub_8217C450);
DECLARE_REX_FUNC(sub_82101A08);
DECLARE_REX_FUNC(sub_82100F88);
DECLARE_REX_FUNC(sub_82146F60);
DECLARE_REX_FUNC(sub_82141E48);

namespace {
constexpr uint32_t manager = 0x84694D00, seed_address = 0x826908A0;
uint32_t random15(uint8_t* base) {
    const uint32_t seed = ld32(base, seed_address) * 214013u + 2531011u;
    st32(base, seed_address, seed);
    return (seed >> 16) & 0x7FFFu;
}
void copy_matrix(uint8_t* base, uint32_t destination, uint32_t source) {
    Vector rows[4];
    for (int i = 0; i < 4; ++i) rows[i] = load(base, source + i * 16);
    for (int i = 0; i < 4; ++i) store(base, destination + i * 16, rows[i]);
}
}

NIER_DECOMP(sub_82144588) {
    const uint64_t emitter_arg = ctx.r3.u64, particle_arg = ctx.r4.u64;
    const uint32_t emitter = ctx.r3.u32, particle = ctx.r4.u32;
    const double dt = ctx.f1.f64;
    GuestFrame frame(ctx, base, 576);
    auto scalar_at = [&](uint32_t address) { return scalar(ctx, base, address); };
    Vector position = load(base, particle + 48);
    store(base, frame.slot(96), position);
    const double phase = mul(ctx, scalar_at(particle + 104), scalar_at(particle + 96));
    const uint32_t sample = fctiwz(phase);
    const double fraction = sub(ctx, phase, static_cast<float>(static_cast<int32_t>(sample)));
    auto curve = [&](int32_t index) {
        ctx.f1.f64 = fraction;
        ctx.r4.u64 = sample;
        ctx.r3.u64 = uint64_t(ld32(base, emitter + 48)) + uint64_t(uint32_t(index)) * 132;
        call_guest(ctx, base, sub_82214578);
        return ctx.f1.f64;
    };
    auto matrix_to = [&](uint32_t destination, bool world_space) {
        if (world_space) copy_matrix(base, destination, particle);
        else {
            ctx.r3.u64 = frame.slot(112); ctx.r4.u64 = emitter_arg + 160; ctx.r5.u64 = particle_arg;
            call_guest(ctx, base, sub_82101328);
            copy_matrix(base, destination, frame.slot(112));
        }
    };
    auto emit_once = [&](uint64_t event, uint32_t matrix, double default_value) {
        ctx.r3.u64 = manager; ctx.r4.u64 = event; ctx.r5.u64 = matrix; ctx.r6.u64 = 0;
        ctx.f1.f64 = default_value; ctx.f2.f64 = default_value;
        call_guest(ctx, base, sub_82170FF0);
    };

    uint32_t definition = ld32(base, emitter + 36);
    if (ld32(base, definition + 324) != 1) {
        uint32_t behavior = ld32(base, emitter + 40);
        const int32_t orientation = int32_t(ld32(base, behavior + 8));
        const double zero = scalar_at(0x820454E0), one = scalar_at(0x820007A4);
        if (orientation < 0 || orientation == 4) {
            if (ld32(base, definition + 408)) {
                Vector angles = scale(ctx, load(base, particle + 80), mul(ctx, dt, scalar_at(0x8200BE04)));
                store(base, frame.slot(208), angles);
                const int32_t track = int32_t(ld32(base, behavior + 188));
                if (track >= 0) store(base, frame.slot(208), scale(ctx, angles, curve(track)));
                ctx.r3.u64 = frame.slot(112); ctx.r4.u64 = frame.slot(208);
                call_guest(ctx, base, sub_8217D098);
                rotate_basis(ctx, base, particle, ctx.r3.u32);
            }
        } else {
            const Vector rotation = load(base, particle + 80);
            double speed = component<2>(ctx, rotation);
            const int32_t track = int32_t(ld32(base, behavior + 188));
            if (track >= 0) speed = mul(ctx, curve(track), speed);
            if (speed != zero) {
                const double angle = madd(ctx, mul(ctx, dt, scalar_at(0x8200BE04)), speed,
                                          component<0>(ctx, rotation));
                store(base, particle + 80, replace<0>(ctx, load(base, particle + 80), angle));
            }
        }

        Vector velocity;
        if (ld8(base, particle + 130) & 0x80) {
            definition = ld32(base, emitter + 36);
            velocity = advance(ctx, load(base, particle + 64), load(base, definition + 192), dt);
            store(base, particle + 64, velocity);
        } else {
            behavior = ld32(base, emitter + 40);
            Vector initial = load(base, particle + 64);
            const int32_t track = int32_t(ld32(base, behavior + 176));
            if (track >= -1) {
                if (track >= 65536) initial = scale(ctx, initial, curve(track - 65536));
                else {
                    if (track >= 0) initial = replace<0>(ctx, initial, mul(ctx, curve(track), component<0>(ctx, initial)));
                    const int32_t y = int32_t(ld32(base, behavior + 180));
                    if (y >= 0) initial = replace<1>(ctx, initial, mul(ctx, curve(y), component<1>(ctx, initial)));
                    const int32_t z = int32_t(ld32(base, behavior + 184));
                    if (z >= 0) initial = replace<2>(ctx, initial, mul(ctx, curve(z), component<2>(ctx, initial)));
                }
            }
            definition = ld32(base, emitter + 36);
            velocity = advance(ctx, initial, load(base, definition + 192), scalar_at(particle + 96));
            const int32_t force_track = int32_t(ld32(base, definition + 456));
            if (force_track >= 0) {
                Vector force = scale(ctx, load(base, definition + 176), curve(force_track));
                if (!ld32(base, behavior + 12)) force = transform(ctx, base, emitter + 224, force);
                velocity = sum(ctx, velocity, force);
            }
        }

        if (ld8(base, emitter + 20)) {
            ctx.r3.u64 = frame.slot(176); ctx.r4.u64 = manager;
            ctx.r5.u64 = frame.slot(96); ctx.r6.u64 = ld32(base, emitter + 44);
            call_guest(ctx, base, sub_82148408);
            const Vector external = load(base, frame.slot(176));
            if (!zero_xyz(ctx, external)) {
                definition = ld32(base, emitter + 36);
                double strength = scalar_at(definition + 368);
                const int32_t track = int32_t(ld32(base, definition + 452));
                if (track >= 0) strength = mul(ctx, curve(track), strength);
                const double seed_scale = mul(ctx, double(ld16(base, particle + 128)), scalar_at(0x8200072C));
                const double weight = mul(ctx, madd(ctx, seed_scale, scalar_at(0x8200EE04), scalar_at(0x8200EE00)), strength);
                Vector force = scale(ctx, external, weight);
                store(base, frame.slot(176), force);
                if (!ld32(base, ld32(base, emitter + 40) + 12)) {
                    force = transform(ctx, base, emitter + 224, force);
                    store(base, frame.slot(176), force);
                }
                velocity = sum(ctx, velocity, force);
            }
        }

        position = advance(ctx, position, velocity, dt);
        store(base, frame.slot(96), position);
        behavior = ld32(base, emitter + 40);
        if (!ld32(base, behavior + 12)) {
            definition = ld32(base, emitter + 36);
            const int32_t x = int32_t(ld32(base, definition + 424));
            if (x >= 0 && !ld32(base, definition + 324)) {
                position = replace<0>(ctx, position, mul(ctx, curve(x), component<0>(ctx, position)));
                store(base, frame.slot(96), position);
                const int32_t y = int32_t(ld32(base, definition + 428));
                if (y >= 0) {
                    position = replace<1>(ctx, position, mul(ctx, curve(y), component<1>(ctx, position)));
                    store(base, frame.slot(96), position);
                }
                const int32_t z = int32_t(ld32(base, definition + 432));
                if (z >= 0) {
                    position = replace<2>(ctx, position, mul(ctx, curve(z), component<2>(ctx, position)));
                    store(base, frame.slot(96), position);
                }
            }
        }

        const double default_value = scalar_at(0x820007C0);
        if (int32_t(ld32(base, behavior + 120)) >= 1 && ld8(base, emitter + 25)
            && scalar_at(particle + 120) < scalar_at(particle + 96)) {
            if (scalar_at(behavior + 128) > zero) {
                const uint32_t random = random15(base);
                const uint32_t current_behavior = ld32(base, emitter + 40);
                const double range = scalar_at(current_behavior + 132);
                const double spread = mul(ctx, double(random), sub(ctx, range, -range));
                const double delta = add(ctx, madd(ctx, spread, scalar_at(0x8200072C), -range),
                                         scalar_at(ld32(base, emitter + 40) + 128));
                stf(base, particle + 120, add(ctx, delta, scalar_at(particle + 120)));
            } else stf(base, particle + 120, add(ctx, scalar_at(particle + 100), scalar_at(0x8200EDFC)));
            matrix_to(frame.slot(240), ld32(base, ld32(base, emitter + 40) + 12) != 0);
            const uint32_t current_behavior = ld32(base, emitter + 40);
            const uint32_t table_index = ld32(base, emitter + 68);
            const uint64_t event = uint64_t(ld32(base, manager + 0x120000 + 696 + table_index * 4))
                                 + ld32(base, current_behavior + 120);
            bool persistent = ld32(base, current_behavior + 136) != 0;
            if (!persistent) {
                ctx.r3.u64 = manager; ctx.r4.u64 = event;
                call_guest(ctx, base, sub_821419A0);
                persistent = ctx.r3.u8 != 0;
            }
            if (!persistent) emit_once(event, frame.slot(240), default_value);
            else if (!ld32(base, particle + 124)) {
                ctx.r3.u64 = manager; ctx.r4.u64 = event; ctx.r5.u64 = frame.slot(240);
                ctx.r6.u64 = 0; ctx.r9.u64 = 0; ctx.f1.f64 = default_value; ctx.f2.f64 = default_value;
                call_guest(ctx, base, sub_82170CA8);
                st32(base, particle + 124, ctx.r3.u32);
                if (ctx.r3.u32) {
                    st32(base, ctx.r3.u32 + 776, ld32(base, emitter + 44));
                    ctx.r4.u64 = ld32(base, emitter + 44); ctx.r3.u64 = ld32(base, particle + 124);
                    call_guest(ctx, base, sub_82149B88);
                }
            }
        }

        if (ld8(base, emitter + 21)) {
            store(base, frame.slot(304), load(base, particle + 48));
            store(base, frame.slot(80), position);
            ctx.r3.u64 = 0; ctx.r4.u64 = frame.slot(304); ctx.r5.u64 = frame.slot(80);
            ctx.r6.u64 = frame.slot(336); ctx.r7.u64 = frame.slot(224);
            call_guest(ctx, base, sub_8217C450);
            if (ctx.r3.u8) {
                store(base, frame.slot(192), zero_w(load(base, frame.slot(224))));
                const uint32_t random_a = random15(base), random_b = random15(base);
                definition = ld32(base, emitter + 36);
                const double response = mul(ctx, madd(ctx, double(random_a), scalar_at(0x8200EDF8), scalar_at(0x8200EDF4)),
                                            scalar_at(definition + 380));
                const double range = scalar_at(definition + 384);
                const double spread = mul(ctx, double(random_b), sub(ctx, range, -range));
                const double variation = add(ctx, madd(ctx, spread, scalar_at(0x8200072C), -range), one);
                velocity = scale(ctx, velocity, -response);
                ctx.f1.f64 = mul(ctx, variation, scalar_at(0x8201989C));
                ctx.r3.u64 = frame.slot(80); ctx.r5.u64 = frame.slot(192);
                call_guest(ctx, base, sub_82101A08);
                store(base, frame.slot(320), load(base, frame.slot(80)));
                store(base, frame.slot(192), zero_w(load(base, 0x8200BF20)));
                ctx.r3.u64 = frame.slot(112); ctx.r4.u64 = frame.slot(320); ctx.r5.u64 = frame.slot(192);
                call_guest(ctx, base, sub_82100F88);
                store(base, particle + 64, transform(ctx, base, frame.slot(112), velocity));
                st8(base, particle + 130, ld8(base, particle + 130) | 0x80);
                definition = ld32(base, emitter + 36);
                position = advance(ctx, load(base, frame.slot(336)), load(base, frame.slot(224)), scalar_at(definition + 376));
                store(base, frame.slot(96), position);
                const int32_t event = int32_t(ld32(base, definition + 388));
                if (event >= 0) emit_once(uint32_t(event), particle, default_value);
                definition = ld32(base, emitter + 36);
                const double lifetime = scalar_at(definition + 392);
                if (!(lifetime < zero)) {
                    double target = sub(ctx, scalar_at(particle + 100), lifetime);
                    const double age = scalar_at(particle + 96);
                    if (age > target) target = age;
                    stf(base, particle + 96, target);
                }
            }
        }
        position = replace<3>(ctx, load(base, frame.slot(96)), 1.0);
        store(base, frame.slot(96), position);
        store(base, particle + 48, position);
    }

    ctx.r3.u64 = emitter_arg; ctx.r4.u64 = particle_arg; ctx.f1.f64 = dt;
    call_virtual(ctx, base, emitter, 2);
    ctx.r3.u64 = ld32(base, particle + 124);
    if (ctx.r3.u32) {
        if (int32_t(ld32(base, ctx.r3.u32 + 768)) >= 0) {
            call_guest(ctx, base, sub_82146F60);
            st32(base, particle + 124, 0);
        } else {
            const uint32_t behavior = ld32(base, emitter + 40);
            if (ld32(base, behavior + 136)) {
                matrix_to(frame.slot(112), ld32(base, behavior + 12) != 0);
                ctx.r3.u64 = ld32(base, particle + 124); ctx.r4.u64 = frame.slot(112);
                call_guest(ctx, base, sub_82141E48);
                ctx.r3.u64 = ld32(base, particle + 124); ctx.r4.u64 = ld32(base, emitter + 44);
                call_guest(ctx, base, sub_82149B88);
            }
        }
    }
}
NIER_VERIFY_SPEC(sub_82144588, 0x82144588,
    "r3=const:0x40000000 r4=const:0x40050000 f1=f32 callargs=2 "
    "@r3.36=const:0x40010000 @r3.40=const:0x40020000 @r3.48=const:0x40030000 "
    "@r3.20=const:0 @r3.24=const:0 @r3.0x10144=const:0 @r3.0x10198=range:0..1 "
    "@r3.0x101c4=const:0xffffffff @r3.0x101c8=const:0xffffffff @r3.0x101a8=const:0xffffffff "
    "@r3.0x20008=range:0xfffffffe..5 @r3.0x2000c=range:0..1 @r3.0x200b0=const:0xfffffffe "
    "@r3.0x200b4=const:0xffffffff @r3.0x200b8=const:0xffffffff @r3.0x200bc=const:0xffffffff "
    "@r3.0x20078=const:0 @r3.0x20088=const:0 @r4.124=const:0 @r4.128=int "
    "@r4.96=const:0x3f800000 @r4.104=const:0x3f000000");
