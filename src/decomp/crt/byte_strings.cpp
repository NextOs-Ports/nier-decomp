// Reconstructed byte-string contracts of the linked CRT; unsigned byte loads.
#include <nier/decomp.h>
using namespace nier::guest;
namespace {
uint32_t ascii_lower(uint32_t value) { return value>='A' && value<='Z' ? value+32 : value; }
}

// strlen: word-at-a-time zero detection in the PPC becomes a byte count.
NIER_DECOMP(sub_825DAC60) {
    const uint32_t text=ctx.r3.u32;uint32_t length=0;
    while(ld8(base,text+length))++length;
    ctx.r3.u64=length;
}
NIER_VERIFY_SPEC(sub_825DAC60,0x825DAC60,"r3=bptr");

// strcpy, including the zero byte. Return the full original destination.
NIER_DECOMP(sub_825DABF0) {
    uint32_t dst=ctx.r3.u32,src=ctx.r4.u32;
    for(;;) {uint8_t ch=ld8(base,src++);st8(base,dst++,ch);if(!ch)break;}
}
NIER_VERIFY_SPEC(sub_825DABF0,0x825DABF0,"r3=const:0x40010000 r4=const:0x40050000");

// strcat, retaining the original destination as return value.
NIER_DECOMP(sub_825DAC18) {
    uint32_t dst=ctx.r3.u32,src=ctx.r4.u32;
    while(ld8(base,dst))++dst;
    for(;;) {uint8_t ch=ld8(base,src++);st8(base,dst++,ch);if(!ch)break;}
}
NIER_VERIFY_SPEC(sub_825DAC18,0x825DAC18,"r3=const:0x40010000 r4=const:0x40050000");

// strcmp returns a normalized sign in this CRT variant.
NIER_DECOMP(sub_825DABB0) {
    uint32_t a=ctx.r3.u32,b=ctx.r4.u32;
    for(;;) {
        const int x=ld8(base,a++),y=ld8(base,b++);
        if(x!=y || !y) {ctx.r3.s64=(x>y)-(x<y);return;}
    }
}
NIER_VERIFY_SPEC(sub_825DABB0,0x825DABB0,"r3=bptr r4=bptr");

// strncmp returns the actual byte difference, with a signed count guard.
NIER_DECOMP(sub_825DA610) {
    uint32_t a=ctx.r3.u32,b=ctx.r4.u32;int64_t diff=0;
    for(int32_t n=ctx.r5.s32;n>0;--n) {
        const int x=ld8(base,a++),y=ld8(base,b++);diff=x-y;
        if(diff || !x)break;
    }
    ctx.r3.s64=diff;
}
NIER_VERIFY_SPEC(sub_825DA610,0x825DA610,"r3=bptr r4=bptr r5=range:0..128");

// strncat appends at most n source bytes and always terminates the result.
NIER_DECOMP(sub_825D9F10) {
    uint32_t dst=ctx.r3.u32,src=ctx.r4.u32,n=ctx.r5.u32;
    while(ld8(base,dst))++dst;
    while(n--) {const uint8_t ch=ld8(base,src++);st8(base,dst++,ch);if(!ch)return;}
    st8(base,dst,0);
}
NIER_VERIFY_SPEC(sub_825D9F10,0x825D9F10,"r3=const:0x40010000 r4=const:0x40050000 r5=range:0..32");

// strrchr-equivalent for a byte-valued search argument, including zero.
NIER_DECOMP(sub_825D4050) {
    uint64_t cursor=ctx.r3.u64,found=0;const int32_t target=ctx.r4.s32;
    for(;;) {const uint8_t ch=ld8(base,uint32_t(cursor));if(ch==target)found=cursor;if(!ch)break;++cursor;}
    ctx.r3.u64=found;
}
NIER_VERIFY_SPEC(sub_825D4050,0x825D4050,"r3=bptr r4=u8");

// strstr: a zero-length needle returns the original pointer.
NIER_DECOMP(sub_825D3C48) {
    const uint64_t start=ctx.r3.u64;const uint32_t needle=ctx.r4.u32;
    if(!ld8(base,needle))return;
    for(uint32_t offset=0;ld8(base,uint32_t(start)+offset);++offset) {
        uint32_t i=0;
        while(ld8(base,needle+i) && ld8(base,uint32_t(start)+offset+i)==ld8(base,needle+i))++i;
        if(!ld8(base,needle+i)) {ctx.r3.u64=start+offset;return;}
    }
    ctx.r3.u64=0;
}
NIER_VERIFY_SPEC(sub_825D3C48,0x825D3C48,"r3=bptr r4=bptr");

// strcspn: count the prefix not containing any byte in the reject set.
NIER_DECOMP(sub_825D9F60) {
    bool rejected[256]{};uint32_t set=ctx.r4.u32;
    for(uint8_t ch;(ch=ld8(base,set++))!=0;)rejected[ch]=true;
    rejected[0]=true;
    uint32_t count=0;while(!rejected[ld8(base,ctx.r3.u32+count)])++count;
    ctx.r3.u64=count;
}
NIER_VERIFY_SPEC(sub_825D9F60,0x825D9F60,"r3=bptr r4=bptr");

// strpbrk: first member of an accept set, or a null pointer.
NIER_DECOMP(sub_825DA050) {
    bool accepted[256]{};uint32_t set=ctx.r4.u32;
    for(uint8_t ch;(ch=ld8(base,set++))!=0;)accepted[ch]=true;
    uint64_t cursor=ctx.r3.u64;
    for(uint8_t ch;(ch=ld8(base,uint32_t(cursor)))!=0;++cursor)
        if(accepted[ch]) {ctx.r3.u64=cursor;return;}
    ctx.r3.u64=0;
}
NIER_VERIFY_SPEC(sub_825DA050,0x825DA050,"r3=bptr r4=bptr");

// ASCII case comparison variant: an already-ended RHS returns the raw LHS byte.
NIER_DECOMP(sub_825DACE0) {
    uint32_t a=ctx.r3.u32,b=ctx.r4.u32;
    for(;;) {
        const int x=ld8(base,a++),y=ld8(base,b++);
        if(!y) {ctx.r3.s64=x-y;return;}
        if(x==y)continue;
        const int diff=int(ascii_lower(x))-int(ascii_lower(y));
        if(diff) {ctx.r3.s64=diff;return;}
    }
}
NIER_VERIFY_SPEC(sub_825DACE0,0x825DACE0,"r3=bptr r4=bptr");

// Bounded ASCII case comparison (not locale/Unicode case folding).
NIER_DECOMP(sub_825DB038) {
    uint32_t a=ctx.r3.u32,b=ctx.r4.u32,n=ctx.r5.u32;int diff=0;
    while(n--) {
        const int x=ascii_lower(ld8(base,a++)),y=ascii_lower(ld8(base,b++));
        diff=x-y;if(!x || diff)break;
    }
    ctx.r3.s64=diff;
}
NIER_VERIFY_SPEC(sub_825DB038,0x825DB038,"r3=bptr r4=bptr r5=range:0..128");

// Unbounded ASCII case comparison, both bytes folded before the end test.
NIER_DECOMP(sub_825E4AF0) {
    uint32_t a=ctx.r3.u32,b=ctx.r4.u32;
    for(;;) {
        const int x=ascii_lower(ld8(base,a++)),y=ascii_lower(ld8(base,b++));
        if(!x || x!=y) {ctx.r3.s64=x-y;return;}
    }
}
NIER_VERIFY_SPEC(sub_825E4AF0,0x825E4AF0,"r3=bptr r4=bptr");
