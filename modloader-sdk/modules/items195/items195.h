/* items195 - complete item kinds, recipes (ABI 2) and smelting for the retail
 * CUSA00265 1.95 backend.
 *
 * Image: reconstructed ELF c675e6ce601b6457a1c1466b0da3d9ccd7415f6b0db9f8e9da7b6e511816029f
 * (RX 231a6bfb...c682). Every RVA below is from that image and is cited with
 * the disassembly address that proves it in STATUS.md. Nothing here installs
 * a hook or patches .text. Every native call goes through lce195x_natives,
 * bound from pinned RVAs by lce195x_natives_bind() on the console and by fakes
 * in host tests.
 *
 * Return convention of every publisher/constructor:
 *    1  complete and post-verified
 *    0  refused before the first game write or native allocation
 *   -1  partial/uncertain: native memory may have changed. Keep everything
 *       resident, never retry, report PARTIAL.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "../../include/lce_mod_api.h"
#include "../../loader/backend.h"
#include "../../loader/ext.h"
#include "../../third_party/r72/additive_items_195/catalog.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LCE195X_ABI 2u

/* ---- pinned RVAs ---------------------------------------------------------- */
/* allocation */
#define LCE195X_RVA_ALLOC            0xe6cfd0u  /* operator new(size_t)                         */
#define LCE195X_RVA_ALLOC_ARRAY      0xe6cfc0u  /* operator new[](size_t)                       */
#define LCE195X_RVA_WSTRING          0x67840u   /* wstring ctor (dst, const u16*, n)            */
#define LCE195X_RVA_ITEM_BY_ID       0x385af0u  /* Item* lookup(id)                             */
/* items */
#define LCE195X_RVA_ITEM_CTOR        0x354d80u  /* Item::Item(), 0xa0                           */
#define LCE195X_RVA_ITEM_VT          0x126e638u /* 56 slots                                     */
#define LCE195X_RVA_SNOWBALLITEM_VT  0x12cae00u /* API 1.9 throwables: Item + class id/dtors/use (projectiles195 pins) */
#define LCE195X_RVA_SNOWBALLITEM_USE 0x637970u  /* SnowballItem::use (sret, this, level, sp<Player>*, hand) */
#define LCE195X_RVA_BOATITEM_VT      0x1219f18u /* API 1.10 boat items: BoatItem(this, int type) 0xd7640 = Item ctor + vptr + [+0x9c]=type + stack 1 */
#define LCE195X_RVA_BOATITEM_USE     0xd7c00u   /* BoatItem::use (sret, this, level, sp<Player>*, hand); reads [this+0x9c] at 0xd80f9 */
#define LCE195X_OFF_BOAT_TYPE        0x9cu      /* Boat::Type 0..5 oak spruce birch jungle acacia dark_oak (Items::init 0x399422, 0x3a1f44..0x3a2424) */
#define LCE195X_RVA_SET_MAX_DAMAGE   0x35ecd0u  /* Item::setMaxDamage(item,int): +0x1c + property */
#define LCE195X_RVA_PICKAXE_CTOR     0x52bd50u  /* (item, Tier*) 0xc8, -> DiggerItem 0x1fcfd0   */
#define LCE195X_RVA_PICKAXE_VT       0x129e568u
#define LCE195X_RVA_AXE_CTOR         0x3e3c0u   /* (item, Tier*) 0xc8                           */
#define LCE195X_RVA_AXE_VT           0x120b688u
#define LCE195X_RVA_SHOVEL_CTOR      0x60b060u  /* (item, Tier*) 0xc8                           */
#define LCE195X_RVA_SHOVEL_VT        0x12be6c0u
#define LCE195X_RVA_SWORD_CTOR       0x6cefd0u  /* (item, Tier*) 0xa8, 57 slots                 */
#define LCE195X_RVA_SWORD_VT         0x12de818u
#define LCE195X_RVA_HOE_VT           0x1267b68u /* inline in Items::init 0x395d69..0x395dd6     */
#define LCE195X_RVA_ARMOUR_CTOR      0x32cd0u   /* (item, ArmorMaterial*, int, EquipmentSlot*) 0xd0 */
#define LCE195X_RVA_ARMOUR_VT        0x12095f8u
#define LCE195X_RVA_IRON_MATERIAL    0x14090f8u /* ArmorMaterial* global (iron)                 */
#define LCE195X_RVA_EQUIP_HEAD       0x1415450u /* EquipmentSlot* globals head,chest,legs,feet (+8 each); slot+8 index 3..0 */
#define LCE195X_RVA_ARMOUR_DUR_TABLE 0xe72a00u /* int[4] {13,15,16,11} feet..head, x material+0x28 */
#define LCE195X_RVA_FOOD_CTOR        0x2d69d0u  /* FoodItem(this, int nutrition, float sat, bool meat), 0xb8 */
#define LCE195X_RVA_FOOD_VT          0x12594d8u /* FoodItem, 62 slots                           */
#define LCE195X_RVA_FOOD_SET_EFFECT  0x2f5ff0u  /* FoodItem vt +0x1e0 setEatEffect(this, MobEffectInstance*, float p) */
#define LCE195X_RVA_MEI_CTOR         0x491aa0u  /* MobEffectInstance(this[0x18], MobEffect*, duration, amp) */
#define LCE195X_RVA_TIER_REPAIR      0x3aecf0u  /* Tier::getRepairItem: identity switch on stock tiers */
/* vtable slots (byte offsets) */
#define LCE195X_SLOT_USE             0x40u
#define LCE195X_SLOT_HAND_EQUIPPED   0x88u     /* Item::isHandEquipped */
#define LCE195X_RVA_HAND_EQUIPPED   0x3aef40u
#define LCE195X_OFF_HAND_EQUIPPED   0x30u
#define LCE195X_SLOT_NAME            0x108u     /* stock 0x3af070                               */
#define LCE195X_SLOT_FOIL            0x110u     /* Item::isFoil */
#define LCE195X_RVA_FOIL_STOCK       0x3af180u
#define LCE195X_RVA_FOIL_ALWAYS      0x6276d0u  /* SimpleFoiledItem::isFoil */
#define LCE195X_SLOT_ENCHANT_VALUE   0x138u     /* stock 0x3af3c0 (returns 0); digger 0x1fd370  */
#define LCE195X_SLOT_REPAIR          0x148u     /* isValidRepairItem(this, sp<II>&, sp<II>&); stock 0x3af3e0 */
/* recipes */
#define LCE195X_RVA_STACK_CTOR       0x3b7720u  /* ItemInstance(this, Item*, count, aux), 0xa8  */
#define LCE195X_RVA_SHAPED_CTOR      0x6013d0u  /* (this[0x40], w, h, II** cells, II* result, group) */
#define LCE195X_RVA_SHAPED_VT        0x12bbed8u
#define LCE195X_RVA_SHAPELESS_CTOR   0x602e10u  /* (this[0x30], II* result, vector<II*>* in, group) */
#define LCE195X_RVA_SHAPELESS_VT     0x12bbf58u
#define LCE195X_RVA_OUTPUT_KEY       0x3b86f0u  /* (id << 16) | aux                             */
#define LCE195X_RVA_RECIPE_BUCKET    0x5ab110u  /* unordered_map<int, vector<Recipe*>>::operator[](owner,&key) -> vector* */
#define LCE195X_RVA_RECIPE_VEC_GROW  0x5d3150u  /* vector<Recipe*>::grow(vec, 1)                */
#define LCE195X_RVA_STACK_VEC_GROW   0x5d4130u  /* vector<ItemInstance*>::grow(vec, 1)          */
#define LCE195X_RVA_RECIPES_OWNER    0x142f278u /* Recipes* global                              */
/* furnace */
#define LCE195X_RVA_FURNACE_GLOBAL   0x1416a80u /* FurnaceRecipes*, object 0xf8                 */
#define LCE195X_RVA_FURNACE_RESULT   0x329640u  /* map<II*,II*>::operator[] at +0x00           */
#define LCE195X_RVA_FURNACE_XP       0x329820u  /* map<II*,float>::operator[] at +0x50         */
#define LCE195X_RVA_FURNACE_MASK     0x329a00u  /* map<II*,u32 mask>::operator[] at +0xa8      */
#define LCE195X_RVA_BURN_DURATION    0x320f20u  /* getBurnDuration: Item* identity if-chain (no virtual) */
/* effects / potions / brewing / enchantments */
#define LCE195X_RVA_EFFECT_BY_ID     0x490d60u  /* MobEffect::byId(int) -> registry 0x14195c8 vt+0x40, null if out of range */
#define LCE195X_RVA_POTION_BY_ID     0x55e560u  /* Potion::byId(int) -> registry 0x142e938 vt+0x40 */
#define LCE195X_RVA_POTION_MIXES     0x142e948u /* vector<Mix*> object: +8 begin, +0x10 end, +0x18 cap */
#define LCE195X_RVA_MIX_VEC_GROW     0x583020u  /* reserve space for n more pointers */
#define LCE195X_RVA_MIX_FREE         0xe6cfb0u  /* operator delete(void*) */
#define LCE195X_RVA_MIX_PREDICATE_VT 0x12a62a8u /* PotionBrewing::ItemInstancePredicate {vt, Item*, int aux} 0x18 */
#define LCE195X_RVA_EFFECT_REGISTRY  0x14195c8u
#define LCE195X_RVA_ENCHANT_REGISTRY 0x1415448u

#define LCE195X_MAX_ITEMS     64u
#define LCE195X_MAX_SLOTS     62u
#define LCE195X_RECIPE_MAX    128u
#define LCE195X_SMELT_MAX     64u

/* Tier exactly as read by 0x35ee90 (+4 uses), 0x3aecc0 (+8 speed),
 * 0x35eea0 (+0xc damage), 0x3aece0 (+0x10 enchantment); +0 level. */
typedef struct lce195x_tier { int32_t level, uses; float speed, damage; int32_t enchantment; } lce195x_tier;

typedef struct lce195x_natives {
    void *(*alloc)(size_t);
    void *(*alloc_array)(size_t);
    void (*wstring)(void *dst, const uint16_t *units, uint64_t n);
    uintptr_t (*item_by_id)(unsigned id);
    /* items */
    void (*item_ctor)(void *item);
    void (*set_max_damage)(void *item, int damage);
    void (*pickaxe_ctor)(void *item, lce195x_tier *tier);
    void (*axe_ctor)(void *item, lce195x_tier *tier);
    void (*shovel_ctor)(void *item, lce195x_tier *tier);
    void (*sword_ctor)(void *item, lce195x_tier *tier);
    void (*armour_ctor)(void *item, void *material, int render_index, void *slot);
    void (*food_ctor)(void *item, int nutrition, float saturation, int is_meat);
    void (*food_set_effect)(void *item, void *mei, float probability);
    void (*mei_ctor)(void *mei, void *effect, int duration, int amplifier);
    /* recipes */
    void (*stack_ctor)(void *stack, void *item, int count, int aux);
    void (*shaped_ctor)(void *recipe, int w, int h, void **cells, void *result, int group);
    void (*shapeless_ctor)(void *recipe, void *result, void *ingredients, int group);
    unsigned (*output_key)(void *stack);
    uintptr_t (*recipe_bucket)(uintptr_t owner, const unsigned *key);
    void (*recipe_vec_grow)(uintptr_t vec, int n);
    void (*stack_vec_grow)(uintptr_t vec, int n);
    /* furnace: (map, &key) -> &value */
    uintptr_t (*furnace_map[3])(uintptr_t map, const uintptr_t *key);
    /* effects / brewing */
    uintptr_t (*effect_by_id)(int id);
    uintptr_t (*potion_by_id)(int id);
    void (*mix_vec_grow)(uintptr_t vec, uint64_t n);
    void (*mix_free)(void *object);
} lce195x_natives;

/* Memory access with protection checks; held() is 1 while the caller owns the
 * startup phase (item window / crafting owner callback / furnace ctor). */
typedef struct lce195x_memory {
    void *ctx;
    int (*read)(void *, uintptr_t, void *, size_t);
    int (*write)(void *, uintptr_t, const void *, size_t);
    int (*held)(void *);
} lce195x_memory;

typedef struct lce195x_env {
    uintptr_t base;
    lce195x_natives n;
    lce195x_memory m;
    uint32_t allow_fuel;           /* composite owns fuel publication */
} lce195x_env;

/* Console only: fill natives from pinned RVAs. Never call on the host. */
void lce195x_natives_bind(lce195x_natives *n, uintptr_t base);

/* ======================= items ========================================== */

/* Exactly what the module can build. LCE_OK or LCE_E_UNSUPPORTED with reason.
 * Use this as the backend's check_item for non-generic kinds. */
lce_status lce195x_item_check(const lce_item_desc *d, char *err, uint32_t cap);

/* Catalog kind (catalog.h lce195_item_kind) the desc produces. */
uint32_t lce195x_catalog_kind(const lce_item_desc *d);
/* Catalog max_stack (catalog.c requires 1 for TOOL/ARMOUR). */
uint32_t lce195x_catalog_max_stack(const lce_item_desc *d);

/* Process-lifetime record of one constructed item (keep it static). */
typedef struct lce195x_item {
    uint32_t state;              /* 0 empty, 1 started, 2 done, 3 refused, 4 uncertain */
    uint32_t id, kind, slots;
    const lce_item_desc *desc;
    uintptr_t object, class_vt;  /* native object, stock class vtable (absolute) */
    uintptr_t repair_item;       /* Item* accepted by isValidRepairItem, 0 = stock */
    int32_t enchantability;      /* returned by the +0x138 override (hoe/generic) */
    uint32_t fuel_ticks;         /* recorded only; see lce195x_fuel_check */
    uintptr_t foil_fn;           /* confirmed unconditional stock glint, or 0 */
    lce195x_tier tier;           /* per-item tier the native tool reads through +0xa0/+0xc0 */
    int32_t protections[4];      /* armour material: feet, legs, chest, head (index = slot+8) */
    uintptr_t effect_instance;   /* food: MobEffectInstance handed to setEatEffect */
    uintptr_t orig_enchant, orig_repair;   /* class slots +0x138/+0x148 the dispatchers forward to */
    uint8_t material[0x50] __attribute__((aligned(16)));
    uintptr_t vtable[2 + LCE195X_MAX_SLOTS];   /* private copy: offset-to-top, RTTI, slots */
    uintptr_t use_fn;            /* events195 item-use thunk installed at +0x40 (0 = stock) */
    uintptr_t use_base;          /* API 1.9 throwable: the +0x40 the item starts with (projectiles195 p195_item_use), 0 = class stock */
} lce195x_item;

typedef struct lce195x_item_spec {
    const lce_item_desc *desc;
    uint32_t id;
    const char *icon;            /* ASCII atlas name (<= 31 chars) written to +0x70 */
    uintptr_t name_fn;           /* installed at +0x108 (0 = keep stock) */
    uintptr_t repair_item;       /* resolved Item* of desc->repair_item, 0 = none */
    uintptr_t food_effect;       /* resolved MobEffect* of desc->food_effects[0], 0 = none */
} lce195x_item_spec;

/* Construct inside the held post-stock-items window (provider collect()).
 * Does not register: the shipping lce195_native_items_publish does (0x3af660).
 * The caller adds a catalog entry with kind lce195x_catalog_kind(desc). */
int lce195x_item_construct(lce195x_env *e, lce195x_item *it, const lce195x_item_spec *s);
/* Route slot +0x40 (Item::use, stock 0x3aedb0 for the base class) of a
 * constructed item's PRIVATE vtable to an event thunk (events195
 * lce195_ev_item_use_thunk). Data write into our own table only; identity
 * accepts it afterwards. Call inside the same held window, before the item is
 * registered. 1 routed, 0 refused (unchanged). */
int lce195x_item_route_use(lce195x_env *e, lce195x_item *it, uintptr_t thunk);
/* API 1.9 throwables: a generic item with LCE_ITEM_FLAG_THROWABLE is built as a SnowballItem (same Item ctor and
 * size; private copy of the SnowballItem vtable 0x12cae00) whose +0x40 is `use` (projectiles195 p195_item_use,
 * which runs the stock SnowballItem::use 0x637970). 0 (default) = throwables refused at construct. Process-wide. */
void lce195x_set_throw_use(uintptr_t use);
/* 1 if the object's vptr is still the private table and every slot is intact. */
int lce195x_item_identity(lce195x_env *e, const lce195x_item *it);

/* Private-vtable dispatchers (installed only where an override is needed). */
int lce195x_enchant_dispatch(void *item);
int lce195x_repair_dispatch(void *item, void *sp_a, void *sp_b);

/* Fuel: no data route exists for arbitrary burn times (STATUS.md, fuel row).
 * Returns LCE_OK only for values the native if-chain already produces for this
 * kind (wooden-tier tools/swords/hoes = 200 at base 200), else UNSUPPORTED. */
lce_status lce195x_fuel_check(const lce_item_desc *d, uint32_t burn_ticks, char *err, uint32_t cap);

/* ======================= recipes (ABI 2) ================================= */
#define LCE195X_RECIPE_ABI 2u

typedef struct lce195x_ingredient { uint32_t id; int32_t aux; uintptr_t item; } lce195x_ingredient; /* id 0 = empty; aux -1 = any */

typedef struct lce195x_recipe {
    uint32_t abi, size;
    const char *provider, *key;
    uint32_t shapeless;                  /* 0 shaped (width x height, row-major cropped), 1 shapeless */
    uint32_t width, height;              /* shaped only, 1..3 */
    uint32_t ingredient_count;           /* shapeless only, 1..9 (cells[0..n-1]) */
    uint32_t output_id, output_count;    /* count 1..64 */
    int32_t output_aux;                  /* 0..32767 */
    uint32_t classification;             /* must equal output Item +0x28 */
    uint32_t group;                      /* native recipe group field (0..7) */
    uint32_t category;                   /* Mods crafting tab 0..6 */
    uint32_t shared_output;              /* 1: may join a non-empty output-key bucket (stock semantics) */
    uintptr_t output;
    lce195x_ingredient cells[9];
} lce195x_recipe;

typedef struct lce195x_recipe_publisher {
    lce195x_env *env;
    uintptr_t owner;
    unsigned state, count, published;
    uintptr_t outputs[LCE195X_RECIPE_MAX], inputs[LCE195X_RECIPE_MAX][9], cells[LCE195X_RECIPE_MAX];
    uintptr_t recipes[LCE195X_RECIPE_MAX], buckets[LCE195X_RECIPE_MAX];
    uintptr_t stock[8192];
} lce195x_recipe_publisher;

/* One batch per owner, inside the pre-cache crafting owner callback
 * (steel195_content_on_crafting_scoped, before 0x5a9ef1 -> 0x5aa200). */
int lce195x_recipes_publish(lce195x_recipe_publisher *p, const lce195x_recipe *defs, unsigned count);

/* Provider registry (mirrors r72's, independent of it). */
typedef int (*lce195x_recipe_provider_fn)(void *ctx, uintptr_t owner);
int lce195x_recipe_provider_register(const char *id, void *ctx, lce195x_recipe_provider_fn fn);
/* INTEGRATION: call once right after lce195_recipe_providers_publish(owner). */
int lce195x_recipe_providers_publish(uintptr_t owner);
unsigned lce195x_recipe_provider_status(const char *id);

/* Snapshot of this module's published recipes (never partial). */
unsigned lce195x_recipe_snapshot(uintptr_t owner, uintptr_t *recipes, unsigned *categories, unsigned capacity);
/* r72 snapshot + this module's, never partial. Bind the r72 function first.
 * INTEGRATION: craft_ui_capture.cpp calls this instead of lce195_recipe_snapshot. */
typedef unsigned (*lce195x_snapshot_fn)(uintptr_t, uintptr_t *, unsigned *, unsigned);
void lce195x_bind_r72_snapshot(lce195x_snapshot_fn fn);
unsigned lce195x_recipe_snapshot_all(uintptr_t owner, uintptr_t *recipes, unsigned *categories, unsigned capacity);

/* ======================= smelting ======================================== */
/* Mask bits (0x329ca0 tests byte[type>>3] & 1<<(type&7)): */
#define LCE195X_COOK_FURNACE  1u
#define LCE195X_COOK_BLAST    2u
#define LCE195X_COOK_SMOKER   4u
#define LCE195X_COOK_CAMPFIRE 8u

typedef struct lce195x_smelt {
    uintptr_t input_item; int32_t input_aux;   /* aux -1 = any */
    uintptr_t output_item; uint32_t output_count; int32_t output_aux;
    float xp;                                   /* per item, applied by 0x32a530 */
    uint32_t mask;                              /* LCE195X_COOK_*; 0 = furnace only */
} lce195x_smelt;

typedef struct lce195x_furnace_publisher {
    lce195x_env *env;
    uintptr_t owner;                            /* FurnaceRecipes object (0xf8) */
    unsigned state, count, published;
    uintptr_t inputs[LCE195X_SMELT_MAX], outputs[LCE195X_SMELT_MAX];
    struct { uintptr_t node, key, value; } stock[3][1024];
    uint64_t stock_count[3];
} lce195x_furnace_publisher;

/* Inside the furnace ctor window (after 0x325fd0 returned, before the store
 * of the singleton at 0x325fbb, or any later point before first world load:
 * the maps are only read by linear scans). New entries are front-inserted by
 * the native operator[] and therefore shadow stock entries for the same input. */
int lce195x_furnace_publish(lce195x_furnace_publisher *p, const lce195x_smelt *defs, unsigned count);

/* ======================= effects / potions / brewing / enchantments ====== */
/* Stock ids: effects 1..31 in registration order of 0x4781e0 (speed=1 ..
 * village_hero=31); potions 0..42 in registration order of 0x55e6d0
 * (empty=0, water=1, mundane=2, thick=3, awkward=4, ...). -1 = unknown. */
int32_t lce195x_stock_effect_id(const char *key);
int32_t lce195x_stock_potion_id(const char *key);
/* Mod potions for brewing (effects195 lce195e_potion_id). Check time accepts any non-vanilla key;
 * lce195x_brew_from_plan resolves it after the potions phase. NULL (default): stock only. */
void lce195x_set_mod_potion_resolver(int32_t (*fn)(const char *key));
int32_t lce195x_brew_potion_id(const char *key);   /* stock id, mod id (after publish), or -1 */

lce_status lce195x_effect_check(const lce_effect_desc *d, char *err, uint32_t cap);
lce_status lce195x_potion_check(const lce_potion_desc *d, char *err, uint32_t cap);
lce_status lce195x_brew_check(const lce_brew_desc *d, char *err, uint32_t cap);
lce_status lce195x_enchant_check(const lce_enchant_desc *d, char *err, uint32_t cap);
/* backend check_ext hook for every lce_ext_type (loader/ext.h). */
lce_status lce195x_check_ext(uint32_t type, const void *desc, char *err, uint32_t cap);
/* LCE_CAP_* this module can really back on 1.95. */
uint32_t lce195x_capabilities(void);

typedef struct lce195x_brew {
    int32_t from_potion, to_potion;   /* retail Potion::byId ids (stock or registered SDK potions) */
    uintptr_t reagent;                /* Item* */
    int32_t reagent_aux;              /* -1 any (stock uses -1) */
} lce195x_brew;

typedef struct lce195x_brew_publisher {
    lce195x_env *env;
    unsigned state, count, published;
    uintptr_t predicates[LCE195X_SMELT_MAX], mixes[LCE195X_SMELT_MAX];
    uintptr_t stock[1024];
    uint64_t stock_count;
} lce195x_brew_publisher;

/* Phase: after PotionBrewing setup 0x562e30 (static chain 0x47812f), on the
 * startup thread, before the first world load. The console-proven Creative
 * group hook (0xa33027 inside 0xa25790, reached from 0xc800b7) runs after it. */
int lce195x_brew_publish(lce195x_brew_publisher *p, const lce195x_brew *defs, unsigned count);

/* ======================= plan adapters ==================================== */
/* Convert core plan entries (numeric ids already resolved) into publisher
 * descriptors; items are looked up through env->n.item_by_id. 1 ok, 0 refused. */
int lce195x_recipe_from_plan(lce195x_env *e, const lce_plan_recipe *r, const char *provider, lce195x_recipe *out);
int lce195x_smelt_from_plan(lce195x_env *e, const lce_plan_smelt *s, lce195x_smelt *out);
/* LCE_EXT_BREW plan entry -> lce195x_brew (reagent via plan->resolve(LCE_REF_ITEM) + item_by_id). */
int lce195x_brew_from_plan(lce195x_env *e, const lce_plan *plan, const lce_plan_ext *x, lce195x_brew *out);

#ifdef __cplusplus
}
#endif
