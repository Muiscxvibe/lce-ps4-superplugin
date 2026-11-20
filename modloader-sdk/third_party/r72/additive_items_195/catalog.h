#ifndef LCE_ADDITIVE_ITEMS_195_CATALOG_H
#define LCE_ADDITIVE_ITEMS_195_CATALOG_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define LCE195_ADDITIVE_ABI 1u
#define LCE195_ADDITIVE_CAPACITY 64u
enum lce195_item_kind { LCE195_MATERIAL=1,LCE195_TOOL=2,LCE195_ARMOUR=3,LCE195_CONSUMABLE=4 };
/* Descriptors and their strings remain alive until process exit. A provider
 * constructs its own native Item subclasses; this interface never turns a
 * base Item into a tool or BlockItem by changing classification fields. */
typedef struct lce195_additive_item {
 uint32_t abi, size;
 const char *provider;       /* stable Mods choice ID */
 const char *key;            /* native namespace:path, ASCII */
 const char *display_name;   /* authored, nonlocalized fallback */
 const char *icon;           /* distinct atlas name, not a stock alias */
 uint32_t id, max_stack, kind;
 uintptr_t item;
} lce195_additive_item;
typedef struct lce195_item_catalog {
 uint32_t state, count;      /* 0 collecting, 1 frozen, 2 rejected */
 lce195_additive_item entries[LCE195_ADDITIVE_CAPACITY];
} lce195_item_catalog;
/* Mutation is startup-thread-only. Rejects duplicate ID, key, item pointer,
 * invalid bounds, and IDs interpreted as legacy remapped items. No partial
 * append on failure. Failure does not erase an existing catalog. */
int lce195_catalog_add(lce195_item_catalog *,const lce195_additive_item *);
int lce195_catalog_freeze(lce195_item_catalog *);
const lce195_additive_item *lce195_catalog_at(const lce195_item_catalog *,unsigned);
size_t lce195_catalog_count(const lce195_item_catalog *);
#ifdef __cplusplus
}
#endif
#endif
