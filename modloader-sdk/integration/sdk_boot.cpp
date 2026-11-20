// Shipping-plugin entry points for the LCE mod loader SDK (console build).
//
// The patched shipping code (integration/shipping-plugin.patch) calls these,
// in this order (see INTEGRATION.md section 2 and loader/retail195/composite195.h):
//
//   super.cpp, after attestation, before steel195_tools_runtime_prepare():
//     lce_sdk_mods_present()            cheap: any /data/lce-mods/<id>/mod.lcm ?
//     lce_sdk_boot(base, logline)       discover, run scripts, validate, allocate +
//                                       persist IDs, arm the composite backend
//                                       (item + ABI-2 recipe providers). No game memory.
//     lce_sdk_blocks_hook()             block registry data hook (retry while 0 + waiting)
//     lce_sdk_events_wanted()           add the events195 Mc195SharedScope subscriber
//                                       (lce_sdk_scope_tick/finish/failed)
//   game Block init                     relay at the registry vptr publishes SDK blocks
//   registry_trace.cpp items()          provider collect() already ran inside the item
//                                       window; then lce_sdk_after_items() (BlockItems)
//   content_process.cpp crafting cb     lce195x_recipe_providers_publish(owner)
//   super.cpp ui_tick (title)           lce_sdk_ui_tick(controller): strings, potions,
//                                       enchantments, furnace, brewing, biomes, entity
//                                       types/spawns/renderers, worldgen, audio (once);
//                                       afterwards every tick: mod string ids ensure
//   world entry                         lce_sdk_world_entry_allowed()
//
// Binds to these existing symbols of the shipping content stack
// (r72-session-candidate/loader): mc195_custom_items_add_provider,
// lce195_catalog_add, lce195_items_window_held, mc195_lucky_server_current,
// mc195_lucky_server_borrow. Everything else is inside lcemodloader.o.
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "sdk_creative_blocks.h"
#include "sdk_creative_potions.h"
#include "../loader/loader.h"
#if defined(LCE_EXPERIMENTAL_VIEWPORT_TINT)
#include "viewport_tint_config.hpp"
#endif
#include "../loader/package.h"
#include "../loader/retail195/composite195.h"
#include "../loader/retail195/host_ps4.h"
#include "../loader/retail195/id_space_195.h"
#include "../modules/events195/ev195.h"
#include "../third_party/r72/lucky_block_world_events_195/server_scope.h"

static_assert(sizeof(lce195_view) == sizeof(mc195_lucky_server_view), "events195 view must mirror mc195_lucky_server_view");
static_assert(offsetof(lce195_view, player_object) == offsetof(mc195_lucky_server_view, player), "view layout");
static_assert(offsetof(lce195_view, tick) == offsetof(mc195_lucky_server_view, tick), "view layout");

/* netsync195: snapshot the ledger + committed plan for the LCE|ModIds
   handshake. "published" = boot state 2 (armed: the backend registers the
   items when the game builds Item::items). Weak so a build without the module
   links unchanged. */
/* r72_world_bind195.cpp: set_block / spawn / loaded / explode through the r72 Lucky Block world helpers. */
extern "C" const void *lce_sdk_world_ops(uintptr_t base, const lce_plan *plan);   /* lce195_world_ops* (see services_glue195.cpp) */
extern "C" int lce_sdk_world_entry_allowed(void);
extern "C" int lce195_world_entry_guard_arm(uintptr_t, void (*)(const char *)) __attribute__((weak));
extern "C" int lce195_world_entry_guard_retire(void) __attribute__((weak));
extern "C" int lce_netsync_configure_from_loader(const lce_loader *, int, void (*)(const char *)) __attribute__((weak));

namespace {
const char kRoot[] = "/data/lce-mods";
const char kLedger[] = "/data/lce-mods/reservations/sdk-ids-v1.txt";
constexpr uintptr_t kServerSingleton = 0x14648d0u;   // MinecraftServer* (natives195.h, console-proven)

lce195c g_composite;          // process lifetime (vtable copies and names live here)
lce_loader *g_loader;         // process lifetime once published; never destroyed
uintptr_t g_base;
void (*g_log)(const char *);
unsigned g_boot_state;        // 0 none, 1 booting, 2 armed, 3 nothing to do, 4 refused, 5 uncertain
unsigned g_events_ready, g_late_done;
bool g_world_entry_guard, g_world_entry_guard_uncertain;
uint32_t g_viewport_tint;
#if defined(LCE_WAYPOINT_PACKAGES)
constexpr unsigned kLayerWaypointLimit = 64;
lce_pkg_waypoint g_waypoints[LCE_PKG_WAYPOINT_LAYERS][kLayerWaypointLimit];
unsigned g_waypoint_count[LCE_PKG_WAYPOINT_LAYERS];
#endif

void logline(const char *line) {
    if (!g_log) return;
    char buf[320];
    snprintf(buf, sizeof buf, "%s\n", line);
    g_log(buf);
}
void retire_world_entry_guard() {
    if (g_world_entry_guard && lce195_world_entry_guard_retire() <= 0)
        logline("LCE_SDK WORLD_ENTRY: native call restoration failed");
}
bool needs_world_entry_guard() { return g_composite.has_entities || g_composite.fuel_count; }
void log_cb(void *, const char *line) { logline(line); }
int crafting_held(void *) { return lce195x_recipe_provider_status(LCE195_SDK_PROVIDER) == 1u; }

// ---- block/entity/worldgen memory adapters ----------------------------------
int cas_word(void *, uintptr_t a, uintptr_t e, uintptr_t d) { return lce_ps4_mem_cas8(nullptr, a, e, d); }
uint64_t thread_id(void *) { return lce_ps4_thread_id(); }
int attested(void *) { return g_base != 0; }
int unused(void *) { return 0; }
int unused4(void *, uintptr_t, uintptr_t, uint32_t) { return 0; }

// ---- events195 host (r72 server scope) --------------------------------------
uint64_t ev_tid(void *) { return lce_ps4_thread_id(); }
int ev_read(void *, uintptr_t a, void *v, size_t n) { return lce_ps4_mem_read(nullptr, a, v, n); }
int ev_write_word(void *, uintptr_t a, uintptr_t e, uintptr_t d) { return lce_ps4_mem_cas8(nullptr, a, e, d); }
int ev_is_code(void *, uintptr_t a) { return lce_ps4_is_code(g_base, a); }
int ev_current(void *, const lce195_view *v) { return mc195_lucky_server_current((const mc195_lucky_server_view *)(const void *)v); }
int ev_borrow(void *, const lce195_pair *p, uintptr_t level, lce195_view *out) {
    return mc195_lucky_server_borrow((const mc195_lucky_native_pair *)(const void *)p, level, (mc195_lucky_server_view *)(void *)out);
}
uint64_t ev_now(void *) { return lce_ps4_now_ms(); }
void ev_log(void *, const char *l) { logline(l); }

// ---- composite host callbacks -------------------------------------------------
uintptr_t dispatcher(void *) {
    uintptr_t mc = 0, d = 0;
    if (!lce_ps4_mem_read(nullptr, g_base + LCE195C_RVA_MINECRAFT, &mc, 8) || !mc) return 0;
    if (!lce_ps4_mem_read(nullptr, mc + LCE195C_MC_DISPATCHER, &d, 8)) return 0;
    return d;
}
int no_level(void *) {
    // Title screen after boot: no MinecraftServer and no server tick seen yet.
    uintptr_t server = 1;
    return lce_ps4_mem_read(nullptr, g_base + kServerSingleton, &server, 8) && !server && lce195_ev_server_ticks() == 0 ? 1 : 0;
}
uint64_t host_tid(void *) { return lce_ps4_thread_id(); }

struct Probe { int found; };
void probe_emit(void *ctx, const char *name) {
    Probe *p = (Probe *)ctx;
    if (p->found || !lce_valid_modid(name)) return;
    char path[96];
    snprintf(path, sizeof path, "%s/%s/mod.lcm", kRoot, name);
    lce_host_ops ops;
    lce_ps4_host_ops(&ops, nullptr, nullptr);
    if (ops.exists(nullptr, path)) p->found = 1;
}
}  // namespace

extern "C" int lce_sdk_mods_present(void) {
    lce_host_ops ops;
    lce_ps4_host_ops(&ops, nullptr, nullptr);
    Probe p{0};
    ops.list_dirs(nullptr, kRoot, probe_emit, &p);
    return p.found;
}

extern "C" int lce_sdk_boot(uintptr_t attested_base, void (*log)(const char *)) {
    unsigned zero = 0;
    if (!attested_base || !__atomic_compare_exchange_n(&g_boot_state, &zero, 1u, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return 0;
    g_log = log;
    g_base = attested_base;

    // Establish the native gate before exposing capabilities that can require it.
    const int guard = lce195_world_entry_guard_arm && lce195_world_entry_guard_retire
        ? lce195_world_entry_guard_arm(attested_base, g_log) : 0;
    if (guard < 0) {
        g_world_entry_guard_uncertain = true;
        logline("LCE_SDK WORLD_ENTRY: uncertain native guard publication");
        __atomic_store_n(&g_boot_state, 5u, __ATOMIC_RELEASE);
        return -1;
    }
    g_world_entry_guard = guard == 1;
    if (!g_world_entry_guard) logline("LCE_SDK WORLD_ENTRY: guard unavailable; custom entities and fuels disabled");

    // events195 first: its capability bits decide what the loader accepts.
    static lce195_host host;
    memset(&host, 0, sizeof host);
    host.image = attested_base; host.thread_id = ev_tid; host.read = ev_read; host.write_word = ev_write_word; host.is_code = ev_is_code;
    host.view_current = ev_current; host.view_borrow = ev_borrow; host.now_ms = ev_now; host.log = ev_log;
    g_events_ready = (unsigned)lce195_ev_init(&host, lce195c_event_sink, &g_composite);

    static lce195c_bindings b;
    memset(&b, 0, sizeof b);
    b.base = attested_base;
    lce195_natives_bind(&b.core, attested_base);
    b.seams.items_add_provider = mc195_custom_items_add_provider;
    b.seams.catalog_add = lce195_catalog_add;
    b.seams.recipe_provider_register = lce195x_recipe_provider_register;
    b.mem = lce195_memory{nullptr, lce_ps4_mem_read, lce_ps4_mem_write, crafting_held, nullptr};
    static lce195x_natives xn;
    lce195x_natives_bind(&xn, attested_base);
    b.items = &xn;
#if defined(LCE_EXPERIMENTAL_FUEL)
    if (g_world_entry_guard) b.fuel = fuel195_ps4_ops();
#endif
    static b195_natives bn;
    b195_natives_bind(&bn, attested_base);
    b.blocks = &bn;
    b.blocks_mem = b195_memory{nullptr, lce_ps4_mem_read, lce_ps4_mem_write, cas_word, thread_id, attested, unused, unused, unused, unused4};
    static le195_natives en;
    le195_natives_bind(&en, attested_base);
    b.entities = g_world_entry_guard ? &en : nullptr;
    b.entities_mem = le195_memory{nullptr, lce_ps4_mem_read, lce_ps4_mem_write};
    static lce_wg195_natives wn;
    lce_wg195_natives_bind(&wn, attested_base);
    b.worldgen = &wn;
    b.worldgen_mem = lce_wg195_memory{nullptr, lce_ps4_mem_read, lce_ps4_mem_cas8};
    // API 1.1 registries: strings (Localizer map), effects/potions, enchantments, biomes, custom audio
    b.strings = s195_env{attested_base, nullptr, lce_ps4_mem_read, lce_ps4_mem_write, nullptr, s195_native_call};
    b.strings_bound = 1;
    static lce195e_env fx;
    lce195e_natives_bind(&fx, attested_base);
    b.effects = &fx;
    static lce_b195_natives bio;
    lce_b195_natives_bind(&bio, attested_base);
    b.biomes = &bio;
    b.biomes_mem = lce_b195_memory{nullptr, lce_ps4_mem_read, lce_ps4_mem_write, lce_ps4_mem_cas8,
                                   [](void *, uintptr_t at, uint64_t e, uint64_t d) -> int { return lce_ps4_mem_patch_ro8(g_base, at, e, d); }};
    static a195_natives au;
    a195_natives_bind(&au, attested_base);
    b.audio = &au;
    b.audio_mem = a195_memory{nullptr, lce_ps4_mem_read, lce_ps4_mem_write,
                              [](void *, uintptr_t a, uintptr_t e, uintptr_t d) -> int { return lce_ps4_mem_cas8(nullptr, a, e, d); }};
    // API 1.9 throwables: the hit effects run through services in an events195 level frame, so only with events
    static p195_natives pn;
    p195_natives_bind(&pn, attested_base);
    b.projectiles = g_events_ready ? &pn : nullptr;
    b.projectiles_mem = p195_memory{nullptr, lce_ps4_mem_read, lce_ps4_mem_cas8,
                                    [](void *, uintptr_t at, uint64_t e, uint64_t d) -> int { return lce_ps4_mem_patch_ro8(g_base, at, e, d); },
                                    [](void *) -> uint64_t { return lce_ps4_thread_id(); }};
    b.event_caps = g_events_ready ? lce195_ev_capabilities() : 0;
    b.event_caps2 = g_events_ready ? lce195_ev_capabilities2() : 0;
    b.services = g_events_ready;
    b.scripts = 1;
    b.renderer_dispatcher = dispatcher;
    b.no_level = no_level;
    b.thread_id = host_tid;
    b.log = logline;
    lce195c_init(&g_composite, &b);

    lce_loader_config cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.root = kRoot;
    cfg.ledger_path = kLedger;
    // per-mod fail-closed; no natives; no placeholders on retail yet. Only mods
    // the user enabled load: the plugin's Mods list keeps <dir>/enabled in step
    // with its Next-launch choice before this boot (crashlog-build sdk_mods_list.cpp).
    cfg.flags = LCE_LOADER_REQUIRE_ENABLED;
#if defined(LCE_WAYPOINT_PACKAGES)
    cfg.flags |= LCE_LOADER_WAYPOINT_PACKAGES;
#endif
    lce195_default_id_space(&cfg.space);
    // Boot runs before the game builds Item::items, so no game function may be
    // called here. Occupancy at allocation time = static vanilla table +
    // reserved providers; the authoritative live vacancy check
    // (0x385af0 lookup) runs inside the held item window (items195 constructor).
    cfg.space.item_in_use = [](void *, uint32_t id) -> int { return id >= 2398 || lce_vanilla_item_exists(id); };
    lce195_ext_id_space(&cfg.space);   // effect 32..127, enchantment 100..1023, biome 168..254 then 50..126
    cfg.backend = &g_composite.iface;
    lce_ps4_host_ops(&cfg.host, log_cb, nullptr);

    g_loader = lce_loader_create(&cfg);
    if (!g_loader) {
        __atomic_store_n(&g_boot_state, 4u, __ATOMIC_RELEASE);
        retire_world_entry_guard();
        return 0;
    }
    lce195c_attach(&g_composite, g_loader);
    lce_loader_set_script_runner(g_loader, lce195c_script_runner, &g_composite);
    lce_loader_discover(g_loader);
    lce_status st = lce_loader_commit(g_loader);
    if (st == LCE_OK && g_events_ready &&
        !lce195g_services_install(g_loader, attested_base, lce_sdk_world_ops(attested_base, lce_loader_plan(g_loader))))
        logline("LCE_SDK services: services195 refused installation; runtime services report UNSUPPORTED");
    uint32_t live = 0;
    for (uint32_t i = 0; i < lce_loader_mod_count(g_loader); ++i) {
        const lce_mod_report *r = lce_loader_mod(g_loader, i);
        char line[400];
        snprintf(line, sizeof line, "LCE_SDK mod=%s v=%s state=%u status=%d items=%u blocks=%u recipes=%u smelts=%u reason=%s",
                 r->modid, r->version, r->state, (int)r->status, r->items, r->blocks, r->recipes, r->smelts, r->reason[0] ? r->reason : "-");
        logline(line);
        live += r->state == LCE_MOD_LIVE;
    }
    {
        char line[160];
        snprintf(line, sizeof line, "LCE_SDK caps=%08x events=%u scripts=%u", lce195c_capabilities(&g_composite), g_events_ready, lce195c_script_count(&g_composite));
        logline(line);
    }
    unsigned final_state = st != LCE_OK ? (lce195_backend_status(&g_composite.items) == LCE195_SDK_UNCERTAIN ? 5u : 4u) : live ? 2u : 3u;
    if (lce_netsync_configure_from_loader && !lce_netsync_configure_from_loader(g_loader, final_state == 2u, g_log))
        logline("LCE_SDK netsync: ledger not representable; the server will see an empty mod ID table");
#if defined(LCE_EXPERIMENTAL_VIEWPORT_TINT)
    if (final_state == 2u) g_viewport_tint = lce_viewport_tint::committed_color(g_loader);
#endif
#if defined(LCE_WAYPOINT_PACKAGES)
    if (final_state == 2u) {
        for (unsigned layer = 0; layer < LCE_PKG_WAYPOINT_LAYERS; ++layer) {
            unsigned total = lce_loader_waypoints(g_loader, layer, g_waypoints[layer], kLayerWaypointLimit);
            g_waypoint_count[layer] = total < kLayerWaypointLimit ? total : kLayerWaypointLimit;
            if (total > kLayerWaypointLimit) {
                char line[100];
                snprintf(line, sizeof line, "LCE_SDK waypoints: layer %u keeps %u of %u package markers",
                         layer, kLayerWaypointLimit, total);
                logline(line);
            }
        }
    }
#endif
    __atomic_store_n(&g_boot_state, final_state, __ATOMIC_RELEASE);
    if ((final_state != 2u || !needs_world_entry_guard()) && lce_sdk_world_entry_allowed()) retire_world_entry_guard();
    return final_state == 2u ? 1 : final_state == 5u ? -1 : 0;
}

/* The release of the final boot state publishes the immutable color. */
extern "C" uint32_t lce_sdk_viewport_tint_v1(void) {
    return __atomic_load_n(&g_boot_state, __ATOMIC_ACQUIRE) == 2u ? g_viewport_tint : 0;
}

/* 0 none, 1 booting, 2 armed, 3 nothing to load, 4 refused, 5 uncertain;
 * then the item/recipe backend phase in the high byte: LCE195_SDK_* << 8. */
extern "C" unsigned lce_sdk_status(void) {
    return __atomic_load_n(&g_boot_state, __ATOMIC_ACQUIRE) | (lce195_backend_status(&g_composite.items) << 8);
}

/* Event entry: loader subscribers + live scripts. */
extern "C" int lce_sdk_dispatch(int ev, const void *payload) {
    return g_loader ? lce195c_event_sink(&g_composite, (lce_event)ev, payload) : 0;
}

/* Phase 2: block registry data hook. 1 armed or nothing to do, 0 retry later
 * (registry global still null) or refused, -1 uncertain. */
extern "C" int lce_sdk_blocks_hook(void) {
    if (__atomic_load_n(&g_boot_state, __ATOMIC_ACQUIRE) != 2u) return 1;
    return lce195c_phase_blocks_hook(&g_composite);
}
extern "C" int lce_sdk_blocks_hook_waiting(void) { return lce195c_phase_state(&g_composite, LCE195C_PH_BLOCK_HOOK) == LCE195C_WAITING; }

/* Phase 5: right after steel195_tools_runtime_after_items() in registry_trace items(). */
extern "C" int lce_sdk_after_items(void) {
    if (__atomic_load_n(&g_boot_state, __ATOMIC_ACQUIRE) != 2u) return 1;
    return lce195c_phase_after_items(&g_composite);
}

extern "C" int lce_sdk_stock_items_wanted(void) {
    // A committed mod can use Lua services or late registries with only
    // stock items. Never use the stock route for an absent ordinary provider.
    return __atomic_load_n(&g_boot_state, __ATOMIC_ACQUIRE) == 2u &&
        g_composite.plan && g_composite.plan->item_count == 0;
}

extern "C" int lce_sdk_creative_blocks_wanted(void) {
    return __atomic_load_n(&g_boot_state, __ATOMIC_ACQUIRE) == 2u && lce195c_creative_blocks_wanted(&g_composite);
}

extern "C" int lce_sdk_creative_block_entries(lce_sdk_creative_block_entry *out, uint32_t capacity) {
    if (!out && capacity) return -1;
    return __atomic_load_n(&g_boot_state, __ATOMIC_ACQUIRE) == 2u
        ? lce195c_creative_block_entries(&g_composite, out, capacity) : 0;
}

extern "C" const char *lce_sdk_creative_block_key(uintptr_t item, uint32_t item_id) {
    return __atomic_load_n(&g_boot_state, __ATOMIC_ACQUIRE) == 2u
        ? lce195c_creative_block_key(&g_composite, item, item_id) : nullptr;
}

extern "C" int lce_sdk_creative_potions_wanted(void) {
    return __atomic_load_n(&g_boot_state, __ATOMIC_ACQUIRE) == 2u && lce195c_creative_potions_wanted(&g_composite);
}

extern "C" int lce_sdk_creative_potion_entries(uintptr_t base, void *ctx, int (*held)(void *),
                                               lce_sdk_creative_potion_entry *out, uint32_t entry_capacity, uint32_t row_capacity) {
    return __atomic_load_n(&g_boot_state, __ATOMIC_ACQUIRE) == 2u
        ? lce195c_creative_potion_entries(&g_composite, base, ctx, held, out, entry_capacity, row_capacity) : 0;
}

/* Phase 7: called from super.cpp ui_tick on the native UI thread. Runs the
 * late startup phase once, at the title, before any world exists. */
extern "C" void lce_sdk_ui_tick(void *) {
    if (__atomic_load_n(&g_boot_state, __ATOMIC_ACQUIRE) != 2u) return;
    lce195c_net_tick(&g_composite);   // API 1.6: remote clients have no server scope tick
    if (__atomic_load_n(&g_late_done, __ATOMIC_ACQUIRE)) { lce195c_ui_tick_strings(&g_composite); return; }   // re-insert mod names after a language change
    int rc = lce195c_phase_late(&g_composite);
    if (rc == 0 && lce195c_phase_state(&g_composite, LCE195C_PH_SMELT_BREW) == LCE195C_WAITING) return;   // a world is up: retry later
    __atomic_store_n(&g_late_done, 1u, __ATOMIC_RELEASE);
    if (needs_world_entry_guard() && lce_sdk_world_entry_allowed()) retire_world_entry_guard();
    char line[320];
    lce195c_status_line(&g_composite, line, sizeof line);
    logline(line);
}

/* Terrain atlas (gap G1): weak references from shared_terrain_195/runtime.cpp, called inside its reload lease.
 * super.cpp installs that runtime in SDK-only sessions when lce_sdk_terrain_wanted(). */
extern "C" uint32_t lce_sdk_terrain_tiles(void) {
    return __atomic_load_n(&g_boot_state, __ATOMIC_ACQUIRE) == 2u ? lce195c_terrain_tiles(&g_composite) : 0;
}
extern "C" int lce_sdk_terrain_wanted(void) { return lce_sdk_terrain_tiles() ? 1 : 0; }
extern "C" int lce_sdk_terrain_compose(uint32_t tile_size, unsigned mip, uint8_t *out, size_t bytes) {
    return lce_sdk_terrain_tiles() ? lce195c_terrain_compose(&g_composite, tile_size, mip, out, bytes) : 0;
}
extern "C" int lce_sdk_terrain_bind(uintptr_t atlas, uint32_t generation, uint32_t width, uint32_t height, uint32_t stock_height,
                                    uint32_t tile_size, uint32_t first_index, int (*held)(void),
                                    int (*uploaded)(uintptr_t, uintptr_t, uint32_t)) {
    if (!lce_sdk_terrain_tiles()) return 0;
    const lce195c_terrain_lease lease = {held, uploaded};
    return lce195c_terrain_bind(&g_composite, &lease, atlas, generation, width, height, stock_height, tile_size, first_index);
}
extern "C" int lce_sdk_terrain_detach(int (*held)(void)) {
    if (__atomic_load_n(&g_boot_state, __ATOMIC_ACQUIRE) != 2u) return 1;
    const lce195c_terrain_lease lease = {held, nullptr};
    return lce195c_terrain_detach(&g_composite, &lease);
}

extern "C" int lce_sdk_world_entry_allowed(void) {
    const unsigned boot = __atomic_load_n(&g_boot_state, __ATOMIC_ACQUIRE);
    if (boot == 1u) return 0;
    if (boot == 5u) return !g_world_entry_guard_uncertain && !needs_world_entry_guard();
    if (boot != 2u || !needs_world_entry_guard()) return 1;
    return __atomic_load_n(&g_late_done, __ATOMIC_ACQUIRE) && lce195c_world_entry_allowed(&g_composite);
}

/* Shared-scope subscriber (super.cpp adds it to Mc195SharedScope). */
extern "C" int lce_sdk_events_wanted(void) {
    return __atomic_load_n(&g_boot_state, __ATOMIC_ACQUIRE) == 2u && g_events_ready ? 1 : 0;
}
extern "C" void lce_sdk_scope_tick(void *o, const void *view) { lce195_ev_scope_tick(o, (const lce195_view *)view); }
extern "C" void lce_sdk_scope_finish(void *o, uint64_t thread, uint64_t session) { lce195_ev_scope_finish(o, thread, session); }
extern "C" void lce_sdk_scope_failed(void *o, unsigned phase) { lce195_ev_scope_failed(o, phase); }

/* Keybinds: from the existing scePadRead GOT entry, after the debug chord. */
extern "C" uint32_t lce_sdk_pad_filter(int32_t handle, uint32_t buttons, int connected, int debug_menu_visible) {
    return g_events_ready ? lce195_keys_filter(handle, buttons, connected, debug_menu_visible) : buttons;
}
/* Commands: a host-typed chat line ("/ruby give"). 1 queued. */
extern "C" int lce_sdk_chat_submit(const char *utf8_line) { return g_events_ready ? lce195_cmd_submit(utf8_line) : 0; }

/* Mods menu (ps4-metadata-snapshot/crashlog-build/mods_content.cpp): what this
 * boot's loader did with /data/lce-mods/<modid>. Safe from any thread once the
 * boot finished (the reports are not changed after lce_loader_commit).
 *   -1 the loader did not run (no mod.lcm at boot, still booting, or create failed)
 *    0 not among the discovered mods
 *    1 live (registered at this boot); version gets the package version
 *    2 discovered but rejected or dropped (reason in the LCE_SDK log line) */
#if defined(LCE_WAYPOINT_PACKAGES)
// The immutable package snapshot is complete before boot state 2 is published.
extern "C" unsigned lce_sdk_waypoints_available(void) {
    if (__atomic_load_n(&g_boot_state, __ATOMIC_ACQUIRE) != 2u) return 0;
    unsigned count = 0;
    for (unsigned n : g_waypoint_count) count += n;
    return count;
}

extern "C" size_t lce_sdk_copy_waypoints(uint32_t layer, lce_pkg_waypoint *out, size_t cap) {
    if (__atomic_load_n(&g_boot_state, __ATOMIC_ACQUIRE) != 2u || !out || layer >= LCE_PKG_WAYPOINT_LAYERS) return 0;
    const size_t count = g_waypoint_count[layer] < cap ? g_waypoint_count[layer] : cap;
    memcpy(out, g_waypoints[layer], count * sizeof *out);
    return count;
}
#endif

extern "C" int lce_sdk_mod_state(const char *modid, char *version, size_t cap) {
    if (version && cap) version[0] = 0;
    const unsigned st = __atomic_load_n(&g_boot_state, __ATOMIC_ACQUIRE);
    if (st < 2u || !g_loader || !modid) return -1;
    for (uint32_t i = 0; i < lce_loader_mod_count(g_loader); ++i) {
        const lce_mod_report *r = lce_loader_mod(g_loader, i);
        if (!r || strcmp(r->modid, modid)) continue;
        if (version && cap) snprintf(version, cap, "%s", r->version);
        return r->state == LCE_MOD_LIVE ? 1 : 2;
    }
    return 0;
}
