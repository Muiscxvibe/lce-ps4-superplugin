/* fuel195 - DATA-ONLY mod furnace fuel. See fuel195.h for the design and the
 * proof that serverTick is reached only through the vtable. No .text is written. */
#include "fuel195.h"
#include <string.h>

namespace {
void set_err(char *err, uint32_t cap, const char *m) {
    if (!err || !cap) return;
    size_t n = strlen(m); if (n >= cap) n = cap - 1;
    memcpy(err, m, n); err[n] = 0;
}

/* raw in-process field access (console) / buffer access (host test) */
template <class T> T rdf(void *p, uintptr_t off) { T v; memcpy(&v, (uint8_t *)p + off, sizeof v); return v; }
template <class T> void wrf(void *p, uintptr_t off, T v) { memcpy((uint8_t *)p + off, &v, sizeof v); }

fuel195_runtime g_rt;
}  // namespace

extern "C" fuel195_runtime *fuel195_rt(void) { return &g_rt; }

extern "C" lce_status fuel195_check(const lce_fuel_desc *d, char *err, uint32_t cap) {
    if (!d || d->struct_size < offsetof(lce_fuel_desc, burn_ticks) + sizeof d->burn_ticks) {
        set_err(err, cap, "fuel195: incomplete descriptor"); return LCE_E_INVALID;
    }
    if (!d->item || !*d->item) { set_err(err, cap, "fuel195: item is required"); return LCE_E_INVALID; }
    if (d->burn_ticks < 1 || d->burn_ticks > FUEL195_MAX_TICKS) {
        set_err(err, cap, "fuel195: burn_ticks must be 1..72000"); return LCE_E_INVALID;
    }
    return LCE_OK;
}

extern "C" int fuel195_add(fuel195_runtime *rt, uintptr_t item, uint32_t ticks, uintptr_t remainder) {
    if (!rt || !item || !ticks || ticks > FUEL195_MAX_TICKS) return 0;
    for (uint32_t i = 0; i < rt->table.count; ++i)
        if (rt->table.e[i].item == item) {
            rt->table.e[i].remainder = remainder;
            __atomic_store_n(&rt->table.e[i].ticks, ticks, __ATOMIC_RELEASE);
            return 1;
        }
    if (rt->table.count >= FUEL195_MAX) return 0;
    uint32_t i = rt->table.count;
    rt->table.e[i].item = item; rt->table.e[i].ticks = ticks;
    rt->table.e[i].pad = 0; rt->table.e[i].remainder = remainder;
    __atomic_store_n(&rt->table.count, i + 1, __ATOMIC_RELEASE);  /* entry complete before counted */
    return 1;
}

extern "C" const fuel195_entry *fuel195_lookup(const fuel195_runtime *rt, uintptr_t item) {
    if (!rt || !item) return nullptr;
    uint32_t n = __atomic_load_n(&rt->table.count, __ATOMIC_ACQUIRE);
    for (uint32_t i = 0; i < n; ++i)
        if (rt->table.e[i].item == item) return &rt->table.e[i];
    return nullptr;
}

extern "C" size_t fuel195_build_vtable(uint8_t *dst, size_t cap, const uint8_t *src, size_t len,
                                       uintptr_t tick_wrapper, uintptr_t canplace_wrapper) {
    if (!dst || !src || len < FUEL195_VSLOT_CANPLACE + 8 || cap < len || !tick_wrapper || !canplace_wrapper) return 0;
    memcpy(dst, src, len);
    memcpy(dst + FUEL195_VSLOT_TICK, &tick_wrapper, 8);
    memcpy(dst + FUEL195_VSLOT_CANPLACE, &canplace_wrapper, 8);
    return len;
}

/* ---- the decision + mutation core (host-testable) ---- */
extern "C" int fuel195_relight(fuel195_runtime *rt, void *be) {
    if (!rt || !be) return 0;
    const int32_t remaining = rdf<int32_t>(be, FUEL195_OFF_LITTIME);
    // The retail tick decrements before checking for ignition. Refill on the
    // same tick that would reach zero, so stock fuel cannot win at the boundary.
    if (remaining < 0 || remaining > 1) return 0;
    void *level = rdf<void *>(be, FUEL195_OFF_LEVEL);
    if (!level) return 0;
    if (rdf<uint8_t>(level, FUEL195_OFF_LEVEL_CLIENT)) return 0;       /* client side */
    void *C = rdf<void *>(be, FUEL195_OFF_CONTAINER);
    if (!C) return 0;
    void *fuel = rdf<void *>(C, FUEL195_OFF_SLOT_FUEL);
    if (!fuel) return 0;
    if (rt->nat.isEmpty && rt->nat.isEmpty(fuel)) return 0;
    void *item = rt->nat.getItem ? rt->nat.getItem(fuel) : rdf<void *>(fuel, FUEL195_II_ITEM);
    const fuel195_entry *e = fuel195_lookup(rt, (uintptr_t)item);
    if (!e) return 0;                                                  /* not a mod fuel -> stock handles */
    if (!rt->nat.canBurn || !rt->nat.canBurn(be)) return 0;           /* only burn for a valid recipe */

    int32_t cnt = rdf<int32_t>(fuel, FUEL195_II_COUNT);
    if (cnt <= 0) return 0;                                          /* never consume an empty stack */

    wrf<int32_t>(be, FUEL195_OFF_LITTIME, (int32_t)e->ticks);
    wrf<int32_t>(be, FUEL195_OFF_LITDURATION, (int32_t)e->ticks);

    if (e->remainder && cnt <= 1) {
        wrf<void *>(fuel, FUEL195_II_ITEM, (void *)e->remainder);     /* bucket -> remainder in place */
        wrf<int32_t>(fuel, FUEL195_II_COUNT, 1);
        wrf<uint8_t>(fuel, FUEL195_II_EMPTY, 0);
    } else if (rt->nat.setCount) {
        rt->nat.setCount(fuel, cnt - 1);
    } else {
        wrf<int32_t>(fuel, FUEL195_II_COUNT, cnt - 1);
        if (cnt - 1 <= 0) wrf<uint8_t>(fuel, FUEL195_II_EMPTY, 1);
    }
    if (!remaining && rt->nat.setLit) rt->nat.setLit(1, level, (uint8_t *)be + FUEL195_OFF_POS);
    if (rt->nat.setChanged) rt->nat.setChanged(be);
    return 1;
}

extern "C" void fuel195_tick_wrapper(void *be) {
    fuel195_runtime *rt = &g_rt;
    if (fuel195_relight(rt, be) && rt->nat.origTick) {
        // Retail decrements an already-lit furnace at the start of its tick.
        // Vanilla ignition occurs later in that tick, so compensate here.
        int32_t ticks = rdf<int32_t>(be, FUEL195_OFF_LITTIME);
        wrf<int32_t>(be, FUEL195_OFF_LITTIME, ticks + 1);
    }
    if (rt->nat.origTick) rt->nat.origTick(be);                       /* tail: stock serverTick */
}

extern "C" int fuel195_canplace_wrapper(void *be, int32_t slot, void *stackSp) {
    fuel195_runtime *rt = &g_rt;
    if (slot == 1 && stackSp) {                                       /* fuel slot */
        void *ii = rdf<void *>(stackSp, 0);                           /* shared_ptr first word */
        if (ii && !(rt->nat.isEmpty && rt->nat.isEmpty(ii))) {
            void *item = rt->nat.getItem ? rt->nat.getItem(ii) : rdf<void *>(ii, FUEL195_II_ITEM);
            if (fuel195_lookup(rt, (uintptr_t)item)) return 1;        /* accept mod fuel */
        }
    }
    return rt->origCanPlaceFn ? rt->origCanPlaceFn(be, slot, stackSp) : 0;  /* stock decision */
}

extern "C" void fuel195_bind_natives(fuel195_runtime *rt, uintptr_t base) {
    rt->nat.base     = base;
    rt->nat.canBurn  = (uint8_t (*)(void *))(base + FUEL195_RVA_CANBURN);
    rt->nat.setCount = (void (*)(void *, int32_t))(base + FUEL195_RVA_SETCOUNT);
    rt->nat.isEmpty  = (uint8_t (*)(void *))(base + FUEL195_RVA_ISEMPTY);
    rt->nat.getItem  = (void *(*)(void *))(base + FUEL195_RVA_GETITEM);
    rt->nat.setLit   = (void (*)(uint64_t, void *, void *))(base + FUEL195_RVA_SETLIT);
    rt->nat.setChanged = (void (*)(void *))(base + FUEL195_RVA_CHANGED);
    rt->nat.origTick = (void (*)(void *))(base + FUEL195_RVA_SERVERTICK);
    rt->origCanPlaceFn = (int (*)(void *, int32_t, void *))(base + FUEL195_RVA_CANPLACEITEM);
}

extern "C" int fuel195_prepare_vtable(fuel195_runtime *rt, const fuel195_ops *o,
                                      uintptr_t base, uintptr_t *out_vtable) {
    if (!rt || !o || !o->read || !o->write || !o->alloc || !out_vtable) return 0;
    if (rt->private_vtable) { *out_vtable = rt->private_vtable; return 1; }   /* already built */
    uint8_t stock[FUEL195_VTABLE_PREFIX + FUEL195_VTABLE_LEN];
    if (o->read(o->ctx, base + FUEL195_RVA_FBE_VTABLE - FUEL195_VTABLE_PREFIX, stock, sizeof stock) != 1) return 0;
    /* The furnace menu reads the Container subobject displacement from this prefix. */
    uint64_t prefix[4], s_tick, s_place;
    memcpy(prefix, stock, sizeof prefix);
    memcpy(&s_tick,  stock + FUEL195_VTABLE_PREFIX + FUEL195_VSLOT_TICK, 8);
    memcpy(&s_place, stock + FUEL195_VTABLE_PREFIX + FUEL195_VSLOT_CANPLACE, 8);
    if (prefix[0] != FUEL195_FBE_CONTAINER_OFFSET || prefix[1] != FUEL195_FBE_CONTAINER_OFFSET ||
        prefix[2] || prefix[3] != base + FUEL195_RVA_FBE_RTTI ||
        s_tick != base + FUEL195_RVA_SERVERTICK || s_place != base + FUEL195_RVA_CANPLACEITEM) return 0;

    uintptr_t page = o->alloc(o->ctx, FUEL195_PAGE);
    if (!page) return 0;
    uint8_t priv[FUEL195_VTABLE_PREFIX + FUEL195_VTABLE_LEN];
    memcpy(priv, stock, FUEL195_VTABLE_PREFIX);
    if (!fuel195_build_vtable(priv + FUEL195_VTABLE_PREFIX, FUEL195_VTABLE_LEN,
                              stock + FUEL195_VTABLE_PREFIX, FUEL195_VTABLE_LEN,
                              (uintptr_t)&fuel195_tick_wrapper, (uintptr_t)&fuel195_canplace_wrapper))
        return 0;
    if (o->write(o->ctx, page, priv, sizeof priv) != 1) return 0;
    rt->private_vtable = page + FUEL195_VTABLE_PREFIX;
    *out_vtable = rt->private_vtable;
    return 1;
}

extern "C" int fuel195_swap_object(fuel195_runtime *rt, const fuel195_ops *o, void *be,
                                   uintptr_t private_vtable) {
    if (!rt || !o || !o->read || !o->cas8 || !be || !private_vtable) return 0;
    uint64_t cur;
    if (o->read(o->ctx, (uintptr_t)be, &cur, 8) != 1) return 0;
    if (cur == private_vtable) return 1;                              /* already swapped */
    if (cur != rt->nat.base + FUEL195_RVA_FBE_VTABLE) return 0;       /* not a stock furnace vptr */
    return o->cas8(o->ctx, (uintptr_t)be, cur, private_vtable);
}

extern "C" void *fuel195_load_factory_wrapper(void) {
    fuel195_runtime *rt = &g_rt;
    if (!rt->origFactory) return nullptr;
    void *be = rt->origFactory();
    if (be && rt->factory_ops && rt->private_vtable)
        (void)fuel195_swap_object(rt, rt->factory_ops, be, rt->private_vtable);
    return be;
}

extern "C" int fuel195_install_load_factory(fuel195_runtime *rt, const fuel195_ops *o) {
    // The wrapper uses the process-wide runtime, and the caller must hold the
    // registry quiescent from validation until the pointer swap completes.
    if (rt != &g_rt || !o || !o->read || !o->cas8 || !rt->nat.base || !rt->private_vtable) return 0;
    if (rt->factory_slot) return rt->factory_ops == o ? 1 : 0;
    uintptr_t map = rt->nat.base + FUEL195_RVA_BE_REGISTRY;
    uint64_t head = 0, count = 0, node = 0;
    if (o->read(o->ctx, map + 8, &head, 8) != 1 || !head ||
        o->read(o->ctx, map + 16, &count, 8) != 1 || !count || count > 1024 ||
        o->read(o->ctx, head, &node, 8) != 1) return 0;
    uintptr_t found = 0;
    for (uint64_t i = 0; i < count; ++i) {
        uint64_t next = 0, factory = 0;
        if (!node || node == head || node > UINTPTR_MAX - 0x40 ||
            o->read(o->ctx, node, &next, 8) != 1 ||
            o->read(o->ctx, node + 0x38, &factory, 8) != 1) return 0;
        if (factory == rt->nat.base + FUEL195_RVA_FBE_FACTORY) {
            if (found) return 0;
            found = node + 0x38;
        }
        node = next;
    }
    if (node != head || !found) return 0;
    rt->origFactory = (void *(*)())(rt->nat.base + FUEL195_RVA_FBE_FACTORY);
    rt->factory_ops = o;
    int rc = o->cas8(o->ctx, found, (uint64_t)(uintptr_t)rt->origFactory,
                     (uint64_t)(uintptr_t)&fuel195_load_factory_wrapper);
    if (rc == 1) { rt->factory_slot = found; return 1; }
    // A negative result can mean the swap happened but page protection could
    // not be restored; keep wrapper dependencies intact in that case.
    if (rc == 0) { rt->origFactory = nullptr; rt->factory_ops = nullptr; }
    return rc;
}

extern "C" void *fuel195_new_factory_wrapper(void *result, void *entity_block, void *level, int32_t data) {
    fuel195_runtime *rt = &g_rt;
    if (!rt->origNew || !result) return nullptr;
    void *returned = rt->origNew(result, entity_block, level, data);
    void *be = rdf<void *>(result, 0); // first word of the returned shared_ptr
    if (be && rt->new_ops && rt->private_vtable)
        (void)fuel195_swap_object(rt, rt->new_ops, be, rt->private_vtable);
    return returned;
}

extern "C" int fuel195_install_new_factory(fuel195_runtime *rt, const fuel195_ops *o) {
    if (rt != &g_rt || !o || !o->read || !o->cas8 || !rt->nat.base || !rt->private_vtable) return 0;
    if (rt->new_slot) return rt->new_ops == o ? 1 : 0;
    uintptr_t vt = rt->nat.base + FUEL195_RVA_BLOCK_ENTITY_VT;
    uint64_t offset = 0, typeinfo = 0, slot = 0;
    if (o->read(o->ctx, vt - 16, &offset, 8) != 1 || offset != (uint64_t)-(int64_t)0x1e8 ||
        o->read(o->ctx, vt - 8, &typeinfo, 8) != 1 || typeinfo != rt->nat.base + FUEL195_RVA_BLOCK_RTTI ||
        o->read(o->ctx, vt + 0x10, &slot, 8) != 1 || slot != rt->nat.base + FUEL195_RVA_NEW_FURNACE) return 0;
    rt->origNew = (void *(*)(void *, void *, void *, int32_t))(uintptr_t)slot;
    rt->new_ops = o;
    int rc = o->cas8(o->ctx, vt + 0x10, slot, (uintptr_t)&fuel195_new_factory_wrapper);
    if (rc == 1) { rt->new_slot = vt + 0x10; return 1; }
    if (rc == 0) { rt->origNew = nullptr; rt->new_ops = nullptr; }
    return rc;
}

extern "C" int fuel195_may_place_wrapper(void *slot, void *stackSp) {
    fuel195_runtime *rt = &g_rt;
    if (stackSp) {
        void *ii = rdf<void *>(stackSp, 0);
        if (ii && !(rt->nat.isEmpty && rt->nat.isEmpty(ii))) {
            void *item = rt->nat.getItem ? rt->nat.getItem(ii) : rdf<void *>(ii, FUEL195_II_ITEM);
            if (fuel195_lookup(rt, (uintptr_t)item)) return 1;
        }
    }
    return rt->origMayPlace ? rt->origMayPlace(slot, stackSp) : 0;
}

extern "C" int fuel195_install_fuel_slot(fuel195_runtime *rt, const fuel195_ops *o) {
    if (rt != &g_rt || !o || !o->read || !o->cas8 || !rt->nat.base) return 0;
    if (rt->may_place_slot) return rt->slot_ops == o ? 1 : 0;
    uintptr_t vt = rt->nat.base + FUEL195_RVA_FUEL_SLOT_VT;
    uint64_t offset = 0, typeinfo = 0, slot = 0;
    if (o->read(o->ctx, vt - 16, &offset, 8) != 1 || offset ||
        o->read(o->ctx, vt - 8, &typeinfo, 8) != 1 || typeinfo != rt->nat.base + FUEL195_RVA_FUEL_SLOT_RTTI ||
        o->read(o->ctx, vt + 0x30, &slot, 8) != 1 || slot != rt->nat.base + FUEL195_RVA_FUEL_MAY_PLACE) return 0;
    rt->origMayPlace = (int (*)(void *, void *))(uintptr_t)slot;
    rt->slot_ops = o;
    int rc = o->cas8(o->ctx, vt + 0x30, slot, (uintptr_t)&fuel195_may_place_wrapper);
    if (rc == 1) { rt->may_place_slot = vt + 0x30; return 1; }
    if (rc == 0) { rt->origMayPlace = nullptr; rt->slot_ops = nullptr; }
    return rc;
}

extern "C" int fuel195_activate(fuel195_runtime *rt, const fuel195_ops *o, uintptr_t base,
                                 const fuel195_table *table) {
    if (rt != &g_rt || !o || !o->cas8 || !base || base > UINTPTR_MAX - FUEL195_RVA_BE_REGISTRY ||
        !table || !table->count || table->count > FUEL195_MAX || rt->installed ||
        rt->factory_slot || rt->new_slot || rt->may_place_slot ||
        (rt->private_vtable && rt->nat.base != base)) return 0;
    for (uint32_t i = 0; i < table->count; ++i) {
        if (!table->e[i].item || !table->e[i].ticks || table->e[i].ticks > FUEL195_MAX_TICKS) return 0;
        for (uint32_t j = 0; j < i; ++j)
            if (table->e[j].item == table->e[i].item) return 0;
    }
    uintptr_t vt = 0;
    rt->failed_step = "furnace tick table";
    if (!fuel195_prepare_vtable(rt, o, base, &vt)) return 0;
    fuel195_bind_natives(rt, base);
    rt->table = *table;
    rt->failed_step = "saved furnace factory";
    int rc = fuel195_install_load_factory(rt, o);
    if (rc == 1) {
        rt->failed_step = "new furnace factory";
        rc = fuel195_install_new_factory(rt, o);
    }
    if (rc == 1) {
        rt->failed_step = "furnace fuel slot";
        rc = fuel195_install_fuel_slot(rt, o);
    }
    if (rc == 1) { rt->failed_step = nullptr; rt->installed = 1; return 1; }
    if (rc < 0) { rt->installed = -1; return -1; }

    // Roll back in reverse order. If another writer changed a slot, or page
    // protection could not be restored, keep all wrapper dependencies alive.
    if (rt->may_place_slot && o->cas8(o->ctx, rt->may_place_slot,
            (uintptr_t)&fuel195_may_place_wrapper, (uintptr_t)rt->origMayPlace) != 1) {
        rt->installed = -1; return -1;
    }
    if (rt->new_slot && o->cas8(o->ctx, rt->new_slot,
            (uintptr_t)&fuel195_new_factory_wrapper, (uintptr_t)rt->origNew) != 1) {
        rt->installed = -1; return -1;
    }
    if (rt->factory_slot && o->cas8(o->ctx, rt->factory_slot,
            (uintptr_t)&fuel195_load_factory_wrapper, (uintptr_t)rt->origFactory) != 1) {
        rt->installed = -1; return -1;
    }
    rt->factory_slot = rt->new_slot = rt->may_place_slot = 0;
    rt->origFactory = nullptr; rt->origNew = nullptr; rt->origMayPlace = nullptr;
    rt->factory_ops = rt->new_ops = rt->slot_ops = nullptr;
    memset(&rt->table, 0, sizeof rt->table);
    return 0;
}
