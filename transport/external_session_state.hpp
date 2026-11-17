#pragma once
#include "external_players.hpp"
#include "session_query_dispatch.hpp"

namespace lce_external_session {
// Non-owning view over Players. The owner must call deactivate() before
// retiring/freeing Players, and must retain this object while hooks can run.
// Deactivation does not revoke pointers already returned by a query: the
// owner must also drain game users before retiring any player or socket.
class State {
public:
    State() = default;
    State(const State &) = delete;
    State &operator=(const State &) = delete;

    // Activate only after Players::prepare() succeeds and the transport has
    // established its client connection. local_id is the server-assigned ID.
    bool activate(void *manager, lce_external_players::Players &players,
                  int local_user, uint8_t local_id);
    void set_ready(bool ready);
    void set_gameplay(bool gameplay);
    // Preserve the custom player pointers while making the loading worker's
    // readiness check finish and its in-session check fail. This does not
    // free objects or replace the game's normal disconnect/leave handling.
    void mark_disconnected();
    void deactivate();
    bool active() const;
    lce_session_queries::Custom query_callbacks();

private:
    class Guard {
    public:
        explicit Guard(const State &state) : state_(state) { state_.lock(); }
        ~Guard() { state_.unlock(); }
        Guard(const Guard &) = delete;
        Guard &operator=(const Guard &) = delete;
    private:
        const State &state_;
    };
    void lock() const;
    void unlock() const;
    bool valid() const; // caller holds lock_
    bool matches(void *manager) const; // caller holds lock_
    static bool active_cb(void *, void *);
    static int count_cb(void *, void *);
    static void *local_cb(void *, void *, int);
    static void *small_id_cb(void *, void *, uint8_t);
    static void *host_cb(void *, void *);
    static bool host_flag_cb(void *, void *);
    static bool session_cb(void *, void *);
    static bool gameplay_cb(void *, void *);
    static bool ready_cb(void *, void *);

    mutable unsigned char lock_ = 0;
    void *manager_ = nullptr;
    lce_external_players::Players *players_ = nullptr;
    int local_user_ = -1;
    uint8_t local_id_ = 0xff;
    bool active_ = false;
    bool connected_ = false;
    bool ready_ = false;
    bool gameplay_ = false;
};
}
