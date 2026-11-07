// Emissão PM4 dos intervalos contíguos marcados no bitset de constantes.
// O caminho comum copia palavras big-endian diretamente, sem troca de bytes.
#include <nier/decomp.h>
#include <bit>
using namespace nier::guest;
DECLARE_REX_FUNC(sub_82577A38);

NIER_DECOMP(sub_82578080) {
    const uint64_t device_arg = ctx.r3.u64;
    const uint32_t device = ctx.r3.u32;
    uint64_t mask = ctx.r4.u64, first_register = ctx.r5.u64;
    uint32_t source = ctx.r6.u32 - 4;
    uint32_t cursor = ld32(base, device + 48);
    GuestFrame frame(ctx, base, 144);
    do {
        const uint32_t skip = std::countl_zero(mask);
        const uint32_t limit = ld32(base, device + 52);
        mask = skip == 64 ? 0 : mask << skip;
        source += skip * 4;
        const uint32_t count = std::countl_one(mask);
        first_register += skip;
        if (cursor + count * 4 >= limit) {
            ctx.r3.u64 = device_arg;
            ctx.r4.u64 = cursor;
            ctx.r5.u64 = first_register;
            ctx.r6.u64 = source;
            ctx.r7.u64 = count;
            ctx.r8.u64 = 1;
            call_guest(ctx, base, sub_82577A38);
            cursor = ctx.r3.u32;
            source += count * 4;
        } else {
            cursor += 4;
            st32(base, cursor, ((count - 1) << 16) | uint32_t(first_register));
            // A entrada normal tem mask != 0; o do/while preserva também o
            // comportamento de acesso da tradução para o caso inválido vazio.
            uint32_t remaining = count;
            do {
                source += 4;
                cursor += 4;
                uint32_t word;
                std::memcpy(&word, base + source, 4);
                std::memcpy(base + cursor, &word, 4);
            } while (--remaining);
        }
        first_register += count;
        mask = count == 64 ? 0 : mask << count;
    } while (mask);
    st32(base, device + 48, cursor);
}
NIER_VERIFY_SPEC(sub_82578080, 0x82578080,
    "r3=const:0x40010000 r4=range:1..0xfffffffffffffffe r5=range:8192..20000 r6=const:0x40020000 "
    "@r3.48=const:0x40030000 @r3.52=const:0x40031000");

// Mesmo bitset, com 16 palavras por entrada e alinhamento dos blocos VMX.
NIER_DECOMP(sub_82578418) {
    const uint64_t device_arg = ctx.r3.u64;
    const uint32_t device = ctx.r3.u32;
    uint64_t mask = ctx.r4.u64, first_register = ctx.r5.u64;
    uint32_t source = ctx.r6.u32 - 4;
    uint32_t cursor = ld32(base, device + 48);
    GuestFrame frame(ctx, base, 176);
    do {
        const uint32_t skip = std::countl_zero(mask);
        const uint32_t limit = ld32(base, device + 52);
        mask = skip == 64 ? 0 : mask << skip;
        source += skip * 64;
        const uint32_t count = std::countl_one(mask), words = count * 16;
        first_register += skip * 16;
        if (cursor + (words + 3) * 4 >= limit) {
            ctx.r3.u64 = device_arg;
            ctx.r4.u64 = cursor;
            ctx.r5.u64 = first_register;
            ctx.r6.u64 = source;
            ctx.r7.u64 = words;
            ctx.r8.u64 = 16;
            call_guest(ctx, base, sub_82577A38);
            cursor = ctx.r3.u32;
            source += words * 4;
        } else {
            const uint32_t alignment = cursor & 15;
            const uint32_t padding = alignment == 4 ? 1 : alignment == 0 ? 2 : alignment == 12 ? 3 : 0;
            for (uint32_t i = 0; i < padding; ++i) {
                cursor += 4;
                st32(base, cursor, 0x80000000u);
            }
            cursor += 4;
            st32(base, cursor, ((words - 1) << 16) | uint32_t(first_register));
            uint32_t remaining = count;
            do {
                // Os quatro vetores são lidos antes de qualquer escrita, inclusive com alias.
                uint8_t block[64];
                std::memcpy(block, base + ((source + 4) & ~15u), 64);
                std::memcpy(base + ((cursor + 4) & ~15u), block, 64);
                source += 64;
                cursor += 64;
            } while (--remaining);
        }
        first_register += words;
        mask = count == 64 ? 0 : mask << count;
    } while (mask);
    st32(base, device + 48, cursor);
}
NIER_VERIFY_SPEC(sub_82578418, 0x82578418,
    "r3=const:0x40010000 r4=range:1..0xfffffffffffffffe r5=range:8192..20000 r6=const:0x40020000 "
    "@r3.48=const:0x40030000 @r3.52=const:0x40034000");
