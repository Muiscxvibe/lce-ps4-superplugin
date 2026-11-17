#include "session_coordinator.hpp"
#include <assert.h>

namespace lce_session_coordinator {
struct CoordinatorTestAccess {
    static void setup(Coordinator &c,const Ops &ops,uint64_t generation) {
        c.ops_=ops;
        c.generation_=generation;
        c.phase_=Phase::Starting;
        c.transport_started_=true;
    }
    static bool defer(Coordinator &c,uint64_t generation,Error error) {
        return c.defer_transport_cleanup(generation,error);
    }
    static void set_phase(Coordinator &c,Phase phase) { c.phase_=phase; }
};
}

namespace {
struct Queue { bool reject=false; unsigned submits=0; void (*task)(void *,uint64_t)=nullptr;
               void *context=nullptr; uint64_t generation=0; } queue;
unsigned close_calls=0,reported=0;
bool state_active=false,gameplay=false;
bool schedule(void *p,void (*task)(void *,uint64_t),void *context,uint64_t generation) {
    auto &q=*static_cast<Queue *>(p); ++q.submits;
    if(q.reject)return false;
    q.task=task;q.context=context;q.generation=generation;return true;
}
void report(void *,lce_session_coordinator::Error e) { reported=static_cast<unsigned>(e)+1; }
}

extern "C" void lce_session_close(lce_session_transport *s) {
    assert(s); ++close_calls; s->state=LCE_SESSION_CLOSED;
}
extern "C" int lce_session_start(lce_session_transport *,const lce_session_io *,
        const uint8_t[4],uint16_t,uint64_t,uint32_t,uint8_t *,size_t,uint8_t *,size_t) { return 0; }
extern "C" int lce_session_start_authenticated(lce_session_transport *,const lce_session_io *,
        const uint8_t[4],uint16_t,uint64_t,uint32_t,uint8_t *,size_t,uint8_t *,size_t,
        const char *,const char *) { return 0; }
extern "C" void lce_auth_wipe(void *,size_t) {}
extern "C" lce_session_state lce_session_tick(lce_session_transport *,uint64_t,
        lce_frame_callback,void *) { return LCE_SESSION_FAILED; }
extern "C" lce_session_state lce_native_bridge_tick(lce_native_bridge *,uint64_t) {
    return LCE_SESSION_FAILED;
}
namespace lce_external_session {
bool State::active() const { return state_active; }
void State::mark_disconnected() {}
bool State::activate(void *,lce_external_players::Players &,int,uint8_t) { return false; }
void State::set_ready(bool) {}
void State::set_gameplay(bool value) { gameplay=value; }
}
namespace lce_external_players {
bool prepare_native(Players &,void *,int,uint8_t,bool (*)(void *,void *)) { return false; }
}
namespace lce_player_channel {
bool Channel::start(uintptr_t,lce_external_players::Players &,lce_session_transport &,
                    const lce_native_bridge_ops &,lce_send_failure_fn,void *) { return false; }
}
namespace lce_native_start { bool start_client(void *) { return false; } }

int main() {
    using namespace lce_session_coordinator;
    Coordinator coordinator;
    Ops ops{}; ops.context=&queue; ops.schedule_worker=schedule; ops.report_error=report;
    CoordinatorTestAccess::setup(coordinator,ops,41);
    queue.reject=true;
    assert(!CoordinatorTestAccess::defer(coordinator,41,Error::NativePlayerSetupFailed));
    assert(coordinator.phase()==Phase::CleanupPending);
    assert(close_calls==0); // failed enqueue did not close from the game thread
    assert(reported==static_cast<unsigned>(Error::SchedulerRejected)+1);

    queue.reject=false;
    assert(coordinator.retry_cleanup());
    assert(queue.task&&queue.generation==41);
    queue.task(queue.context,queue.generation); // the worker owns this close
    assert(close_calls==1);
    assert(coordinator.phase()==Phase::Idle);
    assert(coordinator.ready());
    assert(coordinator.last_error()==Error::NativePlayerSetupFailed);

    // A stale game callback cannot start setup after the retry generation changes.
    CoordinatorTestAccess::setup(coordinator,ops,42);
    assert(!coordinator.commit_game_thread(41));
    assert(coordinator.phase()==Phase::Starting);
    assert(close_calls==1);

    Coordinator loading;
    CoordinatorTestAccess::setup(loading,ops,77);
    state_active=true;
    assert(!loading.mark_gameplay_loaded(76) && !gameplay);
    assert(loading.mark_gameplay_loaded(77) && gameplay); // load can finish during Starting
    gameplay=false;
    CoordinatorTestAccess::set_phase(loading,Phase::Active);
    assert(loading.mark_gameplay_loaded(77) && gameplay);
    gameplay=false; state_active=false;
    assert(!loading.mark_gameplay_loaded(77) && !gameplay);
}
