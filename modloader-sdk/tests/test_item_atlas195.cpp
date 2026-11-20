// Item-atlas growth + icon binding for SDK items through the REAL shipping
// resource owner (r72 super_content_steel_nbt_save_195/resource.cpp, compiled
// read-only with -DSTEEL_RESOURCE_TEST), the real UV planner and the real
// atlas layout/compose (additive_items_195/atlas.c). The native image/icon
// calls are replaced by host stand-ins that keep their contract: the grown
// image is composed with lce195_atlas_compose, and each bound icon records the
// atlas cell lce195_icons_bind_uploaded would give it (lce195_atlas_position).
//
// Regression (rubyretail on the console): in an SDK-only session the bind step
// required >= 6 catalog entries (the Steel pair + 4 extras), so a mod with 1..5
// items failed with STEEL_RESOURCE reason=23 and every SDK item kept the atlas
// fallback sprite (a stretched stock cell) in the inventory, hand and world.
//
// Each scenario runs in its own process (resource.cpp keeps process state).
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "check.h"
extern "C" {
#include "../../r72-session-candidate/loader/additive_items_195/atlas.h"
#include "../../r72-session-candidate/loader/steel_195_static_audit/grown_image_195.h"
#include "../../r72-session-candidate/loader/steel_startup_trace_195/atlas_sample.h"
}
#include "../../r72-session-candidate/loader/additive_items_195/icons.h"

extern "C" {
int steel195_resource_seed(uintptr_t);
uintptr_t steel195_resource_grow(uintptr_t, uintptr_t);
int steel195_resource_bind(uintptr_t);
unsigned steel195_resource_status(void);
uintptr_t steel195_resource_retire_begin(uintptr_t);
void steel195_resource_retire_end(uintptr_t, uintptr_t);
}

namespace {
enum Mode { SDK_ONLY, STEEL, LUCKY_ALONE };
Mode g_mode;
const uintptr_t kBase = 0x400000, kFallback = 0x5550;
alignas(16) unsigned char g_atlas[0x120], g_source[0x60], g_clone[0x60], g_texture[0xc0], g_stock[288][0xc0];
alignas(16) unsigned char g_items[64][0xd0];
unsigned char g_pixels[64][1024];
unsigned char *g_composed;  // 256 x height RGBA, from the real compose
unsigned g_height;
struct FakeIcon { uint32_t x, y; unsigned generation; };
FakeIcon g_icons[LCE195_ICON_GENERATIONS][64];
lce195_item_catalog g_catalog;
unsigned g_shown, g_binds;

void tile(unsigned i, unsigned char *out) {  // distinct, non-uniform pixels per item
    for (unsigned p = 0; p < 256; ++p) {
        out[p * 4] = (unsigned char)(i * 7 + 1); out[p * 4 + 1] = (unsigned char)p;
        out[p * 4 + 2] = (unsigned char)(255 - i); out[p * 4 + 3] = (p % 3) ? 255 : 0;
    }
}
}  // namespace

// ---- shipping seams resource.cpp calls (host stand-ins) ------------------------
extern "C" {
int32_t scePthreadGetthreadid(void) { return 71; }
int32_t sceKernelQueryMemoryProtection(void *, void **lo, void **hi, int32_t *prot) { *lo = nullptr; *hi = (void *)UINTPTR_MAX; *prot = 3; return 0; }
extern const unsigned char lce_steel_ingot_rgba_195[1024];
extern const unsigned char lce_steel_pickaxe_rgba_195[1024];
const unsigned char lce_steel_ingot_rgba_195[1024] = {1};
const unsigned char lce_steel_pickaxe_rgba_195[1024] = {2};
int steel195_tools_runtime_sdk_only(void) { return g_mode == SDK_ONLY; }
uintptr_t steel195_tools_runtime_sdk_base(void) { return g_mode == SDK_ONLY ? kBase : 0; }
int steel195_tools_runtime_identity(void) { return 1; }
const lce195_item_catalog *steel195_tools_runtime_catalog(void) { return &g_catalog; }
unsigned steel195_tools_runtime_atlas_height(void) { lce195_atlas_layout l{}; return lce195_atlas_layout_make(&g_catalog, &l) ? l.height : 0; }
void steel195_tools_runtime_icons(unsigned n, unsigned) { g_shown = n; }
unsigned steel195_owner_status(void) { return g_mode == STEEL ? 3 : 0; }
int steel195_owner_items(uintptr_t *a, uintptr_t *b) {
    if (g_mode != STEEL) return 0;
    *a = (uintptr_t)g_items[0]; *b = (uintptr_t)g_items[1]; return 1;
}
uintptr_t steel195_content_resource_identity(uintptr_t a, uintptr_t b) { return g_mode == STEEL && a == (uintptr_t)g_items[0] && b == (uintptr_t)g_items[1] ? kBase : 0; }
int mc195_lucky_alone(void) { return g_mode == LUCKY_ALONE; }
}
// resource.cpp reads the Lucky-only owner's publisher.base: use the real layout.
#include "../../r72-session-candidate/loader/super_content_steel_nbt_save_195/lucky_only_items.h"
static mc195_lucky_only_items g_lucky;
extern "C" {
const mc195_lucky_only_items *mc195_lucky_only_owner(void) { return &g_lucky; }
int mc195_lucky_only_identity(const mc195_lucky_only_items *) { return g_mode == LUCKY_ALONE; }
const lce195_item_catalog *mc195_lucky_only_atlas_catalog(const mc195_lucky_only_items *) { return g_mode == LUCKY_ALONE ? &g_catalog : nullptr; }
int steel_atlas_sample_195(uintptr_t a, void *, int (*)(void *, uintptr_t, void *, size_t), struct steel_atlas_sample_195 *out) {
    out->state = 2; out->count = 288; out->atlas = a; out->sentinel = 0x1234;
    for (unsigned i = 0; i < 288; i++) {
        out->records[i].icon = (uintptr_t)g_stock[i]; out->records[i].node = 0x5000 + i;
        out->records[i].name_length = 1; out->records[i].name[0] = (uint16_t)(i + 1);
        memcpy(out->records[i].uv_bits, g_stock[i] + 0x90, 16);
    }
    return 1;
}
// Native image clone: the real compose of the stock image plus every catalog tile.
int lce_grown_image_build_195(lce_grown_image_195 *s) {
    if (!s->held(nullptr) || !s->assets_valid(nullptr, s->ingot_pixels, s->pickaxe_pixels)) return 0;
    lce195_atlas_layout l{};
    if (!lce195_atlas_layout_make(&g_catalog, &l)) return 0;
    static unsigned char stock[256 * 288 * 4];
    for (size_t i = 0; i < sizeof stock; ++i) stock[i] = (unsigned char)(i * 31 + 5);
    const uint8_t *tiles[LCE195_ADDITIVE_CAPACITY] = {};
    for (unsigned i = 0; i < l.count; ++i) tiles[i] = g_pixels[i];
    free(g_composed);
    g_composed = (unsigned char *)malloc((size_t)256 * l.height * 4);
    if (!lce195_atlas_compose(&l, stock, sizeof stock, tiles, g_composed, (size_t)256 * l.height * 4)) return 0;
    g_height = l.height;
    s->state = 2; s->result = (uintptr_t)g_clone;
    return 1;
}
int lce_grown_image_dispose_195(lce_grown_image_195 *) { return 1; }
void steel_test_dispose_source(uintptr_t) {}
// Icon binding: same preconditions and cell choice as additive_items_195/icons.cpp.
int lce195_icons_bind_uploaded(lce195_icons_owner *o, uintptr_t texture, unsigned generation, uintptr_t fallback) {
    if (!o || !o->catalog || !texture || !fallback || o->bound_generation || o->generations >= LCE195_ICON_GENERATIONS ||
        !lce195_atlas_layout_make(o->catalog, &o->layout) || !o->ready(o->context, texture, generation)) return 0;
    for (unsigned i = 0; i < o->catalog->count; ++i) if (*(uintptr_t *)(o->catalog->entries[i].item + 0x20) != fallback) return 0;
    unsigned g = o->generations++;
    o->count = o->catalog->count;
    for (unsigned i = 0; i < o->count; ++i) {
        FakeIcon &ic = g_icons[g][i];
        if (!lce195_atlas_position(&o->layout, i, &ic.x, &ic.y)) return -1;
        ic.generation = generation;
        o->icons[g][i] = (uintptr_t)&ic;
        *(uintptr_t *)(o->catalog->entries[i].item + 0x20) = (uintptr_t)&ic;
    }
    o->texture = texture; o->fallback = fallback; o->bound_generation = g + 1; o->state = 2; ++g_binds;
    return 1;
}
int lce195_icons_detach(lce195_icons_owner *o, uintptr_t fallback) {
    if (!o->bound_generation) return 1;
    for (unsigned i = 0; i < o->count; ++i) *(uintptr_t *)(o->catalog->entries[i].item + 0x20) = fallback;
    o->bound_generation = 0; o->state = 0; return 1;
}
}

// One startup (and optional reloads) in this process; returns the number of failed checks.
static int scenario(Mode mode, unsigned n, unsigned reloads, int expect_ok) {
    g_mode = mode;
    memset(&g_catalog, 0, sizeof g_catalog);
    g_catalog.state = 1; g_catalog.count = n;
    for (unsigned i = 0; i < n; ++i) {
        g_catalog.entries[i].item = (uintptr_t)g_items[i];
        static char names[64][16];
        snprintf(names[i], sizeof names[i], "lcesdk_%u", 2283 + i);
        g_catalog.entries[i].icon = names[i];
        *(uintptr_t *)(g_items[i] + 0x20) = kFallback;
        tile(i, g_pixels[i]);
    }
    g_lucky.publisher.base = kBase;
    *(uintptr_t *)(g_atlas + 0xe8) = kFallback;
    for (unsigned i = 0; i < 288; i++) {
        *(uintptr_t *)g_stock[i] = kBase + 0x13451d0;
        float *uv = (float *)(g_stock[i] + 0x90);
        uv[0] = (float)(i % 16) / 16; uv[1] = (float)(i % 16 + 1) / 16; uv[2] = (float)(i / 16) / 18; uv[3] = (float)(i / 16 + 1) / 18;
        uv[4] = 0.000268f; uv[5] = 0.000268f;
    }
    for (unsigned cycle = 0; cycle <= reloads; ++cycle) {
        CHECK_EQ(steel195_resource_seed((uintptr_t)g_atlas), 1);
        CHECK_EQ(steel195_resource_grow((uintptr_t)g_source, (uintptr_t)g_atlas), (uintptr_t)g_clone);
        *(uintptr_t *)(g_atlas + 0xf0) = (uintptr_t)g_texture;
        int rc = steel195_resource_bind((uintptr_t)g_atlas);
        if (!expect_ok) {
            CHECK_EQ(rc, 0); CHECK_EQ(steel195_resource_status(), 7);
            for (unsigned i = 0; i < n; ++i) CHECK_EQ(*(uintptr_t *)(g_items[i] + 0x20), kFallback);
            return g_failures;
        }
        CHECK_EQ(rc, 1);
        CHECK_EQ(steel195_resource_status(), 6);
        CHECK_EQ(g_shown, n);
        CHECK_EQ(g_height, 288u + 16u * ((n + 15) / 16));
        // Every item's bound icon names the atlas cell that holds exactly its own pixels.
        for (unsigned i = 0; i < n; ++i) {
            const FakeIcon *ic = (const FakeIcon *)*(uintptr_t *)(g_items[i] + 0x20);
            CHECK(ic != nullptr && (uintptr_t)ic != kFallback);
            if (!ic || (uintptr_t)ic == kFallback) continue;
            CHECK(ic->y >= 288 && ic->y + 16 <= g_height && ic->x + 16 <= 256);
            int same = 1;
            for (unsigned row = 0; row < 16; ++row)
                same &= !memcmp(g_composed + ((size_t)(ic->y + row) * 256 + ic->x) * 4, g_pixels[i] + row * 64, 64);
            CHECK(same);
        }
        // Stock sprites keep their pixel rows after the growth (UVs rescaled to the new height).
        for (unsigned i = 0; i < 288; ++i) {
            const float *uv = (const float *)(g_stock[i] + 0x90);
            CHECK_EQ((int)(uv[2] * (float)g_height), (int)(((float)(i / 16) / 18) * 288));
        }
        if (cycle == reloads) break;
        uintptr_t token = steel195_resource_retire_begin((uintptr_t)g_atlas);
        CHECK(token != 0);
        for (unsigned i = 0; i < n; ++i) CHECK_EQ(*(uintptr_t *)(g_items[i] + 0x20), kFallback);
        *(uintptr_t *)(g_atlas + 0xf0) = 0;
        steel195_resource_retire_end((uintptr_t)g_atlas, token);
    }
    CHECK_EQ(g_binds, reloads + 1);
    return g_failures;
}

static int run(const char *name, Mode mode, unsigned n, unsigned reloads, int ok) {
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) { g_failures = 0; int f = scenario(mode, n, reloads, ok); printf("  %-34s %s\n", name, f ? "FAIL" : "ok"); fflush(stdout); _exit(f ? 1 : 0); }
    int st = 0;
    waitpid(pid, &st, 0);
    ++g_checks;
    if (!WIFEXITED(st) || WEXITSTATUS(st)) {
        ++g_failures;
        fprintf(stderr, "scenario %s failed (%s %d)\n", name, WIFEXITED(st) ? "exit" : "signal", WIFEXITED(st) ? WEXITSTATUS(st) : WTERMSIG(st));
    }
    return 0;
}

int main() {
    // SDK-only sessions (Steel and Lucky off): every item count the catalog allows.
    run("sdk-only 1 item", SDK_ONLY, 1, 0, 1);
    run("sdk-only 2 items", SDK_ONLY, 2, 0, 1);
    run("sdk-only 3 items (rubyretail)", SDK_ONLY, 3, 2, 1);
    run("sdk-only 5 items", SDK_ONLY, 5, 0, 1);
    run("sdk-only 6 items", SDK_ONLY, 6, 0, 1);
    run("sdk-only 16 items (one row)", SDK_ONLY, 16, 0, 1);
    run("sdk-only 17 items (two rows)", SDK_ONLY, 17, 1, 1);
    run("sdk-only 64 items (capacity)", SDK_ONLY, 64, 0, 1);
    // Steel on: 6 Steel entries + SDK entries; the Steel gate stays >= 6.
    run("steel 6 + sdk 3", STEEL, 9, 1, 1);
    run("steel without its 6 entries", STEEL, 5, 0, 0);
    // Lucky alone (SDK items ride the Lucky-only owner).
    run("lucky-alone 1 item", LUCKY_ALONE, 1, 0, 1);
    run("lucky-alone 4 items", LUCKY_ALONE, 4, 1, 1);
    TEST_MAIN_END("test_item_atlas195");
}
