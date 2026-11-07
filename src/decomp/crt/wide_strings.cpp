// Xbox wide strings contain big-endian 16-bit code units, unlike Linux wchar_t.
#include <nier/decomp.h>
using namespace nier::guest;

// wcsncpy counts 16-bit code units and pads with zero units after the NUL.
NIER_DECOMP(sub_825D3958) {
    uint32_t dst=ctx.r3.u32,src=ctx.r4.u32,n=ctx.r5.u32;
    while(n) {
        const uint16_t ch=ld16(base,src);src+=2;st16(base,dst,ch);dst+=2;--n;
        if(!ch) {while(n--) {st16(base,dst,0);dst+=2;}break;}
    }
}
NIER_VERIFY_SPEC(sub_825D3958,0x825D3958,"r3=const:0x40010000 r4=const:0x40050000 r5=range:0..128");

// Single-byte locale conversion: zero/input absent/count zero returns 0;
// otherwise one byte becomes one BE code unit. A null output is allowed.
NIER_DECOMP(sub_825E9678) {
    const uint32_t dst=ctx.r3.u32,src=ctx.r4.u32;
    if(!src || !ctx.r5.u32) {ctx.r3.u64=0;return;}
    const uint8_t ch=ld8(base,src);
    if(dst)st16(base,dst,ch);
    ctx.r3.u64=ch?1:0;
}
NIER_VERIFY_SPEC(sub_825E9678,0x825E9678,"r3=const:0x40010000 r4=const:0x40050000 r5=range:0..8");

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
    "r3=const:0x40010000 r4=const:0x40050000");

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
    "r3=const:0x40010000 r4=const:0x40050000");

// wcschr: low 16 bits of the search value, including the terminating zero.
NIER_DECOMP(sub_825D39B8) {
    uint64_t cursor=ctx.r3.u64;const uint16_t target=ctx.r4.u16;
    for(;;) {
        const uint16_t value=ld16(base,uint32_t(cursor));
        if(value==target) {ctx.r3.u64=cursor;return;}
        if(!value) {ctx.r3.u64=0;return;}
        cursor+=2;
    }
}
NIER_VERIFY_SPEC(sub_825D39B8,0x825D39B8,"r3=ptr:2 r4=range:0..255 @r3.0=range:0x00410001..0x004100FF");

// wcsstr searches 16-bit code units and preserves the full returned pointer.
NIER_DECOMP(sub_825D3BD0) {
    const uint64_t start=ctx.r3.u64;const uint32_t needle=ctx.r4.u32;
    if(!ld16(base,needle))return;
    for(uint32_t offset=0;ld16(base,uint32_t(start)+offset);offset+=2) {
        uint32_t i=0;
        while(ld16(base,needle+i) && ld16(base,uint32_t(start)+offset+i)==ld16(base,needle+i))i+=2;
        if(!ld16(base,needle+i)) {ctx.r3.u64=start+offset;return;}
    }
    ctx.r3.u64=0;
}
NIER_VERIFY_SPEC(sub_825D3BD0,0x825D3BD0,"r3=ptr:2 r4=ptr:2");

// wcscmp returns only the normalized sign of the first differing code unit.
NIER_DECOMP(sub_825D3CC0) {
    uint32_t a=ctx.r3.u32,b=ctx.r4.u32;
    for(;;) {
        const uint16_t x=ld16(base,a),y=ld16(base,b);
        if(x!=y || !y) {ctx.r3.s64=(x>y)-(x<y);return;}
        a+=2;b+=2;
    }
}
NIER_VERIFY_SPEC(sub_825D3CC0,0x825D3CC0,"r3=ptr:2 r4=ptr:2");

// wcsncmp instead returns the full code-unit difference.
NIER_DECOMP(sub_825D3D08) {
    uint32_t a=ctx.r3.u32,b=ctx.r4.u32,n=ctx.r5.u32;int diff=0;
    while(n--) {
        const uint16_t x=ld16(base,a),y=ld16(base,b);diff=int(x)-int(y);
        if(!x || diff)break;a+=2;b+=2;
    }
    ctx.r3.s64=diff;
}
NIER_VERIFY_SPEC(sub_825D3D08,0x825D3D08,"r3=ptr:2 r4=ptr:2 r5=range:0..128");
