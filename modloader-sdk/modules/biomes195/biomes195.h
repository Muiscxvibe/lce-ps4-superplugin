/* biomes195 - new overworld biomes for Minecraft PS4 Edition 1.95 (CUSA00265,
 * analysis image c675e6ce...029f). See STATUS.md for the evidence of every RVA.
 *
 * Three pieces:
 *
 * 1. Registration (title screen, before worldgen195 publishes): each mod biome
 *    is a real PlainsBiome built by the retail ctor 0x53d140(this, id, 0, props)
 *    -> Biome base ctor 0x83e60, which itself stores biomes[id] = this
 *    (Biome* biomes[256] at 0x1409fc0, store at 0x840cd), the id at +0x68, the
 *    name wstring at +0x28 and inserts the name into the biome name map
 *    0x140a7c0. The 0x48-byte props object is built exactly like the static
 *    init 0x7f900 builds it (wstring name +0, depth +0x28, scale +0x2c,
 *    temperature +0x30, rainfall +0x34, water rgb +0x38, snow +0x3c, rain
 *    +0x3d, parent id +0x40) and is consumed (freed) by the base ctor. After
 *    the ctor the colour-enum fields the static init writes for plains
 *    (+0x8, +0x190, +0x194, +0x198) are copied from the stock plains object
 *    and the top/filler block (+0x50/+0x54, +0x58/+0x5c: id, data) are set.
 *
 * 2. Selection (the actual generation): BiomeInitLayer (vt 0x12163b8, ctor
 *    0x89ec0, built by Layer::getDefaultLayers 0x3e0d20 for every level's
 *    BiomeSource 0x8d4c0) keeps its four climate pools as PER-INSTANCE heap
 *    arrays {Biome** +0, int len +8} at +0x38 warm, +0x48 medium, +0x58 cold,
 *    +0x68 icy, and getArea 0x8a290 returns pool[nextRandom(len)]->id. The
 *    module replaces slot +0x20 (getArea) of the BiomeInitLayer vtable - a
 *    PT_SCE_RELRO data word at 0x12163d8, outside segment 0, so not part of
 *    game_text_sha256 - with lce_b195_get_area, which (once per layer object)
 *    swaps each pool for a new[] copy holding the stock entries followed by
 *    the mod biomes, then tail-calls the retail getArea. Every layer object
 *    is patched before its first getArea, so generation is deterministic for
 *    a given seed and mod set, including the spawn chunks.
 *
 * 3. Colours (optional): a mod biome with explicit grass/foliage/water/sky
 *    RGB gets a private vtable copy (RTTI prefix + all 27 slots) of its
 *    CURRENT vtable (worldgen195's private copy if it already published) with
 *    slots +0x60 grass, +0x68 foliage, +0xa8 water, +0x28 sky replaced. Its
 *    vptr is CASed. Pass worldgen195's adopt hook so its decorate gate keeps
 *    accepting the object (see worldgen195-adopt.patch).
 *
 * Return convention: 1 done, 0 clean refusal (nothing changed), -1 uncertain
 * (something may have changed; keep process-lifetime storage, do not retry).
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "../../include/lce_mod_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- pinned 1.95 RVAs (tests/test_image_pins.py) ----------------------- */
#define LCE_B195_RVA_BIOME_ARRAY     0x1409fc0u /* Biome* biomes[256], BSS */
#define LCE_B195_RVA_PLAINS_CTOR     0x53d140u  /* PlainsBiome(this, id, sunflower, props) -> base 0x83e60(this,id,0xb,props) */
#define LCE_B195_RVA_PLAINS_VT       0x12a2bb0u /* PlainsBiome vtable (27 slots + 0 terminator) */
#define LCE_B195_RVA_PLAINS_GLOBAL   0x140a810u /* Biome::plains (id 1) */
#define LCE_B195_RVA_WSTR_ASSIGN     0x67840u   /* wstring::assign(this, const wchar16*, n) */
#define LCE_B195_RVA_OP_NEW          0xe6cfd0u  /* operator new (PLT) */
#define LCE_B195_RVA_OP_NEW_ARR      0xe6cfc0u  /* operator new[] (PLT); BiomeInitLayer dtor 0x8a1b0 delete[]s the pools */
#define LCE_B195_RVA_BIL_VT          0x12163b8u /* BiomeInitLayer vtable (RELRO) */
#define LCE_B195_RVA_BIL_GETAREA     0x8a290u   /* BiomeInitLayer::getArea(this, x, z, w, h) -> {int*, len} */
#define LCE_B195_BIL_GETAREA_SLOT    4u         /* +0x20 */
#define LCE_B195_BIOME_SIZE          0x1d8u     /* PlainsBiome object size (static init alloc) */
#define LCE_B195_PROPS_SIZE          0x48u
#define LCE_B195_BIOME_SLOTS         27u
#define LCE_B195_MAX_BIOMES          32u
#define LCE_B195_POOL_MAX            64u        /* entries per climate pool after the rewrite */

/* Climate pools of BiomeInitLayer: offset of {Biome**, int} */
enum { LCE_B195_WARM = 1, LCE_B195_MEDIUM = 2, LCE_B195_COLD = 3, LCE_B195_ICY = 4 };

typedef struct { int32_t *data; uint32_t length; } lce_b195_intarr;   /* returned in rax:rdx */

typedef struct {
    void *(*op_new)(size_t n);
    void *(*op_new_arr)(size_t n);
    void (*wstr_assign)(void *ws, const uint16_t *s, size_t n);
    void (*plains_ctor)(void *self, int32_t id, int32_t sunflower, void *props);
    lce_b195_intarr (*bil_get_area)(void *self, int32_t x, int32_t z, int32_t w, int32_t h);
} lce_b195_natives;
void lce_b195_natives_bind(lce_b195_natives *n, uintptr_t base);   /* console only */

/* Checked memory for setup (protection-checked on the console). cas8: one
 * 8-byte aligned atomic compare-exchange of writable data, 1 = swapped.
 * patch_ro8: the same CAS on a READ-ONLY data page (RELRO): sceKernelMprotect
 * RW, CAS, restore R; 1 swapped, 0 expected value mismatch (nothing written),
 * -1 the protection could not be restored (uncertain). */
typedef struct {
    void *ctx;
    int (*read)(void *ctx, uintptr_t at, void *out, size_t n);
    int (*write)(void *ctx, uintptr_t at, const void *in, size_t n);
    int (*cas8)(void *ctx, uintptr_t at, uint64_t expected, uint64_t desired);
    int (*patch_ro8)(void *ctx, uintptr_t at, uint64_t expected, uint64_t desired);
} lce_b195_memory;

typedef struct {
    char key[64];                     /* "modid:name"; also the native biome name (name map key, F3) */
    uint8_t id;                       /* free biome id (ledger), see lce_b195_id_free */
    uint8_t climate;                  /* LCE_B195_WARM..ICY; 0 = from temperature */
    uint8_t snow, rain;               /* precipitation flags (props +0x3c/+0x3d) */
    float temperature, rainfall;      /* 0..2, 0..1 */
    float depth, scale;               /* terrain height base / variation (plains 0.125 / 0.05) */
    uint16_t top_id, top_meta;        /* surface block; top_id 0 = keep grass */
    uint16_t filler_id, filler_meta;  /* below-surface block; filler_id 0 = keep dirt */
    uint32_t grass_rgb, foliage_rgb, water_rgb, sky_rgb;   /* 0 = stock (colormap / colour table) */
    uint16_t copies;                  /* pool entries (vanilla entries are 1 each); 0 = never generated */
    uint16_t placeholder;             /* 1 = a missing mod's ledger id kept loadable (never generated) */
} lce_b195_biome;

/* Fill *out from an SDK descriptor. top/filler ids are resolved block ids (0 = keep).
 * weight: 10 = one vanilla pool entry (rounded, 1..16 entries); 0 = registered, not generated. */
lce_status lce_b195_from_desc(const lce_biome_desc *d, uint8_t id, uint16_t top_id, uint16_t top_meta,
                              uint16_t filler_id, uint16_t filler_meta, lce_b195_biome *out, char *err, uint32_t cap);

/* Ids that are never used by a stock 1.95 biome and are safe for mod biomes:
 * 168..254 (no RegionHills mutation +128 lookup: it applies to ids <= 127 only,
 * 0x5b874e) and 50..126 (their +128 mutation slot 178..254 must stay empty).
 * The module refuses an id whose biomes[] slot is not null at registration. */
int lce_b195_id_allowed(uint32_t id);

enum { LCE_B195_IDLE = 0, LCE_B195_READY = 1, LCE_B195_BUILT = 2, LCE_B195_SELECTING = 3, LCE_B195_UNCERTAIN = 4 };

typedef struct {
    uint32_t state, biome_count, built, coloured;
    uint64_t get_area_calls, layers_patched, patch_races, errors;
    char last_error[128];
} lce_b195_status;

/* Lifecycle (one instance per process):
 *   lce_b195_init         once, base attested.
 *   lce_b195_add          REGISTER/COMMIT (also placeholders), before construct.
 *   lce_b195_construct    title screen, after the Biome static init 0x7f900 and
 *                         BEFORE lce_wg195_publish (so worldgen195 hooks the mod
 *                         biomes' decorate like any PlainsBiome), before any world.
 *   lce_b195_colours      after lce_wg195_publish. adopt (may be NULL) is told
 *                         every private vptr so worldgen195's gate accepts it.
 *   lce_b195_select       after construct, before any world: installs the
 *                         BiomeInitLayer getArea slot (RELRO CAS).
 *   lce_b195_restore      optional: restores the getArea slot (patched layers keep
 *                         their pools; mod biome objects stay registered). */
int lce_b195_init(uintptr_t base, const lce_b195_natives *n, const lce_b195_memory *m);
int lce_b195_add(const lce_b195_biome *b, char *err, uint32_t cap);
int lce_b195_construct(void);
int lce_b195_colours(int (*adopt)(uintptr_t private_vptr, uint8_t dimension));
int lce_b195_select(void);
int lce_b195_restore(void);
void lce_b195_snapshot(lce_b195_status *out);
int32_t lce_b195_lookup(const char *key);           /* mod biome key -> id, -1 */
uintptr_t lce_b195_object(uint32_t id);              /* constructed mod biome object, 0 */

/* Installed into the BiomeInitLayer vtable slot +0x20. */
lce_b195_intarr lce_b195_get_area(void *self, int32_t x, int32_t z, int32_t w, int32_t h);
/* Rewrites one BiomeInitLayer's pools (idempotent). 1 patched now, 2 already, 0 not ready, -1 error. */
int lce_b195_patch_layer(uintptr_t layer);

/* Colour slots (exposed for tests). */
uint32_t lce_b195_grass(void *biome, const void *pos);
uint32_t lce_b195_foliage(void *biome, const void *pos);
uint32_t lce_b195_water(void *biome);
uint32_t lce_b195_sky(void *biome, float temperature);

/* Climate pool for a temperature when spec.climate == 0. */
uint8_t lce_b195_climate_of(float temperature);

#ifdef LCE_B195_TEST
void lce_b195_test_reset(void);
#endif

#ifdef __cplusplus
}
#endif
