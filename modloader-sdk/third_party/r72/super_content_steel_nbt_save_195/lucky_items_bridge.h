#pragma once
#include "../additive_items_195/window.h"
#include "../additive_items_195/catalog.h"
#ifdef __cplusplus
extern "C" {
#endif
#define MC195_ITEMS_PROVIDER_CAPACITY 16u
enum mc195_items_failure {
 MC195_ITEMS_FAILURE_NONE=0, MC195_ITEMS_FAILURE_COLLECT=1,
 MC195_ITEMS_FAILURE_DESCRIPTOR=2, MC195_ITEMS_FAILURE_ICON_OWNER=3,
 MC195_ITEMS_FAILURE_ICON_COLLISION=4
};
/* Process-lifetime providers, registered before preparation begins.
 * prepare runs on the storage worker; zero means a clean refusal, negative an uncertain failure. collect runs in the held item window.
 * identity must verify each private vtable remains installed, and icon must
 * supply readable process-lifetime 16x16 RGBA only for names it owns. Atlas
 * names must be unique across the combined catalog and all providers. A provider may decline all
 * registration until its real behavior is ready, or explicitly expose a
 * diagnostic item when that behavior is intentionally disabled. */
typedef struct mc195_items_provider {
 void *opaque;
 int (*prepare)(void *,uintptr_t);
 int (*collect)(void *,lce195_items_window *,lce195_item_catalog *);
 int (*reserved)(void *,const char *,uint32_t,const char *);
 int (*identity)(void *);
 const uint8_t *(*icon)(void *,const char *);
} mc195_items_provider;
int mc195_custom_items_configure(const mc195_items_provider *,unsigned);
int mc195_custom_items_add_providers(const mc195_items_provider *,unsigned);
int mc195_custom_items_add_provider(const mc195_items_provider *);
int mc195_custom_items_prepare(uintptr_t);
int mc195_custom_items_collect(lce195_items_window *,lce195_item_catalog *);
int mc195_custom_items_reserved(const char *,uint32_t,const char *);
int mc195_custom_items_identity(void);
const uint8_t *mc195_custom_items_icon(const char *);
unsigned mc195_custom_items_count(void);
unsigned mc195_custom_items_entry_count(void);
unsigned mc195_custom_items_status(void);
/* First collection failure after startup, with provider index in the retained
 * post-prepare list. Returns NONE while no collection failure is recorded. */
unsigned mc195_custom_items_failure(unsigned *provider_index);
#ifdef __cplusplus
}
#endif
