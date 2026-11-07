// Real XAM profile imports plus the runtime's actual device-assignment policy.
#include <rex/runtime.h>
#include <rex/kernel/init.h>
#include <rex/system/xthread.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xam/user_profile.h>
#include <rex/system/xam/content_manager.h>
#include <rex/input/input_system.h>
#include <rex/input/input_driver.h>
#include <rex/ppc/context.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
using namespace rex;
extern "C" void __imp__XamUserGetSigninState(PPCContext&,uint8_t*);
extern "C" void __imp__XamUserGetXUID(PPCContext&,uint8_t*);
extern "C" void __imp__XamUserGetName(PPCContext&,uint8_t*);
extern "C" void __imp__XamUserGetGamerTag(PPCContext&,uint8_t*);
extern "C" void __imp__XamUserGetSigninInfo(PPCContext&,uint8_t*);
class FixturePads final:public input::InputDriver {
    unsigned count_;
public:
    explicit FixturePads(unsigned count):InputDriver(nullptr,0),count_(count){}
    X_STATUS Setup() override{return X_STATUS_SUCCESS;}
    void EnumerateDevices(std::vector<input::DeviceInfo>& out) override {
        for(unsigned i=0;i<count_;++i) {input::DeviceInfo d;d.id=static_cast<input::DeviceId>(0x600001+i);
            d.name=i?"Fixture scripted SDL controller":"Fixture first SDL controller";d.synthetic=false;out.push_back(d);}
    }
    X_RESULT GetDeviceState(input::DeviceId id,input::X_INPUT_STATE* state) override {
        std::memset(state,0,sizeof(*state));if(uint64_t(id)==0x600000+count_)state->gamepad.buttons=0x10;return X_ERROR_SUCCESS;
    }
    X_RESULT GetDeviceCapabilities(input::DeviceId,uint32_t,input::X_INPUT_CAPABILITIES* caps) override {std::memset(caps,0,sizeof(*caps));return X_ERROR_SUCCESS;}
    X_RESULT SetDeviceVibration(input::DeviceId,input::X_INPUT_VIBRATION*) override{return X_ERROR_SUCCESS;}
    X_RESULT GetDeviceKeystroke(input::DeviceId,uint32_t,input::X_INPUT_KEYSTROKE*) override{return X_ERROR_EMPTY;}
};
int main(int argc,char** argv) {
    if(argc!=6)return 2;
    const unsigned pads=std::strtoul(argv[3],nullptr,10),expected=std::strtoul(argv[4],nullptr,10);
    if(pads<1 || pads>2 || expected>1)return 2;
    Runtime runtime(argv[1],argv[2]);RuntimeConfig config;
    config.kernel_init=kernel::InitializeKernel;config.tool_mode=true;
    config.input_factory=[&](bool) {auto input=input::CreateDefaultInputSystem(true);input->AddDriver(std::make_unique<FixturePads>(pads));return input;};
    if(runtime.Setup(std::move(config))!=0 || runtime.LoadXexImage("game:\\default.xex")!=0)return 3;
    const uint32_t storage=runtime.memory()->SystemHeapAlloc(4096);if(!storage)return 4;
    auto* memory=runtime.memory()->TranslateVirtual(storage);auto* base=runtime.virtual_membase();int failed=0;
    auto check=[&](bool ok,const char* label){std::printf("%s %s\n",ok?"PASS":"FAIL",label);if(!ok)++failed;};
    auto be32=[](const uint8_t* p){uint32_t v;std::memcpy(&v,p,4);return __builtin_bswap32(v);};
    auto be64=[](const uint8_t* p){uint64_t v;std::memcpy(&v,p,8);return __builtin_bswap64(v);};
    system::object_ref<system::XHostThread> worker(new system::XHostThread(runtime.kernel_state(),128*1024,0,[&] {
        for(unsigned i=0;i<4;++i) {PPCContext c{};c.r3.u64=i;__imp__XamUserGetSigninState(c,base);
            std::printf("SIGNIN user=%u state=%u\n",i,c.r3.u32);check(c.r3.u32==(i?0u:1u),"local signin state");}
        std::memset(memory,0,4096);PPCContext c{};c.r3.u64=0;c.r4.u64=7;c.r5.u64=storage;
        __imp__XamUserGetXUID(c,base);check(c.r3.u32==0 && be64(memory)==0xB13EBABEBABEBABEull,"PC XUID");
        c={};c.r3.u64=0;c.r4.u64=storage+16;c.r5.u64=16;__imp__XamUserGetName(c,base);
        check(c.r3.u32==0 && std::strcmp(reinterpret_cast<char*>(memory+16),"User")==0,"PC gamertag");
        c={};c.r3.u64=0;c.r4.u64=0;c.r5.u64=storage+64;__imp__XamUserGetSigninInfo(c,base);
        check(c.r3.u32==0 && be64(memory+64)==0xB13EBABEBABEBABEull && be32(memory+76)==1 &&
            std::strcmp(reinterpret_cast<char*>(memory+88),"User")==0,"signin info identity/state");
        c={};c.r3.u64=0;c.r4.u64=storage+128;c.r5.u64=16;__imp__XamUserGetGamerTag(c,base);
        const uint8_t tag[]{0,'U',0,'s',0,'e',0,'r',0,0};
        check(c.r3.u32==0 && std::memcmp(memory+128,tag,sizeof tag)==0,"UTF16BE gamertag");
        unsigned active=99;
        for(unsigned i=0;i<4;++i) {input::X_INPUT_STATE state{};
            if(static_cast<input::InputSystem*>(runtime.input_system())->GetState(i,&state)==0 && (uint16_t(state.gamepad.buttons)&0x10))active=i;}
        c={};c.r3.u64=active;__imp__XamUserGetSigninState(c,base);
        std::printf("INPUT pads=%u active_user=%u active_signin=%u\n",pads,active,c.r3.u32);
        check(active==expected && c.r3.u32==(expected?0u:1u),"controller/profile binding");
        using Profile=system::xam::UserProfile;
        const std::string marker="NIER-C5-LOCAL-PROFILE";std::vector<uint8_t> payload(marker.begin(),marker.end());
        auto* profile=runtime.kernel_state()->user_profile();
        if(std::string(argv[5])=="write")profile->AddSetting(std::make_unique<Profile::BinarySetting>(0x63E83FFD,payload));
        auto* saved=static_cast<Profile::BinarySetting*>(profile->GetSetting(0x63E83FFD));
        check(saved && saved->value==payload,"persistent profile setting");
        std::printf("PROFILE xuid=%016llX gamertag=%s path=%s\n",(unsigned long long)profile->xuid(),profile->name().c_str(),
            runtime.kernel_state()->content_manager()->ResolveGameUserContentPath().string().c_str());
        return uint32_t(0);
    }));
    if(worker->Create()!=0)return 5;worker->Wait(0,0,0,nullptr);
    std::printf("profile checks failed=%d\n",failed);return failed?1:0;
}
