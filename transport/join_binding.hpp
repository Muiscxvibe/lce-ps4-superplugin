#pragma once
#include "join_adapter.hpp"
#include "join_vtable.hpp"

// Game-thread-only hook dispatch. Binding and all callbacks must remain alive
// until dispatch is quiescent and the original vptr is restored. Publishing
// the replacement vptr is deliberately managed by the eventual game owner.
namespace lce_join_hook {
class Binding {
public:
    Binding(void *manager, const lce_servers::List &, const lce_join_adapter::Ops &);
    Binding(const Binding &) = delete;
    Binding &operator=(const Binding &) = delete;
    bool activate();
    // Only call after the original vptr is restored and no hooks are in flight.
    bool deactivate();
    Replacements replacements() const;
private:
    static lce_join_list::Vector *list(void *, int, int, bool);
    static int join(void *, void *, int, int);
    static void full_info(void *, void *, lce_join_adapter::Complete, void *);
    static int quick_match(void *, int, int, int, int, int *, int *);
    static Binding *active_;
    void *manager_;
    lce_join_adapter::Ops ops_;
    lce_join_adapter::Adapter adapter_;
};
}
