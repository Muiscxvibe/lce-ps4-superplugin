#include "join_vtable.hpp"
// Optional diagnostic: when the plugin links a tracer, every slot this clone
// does not replace is routed through a pass-through thunk that records calls.
extern "C" __attribute__((weak)) void lce_vtable_trace_install(uintptr_t *slots, unsigned count, const unsigned char *replaced);
namespace lce_join_hook {
namespace {
constexpr uintptr_t expected[Vtable::slots] = {
 0xddf8e0,0xddf980,0xddf990,0xddf9a0,0xddfa60,0xddfca0,0xddfcc0,0xddfcd0,
 0xddfce0,0xddfcf0,0xde1280,0xde0fd0,0xde1000,0xde1030,0xde1080,0xddfcb0,
 0xde10b0,0xde10e0,0xde1110,0xddfe80,0xde1190,0xde11a0,0xde11c0,0xddfe20,
 0xddfe40,0xddfe50,0xddfe30,0xddfe60,0xddfe70,0xddfff0,0xde0450,0xde04e0,
 0xde05c0,0xde05d0,0xdde8b0,0xde0600,0xdde8c0,0xdde8d0,0xdde8e0,0xde0610,
 0xde0630,0xde0660,0xde0670,0xddffe0,0xde0100,0xde0430,0xde0710,0xde0830,
 0xde0890,0xde0a30,0xde0af0,0xde0b70,0xde0bf0,0xde0c30,0xde1140,0xde1150,
 0xde1160,0xde1180,0xde0c70,0xde0f00,0xde0f10,0xde0f20,0xde0f50,0xde0f90,
 0xde12a0,0xde1270,0xde11d0,0xde11e0,0xde1210,0xde1200,0xde1230,0xde1220,
 0xde11f0,0xde1240,0xde1250,0xde1260,0xde1580,0xde1590,0xde1290,0xde1360,
 0xde1370,0xde1380,0xde1390,0xddea50,0xddeaf0,0xddeec0,0xdde8f0,0xddf2d0,
 0xddf880,0xddf8a0,0xddf8c0,0xde15a0,0xde1670
};
}
bool Vtable::prepare(uintptr_t base, const uintptr_t *source, Replacements hooks) {
    if (prepared_ || !base || base > UINTPTR_MAX - 0x134a120 || !source ||
        !hooks.join || !hooks.list || !hooks.full_info) return false;
    uintptr_t snapshot[slots + 2];
    for (size_t i = 0; i < slots + 2; ++i) snapshot[i] = source[i];
    if (snapshot[0] != 0 || snapshot[1] != base + 0x134a120) return false;
    for (size_t i = 0; i < slots; ++i)
        if (snapshot[i + 2] != base + expected[i]) return false;
    for (size_t i = 0; i < slots + 2; ++i) words_[i] = snapshot[i];
    words_[2 + 30] = hooks.join;
    words_[2 + 58] = hooks.list;
    words_[2 + 61] = hooks.full_info;
    // Slot numbers are verified against the pinned 1.95 primary vtable and
    // the matching PlatformNetworkManagerSony virtual declarations.
    if (hooks.player_count) words_[2 + 5] = hooks.player_count;
    if (hooks.local_player) words_[2 + 11] = hooks.local_player;
    if (hooks.player_by_small_id) words_[2 + 14] = hooks.player_by_small_id;
    if (hooks.host_player) words_[2 + 16] = hooks.host_player;
    if (hooks.is_host) words_[2 + 17] = hooks.is_host;
    if (hooks.in_session) words_[2 + 20] = hooks.in_session;
    if (hooks.in_gameplay) words_[2 + 21] = hooks.in_gameplay;
    if (hooks.ready_to_play_or_idle) words_[2 + 22] = hooks.ready_to_play_or_idle;
    if (hooks.quick_match) words_[2 + 31] = hooks.quick_match;
    if (lce_vtable_trace_install) {
        unsigned char replaced[slots];
        for (size_t i = 0; i < slots; ++i) replaced[i] = words_[i + 2] != snapshot[i + 2];
        lce_vtable_trace_install(words_ + 2, slots, replaced);
    }
    prepared_ = true;
    return true;
}
}
