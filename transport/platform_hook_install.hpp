#pragma once
#include "join_binding.hpp"
#include "session_query_dispatch.hpp"

namespace lce_platform_hook {
// One instance is kept for the lifetime of the game. The caller supplies
// custom backend operations, genuine row operations and stable session state.
// All of them must remain valid until the process exits.
class Install {
public:
    Install(uintptr_t image, void *manager, const lce_servers::List &servers,
            const lce_join_list::Ops &rows,
            bool (*backend_ready)(),
            int (*backend_join)(const lce_servers::Server &,int,int),
            void (*backend_info)(const lce_servers::Server &,void *,lce_join_adapter::Complete,void *),
            bool (*may_append_rows)(void *), void *row_gate_context,
            const lce_session_queries::Custom &custom);
    Install(const Install &) = delete;
    Install &operator=(const Install &) = delete;
    // Call on the game owner thread. The original and replacement tables and
    // callback state must remain valid for process lifetime, including for
    // dispatches already executing through the original table. Returns false
    // if the pinned executable/manager differs or its vptr changes mid-switch.
    bool publish();
private:
    static lce_join_adapter::Ops make_ops(uintptr_t, const lce_join_list::Ops &,
            bool (*)(), int (*)(const lce_servers::Server &,int,int),
            void (*)(const lce_servers::Server &,void *,lce_join_adapter::Complete,void *),
            bool (*)(void *), void *);
    static lce_session_queries::Native native_queries(uintptr_t);
    uintptr_t image_;
    void *manager_;
    lce_join_hook::Binding binding_;
    lce_session_queries::Dispatcher queries_;
    lce_join_hook::Vtable table_;
    bool published_=false;
};
}
