/*
 * lce_mod_api.h - Stable C ABI for Minecraft PS4 Edition 1.95 (CUSA00265) mods.
 *
 * Versioning rules (see API_STATUS.md, "ABI policy"):
 *   - LCE_MOD_API_MAJOR changes only on an incompatible change. The loader
 *     refuses a mod whose major differs from its own.
 *   - LCE_MOD_API_MINOR grows when functions/fields are appended. A mod built
 *     against minor N runs on a loader with minor >= N. Every descriptor starts
 *     with `uint32_t struct_size`; the loader reads only the fields that fit,
 *     and treats missing trailing fields as zero/default.
 *   - Function tables are append-only; `lce_api.struct_size` tells a mod which
 *     entries exist. Never reorder or remove fields.
 *
 * Two ways to write a mod:
 *   1. Data-only package (recommended; covers most Java/Bedrock content ports):
 *      a compiled mod.lcm produced by tools/lce_modpack.py. No native code.
 *   2. Native mod (EXPERIMENTAL, off by default): a PRX exporting
 *      `lce_mod_main`, which receives `const lce_api *` and registers content
 *      with the same descriptors, plus event subscriptions.
 *
 * Both paths go through one validation + staging layer. Nothing reaches game
 * memory until the whole transaction (every enabled mod) validated; on any
 * failure the loader registers nothing (fail-closed) and reports why.
 *
 * Content keys: "<modid>:<name>", lowercase [a-z0-9_./-]. Vanilla content is
 * referenced as "minecraft:<name>" using the 1.95 resource names (see
 * tools/data/vanilla_195.json) or "minecraft:#<numeric id>".
 */
#ifndef LCE_MOD_API_H
#define LCE_MOD_API_H
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LCE_MOD_API_MAJOR 1
#define LCE_MOD_API_MINOR 12
#define LCE_MOD_API_VERSION ((LCE_MOD_API_MAJOR << 16) | LCE_MOD_API_MINOR)
#define LCE_MOD_TARGET_GAME "CUSA00265-1.95"
#define LCE_MOD_ENTRY_SYMBOL "lce_mod_main"

#define LCE_MAX_KEY 64     /* bytes incl. NUL, e.g. "examplemod:ruby_block" */
#define LCE_MAX_MODID 32
#define LCE_MAX_TEXT 128   /* UTF-8 display strings */

/* ---- status codes ------------------------------------------------------ */
typedef enum {
    LCE_OK = 0,
    LCE_E_INVALID = -1,      /* malformed descriptor / package */
    LCE_E_COLLISION = -2,    /* key or numeric ID already owned */
    LCE_E_PHASE = -3,        /* call not allowed in the current lifecycle phase */
    LCE_E_UNSUPPORTED = -4,  /* backend lacks this capability on this build */
    LCE_E_FULL = -5,         /* reserved ID range / capacity exhausted */
    LCE_E_BACKEND = -6,      /* game-side guard failed (pin/sig mismatch) */
    LCE_E_IO = -7,
    LCE_E_HASH = -8,         /* package or ledger integrity failure */
    LCE_E_VERSION = -9,      /* api major / target game mismatch */
    LCE_E_NOTFOUND = -10,    /* unresolved reference (item key, texture...) */
    LCE_E_LEDGER = -11       /* persistent ID ledger unreadable: fail closed */
} lce_status;

/* ---- lifecycle --------------------------------------------------------- */
typedef enum {
    LCE_PHASE_NONE = 0,
    LCE_PHASE_DISCOVER,   /* scan /data/lce-mods/<id>/, parse + verify packages */
    LCE_PHASE_INIT,       /* native lce_mod_main() runs; may subscribe to events */
    LCE_PHASE_REGISTER,   /* register_* calls accepted (staged, not yet live) */
    LCE_PHASE_COMMIT,     /* loader-only: IDs allocated, backend publishes */
    LCE_PHASE_POST_INIT,  /* content live; resolve() returns numeric IDs */
    LCE_PHASE_RUNNING,    /* ticks and gameplay events flow */
    LCE_PHASE_FAILED      /* fail-closed: nothing registered, events disabled */
} lce_phase;

/* ---- backend capability bits (lce_api.capabilities) -------------------- */
#define LCE_CAP_ITEMS        (1u << 0)
#define LCE_CAP_BLOCKS       (1u << 1)
#define LCE_CAP_RECIPES      (1u << 2)
#define LCE_CAP_SMELTING     (1u << 3)
#define LCE_CAP_CRAFT_TABS   (1u << 4)
#define LCE_CAP_CREATIVE     (1u << 5)
#define LCE_CAP_TEXTURES     (1u << 6)
#define LCE_CAP_STRINGS      (1u << 7)
#define LCE_CAP_ENTITIES     (1u << 8)
#define LCE_CAP_WORLDGEN     (1u << 9)
#define LCE_CAP_SOUNDS       (1u << 10)
#define LCE_CAP_EV_TICK      (1u << 11)
#define LCE_CAP_EV_BLOCK     (1u << 12)
#define LCE_CAP_EV_ITEM_USE  (1u << 13)
#define LCE_CAP_EV_WORLD     (1u << 14)
#define LCE_CAP_COMMANDS     (1u << 15)
#define LCE_CAP_NET_SYNC     (1u << 16)
/* API 1.1 */
#define LCE_CAP_BLOCK_ENTITY (1u << 17)
#define LCE_CAP_TOOLS        (1u << 18)  /* tool/weapon item kinds */
#define LCE_CAP_ARMOUR       (1u << 19)
#define LCE_CAP_FOOD         (1u << 20)
#define LCE_CAP_EFFECTS      (1u << 21)  /* status effects + potions */
#define LCE_CAP_BREWING      (1u << 22)
#define LCE_CAP_ENCHANTS     (1u << 23)
#define LCE_CAP_FUEL         (1u << 24)
#define LCE_CAP_BIOMES       (1u << 25)
#define LCE_CAP_STRUCTURES   (1u << 26)
#define LCE_CAP_PARTICLES    (1u << 27)
#define LCE_CAP_KEYBINDS     (1u << 28)
#define LCE_CAP_EV_ENTITY    (1u << 29)  /* damage/death */
#define LCE_CAP_EV_PLAYER    (1u << 30)  /* join/leave/chat */
#define LCE_CAP_SERVICES     (1u << 31)  /* runtime world services (give, set_block, spawn, chat) */

/* API 1.2: second capability word (lce_api.capabilities2). One bit per event source / service, because
 * a backend can serve some of them without the others. */
#define LCE_CAP2_EV_BLOCK_USE       (1u << 0)
#define LCE_CAP2_EV_ENTITY_INTERACT (1u << 1)
#define LCE_CAP2_EV_BLOCK_TICK      (1u << 2)
#define LCE_CAP2_EV_ENTITY_TICK     (1u << 3)
#define LCE_CAP2_EV_CONTEXT         (1u << 4)   /* the API 1.2 payload fields (level, position, held item) are filled */
#define LCE_HAVE_EV_1_3 1
#define LCE_CAP2_EV_INVENTORY_TICK  (1u << 5)   /* API 1.3: LCE_EV_ITEM_INVENTORY_TICK */
#define LCE_CAP2_EV_NEIGHBOR        (1u << 6)   /* API 1.3: LCE_EV_BLOCK_NEIGHBOR */
#define LCE_CAP2_EV_BLOCK_COLLIDE   (1u << 7)   /* API 1.3: LCE_EV_BLOCK_ENTITY_INSIDE + LCE_EV_BLOCK_STEP_ON */
#define LCE_CAP2_SVC_EXPLODE        (1u << 8)
#define LCE_CAP2_SVC_FIRE           (1u << 9)
#define LCE_CAP2_SVC_DAMAGE         (1u << 10)
#define LCE_CAP2_SVC_HEAL           (1u << 11)
#define LCE_CAP2_SVC_HELD_ITEM      (1u << 12)  /* damage_held_item / consume_held_item */
#define LCE_CAP2_SVC_TELEPORT       (1u << 13)
#define LCE_CAP2_SVC_MOTION         (1u << 14)
#define LCE_CAP2_SVC_WORLD          (1u << 15)  /* give_item / set_block / get_block / spawn_entity bound to the world helpers */
#define LCE_HAVE_SVC_1_3 1
#define LCE_CAP2_SVC_PARTICLES      (1u << 16)  /* API 1.3: spawn_particles */
#define LCE_CAP2_SVC_ITEM_NBT       (1u << 17)  /* API 1.4: item_tag_get / item_tag_set / item_tag_remove */
#define LCE_HAVE_NET_1_4 1
#define LCE_CAP2_EV_PAYLOAD         (1u << 18)  /* API 1.4: LCE_EV_CUSTOM_PAYLOAD */
#define LCE_CAP2_SVC_PAYLOAD        (1u << 19)  /* API 1.4: send_payload */
#define LCE_HAVE_ENTITY_NBT_1_5 1
#define LCE_CAP2_SVC_ENTITY_NBT     (1u << 20)  /* API 1.5: entity_nbt_get */
#define LCE_CAP2_SVC_ENTITY_DATA    (1u << 21)  /* API 1.5: entity_data_get / entity_data_set / entity_data_remove */
#define LCE_HAVE_NET_1_6 1
#define LCE_CAP2_EV_CLIENT_PAYLOAD  (1u << 22)  /* API 1.6: LCE_EV_CLIENT_PAYLOAD (server -> this client, client thread) */
#define LCE_CAP2_SVC_SEND_TO_SERVER (1u << 23)  /* API 1.6: send_to_server */
#define LCE_HAVE_BLOCK_STATES_1_7 1
#define LCE_CAP2_BLOCK_STATES       (1u << 24)  /* API 1.7: lce_block_desc variant_count / shape / box */
#define LCE_HAVE_CONFIG_1_8 1
#define LCE_CAP2_CONFIG             (1u << 25)  /* API 1.8: config_get / config_int / config_double / config_string */
#define LCE_HAVE_PROJECTILES_1_9 1
#define LCE_CAP2_PROJECTILES        (1u << 26)  /* API 1.9: LCE_ITEM_FLAG_THROWABLE items + LCE_EV_PROJECTILE_HIT */
#define LCE_HAVE_BOATS_1_10 1
#define LCE_CAP2_BOATS              (1u << 27)  /* API 1.10: LCE_ITEM_FLAG_BOAT items (place a stock boat of boat_type) */
#define LCE_HAVE_BLOCK_BOXES_1_11 1
#define LCE_CAP2_BLOCK_BOXES        (1u << 28)  /* API 1.11: lce_block_desc shape LCE_SHAPE_BOXES (multi-box blocks) */
#define LCE_HAVE_TINTED_PARTICLES_1_12 1
#define LCE_CAP2_SVC_TINTED_PARTICLES (1u << 29) /* API 1.12: spawn_tinted_particles */

/* ---- content descriptors ---------------------------------------------- */
typedef enum {
    LCE_ITEM_GENERIC = 0, LCE_ITEM_FOOD, LCE_ITEM_SWORD, LCE_ITEM_PICKAXE,
    LCE_ITEM_AXE, LCE_ITEM_SHOVEL, LCE_ITEM_HOE, LCE_ITEM_HELMET,
    LCE_ITEM_CHESTPLATE, LCE_ITEM_LEGGINGS, LCE_ITEM_BOOTS, LCE_ITEM_KIND_COUNT
} lce_item_kind;

typedef enum { LCE_TIER_NONE = 0, LCE_TIER_WOOD, LCE_TIER_STONE, LCE_TIER_IRON, LCE_TIER_DIAMOND, LCE_TIER_GOLD } lce_tool_tier;

#define LCE_ITEM_FLAG_HANDHELD   (1u << 0)  /* render like a tool */
#define LCE_ITEM_FLAG_FOIL       (1u << 1)  /* enchant glint */
#define LCE_ITEM_FLAG_FOOD_ALWAYS (1u << 2) /* edible when not hungry */
#define LCE_ITEM_FLAG_THROWABLE  (1u << 3)  /* API 1.9: use throws a projectile (generic items; renders/saves as a snowball) */
#define LCE_ITEM_FLAG_BOAT       (1u << 4)  /* API 1.10: use places a stock boat of boat_type (generic items; the boat drops the stock boat item) */

typedef struct {
    uint32_t struct_size;            /* sizeof(lce_item_desc) */
    const char *key;                 /* "modid:ruby" */
    const char *name;                /* default (en) display name, UTF-8 */
    const char *texture;             /* texture key registered via register_texture */
    const char *creative_tab;        /* "minecraft:materials" or a mod tab key; NULL = mod tab */
    uint16_t kind;                   /* lce_item_kind */
    uint16_t max_stack;              /* 1..64; 0 = 64 */
    uint16_t max_damage;             /* tools/armor durability; 0 = unbreakable/not damageable */
    uint16_t tier;                   /* lce_tool_tier for tools */
    uint16_t food_hunger;            /* LCE_ITEM_FOOD: half-shanks */
    uint16_t food_saturation_milli;  /* saturation modifier * 1000 */
    uint32_t flags;
    int32_t attack_bonus_milli;      /* weapon damage bonus * 1000 */
    /* ---- API 1.1 (zero = default) ---- */
    uint16_t armor_points;           /* armour: protection points (leather helmet = 1) */
    uint16_t enchantability;         /* 0 = tier default */
    const char *armor_texture;       /* armour: 64x32 layer texture key (atlas 2) */
    const char *repair_item;         /* anvil repair material, NULL = none */
    uint32_t fuel_ticks;             /* furnace burn time (coal = 1600), 0 = not fuel */
    uint32_t use_ticks;              /* food: eating duration, 0 = 32 */
    const struct lce_effect_ref *food_effects; /* effects applied when eaten */
    uint32_t food_effect_count;
    uint32_t mining_speed_milli;     /* tools: 0 = tier default */
    /* ---- API 1.9 throwables (LCE_ITEM_FLAG_THROWABLE; zero = default) ---- */
    uint32_t projectile_speed_milli;       /* 0 = 1500 (snowball) */
    uint32_t projectile_inaccuracy_milli;  /* 0 = 1000 */
    uint32_t projectile_damage_milli;      /* damage to the hit entity, 0 = none */
    uint32_t projectile_fire_ticks;        /* sets the hit entity on fire, 0 = none */
    uint32_t projectile_explode_milli;     /* explosion radius at the impact, 0 = none (max 8000) */
    const struct lce_effect_ref *projectile_effects;   /* applied to the hit entity */
    uint32_t projectile_effect_count;      /* 0..8 */
    /* ---- API 1.10 boat items (LCE_ITEM_FLAG_BOAT) ---- */
    uint32_t boat_type;                    /* 0 oak, 1 spruce, 2 birch, 3 jungle, 4 acacia, 5 dark oak; must be 0 without the flag */
} lce_item_desc;

typedef enum {
    LCE_MAT_STONE = 0, LCE_MAT_WOOD, LCE_MAT_METAL, LCE_MAT_DIRT, LCE_MAT_SAND,
    LCE_MAT_GLASS, LCE_MAT_PLANT, LCE_MAT_WOOL, LCE_MAT_COUNT
} lce_block_material;

typedef enum { LCE_SOUND_STONE = 0, LCE_SOUND_WOOD, LCE_SOUND_GRAVEL, LCE_SOUND_GRASS, LCE_SOUND_METAL, LCE_SOUND_GLASS, LCE_SOUND_CLOTH, LCE_SOUND_SAND, LCE_SOUND_COUNT } lce_block_sound;

#define LCE_BLOCK_FLAG_TRANSPARENT (1u << 0)
#define LCE_BLOCK_FLAG_NO_ITEM     (1u << 1)  /* no block-item is registered */

typedef struct {
    uint32_t struct_size;
    const char *key;
    const char *name;
    const char *texture[6];          /* faces: down, up, north, south, west, east; [1..5]=NULL -> texture[0] */
    const char *drop;                /* item key dropped; NULL = itself */
    const char *creative_tab;
    uint32_t hardness_milli;         /* hardness * 1000 (stone = 1500) */
    uint32_t resistance_milli;       /* blast resistance * 1000 */
    uint16_t material;               /* lce_block_material */
    uint16_t sound;                  /* lce_block_sound */
    uint8_t light_emission;          /* 0..15 */
    uint8_t harvest_tier;            /* lce_tool_tier required, 0 = hand */
    uint8_t drop_min, drop_max;      /* 0,0 = exactly 1 */
    uint32_t flags;
    /* ---- API 1.1 ---- */
    uint16_t block_entity_slots;     /* >0: block entity with an item inventory of this size, saved in the world.
                                      * Retail 1.95: 9 (DispenserBlockEntity) or 27 (ChestBlockEntity, opaque wood cube).
                                      * Two adjacent compatible 27-slot blocks can form a 54-slot double chest. */
    uint8_t placement;               /* lce_block_placement */
    uint8_t flammability;            /* 0 = not flammable */
    uint32_t slipperiness_milli;     /* 0 = 600 (normal), ice = 980 */
    const char *harvest_tool;        /* "pickaxe" "axe" "shovel" "hoe" "shears" or NULL = any */
    /* ---- API 1.7 (zero = default) ---- */
    uint8_t variant_count;           /* 0/1: one state. 2..16: data values 0..n-1 are distinct block states (set_block data,
                                      * placement from the item's aux, the drop and the block item keep the variant) */
    uint8_t shape;                   /* lce_block_shape */
    uint8_t box[6];                  /* LCE_SHAPE_BOX: min x,y,z then max x,y,z in 1/16 block (0..16), min < max per axis */
    const char *const *variant_names;    /* [variant_count] display names, NULL entry = name; [0] is ignored (name) */
    const char *const *variant_textures; /* [variant_count * 6] terrain texture keys, [v*6 + face]; NULL entry = [v*6 + 0],
                                          * then texture[face]. Row 0 must be all NULL (variant 0 is texture[]) */
    /* ---- API 1.11 multi-box shapes (shape LCE_SHAPE_BOXES; zero otherwise) ---- */
    uint8_t box_count;               /* 1..8 boxes per set */
    uint8_t box_sets;                /* 1 = the same boxes for every state; variant_count = one set per variant (data value) */
    uint16_t reserved3;              /* 0 */
    const uint8_t *boxes;            /* [box_sets][box_count][6]: min x,y,z then max x,y,z in 1/16 block, min < max <= 16 */
} lce_block_desc;

typedef enum { LCE_PLACE_FIXED = 0, LCE_PLACE_FACING_PLAYER, LCE_PLACE_AXIS /* logs */ } lce_block_placement;
typedef enum { LCE_SHAPE_CUBE = 0, LCE_SHAPE_BOX = 1, LCE_SHAPE_BOXES = 2 /* API 1.11 */ } lce_block_shape;   /* API 1.7 */
#define LCE_BLOCK_FLAG_REPLACEABLE (1u << 2)
#define LCE_BLOCK_FLAG_NO_COLLIDE  (1u << 3)
#define LCE_BLOCK_FLAG_RANDOM_TICK (1u << 4)  /* API 1.3: the block receives random ticks (LCE_EV_BLOCK_TICK) */

typedef struct {
    const char *item;                /* key or "minecraft:..." ; NULL = empty cell */
    int16_t aux;                     /* data value, -1 = any */
} lce_ingredient;

typedef struct {
    uint32_t struct_size;
    const char *key;                 /* recipe id, "modid:ruby_block_from_rubies" */
    uint8_t width, height;           /* 1..3 */
    uint8_t shapeless;               /* 1: pattern order ignored, width*height = count */
    uint8_t reserved;
    lce_ingredient cells[9];         /* row-major */
    const char *result;
    uint16_t result_count;           /* 1..64 */
    int16_t result_aux;
    const char *craft_tab;           /* LCE crafting group, e.g. "minecraft:tools"; NULL = mod craft tab */
} lce_recipe_desc;

typedef struct {
    uint32_t struct_size;
    const char *key;
    lce_ingredient input;
    const char *result;
    uint16_t result_count;
    int16_t result_aux;
    uint32_t xp_milli;
} lce_smelt_desc;

typedef struct {
    uint32_t struct_size;
    const char *key;                 /* "modid:tab" */
    const char *name;
    const char *icon_item;           /* item key shown on the tab */
    uint8_t creative;                /* 1: Creative inventory page */
    uint8_t crafting;                /* 1: crafting-menu group */
    uint16_t reserved;
} lce_tab_desc;

typedef enum { LCE_TEXTURE_ITEMS = 0, LCE_TEXTURE_TERRAIN = 1 } lce_texture_atlas;

typedef struct {
    uint32_t struct_size;
    const char *key;                 /* mod-owned resource key referenced by item/block descriptors */
    uint16_t width, height;          /* each 16 pixels for LCE_TEXTURE_ITEMS and LCE_TEXTURE_TERRAIN */
    const uint8_t *rgba;             /* width*height*4 bytes, row-major RGBA8, straight alpha; copied */
    uint8_t atlas;                   /* lce_texture_atlas for item and block resources */
    uint8_t reserved[3];
} lce_texture_desc;

typedef struct {
    uint32_t struct_size;
    const char *locale;              /* "en_US" etc. */
    const char *key;                 /* content key or "modid:custom.string" */
    const char *value;
} lce_string_desc;

/* Legacy entity descriptor. Use register_entity2 for natural spawning. */
typedef struct {
    uint32_t struct_size;
    const char *key;
    const char *base_mob;            /* clone behaviour of a vanilla mob, e.g. "minecraft:zombie" */
    const char *texture;             /* 64x32 / 64x64 skin texture key */
    uint32_t max_health_milli;
    uint32_t spawn_weight;           /* must be 0; use register_entity2 to spawn */
} lce_entity_desc;

/* Legacy ore vein generation descriptor; uses the feature backend. */
typedef struct {
    uint32_t struct_size;
    const char *key;
    const char *block;               /* block to place */
    const char *replace;             /* NULL = minecraft:stone */
    uint8_t vein_size, veins_per_chunk, min_y, max_y;
    uint8_t dimension;               /* 0 overworld, 1 nether, 2 end */
    uint8_t reserved[3];
} lce_ore_desc;

/* ---- API 1.1 content -------------------------------------------------- */
typedef struct lce_effect_ref {
    const char *effect;              /* "minecraft:speed" or "modid:effect" */
    uint32_t duration_ticks;
    uint8_t amplifier;               /* 0 = level I */
    uint8_t chance_percent;          /* food effects: 0 = 100 */
    uint8_t reserved[2];
} lce_effect_ref;

typedef struct {
    uint32_t struct_size;
    const char *key, *name;
    uint32_t color_rgb;
    uint8_t beneficial;              /* 1 good, 0 harmful */
    uint8_t instant;                 /* instant (heal/harm-like) */
    uint16_t reserved;
    const char *icon_texture;        /* 18x18 inventory icon, NULL = generic */
    /* behaviour: per-tick hook through LCE_EV_EFFECT_TICK (native/script) or a vanilla clone */
    const char *clone_of;            /* e.g. "minecraft:regeneration": reuse vanilla behaviour */
} lce_effect_desc;

typedef struct {
    uint32_t struct_size;
    const char *key, *name;
    const lce_effect_ref *effects; uint32_t effect_count;
    uint8_t splash, lingering;       /* also register splash / lingering variants */
    uint16_t reserved;
} lce_potion_desc;

typedef struct {
    uint32_t struct_size;
    const char *key;
    const char *input_potion;        /* "minecraft:awkward" or a mod potion key */
    const char *reagent;             /* item key */
    const char *output_potion;
} lce_brew_desc;

typedef struct {
    uint32_t struct_size;
    const char *key, *name;
    uint8_t max_level;               /* 1..10 */
    uint8_t weight;                  /* rarity weight: common 10, uncommon 5, rare 2, very rare 1 */
    uint16_t applies_to;             /* bitmask of (1 << lce_item_kind) */
    uint8_t treasure;                /* not from the enchanting table */
    uint8_t reserved[3];
    const char *clone_of;            /* reuse a vanilla enchantment's effect, e.g. "minecraft:sharpness" */
} lce_enchant_desc;

typedef struct {
    uint32_t struct_size;
    const char *item;                /* any item key, incl. vanilla */
    uint32_t burn_ticks;
} lce_fuel_desc;

typedef enum { LCE_AI_WANDER = 1, LCE_AI_LOOK_AT_PLAYER, LCE_AI_PANIC, LCE_AI_MELEE_ATTACK, LCE_AI_TARGET_PLAYER,
               LCE_AI_FLEE_SUN, LCE_AI_FOLLOW_TEMPT_ITEM, LCE_AI_BREED, LCE_AI_SWIM, LCE_AI_AVOID_ENTITY } lce_ai_goal_kind;
typedef struct { uint16_t kind; uint16_t priority; uint32_t speed_milli; const char *param; } lce_ai_goal;
typedef struct { const char *item; uint8_t min, max, chance_percent, looting_bonus; } lce_drop;

typedef enum { LCE_MOB_CREATURE = 0, LCE_MOB_MONSTER, LCE_MOB_WATER, LCE_MOB_AMBIENT } lce_mob_category;

/* API 1.1 extension of lce_entity_desc lives in lce_entity_desc2 (first fields identical). */
typedef struct {
    uint32_t struct_size;
    const char *key;
    const char *base_mob;            /* vanilla mob whose model/renderer/class is cloned, e.g. "minecraft:zombie" */
    const char *texture;             /* 64x32 or 64x64 skin texture key (atlas 3) */
    uint32_t max_health_milli;
    uint32_t spawn_weight;           /* 0 = no natural spawn */
    /* ---- 1.1 ---- */
    const char *name;
    uint16_t category;               /* lce_mob_category */
    uint8_t min_group, max_group;
    uint32_t attack_damage_milli, move_speed_milli;   /* 0 = base mob default */
    uint32_t width_milli, height_milli;               /* 0 = base mob */
    const lce_ai_goal *goals; uint32_t goal_count;     /* empty = base mob AI */
    const lce_drop *drops; uint32_t drop_count;        /* empty = base mob loot */
    const char *const *spawn_biomes; uint32_t spawn_biome_count; /* NULL = base mob biomes */
    uint32_t egg_primary_rgb, egg_secondary_rgb;      /* spawn egg colours; 0,0 = no egg */
    const char *sound_ambient, *sound_hurt, *sound_death; /* sound keys, NULL = base mob */
} lce_entity_desc2;

typedef enum { LCE_FEATURE_ORE = 1, LCE_FEATURE_TREE, LCE_FEATURE_FLOWER, LCE_FEATURE_LAKE, LCE_FEATURE_STRUCTURE } lce_feature_kind;
typedef struct {
    uint32_t struct_size;
    const char *key;
    uint16_t kind;                   /* lce_feature_kind */
    uint8_t dimension;               /* 0 overworld 1 nether 2 end */
    uint8_t reserved;
    const char *block;               /* ORE/FLOWER: block; TREE: log */
    const char *block2;              /* ORE: replaced block (NULL stone); TREE: leaves */
    const char *structure;           /* STRUCTURE: structure key */
    uint16_t size, per_chunk;        /* ORE: vein size / veins; TREE/FLOWER: count */
    uint16_t min_y, max_y;
    uint16_t rarity;                 /* 1 in N chunks, 0 = every chunk */
    uint16_t reserved2;
    const char *const *biomes; uint32_t biome_count; /* NULL = all biomes of the dimension */
} lce_feature_desc;

typedef struct {
    uint32_t struct_size;
    const char *key, *name;
    uint32_t temperature_milli, rainfall_milli;
    const char *top_block, *filler_block;
    uint32_t grass_rgb, foliage_rgb, water_rgb, sky_rgb;
    uint16_t weight;                 /* generation weight vs vanilla biomes */
    uint16_t height_base_milli, height_variation_milli, reserved;
    const char *const *features; uint32_t feature_count;
} lce_biome_desc;

typedef struct {
    uint32_t struct_size;
    const char *key;
    uint16_t size_x, size_y, size_z, reserved;
    const char *const *palette; uint32_t palette_count;   /* block keys; index 0 = keep existing */
    const uint16_t *blocks;          /* palette indices, index = (y*size_z + z)*size_x + x */
} lce_structure_desc;

typedef struct {
    uint32_t struct_size;
    const char *key;
    const char *clone_of;            /* vanilla sound event to play, e.g. "minecraft:mob.zombie.say" */
    const uint8_t *pcm; uint32_t pcm_bytes; /* EXPERIMENTAL raw 16-bit mono PCM (no backend) */
    uint32_t sample_rate;
    uint32_t volume_milli, pitch_milli;
} lce_sound_desc;

typedef struct {
    uint32_t struct_size;
    const char *key;
    const char *clone_of;            /* vanilla particle, e.g. "minecraft:reddust" */
    uint32_t tint_rgb;
    const char *texture;             /* NULL = vanilla texture */
} lce_particle_desc;

typedef struct lce_cmd_ctx lce_cmd_ctx;
typedef int (*lce_command_fn)(void *user, void *player, int argc, const char *const *argv, char *reply, uint32_t reply_cap);
typedef struct {
    uint32_t struct_size;
    const char *name;                /* "/ruby" -> "ruby"; namespaced internally as modid:ruby */
    const char *usage;
    uint8_t op_only, reserved[3];
    lce_command_fn fn; void *user;   /* native; scripts register through the script layer */
} lce_command_desc;

typedef enum { LCE_PAD_L3 = 1, LCE_PAD_R3, LCE_PAD_TOUCHPAD, LCE_PAD_DPAD_LEFT_HOLD, LCE_PAD_DPAD_RIGHT_HOLD } lce_pad_button;
typedef int (*lce_key_fn)(void *user, void *player, int pressed);
typedef struct {
    uint32_t struct_size;
    const char *key, *name;
    uint16_t default_button;         /* lce_pad_button (chord with the debug-menu-free buttons) */
    uint16_t reserved;
    lce_key_fn fn; void *user;
} lce_keybind_desc;

/* ---- events -------------------------------------------------------------- */
typedef enum {
    LCE_EV_SERVER_TICK = 1,   /* every server tick (20 Hz), game server thread */
    LCE_EV_CLIENT_TICK,       /* every client frame tick */
    LCE_EV_WORLD_LOAD,
    LCE_EV_WORLD_UNLOAD,
    LCE_EV_BLOCK_BREAK,       /* payload lce_ev_block */
    LCE_EV_BLOCK_PLACE,
    LCE_EV_ITEM_USE,          /* payload lce_ev_item_use */
    /* API 1.1 */
    LCE_EV_ENTITY_DAMAGE,     /* payload lce_ev_entity, cancellable */
    LCE_EV_ENTITY_DEATH,      /* payload lce_ev_entity */
    LCE_EV_PLAYER_JOIN,       /* payload lce_ev_player */
    LCE_EV_PLAYER_LEAVE,
    LCE_EV_CHAT,              /* payload lce_ev_chat, cancellable */
    LCE_EV_EFFECT_TICK,       /* payload lce_ev_entity for mod effects: entity, type_id = effect id, amount_milli = amplifier*1000,
                               * source = the effect key (const char*). Server thread, every 20 ticks without clone_of,
                               * and on each tick the cloned vanilla behaviour runs */
    /* API 1.2 (capabilities2 LCE_CAP2_EV_*) */
    LCE_EV_BLOCK_USE,         /* payload lce_ev_block: a player right-clicks a mod block; nonzero = handled (consumes the click) */
    LCE_EV_ENTITY_INTERACT,   /* payload lce_ev_entity: a player right-clicks a mod entity (entity, source = player) */
    LCE_EV_BLOCK_TICK,        /* payload lce_ev_block: scheduled/random tick of a mod block (player NULL) */
    LCE_EV_ENTITY_TICK,       /* payload lce_ev_entity: server tick of a mod-spawned entity */
    /* API 1.3 (capabilities2 LCE_CAP2_EV_INVENTORY_TICK / NEIGHBOR / BLOCK_COLLIDE) */
    LCE_EV_ITEM_INVENTORY_TICK, /* payload lce_ev_item_use: item_id, player = holder entity; each tick while in an inventory */
    LCE_EV_BLOCK_NEIGHBOR,    /* payload lce_ev_block: a neighbour of your block changed */
    LCE_EV_BLOCK_ENTITY_INSIDE, /* payload lce_ev_block: player = any entity inside the block's cell */
    LCE_EV_BLOCK_STEP_ON,     /* payload lce_ev_block: player = any entity walking on top of your block */
    /* API 1.4 networking (capabilities2 LCE_CAP2_EV_PAYLOAD) */
    LCE_EV_CUSTOM_PAYLOAD,    /* payload lce_ev_payload: a client sent data on a channel your mod registered (server side);
                               * the return value is ignored; registered channels never reach the vanilla handler */
    /* API 1.6 (capabilities2 LCE_CAP2_EV_CLIENT_PAYLOAD) */
    LCE_EV_CLIENT_PAYLOAD,    /* payload lce_ev_payload (player NULL): the server sent data on your channel to this PS4. Client thread on a remote client (world services refuse; send_to_server answers at once); on the host it is delivered on the next server tick */
    /* API 1.9 (capabilities2 LCE_CAP2_PROJECTILES) */
    LCE_EV_PROJECTILE_HIT,    /* payload lce_ev_entity: entity = hit entity or NULL (block hit), source = the projectile, type_id = item id,
                               * level, x/y/z = block position of the impact; server thread, after the declarative fields applied */
    LCE_EV_COUNT
} lce_event;

/* Payloads are append-only: test struct_size before reading a field added in a later minor version. */
typedef struct {
    uint32_t struct_size; int32_t x, y, z; uint32_t block_id; uint32_t aux; void *player;
    /* API 1.2 */
    void *level;                     /* server level of the event (pass to world services) */
    int32_t face;                    /* clicked face 0..5 (BLOCK_USE/PLACE), -1 unknown */
    uint32_t held_item_id;           /* item id in the player's main hand, 0 = empty/unknown */
} lce_ev_block;
typedef struct {
    uint32_t struct_size; uint32_t item_id; uint32_t aux; void *player;
    /* API 1.2 */
    void *level;
    int32_t x, y, z;                 /* block position of the player's feet */
    uint32_t hand;                   /* 0 main hand, 1 off hand */
} lce_ev_item_use;
typedef struct { uint32_t struct_size; uint64_t tick; } lce_ev_tick;
typedef struct {
    uint32_t struct_size; void *entity; void *source; int32_t type_id; uint32_t amount_milli;
    /* API 1.2 */
    void *level;
    int32_t x, y, z;                 /* block position of the entity */
    void *attacker;                  /* ENTITY_DAMAGE: the entity behind the damage source (NULL if none or unknown); INTERACT: the player */
    uint32_t attacker_item_id;       /* item id in the attacker's main hand (players only), 0 = none/unknown */
} lce_ev_entity;
typedef struct { uint32_t struct_size; void *player; const char *name; } lce_ev_player;
typedef struct { uint32_t struct_size; void *player; const char *message; } lce_ev_chat;

/* Return 0 to continue; nonzero from a cancellable event (BLOCK_BREAK,
 * ITEM_USE) asks the backend to cancel when it supports cancelling. */
typedef int (*lce_event_fn)(void *user, lce_event ev, const void *payload);

typedef enum { LCE_LOG_DEBUG = 0, LCE_LOG_INFO, LCE_LOG_WARN, LCE_LOG_ERROR } lce_log_level;

typedef struct lce_mod_ctx lce_mod_ctx;   /* opaque, one per mod */

/* ---- API 1.4: item NBT values ------------------------------------------- */
#define LCE_HAVE_NBT_1_4 1
typedef enum { LCE_NBT_NONE = 0, LCE_NBT_INT = 1, LCE_NBT_DOUBLE = 2, LCE_NBT_STRING = 3 } lce_nbt_type;
typedef struct { uint32_t type; int64_t i; double d; char s[256]; } lce_nbt_value;   /* type = lce_nbt_type; s is ASCII */

typedef struct { uint32_t struct_size; void *player; const char *channel; const uint8_t *data; uint32_t len; } lce_ev_payload;   /* API 1.4 */

/* ---- the API table handed to each mod --------------------------------- */
typedef struct lce_api {
    uint32_t struct_size;
    uint32_t version;                 /* LCE_MOD_API_VERSION of the loader */
    uint32_t capabilities;            /* LCE_CAP_* the active backend really supports */
    uint32_t reserved;

    void (*log)(lce_mod_ctx *ctx, lce_log_level level, const char *msg);
    lce_phase (*phase)(lce_mod_ctx *ctx);

    /* INIT/REGISTER only. Success stages pixels for atlas application by the backend. */
    lce_status (*register_texture)(lce_mod_ctx *ctx, const lce_texture_desc *d);
    lce_status (*register_item)(lce_mod_ctx *ctx, const lce_item_desc *d);
    lce_status (*register_block)(lce_mod_ctx *ctx, const lce_block_desc *d);
    lce_status (*register_recipe)(lce_mod_ctx *ctx, const lce_recipe_desc *d);
    lce_status (*register_smelting)(lce_mod_ctx *ctx, const lce_smelt_desc *d);
    lce_status (*register_tab)(lce_mod_ctx *ctx, const lce_tab_desc *d);
    lce_status (*register_string)(lce_mod_ctx *ctx, const lce_string_desc *d);
    lce_status (*register_entity)(lce_mod_ctx *ctx, const lce_entity_desc *d);
    lce_status (*register_ore)(lce_mod_ctx *ctx, const lce_ore_desc *d);

    /* POST_INIT/RUNNING: numeric ID for a key ("modid:x" or "minecraft:x"), -1 if unknown. */
    int32_t (*resolve_item)(lce_mod_ctx *ctx, const char *key);
    int32_t (*resolve_block)(lce_mod_ctx *ctx, const char *key);

    /* INIT/REGISTER only. Returns LCE_E_UNSUPPORTED if the backend lacks the hook. */
    lce_status (*subscribe)(lce_mod_ctx *ctx, lce_event ev, lce_event_fn fn, void *user);

    /* ---- API 1.1: registration (INIT/REGISTER) ---- */
    lce_status (*register_effect)(lce_mod_ctx *ctx, const lce_effect_desc *d);
    lce_status (*register_potion)(lce_mod_ctx *ctx, const lce_potion_desc *d);
    lce_status (*register_brewing)(lce_mod_ctx *ctx, const lce_brew_desc *d);
    lce_status (*register_enchantment)(lce_mod_ctx *ctx, const lce_enchant_desc *d);
    lce_status (*register_fuel)(lce_mod_ctx *ctx, const lce_fuel_desc *d);
    lce_status (*register_entity2)(lce_mod_ctx *ctx, const lce_entity_desc2 *d);
    lce_status (*register_feature)(lce_mod_ctx *ctx, const lce_feature_desc *d);
    lce_status (*register_biome)(lce_mod_ctx *ctx, const lce_biome_desc *d);
    lce_status (*register_structure)(lce_mod_ctx *ctx, const lce_structure_desc *d);
    lce_status (*register_sound)(lce_mod_ctx *ctx, const lce_sound_desc *d);
    lce_status (*register_particle)(lce_mod_ctx *ctx, const lce_particle_desc *d);
    lce_status (*register_command)(lce_mod_ctx *ctx, const lce_command_desc *d);
    lce_status (*register_keybind)(lce_mod_ctx *ctx, const lce_keybind_desc *d);

    /* ---- API 1.1: runtime services (RUNNING, server thread inside an event
     * callback unless stated). LCE_E_UNSUPPORTED without LCE_CAP_SERVICES. ---- */
    lce_status (*give_item)(lce_mod_ctx *ctx, void *player, const char *item, uint32_t count, int32_t aux);
    lce_status (*set_block)(lce_mod_ctx *ctx, void *level, int32_t x, int32_t y, int32_t z, const char *block, uint32_t data);
    int32_t (*get_block)(lce_mod_ctx *ctx, void *level, int32_t x, int32_t y, int32_t z);
    lce_status (*spawn_entity)(lce_mod_ctx *ctx, void *level, const char *entity, int32_t x, int32_t y, int32_t z);
    lce_status (*drop_item)(lce_mod_ctx *ctx, void *level, int32_t x, int32_t y, int32_t z, const char *item, uint32_t count);
    lce_status (*send_chat)(lce_mod_ctx *ctx, void *player, const char *message);  /* player NULL = everyone */
    lce_status (*add_effect)(lce_mod_ctx *ctx, void *entity, const lce_effect_ref *e);
    lce_status (*play_sound)(lce_mod_ctx *ctx, void *level, int32_t x, int32_t y, int32_t z, const char *sound);
    lce_status (*schedule)(lce_mod_ctx *ctx, uint32_t ticks, lce_event_fn fn, void *user); /* run fn(user, LCE_EV_SERVER_TICK, NULL) after N server ticks */
    const char *(*translate)(lce_mod_ctx *ctx, const char *key);                         /* current game language, falls back to en_US */

    /* ---- API 1.2 ---- */
    uint32_t capabilities2;           /* LCE_CAP2_* the active backend really supports */
    uint32_t reserved2;
    /* Same rules as the 1.1 services: RUNNING, server thread, inside an event callback; entity/player/level must be
     * the event's own (or NULL = the event's). Results: LCE_OK after the native call and its readback succeeded. */
    lce_status (*explode)(lce_mod_ctx *ctx, void *level, double x, double y, double z, float radius, int fire, int destroy_blocks);
    lce_status (*set_fire)(lce_mod_ctx *ctx, void *entity, uint32_t ticks);          /* 0 extinguishes; never shortens a longer fire */
    lce_status (*damage_entity)(lce_mod_ctx *ctx, void *entity, uint32_t amount_milli, void *attacker);   /* attacker: NULL, the event player, or a living entity of the frame level */
    lce_status (*heal)(lce_mod_ctx *ctx, void *entity, uint32_t amount_milli);         /* clamped to max health; dead entities refused */
    lce_status (*damage_held_item)(lce_mod_ctx *ctx, void *player, uint32_t amount);  /* main hand; refuses items without durability */
    lce_status (*consume_held_item)(lce_mod_ctx *ctx, void *player, uint32_t count);  /* main hand; refuses when fewer remain */
    lce_status (*teleport)(lce_mod_ctx *ctx, void *entity, double x, double y, double z);
    lce_status (*set_motion)(lce_mod_ctx *ctx, void *entity, double dx, double dy, double dz);
    /* ---- API 1.3 services (capabilities2 LCE_CAP2_SVC_PARTICLES) ---- */
    lce_status (*spawn_particles)(lce_mod_ctx *ctx, void *level, int32_t type_id, double x, double y, double z, uint32_t count,
                                  double dx, double dy, double dz, double speed);
    /* ---- API 1.4 item NBT (capabilities2 LCE_CAP2_SVC_ITEM_NBT). Keys: 1..64 chars [A-Za-z0-9_:.-];
     * player: NULL or the frame's own player. ---- */
    lce_status (*item_tag_get)(lce_mod_ctx *ctx, void *player, const char *key, lce_nbt_value *out);    /* main-hand stack tag; NOTFOUND if absent */
    lce_status (*item_tag_set)(lce_mod_ctx *ctx, void *player, const char *key, const lce_nbt_value *v); /* int32 / double / ASCII string; creates the tag */
    lce_status (*item_tag_remove)(lce_mod_ctx *ctx, void *player, const char *key);
    /* ---- API 1.4 mod networking ---- */
    lce_status (*register_channel)(lce_mod_ctx *ctx, const char *channel);   /* INIT/REGISTER; "modid:name", 3..20 of [a-z0-9_:.-], not "mc|" */
    lce_status (*send_payload)(lce_mod_ctx *ctx, void *player, const char *channel, const void *data, uint32_t len); /* server -> client; player NULL = all; <= 32000 bytes */
    /* ---- API 1.5 entity NBT (capabilities2 LCE_CAP2_SVC_ENTITY_NBT / LCE_CAP2_SVC_ENTITY_DATA). entity: NULL = the event's player, else an entity of the event's level. ---- */
    lce_status (*entity_nbt_get)(lce_mod_ctx *ctx, void *entity, const char *path, lce_nbt_value *out);   /* read-only vanilla save data: "Health", "Pos[1]", "Attributes[0]/Base", "Tags[#]" (list length) */
    lce_status (*entity_data_get)(lce_mod_ctx *ctx, void *entity, const char *key, lce_nbt_value *out);   /* persistent mod data */
    lce_status (*entity_data_set)(lce_mod_ctx *ctx, void *entity, const char *key, const lce_nbt_value *v); /* saved with the entity; int32/double/ASCII <= 200; never players; <= 1024 tags */
    lce_status (*entity_data_remove)(lce_mod_ctx *ctx, void *entity, const char *key);
    /* ---- API 1.6 client -> server (capabilities2 LCE_CAP2_SVC_SEND_TO_SERVER) ---- */
    lce_status (*send_to_server)(lce_mod_ctx *ctx, const char *channel, const void *data, uint32_t len);   /* registered channel, <= 32000 bytes: immediate inside a ClientPayload handler, else queued (~1 s); host: loopback */
    /* ---- API 1.8 per-mod config (capabilities2 LCE_CAP2_CONFIG). /data/lce-mods/<modid>/config, a TOML subset
     * ("[section]" + "key = value"; keys are "section.key"), read once before lce_mod_main and scripts run. Any phase,
     * any thread. A missing key, a value of the wrong type or one outside [min, max] gives def (min > max: no range).
     * Booleans are integers 1/0. MODDING.md 8.5. ---- */
    lce_status (*config_get)(lce_mod_ctx *ctx, const char *key, lce_nbt_value *out);   /* NOTFOUND if absent */
    int64_t (*config_int)(lce_mod_ctx *ctx, const char *key, int64_t def, int64_t min, int64_t max);
    double (*config_double)(lce_mod_ctx *ctx, const char *key, double def, double min, double max);
    const char *(*config_string)(lce_mod_ctx *ctx, const char *key, const char *def);   /* valid for the process lifetime */
    /* API 1.12. Emit the stock colorable particle from a server event. NULL level uses
     * the event level. rgb is 0xRRGGBB, count is 1..1000 and spread per axis is 0..64.
     * Vanilla clients receive the effect through their normal particle
     * packet and apply their own range, particle settings and shading. */
    lce_status (*spawn_tinted_particles)(lce_mod_ctx *ctx, void *level, uint32_t rgb,
                                         double x, double y, double z, uint32_t count, double spread);
} lce_api;

/* Native mod entry point (EXPERIMENTAL native mods only). Called once in
 * LCE_PHASE_INIT; registration calls are accepted until it returns. */
typedef lce_status (*lce_mod_main_fn)(const lce_api *api, lce_mod_ctx *ctx);

#ifdef __cplusplus
}
#endif
#endif /* LCE_MOD_API_H */
