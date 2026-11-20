// worldgen195: Biome vptr data hook + chunk-local mod feature placement.
// See worldgen195.h for the contract and STATUS.md for evidence per RVA.
#include "worldgen195.h"
#include <stdio.h>
#include <string.h>

/* ---- pinned tables ------------------------------------------------------ */
// vtable, typeinfo, stock decorate (+0x58), dimension. Generated from the image
// and re-verified by tests/audit.py (RTTI names, 27 slots, ctor/dtor-only refs).
const lce_wg195_biome_class lce_wg195_biome_classes[] = {
    {0x120bf90u, 0x1212670u, 0x41880u, 0, "BambooJungleBiome"},
    {0x1210488u, 0x1214630u, 0x85430u, 0, "BeachBiome"},
    {0x12378e0u, 0x1241d80u, 0x1fa410u, 0, "DesertBiome"},
    {0x124f350u, 0x1254c30u, 0x29fe00u, 0, "ExtremeHillsBiome"},
    {0x12596d8u, 0x1261d10u, 0x2f6ad0u, 0, "ForestBiome"},
    {0x1267970u, 0x1270f40u, 0x85430u, 1, "HellBiome"},
    {0x126c830u, 0x1271fb0u, 0x373b20u, 0, "IceBiome"},
    {0x12705f8u, 0x12724b0u, 0x3c5010u, 0, "JungleBiome"},
    {0x12808b8u, 0x1286080u, 0x85430u, 0, "MesaBiome"},
    {0x128ce70u, 0x1293720u, 0x2f6ad0u, 0, "MutatedForestBiome"},
    {0x128cf58u, 0x1293740u, 0x4a23f0u, 0, "MutatedSavannaBiome"},
    {0x128cc80u, 0x1293c80u, 0x85430u, 0, "MushroomIslandBiome"},
    {0x1295688u, 0x12a6a20u, 0x85430u, 0, "OceanBiome"},
    {0x1295770u, 0x12a6a40u, 0x85430u, 0, "OceanColdBiome"},
    {0x1295858u, 0x12a6a60u, 0x4d74f0u, 0, "OceanFrozenBiome"},
    {0x1295940u, 0x12a6a80u, 0x85430u, 0, "OceanLukewarmBiome"},
    {0x1296ff8u, 0x12a6f40u, 0x4fb580u, 0, "OceanWarmBiome"},
    {0x12a2bb0u, 0x12a7610u, 0x53d4c0u, 0, "PlainsBiome"},
    {0x12b6f48u, 0x12b8420u, 0x5c6ae0u, 0, "SavannaBiome"},
    {0x12b4130u, 0x12b84b0u, 0x85430u, 0, "RiverBiome"},
    {0x12d3fe8u, 0x12da840u, 0x85430u, 0, "StoneBeachBiome"},
    {0x12ddf70u, 0x12e6ec0u, 0x6ca350u, 0, "SwampBiome"},
    {0x12dead8u, 0x12e7040u, 0x6d1950u, 0, "TaigaBiome"},
    {0x12e0610u, 0x12e72c0u, 0x85430u, 2, "TheEndBiome"},
    {0x12efe30u, 0x12fe220u, 0x85430u, 255, "VoidBiome"},
};
const uint32_t lce_wg195_biome_class_count = sizeof lce_wg195_biome_classes / sizeof lce_wg195_biome_classes[0];

// Native ids and names, in registration order of the static init 0x7f900
// (id = esi of each Biome subclass ctor call, name = the wstring built before it).
const lce_wg195_biome_name lce_wg195_biome_names[] = {
    {0, "ocean"}, {24, "deep_ocean"}, {42, "warm_ocean"}, {43, "deep_warm_ocean"}, {44, "lukewarm_ocean"},
    {45, "deep_lukewarm_ocean"}, {46, "cold_ocean"}, {47, "deep_cold_ocean"}, {48, "frozen_ocean"},
    {49, "deep_frozen_ocean"}, {1, "plains"}, {2, "desert"}, {3, "extreme_hills"}, {4, "forest"}, {5, "taiga"},
    {6, "swampland"}, {7, "river"}, {8, "hell"}, {9, "the_end"}, {10, "legacy_frozen_ocean"}, {11, "frozen_river"},
    {12, "ice_plains"}, {13, "ice_mountains"}, {14, "mushroom_island"}, {15, "mushroom_island_shore"}, {16, "beach"},
    {17, "desert_hills"}, {18, "forest_hills"}, {19, "taiga_hills"}, {20, "extreme_hills_edge"}, {21, "jungle"},
    {22, "jungle_hills"}, {23, "jungle_edge"}, {40, "jungle_bamboo"}, {41, "jungle_bamboo_hills"}, {25, "stone_beach"},
    {26, "cold_beach"}, {27, "birch_forest"}, {28, "birch_forest_hills"}, {29, "roofed_forest"}, {30, "cold_taiga"},
    {31, "cold_taiga_hills"}, {32, "mega_taiga"}, {33, "mega_taiga_hills"}, {34, "extreme_hills_plus_trees"},
    {35, "savanna"}, {36, "savanna_plateau"}, {37, "mesa"}, {38, "mesa_plateau_stone"}, {39, "mesa_plateau"},
    {127, "the_void"}, {129, "sunflower_plains"}, {130, "desert_mutated"}, {131, "extreme_hills_mutated"},
    {132, "flower_forest"}, {133, "taiga_mutated"}, {134, "swampland_mutated"}, {140, "ice_plains_spikes"},
    {149, "jungle_mutated"}, {151, "jungle_edge_mutated"}, {155, "birch_forest_mutated"},
    {156, "birch_forest_hills_mutated"}, {157, "roofed_forest_mutated"}, {158, "cold_taiga_mutated"},
    {160, "mega_spruce_taiga"}, {161, "redwood_taiga_hills_mutated"}, {162, "extreme_hills_plus_trees_mutated"},
    {163, "savanna_mutated"}, {164, "savanna_plateau_mutated"}, {165, "mesa_bryce"},
    {166, "mesa_plateau_stone_mutated"}, {167, "mesa_plateau_mutated"},
};
const uint32_t lce_wg195_biome_name_count = sizeof lce_wg195_biome_names / sizeof lce_wg195_biome_names[0];

namespace {

// Java 1.13+ registry names (what Forge/Fabric/Bedrock ports use) -> native id.
struct Alias { const char *name; uint8_t id; };
const Alias kAliases[] = {
    {"mountains", 3}, {"swamp", 6}, {"nether", 8}, {"nether_wastes", 8}, {"snowy_tundra", 12}, {"snowy_plains", 12},
    {"snowy_mountains", 13}, {"mushroom_fields", 14}, {"mushroom_field_shore", 15}, {"wooded_hills", 18},
    {"mountain_edge", 20}, {"stone_shore", 25}, {"stony_shore", 25}, {"snowy_beach", 26}, {"dark_forest", 29},
    {"snowy_taiga", 30}, {"snowy_taiga_hills", 31}, {"giant_tree_taiga", 32}, {"old_growth_pine_taiga", 32},
    {"giant_tree_taiga_hills", 33}, {"wooded_mountains", 34}, {"windswept_forest", 34}, {"badlands", 37},
    {"wooded_badlands_plateau", 38}, {"wooded_badlands", 38}, {"badlands_plateau", 39}, {"bamboo_jungle", 40},
    {"bamboo_jungle_hills", 41}, {"desert_lakes", 130}, {"gravelly_mountains", 131}, {"windswept_gravelly_hills", 131},
    {"taiga_mountains", 133}, {"swamp_hills", 134}, {"ice_spikes", 140}, {"modified_jungle", 149},
    {"modified_jungle_edge", 151}, {"tall_birch_forest", 155}, {"old_growth_birch_forest", 155},
    {"tall_birch_hills", 156}, {"dark_forest_hills", 157}, {"snowy_taiga_mountains", 158},
    {"giant_spruce_taiga", 160}, {"old_growth_spruce_taiga", 160}, {"giant_spruce_taiga_hills", 161},
    {"modified_gravelly_mountains", 162}, {"shattered_savanna", 163}, {"windswept_savanna", 163},
    {"shattered_savanna_plateau", 164}, {"eroded_badlands", 165}, {"modified_wooded_badlands_plateau", 166},
    {"modified_badlands_plateau", 167}, {"sparse_jungle", 23}, {"end", 9},
};

struct ClassCopy {
    uint32_t cls;                       // index into lce_wg195_biome_classes
    alignas(16) uintptr_t vt[2 + LCE_WG195_BIOME_SLOTS];
};
struct BiomeRec {
    uintptr_t object, original_vptr, original_decorate, private_vptr;
    uint8_t dimension, installed;
};
struct Feature {
    lce_wg195_feature d;
    alignas(16) uint8_t object[0x40];   // native feature instance (process lifetime)
    alignas(16) uintptr_t pred[3];      // custom replace predicate: {vptr, id, meta}
};

typedef uintptr_t (*decorate_fn)(void *, void *, void *, const int32_t *);
typedef uint8_t (*place_fn)(void *, void *, void *, const int32_t *);

// Private predicate vtable: slot 0 = test(pred, const int32 {id,meta}*),
// called by OreFeature::place at 0x5048f3. RTTI slot 0 (no type claimed).
uint8_t pred_test(void *self, const int32_t *idmeta);
void pred_nop(void *) {}
alignas(16) const uintptr_t kPredVt[5] = {0, 0, (uintptr_t)&pred_test, (uintptr_t)&pred_nop, (uintptr_t)&pred_nop};

struct Engine {
    uint32_t state;
    uintptr_t base;
    lce_wg195_natives n;
    lce_wg195_memory m;
    uint32_t feature_count, biome_count, class_count;
    Feature features[LCE_WG195_MAX_FEATURES];
    BiomeRec biomes[LCE_WG195_MAX_BIOMES];     // indexed by biome id
    ClassCopy classes[LCE_WG195_MAX_CLASSES];
    uint64_t calls, decorated, skipped, placements, writes_refused, errors;
    uint32_t disabled;
    char last_error[128];
    uintptr_t adopted_vptr[32];                // biomes195 colour vtables (chained copies of ours)
    uint8_t adopted_dim[32];
    uint32_t adopted_count;
    int32_t (*mod_biome_lookup)(const char *key);   // biomes195 lce_b195_lookup
};
Engine g;

void fail(const char *msg) { snprintf(g.last_error, sizeof g.last_error, "%s", msg); }
void set_err(char *err, uint32_t cap, const char *msg) { if (err && cap) snprintf(err, cap, "%s", msg); }
bool rd(uintptr_t a, void *v, size_t n) { return a && g.m.read(g.m.ctx, a, v, n) == 1; }
template <class T> void add(T &c, T v = 1) { __atomic_add_fetch(&c, v, __ATOMIC_RELAXED); }

uint8_t pred_test(void *self, const int32_t *idmeta) {
    const uintptr_t *p = (const uintptr_t *)self;
    return idmeta && (uintptr_t)(uint32_t)idmeta[0] == p[1] && ((intptr_t)p[2] < 0 || (uintptr_t)(uint32_t)idmeta[1] == p[2]);
}

uint64_t mix(uint64_t x) {
    x ^= x >> 30; x *= UINT64_C(0xbf58476d1ce4e5b9); x ^= x >> 27; x *= UINT64_C(0x94d049bb133111eb); return x ^ (x >> 31);
}

int32_t floordiv16(int32_t v) { return v >= 0 ? v / 16 : -1 - (int32_t)((-(int64_t)v - 1) / 16); }

int dimension_of_vptr(uintptr_t vptr) {
    for (uint32_t i = 0; i < g.class_count; ++i)
        if (vptr == (uintptr_t)&g.classes[i].vt[2]) return lce_wg195_biome_classes[g.classes[i].cls].dimension;
    uint32_t n = __atomic_load_n(&g.adopted_count, __ATOMIC_ACQUIRE);
    for (uint32_t i = 0; i < n; ++i)
        if (vptr == g.adopted_vptr[i]) return g.adopted_dim[i];
    return -1;
}

bool biome_allowed(const lce_wg195_feature &f, uint32_t id) {
    if (!(f.biome_mask[0] | f.biome_mask[1] | f.biome_mask[2] | f.biome_mask[3])) return true;
    return id < 256 && (f.biome_mask[id >> 6] >> (id & 63)) & 1;
}

bool valid_feature(const lce_wg195_feature &f, char *err, uint32_t cap) {
    if (f.struct_size != sizeof f) { set_err(err, cap, "worldgen195: struct_size mismatch"); return false; }
    if (f.dimension == 1) { set_err(err, cap, "worldgen195: nether features unsupported (NetherLevelSource::postProcess 0x4b8090 never calls Biome decorate)"); return false; }
    if (f.dimension != 0 && f.dimension != 2) { set_err(err, cap, "worldgen195: dimension must be 0 or 2"); return false; }
    if (f.min_y > f.max_y || f.max_y > 255) { set_err(err, cap, "worldgen195: bad y range"); return false; }
    if (f.rarity > 10000) { set_err(err, cap, "worldgen195: rarity > 10000"); return false; }
    switch (f.kind) {
    case LCE_FEATURE_ORE: {
        int32_t lo, hi;
        if (!f.block_id || f.block_id > 365 || f.block_meta > 15) { set_err(err, cap, "worldgen195: ore block id 1..365, meta 0..15"); return false; }
        if (!lce_wg195_ore_centre_range(f.size, &lo, &hi)) { set_err(err, cap, "worldgen195: ore size must be 2..24 (chunk-local vein bound)"); return false; }
        if (!f.per_chunk || f.per_chunk > 64) { set_err(err, cap, "worldgen195: ore veins per chunk 1..64"); return false; }
        if (f.replace_id < 0 || f.replace_id > 365 || f.replace_meta < -1 || f.replace_meta > 15) { set_err(err, cap, "worldgen195: bad replace block"); return false; }
        return true;
    }
    case LCE_FEATURE_TREE:
        if (!f.trunk_state || !f.leaves_state || f.tree_base_height < 3 || f.tree_base_height > 8 || !f.per_chunk || f.per_chunk > 8) {
            set_err(err, cap, "worldgen195: tree needs trunk/leaves BlockState*, base height 3..8, 1..8 per chunk"); return false;
        }
        return true;
    case LCE_FEATURE_LAKE:
        if (!f.lake_block || f.per_chunk != 1) { set_err(err, cap, "worldgen195: lake needs a Block* and per_chunk 1 (one 16x8x16 box per chunk)"); return false; }
        if (f.min_y < 6) { set_err(err, cap, "worldgen195: lake min_y >= 6 (LakeFeature refuses y < 5 after descending)"); return false; }
        return true;
    case LCE_FEATURE_STRUCTURE: {
        const lce_wg195_structure *s = f.structure;
        if (!s || !s->palette || !s->cells || !s->size_x || !s->size_y || !s->size_z || s->size_x > 16 || s->size_z > 16 ||
            s->size_y > 255 || !s->palette_count || !f.per_chunk || f.per_chunk > 8) {
            set_err(err, cap, "worldgen195: structure must be 1..16 x 1..255 x 1..16 with a palette (one chunk, no cross-chunk writes)"); return false;
        }
        for (uint32_t i = 1; i < s->palette_count; ++i)
            if ((s->palette[i] >> 4) > 365) { set_err(err, cap, "worldgen195: structure palette block id > 365"); return false; }
        uint32_t cells = (uint32_t)s->size_x * s->size_y * s->size_z;
        for (uint32_t i = 0; i < cells; ++i)
            if (s->cells[i] >= s->palette_count) { set_err(err, cap, "worldgen195: structure cell outside palette"); return false; }
        return true;
    }
    default:
        set_err(err, cap, "worldgen195: feature kind unsupported (flowers need a FlowerBlock subclass; see STATUS.md)");
        return false;
    }
}

// ---- per-chunk placement (inside the original chunk only) ----------------
bool place_one(Feature &F, void *level, int32_t ox, int32_t oz, lce_wg195_random &pick, lce_wg195_random &native_rng) {
    const lce_wg195_feature &f = F.d;
    switch (f.kind) {
    case LCE_FEATURE_ORE: {
        int32_t lo = 0, hi = 0;
        lce_wg195_ore_centre_range(f.size, &lo, &hi);
        for (uint32_t v = 0; v < f.per_chunk; ++v) {
            int32_t cx = lo + lce_wg195_random_int(&pick, hi - lo + 1);
            int32_t y = f.min_y + lce_wg195_random_int(&pick, f.max_y - f.min_y + 1);
            int32_t cz = lo + lce_wg195_random_int(&pick, hi - lo + 1);
            // OreFeature::place adds +8 to x and z itself (0x503fc9, 0x50403e).
            int32_t pos[3] = {ox + cx - 8, y, oz + cz - 8};
            g.n.ore_place(F.object, level, &native_rng, pos);
            add(g.placements);
        }
        return true;
    }
    case LCE_FEATURE_TREE: {
        uintptr_t vt = *(const uintptr_t *)F.object;
        place_fn place = *(place_fn *)(vt + 0x10);
        for (uint32_t v = 0; v < f.per_chunk; ++v) {
            int32_t x = ox + 3 + lce_wg195_random_int(&pick, 10), z = oz + 3 + lce_wg195_random_int(&pick, 10);
            int32_t y = f.max_y ? f.min_y + lce_wg195_random_int(&pick, f.max_y - f.min_y + 1) : g.n.heightmap(level, x, z);
            if (y < 1 || y + f.tree_base_height + 4 > 255) { add(g.writes_refused); continue; }
            int32_t pos[3] = {x, y, z};
            place(F.object, level, &native_rng, pos);
            add(g.placements);
        }
        return true;
    }
    case LCE_FEATURE_LAKE: {
        uintptr_t vt = *(const uintptr_t *)F.object;
        place_fn place = *(place_fn *)(vt + 0x10);
        // LakeFeature::place offsets pos by (-8,0,-8) (0x3db933..0x3db959) and
        // fills a 16-wide box: centre +8 keeps the box exactly in this chunk.
        int32_t y = f.min_y + lce_wg195_random_int(&pick, f.max_y - f.min_y + 1);
        int32_t pos[3] = {ox + 8, y, oz + 8};
        place(F.object, level, &native_rng, pos);
        add(g.placements);
        return true;
    }
    case LCE_FEATURE_STRUCTURE: {
        const lce_wg195_structure &s = *f.structure;
        for (uint32_t v = 0; v < f.per_chunk; ++v) {
            int32_t lx = lce_wg195_random_int(&pick, 17 - s.size_x), lz = lce_wg195_random_int(&pick, 17 - s.size_z);
            int32_t y;
            if (f.max_y) y = f.min_y + lce_wg195_random_int(&pick, f.max_y - f.min_y + 1);
            else y = g.n.heightmap(level, ox + lx + s.size_x / 2, oz + lz + s.size_z / 2);
            if (y < 1 || y + s.size_y > 256) { add(g.writes_refused); continue; }
            for (uint32_t yy = 0; yy < s.size_y; ++yy)
                for (uint32_t zz = 0; zz < s.size_z; ++zz)
                    for (uint32_t xx = 0; xx < s.size_x; ++xx) {
                        uint16_t idx = s.cells[(yy * s.size_z + zz) * s.size_x + xx];
                        if (!idx) continue;
                        int32_t wx = ox + lx + (int32_t)xx, wz = oz + lz + (int32_t)zz;
                        if (wx < ox || wx >= ox + 16 || wz < oz || wz >= oz + 16) { add(g.writes_refused); continue; }
                        uint16_t b = s.palette[idx];
                        g.n.set_tile(level, wx, y + (int32_t)yy, wz, b >> 4, b & 15, LCE_WG195_STRUCT_FLAGS);
                    }
            add(g.placements);
        }
        return true;
    }
    }
    return false;
}

void decorate_mods(BiomeRec &b, void *level, const int32_t *pos, uint64_t snapshot) {
    int32_t ox = pos[0], oz = pos[2];
    uint32_t id = (uint32_t)(&b - g.biomes);
    for (uint32_t i = 0; i < g.feature_count; ++i) {
        Feature &F = g.features[i];
        if (F.d.dimension != b.dimension || !biome_allowed(F.d, id)) continue;
        uint64_t s = mix(snapshot ^ ((uint64_t)F.d.salt << 16) ^ ((uint64_t)(uint32_t)floordiv16(ox) << 32) ^ (uint32_t)floordiv16(oz) ^ i);
        lce_wg195_random pick, native_rng;
        lce_wg195_random_seed(&pick, s);
        lce_wg195_random_seed(&native_rng, mix(s ^ UINT64_C(0x6c63655f77673139)));
        if (F.d.rarity > 1 && lce_wg195_random_int(&pick, F.d.rarity) != 0) continue;
        place_one(F, level, ox, oz, pick, native_rng);
    }
}

bool find_class(uintptr_t vptr, uint32_t *out) {
    for (uint32_t c = 0; c < lce_wg195_biome_class_count; ++c)
        if (vptr == g.base + lce_wg195_biome_classes[c].vtable) { *out = c; return true; }
    return false;
}

}  // namespace

/* ---- public helpers ------------------------------------------------------ */
extern "C" void lce_wg195_random_seed(lce_wg195_random *r, uint64_t seed) {
    memset(r, 0, sizeof *r);
    r->seed = (seed ^ UINT64_C(0x5deece66d)) & ((UINT64_C(1) << 48) - 1);   // == 0x596c70
}
static int32_t next_bits(lce_wg195_random *r, int bits) {
    r->seed = (r->seed * UINT64_C(0x5deece66d) + 0xb) & ((UINT64_C(1) << 48) - 1);
    return (int32_t)(r->seed >> (48 - bits));
}
extern "C" int32_t lce_wg195_random_int(lce_wg195_random *r, int32_t n) {
    if (n <= 0) return 0;
    if ((n & -n) == n) return (int32_t)(((int64_t)n * next_bits(r, 31)) >> 31);
    int32_t bits, val;
    do { bits = next_bits(r, 31); val = bits % n; } while (bits - val + (n - 1) < 0);
    return val;
}
extern "C" int lce_wg195_ore_centre_range(uint32_t size, int32_t *lo, int32_t *hi) {
    // OreFeature::place: centre +- size/8 (0x503ff6, x0.125), radius <= size/16+0.5
    // (0x504147 x0.0625, 0x5041b8 +1.0, 0x5041f2 x0.5), plus 1/16 float error
    // (steel_ore_own_chunk_195 audit). Horizontal reach k = 3*size/16 + 9/16 + eps.
    // Keep floor(c-k) >= 1 and floor(c+k) <= 14 (one block margin each side).
    if (size < 2 || size > 24) return 0;
    int32_t f = (int32_t)(3 * size + 9) / 16;
    *lo = f + 2; *hi = 14 - f;
    return *lo <= *hi;
}
extern "C" uint32_t lce_wg195_hash(const char *key) {
    uint32_t h = 2166136261u;
    for (const unsigned char *p = (const unsigned char *)key; p && *p; ++p) h = (h ^ *p) * 16777619u;
    return h;
}
extern "C" int32_t lce_wg195_biome_lookup(const char *key) {
    if (!key) return -1;
    if (!strncmp(key, "minecraft:", 10)) key += 10;
    for (uint32_t i = 0; i < lce_wg195_biome_name_count; ++i)
        if (!strcmp(key, lce_wg195_biome_names[i].name)) return lce_wg195_biome_names[i].id;
    for (const Alias &a : kAliases) if (!strcmp(key, a.name)) return a.id;
    return g.mod_biome_lookup ? g.mod_biome_lookup(key) : -1;
}

// biomes195 integration: mod biomes keep worldgen's decorate dispatch in their colour vtable.
extern "C" int lce_wg195_adopt_vptr(uintptr_t vptr, uint8_t dimension) {
    if (!vptr || dimension > 2 || dimension == 1) return 0;
    uint32_t n = g.adopted_count;
    if (n >= 32) return 0;
    g.adopted_vptr[n] = vptr; g.adopted_dim[n] = dimension;
    __atomic_store_n(&g.adopted_count, n + 1, __ATOMIC_RELEASE);
    return 1;
}
extern "C" void lce_wg195_set_mod_biome_lookup(int32_t (*fn)(const char *key)) { g.mod_biome_lookup = fn; }

extern "C" lce_status lce_wg195_from_desc(const lce_feature_desc *d, uint16_t block_id, uint8_t block_meta,
                                          int32_t replace_id, int32_t replace_meta, lce_wg195_feature *o,
                                          char *err, uint32_t cap) {
    if (!d || !o || d->struct_size < sizeof(*d) || !d->key) { set_err(err, cap, "worldgen195: invalid descriptor"); return LCE_E_INVALID; }
    memset(o, 0, sizeof *o);
    o->struct_size = sizeof *o; o->kind = d->kind; o->dimension = d->dimension; o->salt = lce_wg195_hash(d->key);
    o->block_id = block_id; o->block_meta = block_meta; o->replace_id = replace_id; o->replace_meta = replace_meta;
    o->size = d->size; o->per_chunk = d->per_chunk; o->min_y = d->min_y; o->max_y = d->max_y; o->rarity = d->rarity;
    if (d->kind == LCE_FEATURE_ORE && !d->max_y) o->max_y = 64;
    if (d->kind == LCE_FEATURE_TREE) o->tree_base_height = 4;
    for (uint32_t i = 0; d->biomes && i < d->biome_count; ++i) {
        int32_t id = lce_wg195_biome_lookup(d->biomes[i]);
        if (id < 0 || id >= 256) { set_err(err, cap, "worldgen195: unknown or out-of-range biome key"); return LCE_E_NOTFOUND; }
        o->biome_mask[id >> 6] |= UINT64_C(1) << (id & 63);
    }
    if (d->kind == LCE_FEATURE_FLOWER || d->kind == LCE_FEATURE_TREE || d->kind == LCE_FEATURE_LAKE || d->kind == LCE_FEATURE_STRUCTURE) {
        // tree/lake/structure need native object pointers / templates the caller fills in afterwards
        if (d->kind == LCE_FEATURE_FLOWER) { set_err(err, cap, "worldgen195: flowers unsupported (FlowerFeature 0x2ea330 needs a FlowerBlock)"); return LCE_E_UNSUPPORTED; }
        return LCE_OK;
    }
    char e[128];
    if (!valid_feature(*o, e, sizeof e)) { set_err(err, cap, e); return LCE_E_INVALID; }
    return LCE_OK;
}

/* ---- lifecycle ----------------------------------------------------------- */
extern "C" void lce_wg195_natives_bind(lce_wg195_natives *n, uintptr_t base) {
    n->ore_place = (uint8_t (*)(void *, void *, void *, const int32_t *))(base + LCE_WG195_RVA_ORE_PLACE);
    n->set_tile = (uint8_t (*)(void *, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t))(base + LCE_WG195_RVA_SET_TILE);
    n->heightmap = (int32_t (*)(void *, int32_t, int32_t))(base + LCE_WG195_RVA_HEIGHTMAP);
    n->tree_ctor = (void (*)(void *, int32_t, int32_t, void *, void *, int32_t))(base + LCE_WG195_RVA_TREE_CTOR);
    n->lake_ctor = (void (*)(void *, void *))(base + LCE_WG195_RVA_LAKE_CTOR);
}

extern "C" int lce_wg195_init(uintptr_t base, const lce_wg195_natives *n, const lce_wg195_memory *m) {
    if (g.state != LCE_WG195_IDLE || g.base || !base || (base & 0x3fff) || !n || !m || !m->read || !m->cas8 ||
        !n->ore_place || !n->set_tile || !n->heightmap || !n->tree_ctor || !n->lake_ctor) return 0;
    g.base = base; g.n = *n; g.m = *m;
    return 1;
}

extern "C" int lce_wg195_add_feature(const lce_wg195_feature *f, char *err, uint32_t cap) {
    if (!g.base || g.state != LCE_WG195_IDLE) { set_err(err, cap, "worldgen195: not initialised or already prepared"); return 0; }
    if (!f || !valid_feature(*f, err, cap)) return 0;
    if (g.feature_count >= LCE_WG195_MAX_FEATURES) { set_err(err, cap, "worldgen195: more than 32 features"); return 0; }
    Feature &F = g.features[g.feature_count];
    memset(&F, 0, sizeof F);
    F.d = *f;
    ++g.feature_count;
    return 1;
}

extern "C" int lce_wg195_prepare(void) {
    if (!g.base || g.state != LCE_WG195_IDLE) return 0;
    // 1) pins that do not need a native call
    uintptr_t q = 0;
    if (!rd(g.base + LCE_WG195_RVA_ORE_VT + 0x10, &q, 8) || q != g.base + LCE_WG195_RVA_ORE_PLACE ||
        !rd(g.base + LCE_WG195_RVA_STONE_PREDICATE, &q, 8) || !q) { fail("ore vtable/predicate pin mismatch"); return 0; }
    for (uint32_t i = 0; i < g.feature_count; ++i) {
        const lce_wg195_feature &f = g.features[i].d;
        uintptr_t want = f.kind == LCE_FEATURE_TREE ? LCE_WG195_RVA_TREE_VT : f.kind == LCE_FEATURE_LAKE ? LCE_WG195_RVA_LAKE_VT : 0;
        uintptr_t place = f.kind == LCE_FEATURE_TREE ? LCE_WG195_RVA_TREE_PLACE : LCE_WG195_RVA_LAKE_PLACE;
        if (want && (!rd(g.base + want + 0x10, &q, 8) || q != g.base + place)) { fail("tree/lake vtable pin mismatch"); return 0; }
    }
    // 2) build native objects; a failed readback after a native ctor is uncertain
    for (uint32_t i = 0; i < g.feature_count; ++i) {
        Feature &F = g.features[i];
        const lce_wg195_feature &f = F.d;
        uintptr_t o = (uintptr_t)F.object;
        if (f.kind == LCE_FEATURE_ORE) {
            // Exactly the fields OreFeature::OreFeature 0x503e10 writes (Feature ctor
            // 0x271a20: vptr + doUpdate byte +8 = 0; then +0xc id, +0x10 meta,
            // +0x14 size, +0x18 predicate) - built without a BlockState.
            uintptr_t pred = g.base + LCE_WG195_RVA_STONE_PREDICATE;
            if (f.replace_id) {
                F.pred[0] = (uintptr_t)&kPredVt[2]; F.pred[1] = (uintptr_t)f.replace_id;
                F.pred[2] = f.replace_meta < 0 ? (uintptr_t)-1 : (uintptr_t)f.replace_meta;
                pred = (uintptr_t)F.pred;
            }
            uintptr_t vt = g.base + LCE_WG195_RVA_ORE_VT;
            uint32_t idmeta[3] = {f.block_id, f.block_meta, f.size};
            memcpy(F.object, &vt, 8);
            memcpy(F.object + 0xc, idmeta, 12);
            memcpy(F.object + 0x18, &pred, 8);
        } else if (f.kind == LCE_FEATURE_TREE) {
            g.n.tree_ctor(F.object, 0, f.tree_base_height, f.trunk_state, f.leaves_state, 0);
            uintptr_t vt = 0, trunk = 0, leaves = 0; uint32_t h = 0; uint8_t vines = 1, upd = 1;
            if (!rd(o, &vt, 8) || vt != g.base + LCE_WG195_RVA_TREE_VT || !rd(o + 8, &upd, 1) || upd || !rd(o + 0xc, &h, 4) ||
                h != f.tree_base_height || !rd(o + 0x10, &vines, 1) || vines || !rd(o + 0x18, &trunk, 8) || trunk != (uintptr_t)f.trunk_state ||
                !rd(o + 0x20, &leaves, 8) || leaves != (uintptr_t)f.leaves_state) { g.state = LCE_WG195_UNCERTAIN; fail("TreeFeature ctor readback"); return -1; }
        } else if (f.kind == LCE_FEATURE_LAKE) {
            g.n.lake_ctor(F.object, f.lake_block);
            uintptr_t vt = 0, blk = 0;
            if (!rd(o, &vt, 8) || vt != g.base + LCE_WG195_RVA_LAKE_VT || !rd(o + 0x10, &blk, 8) || blk != (uintptr_t)f.lake_block) {
                g.state = LCE_WG195_UNCERTAIN; fail("LakeFeature ctor readback"); return -1;
            }
        }
    }
    g.state = LCE_WG195_READY;
    return 1;
}

extern "C" int lce_wg195_publish(void) {
    if (g.state != LCE_WG195_READY) return 0;
    uintptr_t arr[LCE_WG195_MAX_BIOMES];
    if (!rd(g.base + LCE_WG195_RVA_BIOME_ARRAY, arr, sizeof arr)) { fail("biome array unreadable"); return 0; }
    // Pass 1: classify every live Biome, build one private vtable per class.
    memset(g.biomes, 0, sizeof g.biomes);
    g.biome_count = g.class_count = 0;
    for (uint32_t id = 0; id < LCE_WG195_MAX_BIOMES; ++id) {
        if (!arr[id]) continue;
        uintptr_t vptr = 0; uint32_t bid = 0xffffffffu, c = 0;
        if (!rd(arr[id], &vptr, 8) || !rd(arr[id] + 0x68, &bid, 4) || bid != id) { fail("biome object id mismatch"); return 0; }
        if (!find_class(vptr, &c)) { fail("biome with unknown vtable (already hooked or new class)"); return 0; }
        uint32_t k = 0;
        while (k < g.class_count && g.classes[k].cls != c) ++k;
        if (k == g.class_count) {
            if (k >= LCE_WG195_MAX_CLASSES) { fail("too many biome classes"); return 0; }
            ClassCopy &cc = g.classes[k];
            const lce_wg195_biome_class &bc = lce_wg195_biome_classes[c];
            uintptr_t next = 1;
            if (!rd(g.base + bc.vtable - 16, cc.vt, sizeof cc.vt) || cc.vt[0] != 0 || cc.vt[1] != g.base + bc.typeinfo ||
                cc.vt[2 + LCE_WG195_DECORATE_SLOT] != g.base + bc.decorate ||
                !rd(g.base + bc.vtable + 8 * LCE_WG195_BIOME_SLOTS, &next, 8) || next != 0) { fail("biome vtable pin mismatch"); return 0; }
            cc.vt[2 + LCE_WG195_DECORATE_SLOT] = (uintptr_t)&lce_wg195_decorate_dispatch;
            cc.cls = c;
            ++g.class_count;
        }
        BiomeRec &b = g.biomes[id];
        b.object = arr[id]; b.original_vptr = vptr; b.original_decorate = g.base + lce_wg195_biome_classes[c].decorate;
        b.private_vptr = (uintptr_t)&g.classes[k].vt[2]; b.dimension = lce_wg195_biome_classes[c].dimension;
        ++g.biome_count;
    }
    if (!g.biome_count) { fail("no biomes constructed yet (publish after 0x7f900)"); return 0; }
    // Pass 2: CAS each vptr. On a lost race roll back what we installed.
    __atomic_store_n(&g.state, (uint32_t)LCE_WG195_PUBLISHED, __ATOMIC_RELEASE);
    for (uint32_t id = 0; id < LCE_WG195_MAX_BIOMES; ++id) {
        BiomeRec &b = g.biomes[id];
        if (!b.object) continue;
        if (g.m.cas8(g.m.ctx, b.object, b.original_vptr, b.private_vptr) == 1) { b.installed = 1; continue; }
        bool ok = true;
        for (uint32_t j = 0; j < id; ++j) {
            BiomeRec &r = g.biomes[j];
            if (r.installed && g.m.cas8(g.m.ctx, r.object, r.private_vptr, r.original_vptr) == 1) r.installed = 0;
            else if (r.installed) ok = false;
        }
        if (!ok) { __atomic_store_n(&g.state, (uint32_t)LCE_WG195_UNCERTAIN, __ATOMIC_RELEASE); fail("publish rollback failed"); return -1; }
        __atomic_store_n(&g.state, (uint32_t)LCE_WG195_READY, __ATOMIC_RELEASE);
        fail("biome vptr changed during publish; rolled back");
        return 0;
    }
    return 1;
}

extern "C" int lce_wg195_restore(void) {
    if (g.state != LCE_WG195_PUBLISHED) return 0;
    __atomic_store_n(&g.disabled, 1u, __ATOMIC_RELEASE);
    bool ok = true;
    for (uint32_t id = 0; id < LCE_WG195_MAX_BIOMES; ++id) {
        BiomeRec &b = g.biomes[id];
        if (!b.installed) continue;
        if (g.m.cas8(g.m.ctx, b.object, b.private_vptr, b.original_vptr) == 1) b.installed = 0; else ok = false;
    }
    // Private vtables and records stay alive: a thread may still be inside dispatch.
    g.state = ok ? LCE_WG195_RESTORED : LCE_WG195_UNCERTAIN;
    if (!ok) { fail("restore: vptr changed by someone else"); return -1; }
    return 1;
}

extern "C" void lce_wg195_snapshot(lce_wg195_status *o) {
    if (!o) return;
    memset(o, 0, sizeof *o);
    o->state = __atomic_load_n(&g.state, __ATOMIC_ACQUIRE);
    o->feature_count = g.feature_count; o->biome_count = g.biome_count; o->class_count = g.class_count;
    o->calls = __atomic_load_n(&g.calls, __ATOMIC_RELAXED); o->decorated = __atomic_load_n(&g.decorated, __ATOMIC_RELAXED);
    o->skipped = __atomic_load_n(&g.skipped, __ATOMIC_RELAXED); o->placements = __atomic_load_n(&g.placements, __ATOMIC_RELAXED);
    o->writes_refused = __atomic_load_n(&g.writes_refused, __ATOMIC_RELAXED); o->errors = __atomic_load_n(&g.errors, __ATOMIC_RELAXED);
    memcpy(o->last_error, g.last_error, sizeof o->last_error);
}

extern "C" uintptr_t lce_wg195_decorate_dispatch(void *biome, void *level, void *random, const int32_t *pos) {
    add(g.calls);
    // Identify the record by object identity (records never change after publish).
    uint32_t id = biome ? *(const uint32_t *)((uintptr_t)biome + 0x68) : 0xffffffffu;
    if (id >= LCE_WG195_MAX_BIOMES || g.biomes[id].object != (uintptr_t)biome) {
        add(g.errors);
        return 0;   // unreachable unless another module installed our dispatch
    }
    BiomeRec &b = g.biomes[id];
    uint64_t before = random ? *(const uint64_t *)random : 0;
    uintptr_t result = ((decorate_fn)b.original_decorate)(biome, level, random, pos);
    if (__atomic_load_n(&g.state, __ATOMIC_ACQUIRE) != LCE_WG195_PUBLISHED || __atomic_load_n(&g.disabled, __ATOMIC_ACQUIRE)) return result;
    // Gate: genuine ServerLevel, chunk-origin pos (y=0, x,z multiples of 16), decorated dimension.
    if (!level || !random || !pos || *(const uintptr_t *)level != g.base + LCE_WG195_RVA_SERVER_LEVEL_VT ||
        pos[1] != 0 || (pos[0] & 15) || (pos[2] & 15) || pos[0] < -30000000 || pos[0] > 30000000 ||
        pos[2] < -30000000 || pos[2] > 30000000 || before >> 48 || b.dimension > 2 ||
        dimension_of_vptr(*(const uintptr_t *)biome) < 0) {
        add(g.skipped);
        return result;
    }
    uint64_t after = *(const uint64_t *)random;
    decorate_mods(b, level, pos, before);
    if (*(const uint64_t *)random != after) {   // we never pass it on; a change means misuse
        __atomic_store_n(&g.disabled, 1u, __ATOMIC_RELEASE);
        add(g.errors);
        fail("live chunk Random changed during mod placement; disabled");
        return result;
    }
    add(g.decorated);
    return result;
}

#ifdef LCE_WG195_TEST
extern "C" void lce_wg195_test_reset(void) { memset(&g, 0, sizeof g); }
#endif
