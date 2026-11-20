// Host tests for biomes195 (fake 1.95 image, ASan/UBSan). The retail behaviour of the
// natives faked here is covered by tests/test_unicorn_retail.py.
#include "../biomes195.h"
#include <atomic>
#include <mutex>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <thread>
#include <vector>

static int checks, fails;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static uintptr_t base;
static std::vector<void *> allocations;
static int ctor_calls, new_calls, ro_patches, ro_result = 1;

static uintptr_t &Q(uintptr_t a) { return *(uintptr_t *)a; }
static uint32_t &D(uintptr_t a) { return *(uint32_t *)a; }

static std::mutex alloc_mu;
static void *f_new(size_t n) { std::lock_guard<std::mutex> l(alloc_mu); ++new_calls; void *p = calloc(1, n + 16); allocations.push_back(p); return p; }
static void f_wstr(void *ws, const uint16_t *s, size_t n) {      // heap form like the retail assign for n >= 8
    uint16_t *buf = (uint16_t *)f_new(2 * (n + 1));
    memcpy(buf, s, 2 * n);
    uintptr_t w = (uintptr_t)ws;
    Q(w + 8) = (uintptr_t)buf; Q(w + 0x18) = n; Q(w + 0x20) = n < 8 ? 7 : n | 7;
}
static void f_plains(void *self, int32_t id, int32_t sunflower, void *props) {  // models 0x53d140 -> 0x83e60
    ++ctor_calls;
    uintptr_t o = (uintptr_t)self, p = (uintptr_t)props;
    Q(o) = base + LCE_B195_RVA_PLAINS_VT;
    D(o + 0x68) = (uint32_t)id;
    memcpy((void *)(o + 0x10), (void *)(p + 0x28), 16);
    *(uint8_t *)(o + 0x20) = *(uint8_t *)(p + 0x3c); *(uint8_t *)(o + 0x21) = *(uint8_t *)(p + 0x3d);
    D(o + 0x24) = D(p + 0x40);
    memcpy((void *)(o + 0x28), props, 0x28);              // name wstring
    D(o + 0x50) = 2; D(o + 0x58) = 3;                      // grass / dirt
    *(uint8_t *)(o + 0x1d0) = (uint8_t)sunflower;
    Q(base + LCE_B195_RVA_BIOME_ARRAY + 8u * (uint32_t)id) = o;
}
struct FakeArea { void *self; int32_t x, z, w, h; } last_area;
static lce_b195_intarr f_get_area(void *self, int32_t x, int32_t z, int32_t w, int32_t h) {
    last_area = {self, x, z, w, h};
    static int32_t out[4] = {1, 2, 3, 4};
    return {out, 4};
}
static int m_read(void *, uintptr_t at, void *out, size_t n) { if (!at) return 0; memcpy(out, (void *)at, n); return 1; }
static int m_write(void *, uintptr_t at, const void *in, size_t n) { if (!at) return 0; memcpy((void *)at, in, n); return 1; }
static int m_cas8(void *, uintptr_t at, uint64_t e, uint64_t d) {
    return __atomic_compare_exchange_n((uint64_t *)at, &e, d, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST) ? 1 : 0;
}
static int m_ro8(void *c, uintptr_t at, uint64_t e, uint64_t d) {
    ++ro_patches;
    if (ro_result != 1) return ro_result;
    return m_cas8(c, at, e, d);
}

static lce_b195_natives N = {f_new, f_new, f_wstr, f_plains, f_get_area};
static lce_b195_memory M = {nullptr, m_read, m_write, m_cas8, m_ro8};
static uintptr_t stock_plains;

static void image_reset() {
    memset((void *)(base + 0x1400000), 0, 0x100000);
    // stock plains (id 1) with its colour-enum fields
    stock_plains = (uintptr_t)f_new(LCE_B195_BIOME_SIZE);
    Q(stock_plains) = base + LCE_B195_RVA_PLAINS_VT;
    D(stock_plains + 0x68) = 1;
    Q(stock_plains + 0x8) = 0xe18c04c867f3d31eull;
    D(stock_plains + 0x190) = 0xc; D(stock_plains + 0x194) = 0x34; D(stock_plains + 0x198) = 0x5c;
    Q(base + LCE_B195_RVA_PLAINS_GLOBAL) = stock_plains;
    Q(base + LCE_B195_RVA_BIOME_ARRAY + 8) = stock_plains;
    Q(base + LCE_B195_RVA_BIOME_ARRAY + 8 * 2) = (uintptr_t)f_new(LCE_B195_BIOME_SIZE);   // desert etc.
    // PlainsBiome vtable header + slots, BiomeInitLayer vtable
    Q(base + LCE_B195_RVA_PLAINS_VT - 16) = 0; Q(base + LCE_B195_RVA_PLAINS_VT - 8) = base + 0x12a7610;
    for (uint32_t i = 0; i < LCE_B195_BIOME_SLOTS; ++i) Q(base + LCE_B195_RVA_PLAINS_VT + 8 * i) = base + 0x85000 + 0x10 * i;
    Q(base + LCE_B195_RVA_BIL_VT + 8 * LCE_B195_BIL_GETAREA_SLOT) = base + LCE_B195_RVA_BIL_GETAREA;
    ctor_calls = new_calls = ro_patches = 0; ro_result = 1;
}

static lce_biome_desc desc(const char *key, uint32_t t, uint16_t weight) {
    lce_biome_desc d; memset(&d, 0, sizeof d);
    d.struct_size = sizeof d; d.key = key; d.name = "Crystal Fields";
    d.temperature_milli = t; d.rainfall_milli = 400; d.weight = weight;
    return d;
}

static uintptr_t fake_layer(std::vector<uintptr_t *> &keep) {
    uintptr_t l = (uintptr_t)f_new(0x80);
    static const uint32_t off[5] = {0, 0x38, 0x48, 0x58, 0x68}, len[5] = {0, 6, 6, 4, 4};
    for (int c = 1; c <= 4; ++c) {
        uintptr_t *a = (uintptr_t *)f_new(len[c] * 8);
        for (uint32_t i = 0; i < len[c]; ++i) a[i] = 0x1000 * c + i;
        keep.push_back(a);
        Q(l + off[c]) = (uintptr_t)a; D(l + off[c] + 8) = len[c];
    }
    return l;
}

int main() {
    void *img = mmap(nullptr, 0x1600000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    CHECK(img != MAP_FAILED);
    base = ((uintptr_t)img + 0x3fff) & ~(uintptr_t)0x3fff;
    char err[160];

    // ---- descriptor conversion --------------------------------------------------------------
    lce_b195_biome b;
    lce_biome_desc d = desc("gem:crystal_fields", 800, 20);
    d.grass_rgb = 0x40ffd0; d.sky_rgb = 0x8080ff; d.height_base_milli = 200; d.height_variation_milli = 300;
    CHECK(lce_b195_from_desc(&d, 170, 12, 0, 24, 1, &b, err, sizeof err) == LCE_OK);
    CHECK(!strcmp(b.key, "gem:crystal_fields") && b.id == 170 && b.copies == 2 && b.climate == LCE_B195_MEDIUM);
    CHECK(b.temperature == 0.8f && b.rainfall == 0.4f && b.depth == 0.2f && b.scale == 0.3f && b.rain == 1 && b.snow == 0);
    CHECK(b.top_id == 12 && b.filler_id == 24 && b.filler_meta == 1 && b.grass_rgb == 0x40ffd0 && b.water_rgb == 0);
    lce_biome_desc d0 = desc("gem:plain", 800, 0);
    CHECK(lce_b195_from_desc(&d0, 171, 0, 0, 0, 0, &b, err, sizeof err) == LCE_OK && b.copies == 0 && b.depth == 0.125f && b.scale == 0.05f);
    d0.weight = 1; CHECK(lce_b195_from_desc(&d0, 171, 0, 0, 0, 0, &b, err, sizeof err) == LCE_OK && b.copies == 1);
    d0.weight = 500; CHECK(lce_b195_from_desc(&d0, 171, 0, 0, 0, 0, &b, err, sizeof err) == LCE_OK && b.copies == 16);
    d0.temperature_milli = 2000; CHECK(lce_b195_from_desc(&d0, 171, 0, 0, 0, 0, &b, err, sizeof err) == LCE_OK && b.climate == LCE_B195_WARM);
    d0.temperature_milli = 300; CHECK(lce_b195_from_desc(&d0, 171, 0, 0, 0, 0, &b, err, sizeof err) == LCE_OK && b.climate == LCE_B195_COLD);
    d0.temperature_milli = 0; CHECK(lce_b195_from_desc(&d0, 171, 0, 0, 0, 0, &b, err, sizeof err) == LCE_OK && b.climate == LCE_B195_ICY && b.snow);
    d0.temperature_milli = 2001; CHECK(lce_b195_from_desc(&d0, 171, 0, 0, 0, 0, &b, err, sizeof err) == LCE_E_INVALID);
    d0.temperature_milli = 800; d0.water_rgb = 0x1000000; CHECK(lce_b195_from_desc(&d0, 171, 0, 0, 0, 0, &b, err, sizeof err) == LCE_E_INVALID);
    d0.water_rgb = 0; CHECK(lce_b195_from_desc(&d0, 1, 0, 0, 0, 0, &b, err, sizeof err) == LCE_E_INVALID);    // stock id
    CHECK(lce_b195_from_desc(&d0, 129, 0, 0, 0, 0, &b, err, sizeof err) == LCE_E_INVALID);                    // mutation range
    CHECK(lce_b195_from_desc(&d0, 255, 0, 0, 0, 0, &b, err, sizeof err) == LCE_E_INVALID);
    CHECK(lce_b195_from_desc(&d0, 171, 400, 0, 0, 0, &b, err, sizeof err) == LCE_E_INVALID);                  // block id
    lce_biome_desc dk = desc("NoColon", 800, 10);
    CHECK(lce_b195_from_desc(&dk, 171, 0, 0, 0, 0, &b, err, sizeof err) == LCE_E_INVALID);
    CHECK(lce_b195_id_allowed(50) && lce_b195_id_allowed(126) && !lce_b195_id_allowed(127) && !lce_b195_id_allowed(128) &&
          !lce_b195_id_allowed(167) && lce_b195_id_allowed(168) && lce_b195_id_allowed(254) && !lce_b195_id_allowed(255));

    // ---- registration + construct ---------------------------------------------------------------
    image_reset();
    lce_b195_test_reset();
    CHECK(lce_b195_add(&b, err, sizeof err) == 0);                          // before init
    CHECK(lce_b195_init(base, &N, &M) == 1);
    CHECK(lce_b195_init(base, &N, &M) == 0);
    lce_b195_biome crystal, ash, ice, holder;
    CHECK(lce_b195_from_desc(&d, 170, 12, 0, 24, 1, &crystal, err, sizeof err) == LCE_OK);
    lce_biome_desc da = desc("gem:ash_wastes", 1800, 10); da.water_rgb = 0x303030;
    CHECK(lce_b195_from_desc(&da, 171, 0, 0, 0, 0, &ash, err, sizeof err) == LCE_OK && ash.climate == LCE_B195_WARM);
    lce_biome_desc di = desc("gem:glacier", 0, 30);
    CHECK(lce_b195_from_desc(&di, 60, 0, 0, 0, 0, &ice, err, sizeof err) == LCE_OK && ice.copies == 3);
    holder = ash; snprintf(holder.key, sizeof holder.key, "lce:missing_200"); holder.id = 200; holder.placeholder = 1; holder.copies = 5;
    CHECK(lce_b195_add(&crystal, err, sizeof err) == 1);
    CHECK(lce_b195_add(&crystal, err, sizeof err) == 0);                    // same id
    lce_b195_biome dup = ash; snprintf(dup.key, sizeof dup.key, "gem:crystal_fields");
    CHECK(lce_b195_add(&dup, err, sizeof err) == 0 && strstr(err, "duplicate"));
    CHECK(lce_b195_add(&ash, err, sizeof err) == 1 && lce_b195_add(&ice, err, sizeof err) == 1 && lce_b195_add(&holder, err, sizeof err) == 1);
    lce_b195_biome big = ice; big.id = 61; snprintf(big.key, sizeof big.key, "gem:big"); big.copies = 16;
    for (int i = 0; i < 3; ++i) { big.id = (uint8_t)(61 + i); big.key[7] = (char)('a' + i); CHECK(lce_b195_add(&big, err, sizeof err) == 1); }
    big.id = 64; big.key[7] = 'z';
    CHECK(lce_b195_add(&big, err, sizeof err) == 0 && strstr(err, "pool full"));   // icy: 4 + 3 + 48 + 16 > 64

    // precondition refusals: nothing native happens
    Q(base + LCE_B195_RVA_PLAINS_GLOBAL) = 0;
    CHECK(lce_b195_construct() == 0 && ctor_calls == 0);
    Q(base + LCE_B195_RVA_PLAINS_GLOBAL) = stock_plains;
    Q(stock_plains) = 0x1234;                                                  // worldgen already published
    CHECK(lce_b195_construct() == 0 && ctor_calls == 0);
    Q(stock_plains) = base + LCE_B195_RVA_PLAINS_VT;
    Q(base + LCE_B195_RVA_BIOME_ARRAY + 8 * 171) = 0x5555;                     // id taken by a stock biome
    int nc = new_calls;
    CHECK(lce_b195_construct() == 0 && ctor_calls == 0 && new_calls == nc);
    Q(base + LCE_B195_RVA_BIOME_ARRAY + 8 * 171) = 0;
    CHECK(lce_b195_select() == 0);                                             // select before construct
    CHECK(lce_b195_construct() == 1 && ctor_calls == 7);
    CHECK(lce_b195_construct() == 0);
    lce_b195_biome late = ash; late.id = 180; snprintf(late.key, sizeof late.key, "gem:late");
    CHECK(lce_b195_add(&late, err, sizeof err) == 0);                         // after construct
    uintptr_t co = lce_b195_object(170), ao = lce_b195_object(171), io = lce_b195_object(60), ho = lce_b195_object(200);
    CHECK(co && ao && io && ho && Q(base + LCE_B195_RVA_BIOME_ARRAY + 8 * 170) == co);
    CHECK(Q(co) == base + LCE_B195_RVA_PLAINS_VT && D(co + 0x68) == 170);
    CHECK(*(float *)(co + 0x10) == 0.2f && *(float *)(co + 0x14) == 0.3f && *(float *)(co + 0x18) == 0.8f && *(float *)(co + 0x1c) == 0.4f);
    CHECK(*(uint8_t *)(co + 0x21) == 1 && D(co + 0x24) == 0xffffffffu && *(uint8_t *)(co + 0x1d0) == 0);
    CHECK(D(co + 0x50) == 12 && D(co + 0x54) == 0 && D(co + 0x58) == 24 && D(co + 0x5c) == 1);   // descriptor blocks
    CHECK(D(ao + 0x50) == 2 && D(ao + 0x58) == 3);                                                // kept grass/dirt
    CHECK(Q(co + 8) == 0xe18c04c867f3d31eull && D(co + 0x190) == 0xc && D(co + 0x194) == 0x34 && D(co + 0x198) == 0x5c);
    uintptr_t wbuf = Q(co + 0x28 + 8);
    CHECK(Q(co + 0x28 + 0x18) == 18 && !memcmp((void *)wbuf, u"gem:crystal_fields", 36));
    CHECK(*(uint8_t *)(io + 0x20) == 1);                                       // snow on the icy biome
    CHECK(lce_b195_lookup("gem:glacier") == 60 && lce_b195_lookup("gem:nope") == -1 && lce_b195_lookup(nullptr) == -1);

    // ---- selection: vtable slot ------------------------------------------------------------------
    uintptr_t slot = base + LCE_B195_RVA_BIL_VT + 8 * LCE_B195_BIL_GETAREA_SLOT;
    Q(slot) = 0x777;
    CHECK(lce_b195_select() == 0 && Q(slot) == 0x777 && ro_patches == 0);    // foreign hook: refused
    Q(slot) = base + LCE_B195_RVA_BIL_GETAREA;
    CHECK(lce_b195_patch_layer(0) == 0);
    CHECK(lce_b195_select() == 1 && Q(slot) == (uintptr_t)&lce_b195_get_area && ro_patches == 1);
    CHECK(lce_b195_select() == 0);

    // ---- selection: pools ------------------------------------------------------------------------
    std::vector<uintptr_t *> keep;
    uintptr_t layer = fake_layer(keep);
    uintptr_t warm0 = Q(layer + 0x38), med0 = Q(layer + 0x48), cold0 = Q(layer + 0x58), icy0 = Q(layer + 0x68);
    lce_b195_intarr r = lce_b195_get_area((void *)layer, -16, 32, 8, 4);
    CHECK(last_area.self == (void *)layer && last_area.x == -16 && last_area.z == 32 && last_area.w == 8 && last_area.h == 4 && r.length == 4);
    CHECK(Q(layer + 0x58) == cold0 && D(layer + 0x60) == 4);                  // cold: no mod biome, untouched
    CHECK(D(layer + 0x40) == 7 && D(layer + 0x50) == 8 && D(layer + 0x70) == 4 + 3 + 48);
    uintptr_t *wp = (uintptr_t *)Q(layer + 0x38), *mp = (uintptr_t *)Q(layer + 0x48), *ip = (uintptr_t *)Q(layer + 0x68);
    CHECK(wp != (uintptr_t *)warm0 && mp != (uintptr_t *)med0 && ip != (uintptr_t *)icy0);
    bool order = true;
    for (int i = 0; i < 6; ++i) order &= wp[i] == 0x1000u + i && mp[i] == 0x2000u + i;
    for (int i = 0; i < 4; ++i) order &= ip[i] == 0x4000u + i;
    CHECK(order);                                                             // stock entries first, same order
    CHECK(wp[6] == ao && mp[6] == co && mp[7] == co && ip[4] == io && ip[5] == io && ip[6] == io);
    bool no_holder = true;
    for (uint32_t i = 0; i < D(layer + 0x70); ++i) no_holder &= ip[i] != ho;
    for (uint32_t i = 0; i < D(layer + 0x40); ++i) no_holder &= wp[i] != ho;
    CHECK(no_holder);                                                          // placeholders never generate
    CHECK(((uintptr_t *)warm0)[0] == 0x1000);                                  // stock array left intact (never freed)
    CHECK(lce_b195_patch_layer(layer) == 2 && Q(layer + 0x48) == (uintptr_t)mp && D(layer + 0x50) == 8);   // idempotent
    lce_b195_status st; lce_b195_snapshot(&st);
    CHECK(st.state == LCE_B195_SELECTING && st.biome_count == 7 && st.built == 7 && st.layers_patched == 1 && st.get_area_calls == 1);
    // corrupt pool -> error, untouched
    uintptr_t bad = fake_layer(keep); D(bad + 0x50) = 0;
    CHECK(lce_b195_patch_layer(bad) == -1);

    // concurrent first use of one layer from 8 threads
    for (int round = 0; round < 50; ++round) {
        uintptr_t l = fake_layer(keep);
        std::atomic<int> go{0};
        std::vector<std::thread> th;
        for (int t = 0; t < 8; ++t) th.emplace_back([&] { while (!go.load()) {} lce_b195_get_area((void *)l, 0, 0, 4, 4); });
        go = 1;
        for (auto &t : th) t.join();
        uintptr_t *m2 = (uintptr_t *)Q(l + 0x48);
        bool good = D(l + 0x50) == 8 && m2[6] == co && m2[7] == co && D(l + 0x40) == 7 && D(l + 0x70) == 55;
        for (int i = 0; i < 6; ++i) good &= m2[i] == 0x2000u + i;
        CHECK(good);
    }

    // ---- colours --------------------------------------------------------------------------------
    // worldgen195 published in between: the mod biome now has worldgen's private PlainsBiome copy
    static uintptr_t wg_vt[2 + LCE_B195_BIOME_SLOTS + 1];
    memcpy(wg_vt, (void *)(base + LCE_B195_RVA_PLAINS_VT - 16), sizeof wg_vt - 8);
    wg_vt[2 + 11] = 0xdec0;                                                    // decorate dispatch
    Q(co) = (uintptr_t)&wg_vt[2];
    static std::vector<uintptr_t> adopted;
    auto adopt = [](uintptr_t v, uint8_t dim) -> int { adopted.push_back(v); return dim == 0 ? 1 : 0; };
    uintptr_t ao_vt = Q(ao);
    CHECK(lce_b195_colours(adopt) == 1);
    uintptr_t *cv = (uintptr_t *)Q(co);
    CHECK(cv != &wg_vt[2] && cv[-1] == base + 0x12a7610 && cv[-2] == 0);
    CHECK(cv[11] == 0xdec0 && cv[12] == (uintptr_t)&lce_b195_grass && cv[5] == (uintptr_t)&lce_b195_sky);
    CHECK(cv[13] == wg_vt[2 + 13] && cv[21] == wg_vt[2 + 21] && cv[LCE_B195_BIOME_SLOTS] == 0);   // foliage/water stock
    uintptr_t *av = (uintptr_t *)Q(ao);
    CHECK(av != (uintptr_t *)ao_vt && av[21] == (uintptr_t)&lce_b195_water);
    CHECK(av[12] == ((uintptr_t *)ao_vt)[12]);
    CHECK(Q(io) == base + LCE_B195_RVA_PLAINS_VT);                              // no colours -> untouched
    CHECK(adopted.size() == 2 && adopted[0] == (uintptr_t)cv && adopted[1] == (uintptr_t)av);
    CHECK(lce_b195_grass((void *)co, nullptr) == 0xff40ffd0u && lce_b195_sky((void *)co, 0.8f) == 0xff8080ffu);
    CHECK(lce_b195_water((void *)ao) == 0xff303030u);
    CHECK(lce_b195_grass((void *)stock_plains, nullptr) == 0xff79c05au);        // not a mod biome -> neutral default
    lce_b195_snapshot(&st); CHECK(st.coloured == 2);
    CHECK(lce_b195_colours(adopt) == 1 && adopted.size() == 2);                 // idempotent

    // ---- restore ---------------------------------------------------------------------------------
    ro_result = -1;
    CHECK(lce_b195_restore() == -1);
    ro_result = 1;
    CHECK(lce_b195_restore() == 1 && Q(slot) == base + LCE_B195_RVA_BIL_GETAREA);
    CHECK(lce_b195_restore() == 0);

    // select: uncertain RELRO restore, and nothing to generate
    lce_b195_test_reset(); image_reset();
    CHECK(lce_b195_init(base, &N, &M) == 1 && lce_b195_add(&crystal, err, sizeof err) == 1 && lce_b195_construct() == 1);
    ro_result = -1;
    CHECK(lce_b195_select() == -1);
    lce_b195_snapshot(&st); CHECK(st.state == LCE_B195_UNCERTAIN);
    lce_b195_test_reset(); image_reset();
    CHECK(lce_b195_init(base, &N, &M) == 1 && lce_b195_add(&holder, err, sizeof err) == 1 && lce_b195_construct() == 1);
    CHECK(lce_b195_select() == 1 && ro_patches == 0);                          // placeholder only: vtable untouched
    lce_b195_test_reset(); image_reset();
    CHECK(lce_b195_init(base, &N, &M) == 1 && lce_b195_construct() == 1);       // no biomes at all

    for (void *p : allocations) free(p);
    printf("test_biomes195: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
