#include "native_bridge.h"
#include <limits.h>
/* Optional plugin hook (weak: absent in the host tests). Returns nonzero when it
   consumed a server->plugin control frame that must not reach the retail client. */
extern int lce_remote_inbound_frame(const uint8_t *,size_t) __attribute__((weak));
static int receive_frame(void *context,const uint8_t *data,size_t n) {
    lce_native_bridge *b=context;
    if (!b->attached || !b->socket || !n || n>UINT32_MAX) return 0;
    if (lce_remote_inbound_frame && lce_remote_inbound_frame(data,n)) return 1;
    b->ops.push(b->socket,data,(uint32_t)n,1);
    return 1;
}
int lce_native_bridge_attach(lce_native_bridge *b,lce_session_transport *s,void *socket,
                             const lce_native_bridge_ops *ops) {
    if (!b||!s||!socket||!ops||!ops->lock||!ops->unlock||!ops->push||
        (s->state!=LCE_SESSION_CONNECTING&&s->state!=LCE_SESSION_ACTIVE)) return 0;
    b->session=s; b->socket=socket; b->ops=*ops; b->attached=1; return 1;
}
int lce_native_bridge_send(lce_native_bridge *b,const uint8_t *data,size_t n) {
    if (!b || !b->ops.lock || !b->ops.unlock) return 0;
    b->ops.lock(b->ops.context);
    int ok=b->attached && lce_session_send(b->session,data,n);
    b->ops.unlock(b->ops.context); return ok;
}
lce_session_state lce_native_bridge_tick(lce_native_bridge *b,uint64_t now) {
    if (!b || !b->ops.lock || !b->ops.unlock) return LCE_SESSION_FAILED;
    b->ops.lock(b->ops.context);
    lce_session_state state=b->attached ? lce_session_tick(b->session,now,receive_frame,b) : LCE_SESSION_CLOSED;
    b->ops.unlock(b->ops.context); return state;
}
void lce_native_bridge_detach(lce_native_bridge *b) {
    if (!b || !b->ops.lock || !b->ops.unlock) return;
    b->ops.lock(b->ops.context);
    if (b->attached) { b->attached=0; lce_session_close(b->session); b->socket=0; }
    b->ops.unlock(b->ops.context);
}
