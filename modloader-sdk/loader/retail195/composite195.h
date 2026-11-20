/* Composite retail195 backend + startup phase dispatcher (CUSA00265 1.95).
 *
 * One lce_backend ("retail195") whose capabilities, check_* hooks and
 * prepare/publish delegate to the host-tested modules, so the core accepts
 * exactly what those modules can publish:
 *
 *   items, tools, armour, food, recipes ABI 2, smelting, brewing  backend195 + modules/items195
 *   blocks (OreBlock clones, BlockItems, drops, fire odds)        modules/blocks195
 *   entities (cloned mob types, spawns, eggs, renderers)          modules/entities195
 *   worldgen features (ores, trees, lakes, structures)            modules/worldgen195
 *   events, services, commands, keybinds, localisation, sounds    modules/events195 (+ services_glue195.cpp)
 *   particles (tinted clones, data only)                          modules/ui195
 *   scripts (sandboxed Lua 5.4.8, one VM per mod)                 modules/script
 *
 *   strings (mod string ids in the live Localizer map)            modules/strings195
 *   status effects + potion types (splash/lingering/arrows)       modules/effects195
 *   enchantments (stock-class clones or behaviour-less)           modules/enchants195
 *   biomes (PlainsBiome objects, BiomeInitLayer pools, colours)   modules/biomes195
 *   custom audio (SoundEvents + Miles samples via the engine)     modules/audio195
 *
 * Refused with the module's reason: unsupported block entity layouts, nether features,
 * flowers, custom armour layers, custom mob skins, mob drops, new effect icon
 * art, nether/end biomes, identity-only effect/enchantment clones.
 *
 * Nothing here patches .text. The patched shipping code calls the phase
 * functions below at existing, console-proven sites, in this order:
 *
 *   phase                         caller (integration/shipping-plugin.patch)          thread
 *   1 boot/configure              super.cpp -> lce_sdk_boot()                         startup
 *   2 block registry data hook    super.cpp -> lce_sdk_blocks_hook() (retry while 0)  startup
 *   3 block phase                 game: relay at registry vptr +0x28, id 365          game init
 *   4 items window                tools_runtime after_items -> provider collect()     game init
 *   5 after items                 registry_trace items() -> lce_sdk_after_items()     game init
 *   6 crafting pre-cache          content_process.cpp -> lce195x_recipe_providers_publish
 *   7 late startup (title)        super.cpp ui_tick -> lce_sdk_ui_tick(): furnace,     main UI
 *                                 brewing, entity types (after 0x28dc10), spawns,
 *                                 worldgen prepare+publish, renderers
 *   8 world entry gate            lce_sdk_world_entry_allowed() (0 if entity types
 *                                 published without renderers)
 *   9 server tick                 Mc195SharedScope subscriber (events195)             server
 *  10 scripts                     loader REGISTER (lce_loader_set_script_runner) and
 *                                 the event sink (lce_script_event per live mod)
 *
 * Every phase returns 1 done (or nothing to do), 0 clean refusal before any
 * game-visible write, -1 partial/uncertain (keep resident, never retry). */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "backend195.h"
#include "../loader.h"
#include "../../modules/blocks195/blocks195.h"
#include "../../modules/entities195/entities195.h"
#include "../../modules/worldgen195/worldgen195.h"
#include "../../modules/strings195/strings195.h"
#include "../../modules/enchants195/enchants195.h"
#include "../../modules/effects195/effects195.h"
#include "../../modules/biomes195/biomes195.h"
#include "../../modules/audio195/audio195.h"
#include "../../modules/projectiles195/projectiles195.h"
#include "../../modules/fuel195/fuel195.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LCE195C_MAX_FEATURES 32u
#define LCE195C_MAX_STRUCTURES 16u
#define LCE195C_MAX_SCRIPTS 32u
#define LCE195C_RVA_MINECRAFT 0x1464780u   /* Minecraft* (shared_terrain_195/runtime.cpp reads +0x1f0 textures, +0x338 running) */
#define LCE195C_MC_DISPATCHER 0x220u        /* EntityRenderDispatcher* (set at 0xc72b0f; entities195 audit) */

typedef struct lce195c_bindings {
    uintptr_t base;                          /* attested image base */
    lce195_natives core; lce195_seams seams; lce195_memory mem;   /* backend195 */
    const fuel195_ops *fuel;                 /* NULL: custom fuel refused */
    const lce195x_natives *items;            /* full items195 table, NULL: generic items only */
    const b195_natives *blocks; b195_memory blocks_mem;           /* NULL: no blocks */
    const le195_natives *entities; le195_memory entities_mem;     /* NULL: no entities */
    const lce_wg195_natives *worldgen; lce_wg195_memory worldgen_mem; /* NULL: no worldgen */
    /* API 1.1 registries (NULL / 0: refused like before) */
    s195_env strings;                        /* strings195 + enchants195 env: base/ctx/read/write/call; held is the composite's */
    uint32_t strings_bound;                  /* 1: strings.read/write/call are bound */
    const lce195e_env *effects;              /* effects195 natives + thunks (lce195e_natives_bind); memory comes from mem */
    const lce_b195_natives *biomes; lce_b195_memory biomes_mem;   /* NULL: no biomes */
    const a195_natives *audio; a195_memory audio_mem;             /* NULL: clone sounds only */
    const p195_natives *projectiles; p195_memory projectiles_mem; /* NULL: throwable items refused (API 1.9) */
    uint32_t event_caps;                     /* lce195_ev_capabilities() after lce195_ev_init, 0 = no events */
    uint32_t event_caps2;                    /* lce195_ev_capabilities2() (API 1.2 events: block use, payload context) */
    uint32_t services;                       /* 1: services195 will be filled (LCE_CAP_SERVICES) */
    uint32_t scripts;                        /* 1: the Lua runtime is linked */
    void *host;
    uintptr_t (*renderer_dispatcher)(void *host);   /* *(Minecraft+0x220) or 0 */
    int (*no_level)(void *host);             /* 1 while no world is loaded/being created */
    uint64_t (*thread_id)(void *host);
    void (*log)(const char *line);
} lce195c_bindings;

enum { LCE195C_PH_BOOT = 0, LCE195C_PH_BLOCK_HOOK, LCE195C_PH_BLOCKS, LCE195C_PH_ITEMS, LCE195C_PH_AFTER_ITEMS, LCE195C_PH_RECIPES,
       LCE195C_PH_SMELT_BREW, LCE195C_PH_ENTITY_TYPES, LCE195C_PH_SPAWNS, LCE195C_PH_WORLDGEN, LCE195C_PH_RENDERERS,
       LCE195C_PH_EFFECTS, LCE195C_PH_STRINGS, LCE195C_PH_POTIONS, LCE195C_PH_ENCHANTS, LCE195C_PH_BIOMES, LCE195C_PH_AUDIO,
       LCE195C_PH_PROJECTILES, LCE195C_PH_FUEL, LCE195C_PH_COUNT };
#define LCE195C_MAX_SIDS 512u
#define LCE195C_MAX_TEXTS 2048u
/* per phase: 0 not run, 1 done, 2 nothing to do, 3 refused, 4 uncertain, 5 waiting (retry) */
enum { LCE195C_NOT_RUN = 0, LCE195C_DONE = 1, LCE195C_NONE = 2, LCE195C_REFUSED = 3, LCE195C_UNCERTAIN = 4, LCE195C_WAITING = 5 };

typedef struct lce195c_feature {
    lce_wg195_feature f;
    uint16_t trunk_id, leaves_id, lake_id;   /* resolved at late phase (Block+0x190 states / Block*) */
    int32_t structure;                       /* index into structures, -1 */
} lce195c_feature;

typedef struct lce195c_script { void *vm; uint32_t mod; } lce195c_script;

typedef struct lce195c {
    lce_backend iface;
    lce195c_bindings bind;
    lce195_backend items;
    lce_loader *loader;
    const lce_plan *plan;
    uint32_t state;                          /* 0 idle, 1 prepared, 2 armed, 3 failed */
    uint8_t phase[LCE195C_PH_COUNT];
    uint32_t items_window;                   /* 1 while lce195c_phase_after_items runs (blocks items_held) */
    uint32_t block_items_publication;        /* 0 pending, 1 complete, 2 failed. Creative readers acquire this. */
    uint64_t items_thread;
    uint32_t late_held;                      /* 1 while lce195c_phase_late runs */
    uint32_t world_entry;                    /* 1 allowed (default), 0 blocked */
    uint32_t fuel_count;                     /* validated declarations awaiting late startup */
    uint32_t fuel_query_ready;               /* release-published after native fuel activation */
    uint32_t has_blocks, has_entities, feature_count, structure_count;
    lce195c_feature features[LCE195C_MAX_FEATURES];
    lce_wg195_structure structures[LCE195C_MAX_STRUCTURES];
    uint16_t palette[LCE195C_MAX_STRUCTURES][256];
    uint32_t block_routes, block_item_routes;
    lce195c_script scripts[LCE195C_MAX_SCRIPTS]; uint32_t script_count;
    /* API 1.1 registries */
    uint32_t sid_count, text_count, strings_inserted, strings_failed, effect_window, ui_held;
    s195_entry sids[LCE195C_MAX_SIDS];
    s195_text texts[LCE195C_MAX_TEXTS];
    uint16_t *text_pool; uint32_t text_pool_used, text_pool_cap;
    uint32_t net_channels, net_client_installed;   /* API 1.6 */
    uint32_t effect_count, potion_count, enchant_count, biome_count, sound_pcm_count;
    uint32_t potion_gate;                   /* 0 Creative open, 1 Creative lease, 2 title owns publication */
    void *creative_potion_ctx;
    int (*creative_potion_held)(void *);
    lce195e_env effects_env;
    lce195e_effect_spec effect_specs[LCE195E_MAX_EFFECTS];
    const lce_plan_ext *potion_ext[LCE195E_MAX_POTIONS]; uint32_t potion_sid[LCE195E_MAX_POTIONS][4];
    e195_spec enchant_specs[E195_MAX]; e195_record enchant_records[E195_MAX];
    a195_module audio;
    /* API 1.9 throwables (projectiles195): kind k = proj_desc[k] / proj_item_id[k] */
    uint32_t proj_count, proj_hits, proj_rng;
    const lce_item_desc *proj_desc[P195_MAX_KINDS];
    uint32_t proj_item_id[P195_MAX_KINDS];
    char last_error[192];
} lce195c;

/* One composite per process. bind is copied. Capabilities are final after this. */
void lce195c_init(lce195c *c, const lce195c_bindings *bind);
uint32_t lce195c_capabilities(const lce195c *c);
/* After lce_loader_create(): remember the loader (event sink, script runner, services). */
void lce195c_attach(lce195c *c, lce_loader *l);

/* ---- phase dispatcher (see the table above) ---- */
int lce195c_phase_blocks_hook(lce195c *c);      /* 1 armed / nothing, 0 retry later (registry not built) or refused, -1 */
int lce195c_phase_after_items(lce195c *c);      /* startup thread, after the item window */
int lce195c_creative_blocks_wanted(const lce195c *c);
int lce195c_creative_block_entries(const lce195c *c, struct lce_sdk_creative_block_entry *out, uint32_t capacity);
const char *lce195c_creative_block_key(const lce195c *c, uintptr_t item, uint32_t item_id);
struct lce_sdk_creative_potion_entry;
int lce195c_creative_potions_wanted(const lce195c *c);
int lce195c_creative_potion_entries(lce195c *c, uintptr_t base, void *ctx, int (*held)(void *),
                                  struct lce_sdk_creative_potion_entry *out, uint32_t entry_capacity, uint32_t row_capacity);
int lce195c_phase_late(lce195c *c);             /* main thread at the title, no Level; runs once */
int lce195c_world_entry_allowed(const lce195c *c);
uint32_t lce195c_phase_state(const lce195c *c, uint32_t phase);
/* Every UI tick at the title or in a world (main UI thread): keeps mod string ids present in the
 * Localizer (rebuilt on a language change). Cheap when nothing is missing. */
void lce195c_ui_tick_strings(lce195c *c);
/* API 1.6: retry the client packet hook (cheap no-op once installed or without channels). Any thread. */
void lce195c_net_tick(lce195c *c);
/* Mod effect id/object (for services/scripts): -1 / 0 when unknown. */
int32_t lce195c_effect_id(const char *key);
/* "phase=after_items rc=1 ..." style one-line summary. */
void lce195c_status_line(const lce195c *c, char *out, uint32_t cap);

/* ---- terrain atlas (gap G1) ----
 * shared_terrain_195/runtime.cpp appends the SDK block tiles after its two fixed tiles (first_index 2) and calls
 * bind/detach inside its reload lease; `held`/`uploaded` are that runtime's own attestations and are used only
 * during the call. A second bind or detach while one is in progress is refused.
 * bind: 1 bound, 0 refused before any write (faces keep the stock fallback), -1 uncertain.
 * detach: 1 detached or nothing bound, 0/-1 refused or uncertain (fails the reload). */
typedef struct lce195c_terrain_lease {
    int (*held)(void);
    int (*uploaded)(uintptr_t atlas, uintptr_t texture, uint32_t generation);
} lce195c_terrain_lease;
uint32_t lce195c_terrain_tiles(const lce195c *c);
int lce195c_terrain_compose(const lce195c *c, uint32_t tile_size, unsigned mip, uint8_t *out, size_t bytes);
int lce195c_terrain_bind(lce195c *c, const lce195c_terrain_lease *l, uintptr_t atlas, uint32_t generation, uint32_t width,
                         uint32_t height, uint32_t stock_height, uint32_t tile_size, uint32_t first_index);
int lce195c_terrain_detach(lce195c *c, const lce195c_terrain_lease *l);

/* ---- events / scripts ---- */
int lce195c_event_sink(void *composite, lce_event ev, const void *payload);   /* lce195_sink */
lce_status lce195c_script_runner(void *composite, lce_loader *l, uint32_t mod_index, const lce_mod_script *script, char *err, uint32_t cap);
uint32_t lce195c_script_count(const lce195c *c);

/* ---- module glue implemented in services_glue195.cpp (events195 headers) ---- */
/* Register plan sounds (clones), commands, keybinds and strings with events195/services195. 1 ok. */
int lce195g_register_plan(const lce_plan *plan, char *err, uint32_t cap);
/* backend check for LCE_EXT_SOUND: vanilla clone only (PCM refused, gap G11). */
lce_status lce195g_check_sound(const lce_sound_desc *d, char *err, uint32_t cap);
/* Install services195 into the loader's API (console: natives from pinned RVAs, no world ops yet). */
int lce195g_services_install(lce_loader *l, uintptr_t base, const void *world_ops /* lce195_world_ops* or NULL */);

#ifdef LCE195C_HOST_TEST
/* Test seam: replace a module phase call (phase id) with a fake; NULL restores. */
typedef int (*lce195c_phase_fn)(void *module, uintptr_t arg);
void lce195c_test_override(uint32_t phase, lce195c_phase_fn fn);
const b195_module *g_comp_blocks_for_tests(void);
const p195_module *g_comp_projectiles_for_tests(void);
#endif

#ifdef __cplusplus
}
#endif
