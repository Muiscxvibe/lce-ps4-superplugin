/* effects195 - new status effects (MobEffect) and new potion types (Potion)
 * for the retail CUSA00265 1.95 backend.
 *
 * Image: reconstructed ELF c675e6ce601b6457a1c1466b0da3d9ccd7415f6b0db9f8e9da7b6e511816029f
 * (RX 231a6bfb...c682). Every RVA is from that image; STATUS.md cites the
 * disassembly that proves each one and its evidence level.
 *
 * Nothing here patches .text. Effects: alloc + the retail MobEffect ctor
 * 0x490db0 + a PRIVATE vtable copy (slots 0/1/2 forwarded to the clone's class
 * functions with `this` replaced by the stock effect, because those functions
 * compare `this` with the static MobEffects pointers), registered at a free id
 * through the registry's own virtual. Potions: alloc 0x38, the six string ids,
 * MobEffectInstances from the retail ctor 0x491aa0, registered through the
 * Potion registry's own virtual. All writes are heap/data.
 *
 * Return convention (same as items195):
 *    1  complete and post-verified
 *    0  refused before the first game write or native allocation
 *   -1  partial/uncertain: native memory may have changed. Keep everything
 *       resident, never retry, report PARTIAL.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "../../include/lce_mod_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LCE195E_ABI 1u

/* ---- pinned RVAs ---------------------------------------------------------- */
#define LCE195E_RVA_ALLOC             0xe6cfd0u  /* operator new(size_t)                              */
#define LCE195E_RVA_FREE              0xe6cfb0u  /* operator delete(void*)                            */
#define LCE195E_RVA_WSTRING           0x67840u   /* wstring assign (dst, const u16*, n)               */
#define LCE195E_RVA_RL_CTOR           0xcda930u  /* ResourceLocation(this[0x68], wstring& ns, wstring& path) */
#define LCE195E_RVA_RL_DTOR           0xcdab00u
/* effects */
#define LCE195E_RVA_EFFECT_REGISTRY   0x14195c8u /* MappedRegistry<ResourceLocation, MobEffect*>* (vt 0x1294038) */
#define LCE195E_RVA_EFFECT_COUNT      0x14195d0u /* int: last registered id (HUD sizes an int[count+1] by it) */
#define LCE195E_RVA_EFFECT_CTOR       0x490db0u  /* MobEffect(this, bool harmful, eMinecraftColour)  */
#define LCE195E_RVA_EFFECT_VT         0x1287760u /* MobEffect vtable (15 slots)                       */
#define LCE195E_RVA_EFFECT_ADD_MOD    0x4917d0u  /* vt+0x40 addAttributeModifier(this, Attribute*, int id, int op, double amount) -> this */
#define LCE195E_RVA_EFFECT_BY_ID      0x490d60u  /* MobEffect::byId(int) -> registry vt+0x40         */
#define LCE195E_RVA_ATTACK_DAMAGE_VT  0x120b560u /* AttackDamageMobEffect vtable (object 0x80, +0x78 double) */
#define LCE195E_RVA_MEI_CTOR          0x491aa0u  /* MobEffectInstance(this[0x18], MobEffect*, duration, amp) */
/* potions */
#define LCE195E_RVA_POTION_REGISTRY   0x142e938u /* DefaultedMappedRegistry<ResourceLocation, Potion*>* (vt 0x12aa668) */
#define LCE195E_RVA_POTION_COUNT      0x142e940u /* int: next potion id (0x55e6d0 post-increments)   */
#define LCE195E_RVA_POTION_BY_ID      0x55e560u  /* Potion::byId(int) -> registry vt+0x40            */
#define LCE195E_RVA_PTR_VEC_GROW      0x6fd00u   /* vector<MobEffectInstance*>::grow(vec, n) as 0x55ecea */
#define LCE195E_RVA_SET_POTION        0x562cd0u  /* PotionUtils::setPotion(sp<II>* out, sp<II>* in, Potion*) */
#define LCE195E_RVA_GET_POTION        0x562670u  /* PotionUtils::getPotion(sp<II>*) (NBT "Potion" key)  */

/* registry virtual slots (byte offsets) */
#define LCE195E_REG_GET       0x00u   /* get(ResourceLocation&)        */
#define LCE195E_REG_REGISTER  0x28u   /* registerMapping(id, ResourceLocation&, value) */
#define LCE195E_REG_KEY       0x30u   /* getKey(out ResourceLocation*, value) */
#define LCE195E_REG_ID        0x38u   /* getId(value)                  */
#define LCE195E_REG_BY_ID     0x40u   /* byId(int)                     */

/* MobEffect object (0x78; AttackDamageMobEffect 0x80) */
#define LCE195E_EFFECT_SIZE   0x80u    /* allocated for every mod effect */
#define LCE195E_EFF_MODMAP    0x08u    /* unordered_map<Attribute*, AttributeModifier*>: +0x10 list head, +0x18 size */
#define LCE195E_EFF_NAME_SID  0x58u    /* u32 string id (getDescriptionId 0x491590)     */
#define LCE195E_EFF_ICON      0x5cu    /* u32 icon frame, 0 = none (0x4915a0 / 0x4915b0) */
#define LCE195E_EFF_HARMFUL   0x60u    /* u8 (0x4915c0)                                  */
#define LCE195E_EFF_DURMOD    0x68u    /* double duration modifier (vt+0x20 0x491790)    */
#define LCE195E_EFF_BENEFIT   0x70u    /* u8 beneficial (vt+0x28 sets / vt+0x30 reads)    */
#define LCE195E_EFF_COLOUR    0x74u    /* u32 eMinecraftColour (vt+0x38 0x4917c0)          */
#define LCE195E_EFF_VT_SLOTS  15u
#define LCE195E_SLOT_TICK     0u       /* applyEffectTick(this, sp<LivingEntity>*, amp)  0x490ed0 */
#define LCE195E_SLOT_INSTANT  1u       /* applyInstantenousEffect(this, sp*, sp*, sp<LivingEntity>*, amp, double) 0x491240 */
#define LCE195E_SLOT_READY    2u       /* isDurationEffectTick(this, duration, amp)       0x491510 */
#define LCE195E_SLOT_IS_INSTANT 3u     /* isInstantenous(this): 0 base / 1 InstantenousMobEffect */

/* Potion object (0x38) */
#define LCE195E_POTION_SIZE   0x38u    /* +0x00 u32 sid[6], +0x18 vector<MEI*> (+0x20 begin, +0x28 end, +0x30 cap) */
#define LCE195E_POTION_VEC    0x18u

#define LCE195E_EFFECT_ID_MIN 32
#define LCE195E_EFFECT_ID_MAX 127      /* NBT "Id" and packets carry one byte (read movzbl); <=127 keeps Java servers' signed byte safe */
#define LCE195E_MAX_EFFECTS   32u
#define LCE195E_MAX_POTIONS   64u
#define LCE195E_MAX_POTION_EFFECTS 8u

/* ---- natives, memory ------------------------------------------------------- */
typedef struct lce195e_natives {
    void *(*alloc)(size_t);
    void (*release)(void *);
    void (*wstring)(void *dst, const uint16_t *units, uint64_t n);
    void (*rl_ctor)(void *rl, void *ns, void *path);
    void (*rl_dtor)(void *rl);
    void (*effect_ctor)(void *effect, int harmful, int colour);
    uintptr_t (*effect_add_modifier)(void *effect, uintptr_t attribute, int id, int op, double amount);
    uintptr_t (*effect_by_id)(int id);
    void (*mei_ctor)(void *mei, uintptr_t effect, int duration, int amplifier);
    uintptr_t (*potion_by_id)(int id);
    void (*ptr_vec_grow)(uintptr_t vec, int n);
    /* registry virtuals, dispatched through the registry object's live vtable */
    void (*reg_register)(uintptr_t reg, int id, void *rl, uintptr_t value);
    uintptr_t (*reg_get)(uintptr_t reg, void *rl);
} lce195e_natives;

/* Same shape as lce195x_memory: held() is 1 only on the startup thread inside the phase. */
typedef struct lce195e_memory {
    void *ctx;
    int (*read)(void *, uintptr_t, void *, size_t);
    int (*write)(void *, uintptr_t, const void *, size_t);
    int (*held)(void *);
} lce195e_memory;

typedef struct lce195e_env {
    uintptr_t base;
    lce195e_natives n;
    lce195e_memory m;
    /* addresses written into private vtable slots 0/1/2; lce195e_natives_bind sets the C thunks below */
    uintptr_t thunk[3];
} lce195e_env;

/* Console only: natives from pinned RVAs and the three thunk addresses. */
void lce195e_natives_bind(lce195e_env *e, uintptr_t base);

/* ======================= stock tables ===================================== */
/* Stock effect ids 1..31 (registration order of 0x4781e0) and potion ids 0..42 (0x55e6d0). */
int32_t lce195e_stock_effect_id(const char *key);
int32_t lce195e_stock_potion_id(const char *key);
/* How a clone of a stock effect behaves on 1.95:
 *   FORWARD   behaviour lives in vtable slots 0/1/2 (regeneration, poison, wither, hunger,
 *             saturation, instant health/damage) or class virtuals (absorption, health boost);
 *   ATTRIBUTE behaviour is the attribute-modifier map, copied (speed, slowness, strength, weakness, luck, unluck,
 *             haste/mining fatigue: attack-speed part only, see STATUS);
 *   IDENTITY  game code tests hasEffect(MobEffects::X) by pointer: a clone has no behaviour (refused). */
enum { LCE195E_CLONE_NONE = 0, LCE195E_CLONE_FORWARD, LCE195E_CLONE_ATTRIBUTE, LCE195E_CLONE_IDENTITY };
int lce195e_clone_kind(int32_t stock_id);
/* Stock icon frame (+0x5c) and colour enum (+0x74) of a stock effect, 0 if out of range. */
uint32_t lce195e_stock_icon(int32_t stock_id);
uint32_t lce195e_stock_colour(int32_t stock_id);
/* Nearest stock effect colour enum to an RGB (the colour table is indexed by enum, so an RGB
 * that is not a stock effect colour cannot be shown: the closest stock effect colour is used). */
uint32_t lce195e_nearest_colour(uint32_t rgb);

/* ======================= effects ========================================== */
typedef void (*lce195e_tick_fn)(void *user, int32_t effect_id, uintptr_t entity, int32_t amplifier);

typedef struct lce195e_effect_spec {
    const char *key;            /* "modid:name" (ResourceLocation, also the /effect key)          */
    int32_t id;                 /* 32..127, from the persistent ledger (saves store this byte)   */
    uint32_t name_sid;          /* string id resolving through the Localizer (strings195)        */
    int32_t clone_of;           /* stock id 1..31, 0 = no vanilla behaviour                      */
    uint32_t colour;            /* eMinecraftColour; 0 = clone's (or speed's)                    */
    uint32_t icon;              /* UINT32_MAX = clone's; 0 = none; else a stock icon frame        */
    uint8_t beneficial;
    uint8_t reserved[3];
    uint32_t tick_interval;     /* 0: clone decides when it ticks; N: on_tick every N ticks      */
    lce195e_tick_fn on_tick;    /* optional, server thread, from the applyEffectTick thunk       */
    void *user;
} lce195e_effect_spec;

/* Process-lifetime record (keep static). */
typedef struct lce195e_effect {
    int32_t id;
    uintptr_t object, vtable_block, stock;
    uintptr_t stock_fn[3];      /* clone class functions of slots 0/1/2, 0 = none */
    uint32_t tick_interval;
    lce195e_tick_fn on_tick;
    void *user;
    uint32_t modifiers;
} lce195e_effect;

lce_status lce195e_effect_check(const lce_effect_desc *d, char *err, uint32_t cap);
/* Desc -> spec (id and name_sid come from the caller). LCE_OK or the refusal reason. */
lce_status lce195e_effect_spec_from_desc(const lce_effect_desc *d, int32_t id, uint32_t name_sid, lce195e_effect_spec *out, char *err, uint32_t cap);

typedef struct lce195e_effect_publisher {
    lce195e_env *env;
    uint32_t state;             /* 0 new, 1 publishing, 2 done, 3 refused, 4 uncertain */
    unsigned count, published;
    int32_t counter_before;
    lce195e_effect records[LCE195E_MAX_EFFECTS];
} lce195e_effect_publisher;

/* Phase: the held post-stock-items window (registry_trace items(), i.e. after the effect
 * initializer 0x4781e0 and its statics 0x47b780, which run before Items::init 0x386070)
 * and before any UI scene is created (the HUD sizes an int[count+1] from 0x14195d0 once).
 * Ids must be 32..127, unused (byId == 0), ascending. Afterwards the count global equals
 * the highest id. Records become visible to the thunks only when the call returns 1. */
int lce195e_effects_publish(lce195e_effect_publisher *p, const lce195e_effect_spec *specs, unsigned count);

/* Effect object of a published mod effect key ("modid:x"), 0 if unknown. */
uintptr_t lce195e_effect_object(const char *key);
int32_t lce195e_effect_id(const char *key);

/* The private-vtable thunks (addresses go into env.thunk[]). They find the record by `this`,
 * call on_tick for slot 0 and forward to the clone's class function with `this` = stock. */
void lce195e_thunk_tick(uintptr_t self, uintptr_t mob_sp, int32_t amplifier);
void lce195e_thunk_instant(uintptr_t self, uintptr_t a, uintptr_t b, uintptr_t mob_sp, int32_t amplifier, double scale);
int lce195e_thunk_ready(uintptr_t self, int32_t duration, int32_t amplifier);
/* What a forwarded slot calls: returns the clone function (0 = none) and sets *self_out to the
 * stock effect. Used by the thunks and by the Unicorn test (which tail-calls the result). */
uintptr_t lce195e_forward_target(uintptr_t self, unsigned slot, uintptr_t *self_out);

/* ======================= potions ========================================== */
typedef struct lce195e_potion_effect { uintptr_t effect; int32_t duration, amplifier; } lce195e_potion_effect;

typedef struct lce195e_potion_spec {
    const char *key;            /* "modid:name": the NBT "Potion" string of every potion item  */
    uint32_t sid[6];            /* names: potion, splash, lingering, tipped arrow; description; arrow description
                                   (0x55e620 reads +0/+4/+8/+0xc by item kind; +0x10/+0x14 are the tooltips) */
    lce195e_potion_effect effects[LCE195E_MAX_POTION_EFFECTS];
    uint32_t effect_count;      /* 0 allowed (a base potion like awkward)                       */
} lce195e_potion_spec;

typedef struct lce195e_potion {
    int32_t id;
    uintptr_t object;
    uintptr_t mei[LCE195E_MAX_POTION_EFFECTS];
    char key[64];
} lce195e_potion;

/* Desc -> spec: resolves effects through lce195e_effect_object / effect_by_id(stock).
 * sids[0..3] are required from the caller (strings195); sid[4] defaults to sid[0],
 * sid[5] to the stock "tipped arrow" tooltip 0x2453270c. */
lce_status lce195e_potion_check(const lce_potion_desc *d, char *err, uint32_t cap);
lce_status lce195e_potion_spec_from_desc(lce195e_env *e, const lce_potion_desc *d, const uint32_t sid[6], lce195e_potion_spec *out, char *err, uint32_t cap);

typedef struct lce195e_potion_publisher {
    lce195e_env *env;
    uint32_t state;
    unsigned count, published;
    int32_t counter_before;
    lce195e_potion records[LCE195E_MAX_POTIONS];
} lce195e_potion_publisher;

/* Phase: after the potion initializer 0x55e6d0 (static chain 0x477ce0), before the brewing
 * publish (items195 lce195x_brew_publish) and before any world loads; startup/title thread
 * with held() == 1 (the composite late phase, ahead of smelt_brew). Ids follow the counter. */
int lce195e_potions_publish(lce195e_potion_publisher *p, const lce195e_potion_spec *specs, unsigned count);

/* Potion id for brewing (items195 lce195x_brew.from_potion/to_potion): a published mod potion
 * key, else a stock "minecraft:" key, else -1. */
int32_t lce195e_potion_id(const char *key);

/* For tests: forget the published tables (host only). */
void lce195e_reset_for_tests(void);

#ifdef __cplusplus
}
#endif
