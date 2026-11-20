// End-to-end test of the composite retail195 backend on a fake 1.95 image.
//
// Real code under test: the loader core, the composite backend + phase
// dispatcher (loader/retail195/composite195.cpp), backend195 with the REAL
// items195 constructors/recipe/furnace/brewing publishers, the REAL blocks195 /
// entities195 / worldgen195 validation + configure steps, the REAL events195
// scope subscriber, item-use routing and command queue, the REAL Lua sandbox
// (modules/script) and ui195 checks. The game-mutating publish steps of
// blocks195/entities195/worldgen195 are replaced by recording fakes through
// lce195c_test_override (each of those modules has its own fake-image suite,
// run by `make test`); here they prove ORDER and FAIL-CLOSED wiring.
//
// Each scenario runs in a forked child, because every module is a
// process-lifetime singleton exactly as on the console.
//
//   test_composite195 <pkgdir>                 run all scenarios
//   test_composite195 --verdicts <mod.lcm>     print the composite's per-content verdicts (tests/test_tools.py)
#include <map>
#include <string>
#include <vector>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../loader/loader.h"
#include "../loader/retail195/composite195.h"
#include "../loader/retail195/id_space_195.h"
#include "../modules/events195/ev195.h"
#include "check.h"
#include "fake_effects195.h"
#include "../integration/sdk_creative_potions.h"
#include "mock_backend.h"

// ============================ fake image ===================================
static const size_t kImage = 0x1480000;
static uintptr_t B;
static std::map<unsigned, uintptr_t> g_reg;   // Item::items by id
alignas(16) static uint8_t g_heap[8 << 20];
static size_t g_used;
static void *fake_alloc(size_t n) { n = (n + 15) & ~(size_t)15; if (g_used + n > sizeof g_heap) return nullptr; void *p = g_heap + g_used; g_used += n; memset(p, 0, n); return p; }
template <class T> static T &F(uintptr_t a) { return *(T *)a; }
static uintptr_t Q(uintptr_t a) { return *(uintptr_t *)a; }
static void fake_wstring(void *dst, const uint16_t *u, uint64_t n) {
    uintptr_t d = (uintptr_t)dst; uint16_t *h = (uint16_t *)fake_alloc((n + 1) * 2); memcpy(h, u, n * 2);
    F<uint16_t *>(d + 8) = h; F<uint64_t>(d + 0x18) = n; F<uint64_t>(d + 0x20) = n > 7 ? n : 7;
}
static uintptr_t by_id(unsigned id) { auto it = g_reg.find(id); return it == g_reg.end() ? 0 : it->second; }
static int g_name_orig;
static void *name_orig(void *o, void *, void *) { ++g_name_orig; return o; }
// --- item classes (field writes as cited in items195.h) ---
static void item_ctor(void *o) { uintptr_t x = (uintptr_t)o; F<uintptr_t>(x) = B + LCE195X_RVA_ITEM_VT; F<uintptr_t>(x + 0x10) = 1; F<uint32_t>(x + 0x18) = 64; F<uint64_t>(x + 0x90) = 7; }
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
static const int kDur[4] = {13, 15, 16, 11};
static void armour(void *o, void *mat, int ri, void *slot) {
    uintptr_t x = (uintptr_t)o, m = (uintptr_t)mat; item_ctor(o); F<uintptr_t>(x) = B + LCE195X_RVA_ARMOUR_VT;
    unsigned idx = F<unsigned>((uintptr_t)slot + 8);
    F<uintptr_t>(x + 0xa0) = (uintptr_t)slot; F<int>(x + 0xa8) = F<int *>(m + 0x30)[idx]; F<float>(x + 0xac) = F<float>(m + 0x48);
    F<int>(x + 0xb0) = ri; F<uintptr_t>(x + 0xb8) = m; F<uint32_t>(x + 0x18) = 1; set_max_damage(o, F<int>(m + 0x28) * kDur[idx]);
}
static void food(void *o, int nut, float sat, int meat) { uintptr_t x = (uintptr_t)o; item_ctor(o); F<uintptr_t>(x) = B + LCE195X_RVA_FOOD_VT; F<int>(x + 0x9c) = nut; F<float>(x + 0xa0) = sat; F<uint8_t>(x + 0xa4) = (uint8_t)meat; }
static void food_effect(void *o, void *mei, float p) { F<uintptr_t>((uintptr_t)o + 0xa8) = (uintptr_t)mei; F<float>((uintptr_t)o + 0xb0) = p; }
static void mei_ctor(void *m, void *eff, int dur, int amp) { F<uintptr_t>((uintptr_t)m) = (uintptr_t)eff; F<int>((uintptr_t)m + 8) = dur; F<int>((uintptr_t)m + 12) = amp; }
// --- recipes ---
static uintptr_t g_empty_stack;
static void stack_ctor(void *s, void *item, int count, int aux) { uintptr_t x = (uintptr_t)s; F<uintptr_t>(x + 0x28) = (uintptr_t)item; F<int>(x + 0x10) = count; F<int>(x + 0x3c) = aux; F<uint8_t>(x + 0x38) = 0; }
static void shaped_ctor(void *r, int w, int h, void **cells, void *res, int g) {
    uintptr_t x = (uintptr_t)r; F<uintptr_t>(x) = B + LCE195X_RVA_SHAPED_VT; F<int>(x + 0x1c) = w; F<int>(x + 0x20) = h; F<int>(x + 0x24) = g;
    F<uintptr_t>(x + 0x28) = (uintptr_t)cells; for (int i = 0; i < w * h; i++) if (!cells[i]) cells[i] = (void *)g_empty_stack;
    F<uintptr_t>(x + 0x30) = (uintptr_t)res;
}
static void shapeless_ctor(void *r, void *res, void *vec, int g) { uintptr_t x = (uintptr_t)r; F<uintptr_t>(x) = B + LCE195X_RVA_SHAPELESS_VT; F<int>(x + 0x1c) = g; F<uintptr_t>(x + 0x20) = (uintptr_t)res; F<uintptr_t>(x + 0x28) = (uintptr_t)vec; }
static unsigned id_of(uintptr_t item) { for (auto &kv : g_reg) if (kv.second == item) return kv.first; return 0; }
static unsigned output_key(void *s) { uintptr_t x = (uintptr_t)s; return (id_of(Q(x + 0x28)) << 16) | F<unsigned>(x + 0x3c); }
static void vec_grow(uintptr_t v, int) {
    uintptr_t b = Q(v + 8), e = Q(v + 16), c = Q(v + 24); size_t n = (e - b) / 8, cap = (c - b) / 8, nc = cap ? cap * 2 : 4;
    uintptr_t nb = (uintptr_t)fake_alloc(nc * 8); if (n) memcpy((void *)nb, (void *)b, n * 8);
    F<uintptr_t>(v + 8) = nb; F<uintptr_t>(v + 16) = nb + n * 8; F<uintptr_t>(v + 24) = nb + nc * 8;
}
static void list_init(uintptr_t map) { uintptr_t h = (uintptr_t)fake_alloc(0x20); F<uintptr_t>(h) = h; F<uintptr_t>(h + 8) = h; F<uintptr_t>(map + 8) = h; F<uint64_t>(map + 0x10) = 0; }
static uintptr_t list_front(uintptr_t map, size_t node) {
    uintptr_t h = Q(map + 8), n = (uintptr_t)fake_alloc(node), first = Q(h);
    F<uintptr_t>(n) = first; F<uintptr_t>(n + 8) = h; F<uintptr_t>(first + 8) = n; F<uintptr_t>(h) = n; F<uint64_t>(map + 0x10) += 1; return n;
}
static uintptr_t recipe_bucket(uintptr_t owner, const unsigned *key) {
    uintptr_t h = Q(owner + 8);
    for (uintptr_t n = Q(h); n != h; n = Q(n)) if (F<unsigned>(n + 0x10) == *key) return n + 0x18;
    uintptr_t n = list_front(owner, 0x38); F<unsigned>(n + 0x10) = *key; return n + 0x18;
}
static uintptr_t furnace_map(uintptr_t map, const uintptr_t *key) {
    uintptr_t h = Q(map + 8);
    for (uintptr_t n = Q(h); n != h; n = Q(n)) if (Q(n + 0x10) == *key) return n + 0x18;
    uintptr_t n = list_front(map, 0x20); F<uintptr_t>(n + 0x10) = *key; return n + 0x18;
}
static uint8_t g_effects[32][0x78], g_potions[43][0x38];
static uintptr_t effect_by_id(int id) { return id >= 1 && id <= 31 ? (uintptr_t)g_effects[id] : 0; }
static uintptr_t potion_by_id(int id) { return id >= 0 && id <= 42 ? (uintptr_t)g_potions[id] : 0; }
static int mem_read(void *, uintptr_t a, void *v, size_t n) { if (!a) return 0; memcpy(v, (const void *)a, n); return 1; }
static int mem_write(void *, uintptr_t a, const void *v, size_t n) { if (!a) return 0; memcpy((void *)a, v, n); return 1; }
static int mem_cas(void *, uintptr_t a, uintptr_t e, uintptr_t d) { return __atomic_compare_exchange_n((uintptr_t *)a, &e, d, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE) ? 1 : 0; }
static int mem_cas8(void *, uintptr_t a, uint64_t e, uint64_t d) { return mem_cas(nullptr, a, e, d); }
static int crafting_held(void *) { return lce195x_recipe_provider_status(LCE195_SDK_PROVIDER) == 1u; }
static uint64_t g_tid = 7;
static uint64_t tid(void *) { return g_tid; }
static int one(void *) { return 1; }
static void dummy() {}
static int orig_enchant(void *) { return 0; }
static int orig_repair(void *, void *, void *) { return 0; }

static void make_vtable(uint32_t rva, unsigned slots) {
    uintptr_t *v = (uintptr_t *)(B + rva - 16);
    v[0] = 0; v[1] = 0x7000 + rva;
    for (unsigned i = 0; i < slots; i++) v[2 + i] = B + 0x500000 + rva / 64 + 8 * i;
    v[2 + LCE195X_SLOT_NAME / 8] = B + 0x3af070;
    v[2 + LCE195X_SLOT_ENCHANT_VALUE / 8] = (uintptr_t)&orig_enchant;
    v[2 + LCE195X_SLOT_REPAIR / 8] = (uintptr_t)&orig_repair;
    if (rva == LCE195X_RVA_ITEM_VT) v[2 + LCE195X_SLOT_USE / 8] = B + 0x3aedb0;   // stock Item::use (events195 routes only this)
}

// ============================ r72 seam stand-ins ============================
extern "C" int lce195_items_window_held(void *) { return 1; }
static std::vector<mc195_items_provider> g_providers;
static int add_provider(const mc195_items_provider *p) { g_providers.push_back(*p); return 1; }
static int catalog_add(lce195_item_catalog *c, const lce195_additive_item *e) {   // acceptance rules of additive_items_195/catalog.c
    if (c->state || c->count >= LCE195_ADDITIVE_CAPACITY || e->abi != LCE195_ADDITIVE_ABI || e->size != sizeof *e || !e->item || e->id < 2268 || e->id >= 2398 ||
        !e->key || !strchr(e->key, ':') || !e->icon || ((e->kind == LCE195_TOOL || e->kind == LCE195_ARMOUR) && e->max_stack != 1)) return 0;
    for (unsigned i = 0; i < c->count; ++i) if (c->entries[i].id == e->id || !strcmp(c->entries[i].key, e->key) || c->entries[i].item == e->item) return 0;
    c->entries[c->count++] = *e;
    return 1;
}

// ============================ events host ==================================
static uintptr_t g_player;
static int ev_read(void *, uintptr_t a, void *v, size_t n) { return mem_read(nullptr, a, v, n); }
static int ev_current(void *, const lce195_view *) { return 1; }
static int ev_is_code(void *, uintptr_t a) { return a >= B && a < B + 0x10c0354 ? 1 : 0; }
static int ev_ww(void *, uintptr_t a, uintptr_t e, uintptr_t d) { return mem_cas(nullptr, a, e, d); }
static uint64_t ev_now(void *) { return 0; }
static std::vector<std::string> g_log;
static void ev_log(void *, const char *l) { g_log.push_back(l); }
// Dinkumware wstring: inline buffer at +8 while capacity < 8, else heap pointer; +0x18 size, +0x20 capacity
static void wstr(uintptr_t at, const char *s) {
    size_t n = strlen(s);
    uint16_t *dst = n < 8 ? (uint16_t *)(at + 8) : (uint16_t *)fake_alloc((n + 1) * 2);
    for (size_t i = 0; i < n; ++i) dst[i] = (uint8_t)s[i];
    dst[n] = 0;
    if (n >= 8) F<uint16_t *>(at + 8) = dst;
    F<uint64_t>(at + 0x18) = n; F<uint64_t>(at + 0x20) = n < 8 ? 7 : n;
}

// ============================ package fs ===================================
static std::map<std::string, std::vector<uint8_t>> files;
static long fs_read(void *, const char *path, uint8_t *buf, size_t cap) {
    auto it = files.find(path); if (it == files.end()) return -1; if (it->second.size() > cap) return -2;
    memcpy(buf, it->second.data(), it->second.size()); return (long)it->second.size();
}
static int fs_write(void *, const char *path, const uint8_t *d, size_t n) { files[path] = std::vector<uint8_t>(d, d + n); return 1; }
static int fs_list(void *, const char *dir, void (*emit)(void *, const char *), void *ctx) {
    std::string pre = std::string(dir) + "/"; std::vector<std::string> seen;
    for (auto &kv : files) {
        if (kv.first.compare(0, pre.size(), pre)) continue;
        std::string rest = kv.first.substr(pre.size()); size_t s = rest.find('/'); if (s == std::string::npos) continue;
        std::string name = rest.substr(0, s); bool dup = false; for (auto &x : seen) dup |= x == name;
        if (!dup) { seen.push_back(name); emit(ctx, name.c_str()); }
    }
    return 1;
}
static int fs_exists(void *, const char *p) { return files.count(p) ? 1 : 0; }
static std::vector<uint8_t> load(const std::string &path) {
    std::vector<uint8_t> v; FILE *f = fopen(path.c_str(), "rb"); if (!f) { fprintf(stderr, "missing %s\n", path.c_str()); return v; }
    int c; while ((c = fgetc(f)) != EOF) v.push_back((uint8_t)c); fclose(f); return v;
}
static void fs_log(void *, const char *l) { g_log.push_back(l); if (getenv("LCE_TEST_VERBOSE")) printf("  | %s\n", l); }
static void clog(const char *l) { g_log.push_back(l); if (getenv("LCE_TEST_VERBOSE")) printf("  > %s\n", l); }
static bool logged(const char *needle) { for (auto &s : g_log) if (s.find(needle) != std::string::npos) return true; return false; }

// ============================ world setup ==================================
static lce195x_natives g_xn;
static b195_natives g_bn;
static le195_natives g_en;
static lce_wg195_natives g_wn;
static uintptr_t g_dispatcher = 0x5150;
static int g_no_level = 1;
static uintptr_t host_dispatcher(void *) { return g_dispatcher; }
static int host_no_level(void *) { return g_no_level; }
static uint64_t host_tid(void *) { return g_tid; }
static uintptr_t g_owner, g_furnace;

static void image() {
    uint8_t *img = (uint8_t *)aligned_alloc(0x4000, kImage);
    memset(img, 0, kImage);
    B = (uintptr_t)img;
    struct { uint32_t rva; unsigned slots; } classes[] = {{LCE195X_RVA_ITEM_VT, 56}, {LCE195X_RVA_PICKAXE_VT, 56}, {LCE195X_RVA_AXE_VT, 56}, {LCE195X_RVA_SHOVEL_VT, 56},
                                                         {LCE195X_RVA_SWORD_VT, 57}, {LCE195X_RVA_HOE_VT, 56}, {LCE195X_RVA_ARMOUR_VT, 56}, {LCE195X_RVA_FOOD_VT, 62}};
    for (auto &c : classes) make_vtable(c.rva, c.slots);
    // API 1.9: SnowballItem (an Item with its own class id/dtors/use) and the Snowball primary vtable (projectiles195 pins)
    make_vtable(LCE195X_RVA_SNOWBALLITEM_VT, 56);
    F<uintptr_t>(B + LCE195X_RVA_SNOWBALLITEM_VT + LCE195X_SLOT_USE) = B + LCE195X_RVA_SNOWBALLITEM_USE;
    make_vtable(LCE195X_RVA_BOATITEM_VT, 56);   // API 1.10: BoatItem (Item + class id/dtors/+0x38/use)
    F<uintptr_t>(B + LCE195X_RVA_BOATITEM_VT + LCE195X_SLOT_USE) = B + LCE195X_RVA_BOATITEM_USE;
    {
        uintptr_t *ball = (uintptr_t *)(B + P195_RVA_SNOWBALL_VT);
        ball[-2] = 0; ball[-1] = 0x7777;
        for (unsigned i = 0; i < P195_SNOWBALL_SLOTS; ++i) ball[i] = B + 0x600000 + 8 * i;
        ball[P195_SLOT_SHOOT / 8] = B + P195_RVA_SHOOT; ball[P195_SLOT_GETOWNER / 8] = B + P195_RVA_GETOWNER; ball[P195_SLOT_ONHIT / 8] = B + P195_RVA_ONHIT;
        ball[P195_SNOWBALL_SLOTS] = (uintptr_t)(intptr_t)P195_SECONDARY_OTT;
    }
    static uint8_t slots[4][16];
    for (int i = 0; i < 4; i++) { F<uint32_t>((uintptr_t)slots[i] + 8) = 3 - i; F<uintptr_t>(B + LCE195X_RVA_EQUIP_HEAD + 8 * i) = (uintptr_t)slots[i]; }
    static uint8_t iron[0x50]; F<uintptr_t>((uintptr_t)iron + 0x40) = 0x5050;
    F<uintptr_t>(B + LCE195X_RVA_IRON_MATERIAL) = (uintptr_t)iron;
    // stock items the fixtures reference: stick 280, bowl 281, wool 35, iron ingot 265, emerald 388, glowstone dust 348
    for (unsigned id : {280u, 281u, 35u, 265u, 388u, 348u}) { uintptr_t it = (uintptr_t)fake_alloc(0xa0); F<uint32_t>(it + 0x28) = 0x1f; g_reg[id] = it; }
    g_empty_stack = (uintptr_t)fake_alloc(0xa8);
    // recipe owner and FurnaceRecipes singleton
    g_owner = (uintptr_t)fake_alloc(0x120); list_init(g_owner);
    g_furnace = (uintptr_t)fake_alloc(0xf8); list_init(g_furnace); list_init(g_furnace + 0x50); list_init(g_furnace + 0xa8);
    // brewing mix vector (+8 begin, +0x10 end, +0x18 cap) with one stock mix
    uintptr_t mixes = B + LCE195X_RVA_POTION_MIXES;
    vec_grow(mixes, 1);
    uintptr_t stock_mix = (uintptr_t)fake_alloc(0x18), stock_pred = (uintptr_t)fake_alloc(0x18);
    F<uintptr_t>(stock_pred) = B + LCE195X_RVA_MIX_PREDICATE_VT; F<uintptr_t>(stock_pred + 8) = g_reg[280];   // awkward-style mix with a stock reagent
    F<uintptr_t>(stock_mix) = (uintptr_t)g_potions[4]; F<uintptr_t>(stock_mix + 8) = stock_pred; F<uintptr_t>(stock_mix + 16) = (uintptr_t)g_potions[28];
    F<uintptr_t>(Q(mixes + 8)) = stock_mix; F<uintptr_t>(mixes + 16) = Q(mixes + 8) + 8;
    memset(&g_xn, 0, sizeof g_xn);
    g_xn = lce195x_natives{fake_alloc, fake_alloc, fake_wstring, by_id, item_ctor, set_max_damage, pick, axe, shovel, sword, armour, food, food_effect, mei_ctor,
                           stack_ctor, shaped_ctor, shapeless_ctor, output_key, recipe_bucket, vec_grow, vec_grow, {furnace_map, furnace_map, furnace_map},
                           effect_by_id, potion_by_id, vec_grow};
    // Blocks/entities/worldgen natives only have to be bound for configure(); their publish steps are recorded fakes.
    void **p = (void **)&g_bn; for (size_t i = 0; i < sizeof g_bn / sizeof(void *); ++i) p[i] = (void *)&dummy;
    p = (void **)&g_en; for (size_t i = 0; i < sizeof g_en / sizeof(void *); ++i) p[i] = (void *)&dummy;
    p = (void **)&g_wn; for (size_t i = 0; i < sizeof g_wn / sizeof(void *); ++i) p[i] = (void *)&dummy;
}

static lce195c g_comp;
static lce_loader *g_l;
static lce195_host g_host;
// API 1.1 registries: effects/potions run their REAL publishers on the fx fake game; strings, enchantments,
// biomes and audio publish through recorded fakes (each module's own suite + Unicorn cover the publishers).
static lce195e_env g_fx;
static lce_b195_natives g_bio; static a195_natives g_au;
static uintptr_t no_call(void *, uintptr_t, const uint64_t *, unsigned, const uint64_t *, unsigned) { return 0; }
static int ro8(void *, uintptr_t, uint64_t, uint64_t) { return 0; }
static void bind_registries(lce195c_bindings &b) {
    fx::reset_effects_game();
    g_fx = fx::make_env();
    b.strings = s195_env{B, nullptr, mem_read, mem_write, nullptr, no_call};
    b.strings_bound = 1;
    b.effects = &g_fx;
    void **p = (void **)&g_bio; for (size_t i = 0; i < sizeof g_bio / sizeof(void *); ++i) p[i] = (void *)&dummy;
    b.biomes = &g_bio; b.biomes_mem = lce_b195_memory{nullptr, mem_read, mem_write, mem_cas8, ro8};
    p = (void **)&g_au; for (size_t i = 0; i < sizeof g_au / sizeof(void *); ++i) p[i] = (void *)&dummy;
    b.audio = &g_au; b.audio_mem = a195_memory{nullptr, mem_read, mem_write, mem_cas};
}

// API 1.9 projectiles195 natives: the stock SnowballItem::use builds a Snowball and shoots it through its (patched) vtable
static uintptr_t g_last_ball; static int g_p_shoots, g_p_onhit; static float g_p_speed, g_p_inacc;
static void p_shoot(void *, void *, float, float, float, float sp, float in) { ++g_p_shoots; g_p_speed = sp; g_p_inacc = in; }
static void *p_use(void *sret, void *, void *level, void *, int) {
    uintptr_t b = (uintptr_t)fake_alloc(0x4c0);
    F<uintptr_t>(b) = B + P195_RVA_SNOWBALL_VT; F<uintptr_t>(b + P195_F_LEVEL) = (uintptr_t)level;
    const double pos[3] = {10.5, 65.25, -4.5};
    memcpy((void *)(b + P195_F_POS), pos, sizeof pos);
    g_last_ball = b;
    uintptr_t f = ((uintptr_t *)Q(b))[P195_SLOT_SHOOT / 8];
    if (f == B + P195_RVA_SHOOT) p_shoot((void *)b, nullptr, 0, 0, 0, 1.5f, 1.0f);   // unpatched: the stock shoot
    else ((void (*)(void *, void *, float, float, float, float, float))f)((void *)b, nullptr, 0, 0, 0, 1.5f, 1.0f);
    return sret;
}
static uint8_t p_onhit(void *, void *) { ++g_p_onhit; return 1; }
static p195_natives g_pn = {p_use, p_shoot, p_onhit};

static lce_loader *boot(bool events = true, bool scripts = true, bool fuel = false) {
    if (events) {
        memset(&g_host, 0, sizeof g_host);
        g_host.image = B; g_host.thread_id = tid; g_host.read = ev_read; g_host.write_word = ev_ww; g_host.is_code = ev_is_code;
        g_host.view_current = ev_current; g_host.now_ms = ev_now; g_host.log = ev_log;
        CHECK_EQ(lce195_ev_init(&g_host, lce195c_event_sink, &g_comp), 1);
    }
    lce195c_bindings b;
    memset(&b, 0, sizeof b);
    b.base = B;
    b.core = lce195_natives{fake_alloc, item_ctor, fake_wstring, by_id, name_orig};
    b.seams = lce195_seams{add_provider, catalog_add, lce195x_recipe_provider_register};
    b.mem = lce195_memory{nullptr, mem_read, mem_write, crafting_held, nullptr};
    b.items = &g_xn;
    static const fuel195_ops fuel_ops = {nullptr, mem_read, mem_write, mem_cas8, nullptr, [](void *, size_t n) -> uintptr_t { return (uintptr_t)fake_alloc(n); }};
    if (fuel) b.fuel = &fuel_ops;
    b.blocks = &g_bn; b.blocks_mem = b195_memory{nullptr, mem_read, mem_write, mem_cas, tid, one, nullptr, nullptr, nullptr, nullptr};
    b.entities = &g_en; b.entities_mem = le195_memory{nullptr, mem_read, mem_write};
    b.worldgen = &g_wn; b.worldgen_mem = lce_wg195_memory{nullptr, mem_read, mem_cas8};
    b.event_caps = events ? lce195_ev_capabilities() : 0;
    if (events) { b.projectiles = &g_pn; b.projectiles_mem = p195_memory{nullptr, mem_read, mem_cas8, mem_cas8, tid}; }   // as sdk_boot: only with events
    b.scripts = scripts;
    b.renderer_dispatcher = host_dispatcher; b.no_level = host_no_level; b.thread_id = host_tid; b.log = clog;
    bind_registries(b);
    lce195c_init(&g_comp, &b);
    lce_loader_config c;
    memset(&c, 0, sizeof c);
    c.root = "/data/lce-mods"; c.ledger_path = "/data/lce-mods/reservations/sdk-ids-v1.txt";
    lce195_default_id_space(&c.space);
    lce195_ext_id_space(&c.space);
    c.space.item_in_use = [](void *, uint32_t id) -> int { return id >= 2398 || lce_vanilla_item_exists(id); };
    c.backend = &g_comp.iface;
    c.host.read_file = fs_read; c.host.write_file_atomic = fs_write; c.host.list_dirs = fs_list; c.host.exists = fs_exists; c.host.log = fs_log;
    g_l = lce_loader_create(&c);
    lce195c_attach(&g_comp, g_l);
    lce_loader_set_script_runner(g_l, lce195c_script_runner, &g_comp);
    lce_loader_discover(g_l);
    return g_l;
}
static const lce_mod_report *mod(const char *id) {
    for (uint32_t i = 0; i < lce_loader_mod_count(g_l); ++i) if (!strcmp(lce_loader_mod(g_l, i)->modid, id)) return lce_loader_mod(g_l, i);
    return nullptr;
}

// ---- recording fakes for the module publish steps (order + inputs) --------
static std::vector<std::string> g_order;
static int g_renderers_rc = 1, g_blocks_rc = 1;
static int f_hook(void *, uintptr_t) { g_order.push_back("block_hook"); return 1; }
static int f_blocks(void *m, uintptr_t) {
    g_order.push_back("blocks");
    // what the relay publish does: block objects exist (for event routing)
    b195_module *b = (b195_module *)m;
    for (uint32_t i = 0; g_blocks_rc == 1 && i < b->count; ++i) { b->blocks[i].object = (uintptr_t)fake_alloc(0x1f0); b->blocks[i].vtable[2 + 0x3e0 / 8] = B + 0x94800;
        for (uint32_t off : {0x1f0u, 0x208u, 0x2c8u, 0x328u, 0x2c0u}) b->blocks[i].vtable[2 + off / 8] = B + 0x93000 + off; }
    return g_blocks_rc;
}
static int f_block_items(void *m, uintptr_t) {
    g_order.push_back("block_items");
    b195_module *b = (b195_module *)m;
    CHECK(!g_reg.empty());
    for (uint32_t i = 0; i < b->count; ++i) {   // BlockItem registered into Item::items (0x3af660), as b195_items_publish does
        uintptr_t it = (uintptr_t)fake_alloc(0xb0); F<uint32_t>(it + 0x28) = 0x1f;
        b->blocks[i].item = it; b->blocks[i].item_vtable[2 + 0x28 / 8] = B + 0x7d4c0;
        g_reg[b->blocks[i].item_id] = it;
        // the drop item (an SDK item) must already be registered: ordering after the item window
        CHECK(by_id((unsigned)b->blocks[i].drop_item) != 0);
    }
    return 1;
}
static int f_types(void *m, uintptr_t) { g_order.push_back("entity_types"); CHECK_EQ(((le195_module *)m)->count, 1u); return 1; }
static int f_spawns(void *, uintptr_t) { g_order.push_back("spawns"); return 1; }
static int f_renderers(void *, uintptr_t d) { g_order.push_back("renderers"); CHECK_EQ(d, g_dispatcher); return g_renderers_rc; }
static int f_worldgen(void *c, uintptr_t) {
    g_order.push_back("worldgen");
    lce195c *x = (lce195c *)c;
    CHECK_EQ(x->feature_count, 2u);
    for (uint32_t i = 0; i < x->feature_count; ++i) {
        const lce_wg195_feature &f = x->features[i].f;
        if (f.kind == LCE_FEATURE_ORE) { CHECK(f.block_id >= 254); CHECK_EQ(f.size, 6); CHECK_EQ(f.max_y, 40); }
        else { CHECK_EQ(f.kind, LCE_FEATURE_STRUCTURE); CHECK(f.structure && f.structure->size_x == 3 && f.structure->palette[0] == (1u << 4)); }
    }
    return 1;
}
static void override_all() {
    lce195c_test_override(LCE195C_PH_BLOCK_HOOK, f_hook);
    lce195c_test_override(LCE195C_PH_BLOCKS, f_blocks);
    lce195c_test_override(LCE195C_PH_AFTER_ITEMS, f_block_items);
    lce195c_test_override(LCE195C_PH_ENTITY_TYPES, f_types);
    lce195c_test_override(LCE195C_PH_SPAWNS, f_spawns);
    lce195c_test_override(LCE195C_PH_RENDERERS, f_renderers);
    lce195c_test_override(LCE195C_PH_WORLDGEN, f_worldgen);
}

// The shipping item window: tools_runtime -> mc195_custom_items_collect -> our collect(), then
// lce195_native_items_publish registers every catalog entry (0x3af660).
static lce195_item_catalog g_catalog;
static int item_window() {
    CHECK_EQ(g_providers.size(), 1u);
    if (g_providers.empty()) return 0;
    mc195_items_provider &p = g_providers[0];
    CHECK_EQ(p.prepare(p.opaque, B), 1);
    static lce195_items_window w;
    int rc = p.collect(p.opaque, &w, &g_catalog);
    if (rc == 1) for (unsigned i = 0; i < g_catalog.count; ++i) g_reg[g_catalog.entries[i].id] = g_catalog.entries[i].item;
    return rc;
}
static uint32_t list_len(uintptr_t map) { return (uint32_t)F<uint64_t>(map + 0x10); }
static uint32_t vec_len(uintptr_t v) { return (uint32_t)((Q(v + 16) - Q(v + 8)) / 8); }

static std::string g_pkg;
static void install(const char *id) { files[std::string("/data/lce-mods/") + id + "/mod.lcm"] = load(g_pkg + "/" + id + "/mod.lcm"); }

static void scenario_lua_texture() {
    image(); override_all();
    install("luatexture"); boot();
    CHECK_EQ(lce_loader_commit(g_l), LCE_OK);
    const lce_mod_report *report = mod("luatexture");
    CHECK(report && report->state == LCE_MOD_LIVE);
    if (!report || report->state != LCE_MOD_LIVE) return;
    CHECK_EQ(report->textures, 2u);
    CHECK_EQ(report->items, 1u);
    CHECK_EQ(report->blocks, 1u);

    CHECK_EQ(lce195c_phase_blocks_hook(&g_comp), 1);
    CHECK_EQ(item_window(), 1);
    CHECK_EQ(g_catalog.count, 1u);
    if (g_catalog.count != 1) return;
    const lce195_additive_item &entry = g_catalog.entries[0];
    CHECK(!strcmp(entry.key, "luatexture:gem"));
    mc195_items_provider &provider = g_providers[0];
    const uint8_t *item_pixels = provider.icon(provider.opaque, entry.icon);
    CHECK(item_pixels != nullptr);
    if (item_pixels) {
        for (unsigned i = 0; i < 256; ++i) {
            const uint8_t rgba[] = {uint8_t((i % 16) * 17), uint8_t((i / 16) * 17), 34, 255};
            CHECK(memcmp(item_pixels + 4 * i, rgba, sizeof rgba) == 0);
        }
    }

    CHECK_EQ(lce195c_phase_after_items(&g_comp), 1);
    CHECK_EQ(lce195c_terrain_tiles(&g_comp), 1u);
    uint8_t tile[16 * 16 * 4] = {};
    CHECK_EQ(lce195c_terrain_compose(&g_comp, 16, 0, tile, sizeof tile), 1);
    for (unsigned i = 0; i < 256; ++i) {
        const uint8_t rgba[] = {240, uint8_t((i % 16) * 17), uint8_t((i / 16) * 17), 191};
        CHECK(memcmp(tile + 4 * i, rgba, sizeof rgba) == 0);
    }

    const lce195c_terrain_lease outer = {[]() -> int {
        const lce195c_terrain_lease inner = {[]() -> int { return 1; }, nullptr};
        CHECK_EQ(lce195c_terrain_detach(&g_comp, &inner), 0);
        return 1;
    }, nullptr};
    CHECK_EQ(lce195c_terrain_detach(&g_comp, &outer), 1);

    lce_ev_tick tick{sizeof tick, 1};
    lce195c_event_sink(&g_comp, LCE_EV_SERVER_TICK, &tick);
    CHECK(logged("texture registration refused during tick"));
    CHECK_EQ(report->textures, 2u);
}

// ============================ scenarios ======================================
// A: full startup in the real order with retailall + rubyretail + allcontent.
static void scenario_full() {
    image(); override_all();
    install("retailall"); install("rubyretail"); install("allcontent");
    boot();
    CHECK_EQ(lce_loader_commit(g_l), LCE_OK);
    const lce_mod_report *ra = mod("retailall"), *rr = mod("rubyretail"), *ac = mod("allcontent");
    CHECK(ra && rr && ac);
    if (!ra || !rr || !ac) return;
    printf("  retailall: state=%u items=%u blocks=%u recipes=%u smelts=%u reason=%s\n", ra->state, ra->items, ra->blocks, ra->recipes, ra->smelts, ra->reason);
    printf("  allcontent: state=%u reason=%s\n", ac->state, ac->reason);
    CHECK_EQ(ra->state, LCE_MOD_LIVE);
    CHECK_EQ(ra->items, 6u);           // 5 package items + 1 registered by scripts/main.lua during REGISTER
    CHECK_EQ(ra->blocks, 1u);
    CHECK_EQ(ra->recipes, 3u);
    CHECK_EQ(ra->smelts, 2u);
    CHECK_EQ(rr->state, LCE_MOD_LIVE);
    // allcontent fails closed as a whole mod (per-content reasons: scenario_verdicts); its script uses an unknown event name
    CHECK(ac->state == LCE_MOD_REJECTED || ac->state == LCE_MOD_DROPPED);
    CHECK(ac->reason[0] != 0);
    CHECK_EQ(lce195c_script_count(&g_comp), 1u);
    const uint32_t caps = lce195c_capabilities(&g_comp);
    const uint32_t want = LCE_CAP_ITEMS | LCE_CAP_BLOCKS | LCE_CAP_RECIPES | LCE_CAP_SMELTING | LCE_CAP_TOOLS | LCE_CAP_ARMOUR | LCE_CAP_FOOD |
                          LCE_CAP_BREWING | LCE_CAP_ENTITIES | LCE_CAP_WORLDGEN | LCE_CAP_STRUCTURES | LCE_CAP_SOUNDS | LCE_CAP_PARTICLES |
                          LCE_CAP_EV_TICK | LCE_CAP_EV_BLOCK | LCE_CAP_EV_ITEM_USE | LCE_CAP_EV_WORLD | LCE_CAP_EV_PLAYER | LCE_CAP_EV_ENTITY |
                          LCE_CAP_COMMANDS | LCE_CAP_KEYBINDS;
    CHECK_EQ(caps & want, want);
    CHECK_EQ(caps & (LCE_CAP_EFFECTS | LCE_CAP_ENCHANTS | LCE_CAP_BIOMES), LCE_CAP_EFFECTS | LCE_CAP_ENCHANTS | LCE_CAP_BIOMES);
    CHECK_EQ(caps & (LCE_CAP_FUEL | LCE_CAP_NET_SYNC), 0u);
    CHECK((caps & LCE_CAP_BLOCK_ENTITY) != 0);   // 9-slot storage blocks (blocks195)
    // nothing touched the "game" during commit
    CHECK_EQ(g_reg.size(), 6u);
    CHECK_EQ(lce195c_phase_state(&g_comp, LCE195C_PH_BLOCK_HOOK), 0u);

    // 2. block registry data hook (startup, before Block init)
    CHECK_EQ(lce195c_phase_blocks_hook(&g_comp), 1);
    // 3+4. game Block init (relay) ... then the item window: every kind through the items195 constructors
    CHECK_EQ(item_window(), 1);
    CHECK_EQ(g_catalog.count, 9u);   // retailall 6 + rubyretail 3
    unsigned kinds[5] = {0};
    for (unsigned i = 0; i < g_catalog.count; ++i) {
        const lce195_additive_item &e = g_catalog.entries[i];
        if (e.kind < 5) ++kinds[e.kind];
        uintptr_t vt = Q(e.item);
        CHECK(vt != B + LCE195X_RVA_ITEM_VT && ((uintptr_t *)vt)[LCE195X_SLOT_NAME / 8] == (uintptr_t)&lce195_sdk_name_dispatch);
        CHECK_EQ(e.max_stack, (e.kind == LCE195_TOOL || e.kind == LCE195_ARMOUR) ? 1u : e.max_stack);
    }
    CHECK_EQ(kinds[LCE195_TOOL], 2u);
    CHECK_EQ(kinds[LCE195_ARMOUR], 1u);
    CHECK_EQ(kinds[LCE195_CONSUMABLE], 1u);
    CHECK_EQ(kinds[LCE195_MATERIAL], 5u);
    CHECK_EQ(g_providers[0].identity(g_providers[0].opaque), 1);
    int32_t gem = lce_loader_item_id(g_l, "retailall:gem"), pickid = lce_loader_item_id(g_l, "retailall:gem_pick"), stew = lce_loader_item_id(g_l, "retailall:gem_stew");
    uintptr_t pick_obj = by_id((unsigned)pickid), stew_obj = by_id((unsigned)stew), gem_obj = by_id((unsigned)gem);
    CHECK_EQ(((uintptr_t *)Q(pick_obj))[-1], 0x7000u + LCE195X_RVA_PICKAXE_VT);                      // RTTI of the class copy kept
    CHECK_EQ(((uintptr_t *)Q(pick_obj))[LCE195X_SLOT_REPAIR / 8], (uintptr_t)&lce195x_repair_dispatch); // SDK repair material
    CHECK_EQ(F<uintptr_t>(stew_obj + 0xa8) != 0, true);                                                  // eat effect (regeneration)
    CHECK_EQ(F<float>(stew_obj + 0xb0), 0.5f);
    CHECK_EQ(((uintptr_t *)Q(gem_obj))[LCE195X_SLOT_USE / 8], (uintptr_t)&lce195_ev_item_use_thunk);     // ITEM_USE routed (generic class)
    // names: en_US string, original for others
    uint8_t out[0x28];
    lce195_sdk_name_dispatch(out, (void *)gem_obj, nullptr);
    CHECK_EQ(F<uint64_t>((uintptr_t)out + 0x18), 3u);
    // 5. after items: blocks relay result + BlockItems (drop item already registered)
    CHECK_EQ(lce195c_phase_after_items(&g_comp), 1);
    CHECK(logged("LCE_SDK phase=items rc=1 9 SDK item(s) registered in Item::items"));
    CHECK_EQ(g_comp.block_routes, 1u);
    {   // API 1.3 routes: block tick/neighbour/step-on/inside on the private block vtable, inventoryTick on generic items
        const b195_block &bl = g_comp_blocks_for_tests()->blocks[0];
        CHECK_EQ(bl.vtable[2 + 0x1f0 / 8], (uintptr_t)&lce195_ev_block_tick_thunk);
        CHECK_EQ(bl.vtable[2 + 0x208 / 8], (uintptr_t)&lce195_ev_block_neighbor_thunk);
        CHECK_EQ(bl.vtable[2 + 0x2c8 / 8], (uintptr_t)&lce195_ev_block_step_on_thunk);
        CHECK_EQ(bl.vtable[2 + 0x328 / 8], (uintptr_t)&lce195_ev_block_inside_thunk);
        CHECK_EQ(((uintptr_t *)Q(gem_obj))[0xc8 / 8], (uintptr_t)&lce195_ev_item_inventory_tick_thunk);
        CHECK((g_comp.iface.capabilities2 & (LCE_CAP2_EV_BLOCK_TICK | LCE_CAP2_EV_NEIGHBOR | LCE_CAP2_EV_BLOCK_COLLIDE | LCE_CAP2_EV_INVENTORY_TICK)) ==
              (LCE_CAP2_EV_BLOCK_TICK | LCE_CAP2_EV_NEIGHBOR | LCE_CAP2_EV_BLOCK_COLLIDE | LCE_CAP2_EV_INVENTORY_TICK));
    }
    CHECK_EQ(g_comp.block_item_routes, 1u);
    {   // terrain atlas seam (gap G1): retailall:gem_ore has one terrain tile, appended after the two fixed tiles
        CHECK_EQ(lce195c_terrain_tiles(&g_comp), 1u);
        std::vector<uint8_t> t16(16 * 16 * 4), t8(8 * 8 * 4), t32(32 * 32 * 4);
        CHECK_EQ(lce195c_terrain_compose(&g_comp, 16, 0, t16.data(), t16.size()), 1);
        CHECK_EQ(lce195c_terrain_compose(&g_comp, 16, 1, t8.data(), t8.size()), 1);
        CHECK_EQ(lce195c_terrain_compose(&g_comp, 32, 0, t32.data(), t32.size()), 1);     // 32 px pack: nearest-neighbour
        CHECK_EQ(lce195c_terrain_compose(&g_comp, 16, 0, t16.data(), t16.size() - 4), 0);
        CHECK(memcmp(t16.data(), t32.data(), 4) == 0 && t16[3] != 0);
        static int held_calls, up_calls;
        held_calls = up_calls = 0;
        const lce195c_terrain_lease none = {nullptr, nullptr};
        const lce195c_terrain_lease lease = {[]() -> int { ++held_calls; return 1; },
                                             [](uintptr_t, uintptr_t, uint32_t) -> int { ++up_calls; return 1; }};
        CHECK_EQ(lce195c_terrain_bind(&g_comp, &none, 0x1000, 1, 256, 640, 624, 16, 2), 0);   // no lease: refused before the module
        CHECK_EQ(lce195c_terrain_bind(&g_comp, nullptr, 0x1000, 1, 256, 640, 624, 16, 2), 0);
        CHECK_EQ(lce195c_terrain_detach(&g_comp, &none), 0);                                  // a detach needs the lease
        CHECK_EQ(lce195c_terrain_detach(&g_comp, &lease), 1);                                 // nothing bound yet
        CHECK(held_calls >= 1);
        char line[400];
        lce195c_status_line(&g_comp, line, sizeof line);
        CHECK(strstr(line, " terrain_tiles=1 binds=0 rc=0 gen=0") != nullptr);
    }
    // 6. crafting pre-cache: ABI-2 providers (one-line r72 change in content_process.cpp)
    CHECK_EQ(lce195x_recipe_providers_publish(g_owner), 1);
    CHECK_EQ(vec_len(g_owner + 0x50), 5u);   // 3 retailall + 2 rubyretail
    bool shapeless = false, count4 = false;
    for (uint32_t i = 0; i < vec_len(g_owner + 0x50); ++i) {
        uintptr_t r = ((uintptr_t *)Q(g_owner + 0x50 + 8))[i];
        if (Q(r) == B + LCE195X_RVA_SHAPELESS_VT) shapeless = true;
        else {
            uintptr_t res = Q(r + 0x30);
            if (F<int>(res + 0x10) == 4) { count4 = true; CHECK_EQ(F<int>(Q(Q(r + 0x28)) + 0x3c), 14); }   // wool@14 matched exactly
        }
    }
    CHECK(shapeless); CHECK(count4);
    // 7. late startup at the title (no Level): smelting, brewing, entity types, spawns, worldgen, renderers
    F<uintptr_t>(B + LCE195_RVA_FURNACE_GLOBAL) = g_furnace;
    g_no_level = 0;
    CHECK_EQ(lce195c_phase_late(&g_comp), 0);                          // a world exists: wait, nothing published
    CHECK_EQ(lce195c_phase_state(&g_comp, LCE195C_PH_SMELT_BREW), (uint32_t)LCE195C_WAITING);
    CHECK_EQ(list_len(g_furnace), 0u);
    g_no_level = 1;
    CHECK_EQ(lce195c_phase_late(&g_comp), 1);
    CHECK_EQ(list_len(g_furnace), 2u);                                 // input -> result (block-item ore, gem)
    CHECK_EQ(list_len(g_furnace + 0x50), 2u);                          // xp
    CHECK_EQ(vec_len(B + LCE195X_RVA_POTION_MIXES), 2u);               // stock mix + ours appended
    if (vec_len(B + LCE195X_RVA_POTION_MIXES) != 2u) printf("  late error: %s\n", g_comp.items.last_error);
    CHECK_EQ(lce195c_world_entry_allowed(&g_comp), 1);
    std::string order; for (auto &s : g_order) order += s + " ";
    printf("  phase order: %s\n", order.c_str());
    CHECK(order == "block_hook blocks block_items entity_types spawns renderers worldgen ");
    CHECK_EQ(lce195c_phase_late(&g_comp), 1);                          // once
    CHECK_EQ(g_order.size(), 7u);
    // 9. server tick through the real events195 subscriber -> loader + script
    g_player = (uintptr_t)fake_alloc(0xc00);
    F<uintptr_t>(g_player) = B + 0x1341168;
    wstr(g_player + 0x2d8, "uuid-1"); wstr(g_player + 0x9a8, "Tester");
    lce195_view v; memset(&v, 0, sizeof v);
    v.image = B; v.player_object = g_player; v.session = 1; v.thread = g_tid;
    for (int i = 0; i < 3; ++i) { lce195_ev_scope_tick(nullptr, &v); lce195_ev_scope_finish(nullptr, g_tid, 1); }
    CHECK_EQ(lce195_ev_server_ticks(), 3u);
    CHECK(logged("joined: Tester"));                                   // script PlayerJoin callback ran
    CHECK(logged("paced tick 2 after 1 skips"));                        // paced script callback ran on its due host tick
    CHECK(!logged("paced tick 1") && !logged("paced tick 3"));
    // cancellable CHAT: script returns true for "!cancel"
    lce_ev_chat chat{sizeof chat, (void *)g_player, "!cancel"};
    CHECK_EQ(lce195c_event_sink(&g_comp, LCE_EV_CHAT, &chat), 1);
    lce_ev_chat chat2{sizeof chat2, (void *)g_player, "hello"};
    CHECK_EQ(lce195c_event_sink(&g_comp, LCE_EV_CHAT, &chat2), 0);
    char line[320]; lce195c_status_line(&g_comp, line, sizeof line); printf("  %s\n", line);
}


// H: the content types that used to be refused: effects (+ food with a mod effect), potions with
// splash/lingering/arrow names, brewing into and out of mod potions, enchantments, a biome, custom audio.
static std::vector<std::string> g_reg_order;
static int f_strings(void *env, uintptr_t) {
    g_reg_order.push_back("strings");
    CHECK(env != nullptr);
    return 1;
}
static int f_enchants(void *rec, uintptr_t) {
    g_reg_order.push_back("enchants");
    e195_record *r = (e195_record *)rec;
    for (uint32_t i = 0; i < g_comp.enchant_count; ++i) { r[i].state = 1; r[i].id = g_comp.enchant_specs[i].id; }
    return 1;
}
static int f_biomes(void *, uintptr_t step) { g_reg_order.push_back(step == 1 ? "biomes:construct" : step == 2 ? "biomes:colours" : "biomes:select"); return 1; }
static int f_audio(void *m, uintptr_t step) {
    a195_module *a = (a195_module *)m;
    g_reg_order.push_back(step ? "audio:hook" : "audio:events");
    if (!step) { for (uint32_t i = 0; i < a->count; ++i) { a->sounds[i].id = 523 + (int32_t)i; a->sounds[i].event = (uintptr_t)fake_alloc(0x38); } a->events_state = 1; }
    else a->hooked = 1;
    return 1;
}
static const char *text_of(uint32_t sid, const char *lang, char *buf, size_t cap) {
    buf[0] = 0;
    for (uint32_t i = 0; i < g_comp.sid_count; ++i) {
        if (g_comp.sids[i].sid != sid) continue;
        for (uint32_t k = 0; k < g_comp.sids[i].text_count; ++k) {
            const s195_text &t = g_comp.sids[i].texts[k];
            if (strcmp(t.lang, lang)) continue;
            size_t n = 0; for (; n < t.len && n + 1 < cap; ++n) buf[n] = (char)t.utf16[n];
            buf[n] = 0;
        }
    }
    return buf;
}
static int creative_lease(void *context) { return *(int *)context; }

static void boot_potion_only() {
    image(); override_all();
    g_xn.potion_by_id = fx::f_potion_by_id;
    lce195c_test_override(LCE195C_PH_STRINGS, f_strings);
    install("potiononly");
    boot();
    CHECK_EQ(lce_loader_commit(g_l), LCE_OK);
    CHECK(mod("potiononly") && mod("potiononly")->state == LCE_MOD_LIVE);
    CHECK_EQ(g_comp.items.item_count, 0u);
    CHECK_EQ(g_comp.has_blocks, 0u);
    CHECK_EQ(lce195c_creative_blocks_wanted(&g_comp), 0);
    CHECK_EQ(lce195c_creative_potions_wanted(&g_comp), 1);
}
static void scenario_potion_only_creative() {
    boot_potion_only();
    int held = 1;
    lce_sdk_creative_potion_entry out[2] = {};
    auto capture = [&](unsigned capacity, unsigned rows = 6) {
        return lce195c_creative_potion_entries(&g_comp, B, &held, creative_lease, out, capacity, rows);
    };
    const uintptr_t counter = fx::B + LCE195E_RVA_POTION_COUNT;
    CHECK_EQ(capture(2), 0);                                    // before items completion
    CHECK_EQ(lce195c_phase_after_items(&g_comp), 1);
    CHECK_EQ(capture(1), 0);                                    // entry array too short
    CHECK_EQ(capture(2, 5), 0);                                 // insufficient room for complete batch
    held = 0; CHECK_EQ(capture(2), 0); held = 1;
    g_no_level = 0; CHECK_EQ(capture(2), 0); g_no_level = 1;
    F<uintptr_t>(B + 0x143d010u) = 1; CHECK_EQ(capture(2), 0);      // tabs already visible
    F<uintptr_t>(B + 0x143d010u) = 0;
    CHECK_EQ(F<int32_t>(counter), 43);
    CHECK(!out[0].potion && !out[0].key);
    CHECK_EQ(capture(2), 2);                                    // both distinct live records
    CHECK(out[0].key && strcmp(out[0].key, "potiononly:first") == 0);
    CHECK(out[1].key && strcmp(out[1].key, "potiononly:second") == 0);
    CHECK_EQ(out[0].potion, fx::f_potion_by_id(43));
    CHECK_EQ(out[1].potion, fx::f_potion_by_id(44));
    CHECK_EQ(out[0].forms, 15u);
    CHECK_EQ(out[1].forms, (uint32_t)(LCE_SDK_CREATIVE_POTION_DRINK | LCE_SDK_CREATIVE_POTION_ARROW));
    CHECK_EQ(capture(2), 2);                                    // excludes a second publication
    CHECK_EQ(F<int32_t>(counter), 45);
    CHECK_EQ(lce195c_phase_late(&g_comp), 1);
    CHECK_EQ(F<int32_t>(counter), 45);
    CHECK_EQ(lce195c_phase_state(&g_comp, LCE195C_PH_POTIONS), (uint32_t)LCE195C_DONE);
    CHECK_EQ(capture(2), 0);                                    // title closes Creative lease
}
static void scenario_potion_only_fallback() {
    boot_potion_only();
    CHECK_EQ(lce195c_phase_after_items(&g_comp), 1);
    int held = 1;
    lce_sdk_creative_potion_entry out[2] = {};
    auto capture = [&] { return lce195c_creative_potion_entries(&g_comp, B, &held, creative_lease, out, 2, 6); };
    const uintptr_t counter = fx::B + LCE195E_RVA_POTION_COUNT;
    F<int32_t>(counter) = 42;                                    // native stock initialization incomplete
    CHECK_EQ(capture(), 0);
    CHECK_EQ(lce195c_phase_state(&g_comp, LCE195C_PH_POTIONS), (uint32_t)LCE195C_WAITING);
    F<int32_t>(counter) = 43;
    CHECK_EQ(capture(), 0);                                     // fallback belongs to title only
    CHECK_EQ(F<int32_t>(counter), 43);
    CHECK_EQ(lce195c_phase_late(&g_comp), 1);
    CHECK_EQ(F<int32_t>(counter), 45);
    CHECK_EQ(lce195c_phase_late(&g_comp), 1);
    CHECK_EQ(F<int32_t>(counter), 45);
}
static void scenario_potion_only_uncertain() {
    boot_potion_only();
    CHECK_EQ(lce195c_phase_after_items(&g_comp), 1);
    int held = 1;
    lce_sdk_creative_potion_entry out[2] = {};
    const uintptr_t counter = fx::B + LCE195E_RVA_POTION_COUNT;
    fx::drop_register = 1;                                      // fail the publisher readback after first mutation
    CHECK_EQ(lce195c_creative_potion_entries(&g_comp, B, &held, creative_lease, out, 2, 6), 0);
    CHECK_EQ(lce195c_phase_state(&g_comp, LCE195C_PH_POTIONS), (uint32_t)LCE195C_UNCERTAIN);
    CHECK(!out[0].potion && !out[0].key);
    fx::drop_register = 0;
    CHECK_EQ(lce195c_phase_late(&g_comp), -1);
    CHECK_EQ(F<int32_t>(counter), 44);
    CHECK(lce195e_potion_id("potiononly:first") < 0);
}

static int throw_potion_registration(void *, uintptr_t) {
    ++F<int32_t>(fx::B + LCE195E_RVA_POTION_COUNT);
    throw 1;
}
static void scenario_potion_only_unwind() {
    boot_potion_only();
    CHECK_EQ(lce195c_phase_after_items(&g_comp), 1);
    lce195c_test_override(LCE195C_PH_POTIONS, throw_potion_registration);
    int held = 1;
    lce_sdk_creative_potion_entry out[2] = {};
    CHECK_EQ(lce195c_creative_potion_entries(&g_comp, B, &held, creative_lease, out, 2, 6), 0);
    CHECK_EQ(lce195c_phase_state(&g_comp, LCE195C_PH_POTIONS), (uint32_t)LCE195C_UNCERTAIN);
    CHECK_EQ(g_comp.effect_window, 0u);
    CHECK(!g_comp.creative_potion_held && !g_comp.creative_potion_ctx);
    lce195c_test_override(LCE195C_PH_POTIONS, nullptr);
    CHECK_EQ(lce195c_phase_late(&g_comp), -1);  // completes the title pass without retrying publication
    CHECK_EQ(F<int32_t>(fx::B + LCE195E_RVA_POTION_COUNT), 44);
}
static void scenario_registries() {
    image(); override_all();
    g_xn.effect_by_id = fx::f_effect_by_id; g_xn.potion_by_id = fx::f_potion_by_id;   // on the console both are the retail byId
    lce195c_test_override(LCE195C_PH_STRINGS, f_strings);
    lce195c_test_override(LCE195C_PH_ENCHANTS, f_enchants);
    lce195c_test_override(LCE195C_PH_BIOMES, f_biomes);
    lce195c_test_override(LCE195C_PH_AUDIO, f_audio);
    install("newretail");
    boot();
    CHECK_EQ(lce_loader_commit(g_l), LCE_OK);
    const lce_mod_report *m = mod("newretail");
    CHECK(m && m->state == LCE_MOD_LIVE);
    if (!m || m->state != LCE_MOD_LIVE) { if (m) printf("  newretail: %s\n", m->reason); return; }
    const uint32_t caps = lce195c_capabilities(&g_comp);
    CHECK_EQ(caps & (LCE_CAP_EFFECTS | LCE_CAP_ENCHANTS | LCE_CAP_BIOMES | LCE_CAP_SOUNDS | LCE_CAP_BREWING), LCE_CAP_EFFECTS | LCE_CAP_ENCHANTS | LCE_CAP_BIOMES | LCE_CAP_SOUNDS | LCE_CAP_BREWING);
    // persistent ids from the ledger (one space per kind)
    const std::vector<uint8_t> &led = files["/data/lce-mods/reservations/sdk-ids-v1.txt"];
    std::string lt(led.begin(), led.end());
    CHECK(lt.find("effect newretail:glow 32\n") != std::string::npos);
    CHECK(lt.find("effect newretail:calm 34\n") != std::string::npos);
    CHECK(lt.find("enchant newretail:gem_edge 100\n") != std::string::npos);
    CHECK(lt.find("biome newretail:crystal_fields 168\n") != std::string::npos);
    CHECK(lt.find("potion ") == std::string::npos);   // potions are saved by key
    // string ids: 3 effects + 2 potions x 4 variants + 2 enchantments
    CHECK_EQ(g_comp.sid_count, 13u);
    CHECK_EQ(g_comp.effect_count, 3u); CHECK_EQ(g_comp.potion_count, 2u); CHECK_EQ(g_comp.enchant_count, 2u);
    CHECK_EQ(g_comp.biome_count, 1u); CHECK_EQ(g_comp.sound_pcm_count, 1u);
    char t[128];
    CHECK(!strcmp(text_of(g_comp.effect_specs[0].name_sid, "en_US", t, sizeof t), "Glow"));
    CHECK(!strcmp(text_of(g_comp.effect_specs[0].name_sid, "de_DE", t, sizeof t), "Leuchten"));
    CHECK(!strcmp(text_of(g_comp.potion_sid[0][0], "en_US", t, sizeof t), "Potion of Glow"));
    CHECK(!strcmp(text_of(g_comp.potion_sid[0][0], "de_DE", t, sizeof t), "Trank des Leuchtens"));
    CHECK(!strcmp(text_of(g_comp.potion_sid[0][1], "en_US", t, sizeof t), "Splash of Glow"));          // package override
    CHECK(!strcmp(text_of(g_comp.potion_sid[0][2], "en_US", t, sizeof t), "Lingering Potion of Glow"));
    CHECK(!strcmp(text_of(g_comp.potion_sid[1][3], "en_US", t, sizeof t), "Arrow of Strong Glow"));
    CHECK(!strcmp(text_of(g_comp.enchant_specs[0].name_sid, "de_DE", t, sizeof t), "Edelsteinschneide"));
    CHECK_EQ(g_comp.enchant_specs[0].clone, (uint32_t)E195_CLONE_SHARPNESS);
    CHECK_EQ(g_comp.enchant_specs[0].category, (uint32_t)E195_CAT_BREAKABLE);   // sword + axe
    CHECK_EQ(g_comp.enchant_specs[1].clone, (uint32_t)E195_CLONE_NONE);
    CHECK(g_comp.enchant_specs[1].treasure == 1);
    // item window: effects first (pre_collect, real effects195 publisher), then the food item takes the mod effect
    CHECK_EQ(lce195c_phase_blocks_hook(&g_comp), 1);
    CHECK_EQ(item_window(), 1);
    CHECK_EQ(lce195c_phase_state(&g_comp, LCE195C_PH_EFFECTS), (uint32_t)LCE195C_DONE);
    uintptr_t glow = fx::f_effect_by_id(32), swift = fx::f_effect_by_id(33), calm = fx::f_effect_by_id(34);
    CHECK(glow && swift && calm);
    CHECK_EQ(lce195c_effect_id("newretail:calm"), 34);
    // the game ticks the regeneration clone: the thunk forwards to the stock behaviour AND raises EFFECT_TICK (script)
    uintptr_t mob = (uintptr_t)fake_alloc(0x40), mob_sp[2] = {mob, 0};
    fx::seen_tick_this = 0;
    lce195e_thunk_tick(glow, (uintptr_t)mob_sp, 1);
    CHECK_EQ(fx::seen_tick_this, fx::stock_eff[10]);                     // `this` = stock regeneration
    CHECK(logged("effect tick newretail:glow amp 1 id 32"));
    uintptr_t berries = by_id((unsigned)lce_loader_item_id(g_l, "newretail:glow_berries"));
    CHECK(berries != 0);
    if (berries) CHECK_EQ(Q(F<uintptr_t>(berries + 0xa8)), glow);   // MobEffectInstance -> the mod effect
    CHECK_EQ(lce195c_phase_after_items(&g_comp), 1);
    int held = 1;
    lce_sdk_creative_potion_entry entries[2] = {};
    CHECK_EQ(lce195c_creative_potion_entries(&g_comp, B, &held, creative_lease, entries, 2, 8), 2);
    CHECK_EQ(entries[0].potion, fx::f_potion_by_id(43));
    CHECK(entries[0].key && strcmp(entries[0].key, "newretail:glow_potion") == 0);
    // late startup: strings, potions (real), enchantments, biome objects, furnace/brewing (real, mod potions), worldgen, audio
    F<uintptr_t>(B + LCE195_RVA_FURNACE_GLOBAL) = g_furnace;
    const uint32_t mixes_before = vec_len(B + LCE195X_RVA_POTION_MIXES);
    int rc = lce195c_phase_late(&g_comp);
    CHECK_EQ(rc, 1);
    if (rc != 1) { printf("  late: %s | %s\n", g_comp.items.last_error, g_comp.last_error); for (auto &l : g_log) if (l.find("LCE_SDK phase") != std::string::npos) printf("   %s\n", l.c_str()); fflush(stdout); }
    CHECK_EQ(lce195c_phase_state(&g_comp, LCE195C_PH_POTIONS), (uint32_t)LCE195C_DONE);
    uintptr_t pot = fx::f_potion_by_id(43), strong = fx::f_potion_by_id(44);
    CHECK(pot && strong && pot != fx::f_potion_by_id(1));
    CHECK_EQ(F<uint32_t>(pot + 4), g_comp.potion_sid[0][1]);          // splash name sid at +4 (0x55e620)
    CHECK_EQ(vec_len(B + LCE195X_RVA_POTION_MIXES), mixes_before + 2);   // awkward -> glow, glow -> strong glow
    if (vec_len(B + LCE195X_RVA_POTION_MIXES) != mixes_before + 2) return;
    uintptr_t last = ((uintptr_t *)Q(B + LCE195X_RVA_POTION_MIXES + 8))[mixes_before + 1];
    CHECK_EQ(Q(last), pot); CHECK_EQ(Q(last + 16), strong);
    std::string order; for (auto &x : g_reg_order) order += x + " ";
    printf("  registries order: %s\n", order.c_str());
    CHECK(order == "strings enchants biomes:construct biomes:colours biomes:select audio:events audio:hook ");
    CHECK(logged("phase=audio rc=1 1 custom sound(s) registered"));
    CHECK(logged("phase=effects rc=1 3 status effect(s) published, ids 32..34"));
    CHECK(logged("phase=biomes rc=1 built="));
    // API 1.4 networking: own channel accepted (forwarded to events195 after publish), foreign channel refused
    CHECK(logged("register_channel newretail:sync rc 0"));
    CHECK(logged("register_channel foreign rc -1") || logged("register_channel foreign rc -2"));
    CHECK(!logged("network channel newretail:sync refused"));
    CHECK(g_comp.iface.capabilities2 & LCE_CAP2_EV_PAYLOAD);
    // per-tick strings ensure is a no-op before/after (fake env never called: overridden only in the late phase)
    char line[400]; lce195c_status_line(&g_comp, line, sizeof line); printf("  %s\n", line);
}

// I: console run 1 (rubyretail): our collect() built the items but the shipping publisher refused to
// register them (tools_runtime reserved() gated on the Steel owner -> STEEL_TOOLS registry=4). The
// composite must report items refused instead of items=1.
static void scenario_publisher_refused() {
    image(); override_all();
    install("rubyretail");
    boot();
    CHECK_EQ(lce_loader_commit(g_l), LCE_OK);
    CHECK_EQ(g_providers.size(), 1u);
    if (g_providers.empty()) return;
    mc195_items_provider &p = g_providers[0];
    CHECK_EQ(p.prepare(p.opaque, B), 1);
    static lce195_items_window w;
    CHECK_EQ(p.collect(p.opaque, &w, &g_catalog), 1);   // built, but nothing lands in Item::items
    CHECK_EQ(lce195c_phase_after_items(&g_comp), 1);
    CHECK_EQ(lce195c_phase_state(&g_comp, LCE195C_PH_ITEMS), (uint32_t)LCE195C_REFUSED);
    CHECK(logged("LCE_SDK phase=items rc=0 3 of 3 SDK item(s) built but NOT in Item::items"));
}

// B: renderers fail after types registered -> world entry blocked.
static void scenario_renderers_fail() {
    image(); override_all(); g_renderers_rc = 0;
    install("retailall");
    boot();
    CHECK_EQ(lce_loader_commit(g_l), LCE_OK);
    CHECK_EQ(lce195c_phase_blocks_hook(&g_comp), 1);
    CHECK_EQ(item_window(), 1);
    CHECK_EQ(lce195c_phase_after_items(&g_comp), 1);
    CHECK_EQ(lce195x_recipe_providers_publish(g_owner), 1);
    F<uintptr_t>(B + LCE195_RVA_FURNACE_GLOBAL) = g_furnace;
    CHECK_EQ(lce195c_phase_late(&g_comp), -1);
    CHECK_EQ(lce195c_world_entry_allowed(&g_comp), 0);
    CHECK(logged("WORLD_ENTRY BLOCKED"));
    CHECK_EQ(lce195c_phase_state(&g_comp, LCE195C_PH_RENDERERS), (uint32_t)LCE195C_REFUSED);
}

// C: no renderer dispatcher -> entity types never registered, world entry stays allowed.
static void scenario_no_dispatcher() {
    image(); override_all(); g_dispatcher = 0;
    install("retailall");
    boot();
    CHECK_EQ(lce_loader_commit(g_l), LCE_OK);
    CHECK_EQ(lce195c_phase_blocks_hook(&g_comp), 1);
    CHECK_EQ(item_window(), 1);
    CHECK_EQ(lce195c_phase_after_items(&g_comp), 1);
    F<uintptr_t>(B + LCE195_RVA_FURNACE_GLOBAL) = g_furnace;
    lce195c_phase_late(&g_comp);
    for (auto &s : g_order) CHECK(s != "entity_types" && s != "renderers");
    CHECK_EQ(lce195c_phase_state(&g_comp, LCE195C_PH_ENTITY_TYPES), (uint32_t)LCE195C_REFUSED);
    CHECK_EQ(lce195c_world_entry_allowed(&g_comp), 1);
}

static void scenario_lua_entity() {
    image(); override_all();
    g_renderers_rc = 0;
    install("luamobs");
    boot();
    CHECK_EQ(lce_loader_commit(g_l), LCE_OK);
    const lce_mod_report *r = mod("luamobs");
    CHECK(r && r->state == LCE_MOD_LIVE);
    const lce_plan *plan = lce_loader_plan(g_l);
    CHECK(plan);
    if (!plan) return;
    const lce_entity_desc2 *d = nullptr;
    uint32_t num_entities = 0;
    for (uint32_t i = 0; i < plan->ext_count; ++i) {
        if (plan->ext[i].type != LCE_EXT_ENTITY) continue;
        ++num_entities;
        d = (const lce_entity_desc2 *)plan->ext[i].desc;
        CHECK(plan->ext[i].id >= 150);
    }
    CHECK_EQ(num_entities, 1u);
    CHECK(d);
    if (!d) return;
    CHECK(!strcmp(d->key, "luamobs:grove_zombie") && !strcmp(d->base_mob, "minecraft:zombie"));
    CHECK(d->max_health_milli == 30000 && d->move_speed_milli == 280 && d->attack_damage_milli == 4000);
    CHECK(d->spawn_weight == 10 && d->category == LCE_MOB_MONSTER && d->min_group == 1 && d->max_group == 2);
    CHECK(d->sound_ambient && !strcmp(d->sound_ambient, "minecraft:entity.zombie.ambient"));
    CHECK(d->egg_primary_rgb && !d->egg_secondary_rgb);
    CHECK(d->spawn_biome_count == 1 && d->spawn_biomes && !strcmp(d->spawn_biomes[0], "minecraft:plains"));
    CHECK(d->goal_count == 2 && d->goals && d->goals[0].speed_milli == 1200 &&
          d->goals[1].kind == LCE_AI_AVOID_ENTITY && d->goals[1].param && !strcmp(d->goals[1].param, "minecraft:player"));
    lce_ev_tick tick{};
    tick.struct_size = sizeof tick;
    lce195c_event_sink(&g_comp, LCE_EV_SERVER_TICK, &tick);
    CHECK(logged("late mob registration refused"));
    CHECK_EQ(lce195c_phase_late(&g_comp), -1);
    CHECK_EQ(lce195c_phase_state(&g_comp, LCE195C_PH_RENDERERS), (uint32_t)LCE195C_REFUSED);
    CHECK_EQ(lce195c_world_entry_allowed(&g_comp), 0);
}

// D: block relay never fired -> BlockItems not published, and smelting the block-item fails closed later.
static void scenario_blocks_missed() {
    image(); override_all(); g_blocks_rc = 0;
    install("retailall");
    boot();
    CHECK_EQ(lce_loader_commit(g_l), LCE_OK);
    CHECK_EQ(lce195c_phase_blocks_hook(&g_comp), 1);
    CHECK_EQ(item_window(), 1);
    CHECK_EQ(lce195c_phase_after_items(&g_comp), 0);
    CHECK_EQ(lce195c_phase_state(&g_comp, LCE195C_PH_BLOCKS), (uint32_t)LCE195C_REFUSED);
    for (auto &s : g_order) CHECK(s != "block_items");
    F<uintptr_t>(B + LCE195_RVA_FURNACE_GLOBAL) = g_furnace;
    CHECK_EQ(lce195c_phase_late(&g_comp), 0);                          // smelting input (block-item) unresolved: refused, nothing written
    CHECK_EQ(list_len(g_furnace), 0u);
    CHECK_EQ(lce195c_phase_state(&g_comp, LCE195C_PH_SMELT_BREW), (uint32_t)LCE195C_REFUSED);
}

// E: crafting callback before the item window -> outputs unregistered -> clean refusal, owner untouched.
static void scenario_crafting_early() {
    image(); override_all();
    install("rubyretail");
    boot();
    CHECK_EQ(lce_loader_commit(g_l), LCE_OK);
    CHECK_EQ(lce195x_recipe_providers_publish(g_owner), 0);
    CHECK_EQ(vec_len(g_owner + 0x50), 0u);
    CHECK_EQ(lce195_backend_status(&g_comp.items), LCE195_SDK_REFUSED);
}

// F: only refused content -> whole mod dropped, nothing armed, no provider registered.
static void scenario_allcontent_only() {
    image(); override_all();
    install("allcontent");
    boot();
    CHECK_EQ(lce_loader_commit(g_l), LCE_OK);
    const lce_mod_report *ac = mod("allcontent");
    CHECK(ac && ac->state != LCE_MOD_LIVE);
    CHECK_EQ(g_providers.size(), 0u);
    CHECK_EQ(lce195_backend_status(&g_comp.items), LCE195_SDK_IDLE);
    CHECK_EQ(lce195c_phase_blocks_hook(&g_comp), 0);                   // not armed: no phase runs
    CHECK_EQ(lce195c_phase_late(&g_comp), 0);
}

// G: without events/scripts bound the same mod is dropped (scripts + sounds need them), with the reason.
static void scenario_no_events() {
    image(); override_all();
    install("retailall");
    boot(false, false);
    CHECK_EQ(lce_loader_commit(g_l), LCE_OK);
    const lce_mod_report *ra = mod("retailall");
    CHECK(ra && ra->state != LCE_MOD_LIVE);
    printf("  retailall without events/scripts: %s\n", ra ? ra->reason : "?");
    CHECK(ra && strstr(ra->reason, "script"));
}

// Fuel is advertised only with a complete binding; registration validates bounds.
static void scenario_fuel_binding() {
    image(); override_all(); boot(true, true, true);
    CHECK((g_comp.iface.capabilities & LCE_CAP_FUEL) != 0);
    char err[192] = {};
    lce_fuel_desc d{}; d.struct_size = sizeof d; d.item = "minecraft:stick"; d.burn_ticks = 200;
    CHECK_EQ(g_comp.iface.check_ext(g_comp.iface.self, LCE_EXT_FUEL, &d, err, sizeof err), LCE_OK);
    d.burn_ticks = 72001;
    CHECK_EQ(g_comp.iface.check_ext(g_comp.iface.self, LCE_EXT_FUEL, &d, err, sizeof err), LCE_E_INVALID);
    lce_item_desc item{}; item.struct_size = sizeof item; item.key = "test:fuel"; item.name = "Fuel"; item.kind = LCE_ITEM_GENERIC; item.fuel_ticks = 200;
    CHECK_EQ(g_comp.iface.check_item(g_comp.iface.self, &item, err, sizeof err), LCE_OK);
    item.fuel_ticks = 72001;
    CHECK(g_comp.iface.check_item(g_comp.iface.self, &item, err, sizeof err) != LCE_OK);
    item.fuel_ticks = 200;
    lce_plan_item pi{}; pi.desc = &item; pi.id = 2283;
    d.item = item.key; d.burn_ticks = 200;
    lce_plan_ext ext{}; ext.type = LCE_EXT_FUEL; ext.desc = &d; ext.key = d.item;
    lce_plan plan{}; plan.items = &pi; plan.item_count = 1; plan.ext = &ext; plan.ext_count = 1;
    plan.resolve = [](const void *, uint32_t, const char *) -> int32_t { return 2283; };
    CHECK_EQ(g_comp.iface.prepare(g_comp.iface.self, &plan, err, sizeof err), LCE_E_COLLISION);
    CHECK(strstr(err, "duplicate fuel") != nullptr);
    CHECK_EQ(g_comp.state, 0);
}

static void prepare_fuel_image() {
    image(); override_all();
    uintptr_t vt = B + FUEL195_RVA_FBE_VTABLE;
    F<uint64_t>(vt - 32) = 0x50; F<uint64_t>(vt - 24) = 0x50;
    F<uint64_t>(vt - 16) = 0; F<uint64_t>(vt - 8) = B + FUEL195_RVA_FBE_RTTI;
    F<uint64_t>(vt + FUEL195_VSLOT_TICK) = B + FUEL195_RVA_SERVERTICK;
    F<uint64_t>(vt + FUEL195_VSLOT_CANPLACE) = B + FUEL195_RVA_CANPLACEITEM;
    uintptr_t block = B + FUEL195_RVA_BLOCK_ENTITY_VT;
    F<uint64_t>(block - 16) = (uint64_t)-(int64_t)0x1e8;
    F<uint64_t>(block - 8) = B + FUEL195_RVA_BLOCK_RTTI;
    F<uint64_t>(block + 0x10) = B + FUEL195_RVA_NEW_FURNACE;
    uintptr_t slot = B + FUEL195_RVA_FUEL_SLOT_VT;
    F<uint64_t>(slot - 16) = 0; F<uint64_t>(slot - 8) = B + FUEL195_RVA_FUEL_SLOT_RTTI;
    F<uint64_t>(slot + 0x30) = B + FUEL195_RVA_FUEL_MAY_PLACE;
    uintptr_t menu = B + FUEL195_RVA_FURNACE_MENU_VT;
    F<uint64_t>(menu - 16) = 0;
    F<uint64_t>(menu - 8) = B + FUEL195_RVA_FURNACE_MENU_RTTI;
    F<uint64_t>(menu + FUEL195_VSLOT_QUICK_MOVE) = B + FUEL195_RVA_QUICK_MOVE;
    uintptr_t container = B + FUEL195_RVA_CONTAINER_VT;
    F<uint64_t>(container - 16) = (uint64_t)-(int64_t)FUEL195_FBE_CONTAINER_OFFSET;
    F<uint64_t>(container - 8) = B + FUEL195_RVA_FBE_RTTI;
    F<uint64_t>(container + FUEL195_VSLOT_CONTAINER_CAN_PLACE) = B + FUEL195_RVA_CONTAINER_CAN_PLACE;
    F<uint64_t>(container + FUEL195_VSLOT_CONTAINER_SIDED) = B + FUEL195_RVA_CONTAINER_SIDED;
    uintptr_t head = (uintptr_t)fake_alloc(0x40), node = (uintptr_t)fake_alloc(0x40);
    F<uint64_t>(B + FUEL195_RVA_BE_REGISTRY + 8) = head;
    F<uint64_t>(B + FUEL195_RVA_BE_REGISTRY + 16) = 1;
    F<uint64_t>(head) = node; F<uint64_t>(node) = head;
    F<uint64_t>(node + 0x38) = B + FUEL195_RVA_FBE_FACTORY;
}

// Exercise a staged fuel mod through item publication and the late startup gate.
static void scenario_fuel_publish() {
    prepare_fuel_image();
    const uintptr_t menu = B + FUEL195_RVA_FURNACE_MENU_VT;
    const uintptr_t container = B + FUEL195_RVA_CONTAINER_VT;
    install("fueltest"); install("luafuel"); boot(true, true, true);
    CHECK_EQ(lce_loader_commit(g_l), LCE_OK);
    const lce_mod_report *m = mod("fueltest"); CHECK(m && m->state == LCE_MOD_LIVE);
    const lce_mod_report *lua = mod("luafuel"); CHECK(lua && lua->state == LCE_MOD_LIVE);
    CHECK_EQ(lce195c_world_entry_allowed(&g_comp), 0);
    CHECK_EQ(lce195c_phase_blocks_hook(&g_comp), 1);
    CHECK_EQ(item_window(), 1);
    CHECK_EQ(lce195c_phase_after_items(&g_comp), 1);
    CHECK_EQ(lce195c_phase_late(&g_comp), 1);
    CHECK_EQ(lce195c_phase_state(&g_comp, LCE195C_PH_FUEL), LCE195C_DONE);
    CHECK_EQ(fuel195_rt()->quick_move_slot, menu + FUEL195_VSLOT_QUICK_MOVE);
    CHECK_EQ(fuel195_rt()->hopper_slot, container + FUEL195_VSLOT_CONTAINER_CAN_PLACE);
    CHECK_EQ(F<uint64_t>(container + FUEL195_VSLOT_CONTAINER_CAN_PLACE), B + FUEL195_RVA_CONTAINER_SIDED);
    CHECK(F<uint64_t>(menu + FUEL195_VSLOT_QUICK_MOVE) != B + FUEL195_RVA_QUICK_MOVE);
    CHECK_EQ(fuel195_rt()->table.count, 4u);
    CHECK(fuel195_lookup(fuel195_rt(), by_id((unsigned)lce_loader_item_id(g_l, "fueltest:gem"))) != nullptr);
    CHECK(fuel195_lookup(fuel195_rt(), by_id((unsigned)lce_loader_item_id(g_l, "fueltest:ember"))) != nullptr);
    const auto *lua_fuel = fuel195_lookup(fuel195_rt(), by_id((unsigned)lce_loader_item_id(g_l, "luafuel:ember")));
    CHECK(lua_fuel && lua_fuel->ticks == 1234);
    const auto *lua_inline = fuel195_lookup(fuel195_rt(), by_id((unsigned)lce_loader_item_id(g_l, "luafuel:inline")));
    CHECK(lua_inline && lua_inline->ticks == 2345);
    CHECK_EQ(lce195c_world_entry_allowed(&g_comp), 1);
}

static void scenario_fuelroute_stock_only() {
    prepare_fuel_image();
    constexpr unsigned paper_id = 339;
    void *paper = fake_alloc(0xa0);
    item_ctor(paper);
    g_reg[paper_id] = (uintptr_t)paper;
    install("fuelroute");
    files["/data/lce-mods/fuelroute/enabled"] = {};
    boot(true, true, true);
    CHECK_EQ(lce_loader_commit(g_l), LCE_OK);
    CHECK_EQ(lce_loader_mod_count(g_l), 1u);
    const lce_mod_report *report = mod("fuelroute");
    CHECK(report && report->state == LCE_MOD_LIVE && report->items == 0u);
    const lce_plan *plan = lce_loader_plan(g_l);
    CHECK(plan && plan == g_comp.plan);
    if (!plan) return;
    CHECK_EQ(plan->item_count, 0u);
    CHECK_EQ(plan->recipe_count, 0u);
    CHECK_EQ(g_comp.items.smelt_count, 0u);
    CHECK_EQ(g_comp.items.brew_count, 0u);
    CHECK_EQ(g_comp.fuel_count, 1u);
    CHECK_EQ(g_providers.size(), 0u);
    CHECK_EQ(lce195c_world_entry_allowed(&g_comp), 0);
    CHECK_EQ(lce195c_phase_blocks_hook(&g_comp), 1);
    CHECK_EQ(lce195c_phase_after_items(&g_comp), 1);
    CHECK_EQ(lce195c_phase_late(&g_comp), 1);
    CHECK_EQ(lce195c_phase_state(&g_comp, LCE195C_PH_FUEL), LCE195C_DONE);
    CHECK(logged("LCE_SDK phase=fuel rc=1 installed 1 fuel(s)"));
    CHECK_EQ(fuel195_rt()->table.count, 1u);
    const fuel195_entry *fuel = fuel195_lookup(fuel195_rt(), (uintptr_t)paper);
    CHECK(fuel && fuel->ticks == 600u && fuel->remainder == 0u);
    CHECK_EQ(fuel195_rt()->quick_move_slot, B + FUEL195_RVA_FURNACE_MENU_VT + FUEL195_VSLOT_QUICK_MOVE);
    CHECK_EQ(fuel195_rt()->hopper_slot, B + FUEL195_RVA_CONTAINER_VT + FUEL195_VSLOT_CONTAINER_CAN_PLACE);
    CHECK_EQ(lce195c_world_entry_allowed(&g_comp), 1);
}

static void scenario_fuel_install_failure() {
    image(); override_all(); install("fueltest"); boot(true, true, true);
    CHECK_EQ(lce_loader_commit(g_l), LCE_OK);
    CHECK_EQ(lce195c_phase_blocks_hook(&g_comp), 1);
    CHECK_EQ(item_window(), 1);
    CHECK_EQ(lce195c_phase_after_items(&g_comp), 1);
    CHECK_EQ(lce195c_phase_late(&g_comp), 0);
    CHECK_EQ(lce195c_phase_state(&g_comp, LCE195C_PH_FUEL), LCE195C_REFUSED);
    CHECK_EQ(lce195c_world_entry_allowed(&g_comp), 0);
}

// ============================ verdicts =======================================
// Per-content verdicts of the composite exactly as the loader applies them:
// capability first (with the backend's reason), then the check_* hook.
static int verdicts(const char *lcm) {
    image();
    static MockGame game;
    mock_game_init(&game, 0xffffffffu);
    lce_backend mb = mock_backend(&game);
    mb.capabilities2 = 0xffffffffu;   // the mock accepts every descriptor; the composite's checks below decide
    lce_loader_config c;
    memset(&c, 0, sizeof c);
    lce195_default_id_space(&c.space);
    lce195_ext_id_space(&c.space);
    c.space.item_in_use = mock_item_in_use; c.space.block_in_use = mock_block_in_use; c.space.user = &game;
    c.backend = &mb;
    c.ledger_path = "/mem/verdict-ledger";
    c.host.read_file = fs_read; c.host.write_file_atomic = fs_write;
    lce_loader *ml = lce_loader_create(&c);
    std::vector<uint8_t> pkg = load(lcm);
    if (lce_loader_add_package(ml, pkg.data(), pkg.size(), "verdicts") != LCE_OK || lce_loader_commit(ml) != LCE_OK || !lce_loader_plan(ml)) {
        fprintf(stderr, "mock load failed: %s\n", lce_loader_mod_count(ml) ? lce_loader_mod(ml, 0)->reason : "-");
        lce_loader_destroy(ml);
        return 2;
    }
    const lce_plan *p = lce_loader_plan(ml);
    boot(true, true);   // composite with the console binding set (events + scripts bound)
    const lce_backend &be = g_comp.iface;
    const uint32_t caps = be.capabilities;
    char err[256];
    auto out = [&](const char *kind, const char *key, lce_status st) { printf("VERDICT %s %s %s %s\n", kind, key, st == LCE_OK ? "OK" : "REFUSED", st == LCE_OK ? "" : err); };
    for (uint32_t i = 0; i < p->item_count; ++i) {
        const lce_item_desc *d = p->items[i].desc; err[0] = 0;
        uint32_t kc = d->kind == LCE_ITEM_FOOD ? LCE_CAP_FOOD : d->kind >= LCE_ITEM_HELMET ? LCE_CAP_ARMOUR : d->kind >= LCE_ITEM_SWORD ? LCE_CAP_TOOLS : 0;
        lce_status st = be.check_item(be.self, d, err, sizeof err);
        if (st == LCE_OK && kc && !(caps & kc)) { st = LCE_E_UNSUPPORTED; snprintf(err, sizeof err, "kind not supported"); }
        out("item", d->key, st);
    }
    for (uint32_t i = 0; i < p->block_count; ++i) { err[0] = 0; out("block", p->blocks[i].desc->key, be.check_block(be.self, p->blocks[i].desc, err, sizeof err)); }
    for (uint32_t i = 0; i < p->recipe_count; ++i) { err[0] = 0; out("recipe", p->recipes[i].desc->key, be.check_recipe(be.self, p->recipes[i].desc, err, sizeof err)); }
    for (uint32_t i = 0; i < p->smelt_count; ++i) { err[0] = 0; out("smelting", p->smelts[i].desc->key, (caps & LCE_CAP_SMELTING) ? LCE_OK : LCE_E_UNSUPPORTED); }
    static const char *names[] = {"", "effect", "potion", "brewing", "enchantment", "fuel", "entity", "feature", "biome", "structure", "sound", "particle", "command", "keybind"};
    for (uint32_t i = 0; i < p->ext_count; ++i) {
        const lce_plan_ext &x = p->ext[i]; err[0] = 0;
        lce_status st = be.check_ext(be.self, x.type, x.desc, err, sizeof err);
        if (st == LCE_OK && !(caps & lce_ext_schema_of(x.type)->capability)) { st = LCE_E_UNSUPPORTED; snprintf(err, sizeof err, "capability"); }
        out(x.type < 14 ? names[x.type] : "?", x.key, st);
    }
    lce_loader_destroy(g_l);
    lce_loader_destroy(ml);
    return 0;
}

#ifdef LCE_HAVE_BOATS_1_10
// J: API 1.10 boat items: BoatItem copies built in the item window with +0x9c = the wood type, ITEM_USE routed over the
// stock BoatItem::use, stack 1 unless max_stack is set; the composite advertises LCE_CAP2_BOATS.
static void scenario_boats() {
    image(); override_all();
    install("boats");
    boot();
    CHECK_EQ(lce_loader_commit(g_l), LCE_OK);
    const lce_mod_report *m = mod("boats");
    CHECK(m && m->state == LCE_MOD_LIVE);
    if (!m || m->state != LCE_MOD_LIVE) { if (m) printf("  boats: %s\n", m->reason); return; }
    CHECK((g_comp.iface.capabilities2 & LCE_CAP2_BOATS) != 0);
    {   // composite/items195 checks: generic, not throwable, type 0..5
        lce_item_desc d; memset(&d, 0, sizeof d); d.struct_size = sizeof d; d.key = "t:x"; d.name = "X"; d.kind = LCE_ITEM_GENERIC;
        d.flags = LCE_ITEM_FLAG_BOAT; d.boat_type = 5; char e[200];
        CHECK_EQ(g_comp.iface.check_item(g_comp.iface.self, &d, e, sizeof e), LCE_OK);
        d.boat_type = 6; CHECK(g_comp.iface.check_item(g_comp.iface.self, &d, e, sizeof e) != LCE_OK);
        d.boat_type = 1; d.flags |= LCE_ITEM_FLAG_THROWABLE; CHECK(g_comp.iface.check_item(g_comp.iface.self, &d, e, sizeof e) != LCE_OK);
    }
    CHECK_EQ(lce195c_phase_blocks_hook(&g_comp), 1);
    CHECK_EQ(item_window(), 1);
    CHECK_EQ(g_catalog.count, 3u);
    CHECK_EQ(g_providers[0].identity(g_providers[0].opaque), 1);
    const char *keys[3] = {"boats:redwood_boat", "boats:paper_boat", "boats:jungle_raft"};
    const uint32_t types[3] = {1, 2, 3}, stacks[3] = {1, 4, 1};
    for (int i = 0; i < 3; ++i) {
        uintptr_t obj = by_id((unsigned)lce_loader_item_id(g_l, keys[i]));
        CHECK(obj != 0);
        if (!obj) continue;
        const uintptr_t *vt = (const uintptr_t *)Q(obj);
        CHECK_EQ(vt[-1], 0x7000u + LCE195X_RVA_BOATITEM_VT);                              // a BoatItem copy
        CHECK_EQ(vt[LCE195X_SLOT_USE / 8], (uintptr_t)&lce195_ev_item_use_thunk);         // ITEM_USE routed over BoatItem::use
        CHECK_EQ(vt[LCE195X_SLOT_NAME / 8], (uintptr_t)&lce195_sdk_name_dispatch);
        CHECK_EQ(F<uint32_t>(obj + LCE195X_OFF_BOAT_TYPE), types[i]);
        CHECK_EQ(F<uint32_t>(obj + 0x18), stacks[i]);
    }
    CHECK_EQ(lce195c_phase_after_items(&g_comp), 1);
}
#endif

// I: API 1.9 throwables end to end: SnowballItem copies built in the item window (ITEM_USE still routed), the shoot slot
// armed after the items, a throw swaps the new Snowball to the kind vtable, a hit applies the declarative fields through
// the services inside an events195 level frame and reaches the script as ProjectileHit.
struct SvcCall { std::string what; void *entity; uint32_t value; bool framed; };
static std::vector<SvcCall> g_svc;
static bool framed_now() { const lce195_frame *f = lce195_ev_frame(); return f && f->active && !f->has_player; }
static void scenario_throwables() {
    image(); override_all();
    install("throwables");
    boot();
    lce_api svc; memset(&svc, 0, sizeof svc);
    svc.struct_size = sizeof svc;
    svc.damage_entity = +[](lce_mod_ctx *, void *e, uint32_t milli, void *att) -> lce_status { g_svc.push_back({"damage", e, milli, framed_now()}); CHECK(att == nullptr); return LCE_OK; };
    svc.set_fire = +[](lce_mod_ctx *, void *e, uint32_t t) -> lce_status { g_svc.push_back({"fire", e, t, framed_now()}); return LCE_OK; };
    svc.add_effect = +[](lce_mod_ctx *, void *e, const lce_effect_ref *r) -> lce_status {
        g_svc.push_back({std::string("effect ") + r->effect, e, r->duration_ticks * 10 + r->amplifier, framed_now()}); return LCE_OK; };
    svc.explode = +[](lce_mod_ctx *, void *lv, double, double y, double, float r, int fire, int destroy) -> lce_status {
        g_svc.push_back({"explode", lv, (uint32_t)(r * 1000) + (fire ? 100000u : 0u) + (destroy ? 1000000u : 0u), framed_now() && y == 65.25}); return LCE_OK; };
    lce_loader_set_services(g_l, &svc);
    CHECK_EQ(lce_loader_commit(g_l), LCE_OK);
    const lce_mod_report *m = mod("throwables");
    CHECK(m && m->state == LCE_MOD_LIVE);
    if (!m || m->state != LCE_MOD_LIVE) { if (m) printf("  throwables: %s\n", m->reason); return; }
    CHECK((g_comp.iface.capabilities2 & LCE_CAP2_PROJECTILES) != 0);
    CHECK_EQ(g_comp.proj_count, 3u);
    {   // composite checks: projectiles195 ranges, generic kind only
        lce_item_desc d; memset(&d, 0, sizeof d); d.struct_size = sizeof d; d.key = "t:x"; d.name = "X"; d.kind = LCE_ITEM_GENERIC;
        d.flags = LCE_ITEM_FLAG_THROWABLE; char e[200];
        CHECK_EQ(g_comp.iface.check_item(g_comp.iface.self, &d, e, sizeof e), LCE_OK);
        d.projectile_speed_milli = 6000; CHECK_EQ(g_comp.iface.check_item(g_comp.iface.self, &d, e, sizeof e), LCE_E_UNSUPPORTED);
        d.projectile_speed_milli = 50; CHECK_EQ(g_comp.iface.check_item(g_comp.iface.self, &d, e, sizeof e), LCE_E_UNSUPPORTED);
        d.projectile_speed_milli = 0; d.projectile_inaccuracy_milli = 30000; CHECK_EQ(g_comp.iface.check_item(g_comp.iface.self, &d, e, sizeof e), LCE_E_UNSUPPORTED);
        d.projectile_inaccuracy_milli = 0; d.kind = LCE_ITEM_SWORD; d.tier = LCE_TIER_IRON;
        CHECK_EQ(g_comp.iface.check_item(g_comp.iface.self, &d, e, sizeof e), LCE_E_UNSUPPORTED);
        CHECK(strstr(e, "generic") != nullptr);
    }
    CHECK_EQ(lce195c_phase_blocks_hook(&g_comp), 1);
    CHECK_EQ(item_window(), 1);
    CHECK_EQ(g_catalog.count, 3u);
    CHECK_EQ(g_providers[0].identity(g_providers[0].opaque), 1);
    int32_t shard = lce_loader_item_id(g_l, "throwables:ruby_shard"), bomb = lce_loader_item_id(g_l, "throwables:fire_bomb");
    uintptr_t shard_obj = by_id((unsigned)shard), bomb_obj = by_id((unsigned)bomb);
    CHECK(shard_obj && bomb_obj);
    if (!shard_obj || !bomb_obj) return;
    const uintptr_t *svt = (const uintptr_t *)Q(shard_obj);
    CHECK_EQ(svt[-1], 0x7000u + LCE195X_RVA_SNOWBALLITEM_VT);                       // a SnowballItem copy
    CHECK_EQ(svt[LCE195X_SLOT_USE / 8], (uintptr_t)&lce195_ev_item_use_thunk);         // ITEM_USE routed over p195_item_use
    CHECK_EQ(svt[LCE195X_SLOT_NAME / 8], (uintptr_t)&lce195_sdk_name_dispatch);
    CHECK_EQ(Q(B + P195_RVA_SNOWBALL_VT + P195_SLOT_SHOOT), B + P195_RVA_SHOOT);        // nothing armed before the items exist
    CHECK_EQ(lce195c_phase_after_items(&g_comp), 1);
    CHECK_EQ(lce195c_phase_state(&g_comp, LCE195C_PH_PROJECTILES), (uint32_t)LCE195C_DONE);
    CHECK_EQ(Q(B + P195_RVA_SNOWBALL_VT + P195_SLOT_SHOOT), (uintptr_t)&p195_shoot_hook);
    CHECK(logged("phase=projectiles rc=1"));
    // a world: the host player's view anchors the server level (events195 frames)
    uintptr_t level = (uintptr_t)fake_alloc(0x300);
    F<uintptr_t>(level) = B + 0x1340d50u; F<uint8_t>(level + P195_F_LEVEL_CLIENT) = 0;   // ServerLevel vtable (events195 natives195.h)
    g_player = (uintptr_t)fake_alloc(0xc00);
    F<uintptr_t>(g_player) = B + 0x1341168;
    wstr(g_player + 0x2d8, "uuid-1"); wstr(g_player + 0x9a8, "Tester");
    lce195_view v; memset(&v, 0, sizeof v);
    v.image = B; v.player_object = g_player; v.session = 1; v.thread = g_tid; v.level = level;
    lce195_ev_scope_tick(nullptr, &v);   // the level ticks (entities, projectiles) run inside the server tick scope
    // throw the shard through its vtable: events195 thunk -> p195_item_use -> stock use -> patched shoot
    lce195_pair pp{g_player, g_player + 0x800};
    uint8_t holder[0x20];
    typedef void *(*use_fn)(void *, void *, void *, const lce195_pair *, int);
    CHECK(((use_fn)svt[LCE195X_SLOT_USE / 8])(holder, (void *)shard_obj, (void *)level, &pp, 0) == holder);
    const p195_module *pm = g_comp_projectiles_for_tests();
    CHECK_EQ(g_p_shoots, 1); CHECK(g_p_speed == 2.0f && g_p_inacc == 0.5f);
    CHECK_EQ(Q(g_last_ball), (uintptr_t)&pm->proj_vt[0][2]);
    // the shard hits a mob: damage 4 + slowness II for 100 ticks, framed, then the stock onHit, then the script
    uintptr_t mob = (uintptr_t)fake_alloc(0x700);
    uint8_t hit[0x40] = {0};
    F<uintptr_t>((uintptr_t)hit + P195_F_HIT_ENTITY) = mob;
    CHECK_EQ(p195_on_hit((void *)g_last_ball, hit), 1);
    CHECK_EQ(g_p_onhit, 1);
    CHECK_EQ(g_svc.size(), 2u);
    if (g_svc.size() == 2) {
        CHECK(g_svc[0].what == "damage" && g_svc[0].entity == (void *)mob && g_svc[0].value == 4000 && g_svc[0].framed);
        CHECK(g_svc[1].what == "effect minecraft:slowness" && g_svc[1].value == 1001 && g_svc[1].framed);
    }
    CHECK(logged(("projectile " + std::to_string(shard) + " hit an entity at 10,65,-5").c_str()));
    // the fire bomb lands on a block: no entity services, an explosion of 1.5 with fire at the impact
    g_svc.clear();
    const uintptr_t *bvt = (const uintptr_t *)Q(bomb_obj);
    CHECK(((use_fn)bvt[LCE195X_SLOT_USE / 8])(holder, (void *)bomb_obj, (void *)level, &pp, 0) == holder);
    CHECK_EQ(g_p_shoots, 2); CHECK(g_p_speed == 1.2f && g_p_inacc == 1.0f);
    uint8_t block_hit[0x40] = {0};
    CHECK_EQ(p195_on_hit((void *)g_last_ball, block_hit), 1);
    CHECK_EQ(g_svc.size(), 1u);
    if (g_svc.size() == 1) CHECK(g_svc[0].what == "explode" && g_svc[0].entity == (void *)level && g_svc[0].value == 1101500u && g_svc[0].framed);
    CHECK(logged("hit a block at 10,65,-5"));
    // a player's own snowball (no pending kind) and a client-side hit stay stock
    uintptr_t plain = (uintptr_t)fake_alloc(0x4c0); F<uintptr_t>(plain) = B + P195_RVA_SNOWBALL_VT;
    p195_shoot_hook((void *)plain, nullptr, 0, 0, 0, 1.5f, 1.0f);
    CHECK(Q(plain) == B + P195_RVA_SNOWBALL_VT && g_p_speed == 1.5f);
    g_svc.clear();
    CHECK_EQ(p195_on_hit((void *)plain, hit), 1);
    CHECK(g_svc.empty());
    CHECK_EQ(g_comp.proj_hits, 2u);
    lce195_ev_scope_finish(nullptr, g_tid, 1);
    g_svc.clear();   // outside the tick nothing is framed: no services, no event (the stock onHit still runs)
    const int stock_before = g_p_onhit;
    CHECK(((use_fn)svt[LCE195X_SLOT_USE / 8])(holder, (void *)shard_obj, (void *)level, &pp, 0) == holder);
    CHECK_EQ(p195_on_hit((void *)g_last_ball, hit), 1);
    CHECK(g_svc.empty() && g_p_onhit == stock_before + 1 && g_comp.proj_hits == 2u);
    char line[400]; lce195c_status_line(&g_comp, line, sizeof line); printf("  %s\n", line);
    CHECK(strstr(line, "projectiles=1") != nullptr);
}

static void scenario_lua_worldgen() {
    image();
    override_all();
    install("luaworldgen");
    boot();
    CHECK_EQ(lce_loader_commit(g_l), LCE_OK);
    const lce_mod_report *m = mod("luaworldgen");
    CHECK(m && m->state == LCE_MOD_LIVE);
    if (!m || m->state != LCE_MOD_LIVE) {
        if (m) printf("  luaworldgen: %s\n", m->reason);
        return;
    }
    CHECK_EQ(g_comp.biome_count, 1u);
    CHECK_EQ(g_comp.feature_count, 1u);
    CHECK_EQ(g_comp.structure_count, 1u);
    const lce_plan *plan = lce_loader_plan(g_l);
    int32_t biome_id = -1;
    const lce_feature_desc *feature = nullptr;
    for (uint32_t i = 0; i < plan->ext_count; ++i) {
        const lce_plan_ext &ext = plan->ext[i];
        if (ext.type == LCE_EXT_BIOME && !strcmp(ext.key, "luaworldgen:amber_grove")) biome_id = ext.id;
        if (ext.type == LCE_EXT_FEATURE) feature = static_cast<const lce_feature_desc *>(ext.desc);
    }
    CHECK(biome_id >= 0 && biome_id < 256);
    if (biome_id < 0 || biome_id >= 256 || !feature || !g_comp.feature_count) return;
    const lce_wg195_feature &gen = g_comp.features[0].f;
    CHECK(gen.biome_mask[biome_id >> 6] & (UINT64_C(1) << (biome_id & 63)));
    const int32_t plains = lce_wg195_biome_lookup("minecraft:plains");
    CHECK(plains >= 0 && plains < 256);
    if (plains >= 0 && plains < 256) CHECK(gen.biome_mask[plains >> 6] & (UINT64_C(1) << (plains & 63)));
    const uint16_t cells[] = {0, 2, 1, 0, 1, 2};
    CHECK(gen.structure && !memcmp(gen.structure->cells, cells, sizeof cells));

    lce_feature_desc other = *feature;
    char error[200];
    other.dimension = 1;
    CHECK_EQ(g_comp.iface.check_ext(g_comp.iface.self, LCE_EXT_FEATURE, &other, error, sizeof error), LCE_E_UNSUPPORTED);
    other.dimension = 2;
    CHECK_EQ(g_comp.iface.check_ext(g_comp.iface.self, LCE_EXT_FEATURE, &other, error, sizeof error), LCE_E_UNSUPPORTED);
    other.biomes = nullptr;
    other.biome_count = 0;
    CHECK_EQ(g_comp.iface.check_ext(g_comp.iface.self, LCE_EXT_FEATURE, &other, error, sizeof error), LCE_OK);
    other.dimension = 3;
    CHECK_EQ(g_comp.iface.check_ext(g_comp.iface.self, LCE_EXT_FEATURE, &other, error, sizeof error), LCE_E_INVALID);
}

// ============================ driver =======================================
static int run(const char *name, void (*fn)()) {
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) { g_checks = g_failures = 0; fn(); printf("  [%s] %d checks, %d failures\n", name, g_checks, g_failures); fflush(stdout); _exit(g_failures ? 1 : 0); }
    int status = 0;
    waitpid(pid, &status, 0);
    ++g_checks;
    if (!WIFEXITED(status) || WEXITSTATUS(status)) { ++g_failures; fprintf(stderr, "scenario %s FAILED (status %d)\n", name, status); }
    return 0;
}

int main(int argc, char **argv) {
    if (argc > 2 && !strcmp(argv[1], "--verdicts")) return verdicts(argv[2]);
    g_pkg = argc > 1 ? argv[1] : "build/pkg";
    run("Lua texture registration to item and terrain owners", scenario_lua_texture);
    run("fuel capability binding", scenario_fuel_binding);
    run("fuel startup publication", scenario_fuel_publish);
    run("fuelroute stock-only publication", scenario_fuelroute_stock_only);
    run("fuel installation failure blocks entry", scenario_fuel_install_failure);
    run("full startup order", scenario_full);
    run("renderers missing -> world entry blocked", scenario_renderers_fail);
    run("no renderer dispatcher -> no entity types", scenario_no_dispatcher);
    run("Lua custom mob registration preserves renderer guard", scenario_lua_entity);
    run("block relay missed -> fail closed", scenario_blocks_missed);
    run("crafting before items -> refused", scenario_crafting_early);
    run("only refused content", scenario_allcontent_only);
    run("events/scripts unbound", scenario_no_events);
    run("potion-only Creative capture", scenario_potion_only_creative);
    run("potion-only clean title fallback", scenario_potion_only_fallback);
    run("potion-only uncertain publication remains one-shot", scenario_potion_only_uncertain);
    run("potion-only native unwind closes startup lease", scenario_potion_only_unwind);
    run("effects, potions, enchantments, biomes, custom audio", scenario_registries);
    run("Lua biome and structure world generation", scenario_lua_worldgen);
    run("shipping publisher refused the built items -> items refused", scenario_publisher_refused);
    run("API 1.9 throwables", scenario_throwables);
#ifdef LCE_HAVE_BOATS_1_10
    run("API 1.10 boat items", scenario_boats);
#endif
    TEST_MAIN_END("test_composite195");
}
