/* services195: lce_api 1.1 runtime services for the retail 1.95 backend.
 *
 * Every service is refused (LCE_E_PHASE) unless the caller is on the native
 * server thread inside an events195 frame whose view is still current
 * (mc195_lucky_server_current). Players and levels passed by mods must be the
 * frame's own retained player / level, or NULL (= the frame's). Nothing is
 * cached across frames. Results: LCE_OK only after the native call and its
 * readback succeeded; LCE_E_BACKEND when a native step ran but could not be
 * verified (never retried). */
#pragma once
#include "ev195.h"
#include "natives195.h"

#ifdef __cplusplus
extern "C" {
#endif

/* World operations delegated to the console-proven r72 helpers
 * (lucky_block_world_events_195 + debug_give_195); bound in r72_bind195.cpp.
 * Each returns 1 ok, 0 refused before mutation, -1 uncertain after. */
typedef struct lce195_world_ops {
    void *opaque;
    int (*give)(void *, const lce195_view *, uint32_t item_id, uint32_t count, uint32_t aux);
    int (*set_block)(void *, const lce195_view *, int32_t x, int32_t y, int32_t z, uint16_t id, uint8_t meta);
    int (*get_block)(void *, const lce195_view *, int32_t x, int32_t y, int32_t z, uint16_t *id, uint8_t *meta);
    /* entity_key "minecraft:zombie"; *out receives the new entity (valid only
     * inside the after-spawn window of the helper; used to track mod entities). */
    int (*spawn)(void *, const lce195_view *, const char *entity_key, double x, double y, double z, int track);
    int (*loaded)(void *, const lce195_view *, int32_t chunk_x, int32_t chunk_z);
    /* API 1.2: ServerLevel explosion (r72 mc195_world_explode); source empty. NULL = unsupported. */
    int (*explode)(void *, const lce195_view *, double x, double y, double z, float radius, int fire, int destroy_blocks);
} lce195_world_ops;

/* Key -> numeric ID resolution (lce_loader_item_id / lce_loader_block_id). */
typedef struct lce195_resolver {
    void *user;
    int32_t (*item)(void *, const char *key);
    int32_t (*block)(void *, const char *key);
} lce195_resolver;

/* [startup] Returns 1. natives/ops are copied. */
int lce195_services_init(const lce195_natives *n, const lce195_world_ops *ops, const lce195_resolver *r);
/* Fill the lce_api 1.1 service pointers (give_item .. translate). Does not
 * touch capabilities: the backend decides whether to advertise LCE_CAP_SERVICES. */
void lce195_services_fill(lce_api *api);

/* Individual services (same signatures as lce_api). */
lce_status lce195_give_item(lce_mod_ctx *, void *player, const char *item, uint32_t count, int32_t aux);
lce_status lce195_set_block(lce_mod_ctx *, void *level, int32_t x, int32_t y, int32_t z, const char *block, uint32_t data);
int32_t lce195_get_block(lce_mod_ctx *, void *level, int32_t x, int32_t y, int32_t z);
lce_status lce195_spawn_entity(lce_mod_ctx *, void *level, const char *entity, int32_t x, int32_t y, int32_t z);
lce_status lce195_drop_item(lce_mod_ctx *, void *level, int32_t x, int32_t y, int32_t z, const char *item, uint32_t count);
lce_status lce195_send_chat(lce_mod_ctx *, void *player, const char *message);
lce_status lce195_add_effect(lce_mod_ctx *, void *entity, const lce_effect_ref *e);
/* API 1.3 item NBT value (provisional until include/lce_mod_api.h defines LCE_HAVE_NBT_1_4 with the same layout). */
#ifndef LCE_HAVE_NBT_1_4
typedef enum { LCE_NBT_NONE = 0, LCE_NBT_INT = 1, LCE_NBT_DOUBLE = 2, LCE_NBT_STRING = 3 } lce_nbt_type;
typedef struct { uint32_t type; int64_t i; double d; char s[256]; } lce_nbt_value;
#endif
/* Main-hand stack tag of the frame's player: get (byte/short/int/long -> INT, float/double -> DOUBLE, string -> STRING,
 * ASCII view), set (int32 / double / ASCII string; creates the tag), remove. Keys: 1..64 of [A-Za-z0-9_:.-]. */
lce_status lce195_item_tag_get(lce_mod_ctx *, void *player, const char *key, lce_nbt_value *out);
lce_status lce195_item_tag_set(lce_mod_ctx *, void *player, const char *key, const lce_nbt_value *v);
lce_status lce195_item_tag_remove(lce_mod_ctx *, void *player, const char *key);
/* API 1.5 entity NBT (include/lce_mod_api.h LCE_HAVE_ENTITY_NBT_1_5, CAP2 bits SVC_ENTITY_NBT / SVC_ENTITY_DATA).
 * entity: NULL = the frame's player, else an entity of the frame's level.
 *   entity_nbt_get: read-only view of the entity's vanilla save data (saveWithoutId into a temporary tag). path = segments
 *     separated by '/', each a key optionally followed by [index] into a list, or [#] for the list length:
 *     "Health", "Pos[1]", "Attributes[0]/Base", "Tags[#]". Scalars as in item_tag_get; compounds/lists -> LCE_E_INVALID.
 *   entity_data_get/set/remove: persistent per-entity mod data (Forge getPersistentData). Stored as entity scoreboard tags
 *     "lce|<key>=<type><value>" so it is saved and loaded with the entity by the game itself. Keys as item_tag_*. Writes refuse
 *     players (LCE_E_INVALID), more than 1024 tags (LCE_E_FULL) and strings over 200 characters. */
lce_status lce195_entity_nbt_get(lce_mod_ctx *, void *entity, const char *path, lce_nbt_value *out);
lce_status lce195_entity_data_get(lce_mod_ctx *, void *entity, const char *key, lce_nbt_value *out);
lce_status lce195_entity_data_set(lce_mod_ctx *, void *entity, const char *key, const lce_nbt_value *v);
lce_status lce195_entity_data_remove(lce_mod_ctx *, void *entity, const char *key);
/* API 1.4 mod networking: ClientboundCustomPayloadPacket on channel "modid:name" (3..20 of [a-z0-9_:.-], not "mc|"),
 * up to 32000 bytes, to one player (the frame's) or everyone (NULL). */
int lce195_net_channel_ok(const char *channel);
lce_status lce195_send_payload(lce_mod_ctx *, void *player, const char *channel, const void *data, uint32_t len);
/* API 1.6 client -> server: ServerboundCustomPayloadPacket on a registered channel, <= 32000 bytes. Inside a client packet
 * handler (LCE_EV_CLIENT_PAYLOAD) it is sent at once by ClientPacketListener::send on the client thread; elsewhere on a remote
 * client it is queued and sent by the next SetTime pump (about 1 s); on the host (its own server ticking) it is delivered to the
 * host's own server next tick as LCE_EV_CUSTOM_PAYLOAD from the host player. LCE_E_FULL when the queue is full. */
lce_status lce195_send_to_server(lce_mod_ctx *, const char *channel, const void *data, uint32_t len);
void lce195_net_client_flush(uintptr_t listener);   /* [client thread, inside a routed handler] send the queue */
/* API 1.3: ServerLevel::sendParticles with a ParticleType id (ParticleType +0x28). LCE_E_NOTFOUND for unknown ids. */
lce_status lce195_spawn_particles(lce_mod_ctx *, void *level, int32_t type_id, double x, double y, double z, uint32_t count,
                                  double dx, double dy, double dz, double speed);
lce_status lce195_play_sound(lce_mod_ctx *, void *level, int32_t x, int32_t y, int32_t z, const char *sound);
lce_status lce195_schedule(lce_mod_ctx *, uint32_t ticks, lce_event_fn fn, void *user);
const char *lce195_translate(lce_mod_ctx *, const char *key);
/* API 1.2 services */
lce_status lce195_explode(lce_mod_ctx *, void *level, double x, double y, double z, float radius, int fire, int destroy_blocks);
lce_status lce195_set_fire(lce_mod_ctx *, void *entity, uint32_t ticks);
lce_status lce195_heal(lce_mod_ctx *, void *entity, uint32_t amount_milli);
lce_status lce195_consume_held_item(lce_mod_ctx *, void *player, uint32_t count);
lce_status lce195_damage_entity(lce_mod_ctx *, void *entity, uint32_t amount_milli, void *attacker);
lce_status lce195_damage_held_item(lce_mod_ctx *, void *player, uint32_t amount);
lce_status lce195_teleport(lce_mod_ctx *, void *entity, double x, double y, double z);
lce_status lce195_set_motion(lce_mod_ctx *, void *entity, double dx, double dy, double dz);
/* LCE_CAP2_* service bits with the bound natives/ops. */
uint32_t lce195_services_capabilities2(void);

/* ---- localisation ---------------------------------------------------------- */
/* Load the plan's string list (lce_plan_string: locale, key, value). Strings
 * must stay valid for the process (the loader's staged plan does). */
typedef struct lce195_l10n_entry { const char *locale, *key, *value; } lce195_l10n_entry;
int lce195_l10n_load(const lce195_l10n_entry *e, uint32_t n);   /* 1 ok, 0 too many */
/* "fr_FR"-style code of the game's current language (in-game Languages menu
 * override from GameSettings, else the PS4 system language), "en_US" fallback. */
const char *lce195_l10n_locale(void);
/* Map an XC language/locale pair (GameSettings +0x90/+0x91) to a locale code. */
const char *lce195_l10n_from_xc(unsigned lang, unsigned locale);
/* Map an SCE system language code to a locale code. */
const char *lce195_l10n_from_sce(int sce_lang);

/* ---- sounds ------------------------------------------------------------- */
int32_t lce195_sound_id(const char *name);           /* vanilla 1.95 name -> SoundEvent id, -1 */
/* Mod sound = clone of a vanilla event with volume/pitch. 1 ok. PCM refused. */
int lce195_sound_register(const char *modid, const lce_sound_desc *d, char *err, uint32_t err_cap);
/* Custom audio (audio195): make a registered mod SoundEvent id playable by key through play_sound. 1 ok. */
int lce195_sound_set_custom(const char *key, int32_t id);

#ifdef LCE195_HOST_TEST
void lce195_services_reset_for_tests(void);
#endif

#ifdef __cplusplus
}
#endif
