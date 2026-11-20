#include "session_runtime.hpp"
#include "socket_native_factory.hpp"
#include <fcntl.h>
#include <orbis/libkernel.h>

// OpenOrbis builds use a freestanding sysroot without <new>; provide the
// standard placement allocation primitive locally for process-lifetime Install.
inline void *operator new(size_t,void *storage) noexcept { return storage; }

/* netsync195 (weak): sends the LCE|ModIds HELLO once the server asked for it.
   Called on the UI/game thread, outside the bridge lock. */
extern "C" void lce_netsync_pump(int active,int (*send)(void *,const uint8_t *,size_t),void *ctx) __attribute__((weak));
static int netsync_send(void *ctx,const uint8_t *packet,size_t size) {
    return static_cast<lce_session_coordinator::Coordinator *>(ctx)->send_game_packet(packet,size) ? 1 : 0;
}

namespace lce_session_runtime {

bool Runtime::reject(Error e) { error_=e; return false; }

bool Runtime::load_server_list(lce_servers::List &out,char *buffer,size_t capacity,
        const char *path) {
    out.count=0;
    if (!buffer || capacity<lce_servers::kMaxFileBytes+1 || !path || !*path)
        return reject(Error::InvalidInput);
    int32_t fd=sceKernelOpen(path,O_RDONLY|O_NONBLOCK|O_NOFOLLOW,0);
    if (fd<0) return reject(Error::ConfigReadFailed);
    size_t used=0;
    bool read_ok=true;
    while (used<capacity) {
        const size_t remaining=capacity-used;
        const size_t got=sceKernelRead(fd,buffer+used,remaining);
        if (!got) break;
        if (got>remaining) { read_ok=false; break; }
        used+=got;
    }
    const int32_t close_result=sceKernelClose(fd);
    if (!read_ok || close_result<0 || used>lce_servers::kMaxFileBytes)
        return reject(Error::ConfigReadFailed);
    if (lce_servers::parse(buffer,used,&out)!=lce_servers::Ok || !out.count)
        return reject(Error::ConfigInvalid);
    // The runtime transport currently has an audited numeric IPv4 connect
    // path, with no DNS lookup. Refuse hostnames early instead of surfacing a
    // delayed connection failure after the player accepts the row.
    uint8_t ipv4[4];
    for (size_t i=0;i<out.count;++i)
        if (!lce_orbis_scheduler::Scheduler::resolve_ipv4(nullptr,out.entries[i].host,ipv4)) {
            out.count=0;
            return reject(Error::ConfigInvalid);
        }
    error_=Error::None;
    return true;
}

bool Runtime::configure(uintptr_t image,void *manager,
        const lce_servers::List &servers,const Config &config) {
    if (configured_ || !image || !manager || !servers.count ||
        servers.count>lce_servers::kMaxServers || !config.network_ready ||
        !config.input_buffer || !config.input_capacity || !config.output_buffer ||
        config.output_capacity<5 || !config.connect_timeout_ms)
        return reject(Error::InvalidInput);
    if (!scheduler_.running()) return reject(Error::SchedulerNotRunning);
    if (!config.network_ready(config.context)) return reject(Error::NetworkNotReady);
    if (image>UINTPTR_MAX-0x143b150 ||
        reinterpret_cast<uintptr_t>(manager)>UINTPTR_MAX-0x18 ||
        *reinterpret_cast<void **>(image+0x143b148)!=manager ||
        !*reinterpret_cast<void **>(reinterpret_cast<uintptr_t>(manager)+0x10))
        return reject(Error::HookOwnerMismatch);
    const uintptr_t owner_thread=reinterpret_cast<uintptr_t>(scePthreadSelf());
    if (!owner_thread) return reject(Error::InvalidInput);

    // Bind only after all caller, scheduler, server and network prerequisites
    // pass. These native bindings are one-shot process-lifetime operations.
    if (!lce_join_native::bind(image)) return reject(Error::NativeRowsRejected);
    config_=config;
    row_owner_thread_=owner_thread;
    image_=image;
    manager_=manager;
    network_ready_=true;
    session_={};

    lce_session_coordinator::Ops ops{};
    ops.context=this;
    ops.session_io=lce_orbis_session_io(&session_);
    ops.resolve_ipv4=resolve;
    ops.schedule_worker=schedule_worker;
    ops.schedule_game=schedule_game;
    ops.monotonic_ms=now_ms;
    ops.bridge.context=this;
    ops.bridge.lock=bridge_lock;
    ops.bridge.unlock=bridge_unlock;
    ops.bridge.push=bridge_push;
    // No proven game/transport quiescence callback exists yet. Keep native
    // socket retirement disabled rather than freeing live game objects.
    ops.retire_socket=retire_socket_unavailable;
    ops.report_error=report_session_error;
    ops.input_buffer=config.input_buffer;
    ops.input_capacity=config.input_capacity;
    ops.output_buffer=config.output_buffer;
    ops.output_capacity=config.output_capacity;
    ops.connect_timeout_ms=config.connect_timeout_ms;
    if (!coordinator_.configure(image,manager,servers,ops))
        return reject(Error::CoordinatorRejected);

    const auto callbacks=coordinator_.join_callbacks();
    const auto custom=coordinator_.query_callbacks();
    install_=new (install_storage_) lce_platform_hook::Install(
        image,manager,servers,lce_join_native::ops(),callbacks.ready,
        callbacks.join,callbacks.info,may_append_rows,this,custom);
    configured_=true;
    error_=Error::None;
    return true;
}

bool Runtime::run_ui_task(bool owned_game_tick) {
    if (!owned_game_tick) return false;
    const bool ran=scheduler_.run_ui_task();
    if (lce_netsync_pump)
        lce_netsync_pump(coordinator_.phase()==lce_session_coordinator::Phase::Active,netsync_send,&coordinator_);
    if (coordinator_.phase()==lce_session_coordinator::Phase::CleanupPending)
        (void)coordinator_.retry_cleanup();
    return ran;
}

bool Runtime::publish(const PublishEvidence &evidence) {
    if (!configured_ || published_ || !install_) return reject(Error::InvalidInput);
    if (!evidence.owned_game_thread || !evidence.process_lifetime || !may_append_rows(this) ||
        !scheduler_.running() || !network_ready_ || !coordinator_.ready())
        return reject(Error::PublicationGateClosed);
    if (*reinterpret_cast<void **>(image_+0x143b148)!=manager_)
        return reject(Error::HookOwnerMismatch);
    if (!install_->publish()) return reject(Error::PublishRejected);
    published_=true;
    error_=Error::None;
    return true;
}

Status Runtime::status() const {
    Status result{};
    result.error=error_;
    result.phase=coordinator_.phase();
    result.session_error=coordinator_.last_error();
    result.configured=configured_;
    result.published=published_;
    result.scheduler_running=scheduler_.running();
    result.network_ready=network_ready_;
    return result;
}

bool Runtime::resolve(void *,const char *host,uint8_t out[4]) {
    return lce_orbis_scheduler::Scheduler::resolve_ipv4(nullptr,host,out);
}
bool Runtime::schedule_worker(void *ctx,void (*task)(void *,uint64_t),void *arg,uint64_t gen) {
    auto *self=static_cast<Runtime *>(ctx);
    return self && lce_orbis_scheduler::Scheduler::schedule_worker(&self->scheduler_,task,arg,gen);
}
bool Runtime::schedule_game(void *ctx,void (*task)(void *,uint64_t),void *arg,uint64_t gen) {
    auto *self=static_cast<Runtime *>(ctx);
    return self && lce_orbis_scheduler::Scheduler::schedule_game(&self->scheduler_,task,arg,gen);
}
uint64_t Runtime::now_ms(void *) {
    return lce_orbis_scheduler::Scheduler::monotonic_ms(nullptr);
}
void Runtime::bridge_lock(void *ctx) {
    auto *self=static_cast<Runtime *>(ctx);
    if (!self) return;
    while (__atomic_test_and_set(&self->bridge_lock_,__ATOMIC_ACQUIRE)) {
        while (__atomic_load_n(&self->bridge_lock_,__ATOMIC_RELAXED)) {
#if defined(__x86_64__)
            __asm__ __volatile__("pause");
#endif
        }
    }
}
void Runtime::bridge_unlock(void *ctx) {
    auto *self=static_cast<Runtime *>(ctx);
    if (self) __atomic_clear(&self->bridge_lock_,__ATOMIC_RELEASE);
}
void Runtime::bridge_push(void *socket,const uint8_t *bytes,uint32_t size,int from_host) {
    lce_socket_native::push(socket,bytes,size,from_host);
}
bool Runtime::retire_socket_unavailable(void *,void *) { return false; }
bool Runtime::may_append_rows(void *ctx) {
    const auto *self=static_cast<const Runtime *>(ctx);
    return self && self->row_owner_thread_ &&
        reinterpret_cast<uintptr_t>(scePthreadSelf())==self->row_owner_thread_;
}
void Runtime::report_session_error(void *ctx,lce_session_coordinator::Error e) {
    auto *self=static_cast<Runtime *>(ctx);
    if (self && self->config_.report_error)
        self->config_.report_error(self->config_.context,e);
}

} // namespace lce_session_runtime
