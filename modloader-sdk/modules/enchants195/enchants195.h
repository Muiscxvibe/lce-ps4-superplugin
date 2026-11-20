/* enchants195 - new enchantments for the retail 1.95 (CUSA00265) image.
 *
 * Image: reconstructed ELF c675e6ce601b6457a1c1466b0da3d9ccd7415f6b0db9f8e9da7b6e511816029f.
 * RVAs and their disassembly evidence are in STATUS.md.
 *
 * A mod enchantment is a real game object:
 *   operator new (0xe6cfd0) + the stock class constructor of its clone
 *   (Enchantment 0x24ab10 / DamageEnchantment 0x1f1a40 / ProtectionEnchantment
 *   0x58d970 / TridentImpalerEnchantment 0x6f6b30), then a PRIVATE copy of the
 *   class vtable (19 slots + RTTI prefix) whose overrides all point at RETAIL
 *   functions (no plugin code is ever called by the game for an enchantment):
 *     +0x18 getMaxLevel   -> a retail "mov $N,%eax; ret" (N = 1..10, pinned)
 *     +0x50 getDescriptionId -> 0x24aeb0 (base: returns +0x10), +0x10 = name_sid
 *     +0x78 isTreasureOnly -> 0x24ae30 (1) / 0x24b090 (0)
 *     +0x80 isCurse        -> 0x24ae30 (1) / 0x24b0a0 (0)
 *     +0x60 canEnchant     -> 0x24b040 (category decides) when a category is set (+0x08)
 *   and registration through the registry's own virtual register (+0x28 of
 *   MappedRegistry vt 0x1255398, object [0x1415448]) at a free numeric id.
 *   Data writes only (heap objects, our vtable, the object's vptr); no .text.
 *
 * Everything that lists enchantments reads the registry live:
 *   enchanting table getAvailableEnchantmentResults 0x24d890 (list at reg+0x108),
 *   Creative books (0xa31da5 in the Creative group ctor), random loot (+0x18),
 *   item NBT / network (short id -> byId +0x40, null-checked at 0x248d13).
 *
 * Phase: after Enchantments bootstrap 0x248fa0 (called at 0x478134, after the
 * item window 0x477cc8) and before the first world: the SDK late title phase.
 *
 * Return convention: 1 done and verified, 0 refused before any game write or
 * allocation, -1 partial/uncertain (keep resident, never retry, report).
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "../strings195/strings195.h"   /* s195_env (memory + native call), s195_native_call */

#ifdef __cplusplus
extern "C" {
#endif

/* ---- pinned RVAs ---------------------------------------------------------- */
#define E195_RVA_REGISTRY        0x1415448u /* MappedRegistry<ResourceLocation, Enchantment*>* (created by static init 0x2d3040) */
#define E195_RVA_REGISTRY_VT     0x1255398u
#define E195_SLOT_CONTAINS       0x20u      /* bool (reg, const RL&)          0x2beb00 */
#define E195_SLOT_REGISTER       0x28u      /* void (reg, int id, const RL&, Enchantment*) 0x2bec20 */
#define E195_SLOT_GET_ID         0x38u      /* int  (reg, Enchantment*)       0x2bef00 */
#define E195_SLOT_BY_ID          0x40u      /* Enchantment* (reg, int)        0x2bf020 (null if out of range) */
#define E195_RVA_RL_CTOR         0xcda2f0u  /* ResourceLocation(this, const wstring&) ("ns:path"; no ns = minecraft) */
#define E195_RVA_RL_DTOR         0xcdab00u
#define E195_RL_SIZE             0x80u      /* object is 0x68 (vptr, +0x18 path, +0x40 namespace wstrings) */
#define E195_RVA_ALLOC           0xe6cfd0u
#define E195_RVA_FREE            0xe6cfb0u
#define E195_RVA_WSTRING         0x67840u
#define E195_RVA_BOOTSTRAP       0x248fa0u  /* Enchantments::bootstrap, called at 0x478134 */

#define E195_RVA_BASE_CTOR       0x24ab10u  /* (this, Rarity*, Category*, EquipmentSlot**, int n) -> 0x30 */
#define E195_RVA_BASE_VT         0x1245d68u
#define E195_RVA_DAMAGE_CTOR     0x1f1a40u  /* (this, Rarity*, int type 0..2, slots, n) -> 0x38, category WEAPON */
#define E195_RVA_DAMAGE_VT       0x1236350u
#define E195_RVA_PROT_CTOR       0x58d970u  /* (this, Rarity*, Type*, slots, n) -> 0x38, FEET if FALL else ARMOR */
#define E195_RVA_PROT_VT         0x12ac170u
#define E195_RVA_IMPALER_CTOR    0x6f6b30u  /* (this, Rarity*, slots, n) -> 0x30, category TRIDENT */
#define E195_RVA_IMPALER_VT      0x12e6680u
#define E195_RVA_PROT_TYPES      0x142ed40u /* Type* globals: ALL, FIRE, FALL, EXPLOSION, PROJECTILE (+8 each) */
#define E195_RVA_RARITIES        0x142f250u /* Rarity* globals COMMON, UNCOMMON, RARE, VERY_RARE (+8); weight at +0x30 (0x59d310) */
#define E195_VT_SLOTS            19u

#define E195_RVA_GET_DESC_FIELD  0x24aeb0u  /* Enchantment::getDescriptionId: return +0x10 */
#define E195_RVA_CAN_ENCHANT_CAT 0x24b040u  /* Enchantment::canEnchant: category(+0x08)->canEnchant(item) */
#define E195_RVA_RET1            0x24ae30u  /* Enchantment::getMinLevel: mov $1,%eax; ret */
#define E195_RVA_TREASURE_FALSE  0x24b090u  /* xor %eax,%eax; ret */
#define E195_RVA_CURSE_FALSE     0x24b0a0u  /* xor %eax,%eax; ret */
#define E195_RVA_GET_FULLNAME    0x24aec0u  /* (wstring* out, ench, level): name + level text */
#define E195_RVA_TABLE_RESULTS   0x24d890u  /* getAvailableEnchantmentResults(cost, II*, allowTreasure) */

/* vtable byte offsets */
#define E195_SLOT_RARITY      0x00u
#define E195_SLOT_MIN_LEVEL   0x10u
#define E195_SLOT_MAX_LEVEL   0x18u
#define E195_SLOT_MIN_COST    0x20u
#define E195_SLOT_MAX_COST    0x28u
#define E195_SLOT_PROTECTION  0x30u
#define E195_SLOT_DAMAGE      0x38u
#define E195_SLOT_COMPATIBLE  0x40u
#define E195_SLOT_DESC_ID     0x50u
#define E195_SLOT_FULLNAME    0x58u
#define E195_SLOT_CAN_ENCHANT 0x60u
#define E195_SLOT_POST_ATTACK 0x68u
#define E195_SLOT_POST_HURT   0x70u
#define E195_SLOT_TREASURE    0x78u
#define E195_SLOT_CURSE       0x80u

/* Ids: stock uses 0..92 with gaps; NBT and packets carry a short. The SDK
 * hands out persistent (ledger) ids in this range. */
#define E195_ID_MIN 100
#define E195_ID_MAX 1023
#define E195_MAX    64u

typedef enum {
    E195_CLONE_NONE = 0,            /* base Enchantment: no built-in effect (scripts / data only) */
    E195_CLONE_SHARPNESS, E195_CLONE_SMITE, E195_CLONE_BANE,   /* DamageEnchantment type 0/1/2: getDamageBonus +0x38 (bane: +0x68 slowness) */
    E195_CLONE_IMPALING,            /* TridentImpalerEnchantment: getDamageBonus +0x38 */
    E195_CLONE_PROTECTION, E195_CLONE_FIRE_PROTECTION, E195_CLONE_FEATHER_FALLING,
    E195_CLONE_BLAST_PROTECTION, E195_CLONE_PROJECTILE_PROTECTION,  /* ProtectionEnchantment: getDamageProtection +0x30 */
    E195_CLONE_COUNT
} e195_clone;

typedef enum {
    E195_CAT_DEFAULT = 0,           /* keep the clone class's category and canEnchant */
    E195_CAT_ALL, E195_CAT_ARMOR, E195_CAT_ARMOR_FEET, E195_CAT_ARMOR_LEGS, E195_CAT_ARMOR_CHEST, E195_CAT_ARMOR_HEAD,
    E195_CAT_WEAPON, E195_CAT_DIGGER, E195_CAT_FISHING_ROD, E195_CAT_BREAKABLE, E195_CAT_BOW, E195_CAT_CROSSBOW,
    E195_CAT_WEARABLE, E195_CAT_TRIDENT, E195_CAT_COUNT
} e195_category;

/* clone_of key ("minecraft:sharpness", "sharpness", NULL/"" = none) -> e195_clone,
 * or -1 with the reason in err (identity-checked stock behaviour). */
int e195_clone_from_key(const char *clone_of, char *err, uint32_t cap);
/* lce_enchant_desc.applies_to (bitmask of 1 << lce_item_kind) -> category. 0 -> DEFAULT. */
uint32_t e195_category_from_applies(uint16_t applies_to);
/* rarity weight 10/5/2/1 -> index 0..3 (COMMON..VERY_RARE); other weights round down to the next stock rarity. */
uint32_t e195_rarity_index(uint8_t weight);
/* retail "mov $N,%eax; ret" function used for getMaxLevel N (1..10), 0 if none. */
uint32_t e195_const_fn(uint32_t n);

typedef struct e195_spec {
    const char *key;                /* "modid:name" (registry key; must be absent) */
    int32_t id;                     /* E195_ID_MIN..E195_ID_MAX, persistent (ledger) */
    uint32_t clone;                 /* e195_clone */
    uint32_t category;              /* e195_category */
    uint8_t max_level;              /* 1..10 */
    uint8_t weight;                 /* 10 common, 5 uncommon, 2 rare, 1 very rare */
    uint8_t treasure, curse;
    uint32_t name_sid;              /* strings195 id (s195_sid_reserved), inserted separately */
} e195_spec;

typedef struct e195_record {
    int state;                      /* 0 none, 1 published, -1 uncertain */
    int32_t id;
    uint32_t clone;
    uintptr_t object;               /* Enchantment* */
    uintptr_t vtable;               /* game-heap copy (vptr = vtable + 16) */
} e195_record;

int e195_check(const e195_spec *s, char *err, uint32_t cap);
/* Publish n enchantments (all checked first; nothing is written if any is
 * refused). out[i] gets each record. 1 all published and verified, 0 refused
 * before any write, -1 partial. */
int e195_publish(s195_env *e, const e195_spec *s, uint32_t n, e195_record *out);
/* Re-verify a published record through the registry (byId/getId and vptr). 1 ok. */
int e195_verify(s195_env *e, const e195_record *r);

#ifdef __cplusplus
}
#endif
