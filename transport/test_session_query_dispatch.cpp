#include "session_query_dispatch.hpp"
#include <cassert>
#include <cstdint>

namespace {
struct State { bool active; int custom_count; int native_calls; int custom_calls; } state;
int native_count(void *) { ++state.native_calls; return 3; }
void *native_local(void *, int) { ++state.native_calls; return reinterpret_cast<void *>(0x31); }
void *native_small(void *, uint8_t) { ++state.native_calls; return reinterpret_cast<void *>(0x32); }
void *native_host(void *) { ++state.native_calls; return reinterpret_cast<void *>(0x33); }
bool native_false(void *) { ++state.native_calls; return false; }
bool custom_active(void *ctx, void *) { return static_cast<State *>(ctx)->active; }
int custom_count(void *ctx, void *) { auto *s=static_cast<State *>(ctx); ++s->custom_calls; return s->custom_count; }
void *custom_local(void *ctx, void *, int) { ++static_cast<State *>(ctx)->custom_calls; return reinterpret_cast<void *>(0x41); }
void *custom_small(void *ctx, void *, uint8_t) { ++static_cast<State *>(ctx)->custom_calls; return reinterpret_cast<void *>(0x42); }
void *custom_host(void *ctx, void *) { ++static_cast<State *>(ctx)->custom_calls; return reinterpret_cast<void *>(0x43); }
bool custom_true(void *ctx, void *) { ++static_cast<State *>(ctx)->custom_calls; return true; }
}

int main() {
    using namespace lce_session_queries;
    Native native{native_count,native_local,native_small,native_host,
                  native_false,native_false,native_false,native_false};
    Custom custom{&state,custom_active,custom_count,custom_local,custom_small,
                  custom_host,custom_true,custom_true,custom_true,custom_true};
    Dispatcher dispatcher(native,custom);
    assert(dispatcher.bind());
    lce_join_hook::Replacements hooks{1,2,3};
    assert(dispatcher.replacements(&hooks));
    assert(hooks.player_count && hooks.local_player && hooks.player_by_small_id &&
           hooks.host_player && hooks.is_host && hooks.in_session &&
           hooks.in_gameplay && hooks.ready_to_play_or_idle);

    int manager;
    state.active=false;
    assert(dispatcher.player_count(&manager)==3);
    assert(dispatcher.local_player(&manager,0)==reinterpret_cast<void *>(0x31));
    assert(dispatcher.player_by_small_id(&manager,7)==reinterpret_cast<void *>(0x32));
    assert(dispatcher.host_player(&manager)==reinterpret_cast<void *>(0x33));
    assert(!dispatcher.is_host(&manager) && !dispatcher.in_session(&manager) &&
           !dispatcher.in_gameplay(&manager) && !dispatcher.ready_to_play_or_idle(&manager));
    assert(state.native_calls==8 && state.custom_calls==0);

    state.active=true; state.custom_count=2;
    assert(dispatcher.player_count(&manager)==2);
    assert(dispatcher.local_player(&manager,0)==reinterpret_cast<void *>(0x41));
    assert(dispatcher.player_by_small_id(&manager,7)==reinterpret_cast<void *>(0x42));
    assert(dispatcher.host_player(&manager)==reinterpret_cast<void *>(0x43));
    assert(dispatcher.is_host(&manager) && dispatcher.in_session(&manager) &&
           dispatcher.in_gameplay(&manager) && dispatcher.ready_to_play_or_idle(&manager));
    assert(state.native_calls==8 && state.custom_calls==8);

    using CountHook = int (*)(void *);
    using PlayerHook = void *(*)(void *, int);
    using SmallIdHook = void *(*)(void *, uint8_t);
    using HostPlayerHook = void *(*)(void *);
    using BoolHook = bool (*)(void *);
    assert(reinterpret_cast<CountHook>(hooks.player_count)(&manager)==2);
    assert(reinterpret_cast<PlayerHook>(hooks.local_player)(&manager,0)==reinterpret_cast<void *>(0x41));
    assert(reinterpret_cast<SmallIdHook>(hooks.player_by_small_id)(&manager,7)==reinterpret_cast<void *>(0x42));
    assert(reinterpret_cast<HostPlayerHook>(hooks.host_player)(&manager)==reinterpret_cast<void *>(0x43));
    assert(reinterpret_cast<BoolHook>(hooks.is_host)(&manager));
    assert(reinterpret_cast<BoolHook>(hooks.in_session)(&manager));
    assert(reinterpret_cast<BoolHook>(hooks.in_gameplay)(&manager));
    assert(reinterpret_cast<BoolHook>(hooks.ready_to_play_or_idle)(&manager));

    // Invalid manager and absent custom callback retain Sony behavior.
    assert(dispatcher.player_count(nullptr)==3);
    return 0;
}
