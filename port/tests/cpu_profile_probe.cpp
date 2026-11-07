#include "../src/cpu_profile.h"
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <pthread.h>
#include <thread>
using Clock=std::chrono::steady_clock;
std::atomic<bool> quit{false};
std::atomic<unsigned> ready{0};
std::atomic<unsigned long long> sink{0};
__attribute__((noinline)) void HotWork() {
  unsigned long long n=7;
  for(unsigned i=0;i<100000;++i){n=n*6364136223846793005ull+1;asm volatile("" : "+r"(n));}
  sink.fetch_add(n,std::memory_order_relaxed);
}
volatile sig_atomic_t owned=0;
int main(int argc,char** argv) {
  if(argc!=2)return 2;
  setenv("NIER_CPU_PROFILE",argv[1],1);setenv("NIER_CPU_PROFILE_DELAY","0",1);setenv("NIER_CPU_PROFILE_SECONDS","3",1);
  if(std::getenv("NIER_TEST_OWNED_SIGNAL")) {
    std::signal(SIGPROF,[](int){owned=1;});NierStartCpuProfile();std::raise(SIGPROF);NierStopCpuProfile();return owned?0:1;
  }
  std::thread hot([]{pthread_setname_np(pthread_self(),"profile-hot");++ready;while(!quit)HotWork();});
  std::thread cool([]{pthread_setname_np(pthread_self(),"profile-cool");++ready;while(!quit){HotWork();std::this_thread::sleep_for(std::chrono::milliseconds(5));}});
  std::thread blocked([]{pthread_setname_np(pthread_self(),"profile-blocked");sigset_t mask;sigemptyset(&mask);sigaddset(&mask,SIGPROF);pthread_sigmask(SIG_BLOCK,&mask,nullptr);++ready;while(!quit)HotWork();});
  while(ready!=3)std::this_thread::yield();
  NierStartCpuProfile();std::this_thread::sleep_for(std::chrono::milliseconds(3300));NierStopCpuProfile();quit=true;hot.join();cool.join();blocked.join();
  std::printf("sampler workload finished\n");
}
