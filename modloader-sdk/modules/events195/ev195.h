/* events195: game event sources, runtime services, commands, keybinds and
 * localisation for the retail CUSA00265 1.95 backend.
 *
 * Every RVA is from the 1.95 image (ELF c675e6ce...029f) and is listed in
 * natives195.h and STATUS.md. Nothing in this module patches .text:
 *   - server tick / player presence / world load: a Mc195SharedScope
 *     subscriber (the existing console-proven call hooks own the native
 *     Connection tick; we only add a subscriber);
 *   - item use: slot +0x40 of the SDK item's private vtable copy;
 *   - block break: slot +0x3e0 of a mod block's private vtable copy;
 *   - entity damage/death: slots +0x248/+0xa00 of a private vtable copy that
 *     is installed only on entities a mod spawned (never on players);
 *   - keybinds: a filter called from the existing scePadRead GOT entry;
 *   - commands/chat: a submit queue drained on the server thread.
 * All callbacks into mods run on the game server thread inside a frame
 * (lce195_frame) that the runtime services check before touching the game.
 *
 * Threading: lce195_ev_* entry points marked [server] must be called from the
 * native server thread (scope subscriber / thunks). [any] are lock-free and
 * may be called from the pad or UI threads. Not reentrant across servers: one
 * process, one game, one instance. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "../../include/lce_mod_api.h"

/* API 1.3 event ids and capability bits served by this module. include/lce_mod_api.h owns the real enum; until it
 * defines LCE_HAVE_EV_1_3 these provisional values follow LCE_EV_ENTITY_TICK in exactly this order, so the header must
 * append them in this order (and before LCE_EV_COUNT) to keep ids stable. */
#ifndef LCE_HAVE_EV_1_3
#define LCE_EV_ITEM_INVENTORY_TICK ((lce_event)(LCE_EV_ENTITY_TICK + 1)) /* lce_ev_item_use: item_id, player = holder entity,
                                                                           * aux = inventory slot, hand 0 = selected, 2 = not held */
#define LCE_EV_BLOCK_NEIGHBOR      ((lce_event)(LCE_EV_ENTITY_TICK + 2)) /* lce_ev_block: neighbour changed; face = side of the
                                                                           * changed neighbour 0..5 (-1 not adjacent), player NULL */
#define LCE_EV_BLOCK_ENTITY_INSIDE ((lce_event)(LCE_EV_ENTITY_TICK + 3)) /* lce_ev_block: player = any entity inside the cell */
#define LCE_EV_BLOCK_STEP_ON       ((lce_event)(LCE_EV_ENTITY_TICK + 4)) /* lce_ev_block: player = any entity walking on top */
#define LCE_CAP2_EV_INVENTORY_TICK (1u << 5)
#define LCE_CAP2_EV_NEIGHBOR       (1u << 6)
#define LCE_CAP2_EV_BLOCK_COLLIDE  (1u << 7)   /* ENTITY_INSIDE + STEP_ON */
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Mirror of mc195_lucky_server_view (r72 server_scope.h). Layout is checked
 * with static_assert in r72_bind195.cpp. A view is borrowed: never stored past
 * the callback that received it. */
typedef struct lce195_view {
    uintptr_t image, server, level;
    uintptr_t player_object, player_control;
    uint64_t session, world, thread, tick;
} lce195_view;

typedef struct lce195_pair { uintptr_t object, control; } lce195_pair;

/* Host bindings. On the console: r72_bind195.cpp. In tests: fakes. */
typedef struct lce195_host {
    void *opaque;
    uintptr_t image;                                         /* attested base */
    uint64_t (*thread_id)(void *);
    int (*read)(void *, uintptr_t, void *, size_t);          /* 1 = ok, checked */
    int (*write_word)(void *, uintptr_t, uintptr_t expected, uintptr_t value); /* CAS of an aligned 8-byte data word; 1 = ok */
    int (*is_code)(void *, uintptr_t);                       /* 1 if address is in the game's RX segment */
    int (*view_current)(void *, const lce195_view *);        /* mc195_lucky_server_current */
    /* Borrow a view for a retained player pair seen in an item/block thunk
     * (mc195_lucky_server_borrow). 1 ok. NULL: thunk events carry no frame. */
    int (*view_borrow)(void *, const lce195_pair *, uintptr_t level, lce195_view *);
    uint64_t (*now_ms)(void *);                               /* monotonic ms (pad thread) */
    void (*log)(void *, const char *);
} lce195_host;

/* The event sink: lce_loader_dispatch / lce_sdk_dispatch. Returns nonzero to cancel. */
typedef int (*lce195_sink)(void *user, lce_event ev, const void *payload);

/* [startup] One-time init. Returns 1, or 0 on a bad host. */
int lce195_ev_init(const lce195_host *host, lce195_sink sink, void *sink_user);
/* Capability bits this module can serve with the given host (for backend195). */
uint32_t lce195_ev_capabilities(void);

/* ---- Mc195SharedScope subscriber (pass &view cast from mc195_lucky_server_view) ---- */
void lce195_ev_scope_tick(void *opaque, const lce195_view *view);           /* [server] */
void lce195_ev_scope_finish(void *opaque, uint64_t thread, uint64_t session);/* [server] */
void lce195_ev_scope_failed(void *opaque, unsigned phase);                  /* [server] */

/* ---- Optional exact lifecycle observers (call from existing r72 handlers) ---- */
void lce195_ev_player_login(uintptr_t player);       /* after 0xcc3780 original returns */
void lce195_ev_player_remove(uintptr_t player);      /* before 0xcc85c0 original runs */
void lce195_ev_world_unloading(void);                /* from the 0xc862e0 epoch exit */

/* ---- item use (private Item vtable slot +0x40, stock 0x3aedb0) ----------- */
/* Remember the original slot value for an SDK item object. Call while
 * building the private vtable; returns 1 if the slot holds the stock
 * Item::use and the item is now routed. */
int lce195_ev_route_item(uintptr_t item_object, uint32_t item_id, uintptr_t original_use);
/* Installed in slot +0x40. ABI (from ItemStack::use 0x3b8190):
 * (sret holder*, Item*, Level*, shared_ptr<Player>*, int hand) -> holder* */
void *lce195_ev_item_use_thunk(void *out, void *item, void *level, const lce195_pair *player, int hand);

/* ---- block break (private Block vtable slot +0x3e0 playerWillDestroy, stock 0x94800) ---- */
int lce195_ev_route_block(uintptr_t block_object, uint32_t block_id, uintptr_t original_will_destroy);
int lce195_ev_block_will_destroy_thunk(uintptr_t block, uintptr_t level, uintptr_t pos, uintptr_t state, const lce195_pair *player);
/* Block place: mod BlockItem private vtable slot +0x28 useOn (stock 0x7d4c0). */
int lce195_ev_route_block_item(uintptr_t block_item_object, uint32_t block_id, uintptr_t block_object, uintptr_t original_use_on);
int lce195_ev_block_item_use_on_thunk(void *item, const lce195_pair *player, void *level, const int32_t *pos,
                                      int hand, int face, float cx, float cy, float cz);

/* Block use (API 1.2): mod Block private vtable slot +0x2c0 (stock 0x940b0 returns false). Returns 1 routed.
 * The thunk forwards every integer register and three stack words unchanged. */
int lce195_ev_route_block_use(uintptr_t block_object, uint32_t block_id, uintptr_t original_use);
uint64_t lce195_ev_block_use_thunk(uintptr_t block, uintptr_t level, uintptr_t pos, uintptr_t state, const lce195_pair *player,
                                   uintptr_t r9, uintptr_t s0, uintptr_t s1, uintptr_t s2);
/* API 1.3 block callbacks on the same private Block vtable copy. Each stock body is a bare `ret` (natives195.h), so a
 * route is accepted only when the slot still holds it. Thunks dispatch, then call the original. */
int lce195_ev_route_block_tick(uintptr_t block_object, uint32_t block_id, uintptr_t original_tick);         /* +0x1f0 */
void lce195_ev_block_tick_thunk(uintptr_t block, uintptr_t level, uintptr_t pos, uintptr_t state, uintptr_t random);
/* Random ticks reach +0x1f0 only when Block::isTicking (+0x140 -> byte +0x149) is set: set it for a routed block that
 * wants random ticks (Java Block.Properties.randomTicks()). CAS of the aligned word +0x148. 1 ok. [startup/server] */
int lce195_ev_set_block_random_ticking(uintptr_t block_object, int on);
int lce195_ev_route_block_neighbor(uintptr_t block_object, uint32_t block_id, uintptr_t original);           /* +0x208 */
void lce195_ev_block_neighbor_thunk(uintptr_t block, uintptr_t state, uintptr_t level, uintptr_t pos, uintptr_t from_block,
                                    uintptr_t from_pos);
int lce195_ev_route_block_step_on(uintptr_t block_object, uint32_t block_id, uintptr_t original);            /* +0x2c8 */
void lce195_ev_block_step_on_thunk(uintptr_t block, uintptr_t level, uintptr_t pos, const lce195_pair *entity);
int lce195_ev_route_block_inside(uintptr_t block_object, uint32_t block_id, uintptr_t original);             /* +0x328 */
void lce195_ev_block_inside_thunk(uintptr_t block, uintptr_t state, uintptr_t level, uintptr_t pos, const lce195_pair *entity);
/* Item +0xc8 inventoryTick on an SDK item's private vtable copy (stock 0x3af000 `ret`). */
int lce195_ev_route_item_inventory_tick(uintptr_t item_object, uint32_t item_id, uintptr_t original);
void lce195_ev_item_inventory_tick_thunk(uintptr_t item, const lce195_pair *stack, uintptr_t level, const lce195_pair *entity,
                                         int slot, int selected);
/* API 1.4 mod networking: client -> server custom payloads on registered mod channels.
 * The host player's ServerGamePacketListenerImpl gets a private vtable copy with +0x340 handleCustomPayload routed
 * (installed on the first retained tick once a channel is registered). Registered channels are dispatched as
 * LCE_EV_CUSTOM_PAYLOAD and not passed to the vanilla handler; every other channel runs the stock 0xcf10a0. */
#ifndef LCE_HAVE_NET_1_4
#define LCE_EV_CUSTOM_PAYLOAD ((lce_event)(LCE_EV_BLOCK_STEP_ON + 1))
typedef struct { uint32_t struct_size; void *player; const char *channel; const uint8_t *data; uint32_t len; } lce_ev_payload;
#define LCE_CAP2_EV_PAYLOAD (1u << 18)
#endif
int lce195_net_register_channel(const char *channel);   /* [startup] 1 ok */
int lce195_ev_route_listener(uintptr_t player);         /* [server] 1 routed (or already) */
void lce195_ev_payload_thunk(void *listener, const lce195_pair *packet);
/* API 1.6 client-side mod networking (remote PS4 clients running the mod). Server -> this client arrives as
 * LCE_EV_CLIENT_PAYLOAD (lce_ev_payload, player NULL) on the client thread; a mod answers with send_to_server.
 * Data hook only: the packet factory map 0x142dcb0 entries for ids 250 (custom payload) and 4 (SetTime, the 1 Hz client pump)
 * point at wrappers that give each received packet a private vtable with handle (+0x28) routed. On the host (its own server is
 * ticking) client payloads are queued and delivered on the server tick instead, so a mod never runs on two threads at once. */
int lce195_net_client_install(void);                       /* [any] 1 installed (or already), 0 refused; needs a channel */
lce195_pair *lce195_ev_payload_create_thunk(lce195_pair *out);   /* packet factory entries (sret shared_ptr<Packet>) */
lce195_pair *lce195_ev_settime_create_thunk(lce195_pair *out);
int lce195_net_channel_registered(const char *channel);   /* [any] 1 if some mod registered it */
void lce195_ev_client_payload_handle_thunk(void *packet, void *listener);   /* private packet vtable +0x28 */
void lce195_ev_client_pump_handle_thunk(void *packet, void *listener);
/* The ClientPacketListener of the calling thread while it is inside a routed client packet handler, else 0. */
uintptr_t lce195_ev_client_listener(void);
/* 1 while this process's own server is ticking (host): client-side work is then delivered on the server tick. */
int lce195_ev_host_server_active(void);
/* [server] Queue a payload from the host player to its own server (send_to_server on the host); delivered next tick as
 * LCE_EV_CUSTOM_PAYLOAD with player = the host player. 1 queued. */
int lce195_ev_loopback_payload(const char *channel, const void *data, uint32_t len);
/* LCE_CAP2_* event bits this module serves (API 1.2). */
uint32_t lce195_ev_capabilities2(void);

/* ---- entity damage / death on mod-spawned entities ----------------------- */
/* Swap the entity's primary vptr to a private copy with +0x248 (hurt) and
 * +0xa00 (die) routed. [server] Returns 1 routed, 0 refused (unchanged). */
int lce195_ev_track_entity(uintptr_t entity);
int lce195_ev_entity_hurt_thunk(void *entity, void *source, float damage);   /* bool */
void lce195_ev_entity_die_thunk(void *entity, void *source);
/* API 1.2: Entity::tick (+0xa8) on the same private copy -> LCE_EV_ENTITY_TICK, then the original. */
void lce195_ev_entity_tick_thunk(void *entity);
/* API 1.2: Entity::interact (+0xe50) -> LCE_EV_ENTITY_INTERACT (nonzero = handled), else the original. */
uint64_t lce195_ev_entity_interact_thunk(void *entity, const lce195_pair *player, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5);

/* ---- frames: what services check ----------------------------------------- */
typedef struct lce195_frame {
    lce195_view view;
    uint32_t active;          /* 1 while a mod callback runs on the server thread */
    uint32_t has_player;
} lce195_frame;
/* Current frame for the calling thread, or NULL outside a server callback. */
const lce195_frame *lce195_ev_frame(void);
/* API 1.9 [server] Run fn(user) for a callback that is not an events195 route (projectiles195 hit): inside a
 * level-only frame when `level` is the anchor's current server level on its thread (level/entity services work,
 * player services refused), else fn is NOT run. 1 ran framed, 0 not run (not the server level/thread, failed). */
int lce195_ev_level_call(uintptr_t level, void (*fn)(void *user), void *user);
/* API 1.9 [startup] lce195_ev_route_item also accepts this original +0x40 (projectiles195 p195_item_use, which a
 * throwable SDK item starts with) besides the stock Item::use; 0 clears. The use thunk calls it after ITEM_USE. */
void lce195_ev_allow_item_use_original(uintptr_t fn);

/* ---- scheduler (server ticks) --------------------------------------------- */
#define LCE195_MAX_SCHEDULED 64u
int lce195_ev_schedule(uint32_t ticks, lce_event_fn fn, void *user);   /* [server or startup] */
uint64_t lce195_ev_server_ticks(void);

/* ---- keybinds ------------------------------------------------------------- */
#define LCE195_PAD_L3        0x00000002u
#define LCE195_PAD_R3        0x00000004u
#define LCE195_PAD_RIGHT     0x00000020u
#define LCE195_PAD_LEFT      0x00000080u
#define LCE195_PAD_TOUCH     0x00100000u
#define LCE195_MAX_KEYBINDS  16u
#define LCE195_HOLD_MS       600u
int lce195_keys_register(const lce_keybind_desc *d);    /* [startup] 1 ok */
/* [pad thread] Filter called from the existing scePadRead GOT entry
 * (debug_pad_sessions_195 mc195_pad_entry) after the debug chord ran.
 * Returns the buttons the game should see. */
uint32_t lce195_keys_filter(int32_t handle, uint32_t buttons, int connected, int debug_menu_visible);

/* ---- commands ------------------------------------------------------------- */
#define LCE195_MAX_COMMANDS 64u
#define LCE195_CMD_LINE 256u
int lce195_cmd_register(const char *modid, const lce_command_desc *d);   /* [startup] 1 ok */
/* [any] Queue a chat line ("/ruby give" or text) for the host player; it is
 * delivered as LCE_EV_CHAT then parsed on the next server tick. 1 queued. */
int lce195_cmd_submit(const char *utf8_line);
/* Candidate data hook: ServerGamePacketListenerImpl private vtable slot +0x300
 * (stock 0x5ff9b0 = unhandled). (listener, shared_ptr<ServerboundChatPacket>*) */
void lce195_ev_chat_packet_thunk(void *listener, const lce195_pair *packet);

/* Host bindings given to lce195_ev_init (NULL before init). */
const lce195_host *lce195_ev_host(void);
/* 1 if the caller is the native server thread observed by the last scope tick. */
int lce195_ev_on_server_thread(void);

/* ---- diagnostics ---------------------------------------------------------- */
typedef struct lce195_ev_stats {
    uint64_t scope_ticks, server_ticks, dispatched, cancelled, refused;
    uint32_t players, joins, leaves, worlds, item_uses, block_breaks, block_places, hurts, deaths;
    uint32_t keys, commands, chat, scheduled_run, tracked_entities, faults;
    uint32_t block_uses, entity_ticks, interacts;
    uint32_t block_ticks, neighbors, insides, step_ons, inventory_ticks;   /* API 1.3 */
    uint32_t payloads, listeners;                                          /* API 1.4 */
    uint32_t client_payloads, client_pumps, client_packets, loopbacks, client_dropped;   /* API 1.6 */
} lce195_ev_stats;
void lce195_ev_get_stats(lce195_ev_stats *out);

#ifdef LCE195_HOST_TEST
void lce195_ev_reset_for_tests(void);
#endif

#ifdef __cplusplus
}
#endif
