// Convert the loader's immutable plan to resolved fuel entries after the item
// registry is published. No game state is changed here.
#include "fuel195.h"
#include <string.h>

namespace {
void error(char *dst, uint32_t cap, const char *message) {
    if (!dst || !cap) return;
    size_t n = strlen(message); if (n >= cap) n = cap - 1;
    memcpy(dst, message, n); dst[n] = 0;
}
lce_status add(fuel195_runtime *scratch, uintptr_t item, uint32_t ticks, char *err, uint32_t cap) {
    if (!item) { error(err, cap, "fuel195: fuel item not published"); return LCE_E_NOTFOUND; }
    if (!ticks || ticks > FUEL195_MAX_TICKS) { error(err, cap, "fuel195: burn_ticks must be 1..72000"); return LCE_E_INVALID; }
    if (fuel195_lookup(scratch, item)) { error(err, cap, "fuel195: duplicate fuel for item"); return LCE_E_COLLISION; }
    if (!fuel195_add(scratch, item, ticks, 0)) { error(err, cap, "fuel195: too many fuel items"); return LCE_E_FULL; }
    return LCE_OK;
}
}

extern "C" lce_status fuel195_collect_plan(const lce_plan *p, uintptr_t (*item_by_id)(unsigned),
                                            fuel195_table *out, char *err, uint32_t cap) {
    if (!p || !item_by_id || !out || (p->item_count && !p->items) || (p->ext_count && !p->ext)) {
        error(err, cap, "fuel195: invalid fuel plan"); return LCE_E_INVALID;
    }
    fuel195_runtime scratch{};
    for (uint32_t i = 0; i < p->item_count; ++i) {
        const lce_plan_item &it = p->items[i];
        if (!it.desc || !it.desc->fuel_ticks) continue;
        if (!it.id || it.id > 65535) { error(err, cap, "fuel195: invalid item id"); return LCE_E_INVALID; }
        lce_status st = add(&scratch, item_by_id(it.id), it.desc->fuel_ticks, err, cap);
        if (st != LCE_OK) return st;
    }
    for (uint32_t i = 0; i < p->ext_count; ++i) {
        const lce_plan_ext &x = p->ext[i];
        if (x.type != LCE_EXT_FUEL) continue;
        const lce_fuel_desc *f = (const lce_fuel_desc *)x.desc;
        lce_status st = fuel195_check(f, err, cap);
        if (st != LCE_OK) return st;
        if (!p->resolve) { error(err, cap, "fuel195: item resolver unavailable"); return LCE_E_INVALID; }
        int32_t id = p->resolve(p->resolve_ctx, LCE_REF_ITEM, f->item);
        if (id <= 0 || id > 65535) { error(err, cap, "fuel195: fuel item unresolved"); return LCE_E_NOTFOUND; }
        st = add(&scratch, item_by_id((unsigned)id), f->burn_ticks, err, cap);
        if (st != LCE_OK) return st;
    }
    *out = scratch.table;
    return LCE_OK;
}
