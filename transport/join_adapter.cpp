#include "join_adapter.hpp"

namespace lce_join_adapter {
Adapter::Adapter(const lce_servers::List &servers, const Ops &ops) : servers_(servers), ops_(ops) {
    if (servers_.count > lce_servers::kMaxServers) servers_.count = 0;
}
bool Adapter::ready() const {
    return ops_.backend_ready && ops_.backend_join && ops_.backend_full_info && ops_.backend_ready();
}
bool Adapter::lookup(void *row, size_t *index) const {
    if (!row || !index) return false;
    uint32_t entry = 0;
    // The game's list refresh may delete a newly returned duplicate while
    // retaining the older displayed row. Identify that live record by its
    // stable session marker and configured index, not its allocation address.
    if (!lce_join_list::endpoint_index(row, &entry) || entry >= servers_.count) return false;
    *index = entry;
    return true;
}
lce_join_list::Vector *Adapter::get_list(void *manager, int pad, int locals, bool party_only) {
    if (!ops_.native_list) return nullptr;
    auto *list = ops_.native_list(manager, pad, locals, party_only);
    if (!list || party_only || !ready()) return list;
    // The native list is a fresh per-call vector. Keep custom allocations and
    // mutations on the thread that configured the runtime; other callers get
    // the untouched native result.
    if (ops_.may_append_rows && !ops_.may_append_rows(ops_.row_gate_context)) return list;
    for (size_t i = 0; i < servers_.count; ++i) {
        (void)lce_join_list::append(list, servers_.entries[i].label,
                                   static_cast<uint32_t>(i), 0, true, ops_.rows);
    }
    return list;
}
int Adapter::join(void *manager, void *row, int mask, int primary) {
    size_t index;
    if (lookup(row, &index)) {
        // Never send an owned record to Sony if the custom backend goes away.
        return ready() ? ops_.backend_join(servers_.entries[index], mask, primary) : 1;
    }
    uint32_t stale_index;
    if (lce_join_list::endpoint_index(row, &stale_index)) return 1;
    return ops_.native_join ? ops_.native_join(manager, row, mask, primary) : 1;
}
void Adapter::full_info(void *manager, void *row, Complete complete, void *context) {
    size_t index;
    if (lookup(row, &index)) {
        if (ready()) ops_.backend_full_info(servers_.entries[index], row, complete, context);
        else if (complete) complete(false, context);
    } else {
        uint32_t stale_index;
        if (lce_join_list::endpoint_index(row, &stale_index)) {
            if (complete) complete(false, context);
        } else if (ops_.native_full_info) ops_.native_full_info(manager, row, complete, context);
        else if (complete) complete(false, context);
    }
}
}
