// Xbox wide strings contain big-endian 16-bit code units, unlike Linux wchar_t.
#include <nier/decomp.h>
using namespace nier::guest;

// wcslen semantics, 36 bytes: count code units, not Unicode scalar values.
NIER_DECOMP(sub_825D3620) {
    const uint32_t string=ctx.r3.u32;
    uint32_t count=0;
    while(ld16(base,string+count*2)!=0) ++count;
    ctx.r3.s64=static_cast<int32_t>(count);
}
NIER_VERIFY_SPEC(sub_825D3620,0x825D3620,"r3=ptr:2");

// wcscat semantics, 52 bytes. The original returns the unchanged destination.
NIER_DECOMP(sub_825D3648) {
    uint32_t destination=ctx.r3.u32,source=ctx.r4.u32;
    while(ld16(base,destination)!=0) destination+=2;
    for(;;) {
        const uint16_t code_unit=ld16(base,source);
        st16(base,destination,code_unit);
        if(!code_unit) break;
        source+=2;destination+=2;
    }
}
NIER_VERIFY_SPEC(sub_825D3648,0x825D3648,
    "r3=const:0x40010000 r4=const:0x40080000");

// wcscpy semantics, 28 bytes, including the terminating code unit.
NIER_DECOMP(sub_825D3680) {
    uint32_t destination=ctx.r3.u32,source=ctx.r4.u32;
    for(;;) {
        const uint16_t code_unit=ld16(base,source);
        st16(base,destination,code_unit);
        if(!code_unit) break;
        source+=2;destination+=2;
    }
}
NIER_VERIFY_SPEC(sub_825D3680,0x825D3680,
    "r3=const:0x40010000 r4=const:0x40080000");
