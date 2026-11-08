// Reconstrução dos descritores de fetch: montagem, ordenação e coalescência.
#include <nier/decomp.h>
#include <algorithm>
#include <bit>
using namespace nier::guest;
DECLARE_REX_FUNC(__imp__sub_82578568);
namespace {
struct Fetch { uint32_t instruction,format,address; };
uint32_t stream(const Fetch& f){return ((f.instruction>>18)&124u)|((f.instruction>>25)&3u);}
int32_t offset(const Fetch& f){return std::bit_cast<int32_t>(f.address<<1)>>9;}
uint32_t signature(uint32_t instruction){return ((instruction&~0xC0000u)|(std::rotl(instruction,25)&0xC0000u))&0x1FC0000u;}
uint32_t swizzle(uint16_t bits){return ((bits>>1)|(bits>>4)|(bits>>7)|(bits>>10)|(bits>>13))&7u;}
}
NIER_DECOMP(sub_82578568){
    const uint32_t program=ctx.r3.u32,layout=ctx.r5.u32,mapping=ctx.r6.u32,variant=ctx.r7.u32;
    const uint64_t destination=ctx.r4.u64;
    const uint32_t descriptor=program+ld32(base,program+(variant+112u)*8u)+872;
    const uint32_t count=ld32(base,descriptor+28);
    // O temporário original contém 32 entradas. Preservar a rota original
    // para entradas que excedem a área temporária observada na rotina.
    if(count>32){call_guest(ctx,base,__imp__sub_82578568);return;}
    GuestFrame frame(ctx,base,592);
    if(!count)return;
    const uint32_t patches=descriptor+(ld32(base,descriptor+24)+9u)*4u;
    const uint32_t fields=ld32(base,layout+24),source=program+variant*416u+68;
    uint16_t controls=ld16(base,frame.slot(80));
    Fetch fetch[32];
    for(uint32_t i=0;i<count;++i){
        Fetch f{ld32(base,source+i*12),ld32(base,source+i*12+4),ld32(base,source+i*12+8)};
        const uint32_t patch=ld32(base,patches+i*4);
        uint32_t row=layout+52,field=0;
        for(;field<fields;++field,row+=12)
            if(ld8(base,row+9)==((patch>>12)&15)&&ld8(base,row+10)==((patch>>16)&15))break;
        if(field==fields){
            f.instruction=(f.instruction&~0x3FF00000u)|0x05F00000u;
            f.format=(f.format&0xBFC6CFFFu)|0x60000u;
            f.address=(f.address&0x80000000u)|ld8(base,mapping);
            controls=(controls&15u)|0x9250u;
        }else{
            const uint32_t format=ld32(base,row+4);
            const uint32_t a=(format>>16)&7u,b=(format>>10)&56u,c=(format>>19)&7u;
            controls=uint16_t((((((a|b)<<3)|c)<<1)|(format&0xFC00u))<<3)|(controls&15u);
            if((format&0xC0)==0x40)
                for(unsigned shift:{13u,10u,7u,4u})if(((controls>>shift)&7u)<=3)controls^=uint16_t(1u<<shift);
            const uint32_t slot=ld16(base,row),byte_offset=ld16(base,row+2);
            uint32_t encoded=(95u-slot)*0x01560056u;
            encoded=(encoded&~0xF80000u)|(std::rotl(encoded,11)&0xF80000u);
            f.instruction=((f.instruction&~0x7F00000u)|(std::rotl(encoded,1)&0x7F00000u))&0xC7FFFFFFu;
            f.format=(f.format&0xBFC0CFFFu)|(((std::rotl(format,12)&0x3F000u)|(format&0x300u))<<4);
            f.address=(f.address&0x80000000u)|((byte_offset<<6)&0x7FFFFF00u)|ld8(base,mapping+slot);
        }
        controls|=14;
        for(unsigned channel=0;channel<4;++channel){
            const unsigned shift=channel*3;
            const uint16_t mask=ld16(base,0x82096F98+((f.format>>shift)&7u)*2);
            f.format=(f.format&~(7u<<shift))|(swizzle(mask&controls)<<shift);
        }
        fetch[i]=f;
    }
    uint32_t scan=0;
    while(scan<count){
        while(scan<count&&!(ld32(base,patches+scan*4)&0x300000))++scan;
        if(scan==count)break;
        const uint32_t first=scan;
        uint32_t last=first+1;
        while(last<count&&!(ld32(base,patches+last*4)&0x300000))++last;
        if(last==count)break;
        const bool reserved=(ld32(base,patches+last*4)&0x300000)==0x300000;
        const uint32_t special=reserved?stream(fetch[last]):0xFFFFFFFFu;
        const uint32_t end=last+(reserved?0:1);
        for(uint32_t i=first;i+1<end;++i)for(uint32_t j=i+1;j<end;++j){
            const uint32_t ka=stream(fetch[i]),kb=stream(fetch[j]);
            bool swap;
            if(ka!=kb)swap=ka==special||(kb!=special&&kb>ka);
            else{
                const int32_t difference=offset(fetch[i])-offset(fetch[j]);
                if(difference)swap=difference>0;
                else swap=ld8(base,0x82096FA8+((fetch[i].format>>16)&63u))>
                          ld8(base,0x82096FA8+((fetch[j].format>>16)&63u));
            }
            if(swap)std::swap(fetch[i],fetch[j]);
        }
        uint32_t previous=first;
        for(uint32_t i=first+1;i<=last;++i){
            const uint32_t a=uint32_t(offset(fetch[previous])),b=uint32_t(offset(fetch[i]));
            const uint32_t size=ld8(base,0x82096FA8+((fetch[i].format>>16)&63u));
            const uint32_t span=b+size-a;
            if(signature(fetch[previous].instruction)==signature(fetch[i].instruction)&&a<=b&&span<=8){
                const uint32_t width=std::max((fetch[previous].instruction>>27)&7u,span-1u);
                fetch[previous].instruction=(fetch[previous].instruction&~0x38000000u)|((width<<27)&0x38000000u);
                fetch[i].format|=0x40000000;
            }else previous=i;
        }
        scan=last+1;
    }
    for(uint32_t i=0;i<count;++i){
        const uint32_t index=ld32(base,patches+i*4)&0xFFFu;
        ctx.r3.u64=destination+index*12u;
        st32(base,ctx.r3.u32,fetch[i].instruction);
        st32(base,ctx.r3.u32+4,fetch[i].format);
        st32(base,ctx.r3.u32+8,fetch[i].address);
    }
}
NIER_VERIFY_SPEC(sub_82578568,0x82578568,
    "r3=const:0x40010000 r4=const:0x40050000 r5=const:0x40030000 r6=const:0x40060000 r7=range:0..1 "
    "@r3.896=const:0 @r3.904=const:0 @r3.900=range:1..32 @r5.24=range:0..8");
