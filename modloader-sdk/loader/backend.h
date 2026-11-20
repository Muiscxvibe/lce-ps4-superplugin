/* Backend interface: the only layer that touches game memory.
 *
 * The core (registry.cpp) validates and resolves everything first and hands
 * the backend one immutable plan with numeric IDs already allocated. A backend
 * must:
 *   prepare(): check every pin/signature/capacity the plan needs WITHOUT
 *              mutating the game. Return LCE_OK only if publish() can apply
 *              the whole plan. Any error => nothing is published.
 *   publish(): apply the plan. Return LCE_OK (complete), or an error. If it
 *              fails after the first game write it must set *partial = 1;
 *              the loader then reports PARTIAL and keeps all IDs reserved.
 * Backends: retail195 (PS4, loader/retail195/), mock (tests/mock_backend.cpp). */
#pragma once
#include <stdint.h>
#include "../include/lce_mod_api.h"
#include "ext.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *key;                 /* "modid:ruby" */
    uint8_t atlas;                   /* 0 items, 1 terrain */
    const uint8_t *rgba;             /* 16*16*4 */
    int32_t slot;                    /* filled by backend in prepare(): atlas cell, -1 unset */
} lce_plan_texture;

typedef struct {
    const lce_item_desc *desc;       /* strings inside stay valid for the process lifetime */
    uint32_t id;                     /* numeric item ID (ledger) */
    int32_t texture;                 /* index into plan textures */
    int32_t creative_group;          /* backend group index from resolve_group, or -1 = none */
    int32_t mod_tab;                 /* index into plan tabs, or -1 */
    uint8_t placeholder;             /* 1: orphaned ledger ID kept alive with a placeholder */
} lce_plan_item;

typedef struct {
    const lce_block_desc *desc;
    uint32_t id;                     /* numeric block ID */
    uint32_t item_id;                /* block-item ID (lce_block_item_id), 0 if NO_ITEM */
    int32_t texture[6];              /* plan texture indices per face */
    int32_t drop_item;               /* numeric item ID dropped */
    int32_t creative_group;
    int32_t mod_tab;
    uint8_t placeholder;
} lce_plan_block;

typedef struct { int32_t id; int16_t aux; } lce_plan_ingredient;   /* id -1 = empty */

typedef struct {
    const lce_recipe_desc *desc;
    lce_plan_ingredient cells[9];
    int32_t result_id;
    int32_t craft_group;             /* backend crafting group, -1 = mod tab */
    int32_t mod_tab;
} lce_plan_recipe;

typedef struct {
    const lce_smelt_desc *desc;
    lce_plan_ingredient input;
    int32_t result_id;
} lce_plan_smelt;

typedef struct {
    const lce_tab_desc *desc;
    int32_t icon_item_id;
} lce_plan_tab;

typedef struct {
    const char *locale, *key, *value;
    int32_t item_id;                 /* name of this item ID if the key is a content key, else -1 */
} lce_plan_string;

typedef struct {
    lce_plan_texture *textures; uint32_t texture_count;
    lce_plan_tab *tabs; uint32_t tab_count;
    lce_plan_block *blocks; uint32_t block_count;
    lce_plan_item *items; uint32_t item_count;
    lce_plan_recipe *recipes; uint32_t recipe_count;
    lce_plan_smelt *smelts; uint32_t smelt_count;
    lce_plan_string *strings; uint32_t string_count;
    /* API 1.1 content (effects, potions, brewing, enchantments, fuel, entities,
     * features, biomes, structures, sounds, particles, commands, keybinds). */
    lce_plan_ext *ext; uint32_t ext_count;
    lce_resolve_fn resolve; const void *resolve_ctx;
} lce_plan;

typedef struct lce_backend {
    const char *name;
    uint32_t capabilities;           /* LCE_CAP_* */
    void *self;
    /* Group key ("minecraft:materials") -> backend index; crafting=1 for crafting groups. -1 unknown. */
    int32_t (*resolve_group)(void *self, const char *key, int crafting);
    /* Optional per-content checks run during validation: content the backend
     * cannot represent faithfully drops only the owning mod, with the reason. */
    lce_status (*check_item)(void *self, const lce_item_desc *d, char *err, uint32_t err_cap);
    lce_status (*check_recipe)(void *self, const lce_recipe_desc *d, char *err, uint32_t err_cap);
    lce_status (*check_block)(void *self, const lce_block_desc *d, char *err, uint32_t err_cap);
    lce_status (*check_ext)(void *self, uint32_t type, const void *desc, char *err, uint32_t err_cap);
    lce_status (*prepare)(void *self, lce_plan *plan, char *err, uint32_t err_cap);
    lce_status (*publish)(void *self, const lce_plan *plan, int *partial, char *err, uint32_t err_cap);
    uint32_t capabilities2;          /* LCE_CAP2_* event sources (API 1.2); service bits come with lce_loader_set_services */
    /* API 1.4: a live mod's network channel, called once per channel after a successful publish
     * (before any world). NULL = channels unsupported. 1 ok. */
    int (*register_channel)(void *self, const char *channel);
} lce_backend;

#ifdef __cplusplus
}
#endif
