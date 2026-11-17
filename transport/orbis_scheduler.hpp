#pragma once
#include <stdint.h>
#include <orbis/libkernel.h>

namespace lce_orbis_scheduler {
// Process-lifetime worker plus one bounded mailbox for the verified UI tick.
// No task runs inline when scheduled. There is deliberately no hot unload.
class Scheduler {
public:
    using Task = void (*)(void *,uint64_t);
    Scheduler()=default;
    Scheduler(const Scheduler &)=delete;
    Scheduler &operator=(const Scheduler &)=delete;
    bool start();
    bool running() const { return __atomic_load_n(&started_,__ATOMIC_ACQUIRE)==2; }
    // Call from the existing outer UI tick after verifying ownership and
    // excluding nested ticks. Processes at most one queued game task.
    bool run_ui_task();
    static bool schedule_worker(void *,Task,void *,uint64_t);
    static bool schedule_game(void *,Task,void *,uint64_t);
    static uint64_t monotonic_ms(void *);
    // Numeric IPv4 only. DNS names return false until a resolver is wired.
    static bool resolve_ipv4(void *,const char *,uint8_t out[4]);
private:
    struct Mailbox {
        unsigned state=0;
        Task entry=nullptr;
        void *context=nullptr;
        uint64_t generation=0;
        bool post(Task,void *,uint64_t);
        bool run_one();
    };
    static void *worker_main(void *);
    Mailbox worker_,game_;
    OrbisPthread thread_{};
    unsigned started_=0;
};
}
