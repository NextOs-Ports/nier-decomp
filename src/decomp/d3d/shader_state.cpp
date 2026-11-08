// Sincronização do estado VS/PS e emissão de pacotes PM4, em inteiros do host.
#include <nier/decomp.h>
using namespace nier::guest;
DECLARE_REX_FUNC(sub_82577AF0); DECLARE_REX_FUNC(sub_825744F8);
DECLARE_REX_FUNC(sub_82578FB8); DECLARE_REX_FUNC(sub_82579100);
DECLARE_REX_FUNC(sub_82578E68); DECLARE_REX_FUNC(__imp__KeBugCheck);
namespace {
uint32_t gpu_address(uint32_t address, uint32_t mask=0x1FFFFFFFu) {
    return (address&mask)+(((address>>20)+512u)&0x1000u);
}
}
NIER_DECOMP(sub_82579240) {
    const uint64_t device_arg=ctx.r3.u64;
    const uint32_t device=ctx.r3.u32;
    uint64_t dirty=ctx.r4.u64;
    GuestFrame frame(ctx,base,272);
    for(uint32_t off:{84u,88u,92u,100u,104u})st32(base,frame.slot(off),0xFFFFFFFFu);
    st32(base,frame.slot(96),0);
    auto call=[&](PPCFunc* fn){call_guest(ctx,base,fn);};
    auto reserve=[&](){
        ctx.r3.u64=ld32(base,device+48);
        if(ctx.r3.u32>ld32(base,device+56)){ctx.r3.u64=device_arg;call(sub_825744F8);}
        return ctx.r3.u32;
    };
    auto emit=[&](uint32_t& cursor,uint32_t value){cursor+=4;st32(base,cursor,value);};
    if(dirty&0x20000u){ctx.r3.u64=device_arg;ctx.r4.u64=dirty;call(sub_82577AF0);dirty=ctx.r3.u64;}
    const uint32_t vs=ld32(base,device+12872),layout=ld32(base,device+11992),ps=ld32(base,device+12868);
    if(!vs){ctx.r3.u64=dirty;return;}
    uint32_t vs_desc=vs+ld32(base,vs+896)+872,ps_desc=0,ps_mask=0,ps_original=0,ps_high=0,variant=0;
    bool mismatch=false;
    if(!ps){
        if(ld8(base,device+10943)&2){
            uint32_t cursor=reserve();emit(cursor,1480);emit(cursor,0x20000);
            st32(base,device+48,cursor);st8(base,device+10943,ld8(base,device+10943)&~3u);
        }
        if(ld32(base,vs+872)&0x20){variant=1;vs_desc=vs+ld32(base,vs+904)+872;}
        uint32_t mode=ld32(base,device+10580);
        if((mode&7)!=5){
            mode=(mode&~7u)|5;st32(base,device+10580,mode);dirty|=0x80008;
            if(ld8(base,device+10943)&8){st32(base,device+10372,ld32(base,device+10372)&~0xF0000u);dirty|=1ull<<56;}
        }
        mismatch=(ld32(base,vs_desc+20)&31)!=0;
    }else{
        ps_desc=ps+ld32(base,ps+64)+40;
        ps_mask=ps_original=ld32(base,ps_desc+8);ps_high=ld32(base,ps_desc+12);
        const bool extended=(ps_original&0x70000000u)>0x10000000u;
        st8(base,device+10942,ld8(base,device+10942)|(extended?8:0));
        st8(base,device+10943,ld8(base,device+10943)|(extended?2:0));
        if(dirty&0x100000){
            ctx.r3.u64=device_arg;ctx.r4.u64=ps+40;ctx.r5.u64=ld32(base,ps+24);call(sub_82578FB8);
            uint32_t cursor=reserve();emit(cursor,0xC0012700);dirty|=0x4000;
            const uint32_t descriptor=ps+ld32(base,ps+64);
            emit(cursor,gpu_address(ld32(base,descriptor+40)+ld32(base,ps+24),0x1FFFFFFEu)|1u);
            emit(cursor,ld32(base,descriptor+44)>>2);st32(base,device+48,cursor);
            uint32_t mode=ld32(base,device+10580);
            st32(base,device+10536,ld32(base,ps_desc+24));
            if((mode&7)!=4){
                st32(base,device+10580,(mode&~7u)|4u);dirty|=0x80008;
                if(ld8(base,device+10943)&8){dirty|=1ull<<56;st32(base,device+10372,ld32(base,ld32(base,device+12616)+28));}
            }
        }
        const uint32_t ps_fields=ld32(base,ps_desc+20),vs_fields=ld32(base,vs_desc+20);
        uint32_t flags=ld32(base,0x85F4C818),mask;
        if(!(flags&1)){st32(base,0x85F4C818,flags|1u);mask=0x603FF;st32(base,0x85F4C814,mask);}
        else mask=ld32(base,0x85F4C814);
        mismatch=(ps_fields&mask)!=(vs_fields&mask);
    }
    if(!ld32(base,device+13712)){
        if(ps&&(ld32(base,ps+40)&0x10)){ctx.r3.u64=245;call(__imp__KeBugCheck);}
        if(ld32(base,vs+872)&0x10){ctx.r3.u64=245;call(__imp__KeBugCheck);}
    }
    const uint32_t vs_low=ld32(base,vs_desc+8),vs_high=ld32(base,vs_desc+12);
    st32(base,frame.slot(80),vs_low);
    const uint32_t old_mode=ld32(base,device+10580);
    uint32_t mode=old_mode;
    int32_t cached_variant=-1;
    const bool full=(ld8(base,device+10940)&0x40)!=0;
    if(full){
        if(!ps_desc)mode=(mode&~7u)|5u;
        else if(!(ld32(base,device+10556)&8)&&!(ld32(base,ps_desc+28)&0x30)){
            ps_mask=0;const uint8_t flags=ld8(base,device+10943);
            st8(base,device+10943,flags&~1u);st32(base,frame.slot(96),flags&1u);mode=(mode&~7u)|5u;
        }
        if(dirty&0x40000){st32(base,frame.slot(84),vs_low|ps_mask);st32(base,frame.slot(88),vs_high|ps_high);}
        if(mode!=ld32(base,device+12876)||(dirty&8)){
            dirty|=8;st32(base,device+12876,mode);st32(base,frame.slot(100),mode);
            if(ld8(base,device+10943)&8){uint32_t value=ld32(base,device+10372);if((mode&7)==5)value&=~0xF0000u;st32(base,frame.slot(104),value);}
        }
    }
    st8(base,device+10943,ld8(base,device+10943)|((ps_mask&0x70000000u)>0x10000000u?1u:0u));
    auto upload_vs=[&](){ctx.r3.u64=device_arg;ctx.r4.u64=vs+872;ctx.r5.u64=ld32(base,vs+32);call(sub_82578FB8);};
    auto prepare=[&](uint32_t force){
        ctx.r3.u64=device_arg;ctx.r4.u64=force;ctx.r5.u64=vs;ctx.r6.u64=frame.slot(80);
        ctx.r7.u64=layout;ctx.r8.u64=vs_desc;ctx.r9.u64=ps_desc;ctx.r10.u64=variant;call(sub_82578E68);
    };
    auto cached=[&](uint32_t which){ctx.r3.u64=device_arg;ctx.r4.u64=vs;ctx.r5.u64=layout;ctx.r6.u64=which;call(sub_82579100);return ctx.r3.s32!=0;};
    auto alternate=[&](){st32(base,frame.slot(92),1);st8(base,device+10942,ld8(base,device+10942)|0x40);};
    if(!(ld8(base,device+10942)&0x80)&&!mismatch){
        if(dirty&0x80000){
            upload_vs();st8(base,device+10942,ld8(base,device+10942)&~0x40u);
            if(layout&&!(ld32(base,vs+872)&0x40)){
                if(ld8(base,device+10940)&0x80)prepare(0);
                else{
                    if(cached(variant))cached_variant=int32_t(variant);else prepare(0);
                    if(((mode^old_mode)&7)&&(ld32(base,vs+872)&0x20)&&cached(1))alternate();
                }
            }else{
                cached_variant=int32_t(variant);
                if(((mode^old_mode)&7)&&(ld32(base,vs+872)&0x20))alternate();
            }
        }
    }else if(dirty&0x180000){
        st8(base,device+10942,ld8(base,device+10942)&~0x40u);upload_vs();prepare(mismatch?1u:0u);
    }
    if(dirty&0x180000){
        st32(base,device+10532,vs_high|ps_high);st32(base,device+10528,ld32(base,frame.slot(80))|ps_original);dirty|=0x18000;
        if(ld8(base,device+10942)&0x40){
            const uint32_t desc=vs+ld32(base,vs+904);
            st32(base,frame.slot(84),ld32(base,desc+880)|ps_mask);st32(base,frame.slot(88),ld32(base,desc+884)|ps_high);
        }else if(!ps_mask&&ps_original){st32(base,frame.slot(84),ld32(base,frame.slot(80)));st32(base,frame.slot(88),vs_high);}
    }
    auto shader_packet=[&](uint32_t& cursor,uint32_t which,uint32_t header){
        emit(cursor,header);
        const uint32_t desc=vs+ld32(base,vs+(which+112u)*8u);
        emit(cursor,gpu_address(ld32(base,desc+872)+ld32(base,vs+32)));
        emit(cursor,ld32(base,desc+876)>>2);st32(base,vs+which*416u+64,ld32(base,device+10908));
    };
    if(ld8(base,device+10940)&0x40){
        uint32_t cursor=reserve();emit(cursor,0xC0006000);emit(cursor,0x15555555);emit(cursor,0xC0006100);emit(cursor,0);
        if(dirty&8){emit(cursor,8712);emit(cursor,ld32(base,device+10580));dirty&=~8ull;}
        if(ld32(base,frame.slot(96))){emit(cursor,0xC0002601);emit(cursor,0);}
        uint32_t value=ld32(base,frame.slot(100));
        if(value!=0xFFFFFFFFu){emit(cursor,0xC0012D01);emit(cursor,0x40208);emit(cursor,value);}
        if(dirty&(1ull<<56)){emit(cursor,8193);emit(cursor,ld32(base,device+10372));dirty&=~(1ull<<56);}
        value=ld32(base,frame.slot(104));
        if(value!=0xFFFFFFFFu){emit(cursor,0xC0012D01);emit(cursor,0x40001);emit(cursor,value);}
        if(dirty&0x10000){emit(cursor,0x12180);emit(cursor,ld32(base,device+10528));emit(cursor,ld32(base,device+10532));dirty&=~0x18000ull;}
        value=ld32(base,frame.slot(84));
        if(value!=0xFFFFFFFFu){emit(cursor,0xC0022D01);emit(cursor,0x40180);emit(cursor,value);emit(cursor,ld32(base,frame.slot(88)));}
        const uint32_t next_variant=ld32(base,frame.slot(92));
        if(next_variant!=0xFFFFFFFFu){
            shader_packet(cursor,next_variant,0xC0012701);
            if(cached_variant!=-1){emit(cursor,0xC0006000);emit(cursor,0x2AAAAAAA);shader_packet(cursor,uint32_t(cached_variant),0xC0012701);}
        }else if(cached_variant!=-1)shader_packet(cursor,uint32_t(cached_variant),0xC0012700);
        emit(cursor,0xC0006000);emit(cursor,ld32(base,device+12884));emit(cursor,0xC0006100);emit(cursor,ld32(base,device+12888));
        st32(base,device+48,cursor);
    }else if(cached_variant!=-1){uint32_t cursor=reserve();shader_packet(cursor,uint32_t(cached_variant),0xC0012700);st32(base,device+48,cursor);}
    ctx.r3.u64=dirty;
}
NIER_VERIFY_SPEC(sub_82579240,0x82579240,
    "r3=const:0x40010000 r4=const:0x180000 "
    "@r3.12872=const:0x40020000 @r3.12868=const:0x40030000 @r3.11992=const:0 "
    "@r3.48=const:0x40050000 @r3.52=const:0x40058000 @r3.56=const:0x40058000 "
    "@r3.10940=const:0x40000000 @r3.13712=const:1 "
    "@r3.0x10380=const:0 @r3.0x10388=const:0 @r3.0x10368=const:0 "
    "@r3.0x1037c=const:0 @r3.0x20040=const:0 @r3.0x2003c=const:0 "
    "@r3.0x20030=const:0 @r3.0x20034=const:0 @r3.0x20044=const:0");
