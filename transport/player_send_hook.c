#include "player_send_hook.h"
/* Binding lifetime and publication are controlled by the game owner, not by
   this module. Activation is a prerequisite to publishing the hooked vptr. */
static lce_player_send_binding *active;
int lce_player_send_install_binding(lce_player_send_binding *b) {
    if (!b || !b->local_player || !b->host_player ||
        b->local_player==b->host_player || !b->bridge || !b->original || !b->failure) return 0;
    lce_player_send_binding *empty=0;
    return __atomic_compare_exchange_n(&active,&empty,b,0,__ATOMIC_RELEASE,__ATOMIC_RELAXED);
}
void lce_player_send_remove_binding(lce_player_send_binding *b) {
    lce_player_send_binding *expected=b;
    (void)__atomic_compare_exchange_n(&active,&expected,0,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE);
}
void lce_player_send_hook(void *self,void *target,const void *bytes,int size,bool low,bool ack) {
    lce_player_send_binding *b=__atomic_load_n(&active,__ATOMIC_ACQUIRE);
    if (!b) return; /* unreachable while dispatch and binding lifetimes agree */
    if (self==b->local_player && target==b->host_player) {
        if (!bytes || size<=0 || !lce_native_bridge_send(b->bridge,bytes,(size_t)size))
            b->failure(b->failure_context);
        return;
    }
    b->original(self,target,bytes,size,low,ack);
}
