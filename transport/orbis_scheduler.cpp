#include "orbis_scheduler.hpp"

namespace lce_orbis_scheduler {
bool Scheduler::Mailbox::post(Task task,void *ctx,uint64_t gen) {
    if (!task) return false;
    unsigned empty=0;
    if (!__atomic_compare_exchange_n(&state,&empty,1,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)) return false;
    entry=task; context=ctx; generation=gen;
    __atomic_store_n(&state,2,__ATOMIC_RELEASE);
    return true;
}
bool Scheduler::Mailbox::run_one() {
    unsigned ready=2;
    if (!__atomic_compare_exchange_n(&state,&ready,3,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)) return false;
    Task task=entry; void *ctx=context; uint64_t gen=generation;
    __atomic_store_n(&state,0,__ATOMIC_RELEASE);
    task(ctx,gen);
    return true;
}
void *Scheduler::worker_main(void *p) {
    auto *self=static_cast<Scheduler *>(p);
    for (;;) { self->worker_.run_one(); sceKernelUsleep(5000); }
}
bool Scheduler::start() {
    unsigned expected=0;
    if (!__atomic_compare_exchange_n(&started_,&expected,1,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) return false;
    if (scePthreadCreate(&thread_,nullptr,worker_main,this,"lce_server_worker")) {
        __atomic_store_n(&started_,0,__ATOMIC_RELEASE); return false;
    }
    __atomic_store_n(&started_,2,__ATOMIC_RELEASE);
    return true;
}
bool Scheduler::schedule_worker(void *ctx,Task task,void *arg,uint64_t gen) {
    auto *s=static_cast<Scheduler *>(ctx);
    return s && __atomic_load_n(&s->started_,__ATOMIC_ACQUIRE)==2 && s->worker_.post(task,arg,gen);
}
bool Scheduler::schedule_game(void *ctx,Task task,void *arg,uint64_t gen) {
    auto *s=static_cast<Scheduler *>(ctx);
    return s && __atomic_load_n(&s->started_,__ATOMIC_ACQUIRE)==2 && s->game_.post(task,arg,gen);
}
bool Scheduler::run_ui_task() { return game_.run_one(); }
uint64_t Scheduler::monotonic_ms(void *) { return sceKernelGetProcessTime()/1000; }
bool Scheduler::resolve_ipv4(void *,const char *host,uint8_t out[4]) {
    if (!host||!out) return false;
    uint8_t parsed[4]; const char *p=host;
    for (unsigned i=0;i<4;++i) {
        unsigned value=0,digits=0;
        while (*p>='0' && *p<='9') {
            if (++digits>3) return false;
            value=value*10+static_cast<unsigned>(*p++-'0');
        }
        if (!digits || value>255 || (i<3 ? *p!='.' : *p!='\0')) return false;
        parsed[i]=static_cast<uint8_t>(value);
        if (i<3) ++p;
    }
    for (unsigned i=0;i<4;++i) out[i]=parsed[i];
    return true;
}
}
