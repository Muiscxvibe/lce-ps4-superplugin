#include "session_query_dispatch.hpp"

namespace lce_session_queries {
namespace {
Dispatcher *bound_dispatcher;

int player_count_hook(void *manager) {
    auto *d=__atomic_load_n(&bound_dispatcher,__ATOMIC_ACQUIRE);
    return d ? d->player_count(manager) : 0;
}
void *local_player_hook(void *manager, int index) {
    auto *d=__atomic_load_n(&bound_dispatcher,__ATOMIC_ACQUIRE);
    return d ? d->local_player(manager, index) : nullptr;
}
void *player_by_small_id_hook(void *manager, uint8_t id) {
    auto *d=__atomic_load_n(&bound_dispatcher,__ATOMIC_ACQUIRE);
    return d ? d->player_by_small_id(manager, id) : nullptr;
}
void *host_player_hook(void *manager) {
    auto *d=__atomic_load_n(&bound_dispatcher,__ATOMIC_ACQUIRE);
    return d ? d->host_player(manager) : nullptr;
}
bool is_host_hook(void *manager) {
    auto *d=__atomic_load_n(&bound_dispatcher,__ATOMIC_ACQUIRE);
    return d && d->is_host(manager);
}
bool in_session_hook(void *manager) {
    auto *d=__atomic_load_n(&bound_dispatcher,__ATOMIC_ACQUIRE);
    return d && d->in_session(manager);
}
bool in_gameplay_hook(void *manager) {
    auto *d=__atomic_load_n(&bound_dispatcher,__ATOMIC_ACQUIRE);
    return d && d->in_gameplay(manager);
}
bool ready_hook(void *manager) {
    auto *d=__atomic_load_n(&bound_dispatcher,__ATOMIC_ACQUIRE);
    return d && d->ready_to_play_or_idle(manager);
}
template <typename T> uintptr_t address(T function) {
    return reinterpret_cast<uintptr_t>(function);
}
}

Dispatcher::Dispatcher(const Native &native, const Custom &custom)
    : native_(native), custom_(custom) {}

bool Dispatcher::bind() {
    Dispatcher *empty=nullptr;
    if (__atomic_compare_exchange_n(&bound_dispatcher,&empty,this,false,__ATOMIC_RELEASE,__ATOMIC_RELAXED)) return true;
    return empty==this;
}
bool Dispatcher::unbind() {
    Dispatcher *expected=this;
    return __atomic_compare_exchange_n(&bound_dispatcher,&expected,nullptr,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE);
}

bool Dispatcher::replacements(lce_join_hook::Replacements *out) const {
    if (!out || __atomic_load_n(&bound_dispatcher,__ATOMIC_ACQUIRE) != this) return false;
    out->player_count = address(&player_count_hook);
    out->local_player = address(&local_player_hook);
    out->player_by_small_id = address(&player_by_small_id_hook);
    out->host_player = address(&host_player_hook);
    out->is_host = address(&is_host_hook);
    out->in_session = address(&in_session_hook);
    out->in_gameplay = address(&in_gameplay_hook);
    out->ready_to_play_or_idle = address(&ready_hook);
    return true;
}

bool Dispatcher::active(void *manager) const {
    return manager && custom_.active && custom_.active(custom_.context, manager);
}
int Dispatcher::player_count(void *manager) const {
    return active(manager) && custom_.player_count
        ? custom_.player_count(custom_.context, manager)
        : native_.player_count ? native_.player_count(manager) : 0;
}
void *Dispatcher::local_player(void *manager, int index) const {
    return active(manager) && custom_.local_player
        ? custom_.local_player(custom_.context, manager, index)
        : native_.local_player ? native_.local_player(manager, index) : nullptr;
}
void *Dispatcher::player_by_small_id(void *manager, uint8_t id) const {
    return active(manager) && custom_.player_by_small_id
        ? custom_.player_by_small_id(custom_.context, manager, id)
        : native_.player_by_small_id ? native_.player_by_small_id(manager, id) : nullptr;
}
void *Dispatcher::host_player(void *manager) const {
    return active(manager) && custom_.host_player
        ? custom_.host_player(custom_.context, manager)
        : native_.host_player ? native_.host_player(manager) : nullptr;
}
bool Dispatcher::is_host(void *manager) const {
    return active(manager) && custom_.is_host
        ? custom_.is_host(custom_.context, manager)
        : native_.is_host ? native_.is_host(manager) : false;
}
bool Dispatcher::in_session(void *manager) const {
    return active(manager) && custom_.in_session
        ? custom_.in_session(custom_.context, manager)
        : native_.in_session ? native_.in_session(manager) : false;
}
bool Dispatcher::in_gameplay(void *manager) const {
    return active(manager) && custom_.in_gameplay
        ? custom_.in_gameplay(custom_.context, manager)
        : native_.in_gameplay ? native_.in_gameplay(manager) : false;
}
bool Dispatcher::ready_to_play_or_idle(void *manager) const {
    return active(manager) && custom_.ready_to_play_or_idle
        ? custom_.ready_to_play_or_idle(custom_.context, manager)
        : native_.ready_to_play_or_idle ? native_.ready_to_play_or_idle(manager) : false;
}
}
