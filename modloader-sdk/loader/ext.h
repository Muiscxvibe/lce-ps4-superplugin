/* API 1.1 content types, handled by one schema table: deep copy of
 * descriptors (strings + arrays), reference validation, package decoding and
 * plan exposure all derive from the same field list, so a new content type is
 * one schema entry. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "../include/lce_mod_api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LCE_EXT_EFFECT = 1, LCE_EXT_POTION, LCE_EXT_BREW, LCE_EXT_ENCHANT, LCE_EXT_FUEL, LCE_EXT_ENTITY,
    LCE_EXT_FEATURE, LCE_EXT_BIOME, LCE_EXT_STRUCTURE, LCE_EXT_SOUND, LCE_EXT_PARTICLE, LCE_EXT_COMMAND,
    LCE_EXT_KEYBIND, LCE_EXT_TYPE_COUNT
} lce_ext_type;

/* What a reference field must resolve to. */
typedef enum {
    LCE_REF_NONE = 0, LCE_REF_ITEM, LCE_REF_BLOCK, LCE_REF_EFFECT, LCE_REF_POTION, LCE_REF_SOUND,
    LCE_REF_STRUCTURE, LCE_REF_BIOME, LCE_REF_FEATURE, LCE_REF_ENTITY, LCE_REF_TEXTURE_ENTITY,
    LCE_REF_TEXTURE_ICON, LCE_REF_PARTICLE
} lce_ref_kind;

typedef enum {
    LCE_F_KEY = 1,      /* const char*: own-namespace primary key (required) */
    LCE_F_TEXT,         /* const char*: UTF-8 text, nullable */
    LCE_F_REF,          /* const char*: reference (ref kind), nullable unless required */
    LCE_F_REF_REQ,      /* const char*: reference, required */
    LCE_F_NAME,         /* const char*: command/keybind name [a-z0-9_], required */
    LCE_F_U8, LCE_F_U16, LCE_F_U32,
    LCE_F_PTR,          /* native-only pointer (callbacks, user data); never packaged */
    LCE_F_ARR_EFFECT,   /* const lce_effect_ref* + uint32 count (at count_offset) */
    LCE_F_ARR_GOAL,     /* const lce_ai_goal* + count */
    LCE_F_ARR_DROP,     /* const lce_drop* + count */
    LCE_F_ARR_REF,      /* const char* const* + count (ref kind) */
    LCE_F_ARR_U16_XYZ,  /* const uint16_t*, count = size_x*size_y*size_z of lce_structure_desc */
    LCE_F_BLOB          /* const uint8_t* + uint32 bytes (count_offset) */
} lce_field_kind;

typedef struct {
    uint16_t kind;        /* lce_field_kind */
    uint16_t ref;         /* lce_ref_kind */
    uint32_t offset;      /* in the descriptor */
    uint32_t count_offset;/* arrays/blobs: offset of the uint32 count */
    const char *name;     /* manifest field name (packer + error messages) */
} lce_ext_field;

typedef struct {
    const char *name;     /* "effect", "potion", ... */
    uint32_t desc_size;
    uint32_t capability;  /* LCE_CAP_* required to publish */
    uint32_t field_count;
    const lce_ext_field *fields;
    uint8_t packable;     /* 0: contains native pointers (commands, keybinds) */
} lce_ext_schema;

const lce_ext_schema *lce_ext_schema_of(uint32_t type);

/* Plan entry handed to backends. desc points at a loader-owned deep copy. */
typedef struct {
    uint32_t type;        /* lce_ext_type */
    const void *desc;
    const char *key;      /* primary key (FUEL: the item key) */
    int32_t id;           /* backend-assigned numeric id, -1 until prepare() assigns one */
} lce_plan_ext;

/* Reference resolution for backends: numeric id for ITEM/BLOCK refs, index
 * into the plan's ext list for mod content (+1000000), vanilla ids via the
 * backend's own tables. Returns -1 if unknown. */
typedef int32_t (*lce_resolve_fn)(const void *ctx, uint32_t ref_kind, const char *key);

#ifdef __cplusplus
}
#endif
