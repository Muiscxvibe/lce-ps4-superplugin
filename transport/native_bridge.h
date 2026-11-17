#pragma once
#include "session_transport.h"
#ifdef __cplusplus
extern "C" {
#endif

/* The bridge is serialized by caller-supplied lock callbacks. Native socket
   ownership and player lifecycle belong to the game; the owner must prevent
   socket destruction and drain active calls before detaching. push must copy
   bytes before returning and may not re-enter this bridge. */
typedef struct {
    void *context;
    void (*lock)(void *);
    void (*unlock)(void *);
    void (*push)(void *socket, const uint8_t *data, uint32_t size, int from_host);
} lce_native_bridge_ops;
typedef struct {
    lce_session_transport *session;
    void *socket;
    lce_native_bridge_ops ops;
    int attached;
} lce_native_bridge;
int lce_native_bridge_attach(lce_native_bridge *, lce_session_transport *, void *socket,
                             const lce_native_bridge_ops *);
int lce_native_bridge_send(lce_native_bridge *, const uint8_t *, size_t);
lce_session_state lce_native_bridge_tick(lce_native_bridge *, uint64_t now);
/* Call only after external dispatch has stopped and the socket is pinned. */
void lce_native_bridge_detach(lce_native_bridge *);

#ifdef __cplusplus
}
#endif
