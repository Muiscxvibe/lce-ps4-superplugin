#pragma once
#include <stddef.h>
#include <stdint.h>

namespace lce_join_hook {
// An immutable clone for the pinned 1.95 primary platform-manager vtable.
// Preparing it does not modify the game. The caller must own this object and
// its replacement functions for as long as any thread can dispatch to it.
// Query hooks are optional. An omitted hook leaves that native Sony entry in
// place, so callers can roll out session state one method at a time.
struct Replacements {
    uintptr_t join, list, full_info;
    uintptr_t player_count = 0;
    uintptr_t local_player = 0;
    uintptr_t player_by_small_id = 0;
    uintptr_t host_player = 0;
    uintptr_t is_host = 0;
    uintptr_t in_session = 0;
    uintptr_t in_gameplay = 0;
    uintptr_t ready_to_play_or_idle = 0;
};
class Vtable {
public:
    static constexpr size_t slots = 93;
    Vtable() = default;
    Vtable(const Vtable &) = delete;
    Vtable &operator=(const Vtable &) = delete;
    bool prepare(uintptr_t base, const uintptr_t *source_header, Replacements hooks);
    const uintptr_t *address_point() const { return prepared_ ? words_ + 2 : nullptr; }
private:
    uintptr_t words_[slots + 2]{};
    bool prepared_ = false;
};
}
