/* Retail CUSA00265 1.95 backend: items, recipes, smelting and brewing.
 *
 * This is the item/recipe component of the composite retail195 backend
 * (composite195.h). It installs no hook and patches no code. It adapts the SDK
 * plan onto existing process-lifetime seams of the shipping content stack
 * (r72-session-candidate/loader) and onto modules/items195:
 *
 *   items   -> mc195_items_provider via mc195_custom_items_add_provider()
 *              (lucky_items_bridge.h). collect() runs inside the held
 *              post-stock-items window and builds EVERY item kind with the
 *              items195 constructors (lce195x_item_construct: generic 0x354d80,
 *              pickaxe 0x52bd50, axe 0x3e3c0, shovel 0x60b060, sword 0x6cefd0,
 *              hoe inline, armour 0x32cd0, food 0x2d69d0). The existing
 *              lce195_native_items_publish then registers the catalog (0x3af660
 *              into Item::items at base+0x1417720); the existing atlas growth
 *              binds our 16x16 icons; entries appear in the private Mods
 *              Creative group (console-verified for Steel:
 *              mc195-steel-nbt-live-bx8w0hrq).
 *   names   -> each item's private vtable copy (items195) gets slot +0x108
 *              (stock 0x3af070) = lce195_sdk_name_dispatch.
 *   recipes -> ABI 2: lce195x_recipe_provider_register("lce-sdk") +
 *              lce195x_recipes_publish (shaped 0x6013d0, shapeless 0x602e10,
 *              count/aux results, aux-specific inputs) inside the pre-cache
 *              crafting owner callback (owner global base+0x142f278, cache
 *              builder call 0x5a9ef1 -> 0x5aa200). Needs the one-line r72 change
 *              in content_process.cpp (integration/shipping-plugin.patch).
 *   smelt   -> lce195x_furnace_publish on FurnaceRecipes [base+0x1416a80]
 *   brewing -> lce195x_brew_publish on the PotionBrewing mix vector 0x142e948
 *              (both from the late startup phase, see composite195.h).
 *
 * Capabilities are derived from what is bound: a class constructor that is
 * not bound never becomes a capability, so the loader drops the owning mod
 * with a reason instead of the backend failing later. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "../backend.h"
#include "../../modules/items195/items195.h"
#include "../../third_party/r72/additive_items_195/catalog.h"
#include "../../third_party/r72/additive_items_195/window.h"
#include "../../third_party/r72/super_content_steel_nbt_save_195/lucky_items_bridge.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LCE195_SDK_PROVIDER "lce-sdk"
#define LCE195_SDK_MAX_ITEMS 32u      /* shares the 64-entry combined catalog with Steel/Lucky */
#define LCE195_SDK_MAX_RECIPES 128u   /* one ABI-2 publisher batch (LCE195X_RECIPE_MAX) */
#define LCE195_SDK_MAX_SMELTS 64u
#define LCE195_SDK_MAX_BREWS 64u
#define LCE195_SDK_NAME_UNITS 96u

/* Pinned 1.95 RVAs (image c675e6ce...029f). Every one is cited in API_STATUS.md. */
#define LCE195_RVA_ALLOC          0xe6cfd0u  /* operator new(size_t) */
#define LCE195_RVA_ITEM_CTOR      0x354d80u  /* Item::Item() base material item, 0xa0 bytes */
#define LCE195_RVA_ITEM_VTABLE    0x126e638u /* base Item primary vtable, 56 slots */
#define LCE195_RVA_ITEM_NAME      0x3af070u  /* Item vtable +0x108 name(out wstring*, item, stack) */
#define LCE195_RVA_WSTRING        0x67840u   /* wstring ctor (dst, const u16*, n) */
#define LCE195_RVA_ITEM_BY_ID     0x385af0u  /* Item* lookup(unsigned id) */
#define LCE195_RVA_FURNACE_GLOBAL 0x1416a80u /* FurnaceRecipes* (object 0xf8) */
#define LCE195_ITEM_SIZE          0xa0u
#define LCE195_ITEM_VTABLE_SLOTS  56u
#define LCE195_ITEM_NAME_SLOT     (0x108u / 8u)
#define LCE195_ITEM_CLASS_MATERIAL UINT64_C(0x000000030000001f) /* iron-ingot pair at Item+0x28 (candidate mapping, ingot_195.c) */

/* The minimal natives every build binds (generic items + names). The full
 * items195 table is bound with lce195_backend_bind_items(). */
typedef struct {
    void *(*alloc)(size_t);
    void (*item_ctor)(void *item);
    void (*wstring)(void *dst, const uint16_t *units, uint64_t n);
    uintptr_t (*item_by_id)(unsigned id);
    void *(*name_original)(void *out, void *item, void *stack);
} lce195_natives;

/* Seams of the shipping content stack / items195 registry (bound to the real
 * symbols by integration/sdk_boot.cpp; stubbed in tests). */
typedef struct {
    int (*items_add_provider)(const mc195_items_provider *);
    int (*catalog_add)(lce195_item_catalog *, const lce195_additive_item *);
    int (*recipe_provider_register)(const char *, void *, lce195x_recipe_provider_fn);   /* lce195x_recipe_provider_register */
} lce195_seams;

/* Memory access with protection checks (sceKernelQueryMemoryProtection on the
 * console). An 8-byte aligned write must be a single atomic release store. */
typedef struct {
    void *ctx;
    int (*read)(void *, uintptr_t, void *, size_t);
    int (*write)(void *, uintptr_t, const void *, size_t);
    int (*crafting_held)(void *);   /* 1 while on the crafting owner callback thread (lce-sdk provider running) */
    int (*startup_held)(void *);    /* 1 on the startup/main thread in the late phase, before any Level exists; NULL: no smelting/brewing */
} lce195_memory;

/* Optional item-use event routing (events195): called per item inside
 * collect(); route() records the stock slot, thunk is written into +0x40. */
typedef struct {
    int (*route)(uintptr_t item_object, uint32_t item_id, uintptr_t original_use);
    uintptr_t thunk;
} lce195_use_route;

enum {
    LCE195_SDK_IDLE = 0, LCE195_SDK_ARMED = 1, LCE195_SDK_ITEMS_DONE = 2, LCE195_SDK_RECIPES_DONE = 3,
    LCE195_SDK_REFUSED = 4, LCE195_SDK_UNCERTAIN = 5
};
/* phase states of smelt/brew/recipes/items: 0 not run, 1 running, LCE195_SDK_* after */

typedef struct lce195_sdk_item {
    const lce_item_desc *desc;
    uint32_t id;
    char icon[32];                 /* atlas name "lcesdk_<id>" (unique across providers) */
    const uint8_t *rgba;
    char ascii_name[96];           /* catalog display_name: printable ASCII only (catalog.c text()) */
    uint16_t name[LCE195_SDK_NAME_UNITS];
    uint32_t name_len;
    int32_t repair_index;          /* index of an SDK repair item constructed earlier, -1 none/vanilla */
    int32_t repair_vanilla;        /* vanilla repair item id, -1 none */
    int32_t effect_id;             /* stock MobEffect id of the single food effect, -1 none, -2 mod effect (key below) */
    const char *effect_key;
    uint8_t use_routed;
    lce195x_item x;                /* items195 record: object, private vtable, tier, material */
} lce195_sdk_item;

typedef struct lce195_backend {
    lce_backend iface;
    uintptr_t base;
    lce195_natives natives;
    lce195_seams seams;
    lce195_memory mem;
    lce195x_env xenv;              /* items195 environment (natives + held phase) */
    lce195_use_route use_route;
    lce195_use_route inv_route;    /* API 1.3: item +0xc8 inventoryTick route (same shape as use_route) */
    /* Mod status effects (effects195): published by pre_collect at the start of the item window,
     * so FoodItems can take them; mod_effect resolves a published effect by key (0 = unknown). */
    int (*pre_collect)(void *user);
    void *pre_user;
    uintptr_t (*mod_effect)(const char *key);
    int32_t pre_collect_rc;
    uint32_t state, items_state, recipes_state, smelt_state, brew_state;
    uint32_t held_phase;           /* 1 items window, 2 crafting, 3 late startup (thread-confined) */
    lce195_items_window *window;
    uint32_t item_count, recipe_count, smelt_count, brew_count;
    lce195_sdk_item items[LCE195_SDK_MAX_ITEMS];
    const lce_plan *plan;
    lce195x_recipe recipes[LCE195_SDK_MAX_RECIPES];
    lce195x_smelt smelts[LCE195_SDK_MAX_SMELTS];
    lce195x_brew brews[LCE195_SDK_MAX_BREWS];
    const lce_brew_desc *brew_descs[LCE195_SDK_MAX_BREWS];
    char last_error[160];
} lce195_backend;

/* Initialise; base must already be attested (runtime_attest/ps4_attest). The
 * generic item path is bound from n; call lce195_backend_bind_items() for the
 * other kinds, recipes, smelting and brewing. */
void lce195_backend_init(lce195_backend *b, uintptr_t base, const lce195_natives *n, const lce195_seams *s, const lce195_memory *m);
/* Bind the full items195 native table (NULL entries stay unbound). Recomputes capabilities. */
void lce195_backend_bind_items(lce195_backend *b, const lce195x_natives *n);
void lce195_backend_set_use_route(lce195_backend *b, const lce195_use_route *r);
/* Capability bits that the bound natives/seams really back. */
uint32_t lce195_backend_caps(const lce195_backend *b);
/* Fill natives from the pinned RVAs (console only; never call on host). */
void lce195_natives_bind(lce195_natives *n, uintptr_t base);
/* Status for logs/UI: LCE195_SDK_* (items/recipes) */
uint32_t lce195_backend_status(const lce195_backend *b);
/* Late startup phase (startup_held()==1): smelting then brewing. 1 done (or
 * nothing to do), 0 refused before any write, -1 uncertain. */
int lce195_backend_publish_late(lce195_backend *b);
/* Lookup an installed SDK potion mix. Returns a plan-owned key, or NULL. */
const char *lce195_backend_brewing_result(const lce195_backend *b, const char *input, const char *reagent, const char *form);
/* The name dispatcher installed in slot +0x108 of every SDK item's private vtable. */
void *lce195_sdk_name_dispatch(void *out, void *item, void *stack);
/* Exposed for the composite: checks (backend interface signature) and the
 * object pointer of a published SDK item (0 if not constructed). */
lce_status lce195_backend_check_item(void *self, const lce_item_desc *d, char *err, uint32_t cap);
lce_status lce195_backend_check_recipe(void *self, const lce_recipe_desc *d, char *err, uint32_t cap);
uintptr_t lce195_backend_item_object(const lce195_backend *b, uint32_t id);

#ifdef __cplusplus
}
#endif
