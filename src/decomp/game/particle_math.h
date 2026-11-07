#pragma once
#include <nier/decomp.h>
#include <cmath>

// Vetores locais do host, na ordem de lanes do backend SIMD. Nada é guardado
// nos registradores vetoriais do PPCContext. Byteswap só no acesso ao guest.
namespace nier::particle {
using namespace nier::guest;
using Vector = simde__m128;
// A ordem dos operandos também determina qual payload NaN o backend propaga.
// Clang pode comutar mul/add de vetores locais; preservar explicitamente o
// payload no caso raro de dois NaNs, conforme a ordem da tradução de referência.
inline Vector ordered_nan(Vector result, Vector a, Vector b) {
    const Vector both = simde_mm_and_ps(simde_mm_cmpunord_ps(a, a), simde_mm_cmpunord_ps(b, b));
    if (simde_mm_movemask_ps(both)) [[unlikely]] {
        const Vector quiet = simde_mm_castsi128_ps(simde_mm_set1_epi32(0x00400000));
        result = simde_mm_blendv_ps(result, simde_mm_or_ps(a, quiet), both);
    }
    return result;
}
inline Vector ordered_mul(Vector a, Vector b) { return simde_mm_mul_ps(a, b); }
inline Vector ordered_add(Vector a, Vector b) { return simde_mm_add_ps(a, b); }
inline Vector load(const uint8_t* base, uint32_t address) {
    return simde_mm_castsi128_ps(simde_mm_shuffle_epi8(
        simde_mm_loadu_si128(reinterpret_cast<const simde__m128i*>(base + (address & ~15u))),
        simde_mm_loadu_si128(reinterpret_cast<const simde__m128i*>(VectorMaskL))));
}
inline void store(uint8_t* base, uint32_t address, Vector value) {
    simde_mm_storeu_si128(reinterpret_cast<simde__m128i*>(base + (address & ~15u)),
        simde_mm_shuffle_epi8(simde_mm_castps_si128(value),
            simde_mm_loadu_si128(reinterpret_cast<const simde__m128i*>(VectorMaskL))));
}
inline double scalar(PPCContext& ctx, const uint8_t* base, uint32_t address) {
    ctx.fpscr.disableFlushMode();
    return ldf(base, address);
}
inline double mul(PPCContext& ctx, double a, double b) {
    ctx.fpscr.disableFlushMode(); return static_cast<float>(a * b);
}
inline double add(PPCContext& ctx, double a, double b) {
    ctx.fpscr.disableFlushMode(); return static_cast<float>(a + b);
}
inline double sub(PPCContext& ctx, double a, double b) {
    ctx.fpscr.disableFlushMode(); return static_cast<float>(a - b);
}
inline double madd(PPCContext& ctx, double a, double b, double c) {
    ctx.fpscr.disableFlushMode(); return static_cast<float>(std::fma(a, b, c));
}
template<int Lane> inline double component(PPCContext& ctx, Vector value) {
    ctx.fpscr.disableFlushMode();
    return simde_mm_cvtss_f32(simde_mm_shuffle_ps(value, value, (3-Lane)*85));
}
template<int Lane> inline Vector replace(PPCContext& ctx, Vector value, double scalar_value) {
    ctx.fpscr.disableFlushMode();
    return simde_mm_blend_ps(value, simde_mm_set1_ps(static_cast<float>(scalar_value)), 1 << (3-Lane));
}
inline Vector zero_w(Vector value) { return simde_mm_blend_ps(value, simde_mm_setzero_ps(), 1); }
inline Vector scale(PPCContext& ctx, Vector value, double factor) {
    ctx.fpscr.disableFlushMode();
    const Vector multiplier = simde_mm_set1_ps(static_cast<float>(factor));
    ctx.fpscr.enableFlushMode();
    return ordered_mul(value, multiplier);
}
inline Vector sum(PPCContext& ctx, Vector a, Vector b) {
    ctx.fpscr.enableFlushMode(); return ordered_add(a, b);
}
inline Vector advance(PPCContext& ctx, Vector value, Vector delta, double factor) {
    return sum(ctx, scale(ctx, delta, factor), value);
}
inline Vector transform(PPCContext& ctx, const uint8_t* base, uint32_t matrix, Vector value) {
    const Vector row0 = load(base, matrix), row1 = load(base, matrix + 16);
    const Vector row2 = load(base, matrix + 32), row3 = load(base, matrix + 48);
    ctx.fpscr.enableFlushMode();
    const Vector x = simde_mm_shuffle_ps(value, value, 255), y = simde_mm_shuffle_ps(value, value, 170);
    const Vector z = simde_mm_shuffle_ps(value, value, 85), w = simde_mm_shuffle_ps(value, value, 0);
    const Vector a = ordered_add(ordered_mul(row2, z), ordered_mul(row0, x));
    const Vector b = ordered_add(ordered_mul(row3, w), ordered_mul(row1, y));
    return ordered_add(a, b);
}
inline void rotate_basis(PPCContext& ctx, uint8_t* base, uint32_t particle, uint32_t rotation) {
    const Vector x = zero_w(load(base, particle)), y = zero_w(load(base, particle + 16));
    const Vector z = zero_w(load(base, particle + 32));
    Vector output[3];
    ctx.fpscr.enableFlushMode();
    for (int row = 0; row < 3; ++row) {
        const Vector r = zero_w(load(base, rotation + row * 16));
        const Vector rx = simde_mm_shuffle_ps(r, r, 255), ry = simde_mm_shuffle_ps(r, r, 170), rz = simde_mm_shuffle_ps(r, r, 85);
        const Vector a = ordered_nan(simde_mm_mul_ps(x, rx), x, rx);
        const Vector py = ordered_nan(simde_mm_mul_ps(y, ry), y, ry);
        const Vector b = ordered_nan(simde_mm_add_ps(py, a), a, py);
        const Vector c = ordered_nan(simde_mm_mul_ps(z, rz), z, rz);
        // O último produto da terceira linha conserva seu payload; nas outras
        // linhas a referência conserva o acumulador. Não canonicalizar NaNs.
        output[row] = row == 2 ? ordered_nan(simde_mm_add_ps(c, b), c, b) : ordered_nan(simde_mm_add_ps(c, b), b, c);
    }
    for (int row = 0; row < 3; ++row) store(base, particle + row * 16, output[row]);
}
inline bool zero_xyz(PPCContext& ctx, Vector value) {
    ctx.fpscr.enableFlushMode();
    const Vector v = zero_w(value);
    return simde_mm_cvtss_f32(simde_mm_dp_ps(v, v, 0xEF)) == 0.0f;
}
} // namespace nier::particle
