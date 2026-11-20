#include <stdint.h>
#include <string.h>
#include "../fuel195.h"
#include "../../../tests/check.h"

template <class T> static void put(void *p, size_t off, T v) { memcpy((uint8_t *)p + off, &v, sizeof v); }
template <class T> static T get(void *p, size_t off) { T v; memcpy(&v, (uint8_t *)p + off, sizeof v); return v; }

static uint8_t can_burn(void *) { return 1; }
static uint8_t cannot_burn(void *) { return 0; }
static int lit_calls;
static void set_lit(uint64_t lit, void *, void *) { if (lit) ++lit_calls; }
static int changed_calls, original_ticks;
static void set_changed(void *) { ++changed_calls; }
static void stock_tick(void *be) {
    ++original_ticks;
    int32_t ticks = get<int32_t>(be, FUEL195_OFF_LITTIME);
    if (ticks > 0) put(be, FUEL195_OFF_LITTIME, ticks - 1);
}

struct FakeTable {
    uintptr_t base;
    uint8_t source[FUEL195_VTABLE_PREFIX + FUEL195_VTABLE_LEN];
    uint8_t copy[FUEL195_VTABLE_PREFIX + FUEL195_VTABLE_LEN];
    int allocations;
};
static int fake_read(void *u, uintptr_t addr, void *out, size_t n) {
    FakeTable *f = (FakeTable *)u;
    if (addr != f->base + FUEL195_RVA_FBE_VTABLE - FUEL195_VTABLE_PREFIX || n != sizeof f->source) return 0;
    memcpy(out, f->source, n);
    return 1;
}
static int fake_write(void *u, uintptr_t addr, const void *data, size_t n) {
    FakeTable *f = (FakeTable *)u;
    if (addr != (uintptr_t)f->copy || n != sizeof f->copy) return 0;
    memcpy(f->copy, data, n);
    return 1;
}
static uintptr_t fake_alloc(void *u, size_t n) {
    FakeTable *f = (FakeTable *)u;
    if (n != FUEL195_PAGE) return 0;
    ++f->allocations;
    return (uintptr_t)f->copy;
}

struct FakeRegistry {
    uintptr_t base;
    uint64_t sentinel[2];
    uint8_t first[0x40], second[0x40];
    uint64_t count;
};
static int registry_read(void *u, uintptr_t addr, void *out, size_t n) {
    FakeRegistry *r = (FakeRegistry *)u;
    if (n != 8) return 0;
    uintptr_t map = r->base + FUEL195_RVA_BE_REGISTRY;
    if (addr == map + 8) { uint64_t head = (uintptr_t)r->sentinel; memcpy(out, &head, 8); return 1; }
    if (addr == map + 16) { memcpy(out, &r->count, 8); return 1; }
    if (addr == (uintptr_t)r->sentinel || addr == (uintptr_t)r->first ||
        addr == (uintptr_t)r->first + 0x38 || addr == (uintptr_t)r->second || addr == (uintptr_t)r->second + 0x38) {
        memcpy(out, (void *)addr, 8); return 1;
    }
    return 0;
}
static int registry_cas(void *, uintptr_t addr, uint64_t expected, uint64_t repl) {
    uint64_t actual; memcpy(&actual, (void *)addr, 8);
    if (actual != expected) return 0;
    memcpy((void *)addr, &repl, 8); return 1;
}
static uint64_t fake_furnace;
static void *fake_factory() { return &fake_furnace; }
static int object_read(void *, uintptr_t addr, void *out, size_t n) {
    if (addr != (uintptr_t)&fake_furnace || n != 8) return 0;
    memcpy(out, &fake_furnace, n); return 1;
}
struct FakeBlockTable { uintptr_t base; uint64_t offset, typeinfo, slot; };
static int block_read(void *u, uintptr_t addr, void *out, size_t n) {
    FakeBlockTable *t = (FakeBlockTable *)u;
    uintptr_t vt = t->base + FUEL195_RVA_BLOCK_ENTITY_VT;
    if (n != 8) return 0;
    const uint64_t *p = addr == vt - 16 ? &t->offset : addr == vt - 8 ? &t->typeinfo : addr == vt + 0x10 ? &t->slot : nullptr;
    if (!p) return 0;
    memcpy(out, p, 8); return 1;
}
static int block_cas(void *u, uintptr_t addr, uint64_t expected, uint64_t repl) {
    FakeBlockTable *t = (FakeBlockTable *)u;
    if (addr != t->base + FUEL195_RVA_BLOCK_ENTITY_VT + 0x10 || t->slot != expected) return 0;
    t->slot = repl; return 1;
}
static void *fake_new(void *result, void *, void *, int32_t) {
    put(result, 0, (void *)&fake_furnace);
    return result;
}
static int slot_read(void *u, uintptr_t addr, void *out, size_t n) {
    FakeBlockTable *t = (FakeBlockTable *)u;
    uintptr_t vt = t->base + FUEL195_RVA_FUEL_SLOT_VT;
    if (n != 8) return 0;
    const uint64_t *p = addr == vt - 16 ? &t->offset : addr == vt - 8 ? &t->typeinfo : addr == vt + 0x30 ? &t->slot : nullptr;
    if (!p) return 0;
    memcpy(out, p, 8); return 1;
}
static int slot_cas(void *u, uintptr_t addr, uint64_t expected, uint64_t repl) {
    FakeBlockTable *t = (FakeBlockTable *)u;
    if (addr != t->base + FUEL195_RVA_FUEL_SLOT_VT + 0x30 || t->slot != expected) return 0;
    t->slot = repl; return 1;
}
static int fake_may_place(void *, void *) { return 7; }
struct FakeInstall {
    FakeTable table;
    FakeRegistry registry;
    FakeBlockTable block, menu, quick_move, hopper;
    uint64_t sided_placement;
    int fail_quick_move, fail_hopper;
};
static int quick_move_read(const FakeBlockTable &t, uintptr_t addr, void *out, size_t n) {
    uintptr_t vt = t.base + FUEL195_RVA_FURNACE_MENU_VT;
    if (n != 8) return 0;
    const uint64_t *value = addr == vt - 16 ? &t.offset : addr == vt - 8 ? &t.typeinfo :
        addr == vt + FUEL195_VSLOT_QUICK_MOVE ? &t.slot : nullptr;
    if (!value) return 0;
    memcpy(out, value, n);
    return 1;
}
static int hopper_read(const FakeInstall &f, uintptr_t addr, void *out, size_t n) {
    uintptr_t vt = f.hopper.base + FUEL195_RVA_CONTAINER_VT;
    if (n != 8) return 0;
    const uint64_t *value = addr == vt - 16 ? &f.hopper.offset : addr == vt - 8 ? &f.hopper.typeinfo :
        addr == vt + FUEL195_VSLOT_CONTAINER_CAN_PLACE ? &f.hopper.slot :
        addr == vt + FUEL195_VSLOT_CONTAINER_SIDED ? &f.sided_placement : nullptr;
    if (!value) return 0;
    memcpy(out, value, n);
    return 1;
}
static int install_read(void *u, uintptr_t a, void *out, size_t n) {
    FakeInstall *f = (FakeInstall *)u;
    return fake_read(&f->table, a, out, n) || registry_read(&f->registry, a, out, n) ||
           block_read(&f->block, a, out, n) || slot_read(&f->menu, a, out, n) ||
           quick_move_read(f->quick_move, a, out, n) || hopper_read(*f, a, out, n);
}
static int install_write(void *u, uintptr_t a, const void *in, size_t n) {
    return fake_write(&((FakeInstall *)u)->table, a, in, n);
}
static uintptr_t install_alloc(void *u, size_t n) { return fake_alloc(&((FakeInstall *)u)->table, n); }
static int install_cas(void *u, uintptr_t a, uint64_t expected, uint64_t repl) {
    FakeInstall *f = (FakeInstall *)u;
    if (a == f->block.base + FUEL195_RVA_BLOCK_ENTITY_VT + 0x10) return block_cas(&f->block, a, expected, repl);
    if (a == f->menu.base + FUEL195_RVA_FUEL_SLOT_VT + 0x30) return slot_cas(&f->menu, a, expected, repl);
    if (a == f->quick_move.base + FUEL195_RVA_FURNACE_MENU_VT + FUEL195_VSLOT_QUICK_MOVE) {
        if (f->fail_quick_move == 1 || f->quick_move.slot != expected) return 0;
        f->quick_move.slot = repl;
        return f->fail_quick_move == -1 ? -1 : 1;
    }
    if (a == f->hopper.base + FUEL195_RVA_CONTAINER_VT + FUEL195_VSLOT_CONTAINER_CAN_PLACE) {
        if (f->fail_hopper || f->hopper.slot != expected) return 0;
        f->hopper.slot = repl;
        return 1;
    }
    if (a == (uintptr_t)f->registry.first + 0x38 || a == (uintptr_t)f->registry.second + 0x38)
        return registry_cas(nullptr, a, expected, repl);
    return 0;
}
static uintptr_t item_by_id(unsigned id) { return id == 1 ? 0x1110 : id == 2 ? 0x2220 : 0; }
static int32_t resolve_item(const void *, uint32_t kind, const char *key) {
    if (kind != LCE_REF_ITEM || !key) return -1;
    return !strcmp(key, "mod:one") ? 1 : !strcmp(key, "minecraft:coal") ? 2 : -1;
}

int main() {
    fuel195_runtime rt{};
    alignas(8) uint8_t be[0x200]{}, level[0x300]{}, slots[0x40]{}, stack[0x80]{};
    uintptr_t item = 0x1234;
    put(be, FUEL195_OFF_LEVEL, (void *)level);
    put(be, FUEL195_OFF_CONTAINER, (void *)slots);
    put(slots, FUEL195_OFF_SLOT_FUEL, (void *)stack);
    put(stack, FUEL195_II_ITEM, (void *)item);
    put(stack, FUEL195_II_COUNT, int32_t(2));
    rt.nat.canBurn = can_burn;
    rt.nat.setLit = set_lit;
    CHECK_EQ(fuel195_add(&rt, item, 1600, 0), 1);

    CHECK_EQ(fuel195_relight(&rt, be), 1);
    CHECK_EQ(get<int32_t>(be, FUEL195_OFF_LITTIME), 1600);
    CHECK_EQ(get<int32_t>(be, FUEL195_OFF_LITDURATION), 1600);
    CHECK_EQ(get<int32_t>(stack, FUEL195_II_COUNT), 1);
    CHECK_EQ(lit_calls, 1);
    CHECK_EQ(fuel195_relight(&rt, be), 0); // already burning
    CHECK_EQ(get<int32_t>(stack, FUEL195_II_COUNT), 1);

    put(be, FUEL195_OFF_LITTIME, int32_t(0));
    rt.nat.canBurn = cannot_burn;
    CHECK_EQ(fuel195_relight(&rt, be), 0);
    rt.nat.canBurn = nullptr;
    CHECK_EQ(fuel195_relight(&rt, be), 0);
    rt.nat.canBurn = can_burn;
    put(stack, FUEL195_II_COUNT, int32_t(0));
    CHECK_EQ(fuel195_relight(&rt, be), 0);
    CHECK_EQ(get<int32_t>(be, FUEL195_OFF_LITTIME), 0);
    CHECK_EQ(lit_calls, 1);

    put(stack, FUEL195_II_COUNT, int32_t(1));
    put(level, FUEL195_OFF_LEVEL_CLIENT, uint8_t(1));
    CHECK_EQ(fuel195_relight(&rt, be), 0);
    put(level, FUEL195_OFF_LEVEL_CLIENT, uint8_t(0));
    CHECK_EQ(fuel195_relight(&rt, be), 1);
    CHECK_EQ(get<int32_t>(stack, FUEL195_II_COUNT), 0);
    CHECK_EQ(get<uint8_t>(stack, FUEL195_II_EMPTY), 1);

    // The wrapper must give the retail tick a full first tick and mark the
    // fuel stack changed even when its burn duration is only one tick.
    fuel195_runtime *tick_rt = fuel195_rt(); *tick_rt = fuel195_runtime{};
    tick_rt->nat.canBurn = can_burn; tick_rt->nat.setLit = set_lit;
    tick_rt->nat.setChanged = set_changed; tick_rt->nat.origTick = stock_tick;
    CHECK_EQ(fuel195_add(tick_rt, item, 1, 0), 1);
    put(stack, FUEL195_II_ITEM, (void *)item);
    put(stack, FUEL195_II_COUNT, int32_t(1));
    put(stack, FUEL195_II_EMPTY, uint8_t(0));
    put(be, FUEL195_OFF_LITTIME, int32_t(0));
    fuel195_tick_wrapper(be);
    CHECK_EQ(get<int32_t>(be, FUEL195_OFF_LITTIME), 1);
    CHECK_EQ(get<int32_t>(be, FUEL195_OFF_LITDURATION), 1);
    CHECK_EQ(get<int32_t>(stack, FUEL195_II_COUNT), 0);
    CHECK_EQ(changed_calls, 1);
    CHECK_EQ(original_ticks, 1);
    // Reload a fuel item before the last tick expires. The next ignition should
    // happen on that tick, keeping the furnace lit and the cook cycle running.
    put(stack, FUEL195_II_COUNT, int32_t(1));
    put(stack, FUEL195_II_EMPTY, uint8_t(0));
    const int already_lit_calls = lit_calls;
    fuel195_tick_wrapper(be);
    CHECK_EQ(get<int32_t>(be, FUEL195_OFF_LITTIME), 1);
    CHECK_EQ(get<int32_t>(stack, FUEL195_II_COUNT), 0);
    CHECK_EQ(lit_calls, already_lit_calls);
    CHECK_EQ(changed_calls, 2);
    *tick_rt = fuel195_runtime{};

    // The menu and dynamic casts read the virtual-base displacements and RTTI.
    FakeTable table{}; table.base = 0x10000000;
    put(table.source, 0, uint64_t(0x50));
    put(table.source, 8, uint64_t(0x50));
    put(table.source, 24, uint64_t(table.base + FUEL195_RVA_FBE_RTTI));
    put(table.source, FUEL195_VTABLE_PREFIX + FUEL195_VSLOT_TICK, uint64_t(table.base + FUEL195_RVA_SERVERTICK));
    put(table.source, FUEL195_VTABLE_PREFIX + FUEL195_VSLOT_CANPLACE, uint64_t(table.base + FUEL195_RVA_CANPLACEITEM));
    fuel195_ops ops{}; ops.ctx = &table; ops.read = fake_read; ops.write = fake_write; ops.alloc = fake_alloc;
    fuel195_runtime table_rt{}; uintptr_t vt = 0;
    CHECK_EQ(fuel195_prepare_vtable(&table_rt, &ops, table.base, &vt), 1);
    CHECK_EQ(vt, (uintptr_t)table.copy + FUEL195_VTABLE_PREFIX);
    CHECK_EQ(get<uint64_t>((void *)(vt - 0x20), 0), 0x50);
    CHECK_EQ((uintptr_t)be + get<uint64_t>((void *)(vt - 0x18), 0), (uintptr_t)be + 0x50);
    CHECK_EQ(get<uint64_t>((void *)(vt - 0x10), 0), 0);
    CHECK_EQ(get<uint64_t>((void *)(vt - 0x08), 0), table.base + FUEL195_RVA_FBE_RTTI);
    CHECK_EQ(get<uint64_t>(table.copy, FUEL195_VTABLE_PREFIX + FUEL195_VSLOT_TICK), (uintptr_t)&fuel195_tick_wrapper);
    CHECK_EQ(get<uint64_t>(table.copy, FUEL195_VTABLE_PREFIX + FUEL195_VSLOT_CANPLACE), (uintptr_t)&fuel195_canplace_wrapper);
    CHECK_EQ(fuel195_prepare_vtable(&table_rt, &ops, table.base, &vt), 1);
    CHECK_EQ(table.allocations, 1);
    fuel195_runtime bad_rt{};
    put(table.source, 8, uint64_t(0));
    CHECK_EQ(fuel195_prepare_vtable(&bad_rt, &ops, table.base, &vt), 0);
    CHECK_EQ(table.allocations, 1);

    // Saved block entities are created by a registry of factory pointers. A
    // unique, pinned factory is replaced, while a duplicate is left alone.
    FakeRegistry reg{}; reg.base = 0x10000000; reg.count = 2;
    reg.sentinel[0] = (uintptr_t)reg.first;
    put(reg.first, 0, uint64_t((uintptr_t)reg.second));
    put(reg.second, 0, uint64_t((uintptr_t)reg.sentinel));
    put(reg.first, 0x38, uint64_t(reg.base + FUEL195_RVA_FBE_FACTORY));
    put(reg.second, 0x38, uint64_t(0x8765));
    fuel195_ops regops{}; regops.ctx = &reg; regops.read = registry_read; regops.cas8 = registry_cas;
    fuel195_runtime *global = fuel195_rt(); *global = fuel195_runtime{};
    global->nat.base = reg.base; global->private_vtable = 0x1000;
    CHECK_EQ(fuel195_install_load_factory(global, &regops), 1);
    CHECK_EQ(get<uint64_t>(reg.first, 0x38), (uintptr_t)&fuel195_load_factory_wrapper);
    CHECK_EQ(get<uint64_t>(reg.second, 0x38), 0x8765);
    CHECK_EQ(fuel195_install_load_factory(global, &regops), 1);
    *global = fuel195_runtime{};
    global->nat.base = reg.base; global->private_vtable = 0x1000;
    put(reg.first, 0x38, uint64_t(reg.base + FUEL195_RVA_FBE_FACTORY));
    put(reg.second, 0x38, uint64_t(reg.base + FUEL195_RVA_FBE_FACTORY));
    CHECK_EQ(fuel195_install_load_factory(global, &regops), 0);
    CHECK_EQ(get<uint64_t>(reg.first, 0x38), reg.base + FUEL195_RVA_FBE_FACTORY);
    *global = fuel195_runtime{};
    fuel195_ops objectops{}; objectops.read = object_read; objectops.cas8 = registry_cas;
    global->nat.base = reg.base; global->private_vtable = 0x1000;
    global->origFactory = fake_factory; global->factory_ops = &objectops;
    fake_furnace = reg.base + FUEL195_RVA_FBE_VTABLE;
    CHECK(fuel195_load_factory_wrapper() == &fake_furnace);
    CHECK_EQ(fake_furnace, 0x1000);
    *global = fuel195_runtime{};
    FakeBlockTable block{reg.base, (uint64_t)-(int64_t)0x1e8,
                         reg.base + FUEL195_RVA_BLOCK_RTTI, reg.base + FUEL195_RVA_NEW_FURNACE};
    fuel195_ops blockops{}; blockops.ctx = &block; blockops.read = block_read; blockops.cas8 = block_cas;
    global->nat.base = reg.base; global->private_vtable = 0x1000;
    CHECK_EQ(fuel195_install_new_factory(global, &blockops), 1);
    CHECK_EQ(block.slot, (uintptr_t)&fuel195_new_factory_wrapper);
    CHECK_EQ(fuel195_install_new_factory(global, &blockops), 1);
    global->origNew = fake_new; global->new_ops = &objectops;
    fake_furnace = reg.base + FUEL195_RVA_FBE_VTABLE;
    uintptr_t result[2]{};
    CHECK(fuel195_new_factory_wrapper(result, nullptr, nullptr, 0) == result);
    CHECK_EQ(result[0], (uintptr_t)&fake_furnace);
    CHECK_EQ(fake_furnace, 0x1000);
    *global = fuel195_runtime{};
    global->nat.base = reg.base; global->private_vtable = 0x1000;
    block.slot = reg.base + FUEL195_RVA_NEW_FURNACE; block.typeinfo = 1;
    CHECK_EQ(fuel195_install_new_factory(global, &blockops), 0);
    CHECK_EQ(block.slot, reg.base + FUEL195_RVA_NEW_FURNACE);
    *global = fuel195_runtime{};

    FakeBlockTable menu{reg.base, 0, reg.base + FUEL195_RVA_FUEL_SLOT_RTTI, reg.base + FUEL195_RVA_FUEL_MAY_PLACE};
    fuel195_ops menuops{}; menuops.ctx = &menu; menuops.read = slot_read; menuops.cas8 = slot_cas;
    global->nat.base = reg.base;
    CHECK_EQ(fuel195_install_fuel_slot(global, &menuops), 1);
    CHECK_EQ(menu.slot, (uintptr_t)&fuel195_may_place_wrapper);
    CHECK_EQ(fuel195_install_fuel_slot(global, &menuops), 1);
    global->origMayPlace = fake_may_place;
    CHECK_EQ(fuel195_add(global, item, 1600, 0), 1);
    void *stack_sp = stack;
    CHECK_EQ(fuel195_may_place_wrapper(nullptr, &stack_sp), 1);
    put(stack, FUEL195_II_ITEM, (void *)uintptr_t(0x9999));
    CHECK_EQ(fuel195_may_place_wrapper(nullptr, &stack_sp), 7);
    CHECK_EQ(fuel195_may_place_wrapper(nullptr, nullptr), 7);
    *global = fuel195_runtime{};
    global->nat.base = reg.base; menu.slot = reg.base + FUEL195_RVA_FUEL_MAY_PLACE; menu.typeinfo = 1;
    CHECK_EQ(fuel195_install_fuel_slot(global, &menuops), 0);
    CHECK_EQ(menu.slot, reg.base + FUEL195_RVA_FUEL_MAY_PLACE);
    *global = fuel195_runtime{};

    // Both public declaration forms must resolve to published items, and a
    // failure must not leave a partially populated table in the caller.
    lce_item_desc item_desc{}; item_desc.key = "mod:one"; item_desc.fuel_ticks = 100;
    lce_plan_item item_entry{}; item_entry.desc = &item_desc; item_entry.id = 1;
    lce_fuel_desc fuel_desc{}; fuel_desc.struct_size = sizeof fuel_desc; fuel_desc.item = "minecraft:coal"; fuel_desc.burn_ticks = 200;
    lce_plan_ext ext{}; ext.type = LCE_EXT_FUEL; ext.desc = &fuel_desc;
    lce_plan plan{}; plan.items = &item_entry; plan.item_count = 1; plan.ext = &ext; plan.ext_count = 1; plan.resolve = resolve_item;
    fuel195_table collected{}; char err[128]{};
    CHECK_EQ(fuel195_collect_plan(&plan, item_by_id, &collected, err, sizeof err), LCE_OK);
    CHECK_EQ(collected.count, 2);
    CHECK_EQ(collected.e[0].item, 0x1110);
    CHECK_EQ(collected.e[0].ticks, 100);
    CHECK_EQ(collected.e[1].item, 0x2220);
    CHECK_EQ(collected.e[1].ticks, 200);
    fuel195_table previous = collected;
    fuel_desc.struct_size = sizeof fuel_desc.struct_size;
    CHECK_EQ(fuel195_collect_plan(&plan, item_by_id, &collected, err, sizeof err), LCE_E_INVALID);
    CHECK_EQ(memcmp(&collected, &previous, sizeof collected), 0);
    fuel_desc.struct_size = sizeof fuel_desc;
    fuel_desc.item = "mod:one";
    CHECK_EQ(fuel195_collect_plan(&plan, item_by_id, &collected, err, sizeof err), LCE_E_COLLISION);
    CHECK_EQ(memcmp(&collected, &previous, sizeof collected), 0);
    fuel_desc.item = "mod:missing";
    CHECK_EQ(fuel195_collect_plan(&plan, item_by_id, &collected, err, sizeof err), LCE_E_NOTFOUND);
    CHECK_EQ(memcmp(&collected, &previous, sizeof collected), 0);
    fuel_desc.item = "minecraft:coal"; fuel_desc.burn_ticks = 72001;
    CHECK_EQ(fuel195_collect_plan(&plan, item_by_id, &collected, err, sizeof err), LCE_E_INVALID);
    CHECK_EQ(memcmp(&collected, &previous, sizeof collected), 0);

    // A refusal while attaching any hook restores the earlier hooks.
    FakeInstall all{}; all.table.base = all.registry.base = reg.base;
    all.block = FakeBlockTable{reg.base, (uint64_t)-(int64_t)0x1e8, reg.base + FUEL195_RVA_BLOCK_RTTI, reg.base + FUEL195_RVA_NEW_FURNACE};
    all.menu = FakeBlockTable{reg.base, 0, reg.base + FUEL195_RVA_FUEL_SLOT_RTTI, reg.base + FUEL195_RVA_FUEL_MAY_PLACE};
    all.quick_move = FakeBlockTable{reg.base, 0, reg.base + FUEL195_RVA_FURNACE_MENU_RTTI, reg.base + FUEL195_RVA_QUICK_MOVE};
    all.hopper = FakeBlockTable{reg.base, (uint64_t)-(int64_t)FUEL195_FBE_CONTAINER_OFFSET,
        reg.base + FUEL195_RVA_FBE_RTTI, reg.base + FUEL195_RVA_CONTAINER_CAN_PLACE};
    all.sided_placement = reg.base + FUEL195_RVA_CONTAINER_SIDED;
    put(all.table.source, 0, uint64_t(0x50));
    put(all.table.source, 8, uint64_t(0x50));
    put(all.table.source, 24, uint64_t(reg.base + FUEL195_RVA_FBE_RTTI));
    put(all.table.source, FUEL195_VTABLE_PREFIX + FUEL195_VSLOT_TICK, uint64_t(reg.base + FUEL195_RVA_SERVERTICK));
    put(all.table.source, FUEL195_VTABLE_PREFIX + FUEL195_VSLOT_CANPLACE, uint64_t(reg.base + FUEL195_RVA_CANPLACEITEM));
    all.registry.count = 1; all.registry.sentinel[0] = (uintptr_t)all.registry.first;
    put(all.registry.first, 0, uint64_t((uintptr_t)all.registry.sentinel));
    put(all.registry.first, 0x38, uint64_t(reg.base + FUEL195_RVA_FBE_FACTORY));
    fuel195_ops allops{}; allops.ctx = &all; allops.read = install_read; allops.write = install_write; allops.cas8 = install_cas; allops.alloc = install_alloc;
    *global = fuel195_runtime{}; all.fail_quick_move = 1;
    CHECK_EQ(fuel195_activate(global, &allops, reg.base, &previous), 0);
    CHECK_EQ(get<uint64_t>(all.registry.first, 0x38), reg.base + FUEL195_RVA_FBE_FACTORY);
    CHECK_EQ(all.block.slot, reg.base + FUEL195_RVA_NEW_FURNACE);
    CHECK_EQ(all.menu.slot, reg.base + FUEL195_RVA_FUEL_MAY_PLACE);
    CHECK_EQ(all.quick_move.slot, reg.base + FUEL195_RVA_QUICK_MOVE);
    CHECK_EQ(global->table.count, 0);
    CHECK_EQ(global->installed, 0);
    all.fail_quick_move = 0;
    all.fail_hopper = 1;
    CHECK_EQ(fuel195_activate(global, &allops, reg.base, &previous), 0);
    CHECK_EQ(all.quick_move.slot, reg.base + FUEL195_RVA_QUICK_MOVE);
    CHECK_EQ(all.hopper.slot, reg.base + FUEL195_RVA_CONTAINER_CAN_PLACE);
    CHECK_EQ(global->table.count, 0);
    all.fail_hopper = 0;
    CHECK_EQ(fuel195_activate(global, &allops, reg.base, &previous), 1);
    CHECK_EQ(get<uint64_t>(all.registry.first, 0x38), (uintptr_t)&fuel195_load_factory_wrapper);
    CHECK_EQ(all.block.slot, (uintptr_t)&fuel195_new_factory_wrapper);
    CHECK_EQ(all.menu.slot, (uintptr_t)&fuel195_may_place_wrapper);
    CHECK(all.quick_move.slot != reg.base + FUEL195_RVA_QUICK_MOVE);
    CHECK_EQ(global->quick_move_slot, reg.base + FUEL195_RVA_FURNACE_MENU_VT + FUEL195_VSLOT_QUICK_MOVE);
    CHECK_EQ(global->hopper_slot, reg.base + FUEL195_RVA_CONTAINER_VT + FUEL195_VSLOT_CONTAINER_CAN_PLACE);
    CHECK_EQ(all.hopper.slot, reg.base + FUEL195_RVA_CONTAINER_SIDED);
    CHECK_EQ(global->table.count, 2);
    CHECK_EQ(global->installed, 1);
    const uintptr_t quick_move_wrapper = all.quick_move.slot;
    *global = fuel195_runtime{};
    put(all.registry.first, 0x38, uint64_t(reg.base + FUEL195_RVA_FBE_FACTORY));
    all.block.slot = reg.base + FUEL195_RVA_NEW_FURNACE;
    all.menu.slot = reg.base + FUEL195_RVA_FUEL_MAY_PLACE;
    all.quick_move.slot = reg.base + FUEL195_RVA_QUICK_MOVE;
    all.hopper.slot = reg.base + FUEL195_RVA_CONTAINER_CAN_PLACE;
    all.fail_quick_move = -1;
    CHECK_EQ(fuel195_activate(global, &allops, reg.base, &previous), -1);
    CHECK_EQ(all.quick_move.slot, quick_move_wrapper);
    CHECK_EQ(global->table.count, 2);
    CHECK_EQ(global->installed, -1);
    *global = fuel195_runtime{};
    TEST_MAIN_END("test_fuel195");
}
