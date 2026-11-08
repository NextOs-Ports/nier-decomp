// Draw indexado/não indexado: descarregar bitsets e emitir pacotes sem registradores guest no laço.
#include <nier/decomp.h>
#include <bit>
using namespace nier::guest;
DECLARE_REX_FUNC(sub_82578418); DECLARE_REX_FUNC(sub_82579240);
DECLARE_REX_FUNC(sub_82577CE8); DECLARE_REX_FUNC(sub_82578080);
DECLARE_REX_FUNC(sub_825782C0); DECLARE_REX_FUNC(sub_82578140);
DECLARE_REX_FUNC(sub_825744F8); DECLARE_REX_FUNC(sub_82573888);
DECLARE_REX_FUNC(sub_82573670);
namespace {
uint32_t physical(uint32_t address,uint32_t mask=0x1FFFFFFFu){return (address&mask)+(((address>>20)+512u)&0x1000u);}
void constants(PPCContext& ctx,uint8_t* base,PPCFunc* fn,uint64_t device,uint64_t mask,uint32_t key,uint32_t offset){
    ctx.r3.u64=device;ctx.r4.u64=mask;ctx.r5.u64=key;ctx.r6.u64=device+offset;call_guest(ctx,base,fn);
}
void flush_state(PPCContext& ctx,uint8_t* base,uint64_t argument){
    const uint32_t device=uint32_t(argument);
    const uint64_t a=ld64(base,device),b=ld64(base,device+8),c=ld64(base,device+16),d=ld64(base,device+24),e=ld64(base,device+32);
    if(a){constants(ctx,base,sub_82578418,argument,a,16384,1920);st64(base,device,0);}
    if(b){constants(ctx,base,sub_82578418,argument,b,17408,6016);st64(base,device+8,0);}
    if(c){
        uint64_t mask=c;
        if(mask&0x1E0000){ctx.r3.u64=argument;ctx.r4.u64=mask;call_guest(ctx,base,sub_82579240);mask=ctx.r3.u64;}
        if(mask&ld64(base,device+40)){ctx.r3.u64=argument;ctx.r4.u64=mask;ctx.r5.u64=ld32(base,device+10560);call_guest(ctx,base,sub_82577CE8);mask=ctx.r3.u64;}
        if(mask&0xFFF)constants(ctx,base,sub_82578080,argument,std::rotl(mask,52)&0xFFF0000000000000ull,8704,10548);
        if(mask&0x1F000)constants(ctx,base,sub_82578080,argument,std::rotl(mask,47)&0xF800000000000000ull,8576,10528);
        if(mask&0x03FFFC0000000000ull)constants(ctx,base,sub_82578080,argument,std::rotl(mask,6)&0xFFFF000000000000ull,8192,10368);
        if(mask&0x000003FFFFE00000ull)constants(ctx,base,sub_82578080,argument,std::rotl(mask,22)&0xFFFFF80000000000ull,8448,10444);
        st64(base,device+16,0);
    }
    if(d){
        if(uint32_t(d)){ctx.r3.u64=argument;ctx.r4.u64=std::rotl(d,32)&0xFFFFFFFF00000000ull;call_guest(ctx,base,sub_825782C0);}
        if(d&0x007FFFFC00000000ull)constants(ctx,base,sub_82578080,argument,std::rotl(d,9)&0xFFFFF80000000000ull,8832,10596);
        st64(base,device+24,0);
    }
    if(e){
        if(e&0x3FFFFFFFFFull)constants(ctx,base,sub_82578080,argument,std::rotl(e,26)&0xFFFFFFFFFC000000ull,8960,10680);
        if(e&0x00003FC000000000ull)constants(ctx,base,sub_82578080,argument,std::rotl(e,18)&0xFF00000000000000ull,9088,10832);
        if(e&0x007E000000000000ull){ctx.r3.u64=argument;ctx.r4.u64=std::rotl(e,9)&0xFC00000000000000ull;call_guest(ctx,base,sub_82578140);}
        if(e&(1ull<<56))constants(ctx,base,sub_82578080,argument,uint64_t(-16777216ll),18688,10112);
        st64(base,device+32,0);
    }
}
template<bool Indexed> void draw(PPCContext& ctx,uint8_t* base){
    const uint64_t device_arg=ctx.r3.u64;
    const uint32_t device=ctx.r3.u32,primitive=ctx.r4.u32,first=ctx.r5.u32;
    uint32_t start=Indexed?ctx.r6.u32:first,count=Indexed?ctx.r7.u32:ctx.r6.u32;
    GuestFrame frame(ctx,base,Indexed?208:160);
    flush_state(ctx,base,device_arg);
    const uint32_t index_buffer=Indexed?ld32(base,device+12612):0;
    constexpr uint32_t primitive_table=0x8200C7A8;
    auto emit=[&](uint32_t& cursor,uint32_t value){cursor+=4;st32(base,cursor,value);};
    do{
        ctx.r3.u64=ld32(base,device+48);
        if(ctx.r3.u32>ld32(base,device+56)){ctx.r3.u64=device_arg;call_guest(ctx,base,sub_825744F8);}
        uint32_t cursor=ctx.r3.u32;
        emit(cursor,8450);emit(cursor,Indexed?first:start);ctx.r3.u64+=8;
        uint32_t batch=count;
        if(count>65535){const uint32_t unit=ld32(base,primitive_table+primitive*8);if(!unit)__builtin_trap();batch=((65535u/unit)&~1u)*unit;}
        const uint32_t mode=ld8(base,device+12363);
        const uint32_t flags=(ld8(base,device+12298+mode)|(mode?256u:0u))&319u;
        uint32_t word=(batch<<16)|(primitive&63u),address=0,size=0;
        if constexpr(Indexed){
            const uint32_t properties=ld32(base,index_buffer),buffer=ld32(base,index_buffer+24);
            const uint32_t high=(properties<<1)&0xC0000000u;
            address=physical(buffer+start*2);size=(batch&0xFFFFFFu)|high;
            if(properties&0x80000000u){word|=2048;address=physical(buffer+start*4);size=((batch<<1)&0xFFFFFEu)|high;}
        }else word|=128;
        constexpr uint32_t header=Indexed?0xC0032201u:0xC0012201u;
        auto packet=[&](){emit(cursor,header);emit(cursor,flags);emit(cursor,word);if constexpr(Indexed){emit(cursor,address);emit(cursor,size);}};
        if(!(ld8(base,device+10940)&1))packet();
        else{
            uint32_t record=ld32(base,device+13436);
            ctx.r3.u64=device_arg+13424;
            if(record+16>ld32(base,device+13440)){call_guest(ctx,base,sub_82573888);record=ctx.r3.u32;}
            st32(base,record,cursor);st32(base,device+13436,record+16);
            emit(cursor,0xC0006000);emit(cursor,0xFFFFFFFF);packet();
            emit(cursor,0xC0006000);emit(cursor,0x80000000);emit(cursor,0xC0015A01);emit(cursor,26);
            emit(cursor,physical(record+4,0x1FFFFFFEu)|1u);emit(cursor,0xC0004601);emit(cursor,25);
            if constexpr(!Indexed)ctx.r3.u64=25;
        }
        st32(base,device+48,cursor);
        if(count==batch)break;
        const uint32_t overlap=ld32(base,primitive_table+primitive*8+4);
        count=count-batch+overlap;start+=batch-overlap;
    }while(true);
}
}
NIER_DECOMP(sub_825718F8){draw<true>(ctx,base);}
NIER_DECOMP(sub_82571508){draw<false>(ctx,base);}
NIER_DECOMP(sub_82571070){
    const uint64_t argument=ctx.r3.u64;
    const uint32_t device=ctx.r3.u32,primitive=ctx.r4.u32,count=ctx.r5.u32,stride=ctx.r6.u32;
    GuestFrame frame(ctx,base,176);
    const uint32_t previous=ld8(base,device+12704),format=(stride>>2)&255u,words=(count*stride)>>2;
    st8(base,device+12704,uint8_t(format));
    if(format!=ld8(base,device+12008)&&ld32(base,device+12872))st64(base,device+16,ld64(base,device+16)|0x80000u);
    st64(base,device+24,ld64(base,device+24)&~1ull);
    flush_state(ctx,base,argument);
    uint32_t cursor=ld32(base,device+48);
    st8(base,device+12704,uint8_t(previous));
    if(cursor>ld32(base,device+56)){ctx.r3.u64=argument;call_guest(ctx,base,sub_825744F8);cursor=ctx.r3.u32;}
    ctx.r3.u64=argument;ctx.r4.u64=words;ctx.r5.u64=16;call_guest(ctx,base,sub_82573670);
    const uint64_t buffer_arg=ctx.r3.u64;
    const uint32_t buffer=uint32_t(buffer_arg);
    if(!buffer){st32(base,device+48,cursor);ctx.r3.u64=0;return;}
    auto emit=[&](uint32_t value){cursor+=4;st32(base,cursor,value);};
    emit(8450);emit(0);emit(0x548BA);emit(1);emit(0);emit(1);emit(0);
    emit(physical(buffer)|3u);emit(0x10000002u|((words<<2)&0x3FFFFFCu));
    emit(0x25000);emit(0);emit(0);emit(0);
    const uint32_t mode=ld8(base,device+12363);
    const uint32_t flags=(ld8(base,device+12298+mode)|(mode?256u:0u))&319u;
    const uint32_t draw_word=(count<<16)|(primitive&63u)|128u;
    if(!(ld8(base,device+10940)&1)){emit(0xC0012201);emit(flags);emit(draw_word);}
    else{
        uint32_t record=ld32(base,device+13436);
        ctx.r3.u64=argument+13424;
        if(record+16>ld32(base,device+13440)){call_guest(ctx,base,sub_82573888);record=ctx.r3.u32;}
        st32(base,record,cursor);st32(base,device+13436,record+16);
        emit(0xC0006000);emit(0xFFFFFFFF);emit(0xC0012201);emit(flags);emit(draw_word);
        emit(0xC0006000);emit(0x80000000);emit(0xC0015A01);emit(26);
        emit(physical(record+4,0x1FFFFFFEu)|1u);emit(0xC0004601);emit(25);
    }
    st64(base,device+24,ld64(base,device+24)|1u);
    st32(base,device+13652,cursor);st32(base,device+13656,buffer);st32(base,device+13664,words);
    ctx.r3.u64=buffer_arg;
}
#define DRAW_STATE "r3=const:0x40010000 r4=range:1..6 r5=range:0..1000 r6=range:0..60000 r7=range:0..60000 " \
    "@r3.0=const:0 @r3.4=const:0 @r3.8=const:0 @r3.12=const:0 @r3.16=const:0 @r3.20=const:0 " \
    "@r3.24=const:0 @r3.28=const:0 @r3.32=const:0 @r3.36=const:0 " \
    "@r3.48=const:0x40040000 @r3.52=const:0x40048000 @r3.56=const:0x40048000 " \
    "@r3.12612=const:0x40020000 @r3.0x10000=int @r3.0x10018=const:0x40030000 " \
    "@r3.10940=const:0 @r3.13436=const:0x40050000 @r3.13440=const:0x40058000"
NIER_VERIFY_SPEC(sub_825718F8,0x825718F8,DRAW_STATE);
NIER_VERIFY_SPEC(sub_82571508,0x82571508,DRAW_STATE);
#undef DRAW_STATE
NIER_VERIFY_SPEC(sub_82571070,0x82571070,
    "r3=const:0x40010000 r4=range:1..6 r5=range:0..1000 r6=range:4..64 "
    "@r3.0=const:0 @r3.4=const:0 @r3.8=const:0 @r3.12=const:0 @r3.16=const:0 @r3.20=const:0 "
    "@r3.24=const:0 @r3.28=const:0 @r3.32=const:0 @r3.36=const:0 @r3.12872=const:0 "
    "@r3.48=const:0x40040000 @r3.52=const:0x40048000 @r3.56=const:0x40048000 "
    "@r3.10940=const:0 @r3.13436=const:0x40050000 @r3.13440=const:0x40058000 @r3.0x60010=const:0 @r3.0x60014=const:0");
