/* worldgen195 - world generation backend module for Minecraft PS4 Edition 1.95
 * (CUSA00265, analysis image c675e6ce...029f). See STATUS.md for evidence.
 *
 * Mechanism (DATA hook only, no .text patch):
 *   Every Biome object lives on the heap and is listed in the BSS array
 *   Biome::biomes[256] at base+0x1409fc0 (filled by the Biome ctor 0x83e60 at
 *   0x840cd, id at Biome+0x68; 72 biomes are built by the static init 0x7f900).
 *   Each concrete Biome class has a 27-slot primary vtable (pinned in
 *   lce_wg195_biome_classes). Slot +0x58 is decorate(Biome*, Level*, Random*,
 *   const BlockPos*), called virtually by the chunk post-processors:
 *     OverworldLevelSource::postProcess 0x508bb0, call 0x509220
 *     FlatLevelSource::postProcess      0x2e6840, call 0x2e6fc8
 *     TheEndLevelSource::postProcess    0x6dec40, call 0x6ded84
 *   with pos = (chunkX*16, 0, chunkZ*16) and the level source's chunk-seeded
 *   Random (OverworldLevelSource+0x30, seeded at 0x508c4d..0x508cb8).
 *   publish() gives every distinct Biome class one private vtable copy (RTTI
 *   prefix + all 27 slots copied, only +0x58 replaced by
 *   lce_wg195_decorate_dispatch) and CASes each Biome object's vptr. The
 *   dispatch ALWAYS forwards to the original decorate first (stock generation
 *   unchanged), then places mod features inside the origin chunk only, using a
 *   PRIVATE Java Random derived from the stock Random's state captured before
 *   stock decoration (the live Random is never passed to native code by us and
 *   is verified unchanged afterwards).
 *
 * Return convention: 1 done, 0 clean refusal (nothing changed), -1 uncertain
 * (something may have changed; module disabled, keep process-lifetime storage).
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "../../include/lce_mod_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- pinned 1.95 RVAs (all verified by tests/audit.py against the image) -- */
#define LCE_WG195_RVA_BIOME_ARRAY      0x1409fc0u /* Biome* biomes[256], BSS (RW) */
#define LCE_WG195_RVA_BIOME_CTOR       0x83e60u   /* Biome::Biome(this,id,?,props); stores id +0x68, array at 0x840cd */
#define LCE_WG195_RVA_BIOME_BASE_VT    0x1216218u /* abstract Biome vtable (slot 18 pure) */
#define LCE_WG195_RVA_BIOME_DECORATE   0x85430u   /* Biome::decorate -> (Biome+0x60)->vt[+0x10] */
#define LCE_WG195_RVA_SERVER_LEVEL_VT  0x1340d50u /* ServerLevel primary vtable */
#define LCE_WG195_RVA_ORE_VT           0x12991d8u /* OreFeature primary vtable (place at +0x10) */
#define LCE_WG195_RVA_ORE_CTOR         0x503e10u  /* OreFeature(this, BlockState*, size): fields we write directly */
#define LCE_WG195_RVA_ORE_PLACE        0x503f60u  /* bool place(feature, Level*, Random*, const BlockPos*) */
#define LCE_WG195_RVA_STONE_PREDICATE  0x1401258u /* OreFeature::NaturalStonePredicate static (const-initialised) */
#define LCE_WG195_RVA_SET_TILE         0x3efa80u  /* bool Level::setTileAndData(level,x,y,z,id,meta,flags) */
#define LCE_WG195_RVA_HEIGHTMAP        0x3f20c0u  /* int Level::getHeightmap(level,x,z) */
#define LCE_WG195_RVA_TREE_VT          0x12e6598u /* TreeFeature primary vtable */
#define LCE_WG195_RVA_TREE_CTOR        0x6f5b30u  /* TreeFeature(this,doUpdate,baseHeight,trunk*,leaves*,vines) */
#define LCE_WG195_RVA_TREE_PLACE       0x6f5eb0u
#define LCE_WG195_RVA_LAKE_VT          0x1273c48u /* LakeFeature primary vtable */
#define LCE_WG195_RVA_LAKE_CTOR        0x3db8d0u  /* LakeFeature(this, Block*) -> +0x10 */
#define LCE_WG195_RVA_LAKE_PLACE       0x3db900u

#define LCE_WG195_BIOME_SLOTS     27u   /* primary vtable slots of every concrete Biome class */
#define LCE_WG195_DECORATE_SLOT   (0x58u / 8u)
#define LCE_WG195_MAX_BIOMES      256u
#define LCE_WG195_MAX_FEATURES    32u
#define LCE_WG195_MAX_CLASSES     32u
#define LCE_WG195_ORE_FLAGS       0x74  /* flags OreFeature itself passes to 0x3efa80 (0x504920) */
#define LCE_WG195_STRUCT_FLAGS    2     /* flags a non-updating Feature::setBlock (0x2a4960) uses */

typedef struct {
    uint32_t vtable, typeinfo, decorate;   /* RVAs */
    uint8_t dimension;                      /* 0 overworld/flat, 1 nether (never decorated), 2 end, 255 never */
    const char *name;
} lce_wg195_biome_class;
extern const lce_wg195_biome_class lce_wg195_biome_classes[];
extern const uint32_t lce_wg195_biome_class_count;

typedef struct { uint8_t id; const char *name; } lce_wg195_biome_name;
extern const lce_wg195_biome_name lce_wg195_biome_names[];   /* 72 native 1.95 biome ids/names (from 0x7f900) */
extern const uint32_t lce_wg195_biome_name_count;
/* "minecraft:plains", "plains" or a Java alias ("swamp", "mountains"...): id or -1. */
int32_t lce_wg195_biome_lookup(const char *key);

/* ---- natives (bound from pinned RVAs on the console, fakes in tests) ------ */
typedef struct {
    uint8_t (*ore_place)(void *feature, void *level, void *random, const int32_t *pos);
    uint8_t (*set_tile)(void *level, int32_t x, int32_t y, int32_t z, int32_t id, int32_t meta, int32_t flags);
    int32_t (*heightmap)(void *level, int32_t x, int32_t z);
    void (*tree_ctor)(void *self, int32_t do_update, int32_t base_height, void *trunk_state, void *leaves_state, int32_t vines);
    void (*lake_ctor)(void *self, void *block);
} lce_wg195_natives;
void lce_wg195_natives_bind(lce_wg195_natives *n, uintptr_t base);   /* console only */

/* Checked memory (sceKernelQueryMemoryProtection on the console). cas8 must be
 * a single 8-byte aligned atomic compare-exchange; 1 = swapped. */
typedef struct {
    void *ctx;
    int (*read)(void *ctx, uintptr_t at, void *out, size_t n);
    int (*cas8)(void *ctx, uintptr_t at, uint64_t expected, uint64_t desired);
} lce_wg195_memory;

/* ---- features ------------------------------------------------------------ */
typedef struct {
    uint16_t size_x, size_y, size_z;   /* size_x,size_z <= 16 (must fit one chunk) */
    uint16_t palette_count;            /* <= 256 */
    const uint16_t *palette;           /* (id<<4)|meta per entry; index 0 = keep existing */
    const uint16_t *cells;             /* size_x*size_y*size_z, x fastest, then z, then y */
} lce_wg195_structure;                 /* caller keeps storage alive for the process */

typedef struct {
    uint32_t struct_size;
    uint16_t kind;                     /* lce_feature_kind: ORE, TREE, LAKE, STRUCTURE (FLOWER refused) */
    uint8_t dimension;                 /* 0 overworld, 2 end (1 nether refused: no biome decorate) */
    uint8_t reserved;
    uint32_t salt;                     /* RNG salt (lce_wg195_hash(key)) */
    uint16_t block_id, block_meta;     /* ORE: placed block (1..365, meta 0..15) */
    int32_t replace_id;                /* ORE: 0 = native natural stone predicate */
    int32_t replace_meta;              /* ORE: -1 any */
    uint16_t size, per_chunk;          /* ORE: vein size 2..24 / veins 1..64; TREE/LAKE/STRUCTURE: attempts 1..8 */
    uint16_t min_y, max_y;             /* 0..255; STRUCTURE/TREE 0,0 = surface */
    uint16_t rarity;                   /* 1 in N chunks, 0/1 = every chunk */
    uint16_t tree_base_height;         /* TREE: 4 = oak; 3..8 */
    void *trunk_state, *leaves_state;  /* TREE: BlockState* (Block+0x190 default state) */
    void *lake_block;                  /* LAKE: Block* (LakeFeature+0x10) */
    const lce_wg195_structure *structure;
    uint64_t biome_mask[4];            /* bit = biome id; all zero = every biome of the dimension */
} lce_wg195_feature;

uint32_t lce_wg195_hash(const char *key);
/* Fill *out from an SDK descriptor with resolved numeric block ids. Biome keys
 * resolve through lce_wg195_biome_lookup. Returns LCE_OK or an error with err. */
lce_status lce_wg195_from_desc(const lce_feature_desc *d, uint16_t block_id, uint8_t block_meta,
                               int32_t replace_id, int32_t replace_meta, lce_wg195_feature *out,
                               char *err, uint32_t cap);

enum { LCE_WG195_IDLE = 0, LCE_WG195_READY = 1, LCE_WG195_PUBLISHED = 2, LCE_WG195_RESTORED = 3,
       LCE_WG195_UNCERTAIN = 4 };

typedef struct {
    uint32_t state, feature_count, biome_count, class_count;
    uint64_t calls, decorated, skipped, placements, writes_refused, errors;
    char last_error[128];
} lce_wg195_status;

/* Lifecycle (one module instance per process):
 *   lce_wg195_init         any thread, once; base attested.
 *   lce_wg195_add_feature  REGISTER/COMMIT, before prepare.
 *   lce_wg195_prepare      after blocks are registered (tree/lake ctors run here).
 *   lce_wg195_publish      after the Biome static init 0x7f900 ran and BEFORE the
 *                          first world is created/loaded (title screen).
 *   lce_wg195_restore      optional (world unload / disable); storage stays alive. */
int lce_wg195_init(uintptr_t base, const lce_wg195_natives *n, const lce_wg195_memory *m);
int lce_wg195_add_feature(const lce_wg195_feature *f, char *err, uint32_t cap);
int lce_wg195_prepare(void);
int lce_wg195_publish(void);
int lce_wg195_restore(void);
void lce_wg195_snapshot(lce_wg195_status *out);
/* biomes195 integration: accept a mod biome's private colour vtable (a chained copy of
 * our PlainsBiome copy) in the decorate gate; resolve mod biome keys in feature biome lists. */
int lce_wg195_adopt_vptr(uintptr_t vptr, uint8_t dimension);
void lce_wg195_set_mod_biome_lookup(int32_t (*fn)(const char *key));

/* Installed into slot +0x58 of the private vtables. */
uintptr_t lce_wg195_decorate_dispatch(void *biome, void *level, void *random, const int32_t *pos);

/* Pure helpers (exposed for tests). */
typedef struct { uint64_t seed; uint8_t have_gaussian, pad[7]; double gaussian; uint64_t spare; } lce_wg195_random; /* 0x20, native layout: seed +0, flag +8 (0x596c70) */
void lce_wg195_random_seed(lce_wg195_random *r, uint64_t seed);
int32_t lce_wg195_random_int(lce_wg195_random *r, int32_t n);
/* Allowed vein-centre range in chunk-local coordinates for an ore size. */
int lce_wg195_ore_centre_range(uint32_t size, int32_t *lo, int32_t *hi);

#ifdef LCE_WG195_TEST
void lce_wg195_test_reset(void);
#endif

#ifdef __cplusplus
}
#endif
