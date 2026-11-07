// Native byte semantics of the linked CRT memory routines.
#include <nier/decomp.h>
using namespace nier::guest;

// memcpy, 1160 original bytes. The PPC implementation handles alignment and
// cache blocks; ordinary nonoverlapping RAM still has byte-copy semantics.
// Keep the full incoming r3 (including its upper word) as the returned pointer.
NIER_DECOMP(sub_825D3010) {
    const uint32_t destination=ctx.r3.u32, source=ctx.r4.u32, count=ctx.r5.u32;
    for(uint32_t i=0;i<count;++i) st8(base,destination+i,ld8(base,source+i));
}
// Disjoint valid regions, all byte alignments, zero through 2048 bytes.
NIER_VERIFY_SPEC(sub_825D3010,0x825D3010,
    "r3=range:0x40010000..0x40020000 r4=range:0x40080000..0x40090000 r5=range:0..2048");

// memset, 160 original bytes. Replicated-word stores and alignment peeling
// reduce to filling each destination byte with the low byte of the int value.
NIER_DECOMP(sub_825D3D90) {
    const uint32_t destination=ctx.r3.u32, count=ctx.r5.u32;
    const uint8_t value=ctx.r4.u8;
    for(uint32_t i=0;i<count;++i) st8(base,destination+i,value);
}
NIER_VERIFY_SPEC(sub_825D3D90,0x825D3D90,
    "r3=bptr r4=int r5=range:0..2048");

// Scalar CRT byte-copy variant, 168 bytes. Same nonoverlapping memcpy contract,
// using a word-aligned body in the original rather than its cache-block path.
NIER_DECOMP(sub_825D3498) {
    const uint32_t destination=ctx.r3.u32, source=ctx.r4.u32, count=ctx.r5.u32;
    for(uint32_t i=0;i<count;++i) st8(base,destination+i,ld8(base,source+i));
}
NIER_VERIFY_SPEC(sub_825D3498,0x825D3498,
    "r3=range:0x40010000..0x40020000 r4=range:0x40080000..0x40090000 r5=range:0..2048");
