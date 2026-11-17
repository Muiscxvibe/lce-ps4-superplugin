#include "external_session_state.hpp"
#include <cassert>
#include <cstdint>
#include <thread>

namespace {
int next_handle;
void *create_player(void *, bool local, int, uint8_t, const void *) {
    return reinterpret_cast<void *>(static_cast<uintptr_t>(++next_handle + (local?0x100:0x200)));
}
void destroy(void *) {}
void *wrap(void *p) { return reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(p)+0x1000); }
void *make_socket(void *p) { return reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(p)+0x2000); }
bool retire_socket(void *, void *) { return true; }
int native_count(void *) { return 9; }
void *native_local(void *, int) { return reinterpret_cast<void *>(0x91); }
void *native_small(void *, uint8_t) { return reinterpret_cast<void *>(0x92); }
void *native_host(void *) { return reinterpret_cast<void *>(0x93); }
bool native_no(void *) { return false; }
}

int main() {
    using namespace lce_external_players;
    using namespace lce_external_session;
    Ops player_ops{create_player,destroy,wrap,destroy,make_socket,retire_socket};
    int manager, other_manager, local_uid=0, host_uid=0;
    Players players;
    assert(players.prepare(&manager,1,7,&local_uid,&host_uid,player_ops));

    State state;
    auto custom=state.query_callbacks();
    lce_session_queries::Native native{native_count,native_local,native_small,native_host,
                                       native_no,native_no,native_no,native_no};
    lce_session_queries::Dispatcher dispatch(native,custom);

    // Before activation the dispatcher remains on the native Sony methods.
    assert(dispatch.player_count(&manager)==9);
    assert(dispatch.local_player(&manager,1)==reinterpret_cast<void *>(0x91));
    assert(!state.activate(nullptr,players,1,7));
    assert(!state.activate(&manager,players,1,0));
    assert(!state.activate(&manager,players,1,0xff));
    assert(state.activate(&manager,players,1,7));

    assert(dispatch.player_count(&manager)==2);
    assert(dispatch.local_player(&manager,1)==players.local());
    assert(dispatch.local_player(&manager,0)==nullptr);
    assert(dispatch.player_by_small_id(&manager,7)==players.local());
    assert(dispatch.player_by_small_id(&manager,0)==players.host());
    assert(dispatch.player_by_small_id(&manager,8)==nullptr);
    assert(dispatch.host_player(&manager)==players.host());
    assert(!dispatch.is_host(&manager));
    assert(dispatch.in_session(&manager));
    assert(!dispatch.ready_to_play_or_idle(&manager));
    assert(!dispatch.in_gameplay(&manager));

    // Manager identity is part of the activation gate, so unrelated native
    // sessions continue through their original methods.
    assert(dispatch.player_count(&other_manager)==9);
    assert(dispatch.local_player(&other_manager,1)==reinterpret_cast<void *>(0x91));
    state.set_gameplay(true); // cannot jump over the not-ready state
    assert(!dispatch.in_gameplay(&manager));
    state.set_ready(true);
    assert(dispatch.ready_to_play_or_idle(&manager));
    state.set_gameplay(true);
    assert(dispatch.in_gameplay(&manager));
    state.set_ready(false);
    assert(!dispatch.ready_to_play_or_idle(&manager));
    assert(!dispatch.in_gameplay(&manager));

    // The loading thread may query while the owner changes session state.
    // Players stay alive until both threads have finished.
    std::thread reader([&] {
        for (int i=0; i<20000; ++i) {
            const int count=dispatch.player_count(&manager);
            assert(count==0 || count==2 || count==9);
            (void)dispatch.local_player(&manager,1);
            (void)dispatch.ready_to_play_or_idle(&manager);
        }
    });
    for (int i=0; i<20000; ++i) {
        state.set_ready(true);
        state.set_gameplay(true);
        state.deactivate();
        assert(state.activate(&manager,players,1,7));
    }
    reader.join();

    state.mark_disconnected();
    assert(!dispatch.in_session(&manager));
    assert(dispatch.ready_to_play_or_idle(&manager));
    assert(!dispatch.in_gameplay(&manager));
    assert(dispatch.local_player(&manager,1)==players.local());
    state.set_ready(false);
    state.set_gameplay(true);
    assert(dispatch.ready_to_play_or_idle(&manager));
    assert(!dispatch.in_gameplay(&manager));

    state.deactivate();
    assert(!dispatch.in_session(&manager));
    assert(dispatch.player_count(&manager)==9);
    assert(dispatch.host_player(&manager)==reinterpret_cast<void *>(0x93));
    assert(players.retire());
    return 0;
}
