// Real deferred XContent create/open + NT file write/read/close, in an XHostThread.
#include <rex/runtime.h>
#include <rex/kernel/init.h>
#include <rex/system/xthread.h>
#include <rex/system/xio.h>
#include <rex/system/xam/content_manager.h>
#include <rex/ppc/context.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
using namespace rex;
#define IMPORT(n) extern "C" void __imp__##n(PPCContext&,uint8_t*)
IMPORT(XamContentCreateEx);IMPORT(XamContentClose);IMPORT(NtCreateFile);IMPORT(NtWriteFile);IMPORT(NtReadFile);IMPORT(NtFlushBuffersFile);IMPORT(NtClose);
int main(int argc,char** argv) {
    if(argc!=4)return 2;const bool create=std::string(argv[3])=="create";
    Runtime runtime(argv[1],argv[2]);
    if(runtime.Setup({.kernel_init=kernel::InitializeKernel,.tool_mode=true})!=0 || runtime.LoadXexImage("game:\\default.xex")!=0)return 3;
    auto* memory=runtime.memory();const uint32_t storage=memory->SystemHeapAlloc(16384);if(!storage)return 4;
    auto* host=memory->TranslateVirtual(storage);auto* base=runtime.virtual_membase();int failed=0;
    auto word=[&](unsigned offset){uint32_t v;std::memcpy(&v,host+offset,4);return __builtin_bswap32(v);};
    auto store=[&](unsigned offset,uint32_t v){v=__builtin_bswap32(v);std::memcpy(host+offset,&v,4);};
    auto check=[&](bool ok,const char* label,uint32_t status=0){std::printf("%s %s status=%08X\n",ok?"PASS":"FAIL",label,status);std::fflush(stdout);if(!ok)++failed;return ok;};
    system::object_ref<system::XHostThread> worker(new system::XHostThread(runtime.kernel_state(),128*1024,0,[&] {
        std::memset(host,0,16384);std::strcpy(reinterpret_cast<char*>(host),"c6save");
        auto* data=reinterpret_cast<system::xam::XCONTENT_DATA*>(host+0x100);
        data->device_id=1;data->content_type=system::XContentType::kSavedGame;
        data->set_file_name("SYSTEM_DATA");data->set_display_name(u"NieR synthetic save creation proof");
        PPCContext c{};c.r1.u64=storage+0x3000;c.r3.u64=0;c.r4.u64=storage;c.r5.u64=storage+0x100;
        c.r6.u64=create?1:3;c.r7.u64=storage+0x300;c.r8.u64=storage+0x304;c.r9.u64=0;c.r10.u64=4096;
        store(0x3054,storage+0x400);__imp__XamContentCreateEx(c,base);
        if(!check(c.r3.u32==997,"XamContentCreateEx pending",c.r3.u32))return uint32_t(1);
        const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        while(__builtin_bswap32(*reinterpret_cast<volatile uint32_t*>(host+0x400))==997 && std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if(!check(word(0x400)==0 && word(0x418)==0 && word(0x300)==(create?1u:2u),"XContent overlapped completed",word(0x400)))return uint32_t(1);
        const char path[]="c6save:\\SYSTEM_DATA";std::memcpy(host+0x700,path,sizeof path);
        auto* name=reinterpret_cast<system::X_ANSI_STRING*>(host+0x600);name->length=sizeof(path)-1;name->maximum_length=sizeof path;name->pointer=storage+0x700;
        auto* attrs=reinterpret_cast<system::X_OBJECT_ATTRIBUTES*>(host+0x620);attrs->root_directory=0;attrs->name_ptr=storage+0x600;attrs->attributes=0x40;
        c={};c.r1.u64=storage+0x3000;c.r3.u64=storage+0x650;c.r4.u64=0xc0100000;c.r5.u64=storage+0x620;c.r6.u64=storage+0x660;
        c.r7.u64=0;c.r8.u64=0x80;c.r9.u64=7;c.r10.u64=create?2:1;store(0x3054,0x60);__imp__NtCreateFile(c,base);
        if(!check(c.r3.u32==0,"NtCreateFile",c.r3.u32))return uint32_t(1);
        const uint32_t handle=word(0x650);const char payload[]="nier-create-new-file-completed-v1\n";
        if(create) {
            std::memcpy(host+0x800,payload,sizeof payload);
            c={};c.r3.u64=handle;c.r7.u64=storage+0x660;c.r8.u64=storage+0x800;c.r9.u64=sizeof payload;c.r10.u64=storage+0x900;
            __imp__NtWriteFile(c,base);if(!check(c.r3.u32==0 && word(0x664)==sizeof payload,"NtWriteFile",c.r3.u32))return uint32_t(1);
            c={};c.r3.u64=handle;c.r4.u64=storage+0x660;__imp__NtFlushBuffersFile(c,base);check(c.r3.u32==0,"NtFlushBuffersFile",c.r3.u32);
        }
        c={};c.r3.u64=handle;c.r7.u64=storage+0x660;c.r8.u64=storage+0xa00;c.r9.u64=sizeof payload;c.r10.u64=storage+0x900;
        __imp__NtReadFile(c,base);check(c.r3.u32==0 && word(0x664)==sizeof payload && std::memcmp(host+0xa00,payload,sizeof payload)==0,"NtReadFile payload",c.r3.u32);
        c={};c.r3.u64=handle;__imp__NtClose(c,base);check(c.r3.u32==0,"NtClose",c.r3.u32);
        c={};c.r3.u64=storage;c.r4.u64=storage+0x480;__imp__XamContentClose(c,base);
        check(c.r3.u32==997 && word(0x480)==0 && word(0x498)==0,"XamContentClose completed",c.r3.u32);
        std::printf("SAVE_ROOT %s\n",runtime.user_data_root().string().c_str());return uint32_t(0);
    }));
    if(worker->Create()!=0)return 5;worker->Wait(0,0,0,nullptr);
    std::printf("save %s failed=%d\n",create?"create":"reopen",failed);return failed?1:0;
}
