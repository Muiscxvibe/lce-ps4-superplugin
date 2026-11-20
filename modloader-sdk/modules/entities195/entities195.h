/* entities195 - custom entity types for Minecraft PS4 Edition 1.95 (CUSA00265).
 * Every RVA is from the 1.95 analysis image c675e6ce...029f and is checked by
 * tests/audit.py (never mix with 2.72 addresses). Evidence levels: STATUS.md.
 *
 * Model: a custom mob is a CLONE of a vanilla LivingEntity class.
 *   - Type identity. EntityIO keys everything on the 64-bit eINSTANCEOF value
 *     returned by Entity vtable slot +0x60 (GetType). instanceof is bitwise
 *     containment ((type & S) == S, 465 inline sites); exact checks are 64-bit
 *     compares. A custom type gets mask = base_mask | (k+1) << 34: it passes
 *     every instanceof test the base passes (no vanilla mask uses bits >= 34;
 *     only Pillager/Endermite/Ravager use bits 32/33) and is unique for the
 *     registry, save id, factory map and renderer map.
 *   - Registration. The stock registrar 0x28d9c0(factory, id, &name, mask,
 *     &oldName) is called once per type (same call the initializer 0x28dc10
 *     makes 96 times): REGISTRY 0x1415fa8 (+0x90 mask->id, +0xe8 id->mask
 *     vector, +0x100 mask->ResourceLocation), KNOWN set 0x14160f8, old-name
 *     vector 0x1416198 and class->factory map 0x1416148. Save writes the key
 *     of GetType() (0x27bb60), load maps the key back to the mask and calls our
 *     factory (0x28d290 -> 0x283770), AddMob (packet 24, VarInt type) creates by
 *     numeric id (0x28d1b0), natural spawning creates by mask (0x283770).
 *   - Factory thunk. Entity *thunk(Level*) calls the vanilla factory, checks
 *     the product's vptr is the pinned vanilla vtable, swaps it to a private
 *     copy (RTTI prefix + every primary slot copied; +0x60 GetType and the
 *     optional sound slots replaced), then applies attributes and extra goals.
 *   - Renderer. EntityRenderDispatcher::getRenderer 0xbd54c0 does an UNCHECKED
 *     map find for living masks, so every custom mask MUST be in the renderer
 *     map (dispatcher+0, operator[] 0xbd5220) before such an entity renders.
 *
 * Phases (lead calls each exactly once, in this order; never from a hook that
 * can run concurrently with world ticking):
 *   1. le195_configure()          any time after attestation. Pure validation.
 *   2. le195_publish_types()      startup thread, after the registry initializer
 *                                 0x28dc10 returned (call site 0x477d1b) and
 *                                 before any Level exists.
 *   3. le195_publish_spawns()     startup thread, after the biome static init
 *                                 (Biome::biomes 0x1409fc0 filled) and before any
 *                                 Level exists. Optional.
 *   4. le195_publish_renderers()  render/main thread, after Minecraft+0x220 holds
 *                                 the dispatcher (0xc72b0f) and before any world
 *                                 is entered. If it does not return 1 the lead
 *                                 must keep world entry disabled (a custom mob in
 *                                 a save would crash 0xbd54c0).
 * Return convention: 1 done, 0 refused before any game-visible write, -1
 * partial/uncertain (keep storage, never retry, block world entry). */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "../../include/lce_mod_api.h"
#include "../../loader/backend.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- pinned 1.95 RVAs ------------------------------------------------------ */
#define LE195_RVA_ALLOC          0xe6cfd0u  /* operator new */
#define LE195_RVA_FREE           0xe6cfb0u  /* operator delete */
#define LE195_RVA_WSTRING        0x67840u   /* wstring assign(dst[0x28], const u16*, n) */
#define LE195_RVA_REGISTER       0x28d9c0u  /* EntityIO::setId(factory, int id, const wstring& name, u64 mask, const wstring& oldName) */
#define LE195_RVA_REGISTER_INIT  0x28dc10u  /* stock initializer (96 calls of 0x28d9c0), called from 0x477d1b */
#define LE195_RVA_REGISTER_EGG   0x28cba0u  /* EggInfo* (out[0x98], u32 IDS hash, const wstring& name, colour1, colour2, 0) */
#define LE195_RVA_KEY_DTOR       0xcdab00u  /* ResourceLocation dtor (EggInfo out+8) */
#define LE195_RVA_REGISTRY       0x1415fa8u /* EntityIO REGISTRY object (static) */
#define LE195_REG_ID_VEC         0xe8u      /* {begin,end,cap} of u64 masks indexed by numeric id */
#define LE195_RVA_FACTORY_MAP    0x1416148u /* unordered_map<u64 mask, Entity*(*)(Level*)> */
#define LE195_RVA_EGG_MAP        0x1415e78u /* unordered_map<ResourceLocation, EggInfo*> */
#define LE195_RVA_CREATE_BY_MASK 0x283770u  /* (out pair, mask, level): requires mask bit 27 */
#define LE195_RVA_SAVE_ID        0x27bb60u  /* getEncodeId: REGISTRY+0x100 lookup of GetType() */
#define LE195_RVA_RENDER_INDEX   0xbd5220u  /* EntityRenderer*& operator[](dispatcher_map, const u64& mask) */
#define LE195_RVA_GET_RENDERER   0xbd54c0u  /* getRenderer(dispatcher, mask): unchecked find for living masks */
#define LE195_MC_DISPATCHER_OFF  0x220u     /* Minecraft+0x220 = EntityRenderDispatcher* (0x140 bytes, ctor 0xbd36c0) */
#define LE195_RVA_ADD_GOAL       0x2d6260u  /* GoalSelector::addGoal(sel, int prio, Goal*, bool canDelete) */
#define LE195_GOAL_SELECTOR_OFF  0x800u     /* Mob+0x800 goalSelector */
#define LE195_TARGET_SELECTOR_OFF 0x850u    /* Mob+0x850 targetSelector */
#define LE195_RVA_TI_PLAYER      0x12a5440u /* typeinfo for Player (LookAtPlayer / NearestAttackableTarget arg) */
/* goal constructors (this, mob, ...) and allocation sizes, as Creeper/Pig registerGoals 0x1e9120/0x52d0d0 call them */
#define LE195_RVA_GOAL_FLOAT     0x2e9180u  /* FloatGoal(g,mob)                                 0x18 */
#define LE195_RVA_GOAL_STROLL    0x77f550u  /* WaterAvoidingRandomStrollGoal(g,mob,double)      0x48 */
#define LE195_RVA_GOAL_LOOK      0x43a0c0u  /* LookAtPlayerGoal(g,mob,ti,float,float,int,int)   0x48 */
#define LE195_RVA_GOAL_LOOK_AROUND 0x5985d0u /* RandomLookAroundGoal(g,mob)                    0x30 */
#define LE195_RVA_GOAL_MELEE     0x462f20u  /* MeleeAttackGoal(g,mob,double,bool)               0x60 */
#define LE195_RVA_GOAL_TARGET    0x4a7970u  /* NearestAttackableTargetGoal(g,mob,ti,1,0,1)      0x80 */
#define LE195_RVA_GOAL_HURT_BY   0x370ab0u  /* HurtByTargetGoal(g,mob,0,0,0)                    0x58 */
#define LE195_RVA_GOAL_AVOID     0x3d290u   /* AvoidEntityGoal(g,mob,ti,float,double,double,float) 0x68 */
#define LE195_RVA_GOAL_BREED     0xe2950u   /* BreedGoal(g,mob,double)                          0x48 (Animal only) */
/* entity virtual slots (byte offsets in the primary vtable) */
#define LE195_SLOT_GET_TYPE      0x60u
#define LE195_SLOT_SET_HEALTH    0x9c0u     /* setHealth(float) */
#define LE195_SLOT_MAX_HEALTH    0xa90u     /* float getMaxHealth() = 0x427e20 */
#define LE195_SLOT_GET_ATTRIBUTE 0xab8u     /* AttributeInstance* getAttribute(const Attribute*) */
#define LE195_SLOT_SOUND_HURT    0xa18u     /* SoundEvent* getHurtSound(...) */
#define LE195_SLOT_SOUND_DEATH   0xa20u     /* SoundEvent* getDeathSound() */
#define LE195_SLOT_SOUND_AMBIENT 0xd70u     /* SoundEvent* getAmbientSound() (Mob) */
#define LE195_ATTR_SET_BASE      0x20u      /* ModifiableAttributeInstance vtable 0x1287fd0: setBaseValue(double) */
#define LE195_RVA_ATTR_MAX_HEALTH 0x142ff40u /* Attribute* globals from SharedMonsterAttributes init 0x61d8c3 */
#define LE195_RVA_ATTR_SPEED     0x142ff58u
#define LE195_RVA_ATTR_ATTACK    0x142ff68u
/* biome spawn lists */
#define LE195_RVA_BIOMES         0x1409fc0u /* Biome* Biome::biomes[256] (BSS) */
#define LE195_RVA_SPAWNER_VT     0x1214758u /* Biome::MobSpawnerData primary vtable */
#define LE195_SPAWNER_SIZE       0x48u      /* {vt, +8 weight, +0x10 u64 mask, +0x18 min, +0x1c max, +0x20 string} */
#define LE195_LIST_MONSTER       0x78u      /* vector<MobSpawnerData*> {begin,end,cap} in Biome */
#define LE195_LIST_CREATURE      0x98u
#define LE195_LIST_WATER         0x118u
#define LE195_LIST_AMBIENT       0x178u

#define LE195_MASK_LIVING        UINT64_C(0x0c000000)
#define LE195_MASK_MONSTER       UINT64_C(0x4d540000)
#define LE195_MASK_ANIMAL        UINT64_C(0x0d680000)
#define LE195_CUSTOM_SHIFT       34u
#define LE195_MAX_TYPES          16u
#define LE195_MAX_SLOTS          540u       /* largest pinned primary vtable is 532 */
#define LE195_FIRST_ID           150u       /* 150..199 unused by the 96 vanilla registrations */
#define LE195_MAX_GOALS          8u

typedef struct {
    const char *name, *old_name;
    uint32_t id;
    uint64_t mask;
    uint32_t factory, vtable, slots, size;
    uint32_t egg_code, egg_c1, egg_c2;     /* 0 = no vanilla egg */
} le195_base_mob;
extern const le195_base_mob le195_base_mobs[];
extern const uint32_t le195_base_mob_count;
/* "minecraft:zombie", "zombie" or a Java alias ("iron_golem", "cod", "evoker"...): row or NULL. */
const le195_base_mob *le195_base_lookup(const char *key);

typedef void *(*le195_factory_fn)(void *level);
typedef struct {
    void *(*alloc)(size_t);
    void (*free)(void *);
    void (*wstring)(void *dst, const uint16_t *units, uint64_t n);
    void (*register_type)(le195_factory_fn factory, int id, const void *name, uint64_t mask, const void *old_name);
    void (*register_egg)(void *out, uint32_t code, const void *name, uint32_t c1, uint32_t c2, uint32_t extra);
    void (*key_dtor)(void *key);
    void **(*render_index)(void *dispatcher_map, const uint64_t *mask);
    void (*add_goal)(void *selector, int priority, void *goal, int can_delete);
    void (*goal_float)(void *g, void *mob);
    void (*goal_stroll)(void *g, void *mob, double speed);
    void (*goal_look)(void *g, void *mob, const void *ti, float dist, float prob, int a, int b);
    void (*goal_look_around)(void *g, void *mob);
    void (*goal_melee)(void *g, void *mob, double speed, int track);
    void (*goal_target)(void *g, void *mob, const void *ti, int must_see, int must_reach, int c);
    void (*goal_hurt_by)(void *g, void *mob, void *a, void *b, void *c);
    void (*goal_avoid)(void *g, void *mob, const void *ti, float dist, double walk, double sprint, float f);
    void (*goal_breed)(void *g, void *mob, double speed);
} le195_natives;

typedef struct {
    void *ctx;
    int (*read)(void *, uintptr_t, void *, size_t);
    int (*write)(void *, uintptr_t, const void *, size_t);   /* 8 aligned bytes: one atomic store */
} le195_memory;

typedef struct {
    uint8_t texture_fallback;  /* 1: a custom skin texture is ignored and the base renderer is shared
                                  (custom skin upload is not implemented; STATUS.md). 0: refuse. */
    uint8_t reserved[3];
} le195_options;

enum { LE195_IDLE = 0, LE195_CONFIGURED, LE195_TYPES_DONE, LE195_REFUSED, LE195_UNCERTAIN };

typedef struct {
    const lce_entity_desc2 *desc;
    const le195_base_mob *base;
    uint32_t id;                 /* numeric EntityIO id (150+) */
    uint64_t mask;
    uint32_t list_off;           /* spawn list offset by category */
    uint32_t sound_slot_src[3];  /* vanilla vtable RVA supplying hurt/death/ambient, 0 = keep */
    uint32_t goal_count;
    lce_ai_goal goals[LE195_MAX_GOALS];
    uintptr_t goal_ti[LE195_MAX_GOALS];  /* typeinfo RVA for AVOID/TARGET params, 0 = Player */
    uint32_t created, mismatched, attr_missing, goal_failed;
} le195_type;

typedef struct le195_module {
    uintptr_t base;
    le195_natives n;
    le195_memory mem;
    le195_options opt;
    uint32_t state, types_state, spawns_state, renderers_state;
    uint32_t count;
    le195_type types[LE195_MAX_TYPES];
    char last_error[160];
} le195_module;

/* Validation of one descriptor (backend check_ext for LCE_EXT_ENTITY). */
lce_status le195_check_entity(const lce_entity_desc2 *d, const le195_options *opt, char *err, uint32_t cap);
/* Phase 1. Copies natives/memory; validates every LCE_EXT_ENTITY entry of the plan;
 * assigns ids 150+ (plan order) and masks. Sets plan ext ids. 1 / 0. */
int le195_configure(le195_module *m, uintptr_t base, const le195_natives *n, const le195_memory *mem,
                    const le195_options *opt, lce_plan *plan, char *err, uint32_t cap);
/* Phase 2. 1 / 0 (registry not ready, slot taken: nothing written) / -1. */
int le195_publish_types(le195_module *m);
/* Phase 3. biome_id maps a biome key to its Biome::biomes index (worldgen195:
 * lce_wg195_biome_lookup), or NULL to allow only "base mob biomes" types. */
int le195_publish_spawns(le195_module *m, int32_t (*biome_id)(const char *key));
/* Phase 4. dispatcher = *(Minecraft+0x220). */
int le195_publish_renderers(le195_module *m, uintptr_t dispatcher);

/* The spawned object must have this exact primary vtable before insertion.
 * Returns zero until renderers are published or for a descriptor outside the plan. */
uintptr_t le195_spawn_vtable(const lce_entity_desc2 *desc);
int32_t le195_type_id(const le195_module *m, const char *key);
uint64_t le195_type_mask(const le195_module *m, const char *key);
/* The GetType override installed at +0x60 of every private vtable. */
uint64_t le195_get_type(const void *self);
/* Factory thunk for type index i (tests). */
le195_factory_fn le195_factory(uint32_t i);
/* How a thunk calls the vanilla factory at base+RVA (set by le195_natives_bind
 * on the console; tests route it to fakes). */
extern void *(*le195_call_factory)(uintptr_t factory, void *level);
/* Body of every factory thunk (exported for tests). */
void *le195_create_impl(uint32_t k, void *level);
/* Fill natives from the pinned RVAs (console only). */
void le195_natives_bind(le195_natives *n, uintptr_t base);

#ifdef __cplusplus
}
#endif
