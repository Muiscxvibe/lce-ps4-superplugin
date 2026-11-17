#pragma once
#include "external_players.hpp"
#include "native_bridge.h"
#include "player_send_hook.h"
#include "player_vtable.hpp"

namespace lce_player_channel {
// Owns the sender hook and bridge for one custom connection. The owner must
// retain this object and its session, players, locks and callbacks until stop.
class Channel {
public:
    Channel()=default;
    Channel(const Channel &)=delete;
    Channel &operator=(const Channel &)=delete;
    // Call before the player becomes visible to any game thread. The server
    // must already have assigned a valid client ID. If sender binding fails
    // after bridge attachment, the connection is closed; the caller remains
    // responsible for retiring the player objects and any open connection.
    bool start(uintptr_t image, lce_external_players::Players &, lce_session_transport &,
               const lce_native_bridge_ops &, lce_send_failure_fn, void *failure_context);
    // Call only after all game/transport calls have drained and no new calls
    // may begin. Restores the player's native vptr and closes the transport;
    // the caller must subsequently retire the game socket and players.
    bool stop();
    lce_native_bridge *bridge() { return active_ ? &bridge_ : nullptr; }
private:
    lce_player_hook::Vtable table_;
    lce_native_bridge bridge_{};
    lce_player_send_binding binding_{};
    const uintptr_t *original_=nullptr;
    void *local_=nullptr;
    bool active_=false;
    bool attempted_=false;
};
}
