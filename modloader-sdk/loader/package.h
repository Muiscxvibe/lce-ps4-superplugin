/* Parser and validator for mod.lcm (format 1). Pure, allocation-free: the
 * view points into the caller's buffer, which must outlive the view. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "../include/lce_mod_api.h"
#include "../include/lce_mod_package.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const uint8_t *data;
    uint32_t size;
    const lce_pkg_header *header;
    const char *strs; uint32_t strs_size;
    const uint8_t *blob; uint32_t blob_size;
    const lce_pkg_meta *meta;
    const lce_pkg_tex *tex; uint32_t tex_count;
    const lce_pkg_item *items; uint32_t item_count;
    const lce_pkg_block *blocks; uint32_t block_count;
    const lce_pkg_recipe *recipes; uint32_t recipe_count;
    const lce_pkg_smelt *smelts; uint32_t smelt_count;
    const lce_pkg_tab *tabs; uint32_t tab_count;
    const lce_pkg_lang *lang; uint32_t lang_count;
    const lce_pkg_entity *entities; uint32_t entity_count;
    const lce_pkg_ore *ores; uint32_t ore_count;
    const lce_pkg_dep *deps; uint32_t dep_count;
    /* API 1.1 */
    const lce_pkg_script *scripts; uint32_t script_count;
    const uint32_t *ext; uint32_t ext_words, ext_count;   /* raw EXT section */
    const uint32_t *arrs; uint32_t arr_words;
    const lce_pkg_item2 *items2; uint32_t item2_count;
    const lce_pkg_block2 *blocks2; uint32_t block2_count;
    const lce_pkg_block3 *blocks3; uint32_t block3_count;   /* API 1.7 */
    const lce_pkg_block4 *blocks4; uint32_t block4_count;   /* API 1.11 */
    const lce_pkg_item3 *items3; uint32_t item3_count;      /* API 1.9 */
} lce_package_view;

/* Validates structure, bounds, hash, strings and api/target compatibility.
 * On failure writes a short reason into err (may be NULL). */
lce_status lce_package_parse(const uint8_t *data, size_t size, lce_package_view *out, char *err, size_t err_cap);

/* String at a reference; only valid after a successful parse (all refs checked). */
const char *lce_package_str(const lce_package_view *v, lce_pkg_str ref);

/* Replays every record through the API table, in dependency-safe order:
 * textures, tabs, blocks, items, recipes, smelting, strings, entities, ores.
 * Stops at the first error and returns it. */
lce_status lce_package_register(const lce_package_view *v, const lce_api *api, lce_mod_ctx *ctx, char *err, size_t err_cap);

/* Walk/validate EXT records; with build, construct descriptors and call build per record. */
lce_status lce_package_ext_walk(const lce_package_view *v, lce_status (*build)(void *, uint32_t, const void *), void *user, uint32_t *count);

/* Validates a mod id: [a-z0-9_], 2..31 chars, not a reserved /data/lce-mods folder
 * (minecraft, packs, lce, reservations, worlds, downloads, maps, updates, cache, staged). */
int lce_valid_modid(const char *id);
/* Validates a content key "<modid>:<path>", path [a-z0-9_./-], total < LCE_MAX_KEY. */
int lce_valid_key(const char *key);

#ifdef __cplusplus
}
#endif
