#pragma once
#include "join_vtable.hpp"
#include <stdint.h>

namespace lce_session_queries {
// Native entries are the original function pointers copied from the Sony
// vtable before the cloned vtable is published. The dispatcher never modifies
// those functions or their objects.
struct Native {
    int (*player_count)(void *manager);
    void *(*local_player)(void *manager, int user_index);
    void *(*player_by_small_id)(void *manager, uint8_t small_id);
    void *(*host_player)(void *manager);
    bool (*is_host)(void *manager);
    bool (*in_session)(void *manager);
    bool (*in_gameplay)(void *manager);
    bool (*ready_to_play_or_idle)(void *manager);
};

// Custom callbacks share `context`; each callback receives the platform
// manager as well so integrations can validate the active manager identity.
struct Custom {
    void *context;
    bool (*active)(void *context, void *manager);
    int (*player_count)(void *context, void *manager);
    void *(*local_player)(void *context, void *manager, int user_index);
    void *(*player_by_small_id)(void *context, void *manager, uint8_t small_id);
    void *(*host_player)(void *context, void *manager);
    bool (*is_host)(void *context, void *manager);
    bool (*in_session)(void *context, void *manager);
    bool (*in_gameplay)(void *context, void *manager);
    bool (*ready_to_play_or_idle)(void *context, void *manager);
};

class Dispatcher {
public:
    Dispatcher(const Native &native, const Custom &custom);
    // One live primary platform manager exists in the game. Bind before
    // publishing the cloned vtable and retain this dispatcher for its lifetime.
    bool bind();
    // Only before publication or after restoring all original vptrs and
    // draining every hook call.
    bool unbind();
    bool replacements(lce_join_hook::Replacements *out) const;

    int player_count(void *manager) const;
    void *local_player(void *manager, int user_index) const;
    void *player_by_small_id(void *manager, uint8_t small_id) const;
    void *host_player(void *manager) const;
    bool is_host(void *manager) const;
    bool in_session(void *manager) const;
    bool in_gameplay(void *manager) const;
    bool ready_to_play_or_idle(void *manager) const;

private:
    bool active(void *manager) const;
    Native native_;
    Custom custom_;
};
}
