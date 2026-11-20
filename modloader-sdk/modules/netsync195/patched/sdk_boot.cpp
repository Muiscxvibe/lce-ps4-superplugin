// Shipping-plugin entry points for the LCE mod loader SDK (console build).
//
// Call order inside super.cpp (see INTEGRATION.md, "Startup wiring"):
//   1. lce_sdk_mods_present()      - cheap: is there any /data/lce-mods/<id>/mod.lcm ?
//   2. lce_sdk_boot(base, logline) - after image attestation, on the startup
//      thread, BEFORE steel195_tools_runtime_prepare()/mc195_custom_items_prepare()
//      and before the crafting relay is installed: discovers, validates,
//      allocates/persists IDs and registers the "lce-sdk" item and recipe
//      providers. Touches no game memory.
//   3. the existing startup phases (item window, crafting owner callback, atlas
//      seed) then call the providers; lce_sdk_status() reports the outcome.
//
// Binds to these existing symbols of the shipping content stack
// (r72-session-candidate/loader): mc195_custom_items_add_provider,
// lce195_catalog_add, lce195_recipe_provider_register,
// lce195_recipe_provider_status, lce195_shaped_publish, lce195_items_window_held.
#include <stdio.h>
#include <string.h>
#include "../loader/loader.h"
#include "../loader/package.h"
#include "../loader/retail195/backend195.h"
#include "../loader/retail195/host_ps4.h"
#include "../loader/retail195/id_space_195.h"

extern "C" unsigned lce195_recipe_provider_status(const char *provider);

namespace {
const char kRoot[] = "/data/lce-mods";
const char kLedger[] = "/data/lce-mods/reservations/sdk-ids-v1.txt";

lce195_backend g_backend;     // process lifetime (vtable copies and names live here)
lce_loader *g_loader;         // process lifetime once published; never destroyed
void (*g_log)(const char *);
unsigned g_boot_state;        // 0 none, 1 booting, 2 armed, 3 nothing to do, 4 refused, 5 uncertain

void log_cb(void *, const char *line) {
    if (!g_log) return;
    char buf[300];
    snprintf(buf, sizeof buf, "%s\n", line);
    g_log(buf);
}
int crafting_held(void *) { return lce195_recipe_provider_status(LCE195_SDK_PROVIDER) == 1u; }

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

/* netsync195 (weak): snapshot the ledger + committed plan for the LCE|ModIds
   handshake. "published" = boot state 2 (armed: the backend will register the
   items when the game builds Item::items). */
extern "C" int lce_netsync_configure_from_loader(const lce_loader *, int, void (*)(const char *)) __attribute__((weak));

extern "C" int lce_sdk_boot(uintptr_t attested_base, void (*log)(const char *)) {
    unsigned zero = 0;
    if (!attested_base || !__atomic_compare_exchange_n(&g_boot_state, &zero, 1u, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return 0;
    g_log = log;
    lce195_natives natives;
    lce195_natives_bind(&natives, attested_base);
    lce195_seams seams;
    seams.items_add_provider = mc195_custom_items_add_provider;
    seams.catalog_add = lce195_catalog_add;
    seams.recipe_provider_register = lce195_recipe_provider_register;
    seams.shaped_publish = lce195_shaped_publish;
    lce195_memory mem{nullptr, lce_ps4_mem_read, lce_ps4_mem_write, crafting_held};
    lce195_backend_init(&g_backend, attested_base, &natives, &seams, &mem);

    lce_loader_config cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.root = kRoot;
    cfg.ledger_path = kLedger;
    cfg.flags = 0;  // per-mod fail-closed; no natives; no placeholders on retail yet
    lce195_default_id_space(&cfg.space);
    // Boot runs before the game builds Item::items, so no game function may be
    // called here. Occupancy at allocation time = static vanilla table +
    // reserved providers; the authoritative live vacancy check
    // (0x385af0 lookup) runs inside the held item window in prov_collect().
    cfg.space.item_in_use = [](void *, uint32_t id) -> int { return id >= 2398 || lce_vanilla_item_exists(id); };
    cfg.backend = &g_backend.iface;
    lce_ps4_host_ops(&cfg.host, log_cb, nullptr);

    g_loader = lce_loader_create(&cfg);
    if (!g_loader) { __atomic_store_n(&g_boot_state, 4u, __ATOMIC_RELEASE); return 0; }
    lce_loader_discover(g_loader);
    lce_status st = lce_loader_commit(g_loader);
    uint32_t live = 0;
    for (uint32_t i = 0; i < lce_loader_mod_count(g_loader); ++i) {
        const lce_mod_report *r = lce_loader_mod(g_loader, i);
        char line[220];
        snprintf(line, sizeof line, "LCE_SDK mod=%s v=%s state=%u status=%d items=%u recipes=%u reason=%s",
                 r->modid, r->version, r->state, (int)r->status, r->items, r->recipes, r->reason[0] ? r->reason : "-");
        log_cb(nullptr, line);
        live += r->state == LCE_MOD_LIVE;
    }
    unsigned final_state = st != LCE_OK ? (lce195_backend_status(&g_backend) == LCE195_SDK_UNCERTAIN ? 5u : 4u) : live ? 2u : 3u;
    if (lce_netsync_configure_from_loader && !lce_netsync_configure_from_loader(g_loader, final_state == 2u, g_log))
        log_cb(nullptr, "LCE_SDK netsync: ledger not representable; the server will see an empty mod ID table");
    __atomic_store_n(&g_boot_state, final_state, __ATOMIC_RELEASE);
    return final_state == 2u ? 1 : final_state == 5u ? -1 : 0;
}

/* 0 none, 1 booting, 2 armed, 3 nothing to load, 4 refused, 5 uncertain;
 * then the backend phase in the high byte: LCE195_SDK_* << 8. */
extern "C" unsigned lce_sdk_status(void) {
    return __atomic_load_n(&g_boot_state, __ATOMIC_ACQUIRE) | (lce195_backend_status(&g_backend) << 8);
}

/* Event entry for future hooks (tick/use). No-op until the backend advertises them. */
extern "C" int lce_sdk_dispatch(int ev, const void *payload) {
    return g_loader ? lce_loader_dispatch(g_loader, (lce_event)ev, payload) : 0;
}
