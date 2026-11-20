/*
 * lce_mod_package.h - on-disk format of a compiled LCE mod package (mod.lcm),
 * format version 1. Produced by tools/lce_modpack.py, parsed by
 * loader/package.cpp. All integers little-endian; all offsets are relative to
 * the start of the file and bounds-checked by the parser.
 *
 * Install layout on the console:
 *   /data/lce-mods/<modid>/mod.lcm        required, the whole data mod
 *   /data/lce-mods/<modid>/mod.json       optional, informational copy of the source manifest
 *   /data/lce-mods/<modid>/native.prx     optional, EXPERIMENTAL native code (disabled by default)
 *   /data/lce-mods/<modid>/enabled        optional marker; absent = disabled (see INTEGRATION.md)
 * "packs" is reserved (skin/texture packs live in /data/lce-mods/packs).
 * This fits the signed content catalog limit of 8 files per item.
 *
 * File layout:
 *   [0,64)   lce_pkg_header
 *   [64,..)  section table: section_count * lce_pkg_section
 *   ...      section bodies (any order, 4-byte aligned, non-overlapping)
 * header.payload_sha256 = SHA-256 of bytes [64, file_size).
 *
 * String references (lce_pkg_str) are byte offsets into the STRS section;
 * offset 0 is the empty string (STRS must start with a NUL). Every referenced
 * string must be NUL-terminated inside STRS and valid UTF-8 without control
 * characters.
 */
#ifndef LCE_MOD_PACKAGE_H
#define LCE_MOD_PACKAGE_H
#include <stdint.h>

#define LCE_PKG_MAGIC "LCEMODPK"
#define LCE_PKG_FORMAT 1
#define LCE_PKG_MAX_FILE (8u * 1024u * 1024u)
#define LCE_PKG_MAX_SECTIONS 20   /* API 1.9: 16 -> 20 (every 1.9 section kind fits: 18) */

typedef uint32_t lce_pkg_str;

enum {
    LCE_SEC_STRS = 1, /* string pool */
    LCE_SEC_META = 2, /* exactly one lce_pkg_meta */
    LCE_SEC_TEX = 3,  /* lce_pkg_tex[] */
    LCE_SEC_DATA = 4, /* raw blob area referenced by TEX */
    LCE_SEC_ITEM = 5, /* lce_pkg_item[] */
    LCE_SEC_BLCK = 6, /* lce_pkg_block[] */
    LCE_SEC_RCPE = 7, /* lce_pkg_recipe[] */
    LCE_SEC_SMLT = 8, /* lce_pkg_smelt[] */
    LCE_SEC_TABS = 9, /* lce_pkg_tab[] */
    LCE_SEC_LANG = 10,/* lce_pkg_lang[] */
    LCE_SEC_ENTY = 11,/* lce_pkg_entity[] (experimental; no backend) */
    LCE_SEC_ORES = 12,/* lce_pkg_ore[] (experimental; no backend) */
    LCE_SEC_DEPS = 13,/* lce_pkg_dep[] */
    /* API 1.1 */
    LCE_SEC_SCRIPT = 14,/* lce_pkg_script[]: Lua sources stored in DATA */
    LCE_SEC_EXT = 15, /* raw: records {u32 type; u32 slots; u32 slot[slots]} per loader/ext.h schema */
    LCE_SEC_ARRS = 16,/* raw u32 pool referenced by EXT array slots */
    LCE_SEC_ITEM2 = 17,/* lce_pkg_item2[], parallel to ITEM (same count/order): API 1.1 item fields */
    LCE_SEC_BLCK2 = 18, /* lce_pkg_block2[], parallel to BLCK */
    LCE_SEC_BLCK3 = 19, /* lce_pkg_block3[], parallel to BLCK: API 1.7 */
    LCE_SEC_BLCK4 = 21, /* lce_pkg_block4[], parallel to BLCK: API 1.11 multi-box shapes (written only when a block is BOXES) */
    LCE_SEC_ITEM3 = 20  /* lce_pkg_item3[], parallel to ITEM: API 1.9 throwables, API 1.10 boat type (written only when an item is THROWABLE or BOAT) */
};

#pragma pack(push, 1)
typedef struct {
    char magic[8];
    uint16_t format;
    uint16_t api_major;
    uint16_t api_minor;
    uint16_t section_count;
    uint32_t file_size;
    uint32_t flags;          /* bit0: package contains native.prx reference */
    uint8_t payload_sha256[32];
    uint8_t reserved[8];
} lce_pkg_header;            /* 64 bytes */

typedef struct { uint32_t type, offset, size, count; } lce_pkg_section;

typedef struct {
    lce_pkg_str modid, name, version, author, description, target_game;
    uint32_t flags;
} lce_pkg_meta;              /* 28 */

typedef struct { lce_pkg_str modid; lce_pkg_str min_version; } lce_pkg_dep; /* 8 */

typedef struct {
    lce_pkg_str key;
    uint16_t width, height;
    uint8_t atlas, reserved[3];
    uint32_t data_offset;    /* relative to DATA section start */
    uint32_t data_size;      /* width*height*4 */
} lce_pkg_tex;               /* 20 */

typedef struct {
    lce_pkg_str key, name, texture, creative_tab;
    uint16_t kind, max_stack, max_damage, tier, food_hunger, food_saturation_milli;
    uint32_t flags;
    int32_t attack_bonus_milli;
} lce_pkg_item;              /* 36 */

typedef struct {
    lce_pkg_str key, name, texture[6], drop, creative_tab;
    uint32_t hardness_milli, resistance_milli;
    uint16_t material, sound;
    uint8_t light_emission, harvest_tier, drop_min, drop_max;
    uint32_t flags;
} lce_pkg_block;             /* 60 */

typedef struct { lce_pkg_str item; int16_t aux; uint16_t reserved; } lce_pkg_ingredient; /* 8 */

typedef struct {
    lce_pkg_str key;
    uint8_t width, height, shapeless, reserved;
    lce_pkg_ingredient cells[9];
    lce_pkg_str result;
    uint16_t result_count;
    int16_t result_aux;
    lce_pkg_str craft_tab;
} lce_pkg_recipe;            /* 92 */

typedef struct {
    lce_pkg_str key;
    lce_pkg_ingredient input;
    lce_pkg_str result;
    uint16_t result_count;
    int16_t result_aux;
    uint32_t xp_milli;
} lce_pkg_smelt;             /* 24 */

typedef struct {
    lce_pkg_str key, name, icon_item;
    uint8_t creative, crafting, reserved[2];
} lce_pkg_tab;               /* 16 */

typedef struct { lce_pkg_str locale, key, value; } lce_pkg_lang; /* 12 */

typedef struct { lce_pkg_str name; uint32_t data_offset, data_size; } lce_pkg_script; /* 12; name e.g. "main.lua" */

typedef struct {
    uint16_t armor_points, enchantability;
    lce_pkg_str armor_texture, repair_item;
    uint32_t fuel_ticks, use_ticks, mining_speed_milli;
    uint32_t effects_index, effects_count;   /* ARRS: 4 u32 per effect {effect, duration, amplifier, chance} */
} lce_pkg_item2;  /* 32 */

typedef struct {
    uint16_t storage_slots;
    uint8_t placement, flammability;
    uint32_t slipperiness_milli;
    lce_pkg_str harvest_tool;
} lce_pkg_block2; /* 12 */

typedef struct {
    uint8_t variant_count, shape, box[6];
    uint32_t names_index, textures_index;   /* ARRS word index of variant_count / variant_count*6 string refs; 0xffffffff = none */
} lce_pkg_block3; /* 16 */

typedef struct {
    uint32_t speed_milli, inaccuracy_milli, damage_milli, fire_ticks, explode_milli;
    uint32_t effects_index, effects_count;   /* ARRS: 4 u32 per effect {effect, duration, amplifier, chance} */
    uint32_t boat_type;                      /* API 1.10: 0..5 with LCE_ITEM_FLAG_BOAT, else 0 (was reserved = 0) */
} lce_pkg_item3; /* 32 */

typedef struct {
    uint8_t box_count, box_sets;             /* API 1.11: 0,0 = no boxes; else 1..8 and 1 or the variant count */
    uint16_t reserved;                       /* 0 */
    uint32_t boxes_index;                    /* ARRS: box_sets * box_count words, one per box: 6 x 5 bits (x0 y0 z0 x1 y1 z1, bit 0 up) */
} lce_pkg_block4; /* 8 */

/* EXT slot encoding per schema field (loader/ext.h):
 *   KEY/TEXT/REF/REF_REQ/NAME: string ref (0 = NULL)   U8/U16/U32: value
 *   ARR_EFFECT/GOAL/DROP/REF:   2 slots (ARRS index, count); element sizes in u32:
 *                               EFFECT 4 {effect, duration, amplifier, chance}
 *                               GOAL 4 {kind, priority, speed_milli, param}
 *                               DROP 5 {item, min, max, chance, looting}   REF 1 {string}
 *   ARR_U16_XYZ: 1 slot (ARRS index), count = size_x*size_y*size_z, one u32 per value
 *   BLOB: 2 slots (DATA offset, bytes)                   PTR: not packable */

typedef struct {
    lce_pkg_str key, base_mob, texture;
    uint32_t max_health_milli, spawn_weight;
} lce_pkg_entity;            /* 20 */

typedef struct {
    lce_pkg_str key, block, replace;
    uint8_t vein_size, veins_per_chunk, min_y, max_y, dimension, reserved[3];
} lce_pkg_ore;               /* 20 */
#pragma pack(pop)

#ifdef __cplusplus
static_assert(sizeof(lce_pkg_header) == 64, "header");
static_assert(sizeof(lce_pkg_section) == 16, "section");
static_assert(sizeof(lce_pkg_meta) == 28, "meta");
static_assert(sizeof(lce_pkg_tex) == 20, "tex");
static_assert(sizeof(lce_pkg_item) == 36, "item");
static_assert(sizeof(lce_pkg_block) == 60, "block");
static_assert(sizeof(lce_pkg_recipe) == 92, "recipe");
static_assert(sizeof(lce_pkg_smelt) == 24, "smelt");
static_assert(sizeof(lce_pkg_tab) == 16, "tab");
static_assert(sizeof(lce_pkg_lang) == 12, "lang");
static_assert(sizeof(lce_pkg_entity) == 20, "entity");
static_assert(sizeof(lce_pkg_ore) == 20, "ore");
static_assert(sizeof(lce_pkg_script) == 12, "script");
static_assert(sizeof(lce_pkg_item2) == 32, "item2");
static_assert(sizeof(lce_pkg_block2) == 12, "block2");
static_assert(sizeof(lce_pkg_block3) == 16, "block3");
static_assert(sizeof(lce_pkg_item3) == 32, "item3");
static_assert(sizeof(lce_pkg_block4) == 8, "block4");
#endif
#endif
