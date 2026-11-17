#include "external_session_state.hpp"

namespace lce_external_session {
void State::lock() const {
    while (__atomic_test_and_set(&lock_, __ATOMIC_ACQUIRE)) {
        while (__atomic_load_n(&lock_, __ATOMIC_RELAXED)) {
#if defined(__x86_64__)
            __asm__ __volatile__("pause");
#endif
        }
    }
}
void State::unlock() const { __atomic_clear(&lock_, __ATOMIC_RELEASE); }
bool State::active() const { Guard guard(*this); return active_; }
bool State::valid() const {
    return manager_ && players_ && players_->local() && players_->host() &&
           players_->socket() && local_user_ >= 0 && local_user_ < 4 &&
           local_id_ != 0 && local_id_ != 0xff;
}
bool State::matches(void *manager) const {
    return active_ && manager_==manager && valid();
}
bool State::activate(void *manager, lce_external_players::Players &players,
                     int local_user, uint8_t local_id) {
    Guard guard(*this);
    if (active_ || !manager || local_user < 0 || local_user >= 4 ||
        local_id == 0 || local_id == 0xff || !players.local() ||
        !players.host() || !players.socket()) return false;
    manager_ = manager;
    players_ = &players;
    local_user_ = local_user;
    local_id_ = local_id;
    connected_ = true;
    ready_ = gameplay_ = false;
    active_ = true;
    return true;
}
void State::set_ready(bool ready) {
    Guard guard(*this);
    if (active_ && !connected_) return;
    ready_ = active_ && connected_ && valid() && ready;
    if (!ready_) gameplay_ = false;
}
void State::set_gameplay(bool gameplay) {
    Guard guard(*this);
    gameplay_ = active_ && connected_ && ready_ && valid() && gameplay;
}
void State::mark_disconnected() {
    Guard guard(*this);
    if (!active_ || !valid()) return;
    connected_=false;
    ready_=true;
    gameplay_=false;
}
void State::deactivate() {
    Guard guard(*this);
    active_ = connected_ = ready_ = gameplay_ = false;
    manager_ = nullptr;
    players_ = nullptr;
    local_user_ = -1;
    local_id_ = 0xff;
}
lce_session_queries::Custom State::query_callbacks() {
    return {this,active_cb,count_cb,local_cb,small_id_cb,host_cb,host_flag_cb,
            session_cb,gameplay_cb,ready_cb};
}
bool State::active_cb(void *ctx, void *manager) {
    auto *s=static_cast<State *>(ctx);
    if (!s) return false;
    Guard guard(*s);
    return s->matches(manager);
}
int State::count_cb(void *ctx, void *manager) {
    auto *s=static_cast<State *>(ctx);
    if (!s) return 0;
    Guard guard(*s);
    return s->matches(manager) ? 2 : 0;
}
void *State::local_cb(void *ctx, void *manager, int user) {
    auto *s=static_cast<State *>(ctx);
    if (!s) return nullptr;
    Guard guard(*s);
    return s->matches(manager) && user==s->local_user_ ? s->players_->local() : nullptr;
}
void *State::small_id_cb(void *ctx, void *manager, uint8_t id) {
    auto *s=static_cast<State *>(ctx);
    if (!s) return nullptr;
    Guard guard(*s);
    if (!s->matches(manager)) return nullptr;
    if (id==s->local_id_) return s->players_->local();
    if (id==0) return s->players_->host();
    return nullptr;
}
void *State::host_cb(void *ctx, void *manager) {
    auto *s=static_cast<State *>(ctx);
    if (!s) return nullptr;
    Guard guard(*s);
    return s->matches(manager) ? s->players_->host() : nullptr;
}
bool State::host_flag_cb(void *, void *) { return false; }
bool State::session_cb(void *ctx, void *manager) {
    auto *s=static_cast<State *>(ctx);
    if (!s) return false;
    Guard guard(*s);
    return s->matches(manager) && s->connected_;
}
bool State::gameplay_cb(void *ctx, void *manager) {
    auto *s=static_cast<State *>(ctx);
    if (!s) return false;
    Guard guard(*s);
    return s->matches(manager) && s->gameplay_;
}
bool State::ready_cb(void *ctx, void *manager) {
    auto *s=static_cast<State *>(ctx);
    if (!s) return false;
    Guard guard(*s);
    return s->matches(manager) && s->ready_;
}
}
