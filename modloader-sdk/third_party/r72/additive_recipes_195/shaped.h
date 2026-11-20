#ifndef LCE195_ADDITIVE_SHAPED_RECIPE_H
#define LCE195_ADDITIVE_SHAPED_RECIPE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define LCE195_SHAPED_ABI 1u
#define LCE195_SHAPED_MAX 32u
typedef struct lce195_recipe_ingredient {
 uint32_t id; uintptr_t item; /* zero id/item means empty cell */
} lce195_recipe_ingredient;
typedef struct lce195_shaped_recipe {
 uint32_t abi,size;
 const char *provider,*key;
 uint32_t output_id,output_aux,width,height,classification,group,category;
 uintptr_t output;
 lce195_recipe_ingredient cells[9]; /* cropped row-major; ingredients match any aux */
} lce195_shaped_recipe;
typedef struct lce195_shaped_publisher {
 uintptr_t base,owner;void *context;
 int (*held)(void *);
 int (*read)(void *,uintptr_t,void *,size_t);
 int (*write)(void *,uintptr_t,const void *,size_t);
 int (*owned)(void *,const lce195_shaped_recipe *);
 unsigned state,count,published;
 uintptr_t output_stacks[LCE195_SHAPED_MAX],inputs[LCE195_SHAPED_MAX][9],cells[LCE195_SHAPED_MAX],recipes[LCE195_SHAPED_MAX],maps[LCE195_SHAPED_MAX];
 uintptr_t stock[8192];
} lce195_shaped_publisher;
/* One batch per owner on the authenticated native pre-cache crafting thread.
 * Descriptors and native objects remain alive for process lifetime. Output
 * keys must be unique; all ingredient pointers round-trip through registry.
 * Returns 1 published, 0 refused before allocation, -1 after allocation or
 * publication may have begun. Failed partial batches are never retried. */
int lce195_shaped_publish(lce195_shaped_publisher *,const lce195_shaped_recipe *,unsigned);
/* Register process-lifetime native recipe providers before the crafting hook
 * is installed. The loader copies IDs (1–63 ASCII lowercase letters, digits,
 * dot, underscore or hyphen). Context and callbacks stay live until exit.
 * Publication runs once, in registration order, inside the actual pre-cache
 * owner callback. A provider must independently attest its native owner and
 * retain native allocations. It must publish at least one shared recipe or
 * return a refusal. */
typedef int (*lce195_recipe_provider_fn)(void *,uintptr_t owner);
int lce195_recipe_provider_register(const char *provider,void *,lce195_recipe_provider_fn);
int lce195_recipe_providers_publish(uintptr_t owner);
unsigned lce195_recipe_provider_status(const char *provider);
/* Snapshot successful, retained shaped recipes for the native Mods crafting
 * view. Capacity must accommodate every entry: never return a partial list. */
unsigned lce195_recipe_snapshot(uintptr_t owner,uintptr_t *recipes,unsigned *categories,unsigned capacity);
#ifdef __cplusplus
}
#endif
#endif
