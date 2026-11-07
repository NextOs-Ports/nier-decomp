// Observers preserve the translated routines and never change guest timing.
#include "generated/default/nier_pch.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <unordered_map>

extern "C" void __imp__sub_82596750(PPCContext&, uint8_t*);
extern "C" void __imp__sub_8257AB30(PPCContext&, uint8_t*);
namespace {
bool enabled() {
    static const bool value = std::getenv("NIER_TIMING_PROBE") != nullptr;
    return value;
}
struct Sample {
    unsigned count = 0;
    double sum = 0;
    std::chrono::steady_clock::time_point start;
};
std::mutex probe_mutex;
std::unordered_map<uint32_t, Sample> samples;
}
extern "C" void sub_82596750(PPCContext& ctx, uint8_t* base) {
    const auto object = ctx.r3.u32;
    __imp__sub_82596750(ctx, base);
    if (!enabled()) return;
    const auto now = std::chrono::steady_clock::now();
    PPCRegister value{};
    value.u32 = REX_LOAD_U32(object + 88);
    std::lock_guard lock(probe_mutex);
    auto& sample = samples[object];
    if (sample.count == 0) sample.start = now;
    else sample.sum += value.f32;
    ++sample.count;
    if (sample.count <= 3 || sample.count % 300 == 0) {
        std::fprintf(stderr, "NIER_TIMING object=%08x samples=%u dt=%.9f sum=%.6f wall=%.6f\n",
                     object, sample.count, value.f32, sample.sum,
                     std::chrono::duration<double>(now-sample.start).count());
    }
}
extern "C" void sub_8257AB30(PPCContext& ctx, uint8_t* base) {
    const auto start = std::chrono::steady_clock::now();
    __imp__sub_8257AB30(ctx, base);
    if (!enabled()) return;
    static thread_local unsigned calls = 0;
    if (++calls <= 3 || calls % 300 == 0)
        std::fprintf(stderr, "NIER_VBLANK calls=%u wait_ms=%.3f\n", calls,
            std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
}
