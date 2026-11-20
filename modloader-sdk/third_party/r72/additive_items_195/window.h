#ifndef LCE195_ADDITIVE_WINDOW_H
#define LCE195_ADDITIVE_WINDOW_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct lce195_items_window {
 uint32_t state,active;
 uintptr_t base,begin,capacity;
 uint64_t thread;
 void *context;
 uint64_t (*thread_id)(void *);
 int (*read)(void *,uintptr_t,void *,size_t);
 uintptr_t stock[2268];
} lce195_items_window;
/* One-shot post-stock-items phase. It may follow the old Steel pair callback.
 * Caller attests exact image, phase and single super-plugin launch. This is
 * an observed thread/lifecycle boundary, not a general engine-wide mutex. */
int lce195_items_window_enter(lce195_items_window *,uintptr_t base,int image_and_phase_attested);
int lce195_items_window_held(void *window);
int lce195_items_window_read(void *window,uintptr_t,void *,size_t);
int lce195_items_window_leave(lce195_items_window *);
#ifdef __cplusplus
}
#endif
#endif
