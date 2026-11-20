// Drives the SDK retail backend through the REAL shipping seam code compiled
// from r72-session-candidate/loader (read-only): lucky_items_bridge.cpp
// (provider bridge), additive_items_195/catalog.c and window.cpp (the held
// post-stock-items window, which validates a 2268-slot Item::items vector with
// 547 stock entries and the base Item vtable at slot 265, exactly as on the
// console). Game memory is a calloc'd fake image laid out at base+RVA.
#include <stdlib.h>
#include <string.h>
#include "../loader/loader.h"
#include "../loader/package.h"
#include "../loader/retail195/backend195.h"
#include "../loader/retail195/id_space_195.h"
#include "check.h"

static uintptr_t g_base;
static uintptr_t *g_slots;  // Item::items storage (capacity 2398)
alignas(16) static uint8_t g_heap[1 << 16];
static size_t g_used;
static void *fake_alloc(size_t n) { n = (n + 15) & ~(size_t)15; if (g_used + n > sizeof g_heap) return nullptr; void *p = g_heap + g_used; g_used += n; memset(p, 0, n); return p; }
static void fake_item_ctor(void *obj) {
    uintptr_t o = (uintptr_t)obj;
    *(uintptr_t *)o = g_base + LCE195_RVA_ITEM_VTABLE; *(uintptr_t *)(o + 0x10) = 1; *(uint64_t *)(o + 0x90) = 7; *(uint32_t *)(o + 0x18) = 64;
}
static void fake_wstring(void *dst, const uint16_t *u, uint64_t n) {
    uintptr_t d = (uintptr_t)dst; uint16_t *h = (uint16_t *)fake_alloc((n + 1) * 2); memcpy(h, u, n * 2);
    *(uint16_t **)(d + 8) = h; *(uint64_t *)(d + 0x18) = n; *(uint64_t *)(d + 0x20) = n > 7 ? n : 7;
}
static uintptr_t fake_item_by_id(unsigned id) { return id < 2398 ? g_slots[id] : 0; }
static void *fake_name_original(void *out, void *, void *) { return out; }
static int mem_read(void *, uintptr_t a, void *v, size_t n) { memcpy(v, (const void *)a, n); return 1; }
static int mem_write(void *, uintptr_t a, const void *v, size_t n) { memcpy((void *)a, v, n); return 1; }
static int never_held(void *) { return 0; }
static uint64_t thread_id(void *) { return 77; }
static int win_read(void *, uintptr_t a, void *v, size_t n) { memcpy(v, (const void *)a, n); return 1; }
static int no_recipes(const char *, void *, lce195x_recipe_provider_fn) { return 1; }

// Recipe natives are bound (so the recipe capability is advertised) but never
// called here: this test stops at the item window.
static void x_stack(void *, void *, int, int) {}
static void x_shaped(void *, int, int, void **, void *, int) {}
static void x_shapeless(void *, void *, void *, int) {}
static unsigned x_key(void *) { return 0; }
static uintptr_t x_bucket(uintptr_t, const unsigned *) { return 0; }
static void x_grow(uintptr_t, int) {}
extern "C" {
#include "../../r72-session-candidate/loader/additive_items_195/atlas.h"
#include "../../r72-session-candidate/loader/steel_tools_195/assets.h"
}
static uint8_t g_pkg[1 << 16];

// The package's own RGBA for an item key (what the mod author drew), or nullptr.
static const uint8_t *package_rgba(const lce_package_view &v, const char *item_key) {
    for (uint32_t i = 0; i < v.item_count; ++i) {
        if (strcmp(lce_package_str(&v, v.items[i].key), item_key)) continue;
        const char *tex = lce_package_str(&v, v.items[i].texture);
        for (uint32_t t = 0; t < v.tex_count; ++t)
            if (!strcmp(lce_package_str(&v, v.tex[t].key), tex) && v.tex[t].width == 16 && v.tex[t].height == 16 && v.tex[t].atlas == 0)
                return v.blob + v.tex[t].data_offset;
    }
    return nullptr;
}
static size_t g_pkg_size;

int main(int argc, char **argv) {
    const char *pkg = argc > 1 ? argv[1] : "build/pkg/rubyretail/mod.lcm";
    FILE *f = fopen(pkg, "rb");
    CHECK(f != nullptr);
    if (!f) TEST_MAIN_END("test_retail195_bridge");
    g_pkg_size = fread(g_pkg, 1, sizeof g_pkg, f);
    fclose(f);

    // fake image covering base .. base+0x1418000
    uint8_t *image = (uint8_t *)aligned_alloc(4096, 0x1418000);
    memset(image, 0, 0x1418000);
    g_base = (uintptr_t)image;
    uintptr_t *vt = (uintptr_t *)(g_base + LCE195_RVA_ITEM_VTABLE - 16);
    vt[0] = 0; vt[1] = 0x77777;
    for (unsigned i = 0; i < LCE195_ITEM_VTABLE_SLOTS; ++i) vt[2 + i] = g_base + 0x400000 + 16 * i;
    vt[2 + LCE195_ITEM_NAME_SLOT] = g_base + LCE195_RVA_ITEM_NAME;
    g_slots = (uintptr_t *)calloc(2398, 8);
    static uint8_t stock[547][0xa0];
    unsigned k = 0;
    for (unsigned id = 0; id < 2268 && k < 547; ++id)
        if (id == 0 || lce_vanilla_item_exists(id)) { *(uintptr_t *)stock[k] = g_base + LCE195_RVA_ITEM_VTABLE; g_slots[id] = (uintptr_t)stock[k++]; }
    CHECK_EQ(k, 547);  // the static table + air reproduce the live occupancy count
    uintptr_t *hdr = (uintptr_t *)(g_base + 0x1417720);
    hdr[0] = (uintptr_t)g_slots; hdr[1] = (uintptr_t)(g_slots + 2268); hdr[2] = (uintptr_t)(g_slots + 2398);

    lce195_natives nat{fake_alloc, fake_item_ctor, fake_wstring, fake_item_by_id, fake_name_original};
    lce195_seams seams{mc195_custom_items_add_provider, lce195_catalog_add, no_recipes};
    lce195_memory mem{nullptr, mem_read, mem_write, never_held, nullptr};
    static lce195_backend rb;
    lce195_backend_init(&rb, g_base, &nat, &seams, &mem);
    lce195x_natives xn;
    memset(&xn, 0, sizeof xn);
    xn.alloc_array = fake_alloc; xn.stack_ctor = x_stack; xn.shaped_ctor = x_shaped; xn.shapeless_ctor = x_shapeless;
    xn.output_key = x_key; xn.recipe_bucket = x_bucket; xn.recipe_vec_grow = x_grow; xn.stack_vec_grow = x_grow;
    lce195_backend_bind_items(&rb, &xn);

    lce_loader_config c;
    memset(&c, 0, sizeof c);
    lce195_default_id_space(&c.space);
    c.space.item_in_use = [](void *, uint32_t id) -> int { return id >= 2398 || g_slots[id] != 0; };
    c.backend = &rb.iface;
    static uint8_t ledger[1 << 16]; static size_t ledger_len;
    c.ledger_path = "/mem/ledger";
    c.host.read_file = [](void *, const char *, uint8_t *b, size_t cap) -> long { if (!ledger_len) return -1; if (ledger_len > cap) return -2; memcpy(b, ledger, ledger_len); return (long)ledger_len; };
    c.host.write_file_atomic = [](void *, const char *, const uint8_t *d, size_t n) -> int { if (n > sizeof ledger) return 0; memcpy(ledger, d, n); ledger_len = n; return 1; };
    lce_loader *l = lce_loader_create(&c);
    CHECK_EQ(lce_loader_add_package(l, g_pkg, g_pkg_size, "rubyretail"), LCE_OK);
    CHECK_EQ(lce_loader_commit(l), LCE_OK);
    CHECK(ledger_len > 0);  // IDs persisted before any provider ran
    CHECK_EQ(mc195_custom_items_count(), 1);  // our provider is registered in the real bridge

    // --- the shipping startup sequence (tools_runtime prepare worker, then the item window) ---
    CHECK_EQ(mc195_custom_items_prepare(g_base), 1);
    CHECK_EQ(mc195_custom_items_status(), 1);
    static lce195_items_window w;
    w.thread_id = thread_id; w.read = win_read;
    CHECK_EQ(lce195_items_window_enter(&w, g_base, 1), 1);
    static lce195_item_catalog cat;
    CHECK_EQ(mc195_custom_items_collect(&w, &cat), 1);
    unsigned code = 0, idx = 0;
    code = mc195_custom_items_failure(&idx);
    CHECK_EQ(code, 0);
    CHECK_EQ(mc195_custom_items_entry_count(), 3);
    CHECK_EQ(cat.count, 3);
    CHECK_EQ(mc195_custom_items_identity(), 1);
    for (unsigned i = 0; i < cat.count; ++i) {
        CHECK(mc195_custom_items_icon(cat.entries[i].icon) != nullptr);
        CHECK(cat.entries[i].id >= 2283 && cat.entries[i].id <= 2397);
        CHECK_EQ(mc195_custom_items_reserved(cat.entries[i].provider, cat.entries[i].id, cat.entries[i].key), 1);
    }
    CHECK(lce195_catalog_freeze(&cat) == 1);

    // --- item atlas: the shipping compose (additive_items_195/atlas.c) puts each SDK item's
    // own package pixels, unchanged (straight RGBA, like the Steel assets), into the cell the
    // icon owner binds for that catalog index (lce195_atlas_position). Icon names are unique
    // and never shadowed by a Steel asset name (tools_runtime compose looks those up first).
    {
        lce_package_view v;
        CHECK_EQ(lce_package_parse(g_pkg, g_pkg_size, &v, nullptr, 0), LCE_OK);
        lce195_atlas_layout layout{};
        CHECK_EQ(lce195_atlas_layout_make(&cat, &layout), 1);
        CHECK_EQ(layout.height, 288u + 16u * ((cat.count + 15) / 16));
        static uint8_t stock[256 * 288 * 4], out[256 * 352 * 4];
        for (size_t i = 0; i < sizeof stock; ++i) stock[i] = (uint8_t)(i * 13 + 1);
        const uint8_t *tiles[LCE195_ADDITIVE_CAPACITY] = {};
        for (unsigned i = 0; i < cat.count; ++i) {
            tiles[i] = mc195_custom_items_icon(cat.entries[i].icon);
            const uint8_t *want = package_rgba(v, cat.entries[i].key);
            CHECK(tiles[i] != nullptr && want != nullptr);
            if (tiles[i] && want) CHECK(!memcmp(tiles[i], want, 1024));
            CHECK(steel195_asset_find(cat.entries[i].icon) == nullptr);
            for (unsigned j = 0; j < i; ++j) CHECK(strcmp(cat.entries[i].icon, cat.entries[j].icon) != 0);
        }
        CHECK_EQ(lce195_atlas_compose(&layout, stock, sizeof stock, tiles, out, (size_t)256 * layout.height * 4), 1);
        CHECK(!memcmp(out, stock, sizeof stock));  // stock sprites untouched
        for (unsigned i = 0; i < cat.count; ++i) {
            uint32_t x = 0, y = 0;
            CHECK_EQ(lce195_atlas_position(&layout, i, &x, &y), 1);
            const uint8_t *want = package_rgba(v, cat.entries[i].key);
            int same = want != nullptr;
            for (unsigned row = 0; same && row < 16; ++row) same = !memcmp(out + ((size_t)(y + row) * 256 + x) * 4, want + row * 64, 64);
            CHECK(same);
        }
    }
    CHECK_EQ(lce195_items_window_held(&w), 1);  // we never touched the stock vector
    CHECK_EQ(lce195_items_window_leave(&w), 1);
    CHECK_EQ(lce195_backend_status(&rb), LCE195_SDK_ITEMS_DONE);
    lce_loader_destroy(l);  // test only
    free(g_slots);
    free(image);
    TEST_MAIN_END("test_retail195_bridge");
}
