// Retail 1.95 backend against a fake game image: real host memory laid out at
// base+RVA for the vtable, fake native functions with the same observable
// effects as the retail ones (object fields at the offsets the backend checks),
// and stand-ins for the r72 seams with the same acceptance rules.
#include <map>
#include <string>
#include <vector>
#include <stdlib.h>
#include <string.h>
#include "../loader/loader.h"
#include "../loader/retail195/backend195.h"
#include "../modules/items195/items195.h"
#include "../loader/retail195/id_space_195.h"
#include "check.h"
#include "mock_backend.h"

// ---- fake image ------------------------------------------------------------
static uintptr_t g_base;
static uintptr_t *g_vt_region;           // [offset-to-top, rtti, 56 slots] at base+0x126e638-16
static std::map<unsigned, uintptr_t> g_registry;  // numeric id -> Item*
static int g_ctor_bad_vptr, g_name_original_calls, g_window_held = 1, g_crafting_held = 1;

// "game heap": a static bump arena, like the process-lifetime native allocations
alignas(16) static uint8_t g_heap[1 << 16];
static size_t g_heap_used;
static void *fake_alloc(size_t n) {
    n = (n + 15) & ~(size_t)15;
    if (g_heap_used + n > sizeof g_heap) return nullptr;
    void *p = g_heap + g_heap_used; g_heap_used += n; memset(p, 0, n); return p;
}
static void fake_item_ctor(void *obj) {
    uintptr_t o = (uintptr_t)obj;
    *(uintptr_t *)o = g_ctor_bad_vptr ? 0x1234 : g_base + LCE195_RVA_ITEM_VTABLE;
    *(uintptr_t *)(o + 0x10) = 0xabc;   // stock ctor writes a non-null field here
    *(uintptr_t *)(o + 0x20) = 0;       // no icon yet
    *(uint64_t *)(o + 0x88) = 0;        // empty wstring at +0x70
    *(uint64_t *)(o + 0x90) = 7;
    *(uint32_t *)(o + 0x18) = 64;
}
// wstring layout used by the game: +8 SSO buffer / heap pointer, +0x18 length, +0x20 capacity
static void fake_wstring(void *dst, const uint16_t *u, uint64_t n) {
    uintptr_t d = (uintptr_t)dst;
    uint16_t *heap = (uint16_t *)fake_alloc((n + 1) * 2);
    memcpy(heap, u, n * 2);
    *(uint16_t **)(d + 8) = heap;
    *(uint64_t *)(d + 0x18) = n;
    *(uint64_t *)(d + 0x20) = n > 7 ? n : 7;
}
static uintptr_t fake_item_by_id(unsigned id) { auto it = g_registry.find(id); return it == g_registry.end() ? 0 : it->second; }
static void *fake_name_original(void *out, void *, void *) { ++g_name_original_calls; return out; }

static int mem_read(void *, uintptr_t a, void *v, size_t n) { memcpy(v, (const void *)a, n); return 1; }
static int mem_write(void *, uintptr_t a, const void *v, size_t n) { memcpy((void *)a, v, n); return 1; }

// ---- r72 seam stand-ins ----------------------------------------------------
extern "C" int lce195_items_window_held(void *) { return g_window_held; }
static std::vector<mc195_items_provider> g_providers;
static int seam_add_provider(const mc195_items_provider *p) { g_providers.push_back(*p); return 1; }
static int seam_catalog_add(lce195_item_catalog *c, const lce195_additive_item *e) {
    // same acceptance rules as additive_items_195/catalog.c
    if (c->state || c->count >= LCE195_ADDITIVE_CAPACITY || e->abi != LCE195_ADDITIVE_ABI || e->size != sizeof *e || !e->item ||
        e->id < 2268 || e->id >= 2398 || !e->key || !strchr(e->key, ':') || !e->icon) return 0;
    for (unsigned i = 0; i < c->count; ++i) if (c->entries[i].id == e->id || !strcmp(c->entries[i].key, e->key) || c->entries[i].item == e->item) return 0;
    c->entries[c->count++] = *e;
    return 1;
}
// ---- items195 recipe natives (fake owner model, as tests in modules/items195) ----
static uintptr_t g_empty_stack;
static unsigned id_of(uintptr_t item) { for (auto &kv : g_registry) if (kv.second == item) return kv.first; return 0; }
static void stack_ctor(void *s, void *item, int count, int aux) { uintptr_t x = (uintptr_t)s; *(uintptr_t *)(x + 0x28) = (uintptr_t)item; *(int *)(x + 0x10) = count; *(int *)(x + 0x3c) = aux; *(uint8_t *)(x + 0x38) = 0; }
static void shaped_ctor(void *r, int w, int h, void **cells, void *res, int g) {
    uintptr_t x = (uintptr_t)r; *(uintptr_t *)x = g_base + LCE195X_RVA_SHAPED_VT; *(int *)(x + 0x1c) = w; *(int *)(x + 0x20) = h; *(int *)(x + 0x24) = g;
    *(uintptr_t *)(x + 0x28) = (uintptr_t)cells; for (int i = 0; i < w * h; i++) if (!cells[i]) cells[i] = (void *)g_empty_stack;
    *(uintptr_t *)(x + 0x30) = (uintptr_t)res;
}
static void shapeless_ctor(void *r, void *res, void *vec, int g) { uintptr_t x = (uintptr_t)r; *(uintptr_t *)x = g_base + LCE195X_RVA_SHAPELESS_VT; *(int *)(x + 0x1c) = g; *(uintptr_t *)(x + 0x20) = (uintptr_t)res; *(uintptr_t *)(x + 0x28) = (uintptr_t)vec; }
static unsigned output_key(void *s) { uintptr_t x = (uintptr_t)s; return (id_of(*(uintptr_t *)(x + 0x28)) << 16) | *(unsigned *)(x + 0x3c); }
static void vec_grow(uintptr_t v, int) {
    uintptr_t b = *(uintptr_t *)(v + 8), e = *(uintptr_t *)(v + 16), c = *(uintptr_t *)(v + 24); size_t n = (e - b) / 8, cap = (c - b) / 8, nc = cap ? cap * 2 : 4;
    uintptr_t nb = (uintptr_t)fake_alloc(nc * 8); if (n) memcpy((void *)nb, (void *)b, n * 8);
    *(uintptr_t *)(v + 8) = nb; *(uintptr_t *)(v + 16) = nb + n * 8; *(uintptr_t *)(v + 24) = nb + nc * 8;
}
static uintptr_t recipe_bucket(uintptr_t owner, const unsigned *key) {
    uintptr_t h = *(uintptr_t *)(owner + 8);
    for (uintptr_t n = *(uintptr_t *)h; n != h; n = *(uintptr_t *)n) if (*(unsigned *)(n + 0x10) == *key) return n + 0x18;
    uintptr_t n = (uintptr_t)fake_alloc(0x38), first = *(uintptr_t *)h;
    *(uintptr_t *)n = first; *(uintptr_t *)(n + 8) = h; *(uintptr_t *)(first + 8) = n; *(uintptr_t *)h = n; *(uint64_t *)(owner + 0x10) += 1;
    *(unsigned *)(n + 0x10) = *key; return n + 0x18;
}
static uintptr_t make_owner() {
    uintptr_t o = (uintptr_t)fake_alloc(0x120), h = (uintptr_t)fake_alloc(0x20);
    *(uintptr_t *)h = h; *(uintptr_t *)(h + 8) = h; *(uintptr_t *)(o + 8) = h; *(uint64_t *)(o + 0x10) = 0;
    return o;
}
static unsigned recipe_count(uintptr_t owner) { return (unsigned)((*(uintptr_t *)(owner + 0x50 + 16) - *(uintptr_t *)(owner + 0x50 + 8)) / 8); }
static uintptr_t recipe_at(uintptr_t owner, unsigned i) { return ((uintptr_t *)*(uintptr_t *)(owner + 0x50 + 8))[i]; }
static int crafting_status_held(void *) { return lce195x_recipe_provider_status(LCE195_SDK_PROVIDER) == 1u && g_crafting_held; }

// ---- package fs (in-memory, like test_loader) -------------------------------
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
static std::vector<std::string> g_log;
static void fs_log(void *, const char *l) { g_log.push_back(l); if (getenv("LCE_TEST_VERBOSE")) printf("  | %s\n", l); }

static MockGame occupancy;  // live-registry occupancy for the ID allocator

static lce195x_natives g_xn;
static lce_loader *boot(lce195_backend *rb, const lce195_natives &nat, const lce195_seams &seams, const lce195_memory &mem) {
    lce195_backend_init(rb, g_base, &nat, &seams, &mem);
    lce195_backend_bind_items(rb, &g_xn);
    lce_loader_config c;
    memset(&c, 0, sizeof c);
    c.root = "/data/lce-mods"; c.ledger_path = "/data/lce-mods/reservations/sdk-ids-v1.txt";
    lce195_default_id_space(&c.space);
    c.space.item_in_use = mock_item_in_use; c.space.block_in_use = mock_block_in_use; c.space.user = &occupancy;
    c.backend = &rb->iface;
    c.host.read_file = fs_read; c.host.write_file_atomic = fs_write; c.host.list_dirs = fs_list; c.host.exists = fs_exists; c.host.log = fs_log;
    lce_loader *l = lce_loader_create(&c);
    lce_loader_discover(l);
    return l;
}

int main(int argc, char **argv) {
    std::string pkgdir = argc > 1 ? argv[1] : "build/pkg";
    // fake image: vtable region at base+0x126e638-16
    static uintptr_t region[2 + LCE195_ITEM_VTABLE_SLOTS];
    g_vt_region = region;
    g_base = (uintptr_t)region + 16 - LCE195_RVA_ITEM_VTABLE;
    region[0] = 0; region[1] = 0x5151;  // offset-to-top, RTTI
    for (unsigned i = 0; i < LCE195_ITEM_VTABLE_SLOTS; ++i) region[2 + i] = g_base + 0x300000 + 8 * i;
    region[2 + LCE195_ITEM_NAME_SLOT] = g_base + LCE195_RVA_ITEM_NAME;
    static uint8_t stick[0xa0], apple[0xa0];
    *(uint32_t *)(stick + 0x28) = 0x1f; *(uint32_t *)(apple + 0x28) = 0x21;
    g_registry[280] = (uintptr_t)stick; g_registry[260] = (uintptr_t)apple;

    lce195_natives nat{fake_alloc, fake_item_ctor, fake_wstring, fake_item_by_id, fake_name_original};
    lce195_seams seams{seam_add_provider, seam_catalog_add, lce195x_recipe_provider_register};
    lce195_memory mem{nullptr, mem_read, mem_write, crafting_status_held, nullptr};
    g_empty_stack = (uintptr_t)fake_alloc(0xa8);
    memset(&g_xn, 0, sizeof g_xn);
    g_xn.alloc_array = fake_alloc; g_xn.stack_ctor = stack_ctor; g_xn.shaped_ctor = shaped_ctor; g_xn.shapeless_ctor = shapeless_ctor;
    g_xn.output_key = output_key; g_xn.recipe_bucket = recipe_bucket; g_xn.recipe_vec_grow = vec_grow; g_xn.stack_vec_grow = vec_grow;
    mock_game_init(&occupancy, 0);

    files["/data/lce-mods/rubyretail/mod.lcm"] = load(pkgdir + "/rubyretail/mod.lcm");
    files["/data/lce-mods/rubymod/mod.lcm"] = load(pkgdir + "/rubymod/mod.lcm");

    static lce195_backend rb;
    lce_loader *l = boot(&rb, nat, seams, mem);
    CHECK_EQ(lce_loader_commit(l), LCE_OK);
    // rubymod (blocks, smelting, shapeless) is dropped with an explicit reason; rubyretail is armed
    const lce_mod_report *ruby = nullptr, *retail = nullptr;
    for (uint32_t i = 0; i < lce_loader_mod_count(l); ++i) {
        if (!strcmp(lce_loader_mod(l, i)->modid, "rubymod")) ruby = lce_loader_mod(l, i);
        if (!strcmp(lce_loader_mod(l, i)->modid, "rubyretail")) retail = lce_loader_mod(l, i);
    }
    CHECK(ruby && retail);
    CHECK_EQ(ruby->state, LCE_MOD_DROPPED);
    CHECK_EQ(ruby->status, LCE_E_UNSUPPORTED);
    printf("rubymod on retail195: %s\n", ruby->reason);
    CHECK_EQ(retail->state, LCE_MOD_LIVE);
    CHECK_EQ(lce195_backend_status(&rb), LCE195_SDK_ARMED);
    CHECK_EQ(g_providers.size(), 1);
    CHECK_EQ(lce195x_recipe_provider_status(LCE195_SDK_PROVIDER), 0u);  // registered in the items195 (ABI 2) registry, not run yet
    CHECK(rb.iface.capabilities & LCE_CAP_RECIPES);
    CHECK(!(rb.iface.capabilities & (LCE_CAP_TOOLS | LCE_CAP_SMELTING | LCE_CAP_BLOCKS | LCE_CAP_FUEL)));  // unbound -> never advertised
    // nothing touched the "game" during commit
    CHECK(g_registry.size() == 2);

    // ---- startup: items phase (as the shipping item window would call it) ----
    mc195_items_provider &p = g_providers[0];
    CHECK_EQ(p.prepare(p.opaque, g_base), 1);
    CHECK_EQ(p.prepare(p.opaque, g_base + 1), 0);
    static lce195_items_window window;
    static lce195_item_catalog catalog;
    CHECK_EQ(p.collect(p.opaque, &window, &catalog), 1);
    CHECK_EQ(catalog.count, rb.item_count);
    CHECK_EQ(catalog.count, 3);
    int32_t ruby_id = lce_loader_item_id(l, "rubyretail:ruby"), wand_id = lce_loader_item_id(l, "rubyretail:ruby_wand");
    CHECK(ruby_id >= 2283 && wand_id >= 2283);
    for (unsigned i = 0; i < catalog.count; ++i) {
        const lce195_additive_item &e = catalog.entries[i];
        CHECK(!strcmp(e.provider, LCE195_SDK_PROVIDER));
        CHECK_EQ(p.reserved(p.opaque, e.provider, e.id, e.key), 1);
        CHECK_EQ(p.reserved(p.opaque, "steel", e.id, e.key), 0);
        CHECK(p.icon(p.opaque, e.icon) != nullptr);
        uintptr_t vt = *(uintptr_t *)e.item;
        CHECK(vt != g_base + LCE195_RVA_ITEM_VTABLE);                       // private copy installed
        CHECK_EQ(((uintptr_t *)vt)[-1], 0x5151);                            // RTTI preserved
        CHECK_EQ(((uintptr_t *)vt)[LCE195_ITEM_NAME_SLOT], (uintptr_t)&lce195_sdk_name_dispatch);
        CHECK_EQ(((uintptr_t *)vt)[3], region[2 + 3]);                      // every other slot identical
        CHECK_EQ(*(uint64_t *)(e.item + 0x28), LCE195_ITEM_CLASS_MATERIAL);
        CHECK_EQ(*(uint32_t *)(e.item + 0x18), e.max_stack);
        CHECK_EQ(*(uint32_t *)(e.item + 0x1c), 0);
        const uint16_t *icon16 = *(const uint16_t **)(e.item + 0x78);
        char icon[32] = {0}; for (int k = 0; k < 31 && icon16[k]; ++k) icon[k] = (char)icon16[k];
        CHECK(!strcmp(icon, e.icon));
        g_registry[e.id] = e.item;  // what lce195_native_items_publish does next (0x3af660)
    }
    CHECK_EQ(p.identity(p.opaque), 1);
    CHECK(p.icon(p.opaque, "stock_icon") == nullptr);
    // names: our item -> translated en_US string from the package; others fall back
    uint8_t out[0x28];
    memset(out, 0xee, sizeof out);
    CHECK(lce195_sdk_name_dispatch(out, (void *)(uintptr_t)ruby_id, nullptr) == out);  // not an item pointer -> original
    CHECK_EQ(g_name_original_calls, 1);
    CHECK(lce195_sdk_name_dispatch(out, (void *)g_registry[(unsigned)ruby_id], nullptr) == out);
    const uint16_t *name = *(const uint16_t **)(out + 8);
    char nm[64] = {0}; for (int k = 0; name[k] && k < 63; ++k) nm[k] = name[k] < 128 ? (char)name[k] : '?';
    CHECK(!strcmp(nm, "Ruby"));
    CHECK(lce195_sdk_name_dispatch(out, (void *)g_registry[(unsigned)wand_id], nullptr) == out);
    name = *(const uint16_t **)(out + 8);
    CHECK_EQ(name[0], 'R');
    // tampering with the vptr breaks identity (what the bridge re-checks later)
    uintptr_t saved = *(uintptr_t *)g_registry[(unsigned)wand_id];
    *(uintptr_t *)g_registry[(unsigned)wand_id] = g_base + LCE195_RVA_ITEM_VTABLE;
    CHECK_EQ(p.identity(p.opaque), 0);
    *(uintptr_t *)g_registry[(unsigned)wand_id] = saved;
    CHECK_EQ(lce195_backend_status(&rb), LCE195_SDK_ITEMS_DONE);

    // ---- crafting phase (ABI 2 publisher via the items195 provider registry) ----
    uintptr_t owner = make_owner();
    g_crafting_held = 1;
    CHECK_EQ(lce195x_recipe_providers_publish(owner), 1);   // what content_process.cpp calls (one-line r72 change)
    CHECK_EQ(lce195x_recipe_provider_status(LCE195_SDK_PROVIDER), 2u);
    CHECK_EQ(recipe_count(owner), 2u);
    int32_t nugget_id = lce_loader_item_id(l, "rubyretail:ruby_nugget");
    for (unsigned i = 0; i < recipe_count(owner); ++i) {
        uintptr_t r = recipe_at(owner, i);
        CHECK_EQ(*(uintptr_t *)r, g_base + LCE195X_RVA_SHAPED_VT);
        uintptr_t out = *(uintptr_t *)(r + 0x30), *cells = *(uintptr_t **)(r + 0x28);
        int w = *(int *)(r + 0x1c), h = *(int *)(r + 0x20);
        CHECK_EQ(*(int *)(out + 0x10), 1);                              // count 1
        if ((int32_t)id_of(*(uintptr_t *)(out + 0x28)) == ruby_id) {
            CHECK_EQ(w, 3); CHECK_EQ(h, 3);
            for (int c = 0; c < 9; ++c) CHECK_EQ((int32_t)id_of(*(uintptr_t *)(cells[c] + 0x28)), nugget_id);
            CHECK_EQ(*(unsigned *)(r + 0xc), 0x1fu);                    // classification of the output item
        } else {
            CHECK_EQ((int32_t)id_of(*(uintptr_t *)(out + 0x28)), wand_id);
            // pattern [" R","S "] cropped to 2x2 row-major; empty cells -> the shared empty stack
            CHECK_EQ(w, 2); CHECK_EQ(h, 2);
            CHECK_EQ(cells[0], g_empty_stack); CHECK_EQ((int32_t)id_of(*(uintptr_t *)(cells[1] + 0x28)), ruby_id);
            CHECK_EQ(id_of(*(uintptr_t *)(cells[2] + 0x28)), 280u); CHECK_EQ(cells[3], g_empty_stack);
        }
    }
    // the Mods crafting tab snapshot sees both, with their categories (tools 0 / materials 1)
    uintptr_t snap[8]; unsigned cats[8];
    CHECK_EQ(lce195x_recipe_snapshot_all(owner, snap, cats, 8), 2u);
    CHECK((cats[0] == 1 && cats[1] == 0) || (cats[0] == 0 && cats[1] == 1));
    CHECK_EQ(lce195_backend_status(&rb), LCE195_SDK_RECIPES_DONE);

    // ---- negative paths --------------------------------------------------------
    {   // unbound backend: prepare refuses before anything is armed
        static lce195_backend nb;
        lce195_natives none{};
        lce_loader *l2 = boot(&nb, none, seams, mem);
        // unbound natives -> no recipe capability -> the mod is dropped at validation,
        // and the backend is never armed (no provider registered anywhere)
        lce_loader_commit(l2);
        for (uint32_t i = 0; i < lce_loader_mod_count(l2); ++i) CHECK(lce_loader_mod(l2, i)->state != LCE_MOD_LIVE);
        CHECK_EQ(lce195_backend_status(&nb), LCE195_SDK_IDLE);
        lce_loader_destroy(l2);
    }
    {   // second backend in the same process is refused (process-lifetime single owner)
        static lce195_backend b3;
        g_providers.clear();
        files.erase("/data/lce-mods/rubymod/mod.lcm");
        lce_loader *l3 = boot(&b3, nat, seams, mem);
        CHECK(lce_loader_commit(l3) != LCE_OK);
        lce_loader_destroy(l3);
    }
    lce_loader_destroy(l);  // test only: a published loader is never destroyed on the console
    TEST_MAIN_END("test_retail195");
}
