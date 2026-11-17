#pragma once

#include "join_native_ops.hpp"
#include "orbis_scheduler.hpp"
#include "session_coordinator.hpp"

namespace lce_session_runtime {

enum class Error : unsigned {
    None, InvalidInput, ConfigReadFailed, ConfigInvalid,
    SchedulerNotRunning, NetworkNotReady,
    NativeRowsRejected, CoordinatorRejected, HookOwnerMismatch,
    InstallConstructionFailed, PublicationGateClosed, PublishRejected
};

struct Config {
    void *context=nullptr;
    // The game/plugin owner must establish that libSceNet is initialized and
    // usable before runtime configuration. The runtime does not initialize
    // or unload the process-wide network library.
    bool (*network_ready)(void *)=nullptr;
    // May be called on the transport worker or game/UI thread; keep it
    // thread-safe and queue any UI presentation to the owner thread.
    void (*report_error)(void *,lce_session_coordinator::Error)=nullptr;
    uint8_t *input_buffer=nullptr;
    size_t input_capacity=0;
    uint8_t *output_buffer=nullptr;
    size_t output_capacity=0;
    uint32_t connect_timeout_ms=10000;
};

// The caller must run this on the owner thread and keep the runtime, callback
// owners and both vtables alive until process exit. A call already executing
// through the original vtable may finish through that unchanged table.
// This switch does not prove higher-level concurrent game behavior is safe.
struct PublishEvidence {
    bool owned_game_thread=false;
    bool process_lifetime=false;
};

struct Status {
    Error error=Error::None;
    lce_session_coordinator::Phase phase=lce_session_coordinator::Phase::Unconfigured;
    lce_session_coordinator::Error session_error=lce_session_coordinator::Error::None;
    bool configured=false;
    bool published=false;
    bool scheduler_running=false;
    bool network_ready=false;
};

// Process-lifetime runtime owner. The caller owns and starts the Scheduler,
// parses server configuration, supplies buffers, and keeps this object alive
// after publication. No hook is published by configure(). There is no unload
// or post-start retry path because native player teardown is not established.
class Runtime {
public:
    static constexpr const char *server_config_path="/data/lce-servers.conf";
    explicit Runtime(lce_orbis_scheduler::Scheduler &scheduler) : scheduler_(scheduler) {}
    Runtime(const Runtime &)=delete;
    Runtime &operator=(const Runtime &)=delete;

    // Reads only a bounded user-owned file and parses the documented
    // label|numeric-IPv4|port format. DNS names are rejected by the runtime's
    // resolver even though the general host parser can represent them.
    bool load_server_list(lce_servers::List &out,char *buffer,size_t capacity,
                          const char *path=server_config_path);
    bool configure(uintptr_t image,void *platform_manager,
                   const lce_servers::List &servers,
                   const Config &config);
    bool run_ui_task(bool owned_game_tick);
    bool publish(const PublishEvidence &evidence);
    bool configured() const { return configured_; }
    // Call on the game/UI owner thread; configuration/publication status is
    // mutated there. Coordinator transport and session status are synchronized.
    Status status() const;
    lce_session_coordinator::Coordinator &coordinator() { return coordinator_; }

private:
    static bool resolve(void *,const char *,uint8_t out[4]);
    static bool schedule_worker(void *,void (*)(void *,uint64_t),void *,uint64_t);
    static bool schedule_game(void *,void (*)(void *,uint64_t),void *,uint64_t);
    static uint64_t now_ms(void *);
    static void bridge_lock(void *);
    static void bridge_unlock(void *);
    static void bridge_push(void *,const uint8_t *,uint32_t,int);
    static bool retire_socket_unavailable(void *,void *);
    static bool may_append_rows(void *);
    static void report_session_error(void *,lce_session_coordinator::Error);
    bool reject(Error);

    lce_orbis_scheduler::Scheduler &scheduler_;
    lce_orbis_session session_{};
    lce_session_coordinator::Coordinator coordinator_{};
    alignas(lce_platform_hook::Install)
        unsigned char install_storage_[sizeof(lce_platform_hook::Install)]{};
    lce_platform_hook::Install *install_=nullptr;
    Config config_{};
    uintptr_t image_=0;
    void *manager_=nullptr;
    unsigned char bridge_lock_=0;
    uintptr_t row_owner_thread_=0;
    Error error_=Error::None;
    bool configured_=false;
    bool published_=false;
    bool network_ready_=false;
};

} // namespace lce_session_runtime
