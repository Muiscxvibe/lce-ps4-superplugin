#include "orbis_scheduler.hpp"
#include <cassert>
#include <cstring>
extern "C" int32_t scePthreadCreate(OrbisPthread *t,const void *,void *(*)(void *),void *,const char *){*t=1;return 0;}
extern "C" int32_t sceKernelUsleep(uint32_t){return 0;}
extern "C" uint64_t sceKernelGetProcessTime(){return 1234567;}
static void task(void *p,uint64_t n){*static_cast<uint64_t *>(p)=n;}
int main(){using lce_orbis_scheduler::Scheduler;Scheduler scheduler;uint8_t ip[4]={1,2,3,4};
 assert(Scheduler::resolve_ipv4(nullptr,"192.168.1.165",ip));uint8_t want[]={192,168,1,165};assert(!std::memcmp(ip,want,4));
 assert(!Scheduler::resolve_ipv4(nullptr,"192.168.1.256",ip)&&!std::memcmp(ip,want,4));
 assert(!Scheduler::resolve_ipv4(nullptr,"example.com",ip));assert(!Scheduler::resolve_ipv4(nullptr,"1.2.3.4.5",ip));
 assert(!Scheduler::resolve_ipv4(nullptr,"1.2.3.",ip));assert(Scheduler::monotonic_ms(nullptr)==1234);
 uint64_t result=0;assert(!Scheduler::schedule_game(&scheduler,task,&result,42));assert(scheduler.start());
 assert(Scheduler::schedule_game(&scheduler,task,&result,42));assert(result==0);
 assert(!Scheduler::schedule_game(&scheduler,task,&result,43));assert(scheduler.run_ui_task()&&result==42);
 assert(!scheduler.run_ui_task());assert(Scheduler::schedule_game(&scheduler,task,&result,43));assert(scheduler.run_ui_task()&&result==43);
}
