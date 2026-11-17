#pragma once
#include "native_bridge.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Exact 1.95 NetworkPlayerSony::SendData calling convention. All objects,
   hooks and bindings must remain live until original vptrs are restored and
   every in-flight call has drained. Failure callback must schedule teardown;
   it must not destroy the bridge or re-enter this hook. */
typedef void (*lce_player_send_fn)(void *self, void *target, const void *bytes,
                                   int size, bool low_priority, bool ack);
typedef void (*lce_send_failure_fn)(void *);
typedef struct {
    void *local_player, *host_player;
    lce_native_bridge *bridge;
    lce_player_send_fn original;
    lce_send_failure_fn failure;
    void *failure_context;
} lce_player_send_binding;
/* Called only while no game thread can dispatch this hook. */
int lce_player_send_install_binding(lce_player_send_binding *);
void lce_player_send_remove_binding(lce_player_send_binding *);
void lce_player_send_hook(void *, void *, const void *, int, bool, bool);

#ifdef __cplusplus
}
#endif
