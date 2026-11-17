#pragma once
#include "join_list.hpp"
#include "../ps4-toolchain/loader/server_config/servers.hpp"

// Hook-independent dispatcher for the 1.95 platform network manager. This is
// invoked by future game-thread hooks; it does not install patches itself.
namespace lce_join_adapter {
using Complete = void (*)(bool, void *);
struct Ops {
    lce_join_list::Ops rows;
    lce_join_list::Vector *(*native_list)(void *, int, int, bool);
    int (*native_join)(void *, void *, int, int);
    void (*native_full_info)(void *, void *, Complete, void *);
    bool (*backend_ready)();
    int (*backend_join)(const lce_servers::Server &, int, int);
    void (*backend_full_info)(const lce_servers::Server &, void *, Complete, void *);
    bool (*may_append_rows)(void *);
    int (*native_quick_match)(void *, int, int, int, int, int *, int *) = nullptr;
    void *row_gate_context=nullptr;
};
class Adapter {
public:
    Adapter(const lce_servers::List &servers, const Ops &ops);
    lce_join_list::Vector *get_list(void *manager, int pad, int locals, bool party_only);
    int join(void *manager, void *row, int local_mask, int primary_user);
    void full_info(void *manager, void *row, Complete complete, void *context);
    // Mini Games quick-match (platform slot 31) for minigame `type` (Battle = 1).
    // Joins the configured minigame
    // server of that type; returns -1 when none is configured (caller falls back).
    int quick_match(int local_mask, int type);
private:
    bool lookup(void *row, size_t *index) const;
    bool ready() const;
    lce_servers::List servers_;
    Ops ops_;
};
}
