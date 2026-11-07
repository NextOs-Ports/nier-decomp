// End-to-end headless keyboard import, guest UTF-16 and deferred overlapped.
#include <rex/runtime.h>
#include <rex/kernel/init.h>
#include <rex/system/xthread.h>
#include <rex/system/xio.h>
#include <rex/ppc/context.h>
#include <rex/cvar.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
REXCVAR_DECLARE(bool,headless);
REXCVAR_DECLARE(std::string,default_keyboard_text);
extern "C" void __imp__XamShowKeyboardUI(PPCContext&,uint8_t*);

int main(int argc,char** argv) {
    if(argc!=3) return 2;
    rex::Runtime runtime(argv[1],argv[2]);
    if(runtime.Setup({.kernel_init=rex::kernel::InitializeKernel,.tool_mode=true})!=0 ||
       runtime.LoadXexImage("game:\\default.xex")!=0) return 3;
    auto* memory=runtime.memory();
    const uint32_t storage=memory->SystemHeapAlloc(4096);
    if(!storage) return 4;
    auto* host=memory->TranslateVirtual(storage);
    int failed=0,passed=0;
    auto test=[&](const char* label,bool headless,const std::u16string& supplied,bool null_input,
                  const std::string& fallback,uint32_t capacity,const std::u16string& expected) {
        REXCVAR_SET(headless,headless);REXCVAR_SET(default_keyboard_text,fallback);
        std::memset(host,0,4096);std::memset(host+0x100,0x7e,128);
        for(size_t i=0;i<supplied.size();++i) {
            host[2*i]=uint8_t(supplied[i]>>8);host[2*i+1]=uint8_t(supplied[i]);
        }
        PPCContext ctx{};
        ctx.r3.u64=0;ctx.r4.u64=0;ctx.r5.u64=null_input?0:storage;
        ctx.r6.u64=0;ctx.r7.u64=0;ctx.r8.u64=storage+0x100;
        ctx.r9.u64=capacity;ctx.r10.u64=storage+0x300;
        __imp__XamShowKeyboardUI(ctx,runtime.virtual_membase());
        bool ok;
        if(!capacity) ok=ctx.r3.u32==87 && host[0x100]==0x7e;
        else {
            ok=ctx.r3.u32==997; // X_ERROR_IO_PENDING, completed by Kernel Dispatch.
            auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
            while(__builtin_bswap32(*reinterpret_cast<volatile uint32_t*>(host+0x300))==997 &&
                  std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            ok=ok && rex::system::XOverlappedGetResult(host+0x300)==0 &&
               __builtin_bswap32(*reinterpret_cast<uint32_t*>(host+0x318))==0;
            std::u16string actual;
            for(uint32_t i=0;i<capacity;++i) {
                char16_t value=char16_t(host[0x100+i*2]<<8 | host[0x101+i*2]);
                if(!value)break;actual.push_back(value);
            }
            ok=ok && actual==expected && host[0x100+capacity*2]==0x7e;
        }
        std::printf("%s %s\n",ok?"PASS":"FAIL",label);std::fflush(stdout);
        if(ok)++passed;else++failed;
    };
    rex::system::object_ref<rex::system::XHostThread> worker(new rex::system::XHostThread(runtime.kernel_state(),128*1024,0,[&] {
        test("empty default -> Nier",true,u"",false,"Nier",16,u"Nier");
        test("no drawer fallback + UTF16 accent",false,u"",false,"Kaíne",16,u"Kaíne");
        test("nonempty default preserved",true,u"Emil",false,"Nier",16,u"Emil");
        test("null default + non-BMP",true,u"",true,"João🙂",16,u"João🙂");
        test("truncate without half surrogate",true,u"",true,"João🙂",6,u"João");
        test("empty configured fallback still usable",true,u"",true,"",16,u"Nier");
        test("small buffer bounded",true,u"",false,"Nier",2,u"N");
        test("zero capacity rejected",true,u"",false,"Nier",0,u"");
        return uint32_t(0);
    }));
    if(worker->Create()!=0) return 5;
    worker->Wait(0,0,0,nullptr);
    std::printf("keyboard cases=%d failed=%d\n",passed+failed,failed);
    return failed?1:0;
}
