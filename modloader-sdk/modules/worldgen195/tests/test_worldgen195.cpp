// Host test for worldgen195 on a fake 1.95 image (RWX mmap at base): biome
// vtables/objects/array at their pinned RVAs, jump stubs at the stock decorate
// and feature place RVAs that land in host fakes. Natives are fakes.
#define LCE_WG195_TEST 1
#include "../worldgen195.h"
#include <initializer_list>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "../../../tests/check.h"

static uintptr_t B;  // fake image base
static void *g_map;
static const uint32_t IMAGE = 0x1500000;

// ---- fake game state -------------------------------------------------------
struct Write { int32_t x, y, z, id, meta, flags; };
static Write g_writes[1 << 16];
static unsigned g_nwrites;
static int32_t g_places[1 << 14][3];
static uintptr_t g_place_feature[1 << 14], g_place_random[1 << 14];
static unsigned g_nplaces;
static unsigned g_decorate_calls;
static void *g_dec_args[4];
static uint64_t *g_live_random;     // the stock chunk Random
static int g_evil;                  // ore_place corrupts the live random
static int g_bad_tree_ctor;
static int g_cas_fail_at = -1, g_cas_count;
static int32_t g_height = 70;

static uintptr_t fake_decorate(void *biome, void *level, void *random, const int32_t *pos) {
    ++g_decorate_calls;
    g_dec_args[0] = biome; g_dec_args[1] = level; g_dec_args[2] = random; g_dec_args[3] = (void *)pos;
    // stock decoration consumes the chunk Random
    *(uint64_t *)random = (*(uint64_t *)random * UINT64_C(0x5deece66d) + 0xb) & ((UINT64_C(1) << 48) - 1);
    return 0x5151;
}
static uint8_t fake_ore_place(void *f, void *, void *r, const int32_t *pos) {
    unsigned i = g_nplaces++ & ((1 << 14) - 1);
    memcpy(g_places[i], pos, 12); g_place_feature[i] = (uintptr_t)f; g_place_random[i] = (uintptr_t)r;
    if (g_evil && g_live_random) *g_live_random ^= 1;
    return 1;
}
static uint8_t fake_feature_place(void *f, void *l, void *r, const int32_t *pos) { return fake_ore_place(f, l, r, pos); }
static uint8_t fake_set_tile(void *, int32_t x, int32_t y, int32_t z, int32_t id, int32_t meta, int32_t flags) {
    if (g_nwrites < (1 << 16)) g_writes[g_nwrites++] = {x, y, z, id, meta, flags};
    return 1;
}
static int32_t fake_height(void *, int32_t, int32_t) { return g_height; }
static void fake_tree_ctor(void *s, int32_t upd, int32_t h, void *trunk, void *leaves, int32_t vines) {
    uintptr_t o = (uintptr_t)s;
    *(uintptr_t *)o = B + (g_bad_tree_ctor ? 0x10 : LCE_WG195_RVA_TREE_VT);
    *(uint8_t *)(o + 8) = (uint8_t)upd; *(uint32_t *)(o + 0xc) = (uint32_t)h; *(uint8_t *)(o + 0x10) = (uint8_t)vines;
    *(void **)(o + 0x18) = trunk; *(void **)(o + 0x20) = leaves;
}
static void fake_lake_ctor(void *s, void *blk) {
    *(uintptr_t *)s = B + LCE_WG195_RVA_LAKE_VT; *(void **)((uintptr_t)s + 0x10) = blk;
}
static int mem_read(void *, uintptr_t a, void *v, size_t n) { memcpy(v, (const void *)a, n); return 1; }
static int mem_cas(void *, uintptr_t a, uint64_t e, uint64_t d) {
    if (g_cas_count++ == g_cas_fail_at) return 0;
    return __atomic_compare_exchange_n((uint64_t *)a, &e, d, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE) ? 1 : 0;
}

// ---- fake image construction ----------------------------------------------
static void stub(uint32_t rva, void *target) {   // movabs rax, imm64 ; jmp rax
    uint8_t *p = (uint8_t *)(B + rva);
    p[0] = 0x48; p[1] = 0xb8; uintptr_t t = (uintptr_t)target; memcpy(p + 2, &t, 8); p[10] = 0xff; p[11] = 0xe0;
}
static const lce_wg195_biome_class *cls(const char *name) {
    for (uint32_t i = 0; i < lce_wg195_biome_class_count; ++i)
        if (!strcmp(lce_wg195_biome_classes[i].name, name)) return &lce_wg195_biome_classes[i];
    return nullptr;
}
static uintptr_t g_objects[256];
static uint8_t g_obj_store[8][0x200];
static unsigned g_nobj;
static void make_biome(uint32_t id, const char *klass) {
    const lce_wg195_biome_class *c = cls(klass);
    uintptr_t o = (uintptr_t)g_obj_store[g_nobj++];
    *(uintptr_t *)o = B + c->vtable; *(uint32_t *)(o + 0x68) = id;
    ((uintptr_t *)(B + LCE_WG195_RVA_BIOME_ARRAY))[id] = o;
    g_objects[id] = o;
}
static void build_image() {
    void *p = mmap(nullptr, IMAGE + 0x4000, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(p != MAP_FAILED);
    g_map = p;
    B = ((uintptr_t)p + 0x3fff) & ~(uintptr_t)0x3fff;   // console images are 16 KiB aligned
    for (uint32_t i = 0; i < lce_wg195_biome_class_count; ++i) {
        const lce_wg195_biome_class &c = lce_wg195_biome_classes[i];
        uintptr_t *vt = (uintptr_t *)(B + c.vtable);
        vt[-2] = 0; vt[-1] = B + c.typeinfo;
        for (unsigned s = 0; s < LCE_WG195_BIOME_SLOTS; ++s) vt[s] = B + 0x100000 + 0x40 * s;
        vt[LCE_WG195_DECORATE_SLOT] = B + c.decorate;
        vt[LCE_WG195_BIOME_SLOTS] = 0;
        stub(c.decorate, (void *)&fake_decorate);
    }
    *(uintptr_t *)(B + LCE_WG195_RVA_ORE_VT + 0x10) = B + LCE_WG195_RVA_ORE_PLACE;
    *(uintptr_t *)(B + LCE_WG195_RVA_TREE_VT + 0x10) = B + LCE_WG195_RVA_TREE_PLACE;
    *(uintptr_t *)(B + LCE_WG195_RVA_LAKE_VT + 0x10) = B + LCE_WG195_RVA_LAKE_PLACE;
    stub(LCE_WG195_RVA_TREE_PLACE, (void *)&fake_feature_place);
    stub(LCE_WG195_RVA_LAKE_PLACE, (void *)&fake_feature_place);
    *(uintptr_t *)(B + LCE_WG195_RVA_STONE_PREDICATE) = B + 0x1299210;
    make_biome(1, "PlainsBiome"); make_biome(129, "PlainsBiome"); make_biome(4, "ForestBiome");
    make_biome(9, "TheEndBiome"); make_biome(8, "HellBiome"); make_biome(127, "VoidBiome");
}
static lce_wg195_natives natives() {
    lce_wg195_natives n{fake_ore_place, fake_set_tile, fake_height, fake_tree_ctor, fake_lake_ctor};
    return n;
}
static lce_wg195_memory memory() { lce_wg195_memory m{nullptr, mem_read, mem_cas}; return m; }

static lce_wg195_feature ore(uint16_t id, uint16_t size, uint16_t veins) {
    lce_wg195_feature f;
    memset(&f, 0, sizeof f);
    f.struct_size = sizeof f; f.kind = LCE_FEATURE_ORE; f.salt = lce_wg195_hash("t:ore"); f.block_id = id;
    f.size = size; f.per_chunk = veins; f.min_y = 4; f.max_y = 64;
    return f;
}

static uint8_t g_level[0x400];
static uint64_t g_random[4];
static uintptr_t decorate(uint32_t biome_id, int32_t x, int32_t z, uint64_t seed) {
    uintptr_t o = g_objects[biome_id];
    uintptr_t vt = *(uintptr_t *)o;
    g_random[0] = seed; g_live_random = g_random;
    int32_t pos[3] = {x, 0, z};
    typedef uintptr_t (*fn)(void *, void *, void *, const int32_t *);
    return ((fn)(*(uintptr_t *)(vt + 0x58)))((void *)o, g_level, g_random, pos);
}

static void reset() {
    lce_wg195_test_reset();
    g_nwrites = g_nplaces = g_decorate_calls = 0; g_evil = g_bad_tree_ctor = 0; g_cas_fail_at = -1; g_cas_count = 0;
}
static void reset_vptrs() {
    const char *k[256] = {};
    k[1] = "PlainsBiome"; k[129] = "PlainsBiome"; k[4] = "ForestBiome"; k[9] = "TheEndBiome"; k[8] = "HellBiome"; k[127] = "VoidBiome";
    for (unsigned id = 0; id < 256; ++id) if (k[id]) *(uintptr_t *)g_objects[id] = B + cls(k[id])->vtable;
}

static void test_pure() {
    int32_t lo, hi;
    CHECK(lce_wg195_ore_centre_range(11, &lo, &hi)); CHECK_EQ(lo, 4); CHECK_EQ(hi, 12);
    CHECK(lce_wg195_ore_centre_range(24, &lo, &hi)); CHECK_EQ(lo, 7); CHECK_EQ(hi, 9);
    CHECK(!lce_wg195_ore_centre_range(1, &lo, &hi)); CHECK(!lce_wg195_ore_centre_range(25, &lo, &hi));
    for (uint32_t s = 2; s <= 24; ++s) {
        CHECK(lce_wg195_ore_centre_range(s, &lo, &hi));
        double k = 3.0 * s / 16.0 + 0.5 + 0.0625 + 1e-6;
        CHECK(floor(lo - k) >= 1); CHECK(floor(hi + k) <= 14);
    }
    CHECK_EQ(lce_wg195_biome_lookup("minecraft:plains"), 1);
    CHECK_EQ(lce_wg195_biome_lookup("swamp"), 6);
    CHECK_EQ(lce_wg195_biome_lookup("minecraft:bamboo_jungle"), 40);
    CHECK_EQ(lce_wg195_biome_lookup("mesa_bryce"), 165);
    CHECK_EQ(lce_wg195_biome_lookup("nope"), -1);
    CHECK_EQ(lce_wg195_biome_name_count, 72);
    CHECK_EQ(lce_wg195_biome_class_count, 25);
    for (uint32_t i = 0; i < lce_wg195_biome_name_count; ++i)
        for (uint32_t j = i + 1; j < lce_wg195_biome_name_count; ++j) CHECK(lce_wg195_biome_names[i].id != lce_wg195_biome_names[j].id);
    // Java Random reference: new Random(0).nextInt() powers of two and bounded draws are deterministic
    lce_wg195_random r, r2;
    lce_wg195_random_seed(&r, 42); lce_wg195_random_seed(&r2, 42);
    CHECK_EQ(r.seed, (42 ^ UINT64_C(0x5deece66d)));
    // Java: new Random(42).nextInt(10) -> 0, nextInt(10) -> 3 (well-known sequence 0,3,8,4,0)
    int seq[5]; for (int i = 0; i < 5; ++i) seq[i] = lce_wg195_random_int(&r, 10);
    CHECK_EQ(seq[0], 0); CHECK_EQ(seq[1], 3); CHECK_EQ(seq[2], 8); CHECK_EQ(seq[3], 4); CHECK_EQ(seq[4], 0);
    for (int i = 0; i < 1000; ++i) { int v = lce_wg195_random_int(&r2, 61); CHECK(v >= 0 && v < 61); if (v < 0 || v >= 61) break; }
    CHECK_EQ(lce_wg195_random_int(&r2, 0), 0);
}

static void test_validation() {
    reset();
    lce_wg195_natives n = natives(); lce_wg195_memory m = memory();
    lce_wg195_natives bad = n; bad.ore_place = nullptr;
    CHECK_EQ(lce_wg195_init(B, &bad, &m), 0);
    CHECK_EQ(lce_wg195_init(B + 8, &n, &m), 0);
    lce_wg195_feature f = ore(261, 11, 8);
    char err[128];
    CHECK_EQ(lce_wg195_add_feature(&f, err, sizeof err), 0);   // not initialised
    CHECK_EQ(lce_wg195_init(B, &n, &m), 1);
    CHECK_EQ(lce_wg195_init(B, &n, &m), 0);
    f.size = 25; CHECK_EQ(lce_wg195_add_feature(&f, err, sizeof err), 0);
    f.size = 1; CHECK_EQ(lce_wg195_add_feature(&f, err, sizeof err), 0);
    f = ore(261, 11, 8); f.dimension = 1; CHECK_EQ(lce_wg195_add_feature(&f, err, sizeof err), 0);
    CHECK(strstr(err, "0x4b8090") != nullptr);
    f = ore(0, 11, 8); CHECK_EQ(lce_wg195_add_feature(&f, err, sizeof err), 0);
    f = ore(366, 11, 8); CHECK_EQ(lce_wg195_add_feature(&f, err, sizeof err), 0);
    f = ore(261, 11, 0); CHECK_EQ(lce_wg195_add_feature(&f, err, sizeof err), 0);
    f = ore(261, 11, 8); f.min_y = 70; f.max_y = 60; CHECK_EQ(lce_wg195_add_feature(&f, err, sizeof err), 0);
    f = ore(261, 11, 8); f.struct_size = 8; CHECK_EQ(lce_wg195_add_feature(&f, err, sizeof err), 0);
    f = ore(261, 11, 8); f.kind = LCE_FEATURE_FLOWER; CHECK_EQ(lce_wg195_add_feature(&f, err, sizeof err), 0);
    static const uint16_t pal[2] = {0, (1 << 4)}; static uint16_t cells[17 * 2 * 17];
    lce_wg195_structure s{17, 2, 17, 2, pal, cells};
    f = ore(261, 11, 8); f.kind = LCE_FEATURE_STRUCTURE; f.per_chunk = 1; f.structure = &s;
    CHECK_EQ(lce_wg195_add_feature(&f, err, sizeof err), 0);
    s.size_x = 4; s.size_z = 4; cells[3] = 2; CHECK_EQ(lce_wg195_add_feature(&f, err, sizeof err), 0);  // cell outside palette
    cells[3] = 1; CHECK_EQ(lce_wg195_add_feature(&f, err, sizeof err), 1);
    f = ore(261, 11, 8); f.kind = LCE_FEATURE_LAKE; f.lake_block = (void *)0x1000; f.min_y = 2; f.max_y = 60;
    CHECK_EQ(lce_wg195_add_feature(&f, err, sizeof err), 0);
    f = ore(261, 11, 8); f.kind = LCE_FEATURE_TREE; CHECK_EQ(lce_wg195_add_feature(&f, err, sizeof err), 0);
    f = ore(261, 11, 8); CHECK_EQ(lce_wg195_add_feature(&f, err, sizeof err), 1);
    for (int i = 2; i < 40; ++i) lce_wg195_add_feature(&f, err, sizeof err);
    lce_wg195_status st; lce_wg195_snapshot(&st);
    CHECK_EQ(st.feature_count, 32);
    CHECK_EQ(lce_wg195_publish(), 0);   // not prepared
}

static void test_from_desc() {
    const char *biomes[] = {"minecraft:plains", "forest", "minecraft:the_end"};
    lce_feature_desc d;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d; d.key = "ruby:ruby_ore"; d.kind = LCE_FEATURE_ORE; d.size = 8; d.per_chunk = 4; d.min_y = 5;
    d.biomes = biomes; d.biome_count = 3;
    lce_wg195_feature f; char err[128];
    d.struct_size = offsetof(lce_feature_desc, biome_count);
    CHECK_EQ(lce_wg195_from_desc(&d, 261, 0, 0, -1, &f, err, sizeof err), LCE_E_INVALID);
    d.struct_size = sizeof d;
    CHECK_EQ(lce_wg195_from_desc(&d, 261, 0, 0, -1, &f, err, sizeof err), LCE_OK);
    CHECK_EQ(f.max_y, 64);
    CHECK_EQ(f.salt, lce_wg195_hash("ruby:ruby_ore"));
    CHECK_EQ(f.biome_mask[0], (UINT64_C(1) << 1) | (UINT64_C(1) << 4) | (UINT64_C(1) << 9));
    const char *badb[] = {"minecraft:nowhere"}; d.biomes = badb; d.biome_count = 1;
    CHECK_EQ(lce_wg195_from_desc(&d, 261, 0, 0, -1, &f, err, sizeof err), LCE_E_NOTFOUND);
    lce_wg195_set_mod_biome_lookup(+[](const char *) -> int32_t { return 256; });
    CHECK_EQ(lce_wg195_from_desc(&d, 261, 0, 0, -1, &f, err, sizeof err), LCE_E_NOTFOUND);
    lce_wg195_set_mod_biome_lookup(nullptr);
    d.biomes = nullptr; d.biome_count = 0; d.size = 40;
    CHECK_EQ(lce_wg195_from_desc(&d, 261, 0, 0, -1, &f, err, sizeof err), LCE_E_INVALID);
    d.kind = LCE_FEATURE_FLOWER;
    CHECK_EQ(lce_wg195_from_desc(&d, 261, 0, 0, -1, &f, err, sizeof err), LCE_E_UNSUPPORTED);
}

static bool in_chunk_ore(const int32_t *p, int32_t ox, int32_t oz, uint32_t size) {
    int32_t lo, hi; lce_wg195_ore_centre_range(size, &lo, &hi);
    int32_t cx = p[0] + 8 - ox, cz = p[2] + 8 - oz;
    return cx >= lo && cx <= hi && cz >= lo && cz <= hi && p[1] >= 4 && p[1] <= 64;
}

static void test_publish_and_dispatch() {
    reset(); reset_vptrs();
    lce_wg195_natives n = natives(); lce_wg195_memory m = memory();
    CHECK_EQ(lce_wg195_init(B, &n, &m), 1);
    char err[128];
    lce_wg195_feature a = ore(261, 11, 8);                         // all overworld biomes, stone predicate
    lce_wg195_feature b = ore(262, 6, 3); b.salt = 7; b.replace_id = 87; b.replace_meta = -1;   // custom predicate
    b.biome_mask[0] = UINT64_C(1) << 4;                            // forest only
    lce_wg195_feature e = ore(263, 4, 2); e.dimension = 2; e.salt = 9;   // end only
    lce_wg195_feature r = ore(264, 4, 1); r.rarity = 4; r.salt = 11;     // 1 in 4 chunks
    CHECK_EQ(lce_wg195_add_feature(&a, err, sizeof err), 1);
    CHECK_EQ(lce_wg195_add_feature(&b, err, sizeof err), 1);
    CHECK_EQ(lce_wg195_add_feature(&e, err, sizeof err), 1);
    CHECK_EQ(lce_wg195_add_feature(&r, err, sizeof err), 1);
    CHECK_EQ(lce_wg195_prepare(), 1);
    CHECK_EQ(lce_wg195_prepare(), 0);
    *(uintptr_t *)g_level = B + LCE_WG195_RVA_SERVER_LEVEL_VT;

    // dispatch before publish: stock vtable, fake decorate only
    CHECK_EQ(decorate(1, 0, 0, 5), 0x5151);
    CHECK_EQ(g_nplaces, 0);

    // unknown vtable -> refused, nothing swapped
    uintptr_t saved = *(uintptr_t *)g_objects[4];
    *(uintptr_t *)g_objects[4] = B + 0x1216218;   // abstract base vtable is not admitted
    CHECK_EQ(lce_wg195_publish(), 0);
    CHECK_EQ(*(uintptr_t *)g_objects[1], B + cls("PlainsBiome")->vtable);
    *(uintptr_t *)g_objects[4] = saved;
    // id mismatch -> refused
    *(uint32_t *)(g_objects[9] + 0x68) = 10;
    CHECK_EQ(lce_wg195_publish(), 0);
    *(uint32_t *)(g_objects[9] + 0x68) = 9;
    // CAS race on the third object -> rolled back, clean refusal
    g_cas_fail_at = 2; g_cas_count = 0;
    CHECK_EQ(lce_wg195_publish(), 0);
    for (unsigned id : {1u, 4u, 8u, 9u, 127u, 129u}) {
        uintptr_t vt = *(uintptr_t *)g_objects[id];
        bool stock = false;
        for (uint32_t c = 0; c < lce_wg195_biome_class_count; ++c) stock |= vt == B + lce_wg195_biome_classes[c].vtable;
        CHECK(stock);
    }
    g_cas_fail_at = -1;
    CHECK_EQ(lce_wg195_publish(), 1);
    lce_wg195_status st; lce_wg195_snapshot(&st);
    CHECK_EQ(st.state, LCE_WG195_PUBLISHED); CHECK_EQ(st.biome_count, 6); CHECK_EQ(st.class_count, 5);
    // private vtable: RTTI kept, only +0x58 differs, same copy shared by both Plains objects
    uintptr_t pv = *(uintptr_t *)g_objects[1];
    CHECK(pv != B + cls("PlainsBiome")->vtable);
    CHECK_EQ(pv, *(uintptr_t *)g_objects[129]);
    const uintptr_t *orig = (const uintptr_t *)(B + cls("PlainsBiome")->vtable), *cp = (const uintptr_t *)pv;
    CHECK_EQ(cp[-1], orig[-1]); CHECK_EQ(cp[-2], 0);
    unsigned diff = 0;
    for (unsigned s = 0; s < LCE_WG195_BIOME_SLOTS; ++s) diff += cp[s] != orig[s];
    CHECK_EQ(diff, 1);
    CHECK_EQ(cp[LCE_WG195_DECORATE_SLOT], (uintptr_t)&lce_wg195_decorate_dispatch);
    CHECK_EQ(lce_wg195_publish(), 0);

    // overworld plains chunk: stock decorate once with the same args, then 8 veins of 261 (+1 in 1/4 chunks)
    g_nplaces = g_decorate_calls = 0;
    CHECK_EQ(decorate(1, 160, -320, 0x123456789aULL), 0x5151);
    CHECK_EQ(g_decorate_calls, 1);
    CHECK_EQ((uintptr_t)g_dec_args[0], g_objects[1]); CHECK_EQ((uintptr_t)g_dec_args[2], (uintptr_t)g_random);
    unsigned n261 = 0, n262 = 0, n263 = 0;
    for (unsigned i = 0; i < g_nplaces; ++i) {
        uint32_t id = *(uint32_t *)(g_place_feature[i] + 0xc);
        n261 += id == 261; n262 += id == 262; n263 += id == 263;
        CHECK(g_place_random[i] != (uintptr_t)g_random);
        CHECK(in_chunk_ore(g_places[i], 160, -320, *(uint32_t *)(g_place_feature[i] + 0x14)));
    }
    CHECK_EQ(n261, 8); CHECK_EQ(n262, 0); CHECK_EQ(n263, 0);
    // feature objects carry exactly the OreFeature ctor fields
    uintptr_t fa = g_place_feature[0];
    CHECK_EQ(*(uintptr_t *)fa, B + LCE_WG195_RVA_ORE_VT);
    CHECK_EQ(*(uint8_t *)(fa + 8), 0); CHECK_EQ(*(uint32_t *)(fa + 0x10), 0); CHECK_EQ(*(uint32_t *)(fa + 0x14), 11);
    CHECK_EQ(*(uintptr_t *)(fa + 0x18), B + LCE_WG195_RVA_STONE_PREDICATE);

    // forest chunk gets the forest-only ore with a private predicate
    g_nplaces = 0;
    decorate(4, 0, 0, 77);
    uintptr_t fb = 0;
    for (unsigned i = 0; i < g_nplaces; ++i) if (*(uint32_t *)(g_place_feature[i] + 0xc) == 262) fb = g_place_feature[i];
    CHECK(fb != 0);
    if (fb) {
        uintptr_t pred = *(uintptr_t *)(fb + 0x18);
        CHECK(pred != B + LCE_WG195_RVA_STONE_PREDICATE);
        typedef uint8_t (*test_fn)(void *, const int32_t *);
        test_fn t = *(test_fn *)(*(uintptr_t *)pred);
        int32_t nether[2] = {87, 0}, stone[2] = {1, 0}, nether3[2] = {87, 3};
        CHECK_EQ(t((void *)pred, nether), 1); CHECK_EQ(t((void *)pred, stone), 0); CHECK_EQ(t((void *)pred, nether3), 1);
    }
    // end biome: only the end ore; hell and void: nothing (but stock still runs)
    g_nplaces = 0; decorate(9, 16, 16, 3);
    for (unsigned i = 0; i < g_nplaces; ++i) CHECK_EQ(*(uint32_t *)(g_place_feature[i] + 0xc), 263);
    CHECK_EQ(g_nplaces, 2);
    g_nplaces = g_decorate_calls = 0; decorate(8, 0, 0, 3); decorate(127, 0, 0, 3);
    CHECK_EQ(g_nplaces, 0); CHECK_EQ(g_decorate_calls, 2);

    // determinism + rarity + boundary sweep
    unsigned rare = 0, chunks = 0, bad = 0;
    for (int32_t cx = -20; cx < 20; ++cx)
        for (int32_t cz = -5; cz < 5; ++cz) {
            g_nplaces = 0;
            uint64_t seed = (uint64_t)(cx * 341873128712LL + cz * 132897987541LL) & 0xffffffffffffULL;
            decorate(129, cx * 16, cz * 16, seed);
            ++chunks;
            for (unsigned i = 0; i < g_nplaces; ++i) {
                rare += *(uint32_t *)(g_place_feature[i] + 0xc) == 264;
                bad += !in_chunk_ore(g_places[i], cx * 16, cz * 16, *(uint32_t *)(g_place_feature[i] + 0x14));
            }
        }
    CHECK_EQ(bad, 0);
    CHECK(rare > chunks / 8 && rare < chunks / 2);
    g_nplaces = 0; decorate(1, 32, 48, 999); unsigned k1 = g_nplaces; int32_t p1[3]; memcpy(p1, g_places[0], 12);
    g_nplaces = 0; decorate(1, 32, 48, 999); CHECK_EQ(g_nplaces, k1); CHECK(!memcmp(p1, g_places[0], 12));
    g_nplaces = 0; decorate(1, 32, 48, 1000); CHECK(memcmp(p1, g_places[0], 12) != 0);

    // gates: client/unknown level, misaligned pos -> stock only
    g_nplaces = g_decorate_calls = 0;
    *(uintptr_t *)g_level = B + 0x1340d58;
    decorate(1, 0, 0, 1);
    *(uintptr_t *)g_level = B + LCE_WG195_RVA_SERVER_LEVEL_VT;
    decorate(1, 8, 0, 1);
    CHECK_EQ(g_nplaces, 0); CHECK_EQ(g_decorate_calls, 2);
    lce_wg195_snapshot(&st); CHECK(st.skipped >= 2);

    // a native that touches the live Random disables the module (stock keeps running)
    g_evil = 1; decorate(1, 0, 0, 1); g_evil = 0;
    lce_wg195_snapshot(&st); CHECK_EQ(st.errors, 1);
    g_nplaces = g_decorate_calls = 0; decorate(1, 0, 0, 1);
    CHECK_EQ(g_nplaces, 0); CHECK_EQ(g_decorate_calls, 1);

    // restore: stock vptrs back; a stale private vptr only forwards
    CHECK_EQ(lce_wg195_restore(), 1);
    CHECK_EQ(*(uintptr_t *)g_objects[1], B + cls("PlainsBiome")->vtable);
    CHECK_EQ(*(uintptr_t *)g_objects[9], B + cls("TheEndBiome")->vtable);
    typedef uintptr_t (*fn)(void *, void *, void *, const int32_t *);
    int32_t pos[3] = {0, 0, 0}; g_nplaces = g_decorate_calls = 0;
    CHECK_EQ(((fn)cp[LCE_WG195_DECORATE_SLOT])((void *)g_objects[1], g_level, g_random, pos), 0x5151);
    CHECK_EQ(g_decorate_calls, 1); CHECK_EQ(g_nplaces, 0);
    CHECK_EQ(lce_wg195_restore(), 0);
}

static void test_structure_tree_lake() {
    reset(); reset_vptrs();
    lce_wg195_natives n = natives(); lce_wg195_memory m = memory();
    CHECK_EQ(lce_wg195_init(B, &n, &m), 1);
    char err[128];
    // 5x4x3 hut: palette 0 keep, 1 = planks(5:0), 2 = glass(20:0)
    static const uint16_t pal[3] = {0, 5 << 4, 20 << 4};
    static uint16_t cells[5 * 4 * 3];
    unsigned solid = 0;
    for (unsigned i = 0; i < 60; ++i) { cells[i] = (uint16_t)(i % 3); solid += cells[i] != 0; }
    static lce_wg195_structure s{5, 4, 3, 3, pal, cells};
    lce_wg195_feature f = ore(1, 11, 1); f.kind = LCE_FEATURE_STRUCTURE; f.structure = &s; f.min_y = f.max_y = 0; f.salt = 3;
    CHECK_EQ(lce_wg195_add_feature(&f, err, sizeof err), 1);
    static uint8_t trunk[8], leaves[8], water[8];
    lce_wg195_feature t = ore(1, 11, 2); t.kind = LCE_FEATURE_TREE; t.trunk_state = trunk; t.leaves_state = leaves; t.tree_base_height = 5;
    t.min_y = t.max_y = 0; t.salt = 4;
    CHECK_EQ(lce_wg195_add_feature(&t, err, sizeof err), 1);
    lce_wg195_feature l = ore(1, 11, 1); l.kind = LCE_FEATURE_LAKE; l.lake_block = water; l.min_y = 40; l.max_y = 60; l.salt = 5;
    CHECK_EQ(lce_wg195_add_feature(&l, err, sizeof err), 1);
    CHECK_EQ(lce_wg195_prepare(), 1);
    CHECK_EQ(lce_wg195_publish(), 1);
    *(uintptr_t *)g_level = B + LCE_WG195_RVA_SERVER_LEVEL_VT;
    for (int32_t cx = -3; cx < 3; ++cx) {
        g_nwrites = g_nplaces = 0;
        g_height = 60 + cx;
        decorate(1, cx * 16, 32, (uint64_t)(cx + 100));
        CHECK_EQ(g_nwrites, solid);
        for (unsigned i = 0; i < g_nwrites; ++i) {
            const Write &w = g_writes[i];
            CHECK(w.x >= cx * 16 && w.x < cx * 16 + 16 && w.z >= 32 && w.z < 48);
            CHECK(w.y >= g_height && w.y < g_height + 4);
            CHECK(w.flags == LCE_WG195_STRUCT_FLAGS && (w.id == 5 || w.id == 20) && w.meta == 0);
        }
        unsigned trees = 0, lakes = 0;
        for (unsigned i = 0; i < g_nplaces; ++i) {
            uintptr_t vt = *(uintptr_t *)g_place_feature[i];
            if (vt == B + LCE_WG195_RVA_TREE_VT) {
                ++trees;
                CHECK(g_places[i][0] >= cx * 16 + 3 && g_places[i][0] <= cx * 16 + 12 && g_places[i][2] >= 35 && g_places[i][2] <= 44);
                CHECK_EQ(g_places[i][1], g_height);
                CHECK_EQ(*(uint32_t *)(g_place_feature[i] + 0xc), 5);
            } else if (vt == B + LCE_WG195_RVA_LAKE_VT) {
                ++lakes;
                CHECK_EQ(g_places[i][0], cx * 16 + 8); CHECK_EQ(g_places[i][2], 40);
                CHECK(g_places[i][1] >= 40 && g_places[i][1] <= 60);
            }
            CHECK(g_place_random[i] != (uintptr_t)g_random);
        }
        CHECK_EQ(trees, 2); CHECK_EQ(lakes, 1);
    }
    // surface too high: structure and trees refused, nothing written
    g_nwrites = g_nplaces = 0; g_height = 253;
    decorate(1, 0, 0, 1);
    CHECK_EQ(g_nwrites, 0);
    lce_wg195_status st; lce_wg195_snapshot(&st); CHECK(st.writes_refused >= 3);
    CHECK_EQ(lce_wg195_restore(), 1);

    // a tree ctor that does not produce the pinned TreeFeature -> uncertain, no publish
    reset(); reset_vptrs();
    CHECK_EQ(lce_wg195_init(B, &n, &m), 1);
    CHECK_EQ(lce_wg195_add_feature(&t, err, sizeof err), 1);
    g_bad_tree_ctor = 1;
    CHECK_EQ(lce_wg195_prepare(), -1);
    CHECK_EQ(lce_wg195_publish(), 0);
    g_bad_tree_ctor = 0;
    // a mismatched ore vtable pin refuses prepare cleanly
    reset();
    CHECK_EQ(lce_wg195_init(B, &n, &m), 1);
    *(uintptr_t *)(B + LCE_WG195_RVA_ORE_VT + 0x10) = 0;
    CHECK_EQ(lce_wg195_prepare(), 0);
    *(uintptr_t *)(B + LCE_WG195_RVA_ORE_VT + 0x10) = B + LCE_WG195_RVA_ORE_PLACE;
    // empty biome array -> refuse
    reset();
    CHECK_EQ(lce_wg195_init(B, &n, &m), 1);
    CHECK_EQ(lce_wg195_prepare(), 1);
    uintptr_t arr[256]; memcpy(arr, (void *)(B + LCE_WG195_RVA_BIOME_ARRAY), sizeof arr);
    memset((void *)(B + LCE_WG195_RVA_BIOME_ARRAY), 0, sizeof arr);
    CHECK_EQ(lce_wg195_publish(), 0);
    memcpy((void *)(B + LCE_WG195_RVA_BIOME_ARRAY), arr, sizeof arr);
}

int main() {
    build_image();
    test_pure();
    test_validation();
    test_from_desc();
    test_publish_and_dispatch();
    test_structure_tree_lake();
    munmap(g_map, IMAGE + 0x4000);
    TEST_MAIN_END("test_worldgen195");
}
