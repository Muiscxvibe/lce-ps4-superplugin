/* Persistent numeric-ID ledger.
 *
 * Worlds store numeric item/block IDs, so a key must map to the same ID on
 * every launch, forever. The ledger is the single authority:
 *   - entries are never removed (an uninstalled mod's IDs stay reserved);
 *   - allocation only ever picks IDs inside the mod range that are free in
 *     the ledger, not reserved by other providers, and not occupied in the
 *     live game registry (checked through a callback);
 *   - a block ID B also consumes its block-item ID (B < 256 ? B : B + 768,
 *     retail 1.95 rule, see API_STATUS.md), so items and blocks never alias;
 *   - an unreadable/corrupt ledger is fatal (LCE_E_LEDGER): the loader then
 *     registers nothing, rather than re-allocating and corrupting worlds.
 *
 * Text format (UTF-8, '\n' line endings):
 *   LCEIDS 1
 *   item examplemod:ruby 2300
 *   block examplemod:ruby_block 400
 *   effect examplemod:glow 40         (also enchant / biome / potion)
 *   sha256 <64 hex of all preceding bytes>
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "../include/lce_mod_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LCE_LEDGER_MAX 2048
#define LCE_LEDGER_MAX_RESERVED 64

typedef enum { LCE_KIND_ITEM = 0, LCE_KIND_BLOCK = 1,
               /* API 1.1 registries with numeric ids stored in saves/NBT (one id space each, no aliasing) */
               LCE_KIND_EFFECT = 2, LCE_KIND_ENCHANT = 3, LCE_KIND_BIOME = 4, LCE_KIND_POTION = 5, LCE_KIND_COUNT = 6 } lce_id_kind;
#define LCE_KIND_EXT_FIRST LCE_KIND_EFFECT
#define LCE_EXT_KINDS (LCE_KIND_COUNT - LCE_KIND_EXT_FIRST)
#define LCE_EXT_RANGES 2

typedef struct {
    uint32_t item_min, item_max;      /* inclusive mod item range */
    uint32_t block_min, block_max;    /* inclusive mod block range */
    /* IDs owned by other providers (e.g. the bundled Steel/Lucky components). */
    uint32_t reserved_items[LCE_LEDGER_MAX_RESERVED]; uint32_t reserved_item_count;
    uint32_t reserved_blocks[LCE_LEDGER_MAX_RESERVED]; uint32_t reserved_block_count;
    /* If nonzero, blocks are allocated ONLY from this ascending allowlist
     * (IDs observed empty in the live registry); block_min/max still bound it. */
    uint32_t block_allow[LCE_LEDGER_MAX_RESERVED]; uint32_t block_allow_count;
    /* Live game occupancy; return nonzero if the ID is already taken in the game. May be NULL. */
    int (*item_in_use)(void *user, uint32_t id);
    int (*block_in_use)(void *user, uint32_t id);
    void *user;
    /* Ext kinds (effect/enchant/biome/potion): up to 2 inclusive ranges each, tried in order;
     * {0,0} = unused range. A kind with no range cannot allocate (LCE_E_FULL). */
    uint32_t ext_range[LCE_EXT_KINDS][LCE_EXT_RANGES][2];
    int (*ext_in_use)(void *user, uint32_t kind, uint32_t id);   /* live game occupancy, may be NULL */
} lce_id_space;

typedef struct {
    uint8_t kind;          /* lce_id_kind */
    uint8_t live;          /* claimed by a loaded mod this session */
    uint16_t reserved;
    uint32_t id;
    char key[LCE_MAX_KEY];
} lce_ledger_entry;

typedef struct {
    lce_ledger_entry entries[LCE_LEDGER_MAX];
    uint32_t count;
    uint32_t dirty;
} lce_ledger;

uint32_t lce_block_item_id(uint32_t block_id);

void lce_ledger_init(lce_ledger *l);
/* Parses and hash-checks ledger text. Empty input (len 0) = fresh ledger. */
lce_status lce_ledger_load(lce_ledger *l, const char *text, size_t len);
/* Writes the canonical text including the sha256 trailer. */
lce_status lce_ledger_serialize(const lce_ledger *l, char *out, size_t cap, size_t *out_len);
/* Existing ID for key or -1. */
int64_t lce_ledger_find(const lce_ledger *l, lce_id_kind kind, const char *key);
/* Clears every entry's live flag (start of a claim round). */
void lce_ledger_reset_live(lce_ledger *l);
/* Returns existing or newly allocated ID; validates against space. Marks live. */
lce_status lce_ledger_claim(lce_ledger *l, const lce_id_space *space, lce_id_kind kind, const char *key, uint32_t *out_id);

#ifdef __cplusplus
}
#endif
