// items195 against a fake 1.95 image: host memory laid out at base+RVA for the
// class vtables, equipment-slot and material globals; fake natives reproduce
// the observable field writes of the retail functions (cited in items195.h),
// and model the recipe owner (output-key map + list vectors) and the
// FurnaceRecipes maps exactly as the disassembly describes them.
#include <map>
#include <vector>
#include <stdlib.h>
#include <string.h>
#include "../items195.h"
#include "../../../tests/check.h"

static uintptr_t B;                 // fake image base
static int g_held = 1;
static int g_bad_vptr;              // ctor writes a wrong vptr -> uncertain
static int g_bad_group;             // shaped ctor stores a wrong group -> uncertain
static std::map<unsigned, uintptr_t> g_reg;   // id -> Item*
alignas(16) static uint8_t g_heap[1 << 22];
static size_t g_used;
static unsigned g_allocs;

static void *fake_alloc(size_t n) { n = (n + 15) & ~(size_t)15; if (g_used + n > sizeof g_heap) return nullptr; void *p = g_heap + g_used; g_used += n; memset(p, 0, n); ++g_allocs; return p; }
static uintptr_t Q(uintptr_t a) { return *(uintptr_t *)a; }
template <class T> static T &F(uintptr_t a) { return *(T *)a; }
static void fake_wstring(void *dst, const uint16_t *u, uint64_t n) {
    uintptr_t d = (uintptr_t)dst; uint16_t *h = (uint16_t *)fake_alloc((n + 1) * 2); memcpy(h, u, n * 2);
    F<uint16_t *>(d + 8) = h; F<uint64_t>(d + 0x18) = n; F<uint64_t>(d + 0x20) = n > 7 ? n : 7;
}
static uintptr_t fake_by_id(unsigned id) { auto it = g_reg.find(id); return it == g_reg.end() ? 0 : it->second; }
static void item_ctor(void *o) { uintptr_t x = (uintptr_t)o; F<uintptr_t>(x) = g_bad_vptr ? 0x1234 : B + LCE195X_RVA_ITEM_VT; F<uintptr_t>(x + 0x10) = 1; F<uint32_t>(x + 0x18) = 64; F<uint64_t>(x + 0x90) = 7; }
static void set_max_damage(void *o, int d) { F<int>((uintptr_t)o + 0x1c) = d; }
static void digger(void *o, lce195x_tier *t, uint32_t vt) {
    uintptr_t x = (uintptr_t)o; item_ctor(o); F<uintptr_t>(x) = B + vt; F<uintptr_t>(x + 0xc0) = (uintptr_t)t; F<uint32_t>(x + 0x18) = 1;
    set_max_damage(o, t->uses); F<float>(x + 0xb0) = t->speed; F<float>(x + 0xb4) = t->damage + 2.0f;
}
static void pick(void *o, lce195x_tier *t) { digger(o, t, LCE195X_RVA_PICKAXE_VT); }
static void axe(void *o, lce195x_tier *t) { digger(o, t, LCE195X_RVA_AXE_VT); }
static void shovel(void *o, lce195x_tier *t) { digger(o, t, LCE195X_RVA_SHOVEL_VT); }
static void sword(void *o, lce195x_tier *t) {
    uintptr_t x = (uintptr_t)o; item_ctor(o); F<uintptr_t>(x) = B + LCE195X_RVA_SWORD_VT; F<uintptr_t>(x + 0xa0) = (uintptr_t)t; F<uint32_t>(x + 0x18) = 1;
    set_max_damage(o, t->uses); F<float>(x + 0x9c) = t->damage + 4.0f;
}
static const int kTable[4] = {13, 15, 16, 11};
static void armour(void *o, void *mat, int ri, void *slot) {
    uintptr_t x = (uintptr_t)o, m = (uintptr_t)mat; item_ctor(o); F<uintptr_t>(x) = B + LCE195X_RVA_ARMOUR_VT;
    unsigned idx = F<unsigned>((uintptr_t)slot + 8);
    F<uintptr_t>(x + 0xa0) = (uintptr_t)slot; F<int>(x + 0xa8) = F<int *>(m + 0x30)[idx]; F<float>(x + 0xac) = F<float>(m + 0x48);
    F<int>(x + 0xb0) = ri; F<uintptr_t>(x + 0xb8) = m; F<uint32_t>(x + 0x18) = 1; set_max_damage(o, F<int>(m + 0x28) * kTable[idx]);
}
static void food(void *o, int nut, float sat, int meat) {
    uintptr_t x = (uintptr_t)o; item_ctor(o); F<uintptr_t>(x) = B + LCE195X_RVA_FOOD_VT; F<int>(x + 0x9c) = nut; F<float>(x + 0xa0) = sat; F<uint8_t>(x + 0xa4) = (uint8_t)meat;
}
static void food_effect(void *o, void *mei, float p) { F<uintptr_t>((uintptr_t)o + 0xa8) = (uintptr_t)mei; F<float>((uintptr_t)o + 0xb0) = p; }
static void mei_ctor(void *m, void *eff, int dur, int amp) { F<uintptr_t>((uintptr_t)m) = (uintptr_t)eff; F<int>((uintptr_t)m + 8) = dur; F<int>((uintptr_t)m + 12) = amp; }
// ---- recipes ----
static void stack_ctor(void *s, void *item, int count, int aux) { uintptr_t x = (uintptr_t)s; F<uintptr_t>(x + 0x28) = (uintptr_t)item; F<int>(x + 0x10) = count; F<int>(x + 0x3c) = aux; F<uint8_t>(x + 0x38) = 0; }
static uintptr_t g_empty_stack;
static void shaped_ctor(void *r, int w, int h, void **cells, void *res, int g) {
    uintptr_t x = (uintptr_t)r; F<uintptr_t>(x) = B + LCE195X_RVA_SHAPED_VT; F<int>(x + 0xc) = 0; F<int>(x + 0x10) = -1; F<int>(x + 0x1c) = w; F<int>(x + 0x20) = h;
    F<uintptr_t>(x + 0x28) = (uintptr_t)cells; for (int i = 0; i < w * h; i++) if (!cells[i]) cells[i] = (void *)g_empty_stack;   // 0x601450
    F<uintptr_t>(x + 0x30) = (uintptr_t)res; F<int>(x + 0x24) = g_bad_group ? g + 1 : g; F<uint8_t>(x + 0x38) = 0;
}
static void shapeless_ctor(void *r, void *res, void *vec, int g) {
    uintptr_t x = (uintptr_t)r; F<uintptr_t>(x) = B + LCE195X_RVA_SHAPELESS_VT; F<int>(x + 0x1c) = g; F<uintptr_t>(x + 0x20) = (uintptr_t)res; F<uintptr_t>(x + 0x28) = (uintptr_t)vec;
}
static unsigned id_of(uintptr_t item) { for (auto &kv : g_reg) if (kv.second == item) return kv.first; return 0; }
static unsigned output_key(void *s) { uintptr_t x = (uintptr_t)s; return (id_of(Q(x + 0x28)) << 16) | F<unsigned>(x + 0x3c); }
static void vec_grow(uintptr_t v, int) {
    uintptr_t b = Q(v + 8), e = Q(v + 16), c = Q(v + 24); size_t n = (e - b) / 8, cap = (c - b) / 8, nc = cap ? cap * 2 : 4;
    uintptr_t nb = (uintptr_t)fake_alloc(nc * 8); if (n) memcpy((void *)nb, (void *)b, n * 8);
    F<uintptr_t>(v + 8) = nb; F<uintptr_t>(v + 16) = nb + n * 8; F<uintptr_t>(v + 24) = nb + nc * 8;
}
// unordered_map list: head sentinel at [map+8], size [map+0x10]; node next +0, prev +8, key +0x10, value +0x18
static void list_init(uintptr_t map) { uintptr_t h = (uintptr_t)fake_alloc(0x20); F<uintptr_t>(h) = h; F<uintptr_t>(h + 8) = h; F<uintptr_t>(map + 8) = h; F<uint64_t>(map + 0x10) = 0; }
static uintptr_t list_insert_front(uintptr_t map, size_t node_size) {
    uintptr_t h = Q(map + 8), n = (uintptr_t)fake_alloc(node_size), first = Q(h);
    F<uintptr_t>(n) = first; F<uintptr_t>(n + 8) = h; F<uintptr_t>(first + 8) = n; F<uintptr_t>(h) = n; F<uint64_t>(map + 0x10) += 1; return n;
}
static uintptr_t recipe_bucket(uintptr_t owner, const unsigned *key) {
    uintptr_t h = Q(owner + 8);
    for (uintptr_t n = Q(h); n != h; n = Q(n)) if (F<unsigned>(n + 0x10) == *key) return n + 0x18;
    uintptr_t n = list_insert_front(owner, 0x38); F<unsigned>(n + 0x10) = *key; return n + 0x18;
}
static int g_furnace_dirty;
static uintptr_t furnace_map(uintptr_t map, const uintptr_t *key) {
    uintptr_t h = Q(map + 8);
    for (uintptr_t n = Q(h); n != h; n = Q(n)) if (Q(n + 0x10) == *key) return n + 0x18;
    uintptr_t n = list_insert_front(map, 0x20); F<uintptr_t>(n + 0x10) = *key; if (g_furnace_dirty) F<uint32_t>(n + 0x18) = 5; return n + 0x18;
}
static uint8_t g_effects[32][0x78], g_potions[43][0x38];
static uintptr_t effect_by_id(int id) { return id >= 1 && id <= 31 ? (uintptr_t)g_effects[id] : 0; }
static uintptr_t potion_by_id(int id) { return id >= 0 && id <= 42 ? (uintptr_t)g_potions[id] : 0; }
static int mem_read(void *, uintptr_t a, void *v, size_t n) { memcpy(v, (const void *)a, n); return 1; }
static int mem_write(void *, uintptr_t a, const void *v, size_t n) { memcpy((void *)a, v, n); return 1; }
static int held(void *) { return g_held; }
static int g_orig_repair_calls;
static int orig_repair(void *, void *, void *) { ++g_orig_repair_calls; return 0; }
static int orig_enchant(void *) { return 77; }
static void *name_fn(void *o, void *, void *) { return o; }

static void make_vtable(uint32_t rva, unsigned slots, uintptr_t rtti) {
    uintptr_t *v = (uintptr_t *)(B + rva - 16);
    v[0] = 0; v[1] = rtti;
    for (unsigned i = 0; i < slots; i++) v[2 + i] = B + 0x500000 + rva / 64 + 8 * i;
    v[2 + LCE195X_SLOT_NAME / 8] = B + 0x3af070;
    v[2 + LCE195X_SLOT_FOIL / 8] = B + LCE195X_RVA_FOIL_STOCK;
    v[2 + LCE195X_SLOT_HAND_EQUIPPED / 8] = B + LCE195X_RVA_HAND_EQUIPPED;
    v[2 + LCE195X_SLOT_ENCHANT_VALUE / 8] = (uintptr_t)&orig_enchant;
    v[2 + LCE195X_SLOT_REPAIR / 8] = (uintptr_t)&orig_repair;
}

static lce_item_desc desc(uint16_t kind) { lce_item_desc d; memset(&d, 0, sizeof d); d.struct_size = sizeof d; d.key = "t:x"; d.name = "X"; d.kind = kind; return d; }

int main() {
    const size_t image = 0x1420000;
    uint8_t *img = (uint8_t *)aligned_alloc(4096, image);
    memset(img, 0, image);
    B = (uintptr_t)img;
    const uint8_t always_glint[] = {0xb0, 0x01, 0xc3};
    memcpy(img + LCE195X_RVA_FOIL_ALWAYS, always_glint, sizeof always_glint);
    const uint8_t handheld_getter[] = {0x8a, 0x47, LCE195X_OFF_HAND_EQUIPPED, 0xc3};
    memcpy(img + LCE195X_RVA_HAND_EQUIPPED, handheld_getter, sizeof handheld_getter);
    struct { uint32_t rva; unsigned slots; } classes[] = {{LCE195X_RVA_ITEM_VT, 56}, {LCE195X_RVA_PICKAXE_VT, 56}, {LCE195X_RVA_AXE_VT, 56}, {LCE195X_RVA_SHOVEL_VT, 56},
                                                         {LCE195X_RVA_SWORD_VT, 57}, {LCE195X_RVA_HOE_VT, 56}, {LCE195X_RVA_ARMOUR_VT, 56}, {LCE195X_RVA_FOOD_VT, 62}, {LCE195X_RVA_SNOWBALLITEM_VT, 56}, {LCE195X_RVA_BOATITEM_VT, 56}};
    for (auto &c : classes) make_vtable(c.rva, c.slots, 0x7000 + c.rva);
    F<uintptr_t>(B + LCE195X_RVA_SNOWBALLITEM_VT + LCE195X_SLOT_USE) = B + LCE195X_RVA_SNOWBALLITEM_USE;
    F<uintptr_t>(B + LCE195X_RVA_BOATITEM_VT + LCE195X_SLOT_USE) = B + LCE195X_RVA_BOATITEM_USE;
    // equipment slots head..feet with index 3..0; iron material with a sound pointer
    static uint8_t slots[4][16];
    for (int i = 0; i < 4; i++) { F<uint32_t>((uintptr_t)slots[i] + 8) = 3 - i; F<uintptr_t>(B + LCE195X_RVA_EQUIP_HEAD + 8 * i) = (uintptr_t)slots[i]; }
    static uint8_t iron[0x50]; F<uintptr_t>((uintptr_t)iron + 0x40) = 0x5050;
    F<uintptr_t>(B + LCE195X_RVA_IRON_MATERIAL) = (uintptr_t)iron;
    static uint8_t stick[0xa0], ingot[0xa0], planks[0xa0], coal[0xa0];
    F<uint32_t>((uintptr_t)stick + 0x28) = 0x1f; F<uint32_t>((uintptr_t)ingot + 0x28) = 0x1f;
    g_reg[280] = (uintptr_t)stick; g_reg[265] = (uintptr_t)ingot; g_reg[5] = (uintptr_t)planks; g_reg[263] = (uintptr_t)coal;
    g_empty_stack = (uintptr_t)fake_alloc(0xa8);

    static lce195x_env env;
    env.base = B;
    env.n = lce195x_natives{fake_alloc, fake_alloc, fake_wstring, fake_by_id, item_ctor, set_max_damage, pick, axe, shovel, sword, armour, food, food_effect, mei_ctor,
                            stack_ctor, shaped_ctor, shapeless_ctor, output_key, recipe_bucket, vec_grow, vec_grow, {furnace_map, furnace_map, furnace_map},
                            effect_by_id, potion_by_id, vec_grow};
    env.m = lce195x_memory{nullptr, mem_read, mem_write, held};

    // ================= checks =================
    char err[512];
    {
        lce_item_desc d = desc(LCE_ITEM_GENERIC);
        CHECK_EQ(lce195x_item_check(&d, err, sizeof err), LCE_OK);
        d.max_damage = 10; CHECK_EQ(lce195x_item_check(&d, err, sizeof err), LCE_E_UNSUPPORTED);
        d = desc(LCE_ITEM_PICKAXE); CHECK_EQ(lce195x_item_check(&d, err, sizeof err), LCE_E_UNSUPPORTED);   // no tier
        d.tier = LCE_TIER_IRON; CHECK_EQ(lce195x_item_check(&d, err, sizeof err), LCE_OK);
        d.max_stack = 4; CHECK_EQ(lce195x_item_check(&d, err, sizeof err), LCE_E_UNSUPPORTED);
        d = desc(LCE_ITEM_HELMET); d.armor_texture = "ruby:layer"; CHECK_EQ(lce195x_item_check(&d, err, sizeof err), LCE_E_UNSUPPORTED);
        CHECK(strstr(err, "0xb040b0") != nullptr);
        d.armor_texture = "minecraft:leather"; CHECK_EQ(lce195x_item_check(&d, err, sizeof err), LCE_E_UNSUPPORTED);
        d.armor_texture = "minecraft:diamond"; CHECK_EQ(lce195x_item_check(&d, err, sizeof err), LCE_OK);
        d = desc(LCE_ITEM_FOOD); CHECK_EQ(lce195x_item_check(&d, err, sizeof err), LCE_E_INVALID);       // hunger 0
        d.food_hunger = 4; d.use_ticks = 20; CHECK_EQ(lce195x_item_check(&d, err, sizeof err), LCE_E_UNSUPPORTED);
        d.use_ticks = 16; CHECK_EQ(lce195x_item_check(&d, err, sizeof err), LCE_OK);
        lce_effect_ref two[2] = {{"minecraft:speed", 100, 0, 0, {0, 0}}, {"minecraft:poison", 100, 0, 0, {0, 0}}};
        d.food_effects = two; d.food_effect_count = 2; CHECK_EQ(lce195x_item_check(&d, err, sizeof err), LCE_E_UNSUPPORTED);
        d = desc(LCE_ITEM_GENERIC); d.fuel_ticks = 1600; CHECK_EQ(lce195x_item_check(&d, err, sizeof err), LCE_E_UNSUPPORTED);
        CHECK(strstr(err, "not yet supported") != nullptr);
        d = desc(LCE_ITEM_GENERIC); d.food_hunger = 3; CHECK_EQ(lce195x_item_check(&d, err, sizeof err), LCE_E_UNSUPPORTED);
        d = desc(LCE_ITEM_GENERIC); d.flags = LCE_ITEM_FLAG_FOIL; CHECK_EQ(lce195x_item_check(&d, err, sizeof err), LCE_OK);
        CHECK_EQ(lce195x_catalog_kind(&d), LCE195_MATERIAL);
        d.kind = LCE_ITEM_SWORD; CHECK_EQ(lce195x_catalog_kind(&d), LCE195_TOOL);
        d.kind = LCE_ITEM_BOOTS; CHECK_EQ(lce195x_catalog_kind(&d), LCE195_ARMOUR);
        d.kind = LCE_ITEM_FOOD; CHECK_EQ(lce195x_catalog_kind(&d), LCE195_CONSUMABLE);
        CHECK_EQ(lce195x_catalog_max_stack(&d), 64); d.max_stack = 16; CHECK_EQ(lce195x_catalog_max_stack(&d), 16);
        d.kind = LCE_ITEM_HOE; CHECK_EQ(lce195x_catalog_max_stack(&d), 1);
    }

    // ================= items =================
    static lce195x_item items[16];
    unsigned next_id = 2283, ni = 0;
    auto build = [&](const lce_item_desc *d, uintptr_t repair, uintptr_t effect) -> lce195x_item & {
        lce195x_item &it = items[ni++];
        lce195x_item_spec s{d, next_id, "lcesdk_t", (uintptr_t)&name_fn, repair, effect};
        int rc = lce195x_item_construct(&env, &it, &s);
        CHECK_EQ(rc, 1);
        if (rc == 1) { g_reg[next_id] = it.object; CHECK_EQ(lce195x_item_identity(&env, &it), 1); }
        ++next_id;
        return it;
    };
    auto vt_ok = [&](const lce195x_item &it, uint32_t rva, unsigned slots) {
        uintptr_t vt = Q(it.object);
        CHECK_EQ(vt, (uintptr_t)&it.vtable[2]);
        CHECK_EQ(((uintptr_t *)vt)[-1], 0x7000 + rva);                       // RTTI kept
        CHECK_EQ(((uintptr_t *)vt)[-2], 0);
        CHECK_EQ(((uintptr_t *)vt)[LCE195X_SLOT_NAME / 8], (uintptr_t)&name_fn);
        CHECK_EQ(((uintptr_t *)vt)[3], ((uintptr_t *)(B + rva))[3]);
        CHECK_EQ(((uintptr_t *)vt)[slots - 1], ((uintptr_t *)(B + rva))[slots - 1]);
        CHECK_EQ(it.slots, slots);
        const uint16_t *icon = F<const uint16_t *>(it.object + 0x78);
        CHECK(icon && icon[0] == 'l' && F<uint64_t>(it.object + 0x88) == 8);
    };
    {   // generic
        lce_item_desc d = desc(LCE_ITEM_GENERIC); d.max_stack = 16; d.enchantability = 7;
        d.flags = LCE_ITEM_FLAG_FOIL | LCE_ITEM_FLAG_HANDHELD;
        lce195x_item &it = build(&d, 0, 0);
        vt_ok(it, LCE195X_RVA_ITEM_VT, 56);
        CHECK_EQ(F<uint64_t>(it.object + 0x28), 0x000000030000001fULL);
        CHECK_EQ(F<uint32_t>(it.object + 0x18), 16);
        CHECK_EQ(lce195x_enchant_dispatch((void *)it.object), 7);
        CHECK_EQ(it.kind, LCE195_MATERIAL);
        CHECK_EQ(it.vtable[2 + LCE195X_SLOT_FOIL / 8], B + LCE195X_RVA_FOIL_ALWAYS);
        CHECK_EQ(F<uint8_t>(it.object + LCE195X_OFF_HAND_EQUIPPED), 1);
    }
    {   // API 1.9 throwable: refused without a throw use, then a SnowballItem private copy with +0x40 = the throw use
        lce_item_desc d = desc(LCE_ITEM_GENERIC); d.flags = LCE_ITEM_FLAG_THROWABLE; d.max_stack = 16;
        CHECK_EQ(lce195x_item_check(&d, err, sizeof err), LCE_OK);
        lce_item_desc bad = desc(LCE_ITEM_SWORD); bad.tier = LCE_TIER_IRON; bad.flags = LCE_ITEM_FLAG_THROWABLE;
        CHECK_EQ(lce195x_item_check(&bad, err, sizeof err), LCE_E_UNSUPPORTED);
        CHECK(strstr(err, "THROWABLE") != nullptr);
        lce195x_item none; memset(&none, 0, sizeof none);
        lce195x_item_spec s0{&d, next_id, "lcesdk_t", (uintptr_t)&name_fn, 0, 0};
        unsigned before = g_allocs;
        CHECK_EQ(lce195x_item_construct(&env, &none, &s0), 0); CHECK_EQ(g_allocs, before);   // no throw use bound: nothing allocated
        static int throw_fn;
        lce195x_set_throw_use((uintptr_t)&throw_fn);
        lce195x_item &it = build(&d, 0, 0);
        vt_ok(it, LCE195X_RVA_SNOWBALLITEM_VT, 56);
        CHECK_EQ(it.vtable[2 + LCE195X_SLOT_USE / 8], (uintptr_t)&throw_fn);
        CHECK_EQ(it.use_base, (uintptr_t)&throw_fn);
        CHECK_EQ(F<uint32_t>(it.object + 0x18), 16);
        // the events195 use route goes over the throw use (identity accepts it), never twice
        static int route_fn;
        g_held = 1;
        CHECK_EQ(lce195x_item_route_use(&env, &it, (uintptr_t)&route_fn), 1);
        CHECK_EQ(it.vtable[2 + LCE195X_SLOT_USE / 8], (uintptr_t)&route_fn);
        CHECK_EQ(lce195x_item_identity(&env, &it), 1);
        it.vtable[2 + LCE195X_SLOT_USE / 8] = B + LCE195X_RVA_SNOWBALLITEM_USE;   // a stray stock use breaks identity
        CHECK_EQ(lce195x_item_identity(&env, &it), 0);
        it.vtable[2 + LCE195X_SLOT_USE / 8] = (uintptr_t)&route_fn;
        lce195x_set_throw_use(0);
    }
#ifdef LCE_HAVE_BOATS_1_10
    {   // API 1.10 boat item: a BoatItem private copy (stock use 0xd7c00), +0x9c = the boat type, stack 1 by default
        lce_item_desc d = desc(LCE_ITEM_GENERIC); d.flags = LCE_ITEM_FLAG_BOAT; d.boat_type = 3;
        CHECK_EQ(lce195x_item_check(&d, err, sizeof err), LCE_OK);
        lce_item_desc bad = d; bad.boat_type = 6;
        CHECK_EQ(lce195x_item_check(&bad, err, sizeof err), LCE_E_INVALID);
        bad = d; bad.flags |= LCE_ITEM_FLAG_THROWABLE;
        CHECK_EQ(lce195x_item_check(&bad, err, sizeof err), LCE_E_UNSUPPORTED);
        bad = d; bad.kind = LCE_ITEM_SWORD; bad.tier = LCE_TIER_IRON; bad.max_damage = 10;
        CHECK_EQ(lce195x_item_check(&bad, err, sizeof err), LCE_E_UNSUPPORTED);
        bad = desc(LCE_ITEM_GENERIC); bad.boat_type = 2;   // a type without the flag
        CHECK_EQ(lce195x_item_check(&bad, err, sizeof err), LCE_E_INVALID);
        lce195x_item &it = build(&d, 0, 0);
        vt_ok(it, LCE195X_RVA_BOATITEM_VT, 56);
        CHECK_EQ(it.vtable[2 + LCE195X_SLOT_USE / 8], B + LCE195X_RVA_BOATITEM_USE);
        CHECK_EQ(F<uint8_t>(it.object + LCE195X_OFF_HAND_EQUIPPED), 0);
        CHECK_EQ(F<uint32_t>(it.object + LCE195X_OFF_BOAT_TYPE), 3);
        CHECK_EQ(F<uint32_t>(it.object + 0x18), 1);
        CHECK_EQ(it.use_base, 0);
        static int route_fn;
        g_held = 1;
        CHECK_EQ(lce195x_item_route_use(&env, &it, (uintptr_t)&route_fn), 1);   // events195 ITEM_USE over the stock BoatItem::use
        CHECK_EQ(lce195x_item_identity(&env, &it), 1);
        lce_item_desc d2 = d; d2.boat_type = 0; d2.max_stack = 16;   // oak, explicit stack
        lce195x_item &o = build(&d2, 0, 0);
        CHECK_EQ(F<uint32_t>(o.object + LCE195X_OFF_BOAT_TYPE), 0);
        CHECK_EQ(F<uint32_t>(o.object + 0x18), 16);
    }
#endif
    {   // pickaxe with overrides and a repair item
        lce_item_desc d = desc(LCE_ITEM_PICKAXE); d.tier = LCE_TIER_DIAMOND; d.max_damage = 2000; d.mining_speed_milli = 9500; d.enchantability = 18; d.repair_item = "minecraft:iron_ingot";
        lce195x_item &it = build(&d, (uintptr_t)ingot, 0);
        vt_ok(it, LCE195X_RVA_PICKAXE_VT, 56);
        CHECK_EQ(F<uintptr_t>(it.object + 0xc0), (uintptr_t)&it.tier);
        CHECK_EQ(it.tier.level, 3); CHECK_EQ(it.tier.uses, 2000); CHECK(it.tier.speed == 9.5f); CHECK_EQ(it.tier.enchantment, 18);
        CHECK_EQ(F<uint32_t>(it.object + 0x1c), 2000); CHECK_EQ(F<uint32_t>(it.object + 0x18), 1);
        CHECK_EQ(F<uint64_t>(it.object + 0x28), 0x0000000500000003ULL);   // pickaxe, diamond
        // enchantability is native for diggers (tier +0x10 via 0x1fd370): slot untouched
        CHECK_EQ(it.vtable[2 + LCE195X_SLOT_ENCHANT_VALUE / 8], (uintptr_t)&orig_enchant);
        CHECK_EQ(it.vtable[2 + LCE195X_SLOT_REPAIR / 8], (uintptr_t)&lce195x_repair_dispatch);
        uint8_t st[0xa8] = {}; uintptr_t sp[2] = {(uintptr_t)st, 0}, sa[2] = {0, 0};
        F<uintptr_t>((uintptr_t)st + 0x28) = (uintptr_t)ingot;
        CHECK_EQ(lce195x_repair_dispatch((void *)it.object, sa, sp), 1);
        F<uintptr_t>((uintptr_t)st + 0x28) = (uintptr_t)stick;
        CHECK_EQ(lce195x_repair_dispatch((void *)it.object, sa, sp), 0);
        CHECK_EQ(g_orig_repair_calls, 1);                                     // forwarded to the class slot
        F<uintptr_t>((uintptr_t)st + 0x28) = (uintptr_t)ingot; F<uint8_t>((uintptr_t)st + 0x38) = 1;   // empty stack never repairs
        CHECK_EQ(lce195x_repair_dispatch((void *)it.object, sa, sp), 0);
        CHECK_EQ(it.kind, LCE195_TOOL);
    }
    {   // axe / shovel / sword / hoe
        lce_item_desc a = desc(LCE_ITEM_AXE); a.tier = LCE_TIER_STONE;
        lce195x_item &ax = build(&a, 0, 0); vt_ok(ax, LCE195X_RVA_AXE_VT, 56);
        CHECK_EQ(F<uint64_t>(ax.object + 0x28), 0x0000000200000004ULL); CHECK_EQ(F<uint32_t>(ax.object + 0x1c), 131);
        lce_item_desc s = desc(LCE_ITEM_SHOVEL); s.tier = LCE_TIER_GOLD;
        lce195x_item &sh = build(&s, 0, 0); vt_ok(sh, LCE195X_RVA_SHOVEL_VT, 56);
        CHECK_EQ(F<uint64_t>(sh.object + 0x28), 0x0000000400000002ULL);
        lce_item_desc w = desc(LCE_ITEM_SWORD); w.tier = LCE_TIER_IRON; w.attack_bonus_milli = 5000;
        lce195x_item &sw = build(&w, 0, 0); vt_ok(sw, LCE195X_RVA_SWORD_VT, 57);
        CHECK(F<float>(sw.object + 0x9c) == 9.0f);
        CHECK_EQ(F<uint64_t>(sw.object + 0x28), 0x0000000300000001ULL);
        lce_item_desc h = desc(LCE_ITEM_HOE); h.tier = LCE_TIER_WOOD;
        lce195x_item &ho = build(&h, 0, 0); vt_ok(ho, LCE195X_RVA_HOE_VT, 56);
        CHECK_EQ(F<uintptr_t>(ho.object + 0xa0), (uintptr_t)&ho.tier);
        CHECK_EQ(F<uint32_t>(ho.object + 0x1c), 59); CHECK(F<float>(ho.object + 0x9c) == 1.0f);
        CHECK_EQ(lce195x_enchant_dispatch((void *)ho.object), 15);           // hoe slot overridden with tier enchant
        CHECK_EQ(lce195x_enchant_dispatch((void *)stick), 0);                 // unknown item: no forward target
    }
    {   // armour: helmet with custom protection and durability, boots with defaults
        lce_item_desc d = desc(LCE_ITEM_HELMET); d.armor_points = 4; d.max_damage = 400; d.armor_texture = "minecraft:diamond";
        lce195x_item &he = build(&d, (uintptr_t)ingot, 0); vt_ok(he, LCE195X_RVA_ARMOUR_VT, 56);
        CHECK_EQ(F<uintptr_t>(he.object + 0xa0), (uintptr_t)slots[0]);
        CHECK_EQ(F<int>(he.object + 0xa8), 4);
        CHECK_EQ(F<int>(he.object + 0xb0), 3);
        CHECK_EQ(F<uintptr_t>(he.object + 0xb8), (uintptr_t)he.material);
        CHECK_EQ(F<uint32_t>(he.object + 0x1c), 400);
        CHECK_EQ(F<uintptr_t>((uintptr_t)he.material + 0x40), 0x5050);       // iron sound pointer
        CHECK_EQ(F<int>((uintptr_t)he.material + 0x38), 10);
        CHECK_EQ(F<uint64_t>(he.object + 0x28), 0x0000000500000007ULL);
        lce_item_desc b = desc(LCE_ITEM_BOOTS);
        lce195x_item &bo = build(&b, 0, 0);
        CHECK_EQ(F<uintptr_t>(bo.object + 0xa0), (uintptr_t)slots[3]);
        CHECK_EQ(F<int>(bo.object + 0xa8), 2); CHECK_EQ(F<int>(bo.object + 0xb0), 2);
        CHECK_EQ(F<uint32_t>(bo.object + 0x1c), 15 * 13);
        CHECK_EQ(F<uint64_t>(bo.object + 0x28), 0x000000030000000aULL);
        CHECK_EQ(bo.kind, LCE195_ARMOUR);
    }
    {   // food with one effect, fast, always edible
        static uint8_t poison[0x40];
        lce_effect_ref eff = {"minecraft:poison", 100, 1, 60, {0, 0}};
        lce_item_desc d = desc(LCE_ITEM_FOOD); d.food_hunger = 6; d.food_saturation_milli = 600; d.use_ticks = 16; d.flags = LCE_ITEM_FLAG_FOOD_ALWAYS;
        d.food_effects = &eff; d.food_effect_count = 1; d.max_stack = 32;
        lce195x_item &fo = build(&d, 0, (uintptr_t)poison); vt_ok(fo, LCE195X_RVA_FOOD_VT, 62);
        CHECK_EQ(F<int>(fo.object + 0x9c), 6); CHECK(F<float>(fo.object + 0xa0) == 0.6f);
        CHECK_EQ(F<uint8_t>(fo.object + 0xa5), 1); CHECK_EQ(F<uint8_t>(fo.object + 0xa6), 1);
        CHECK_EQ(F<uintptr_t>(fo.object + 0xa8), fo.effect_instance);
        CHECK_EQ(F<uintptr_t>(fo.effect_instance), (uintptr_t)poison); CHECK_EQ(F<int>(fo.effect_instance + 8), 100); CHECK_EQ(F<int>(fo.effect_instance + 12), 1);
        CHECK(F<float>(fo.object + 0xb0) == 0.6f);
        CHECK_EQ(F<uint32_t>(fo.object + 0x18), 32);
        CHECK_EQ(fo.kind, LCE195_CONSUMABLE);
        // effect requested but not resolved: refused before allocation
        lce195x_item r; memset(&r, 0, sizeof r);
        lce195x_item_spec s{&d, next_id, "lcesdk_t", 0, 0, 0};
        unsigned before = g_allocs;
        CHECK_EQ(lce195x_item_construct(&env, &r, &s), 0); CHECK_EQ(g_allocs, before);
    }
    {   // refusals and uncertainty
        lce_item_desc d = desc(LCE_ITEM_GENERIC);
        lce195x_item r; memset(&r, 0, sizeof r);
        lce195x_item_spec s{&d, 280, "lcesdk_t", 0, 0, 0};                    // below the SDK window
        CHECK_EQ(lce195x_item_construct(&env, &r, &s), 0);
        memset(&r, 0, sizeof r); s.id = 2283;                                  // occupied
        unsigned before = g_allocs;
        CHECK_EQ(lce195x_item_construct(&env, &r, &s), 0); CHECK_EQ(g_allocs, before);
        memset(&r, 0, sizeof r); s.id = 2390; g_held = 0;                      // wrong thread
        CHECK_EQ(lce195x_item_construct(&env, &r, &s), 0); CHECK_EQ(g_allocs, before); g_held = 1;
        memset(&r, 0, sizeof r); g_bad_vptr = 1;                               // ctor result does not verify
        CHECK_EQ(lce195x_item_construct(&env, &r, &s), -1); CHECK_EQ(r.state, 4u); g_bad_vptr = 0;
        CHECK_EQ(lce195x_item_construct(&env, &r, &s), 0);                    // never retried
        lce195x_env other = env;                                               // second env refused
        memset(&r, 0, sizeof r); s.id = 2391;
        CHECK_EQ(lce195x_item_construct(&other, &r, &s), 0);
        // identity breaks when the vptr is reset to the stock table
        uintptr_t saved = Q(items[0].object);
        F<uintptr_t>(items[0].object) = B + LCE195X_RVA_ITEM_VT;
        CHECK_EQ(lce195x_item_identity(&env, &items[0]), 0);
        F<uintptr_t>(items[0].object) = saved;
        CHECK_EQ(lce195x_item_identity(&env, &items[0]), 1);
    }

    // ================= recipes =================
    alignas(16) static uint8_t owner_mem[0x200];
    uintptr_t owner = (uintptr_t)owner_mem;
    list_init(owner);
    static uintptr_t stock_recipes[3] = {0x111, 0x222, 0x333};
    vec_grow(owner + 0x50, 1);
    for (auto r : stock_recipes) { F<uintptr_t>(Q(owner + 0x60)) = r; F<uintptr_t>(owner + 0x60) += 8; }
    {   // a stock recipe for planks already owns key 5<<16
        unsigned k = 5u << 16; uintptr_t v = recipe_bucket(owner, &k); vec_grow(v, 1); F<uintptr_t>(Q(v + 0x10)) = 0x111; F<uintptr_t>(v + 0x10) += 8;
    }
    uintptr_t ruby = items[0].object;        // generic item 2283, classification 0x1f
    auto base_recipe = [&]() { lce195x_recipe r; memset(&r, 0, sizeof r); r.abi = LCE195X_RECIPE_ABI; r.size = sizeof r; r.provider = "lce-sdk"; return r; };
    std::vector<lce195x_recipe> defs;
    {
        lce195x_recipe r = base_recipe(); r.key = "t:ruby_x4";       // shaped 2x1 -> 4 rubies aux 2, aux-specific planks
        r.width = 2; r.height = 1; r.output_id = 2283; r.output = ruby; r.output_count = 4; r.output_aux = 2; r.classification = 0x1f; r.category = 1;
        r.cells[0] = {5, 3, (uintptr_t)planks}; r.cells[1] = {280, -1, (uintptr_t)stick};
        defs.push_back(r);
        lce195x_recipe s = base_recipe(); s.key = "t:ruby_shapeless";  // shapeless 3 ingredients
        s.shapeless = 1; s.ingredient_count = 3; s.output_id = 2283; s.output = ruby; s.output_count = 1; s.classification = 0x1f; s.category = 6; s.shared_output = 1;
        s.cells[0] = {263, -1, (uintptr_t)coal}; s.cells[1] = {263, -1, (uintptr_t)coal}; s.cells[2] = {280, 0, (uintptr_t)stick};
        defs.push_back(s);
        lce195x_recipe t = s; t.key = "t:ruby_shapeless2"; t.ingredient_count = 1; t.cells[1] = {}; t.cells[2] = {}; t.cells[0] = {265, -1, (uintptr_t)ingot};
        defs.push_back(t);                                          // same output key, both opt in
        lce195x_recipe p = base_recipe(); p.key = "t:planks_extra";  // joins the stock planks bucket
        p.width = 1; p.height = 1; p.output_id = 5; p.output = (uintptr_t)planks; p.output_count = 2; p.shared_output = 1; p.category = 1;
        p.cells[0] = {280, -1, (uintptr_t)stick};
        defs.push_back(p);
    }
    static lce195x_recipe_publisher pub;
    {   // refusals before allocation
        unsigned before = g_allocs;
        lce195x_recipe bad = defs[3]; bad.shared_output = 0;         // collides with the stock planks bucket
        pub = lce195x_recipe_publisher{}; pub.env = &env; pub.owner = owner;
        CHECK_EQ(lce195x_recipes_publish(&pub, &bad, 1), 0);
        lce195x_recipe two[2] = {defs[1], defs[2]}; two[1].shared_output = 0;   // same key in batch without opt-in
        pub = lce195x_recipe_publisher{}; pub.env = &env; pub.owner = owner;
        CHECK_EQ(lce195x_recipes_publish(&pub, two, 2), 0);
        lce195x_recipe hole = defs[1]; hole.cells[1] = {};          // shapeless with a hole
        pub = lce195x_recipe_publisher{}; pub.env = &env; pub.owner = owner;
        CHECK_EQ(lce195x_recipes_publish(&pub, &hole, 1), 0);
        lce195x_recipe cls = defs[0]; cls.classification = 3;       // classification must match +0x28
        pub = lce195x_recipe_publisher{}; pub.env = &env; pub.owner = owner;
        CHECK_EQ(lce195x_recipes_publish(&pub, &cls, 1), 0);
        lce195x_recipe aux = defs[0]; aux.cells[0].aux = -2;
        pub = lce195x_recipe_publisher{}; pub.env = &env; pub.owner = owner;
        CHECK_EQ(lce195x_recipes_publish(&pub, &aux, 1), 0);
        CHECK_EQ(g_allocs, before);
        CHECK_EQ(F<uint64_t>(owner + 0x10), 1);                       // map untouched
    }
    CHECK_EQ(lce195x_recipe_snapshot(owner, nullptr, nullptr, 0), 0);
    // provider registry drives the publish inside the "crafting owner callback"
    struct Ctx { std::vector<lce195x_recipe> *defs; int rc; } ctx{&defs, 0};
    CHECK_EQ(lce195x_recipe_provider_register("lce-sdk", &ctx, [](void *c, uintptr_t o) -> int {
        Ctx *x = (Ctx *)c; pub = lce195x_recipe_publisher{}; pub.env = nullptr; pub.owner = o;
        extern lce195x_env *g_test_env; pub.env = g_test_env;
        return x->rc = lce195x_recipes_publish(&pub, x->defs->data(), (unsigned)x->defs->size()); }), 1);
    CHECK_EQ(lce195x_recipe_provider_register("lce-sdk", &ctx, [](void *, uintptr_t) { return 1; }), 0);   // duplicate id
    extern lce195x_env *g_test_env; g_test_env = &env;
    CHECK_EQ(lce195x_recipe_providers_publish(owner), 1);
    CHECK_EQ(ctx.rc, 1);
    CHECK_EQ(lce195x_recipe_provider_status("lce-sdk"), 2);
    CHECK_EQ(lce195x_recipe_providers_publish(owner), 0);                  // one-shot
    CHECK_EQ(lce195x_recipe_provider_register("late", &ctx, [](void *, uintptr_t) { return 1; }), 0);
    {
        // list vector: stock order kept, ours appended
        uintptr_t b = Q(owner + 0x58), e = Q(owner + 0x60);
        CHECK_EQ((e - b) / 8, 3 + defs.size());
        for (int i = 0; i < 3; i++) CHECK_EQ(F<uintptr_t>(b + 8 * i), stock_recipes[i]);
        for (unsigned i = 0; i < defs.size(); i++) CHECK_EQ(F<uintptr_t>(b + 8 * (3 + i)), pub.recipes[i]);
        // shaped: 2x1, result 4 x aux 2, cells with aux 3 / -1
        uintptr_t r0 = pub.recipes[0];
        CHECK_EQ(Q(r0), B + LCE195X_RVA_SHAPED_VT); CHECK_EQ(F<int>(r0 + 0xc), 0x1f); CHECK_EQ(F<int>(r0 + 0x10), -1);
        uintptr_t res = Q(r0 + 0x30); CHECK_EQ(F<int>(res + 0x10), 4); CHECK_EQ(F<int>(res + 0x3c), 2);
        uintptr_t *cells = (uintptr_t *)Q(r0 + 0x28); CHECK_EQ(F<int>(cells[0] + 0x3c), 3); CHECK_EQ(F<int>(cells[1] + 0x3c), -1);
        // shapeless: vector of 3 stacks, class/-1 written
        uintptr_t r1 = pub.recipes[1];
        CHECK_EQ(Q(r1), B + LCE195X_RVA_SHAPELESS_VT); CHECK_EQ(F<int>(r1 + 0x1c), 0); CHECK_EQ(F<int>(r1 + 0x10), -1);
        uintptr_t v = Q(r1 + 0x28); CHECK_EQ((Q(v + 0x10) - Q(v + 8)) / 8, 3);
        CHECK_EQ(F<int>(F<uintptr_t>(Q(v + 8) + 16) + 0x3c), 0);
        // buckets: ruby aux0 has 2 shapeless, ruby aux2 has 1, planks has stock + ours
        auto bucket_len = [&](unsigned key) { uintptr_t h = Q(owner + 8); for (uintptr_t n = Q(h); n != h; n = Q(n)) if (F<unsigned>(n + 0x10) == key) return (long)((Q(n + 0x28) - Q(n + 0x20)) / 8); return -1L; };
        CHECK_EQ(bucket_len((2283u << 16) | 2), 1); CHECK_EQ(bucket_len(2283u << 16), 2); CHECK_EQ(bucket_len(5u << 16), 2);
        uintptr_t out[8]; unsigned cats[8];
        CHECK_EQ(lce195x_recipe_snapshot(owner, out, cats, 8), 4);
        CHECK_EQ(out[0], r0); CHECK_EQ(cats[0], 1); CHECK_EQ(cats[1], 6);
        CHECK_EQ(lce195x_recipe_snapshot(owner, out, cats, 3), 0);           // never partial
        CHECK_EQ(lce195x_recipe_snapshot(owner + 8, out, cats, 8), 0);       // other owner
        // merged with the r72 list
        lce195x_bind_r72_snapshot([](uintptr_t, uintptr_t *o, unsigned *c, unsigned cap) -> unsigned { if (cap < 2) return 0; o[0] = 0xaa0; o[1] = 0xbb0; c[0] = 0; c[1] = 6; return 2; });
        CHECK_EQ(lce195x_recipe_snapshot_all(owner, out, cats, 8), 6);
        CHECK_EQ(out[0], 0xaa0); CHECK_EQ(out[2], r0);
        CHECK_EQ(lce195x_recipe_snapshot_all(owner, out, cats, 5), 0);
        lce195x_bind_r72_snapshot([](uintptr_t, uintptr_t *, unsigned *, unsigned) -> unsigned { return 0; });
        CHECK_EQ(lce195x_recipe_snapshot_all(owner, out, cats, 8), 4);
    }
    {   // uncertain after allocation: ctor stores a wrong group
        alignas(16) static uint8_t owner2[0x200]; uintptr_t o2 = (uintptr_t)owner2; list_init(o2);
        lce195x_recipe r = defs[0]; r.key = "t:other"; r.output_aux = 5;
        static lce195x_recipe_publisher p0; p0.env = &env; p0.owner = o2;
        vec_grow(o2 + 0x50, 1);
        CHECK_EQ(lce195x_recipes_publish(&p0, &r, 1), 0);                     // a second recipe owner is refused
        static lce195x_recipe_publisher p2; p2.env = &env; p2.owner = owner;
        g_bad_group = 1;
        CHECK_EQ(lce195x_recipes_publish(&p2, &r, 1), -1); CHECK_EQ(p2.state, 4u);
        g_bad_group = 0;
        CHECK_EQ(lce195x_recipes_publish(&p2, &r, 1), 0);                     // never retried
    }

    // ================= smelting =================
    alignas(16) static uint8_t fr[0xf8]; uintptr_t fo = (uintptr_t)fr;
    for (uintptr_t off : {0x0, 0x50, 0xa8}) list_init(fo + off);
    {   // two stock entries: planks -> coal (xp 0.15, mask 1)
        uintptr_t in = (uintptr_t)fake_alloc(0xa8), out = (uintptr_t)fake_alloc(0xa8);
        stack_ctor((void *)in, planks, 1, -1); stack_ctor((void *)out, coal, 1, 0);
        uintptr_t k = in; F<uintptr_t>(furnace_map(fo, &k)) = out; k = out; F<float>(furnace_map(fo + 0x50, &k)) = 0.15f; k = in; F<uint32_t>(furnace_map(fo + 0xa8, &k)) = 1;
    }
    lce195x_smelt sm[2] = {{ruby, -1, (uintptr_t)ingot, 2, 0, 0.7f, LCE195X_COOK_FURNACE | LCE195X_COOK_BLAST},
                           {(uintptr_t)stick, 0, ruby, 1, 0, 0.1f, 0}};
    static lce195x_furnace_publisher fp;
    {
        lce195x_smelt dup[2] = {sm[0], sm[0]};
        fp.env = &env; fp.owner = fo; unsigned before = g_allocs;
        CHECK_EQ(lce195x_furnace_publish(&fp, dup, 2), 0); CHECK_EQ(g_allocs, before);
        lce195x_smelt bad = sm[0]; bad.mask = 0x10;
        fp = lce195x_furnace_publisher{}; fp.env = &env; fp.owner = fo;
        CHECK_EQ(lce195x_furnace_publish(&fp, &bad, 1), 0);
        fp = lce195x_furnace_publisher{}; fp.env = &env; fp.owner = fo; g_held = 0;
        CHECK_EQ(lce195x_furnace_publish(&fp, sm, 2), 0); g_held = 1;
    }
    fp = lce195x_furnace_publisher{}; fp.env = &env; fp.owner = fo;
    CHECK_EQ(lce195x_furnace_publish(&fp, sm, 2), 1);
    {
        CHECK_EQ(F<uint64_t>(fo + 0x10), 3); CHECK_EQ(F<uint64_t>(fo + 0x60), 3); CHECK_EQ(F<uint64_t>(fo + 0xb8), 3);
        uintptr_t first = Q(Q(fo + 8));                                       // front-inserted: newest first
        CHECK_EQ(Q(first + 0x10), fp.inputs[1]); CHECK_EQ(Q(first + 0x18), fp.outputs[1]);
        uintptr_t xp = Q(Q(fo + 0x58)); CHECK_EQ(Q(xp + 0x10), fp.outputs[1]); CHECK(F<float>(xp + 0x18) == 0.1f);
        uintptr_t mask = Q(Q(fo + 0xb0)); CHECK_EQ(F<uint32_t>(mask + 0x18), 1);
        uintptr_t second = Q(Q(Q(fo + 0xb0))); CHECK_EQ(F<uint32_t>(second + 0x18), 3);
        CHECK_EQ(F<int>(fp.outputs[0] + 0x10), 2); CHECK_EQ(F<int>(fp.inputs[0] + 0x3c), -1);
    }
    {   // node not fresh (non-zero value) -> uncertain
        alignas(16) static uint8_t fr2[0xf8]; uintptr_t f2 = (uintptr_t)fr2;
        for (uintptr_t off : {0x0, 0x50, 0xa8}) list_init(f2 + off);
        static lce195x_furnace_publisher fp2; fp2.env = &env; fp2.owner = f2;
        g_furnace_dirty = 1;
        CHECK_EQ(lce195x_furnace_publish(&fp2, sm, 1), -1); g_furnace_dirty = 0;
        CHECK_EQ(lce195x_furnace_publish(&fp2, sm, 1), 0);
    }

    // ================= effects / brewing =================
    {
        CHECK_EQ(lce195x_stock_effect_id("minecraft:speed"), 1); CHECK_EQ(lce195x_stock_effect_id("minecraft:poison"), 19);
        CHECK_EQ(lce195x_stock_effect_id("minecraft:village_hero"), 31); CHECK_EQ(lce195x_stock_effect_id("ruby:x"), -1);
        CHECK_EQ(lce195x_stock_potion_id("minecraft:awkward"), 4); CHECK_EQ(lce195x_stock_potion_id("minecraft:long_slow_falling"), 42);
        lce_effect_desc ef; memset(&ef, 0, sizeof ef);
        CHECK_EQ(lce195x_check_ext(LCE_EXT_EFFECT, &ef, err, sizeof err), LCE_E_UNSUPPORTED); CHECK(strstr(err, "0x490ed0") != nullptr);
        lce_potion_desc po; memset(&po, 0, sizeof po);
        CHECK_EQ(lce195x_check_ext(LCE_EXT_POTION, &po, err, sizeof err), LCE_E_UNSUPPORTED);
        lce_enchant_desc en; memset(&en, 0, sizeof en);
        CHECK_EQ(lce195x_check_ext(LCE_EXT_ENCHANT, &en, err, sizeof err), LCE_E_UNSUPPORTED);
        lce_fuel_desc fu = {sizeof fu, "minecraft:stick", 100};
        CHECK_EQ(lce195x_check_ext(LCE_EXT_FUEL, &fu, err, sizeof err), LCE_E_UNSUPPORTED);
        lce_brew_desc br = {sizeof br, "t:b", "minecraft:awkward", "t:ruby", "minecraft:strength"};
        CHECK_EQ(lce195x_check_ext(LCE_EXT_BREW, &br, err, sizeof err), LCE_OK);
        br.output_potion = "t:mypotion"; CHECK_EQ(lce195x_check_ext(LCE_EXT_BREW, &br, err, sizeof err), LCE_E_UNSUPPORTED);
        br.output_potion = "minecraft:awkward"; CHECK_EQ(lce195x_check_ext(LCE_EXT_BREW, &br, err, sizeof err), LCE_E_INVALID);
        CHECK(lce195x_capabilities() & LCE_CAP_BREWING); CHECK(!(lce195x_capabilities() & LCE_CAP_FUEL));
        // stock mix vector at base+0x142e948 with one mix awkward + stick -> strength
        uintptr_t vec = B + LCE195X_RVA_POTION_MIXES; vec_grow(vec, 1);
        uintptr_t pred = (uintptr_t)fake_alloc(0x18), mix = (uintptr_t)fake_alloc(0x18);
        F<uintptr_t>(pred) = B + LCE195X_RVA_MIX_PREDICATE_VT; F<uintptr_t>(pred + 8) = (uintptr_t)stick; F<int>(pred + 16) = -1;
        F<uintptr_t>(mix) = potion_by_id(4); F<uintptr_t>(mix + 8) = pred; F<uintptr_t>(mix + 16) = potion_by_id(31);
        F<uintptr_t>(Q(vec + 16)) = mix; F<uintptr_t>(vec + 16) += 8;
        static lce195x_brew_publisher bp;
        lce195x_brew dup = {4, 25, (uintptr_t)stick, -1};
        bp.env = &env; unsigned before = g_allocs;
        CHECK_EQ(lce195x_brew_publish(&bp, &dup, 1), 0); CHECK_EQ(g_allocs, before);           // (awkward, stick) already mixed
        lce195x_brew bad = {4, 99, ruby, -1};
        bp = lce195x_brew_publisher{}; bp.env = &env;
        CHECK_EQ(lce195x_brew_publish(&bp, &bad, 1), 0);                                     // unknown potion id
        lce195x_brew ok[5];
        for (int i = 0; i < 5; i++) ok[i] = {4 + i, 25, i ? (uintptr_t)ingot : ruby, -1};
        ok[1].from_potion = 5;
        bp = lce195x_brew_publisher{}; bp.env = &env;
        CHECK_EQ(lce195x_brew_publish(&bp, ok, 5), 1);                                       // forces a vector grow (cap 4)
        CHECK_EQ((Q(vec + 16) - Q(vec + 8)) / 8, 6);
        CHECK_EQ(F<uintptr_t>(Q(vec + 8)), mix);
        uintptr_t m1 = F<uintptr_t>(Q(vec + 8) + 8);
        CHECK_EQ(Q(m1), potion_by_id(4)); CHECK_EQ(Q(m1 + 16), potion_by_id(25));
        CHECK_EQ(Q(Q(m1 + 8)), B + LCE195X_RVA_MIX_PREDICATE_VT); CHECK_EQ(Q(Q(m1 + 8) + 8), ruby); CHECK_EQ(F<int>(Q(m1 + 8) + 16), -1);
        CHECK_EQ(lce195x_brew_publish(&bp, ok, 5), 0);                                       // one-shot
    }

    // ================= plan adapters =================
    {
        lce_recipe_desc rd_; memset(&rd_, 0, sizeof rd_); rd_.struct_size = sizeof rd_; rd_.key = "t:adapted"; rd_.width = 2; rd_.height = 2; rd_.result_count = 3; rd_.result_aux = 1;
        lce_plan_recipe pr; memset(&pr, 0, sizeof pr); pr.desc = &rd_; pr.result_id = 2283; pr.craft_group = 0;
        for (auto &c : pr.cells) c = {-1, 0};
        pr.cells[1] = {280, -1}; pr.cells[3] = {5, 2};               // 3-wide grid: (1,0) and (0,1)
        lce195x_recipe r;
        CHECK_EQ(lce195x_recipe_from_plan(&env, &pr, "lce-sdk", &r), 1);
        CHECK_EQ(r.width, 2); CHECK_EQ(r.cells[1].id, 280); CHECK_EQ(r.cells[2].id, 5); CHECK_EQ(r.cells[2].aux, 2); CHECK_EQ(r.cells[0].id, 0);
        CHECK_EQ(r.output_count, 3); CHECK_EQ(r.output_aux, 1); CHECK_EQ(r.classification, 0x1f); CHECK_EQ(r.shared_output, 1);
        rd_.shapeless = 1;
        CHECK_EQ(lce195x_recipe_from_plan(&env, &pr, "lce-sdk", &r), 1);
        CHECK_EQ(r.shapeless, 1); CHECK_EQ(r.ingredient_count, 2); CHECK_EQ(r.width, 0); CHECK_EQ(r.cells[0].id, 280); CHECK_EQ(r.cells[1].id, 5);
        pr.cells[4] = {9999, 0};
        CHECK_EQ(lce195x_recipe_from_plan(&env, &pr, "lce-sdk", &r), 0);  // unknown ingredient
        lce_smelt_desc sd; memset(&sd, 0, sizeof sd); sd.result_count = 2; sd.xp_milli = 700;
        lce_plan_smelt ps; ps.desc = &sd; ps.input = {2283, -1}; ps.result_id = 265;
        lce195x_smelt so;
        CHECK_EQ(lce195x_smelt_from_plan(&env, &ps, &so), 1);
        CHECK_EQ(so.input_item, ruby); CHECK_EQ(so.output_item, (uintptr_t)ingot); CHECK(so.xp == 0.7f); CHECK_EQ(so.output_count, 2);
        lce_brew_desc bd = {sizeof bd, "t:b", "minecraft:awkward", "t:ruby", "minecraft:swiftness"};
        lce_plan_ext x = {LCE_EXT_BREW, &bd, "t:b", -1};
        lce_plan plan; memset(&plan, 0, sizeof plan);
        plan.resolve = [](const void *, uint32_t k, const char *key) -> int32_t { return k == LCE_REF_ITEM && !strcmp(key, "t:ruby") ? 2283 : -1; };
        lce195x_brew b;
        CHECK_EQ(lce195x_brew_from_plan(&env, &plan, &x, &b), 1);
        CHECK_EQ(b.from_potion, 4); CHECK_EQ(b.to_potion, 14); CHECK_EQ(b.reagent, ruby);
        bd.reagent = "t:none"; CHECK_EQ(lce195x_brew_from_plan(&env, &plan, &x, &b), 0);
    }

    // ================= natives binding (pointer arithmetic only) =================
    {
        lce195x_natives n; memset(&n, 0, sizeof n);
        lce195x_natives_bind(&n, 0x400000);
        CHECK_EQ((uintptr_t)n.shapeless_ctor, 0x400000 + 0x602e10);
        CHECK_EQ((uintptr_t)n.food_ctor, 0x400000 + 0x2d69d0);
        CHECK_EQ((uintptr_t)n.furnace_map[2], 0x400000 + 0x329a00);
    }
    free(img);
    TEST_MAIN_END("test_items195");
}
lce195x_env *g_test_env;
