#pragma once

#include "external_players_native.hpp"
#include "external_session_state.hpp"
#include "native_bridge.h"
#include "native_start_transition.hpp"
#include "orbis_session.h"
#include "platform_hook_install.hpp"
#include "player_channel.hpp"
#include "auth_client.h"

namespace lce_session_coordinator {

enum class Phase : unsigned {
    Unconfigured, Idle, Connecting, AwaitingGameThread, CleanupPending, Starting, Active,
    Failed, FailedRetained, Cancelled
};

enum class Error : unsigned {
    None, UnsupportedLocalPlayers, UnknownServer, SchedulerRejected, ResolveFailed,
    TransportStartFailed, TransportFailed, NativePlayerSetupFailed,
    BridgeSetupFailed, StateSetupFailed, StartTransitionFailed,
    SendFailed, CancelledBeforeStart, CancelAfterStartUnsupported
};

// Scheduling functions must enqueue and return promptly. worker_entry must
// run on one worker thread for one worker_step(); the coordinator re-enqueues
// it while transport work remains. game_entry must run on the game's UI/game thread and call
// commit_game_thread(). No scheduler callback may invoke its entry inline.
// `resolve_ipv4` must resolve a configured host to four network-order octets.
// The caller supplies all storage and callbacks; their contexts and buffers,
// this object, and the Install that consumes its callbacks must remain alive
// for the process lifetime after hooks are published. Connect failures and
// cancellations before player creation return to Idle and can be retried;
// failures after native player/channel setup remain retained and terminal
// because safe teardown is not established.
struct Ops {
    void *context;
    lce_session_io session_io;
    bool (*resolve_ipv4)(void *, const char *, uint8_t out[4]);
    bool (*schedule_worker)(void *, void (*entry)(void *, uint64_t),
                            void *entry_context, uint64_t generation);
    bool (*schedule_game)(void *, void (*entry)(void *, uint64_t),
                          void *entry_context, uint64_t generation);
    uint64_t (*monotonic_ms)(void *);
    lce_native_bridge_ops bridge;
    bool (*retire_socket)(void *local_wrapper, void *socket);
    // May run on worker or game thread. Must be thread-safe and must not
    // destroy coordinator/session/player state inline. After native setup,
    // failures retain objects because no safe native leave path is known.
    void (*report_error)(void *, Error);
    uint8_t *input_buffer;
    size_t input_capacity;
    uint8_t *output_buffer;
    size_t output_capacity;
    uint32_t connect_timeout_ms;
};

struct JoinCallbacks {
    bool (*ready)();
    int (*join)(const lce_servers::Server &, int local_mask, int primary_user);
    void (*info)(const lce_servers::Server &, void *,
                 lce_join_adapter::Complete, void *);
};

// Native player construction currently supports one local user only.
inline bool supports_player_request(int mask,int user) {
    return user>=0 && user<4 && mask==(1<<user);
}

class Coordinator {
public:
    // Sends one complete serverbound game packet on the active session
    // (used by the plugin to forward debug actions). Thread-safe.
    bool send_game_packet(const uint8_t *packet, size_t size);
    Coordinator() = default;
    Coordinator(const Coordinator &) = delete;
    Coordinator &operator=(const Coordinator &) = delete;

    // Bind native factories and the validated STARTING transition once. This
    // does not install or publish the platform vtable hooks.
    bool configure(uintptr_t image, void *platform_manager,
                   const lce_servers::List &servers, const Ops &ops);
    bool ready() const;
    int join(const lce_servers::Server &, int local_mask, int primary_user);
    // The caller supplies a successful HTTPS account response for this one
    // connection. The gateway ticket is consumed by the transport worker.
    int join_authenticated(const lce_servers::Server &,int local_mask,int primary_user,
                           const lce_auth_result &);
    // UI thread only. Run the validated retail join setup and deliver the
    // staged ticket to the custom backend during its synchronous callback.
    int join_authenticated_native(const lce_servers::Server &,int primary_user,
                                  const lce_auth_result &);
    JoinCallbacks join_callbacks();
    lce_session_queries::Custom query_callbacks();

    // Called by injected scheduler entry points. worker_step owns transport
    // calls; after it sees the assigned ID it schedules one game-thread commit
    // and returns false so no further transport I/O races with setup.
    bool worker_step(uint64_t generation);
    bool commit_game_thread(uint64_t generation);
    // Call only from an authenticated post-load success signal (the pinned
    // RunNetworkGame path reaches app.SetGameStarted(true) after loading).
    // Commit does not set gameplay itself.
    bool mark_gameplay_loaded(uint64_t generation);
    bool cancel(uint64_t generation);
    bool retry_cleanup();
    Phase phase() const;
    Error last_error() const;
    uint64_t generation() const;

private:
    friend struct CoordinatorTestAccess;
    static Coordinator *active_;
    static bool backend_ready();
    static int backend_join(const lce_servers::Server &, int, int);
    static void backend_info(const lce_servers::Server &, void *,
                             lce_join_adapter::Complete, void *);
    static void worker_entry(void *, uint64_t);
    static void game_entry(void *, uint64_t);
    static void send_failure(void *);
    static int frame(void *, const uint8_t *, size_t);
    bool recover_retained();
    bool fail(Error);
    bool defer_transport_cleanup(uint64_t,Error);
    bool is_server(const lce_servers::Server &) const;
    int begin_join(const lce_servers::Server &,int,int,const lce_auth_result *);
    int native_backend_join(const lce_servers::Server &,int,int);
    void lock() const;
    void unlock() const;

    uintptr_t image_=0;
    void *platform_manager_=nullptr;
    void *sqr_manager_=nullptr;
    lce_servers::List servers_{};
    Ops ops_{};
    lce_session_transport transport_{};
    lce_player_channel::Channel channel_{};
    lce_external_players::Players players_{};
    lce_external_session::State state_{};
    lce_session_coordinator::Phase phase_=Phase::Unconfigured;
    lce_session_coordinator::Error error_=Error::None;
    lce_servers::Server selected_{};
    uint8_t ip_[4]{};
    uint8_t local_id_=0;
    int user_index_=-1;
    uint64_t generation_=0;
    bool cancel_requested_=false;
    bool transport_started_=false;
    bool cleanup_queued_=false;
    bool authenticated_=false;
    char ticket_id_[33]{},ticket_key_[65]{};
    lce_auth_result staged_account_{};
    lce_servers::Server staged_server_{};
    int staged_user_=-1;
    bool staged_=false;
    Error cleanup_error_=Error::None;
    uint8_t send_failed_=0;
    mutable unsigned char lock_=0;
};

} // namespace lce_session_coordinator
