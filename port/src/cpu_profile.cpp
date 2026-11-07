#include "cpu_profile.h"
#if defined(__linux__)
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <map>
#include <link.h>
#include <cstring>
#include <mutex>
#include <pthread.h>
#include <set>
#include <sstream>
#include <string>
#include <sys/syscall.h>
#include <sys/time.h>
#include <thread>
#include <ucontext.h>
#include <unistd.h>
#include <vector>
namespace {
using Clock = std::chrono::steady_clock;
constexpr size_t kSlots = 65536;
struct Entry { std::atomic<uintptr_t> pc{0}; std::atomic<int> tid{0}; std::atomic<uint64_t> count{0}; };
std::array<Entry, kSlots> entries;
std::atomic<bool> active{false}, stop{false};
std::atomic<uint64_t> dropped{0}, overruns{0};
static_assert(std::atomic<uintptr_t>::is_always_lock_free && std::atomic<uint64_t>::is_always_lock_free && std::atomic<int>::is_always_lock_free && std::atomic<bool>::is_always_lock_free);
void Sample(int, siginfo_t* info, void* state) {
  if (!active.load(std::memory_order_relaxed) || info->si_code != SI_TIMER) return;
  const auto* context = static_cast<const ucontext_t*>(state);
#if defined(__aarch64__)
  uintptr_t pc = context->uc_mcontext.pc;
#elif defined(__x86_64__)
  uintptr_t pc = context->uc_mcontext.gregs[REG_RIP];
#else
  uintptr_t pc = 0;
#endif
  if (!pc) return;
  const int tid = info->si_value.sival_int;
  overruns.fetch_add(unsigned(info->si_overrun), std::memory_order_relaxed);
  size_t at = ((pc >> 2) * 0x9e3779b97f4a7c15ull ^ unsigned(tid)) & (kSlots - 1);
  for (unsigned probe = 0; probe < 64; ++probe, at = (at + 1) & (kSlots - 1)) {
    auto& e = entries[at]; uintptr_t empty = 0;
    if (e.pc.compare_exchange_strong(empty, pc, std::memory_order_relaxed)) {
      e.tid.store(tid, std::memory_order_release);
      e.count.fetch_add(1, std::memory_order_relaxed); return;
    }
    if (empty == pc && e.tid.load(std::memory_order_acquire) == tid) {
      e.count.fetch_add(1, std::memory_order_relaxed); return;
    }
  }
  dropped.fetch_add(1, std::memory_order_relaxed);
}
// Linux per-thread CPU clock encoding, also used by glibc pthread_getcpuclockid.
// CPUCLOCK_SCHED(2) | CPUCLOCK_PERTHREAD_MASK(4), TID from /proc/self/task only.
clockid_t ThreadClock(int tid) { return clockid_t((~uint32_t(tid) << 3) | 6u); }
uint64_t Nanoseconds(clockid_t clock) {
  timespec now{}; if (clock_gettime(clock, &now)) return 0;
  return uint64_t(now.tv_sec) * 1000000000 + now.tv_nsec;
}
std::string Read(const std::string& path) { std::ifstream in(path); return {std::istreambuf_iterator<char>(in), {}}; }
std::string Quote(const std::string& text) {
  std::string out="\"";
  for (unsigned char c:text) {
    if(c=='"'||c=='\\'){out+='\\';out+=char(c);}
    else if(c=='\n')out+="\\n";
    else if(c=='\r')out+="\\r";
    else if(c=='\t')out+="\\t";
    else if(c>=32)out+=char(c);
  }
  return out+'"';
}
std::string BuildIds() {
  std::string out="{";
  dl_iterate_phdr([](dl_phdr_info* info,size_t,void* user) {
    auto& json=*static_cast<std::string*>(user);
    for(unsigned i=0;i<info->dlpi_phnum;++i) {
      const auto& ph=info->dlpi_phdr[i];if(ph.p_type!=PT_NOTE)continue;
      auto* start=reinterpret_cast<const uint8_t*>(info->dlpi_addr+ph.p_vaddr);
      for(size_t at=0;at+sizeof(ElfW(Nhdr))<=ph.p_memsz;) {
        ElfW(Nhdr) note;std::memcpy(&note,start+at,sizeof note);at+=sizeof note;
        size_t nameBytes=(size_t(note.n_namesz)+3)&~size_t(3),dataBytes=(size_t(note.n_descsz)+3)&~size_t(3);
        if(nameBytes>ph.p_memsz-at || dataBytes>ph.p_memsz-at-nameBytes)break;
        if(note.n_type==NT_GNU_BUILD_ID && note.n_namesz==4 && !std::memcmp(start+at,"GNU",4)) {
          std::string path=info->dlpi_name;
          if(path.empty()){char executable[4096];ssize_t n=readlink("/proc/self/exe",executable,sizeof executable);if(n>0)path.assign(executable,n);}
          if(path.ends_with(" (deleted)"))path.resize(path.size()-10);
          path=path.substr(path.find_last_of('/')+1);
          std::string id;const char* hex="0123456789abcdef";
          for(size_t j=0;j<note.n_descsz;++j){uint8_t c=start[at+nameBytes+j];id+=hex[c>>4];id+=hex[c&15];}
          if(json.size()>1)json+=',';json+=Quote(path)+":"+Quote(id);return 0;
        }
        at+=nameBytes+dataBytes;
      }
    }
    return 0;
  },&out);
  return out+'}';
}
struct Timer { timer_t id; uint64_t start; std::string name; };
struct Sampler {
  std::thread worker;
  std::string path;
  unsigned hz=100, delay=140, seconds=45;
  std::map<int,Timer> timers;
  std::map<int,std::string> names;
  std::map<int,uint64_t> cpuStart, cpuLast;
  std::map<int,bool> signalBlocked;
  uint64_t processStart=0;
  Clock::time_point started;
  unsigned timerErrors=0;
  ~Sampler() { Finish(); }
  void Finish() { stop=true; if(worker.joinable())worker.join(); }
  void Discover() {
    DIR* directory=opendir("/proc/self/task"); if(!directory)return;
    const int own=int(syscall(SYS_gettid)); std::set<int> live;
    while(auto* entry=readdir(directory)) {
      int tid=std::atoi(entry->d_name); if(tid<=0 || tid==own)continue;
      live.insert(tid);
      timespec now{}; auto clock=ThreadClock(tid);
      if(clock_gettime(clock,&now))continue; // exited between enumeration and registration
      uint64_t cpu=uint64_t(now.tv_sec)*1000000000+now.tv_nsec;
      if(!cpuStart.count(tid))cpuStart[tid]=cpu;cpuLast[tid]=cpu;
      std::string name=Read("/proc/self/task/"+std::to_string(tid)+"/comm");
      if(!name.empty()&&name.back()=='\n')name.pop_back();names[tid]=name;
      std::string status=Read("/proc/self/task/"+std::to_string(tid)+"/status");
      size_t field=status.find("SigBlk:");
      signalBlocked[tid]=field!=std::string::npos && (std::strtoull(status.c_str()+field+7,nullptr,16)&(1ull<<(SIGPROF-1)));
      if(timers.count(tid)||signalBlocked[tid])continue;
      sigevent event{};event.sigev_notify=SIGEV_THREAD_ID;event.sigev_signo=SIGPROF;
      event._sigev_un._tid=tid;event.sigev_value.sival_int=tid;
      timer_t timer;
      if(timer_create(clock,&event,&timer)) {++timerErrors;continue;}
      itimerspec period{};period.it_interval.tv_nsec=1000000000/hz;period.it_value=period.it_interval;
      if(timer_settime(timer,0,&period,nullptr)){timer_delete(timer);++timerErrors;continue;}
      timers.emplace(tid,Timer{timer,Nanoseconds(clock),name});
    }
    closedir(directory);
    for(auto it=timers.begin();it!=timers.end();) {
      if(!live.count(it->first)){timer_delete(it->second.id);it=timers.erase(it);}else ++it;
    }
  }
  void Dump(bool complete) {
    struct Row {uintptr_t pc;int tid;uint64_t count;};std::vector<Row> rows;
    uint64_t total=0;
    for(auto& e:entries) {
      uintptr_t pc=e.pc.load();int tid=e.tid.load();uint64_t count=e.count.load();
      if(pc&&tid&&count){rows.push_back({pc,tid,count});total+=count;}
    }
    std::sort(rows.begin(),rows.end(),[](auto a,auto b){return a.count>b.count;});
    std::string temporary=path+".tmp";std::ofstream out(temporary);
    if(!out){std::fprintf(stderr,"NIER_CPU_PROFILE cannot write %s\n",temporary.c_str());return;}
    out<<"{\"tool\":\"nier_cpu_sampler\",\"method\":\"per-thread CPU clock + targeted SIGPROF\",\"hz\":"<<hz
       <<",\"build_ids\":"<<BuildIds()<<",\"delay_seconds\":"<<delay<<",\"requested_seconds\":"<<seconds
       <<",\"pid\":"<<getpid()<<",\"complete\":"<<(complete?"true":"false")
       <<",\"wall_seconds\":"<<std::chrono::duration<double>(Clock::now()-started).count()
       <<",\"process_cpu_seconds\":"<<double(Nanoseconds(CLOCK_PROCESS_CPUTIME_ID)-processStart)/1e9
       <<",\"samples\":"<<total<<",\"table_dropped\":"<<dropped.load()<<",\"timer_overruns\":"<<overruns.load()
       <<",\"timer_errors\":"<<timerErrors<<",\"maps\":"<<Quote(Read("/proc/self/maps"))<<",\"threads\":[";
    bool comma=false;for(auto& [tid,name]:names){uint64_t latest=Nanoseconds(ThreadClock(tid));if(latest&&latest>=cpuStart[tid])cpuLast[tid]=latest;if(comma)out<<',';comma=true;out<<"{\"tid\":"<<tid<<",\"name\":"<<Quote(name)<<",\"cpu_seconds_observed\":"<<double(cpuLast[tid]-cpuStart[tid])/1e9<<",\"sigprof_blocked_last_scan\":"<<(signalBlocked[tid]?"true":"false")<<'}';}
    out<<"],\"pcs\":[";comma=false;
    for(auto row:rows){if(comma)out<<',';comma=true;out<<"{\"pc\":\"0x"<<std::hex<<row.pc<<std::dec<<"\",\"tid\":"<<row.tid<<",\"count\":"<<row.count<<'}';}
    out<<"]}\n";out.close();if(out)std::rename(temporary.c_str(),path.c_str());
  }
  void Run() {
    sigset_t mask;sigemptyset(&mask);sigaddset(&mask,SIGPROF);pthread_sigmask(SIG_BLOCK,&mask,nullptr);
    auto waitUntil=Clock::now()+std::chrono::seconds(delay);
    while(!stop && Clock::now()<waitUntil)std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if(stop)return;
    started=Clock::now();processStart=Nanoseconds(CLOCK_PROCESS_CPUTIME_ID);active=true;
    auto nextDiscover=started, nextDump=started+std::chrono::seconds(5);
    while(!stop && Clock::now()-started<std::chrono::seconds(seconds)) {
      auto now=Clock::now();
      if(now>=nextDiscover){Discover();nextDiscover=now+std::chrono::milliseconds(250);}
      if(now>=nextDump){Dump(false);nextDump=now+std::chrono::seconds(5);}
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    active=false;for(auto& [tid,timer]:timers)timer_delete(timer.id);timers.clear();Dump(true);
    std::fprintf(stderr,"NIER_CPU_PROFILE finished %s dropped=%llu overruns=%llu errors=%u\n",path.c_str(),
      (unsigned long long)dropped.load(),(unsigned long long)overruns.load(),timerErrors);
    // Keep the inactive handler: late timer signals must not restore SIGPROF's terminate action.
  }
} sampler;
unsigned Env(const char* key,unsigned fallback,unsigned minimum,unsigned maximum) {
  const char* value=std::getenv(key);if(!value)return fallback;
  return std::clamp(unsigned(std::strtoul(value,nullptr,10)),minimum,maximum);
}
}
void NierStartCpuProfile() {
  const char* path=std::getenv("NIER_CPU_PROFILE");if(!path||!*path||sampler.worker.joinable())return;
  struct sigaction previous{};itimerval existing{};
  if(sigaction(SIGPROF,nullptr,&previous)||getitimer(ITIMER_PROF,&existing) ||
     (previous.sa_handler!=SIG_DFL&&previous.sa_handler!=SIG_IGN) || existing.it_value.tv_sec||existing.it_value.tv_usec) {
    std::fprintf(stderr,"NIER_CPU_PROFILE disabled: SIGPROF/timer already owned\n");return;
  }
  struct sigaction action{};action.sa_sigaction=Sample;action.sa_flags=SA_SIGINFO|SA_RESTART;
  sigemptyset(&action.sa_mask);sigaddset(&action.sa_mask,SIGPROF);
  if(sigaction(SIGPROF,&action,nullptr)){std::perror("NIER_CPU_PROFILE sigaction");return;}
  sampler.path=path;sampler.hz=Env("NIER_CPU_PROFILE_HZ",100,10,200);
  sampler.delay=Env("NIER_CPU_PROFILE_DELAY",140,0,280);sampler.seconds=Env("NIER_CPU_PROFILE_SECONDS",45,1,120);
  stop=false;sampler.worker=std::thread([]{sampler.Run();});
  std::fprintf(stderr,"NIER_CPU_PROFILE armed hz=%u delay=%u seconds=%u path=%s\n",sampler.hz,sampler.delay,sampler.seconds,path);
}
void NierStopCpuProfile() { sampler.Finish(); }
#else
void NierStartCpuProfile() {}
void NierStopCpuProfile() {}
#endif
