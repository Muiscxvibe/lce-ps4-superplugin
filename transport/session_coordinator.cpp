#include "session_coordinator.hpp"
#include "socket_native_factory.hpp"
#include <string.h>

namespace lce_session_coordinator {
Coordinator *Coordinator::active_=nullptr;

void Coordinator::lock() const {
    while (__atomic_test_and_set(&lock_,__ATOMIC_ACQUIRE)) {
        while (__atomic_load_n(&lock_,__ATOMIC_RELAXED)) {
#if defined(__x86_64__)
            __asm__ __volatile__("pause");
#endif
        }
    }
}
void Coordinator::unlock() const { __atomic_clear(&lock_,__ATOMIC_RELEASE); }

bool Coordinator::configure(uintptr_t image,void *manager,
        const lce_servers::List &servers,const Ops &ops) {
    if (__atomic_load_n(&active_,__ATOMIC_ACQUIRE) || !image || !manager || !servers.count ||
        servers.count>lce_servers::kMaxServers ||
        !ops.resolve_ipv4 || !ops.schedule_worker || !ops.schedule_game ||
        !ops.monotonic_ms || !ops.session_io.open || !ops.session_io.start ||
        !ops.session_io.poll || !ops.session_io.recv || !ops.session_io.send ||
        !ops.session_io.close || !ops.bridge.lock || !ops.bridge.unlock ||
        !ops.bridge.push || !ops.retire_socket ||
        !ops.input_buffer || !ops.input_capacity || !ops.output_buffer ||
        ops.output_capacity<5 || !ops.connect_timeout_ms ||
        image>UINTPTR_MAX-0x143b150 ||
        reinterpret_cast<uintptr_t>(manager)>UINTPTR_MAX-0x18) return false;
    // Reject an early/unrelated owner tick before any one-shot factory binds.
    // A premature factory bind is process-global and cannot be rolled back.
    if (*reinterpret_cast<void **>(image+0x143b148)!=manager) return false;
    void *sqr=*reinterpret_cast<void **>(reinterpret_cast<uintptr_t>(manager)+0x10);
    if (!sqr) return false;
    if (!lce_external_players::bind_native(image) || !lce_native_start::bind(image)) return false;
    image_=image;
    platform_manager_=manager;
    sqr_manager_=sqr;
    servers_=servers;
    ops_=ops;
    phase_=Phase::Idle;
    error_=Error::None;
    __atomic_store_n(&active_,this,__ATOMIC_RELEASE);
    return true;
}

bool Coordinator::ready() const {
    lock(); const bool result=phase_==Phase::Idle; unlock(); return result;
}

bool Coordinator::backend_ready() {
    Coordinator *active=__atomic_load_n(&active_,__ATOMIC_ACQUIRE);
    return active && active->ready();
}
int Coordinator::backend_join(const lce_servers::Server &s,int mask,int user) {
    Coordinator *active=__atomic_load_n(&active_,__ATOMIC_ACQUIRE);
    return active ? active->native_backend_join(s,mask,user) : 1;
}
int Coordinator::native_backend_join(const lce_servers::Server &s,int mask,int user) {
    lce_auth_result account{};
    lock();
    const bool staged=staged_;
    const bool matching=staged && user==staged_user_ && supports_player_request(mask,user) &&
        !strcmp(s.label,staged_server_.label) && !strcmp(s.host,staged_server_.host) && s.port==staged_server_.port;
    if (matching) account=staged_account_;
    if (staged) {lce_auth_wipe(&staged_account_,sizeof staged_account_);staged_=false;staged_user_=-1;}
    unlock();
    if (!staged) return join(s,mask,user);
    const int result=matching?join_authenticated(s,mask,user,account):1;
    lce_auth_wipe(&account,sizeof account);
    return result;
}
void Coordinator::backend_info(const lce_servers::Server &s,void *,
        lce_join_adapter::Complete complete,void *context) {
    Coordinator *active=__atomic_load_n(&active_,__ATOMIC_ACQUIRE);
    const bool ok=active && active->ready() && active->is_server(s);
    // Matches the PC stub's synchronous completion. Missing/stale records or
    // unavailable backend fail closed and never reach the Sony implementation.
    if (complete) complete(ok,context);
}
JoinCallbacks Coordinator::join_callbacks() { return {backend_ready,backend_join,backend_info}; }
lce_session_queries::Custom Coordinator::query_callbacks() { return state_.query_callbacks(); }

bool Coordinator::is_server(const lce_servers::Server &s) const {
    for (size_t i=0;i<servers_.count;++i) {
        const auto &candidate=servers_.entries[i];
        if (!strcmp(candidate.label,s.label) && !strcmp(candidate.host,s.host) &&
            candidate.port==s.port) return true;
    }
    return false;
}

int Coordinator::join(const lce_servers::Server &server,int mask,int user) {
    return begin_join(server,mask,user,nullptr);
}
int Coordinator::join_authenticated(const lce_servers::Server &server,int mask,int user,
                                     const lce_auth_result &account) {
    lce_gateway_handshake probe{};
    if (!account.accepted || !lce_gateway_init(&probe,account.ticket_id,account.ticket_key)) return 1;
    lce_gateway_clear(&probe);
    return begin_join(server,mask,user,&account);
}
int Coordinator::join_authenticated_native(const lce_servers::Server &server,int user,
                                            const lce_auth_result &account) {
    if (!image_ || image_>UINTPTR_MAX-0x147c988 || user!=0 || !is_server(server) || !account.accepted ||
        *reinterpret_cast<void **>(image_+0x143b148)!=platform_manager_) return 1;
    lce_gateway_handshake probe{};
    if (!lce_gateway_init(&probe,account.ticket_id,account.ticket_key)) return 1;
    lce_gateway_clear(&probe);
    const unsigned char entry[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec,0x38};
    if (memcmp(reinterpret_cast<void *>(image_+0x945480),entry,sizeof entry)) return 1;
    void *profile=*reinterpret_cast<void **>(image_+0x147c988);
    if (!profile) return 1;
    auto *table=*reinterpret_cast<uintptr_t **>(profile);
    if (!table || !table[0x50/8] || !table[0x118/8] ||
        reinterpret_cast<int (*)(void *)>(table[0x118/8])(profile)!=user) return 1;
    uint32_t index=0;
    for (;index<servers_.count;++index) {
        const auto &s=servers_.entries[index];
        if (!strcmp(s.label,server.label) && !strcmp(s.host,server.host) && s.port==server.port) break;
    }
    alignas(8) unsigned char record[0x208];
    if (!lce_join_list::make_join_record(record,sizeof record,index)) return 1;
    recover_retained();
    lock();
    if (staged_ || phase_!=Phase::Idle) {unlock();return 1;}
    staged_account_=account;staged_server_=server;staged_user_=user;staged_=true;
    unlock();
    // Retail UIScene_JoinMenu::JoinGame locks profile zero before native join.
    reinterpret_cast<void (*)(void *,int)>(table[0x50/8])(profile,0);
    using NativeJoin=int (*)(void *,void *,int,bool);
    int result=reinterpret_cast<NativeJoin>(image_+0x945480)(reinterpret_cast<void *>(image_+0x143b050),record,1,true);
    lock();
    const bool consumed=!staged_;
    if (staged_) {lce_auth_wipe(&staged_account_,sizeof staged_account_);staged_=false;staged_user_=-1;}
    unlock();
    return result==0 && consumed?0:1;
}
int Coordinator::begin_join(const lce_servers::Server &server,int mask,int user,const lce_auth_result *account) {
    if (!supports_player_request(mask,user)) {
        lock(); error_=Error::UnsupportedLocalPlayers; unlock();
        if (ops_.report_error) ops_.report_error(ops_.context,Error::UnsupportedLocalPlayers);
        return 1;
    }
    if (!is_server(server)) {
        lock(); error_=Error::UnknownServer; unlock();
        if (ops_.report_error) ops_.report_error(ops_.context,Error::UnknownServer);
        return 1;
    }
    uint64_t gen;
    recover_retained();
    lock();
    if (phase_!=Phase::Idle) { unlock(); return 1; }
    selected_=server;
    authenticated_=account!=nullptr;
    if (account) {
        memcpy(ticket_id_,account->ticket_id,sizeof ticket_id_);
        memcpy(ticket_key_,account->ticket_key,sizeof ticket_key_);
    }
    user_index_=user;
    cancel_requested_=false;
    transport_started_=false;
    __atomic_store_n(&send_failed_,0,__ATOMIC_RELEASE);
    error_=Error::None;
    phase_=Phase::Connecting;
    gen=++generation_;
    unlock();
    if (!ops_.schedule_worker(ops_.context,worker_entry,this,gen)) {
        lock(); phase_=Phase::Idle; error_=Error::SchedulerRejected;authenticated_=false;
        lce_auth_wipe(ticket_id_,sizeof ticket_id_);lce_auth_wipe(ticket_key_,sizeof ticket_key_);unlock();
        if (ops_.report_error) ops_.report_error(ops_.context,Error::SchedulerRejected);
        return 1;
    }
    // The asynchronous connection attempt has been accepted. The eventual
    // transport/login result is reported separately by the owner callback.
    return 0;
}

bool Coordinator::fail(Error e) {
    const bool disconnected=state_.active();
    lock();
    error_=e;
    authenticated_=false;
    lce_auth_wipe(ticket_id_,sizeof ticket_id_);lce_auth_wipe(ticket_key_,sizeof ticket_key_);
    const bool retain=players_.has_native_objects() || channel_.bridge() || disconnected;
    if (retain) phase_=Phase::FailedRetained;
    else {
        // No game object has been published. The transport is already closed
        // or will be closed by its owning worker; a fresh generation may retry.
        phase_=Phase::Idle;
        transport_started_=false;
        local_id_=0;
        cancel_requested_=false;
    }
    unlock();
    if (disconnected) {
        state_.mark_disconnected();
        // Let the game notice: a closed client end makes Connection::tick
        // disconnect with "connection lost" and return to the menu instead
        // of freezing in a world whose packets are no longer handled.
        if (void *socket=players_.socket()) lce_socket_native::close_client_end(socket);
    }
    if (ops_.report_error) ops_.report_error(ops_.context,e);
    return false;
}

// Called from the join menu (the game has left the old session). Tears down
// the retained objects from a dropped session so a new join can start.
bool Coordinator::recover_retained() {
    lock();
    if (phase_!=Phase::FailedRetained) { unlock(); return phase_==Phase::Idle; }
    unlock();
    channel_.stop();
    state_.deactivate();
    if (!players_.retire()) players_.abandon();
    lock();
    phase_=Phase::Idle;
    error_=Error::None;
    transport_started_=false;
    local_id_=0;
    cancel_requested_=false;
    unlock();
    return true;
}

int Coordinator::frame(void *ctx,const uint8_t *,size_t) {
    auto *self=static_cast<Coordinator *>(ctx);
    // Before channel activation there is no native socket to receive game
    // packets. Fail closed rather than consuming and silently losing one.
    return self && self->channel_.bridge()!=nullptr;
}

bool Coordinator::worker_step(uint64_t gen) {
    lock();
    if (gen!=generation_) { unlock(); return false; }
    const Phase current=phase_;
    const bool cancelled=cancel_requested_;
    const Error cleanup_error=cleanup_error_;
    if (current==Phase::CleanupPending) cleanup_queued_=false;
    unlock();
    if (__atomic_load_n(&send_failed_,__ATOMIC_ACQUIRE))
        return fail(Error::SendFailed);
    if (current==Phase::CleanupPending) {
        if (transport_started_) lce_session_close(&transport_);
        return fail(cleanup_error);
    }
    if (cancelled && (current==Phase::Connecting || current==Phase::AwaitingGameThread)) {
        if (transport_started_) lce_session_close(&transport_);
        return fail(Error::CancelledBeforeStart);
    }
    if (current==Phase::Connecting && !transport_started_) {
        if (!ops_.resolve_ipv4(ops_.context,selected_.host,ip_))
            return fail(Error::ResolveFailed);
        const uint64_t now=ops_.monotonic_ms(ops_.context);
        const bool started=authenticated_
            ? lce_session_start_authenticated(&transport_,&ops_.session_io,ip_,selected_.port,now,
                ops_.connect_timeout_ms,ops_.input_buffer,ops_.input_capacity,ops_.output_buffer,
                ops_.output_capacity,ticket_id_,ticket_key_)
            : lce_session_start(&transport_,&ops_.session_io,ip_,selected_.port,now,
                ops_.connect_timeout_ms,ops_.input_buffer,ops_.input_capacity,
                ops_.output_buffer,ops_.output_capacity);
        lce_auth_wipe(ticket_id_,sizeof ticket_id_);lce_auth_wipe(ticket_key_,sizeof ticket_key_);
        authenticated_=false;
        if (!started)
            return fail(Error::TransportStartFailed);
        transport_started_=true;
    }
    if (current==Phase::Connecting) {
        const auto result=lce_session_tick(&transport_,ops_.monotonic_ms(ops_.context),frame,this);
        if (result==LCE_SESSION_FAILED || result==LCE_SESSION_REJECTED ||
            result==LCE_SESSION_CLOSED) return fail(Error::TransportFailed);
        if (transport_.connection.reader.has_id) {
            local_id_=transport_.connection.reader.assigned_id;
            if (!local_id_ || local_id_==0xff) return fail(Error::TransportFailed);
            lock();
            if (gen!=generation_ || phase_!=Phase::Connecting) { unlock(); return false; }
            phase_=Phase::AwaitingGameThread;
            unlock();
            if (!ops_.schedule_game(ops_.context,game_entry,this,gen)) {
                lce_session_close(&transport_);
                return fail(Error::SchedulerRejected);
            }
            return false;
        }
        return true;
    }
    if (current==Phase::Active) {
        auto *b=channel_.bridge();
        if (!b) return fail(Error::TransportFailed);
        const auto result=lce_native_bridge_tick(b,ops_.monotonic_ms(ops_.context));
        if (result==LCE_SESSION_FAILED || result==LCE_SESSION_REJECTED ||
            result==LCE_SESSION_CLOSED) return fail(Error::TransportFailed);
        return true;
    }
    return false;
}

void Coordinator::worker_entry(void *ctx,uint64_t gen) {
    auto *self=static_cast<Coordinator *>(ctx);
    if (!self || !self->worker_step(gen)) return;
    if (!self->ops_.schedule_worker(self->ops_.context,worker_entry,self,gen)) {
        const Phase p=self->phase();
        if ((p==Phase::Connecting || p==Phase::AwaitingGameThread) && self->transport_started_)
            lce_session_close(&self->transport_);
        self->fail(Error::SchedulerRejected);
    }
}

bool Coordinator::commit_game_thread(uint64_t gen) {
    lock();
    if (gen!=generation_ || phase_!=Phase::AwaitingGameThread) { unlock(); return false; }
    const bool cancelled=cancel_requested_;
    if (!cancelled) phase_=Phase::Starting;
    unlock();
    if (cancelled) {
        // cancel() already queued worker cleanup for this phase.
        return false;
    }
    if (!transport_.connection.reader.has_id || transport_.state!=LCE_SESSION_ACTIVE ||
        !lce_external_players::prepare_native(players_,sqr_manager_,user_index_,local_id_,ops_.retire_socket))
        return defer_transport_cleanup(gen,Error::NativePlayerSetupFailed);
    if (!channel_.start(image_,players_,transport_,ops_.bridge,send_failure,this))
        return defer_transport_cleanup(gen,Error::BridgeSetupFailed);
    if (!state_.activate(platform_manager_,players_,user_index_,local_id_))
        return fail(Error::StateSetupFailed);
    state_.set_ready(true);
    if (!lce_native_start::start_client(platform_manager_))
        return fail(Error::StartTransitionFailed);
    lock(); phase_=Phase::Active; unlock();
    if (!ops_.schedule_worker(ops_.context,worker_entry,this,gen))
        return fail(Error::SchedulerRejected);
    return true;
}

bool Coordinator::defer_transport_cleanup(uint64_t gen,Error e) {
    lock();
    if (gen!=generation_ || phase_!=Phase::Starting) { unlock(); return false; }
    phase_=Phase::CleanupPending;
    cleanup_error_=e;
    cleanup_queued_=true;
    unlock();
    if (ops_.schedule_worker(ops_.context,worker_entry,this,gen)) return false;
    lock();
    cleanup_queued_=false;
    error_=Error::SchedulerRejected;
    unlock();
    if (ops_.report_error) ops_.report_error(ops_.context,Error::SchedulerRejected);
    return false;
}

bool Coordinator::retry_cleanup() {
    uint64_t gen;
    lock();
    if (phase_!=Phase::CleanupPending || cleanup_queued_) { unlock(); return false; }
    cleanup_queued_=true;
    gen=generation_;
    unlock();
    if (ops_.schedule_worker(ops_.context,worker_entry,this,gen)) return true;
    lock(); cleanup_queued_=false; error_=Error::SchedulerRejected; unlock();
    if (ops_.report_error) ops_.report_error(ops_.context,Error::SchedulerRejected);
    return false;
}

void Coordinator::game_entry(void *ctx,uint64_t gen) {
    auto *self=static_cast<Coordinator *>(ctx);
    if (self) self->commit_game_thread(gen);
}

bool Coordinator::mark_gameplay_loaded(uint64_t gen) {
    lock();
    if (gen!=generation_ || (phase_!=Phase::Starting && phase_!=Phase::Active) || error_!=Error::None ||
        !state_.active()) { unlock(); return false; }
    state_.set_gameplay(true);
    unlock();
    return true;
}

void Coordinator::send_failure(void *ctx) {
    auto *self=static_cast<Coordinator *>(ctx);
    if (self) __atomic_store_n(&self->send_failed_,1,__ATOMIC_RELEASE);
}

bool Coordinator::cancel(uint64_t gen) {
    lock();
    if (gen!=generation_ || (phase_!=Phase::Connecting &&
        phase_!=Phase::AwaitingGameThread)) {
        error_=Error::CancelAfterStartUnsupported;
        unlock();
        if (ops_.report_error) ops_.report_error(ops_.context,Error::CancelAfterStartUnsupported);
        return false;
    }
    cancel_requested_=true;
    const bool waiting_ui=phase_==Phase::AwaitingGameThread;
    unlock();
    // A pending UI commit owns setup sequencing. Wake the worker so it can
    // close the not-yet-published transport; the queued UI entry will observe
    // cancellation and decline to commit.
    if (waiting_ui && !ops_.schedule_worker(ops_.context,worker_entry,this,gen)) {
        lock(); error_=Error::SchedulerRejected; unlock();
        if (ops_.report_error) ops_.report_error(ops_.context,Error::SchedulerRejected);
        // Keep the state pending so cancellation can be retried once the
        // worker mailbox accepts cleanup. The game-thread entry sees the
        // cancellation flag and will not publish players.
        return false;
    }
    return true;
}

Phase Coordinator::phase() const { lock(); Phase p=phase_; unlock(); return p; }
Error Coordinator::last_error() const { lock(); Error e=error_; unlock(); return e; }
uint64_t Coordinator::generation() const { lock(); auto g=generation_; unlock(); return g; }

bool Coordinator::send_game_packet(const uint8_t *packet, size_t size) {
    if (!packet || !size || size>4096) return false;
    lce_native_bridge *bridge=channel_.bridge();
    return bridge && lce_native_bridge_send(bridge,packet,size)!=0;
}

} // namespace lce_session_coordinator
