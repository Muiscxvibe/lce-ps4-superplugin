#include "join_binding.hpp"
namespace lce_join_hook {
Binding *Binding::active_ = nullptr;
Binding::Binding(void *manager, const lce_servers::List &servers, const lce_join_adapter::Ops &ops)
    : manager_(manager), ops_(ops), adapter_(servers, ops) {}
bool Binding::activate() {
    if (!manager_ || !ops_.native_list || !ops_.native_join || !ops_.native_full_info) return false;
    Binding *empty = nullptr;
    return __atomic_compare_exchange_n(&active_, &empty, this, false, __ATOMIC_RELEASE, __ATOMIC_RELAXED);
}
bool Binding::deactivate() {
    Binding *expected = this;
    return __atomic_compare_exchange_n(&active_, &expected, nullptr, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}
Replacements Binding::replacements() const {
    return {reinterpret_cast<uintptr_t>(&join), reinterpret_cast<uintptr_t>(&list),
            reinterpret_cast<uintptr_t>(&full_info)};
}
lce_join_list::Vector *Binding::list(void *manager, int pad, int locals, bool party_only) {
    Binding *b = __atomic_load_n(&active_, __ATOMIC_ACQUIRE);
    if (!b) return nullptr;
    if (manager != b->manager_) return b->ops_.native_list(manager, pad, locals, party_only);
    return b->adapter_.get_list(manager, pad, locals, party_only);
}
int Binding::join(void *manager, void *row, int mask, int primary) {
    Binding *b = __atomic_load_n(&active_, __ATOMIC_ACQUIRE);
    if (!b) return 1;
    if (manager != b->manager_) return b->ops_.native_join(manager, row, mask, primary);
    return b->adapter_.join(manager, row, mask, primary);
}
void Binding::full_info(void *manager, void *row, lce_join_adapter::Complete cb, void *ctx) {
    Binding *b = __atomic_load_n(&active_, __ATOMIC_ACQUIRE);
    if (!b) { if (cb) cb(false, ctx); return; }
    if (manager != b->manager_) b->ops_.native_full_info(manager, row, cb, ctx);
    else b->adapter_.full_info(manager, row, cb, ctx);
}
}
