#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct mc195_lucky_native_pair {uintptr_t object,control;} mc195_lucky_native_pair;
/* Borrowed only inside the actual native Connection tick. No game pointer in
 * this view may be queued, saved, or used after the bridge returns. The native
 * server frame owns its Level array; the caller supplies an already retained
 * player pair. This does not authorize traversing the mutable player list. */
typedef struct mc195_lucky_server_view {
 uintptr_t image,server,level;mc195_lucky_native_pair player;
 uint64_t session,world,thread,tick;
} mc195_lucky_server_view;
typedef struct mc195_lucky_server_config {
 uintptr_t image;void *opaque;
 int (*attested)(void *,uintptr_t);
 int (*read)(void *,uintptr_t,void *,size_t);
 uint64_t (*thread_id)(void *);
 /* The installed NBT lifecycle generation, zero throughout any transition. */
 uint64_t (*session)(void *);
 /* Runs before the retained ServerPlayer's real tick, within Connection tick.
  * Must not store the view or its pointers. Normal native forwarding occurs
  * once, including when admission fails. Exceptions unwind lexical state. */
 void (*tick_player)(void *,const mc195_lucky_server_view *);
 /* Optional, nonthrowing: releases captures before the lexical frame ends,
  * including exception exits. Native addresses must not escape this frame. */
 void (*finish_connection)(void *,uint64_t thread,uint64_t session);
 /* Explicit opt-in for the native paused Connection loop. Normal subscribers
  * are never called through this path. Same retained-player/lifetime contract
  * as tick_player; this is not a general world simulation tick. */
#ifdef __cplusplus
 void (*tick_paused)(void *,const mc195_lucky_server_view *)=nullptr;
#else
 void (*tick_paused)(void *,const mc195_lucky_server_view *);
#endif
} mc195_lucky_server_config;
/* Install the exact guarded call sites while the real startup owner excludes
 * native activity. 1 installed, 0 clean refusal, -1 partial/uncertain: remain
 * resident on 1/-1. tick_paused additionally installs the paused-loop call.
 * Never retry a partial installation or hot-unload it. */
int mc195_lucky_server_start(const mc195_lucky_server_config *,int before_activity);
/* For the live prebreak shared pair, or the retained player tick pair. The
 * supplied pair must remain retained for the entire operation using the view. */
int mc195_lucky_server_borrow(const mc195_lucky_native_pair *,uintptr_t level,mc195_lucky_server_view *);
int mc195_lucky_server_current(const mc195_lucky_server_view *);
enum {MC195_SERVER_SCOPE_NONE=0,MC195_SERVER_SCOPE_NORMAL=1,MC195_SERVER_SCOPE_PAUSED=2};
/* Value-only lexical origin, for synchronous subscriber dispatch/cleanup.
 * Requires the actual owner thread and exact active thread/session values.
 * Cleanup may query it after lifecycle invalidation; this is not native edit
 * authority. Mutation still requires a current retained server view. */
unsigned mc195_lucky_server_scope_kind(uint64_t thread,uint64_t session);
#ifdef __cplusplus
}
#endif
