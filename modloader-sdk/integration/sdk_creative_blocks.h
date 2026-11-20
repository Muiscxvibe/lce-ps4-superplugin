#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Shipping plugin bridge. This does not change the public mod API or the
 * mapped BlockItem descriptor shared with r72. */
#define LCE_SDK_CREATIVE_BLOCK_CAPACITY 304u

typedef struct lce_sdk_creative_block_entry {
    uintptr_t item;
    uint32_t item_id;
    uint32_t aux;
    const char *key;
} lce_sdk_creative_block_entry;

/* Boot-time predicate for a committed plan with at least one BlockItem. */
int lce_sdk_creative_blocks_wanted(void);

/* Live rows in registration order, with ascending aux per item. NULL, 0
 * queries the count. Return 0 if inactive, or -1 for a failed publication,
 * invalid arguments, insufficient space or lost identity. No writes on error. */
int lce_sdk_creative_block_entries(lce_sdk_creative_block_entry *out, uint32_t capacity);

/* Return the process-lifetime key of a live SDK BlockItem. Supply a pointer,
 * a mapped numeric item ID, or both. Both must match when given. */
const char *lce_sdk_creative_block_key(uintptr_t item, uint32_t item_id);

#ifdef __cplusplus
}
#endif
