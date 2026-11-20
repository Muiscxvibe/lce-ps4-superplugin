#ifndef LCE_SDK_CREATIVE_POTIONS_H
#define LCE_SDK_CREATIVE_POTIONS_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LCE_SDK_CREATIVE_MAX_POTIONS 64u
enum lce_sdk_creative_potion_form {
    LCE_SDK_CREATIVE_POTION_DRINK = 1,
    LCE_SDK_CREATIVE_POTION_SPLASH = 2,
    LCE_SDK_CREATIVE_POTION_LINGERING = 4,
    LCE_SDK_CREATIVE_POTION_ARROW = 8,
};
typedef struct lce_sdk_creative_potion_entry {
    uintptr_t potion;
    const char *key;
    uint32_t forms;
} lce_sdk_creative_potion_entry;

int lce_sdk_creative_potions_wanted(void);
/* Shipping owner only. Requires its synchronous pre-TabSpec Creative lease
 * and stock Potion registry. Entry and row capacity must cover every form.
 * Returns the full number of live records, or zero without writing out. */
int lce_sdk_creative_potion_entries(uintptr_t base, void *ctx, int (*held)(void *),
                                  lce_sdk_creative_potion_entry *out, uint32_t entry_capacity, uint32_t row_capacity);

#ifdef __cplusplus
}
#endif
#endif
