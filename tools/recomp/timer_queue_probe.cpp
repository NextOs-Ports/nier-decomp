#include <rex/thread/timer_queue.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <future>
#include <mutex>
#include <thread>
#include <vector>
#include <ctime>
using namespace std::chrono_literals;
using namespace rex::thread;
using Clock=TimerQueueWaitItem::clock;
static double Cpu(){timespec t;clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&t);return t.tv_sec+t.tv_nsec/1e9;}
template<class F> bool Until(F test){auto end=Clock::now()+2s;while(!test()&&Clock::now()<end)std::this_thread::sleep_for(1ms);return test();}
int main(){
  auto start=Clock::now();double cpu=Cpu();std::this_thread::sleep_for(400ms);
  double idleCpu=Cpu()-cpu, idleWall=std::chrono::duration<double>(Clock::now()-start).count();
  std::atomic<bool> farCalled=false, earlyCalled=false;
  auto far=QueueTimerOnce([&](void*){farCalled=true;},nullptr,Clock::now()+1500ms);
  std::this_thread::sleep_for(20ms);auto earlyDue=Clock::now()+20ms;std::atomic<long long> earlyLate=0;
  QueueTimerOnce([&](void*){earlyLate=std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-earlyDue).count();earlyCalled=true;},nullptr,earlyDue);
  if(!Until([&]{return earlyCalled.load();})||farCalled||earlyLate<0||earlyLate>200000)return 1;
  if(auto item=far.lock())item->Disarm();
  std::atomic<unsigned> burst=0;std::vector<std::thread> producers;
  for(int thread=0;thread<4;++thread)producers.emplace_back([&]{for(int n=0;n<300;++n)QueueTimerOnce([&](void*){++burst;},nullptr,Clock::now()+10ms);});
  for(auto& thread:producers)thread.join();if(!Until([&]{return burst==1200;}))return 2;
  std::atomic<unsigned> ticks=0;
  auto recurring=QueueTimerRecurring([&](void*){++ticks;},nullptr,Clock::now()+1ms,1ms);
  auto periodicStart=Clock::now();double periodicCpu=Cpu();
  std::this_thread::sleep_for(400ms);if(auto item=recurring.lock())item->Disarm();
  periodicCpu=Cpu()-periodicCpu;double periodicWall=std::chrono::duration<double>(Clock::now()-periodicStart).count();
  unsigned stopped=ticks;std::this_thread::sleep_for(30ms);if(ticks!=stopped||ticks<100||ticks>800)return 3;
  std::mutex selfMutex;std::weak_ptr<TimerQueueWaitItem> self;std::atomic<unsigned> selfCount=0;
  {std::lock_guard lock(selfMutex);self=QueueTimerRecurring([&](void*){
    if(++selfCount==3){std::lock_guard lock(selfMutex);if(auto item=self.lock())item->Disarm();}
  },nullptr,Clock::now()+20ms,5ms);}
  if(!Until([&]{return selfCount>=3;}))return 4;std::this_thread::sleep_for(30ms);if(selfCount!=3)return 5;
  std::atomic<bool> entered=false,leave=false,exited=false;
  auto busy=QueueTimerOnce([&](void*){entered=true;while(!leave)std::this_thread::sleep_for(1ms);exited=true;},nullptr,Clock::now()+10ms);
  if(!Until([&]{return entered.load();}))return 6;
  auto cancellation=std::async(std::launch::async,[&]{if(auto item=busy.lock())item->Disarm();return exited.load();});
  if(cancellation.wait_for(20ms)!=std::future_status::timeout)return 7;
  leave=true;if(!cancellation.get())return 8;
  std::printf("{\"passed\":true,\"idle_wall_seconds\":%.6f,\"idle_cpu_seconds\":%.6f,\"periodic_wall_seconds\":%.6f,\"periodic_cpu_seconds\":%.6f,\"early_late_us\":%lld,\"burst\":%u,\"recurring_callbacks\":%u,\"self_disarm_callbacks\":%u,\"disarm_wait_pass\":true}\n",idleWall,idleCpu,periodicWall,periodicCpu,earlyLate.load(),burst.load(),stopped,selfCount.load());
}
