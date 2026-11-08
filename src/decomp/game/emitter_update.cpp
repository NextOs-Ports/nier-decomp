// Atualização do emissor e de suas partículas: matrizes, ciclo de vida e emissão.
#include "particle_math.h"
#pragma clang fp contract(off)
using namespace nier::guest;
using namespace nier::particle;
#define EMITTER_CALLEE(name) DECLARE_REX_FUNC(sub_##name)
EMITTER_CALLEE(82107050); EMITTER_CALLEE(82180100); EMITTER_CALLEE(821805C0);
EMITTER_CALLEE(82180640); EMITTER_CALLEE(82101328); EMITTER_CALLEE(82180388);
EMITTER_CALLEE(8243FF78); EMITTER_CALLEE(82214578); EMITTER_CALLEE(82142828);
EMITTER_CALLEE(82141BC8); EMITTER_CALLEE(82145958); EMITTER_CALLEE(82144588);
EMITTER_CALLEE(82146EE8); EMITTER_CALLEE(82154AF0); EMITTER_CALLEE(82157250);
EMITTER_CALLEE(821452E0); EMITTER_CALLEE(8217D2B8);
#undef EMITTER_CALLEE
namespace {
void matrix_copy(uint8_t* base, uint32_t dst, uint32_t src) {
    for (int i = 0; i < 4; ++i) store(base, dst + i*16, load(base, src + i*16));
}
void matrix_transpose(uint8_t* base, uint32_t address) {
    uint32_t words[4][4];
    for(int row=0;row<4;++row)for(int col=0;col<4;++col)words[row][col]=ld32(base,address+row*16+col*4);
    for(int row=0;row<4;++row)for(int col=0;col<4;++col)st32(base,address+row*16+col*4,words[col][row]);
}
uint32_t next_random(uint8_t* base) {
    const uint32_t v=ld32(base,0x826908A0)*214013u+2531011u;
    st32(base,0x826908A0,v);return (v>>16)&32767u;
}
}
NIER_DECOMP(sub_82154D88) {
    const uint64_t emitter_arg=ctx.r3.u64;
    const uint32_t emitter=ctx.r3.u32, incoming=ctx.r5.u32;
    const double dt=ctx.f1.f64;
    GuestFrame frame(ctx,base,1168);
    auto s=[&](uint32_t a){return scalar(ctx,base,a);};
    auto call=[&](PPCFunc* fn,uint64_t a,uint64_t b){ctx.r3.u64=a;ctx.r4.u64=b;call_guest(ctx,base,fn);};
    auto call3=[&](PPCFunc* fn,uint64_t a,uint64_t b,uint64_t c){ctx.r3.u64=a;ctx.r4.u64=b;ctx.r5.u64=c;call_guest(ctx,base,fn);};
    auto birth=[&](uint32_t matrix,double age){ctx.f1.f64=age;call(sub_821452E0,emitter_arg,matrix);};
    matrix_copy(base,emitter+288,emitter+160);
    matrix_copy(base,emitter+160,incoming);
    const double one=s(0x820007A4),zero=s(0x820454E0);
    uint32_t definition=ld32(base,emitter+36);
    const int32_t animation=int32_t(ld32(base,definition+492));
    if(animation>=0){
        ctx.r3.u64=frame.slot(192);call_guest(ctx,base,sub_82107050);
        const uint32_t table=ld32(base,0x847B4F9C+ld32(base,emitter+68)*4);
        uint32_t track=0;
        if(animation<int32_t(ld32(base,table+4))){const uint32_t off=ld32(base,table+(uint32_t(animation)+3)*4);if(off)track=table+off;}
        call(sub_82180100,frame.slot(192),track);
        call3(sub_821805C0,frame.slot(304),frame.slot(192),0);
        ctx.f1.f64=mul(ctx,s(emitter+28),s(0x8200BE04));ctx.f2.f64=one;
        call(sub_82180640,frame.slot(304),frame.slot(352));
        call3(sub_82101328,frame.slot(112),emitter_arg+160,frame.slot(352));
        matrix_copy(base,emitter+160,frame.slot(112));
        ctx.r3.u64=frame.slot(304);call_guest(ctx,base,sub_82180388);
        ctx.r3.u64=frame.slot(192);call_guest(ctx,base,sub_8243FF78);
    }
    definition=ld32(base,emitter+36);
    if(ld32(base,definition+324)){
        double duration=s(definition+8);if(duration<zero)duration=-duration;
        ctx.fpscr.disableFlushMode();
        const double phase=mul(ctx,float(s(emitter+28)/duration),s(0x8200EDE8));
        const uint32_t index=fctiwz(phase);
        const double fraction=sub(ctx,phase,float(int32_t(index)));
        Vector rotation=scale(ctx,scale(ctx,load(base,definition+352),dt),one);
        ctx.fpscr.enableFlushMode();rotation=simde_mm_mul_ps(rotation,load(base,0x8200EF40));
        const int32_t track=int32_t(ld32(base,definition+436));
        double factor=one;
        if(track>=0){ctx.f1.f64=fraction;call(sub_82214578,uint64_t(ld32(base,emitter+48))+uint64_t(track)*132,index);factor=ctx.f1.f64;}
        store(base,emitter+80,advance(ctx,load(base,emitter+80),rotation,factor));
        ctx.f1.f64=fraction;call(sub_82142828,emitter_arg,index);
        call3(sub_82101328,frame.slot(112),emitter_arg+160,emitter_arg+96);
        matrix_copy(base,emitter+160,frame.slot(112));
    }
    if(ld8(base,emitter+23)){
        matrix_copy(base,emitter+224,emitter+160);
        store(base,emitter+272,replace<3>(ctx,simde_mm_setzero_ps(),one));
        matrix_transpose(base,emitter+224);
        call(sub_82141BC8,frame.slot(112),emitter_arg+224);
        matrix_copy(base,emitter+224,ctx.r3.u32);
    }
    definition=ld32(base,emitter+36);
    if(!(s(emitter+28)<s(definition+24))){ctx.r3.u64=emitter_arg;call_guest(ctx,base,sub_82145958);}
    definition=ld32(base,emitter+36);
    uint32_t behavior=ld32(base,emitter+40);
    const uint32_t mode=ld32(base,behavior);
    const bool simulate=ld32(base,definition+324)!=1||mode==2||mode==5;
    uint32_t node=ld32(base,emitter+20832);
    if(!(s(emitter+28)<s(definition+24))){
        while(node){
            const uint32_t particle=node;st32(base,frame.slot(96),particle);node=ld32(base,particle+144);
            stf(base,particle+96,add(ctx,s(particle+96),dt));
            if(simulate){ctx.f1.f64=dt;call(sub_82144588,emitter_arg,particle);}
            behavior=ld32(base,emitter+40);
            if(!(s(particle+96)<s(particle+100))||ld32(base,behavior+64)){
                definition=ld32(base,emitter+36);
                if(s(definition+16)<zero)stf(base,particle+96,zero);
                else{
                    if(ld32(base,particle+124)){call3(sub_82146EE8,0x84694D00,particle+124,0);st32(base,particle+124,0);}
                    if((ld8(base,particle+130)&0x18)&&particle!=16){const uint32_t object=ld32(base,particle+144);if(object){ctx.r3.u64=object;ctx.r4.u64=0;call_virtual(ctx,base,object,3);}}
                    call(sub_82154AF0,emitter_arg+352,frame.slot(96));
                }
            }
        }
    }
    definition=ld32(base,emitter+36);
    const double age=add(ctx,s(emitter+28),dt);stf(base,emitter+28,age);
    const double duration=s(definition+8);
    if(!(age<add(ctx,s(definition+24),duration))&&duration>zero){ctx.r3.u64=0;return;}
    behavior=ld32(base,emitter+40);
    auto interval=[&](){
        auto draw=[&](){const uint32_t random=next_random(base);const uint32_t d=ld32(base,emitter+36);const double range=s(d+44);return add(ctx,madd(ctx,mul(ctx,double(random),sub(ctx,range,-range)),s(0x8200072C),-range),s(d+40));};
        const double candidate=draw(),minimum=s(0x820007D0);
        return candidate<minimum?minimum:draw();
    };
    if(ld32(base,behavior+64)){
        const int32_t count=std::max(1,int32_t(ld32(base,behavior+68)));
        const double total=s(behavior+72);ctx.fpscr.disableFlushMode();
        const double step=float(total/float(count));
        const uint32_t owner=ld32(base,emitter+44),buffer=ld32(base,owner+116);
        const int32_t capacity=int32_t(ld32(base,owner+120));
        if(!capacity)__builtin_trap();
        const int32_t first=int32_t(ld32(base,owner+128)-ld32(base,owner+136)+uint32_t(capacity));
        auto ring=[&](uint32_t i){const int32_t index=int32_t(uint32_t(first)+i);return buffer+uint32_t(int64_t(index)%capacity)*16;};
        for(uint32_t i=0;i<34;++i)store(base,frame.slot(960-i*16),load(base,ring(i)));
        store(base,frame.slot(416),load(base,ring(33)));
        matrix_copy(base,frame.slot(240),emitter+160);
        double time=sub(ctx,total,step);
        const double unit=s(0x8200EF3C),rate=s(0x82000B14);
        for(int32_t remaining=count-1;remaining>=0;--remaining){
            int32_t sample=int32_t(fctiwz(mul(ctx,time,rate)));if(sample>=30)sample=30;
            ctx.fpscr.disableFlushMode();
            const double fraction=mul(ctx,float(-std::fma(double(float(sample)),unit,-time)),rate);
            ctx.f1.f64=fraction;call3(sub_82157250,frame.slot(176),emitter_arg,frame.slot(416+uint32_t(sample)*16));
            store(base,frame.slot(288),replace<3>(ctx,load(base,frame.slot(176)),one));
            definition=ld32(base,emitter+36);ctx.fpscr.disableFlushMode();
            const double offset=float(mul(ctx,s(definition+16),float(remaining))/float(count));
            birth(frame.slot(240),offset>=zero?offset:zero);time=sub(ctx,time,step);
        }
    }else{
        const double next=s(emitter+32);
        if(next>zero&&age>next&&ld8(base,emitter+25)){
            definition=ld32(base,emitter+36);
            if(!ld32(base,definition+48)){const double value=interval();stf(base,emitter+32,add(ctx,value,s(emitter+32)));birth(emitter+160,zero);}
            else{
                const double lag=sub(ctx,age,next);ctx.fpscr.disableFlushMode();
                int32_t count=int32_t(fctiwz(float(lag/s(definition+40))));if(count>16)count=16;
                if(count<=1)birth(emitter+160,zero);
                else for(int32_t i=0;i<count;++i){ctx.fpscr.disableFlushMode();const double fraction=float(float(i)/float(count-1));ctx.f1.f64=fraction;call3(sub_8217D2B8,frame.slot(112),emitter_arg+288,emitter_arg+160);birth(frame.slot(112),mul(ctx,fraction,lag));}
                const double value=interval();stf(base,emitter+32,add(ctx,value,s(emitter+28)));
            }
        }
    }
    ctx.r3.u64=1;
}
NIER_VERIFY_SPEC(sub_82154D88,0x82154D88,
    "r3=const:0x40000000 r5=const:0x40060000 f1=f32 callargs=2 "
    "@r3.36=const:0x40010000 @r3.40=const:0x40020000 @r3.48=const:0x40030000 "
    "@r3.20=const:0 @r3.24=const:0 @r3.28=const:0x3f800000 "
    "@r3.0x101ec=const:0xffffffff @r3.0x10144=const:0 @r3.0x10140=const:0 "
    "@r3.0x10018=const:0 @r3.0x10008=const:0 @r3.0x10010=const:0xbf800000 "
    "@r3.0x20040=const:0 @r3.0x5160=const:0x40040000 "
    "@r3.0x40090=const:0 @r3.0x4007c=const:0 "
    "@r3.0x40060=const:0x3f800000 @r3.0x40064=const:0x447a0000 "
    "@r3.0x40080=range:0..65535 "
    "@r3.0x10198=range:0..1 @r3.0x101a8=const:0xffffffff "
    "@r3.0x101c4=const:0xffffffff @r3.0x101c8=const:0xffffffff "
    "@r3.0x20008=range:0xfffffffe..5 @r3.0x2000c=range:0..1 "
    "@r3.0x200b0=const:0xfffffffe @r3.0x200b4=const:0xffffffff "
    "@r3.0x200b8=const:0xffffffff @r3.0x200bc=const:0xffffffff");
