// Concurrency contracts and thread-CPU measurement for the private SDK build.
#include <rex/thread.h>
#include <rex/cvar.h>
#include <chrono>
#include <cstdio>
#include <thread>
#include <stdexcept>
#include <time.h>
REXCVAR_DECLARE(int32_t,audio_maxqframes);
using namespace std::chrono_literals;
using namespace rex::thread;
static void require(bool condition,const char* message) {
    if (!condition) throw std::runtime_error(message);
}
static double cpu_seconds() {
    timespec ts{}; clock_gettime(CLOCK_THREAD_CPUTIME_ID,&ts);
    return ts.tv_sec+ts.tv_nsec/1e9;
}
int main() {
    auto first=Semaphore::Create(0,64);
    auto second=Event::CreateAutoResetEvent(false);
    WaitHandle* handles[]{first.get(),second.get()};
    auto start=std::chrono::steady_clock::now();
    const double cpu0=cpu_seconds();
    require(WaitAny(handles,2,false,100ms).first==WaitResult::kTimeout,"idle timeout");
    const double cpu=cpu_seconds()-cpu0;
    const double wall=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    std::printf("WAIT_PROBE idle_wall=%.9f idle_cpu=%.9f queue_frames=%d\n",wall,cpu,REXCVAR_GET(audio_maxqframes));
    for (unsigned i=0;i<64;++i) {
        std::jthread producer([&] { std::this_thread::sleep_for(1ms); first->Release(1,nullptr); });
        auto result=WaitAny(handles,2,false,500ms);
        require(result.first==WaitResult::kSuccess && result.second==0,"lost semaphore signal");
        require(Wait(first.get(),false,0ms)==WaitResult::kTimeout,"semaphore consumed twice");
    }
    second->Set(); second->Set();
    require(WaitAny(handles,2,false,0ms).second==1,"event selection");
    require(WaitAny(handles,2,false,1ms).first==WaitResult::kTimeout,"auto-reset event did not coalesce");
    auto a=Semaphore::Create(1,2),b=Semaphore::Create(0,2);
    WaitHandle* all[]{a.get(),b.get()};
    {
        std::jthread producer([&] { std::this_thread::sleep_for(10ms); b->Release(1,nullptr); });
        require(WaitAll(all,2,false,500ms)==WaitResult::kSuccess,"wait-all wake");
    }
    require(Wait(a.get(),false,0ms)==WaitResult::kTimeout && Wait(b.get(),false,0ms)==WaitResult::kTimeout,"wait-all atomic consumption");
    auto manual=Event::CreateManualResetEvent(false);
    bool done1=false,done2=false;
    {
        auto waiter=[&](bool& done) { WaitHandle* h[]{manual.get(),first.get()}; done=WaitAny(h,2,false,500ms).first==WaitResult::kSuccess; };
        std::jthread t1([&]{waiter(done1);}),t2([&]{waiter(done2);});
        std::this_thread::sleep_for(10ms);manual->Set();
    }
    require(done1 && done2,"manual-reset event did not wake both waiters");
    manual->Reset();
    require(Wait(manual.get(),false,0ms)==WaitResult::kTimeout,"manual event reset");
    std::puts("PASS wait-any, wait-all, 64 concurrent semaphore wakeups, coalescing and manual reset");
}
