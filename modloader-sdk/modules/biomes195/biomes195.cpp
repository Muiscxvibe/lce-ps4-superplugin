// biomes195: mod biomes (real PlainsBiome objects at free ids) + generation through the
// BiomeInitLayer climate pools + optional colour slots. See biomes195.h / STATUS.md.
#include "biomes195.h"
#include <stdio.h>
#include <string.h>

namespace {

struct Rec {
    lce_b195_biome spec;
    uintptr_t object;           // constructed Biome
    uintptr_t vt[2 + LCE_B195_BIOME_SLOTS + 1];   // private colour vtable: {offset-to-top, typeinfo, 27 slots, 0}
    uint8_t coloured;
};

struct State {
    uint32_t state;
    uintptr_t base;
    lce_b195_natives n;
    lce_b195_memory m;
    Rec recs[LCE_B195_MAX_BIOMES];
    uint32_t count;
    int16_t by_id[256];           // id -> rec index, -1
    // selection: per climate, the mod biome objects in order (copies expanded)
    uintptr_t pool_add[5][LCE_B195_POOL_MAX];
    uint32_t pool_add_n[5];
    uint32_t pool_planned[5];      // entries reserved by lce_b195_add
    uint32_t selecting;            // 1 after the getArea slot is installed
    uint64_t get_area_calls, layers_patched, patch_races, errors;
    char last_error[128];
} g;

void fail(const char *why) {
    snprintf(g.last_error, sizeof g.last_error, "%s", why);
}
void add(uint64_t &c) { __atomic_add_fetch(&c, 1, __ATOMIC_RELAXED); }

bool rd(uintptr_t at, void *out, size_t n) { return g.m.read(g.m.ctx, at, out, n) == 1; }
bool wr(uintptr_t at, const void *in, size_t n) { return g.m.write(g.m.ctx, at, in, n) == 1; }
template <class T> bool rdv(uintptr_t at, T *v) { return rd(at, v, sizeof *v); }
template <class T> bool wrv(uintptr_t at, T v) { return wr(at, &v, sizeof v); }

void set_err(char *err, uint32_t cap, const char *msg) {
    if (err && cap) snprintf(err, cap, "%s", msg);
}

bool key_ok(const char *k) {
    if (!k) return false;
    size_t n = strlen(k);
    if (n < 3 || n >= 64 || !strchr(k, ':')) return false;
    for (size_t i = 0; i < n; ++i) {
        char c = k[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == ':' || c == '/' || c == '.' || c == '-'))
            return false;
    }
    return true;
}

const uint32_t kPoolOff[5] = {0, 0x38, 0x48, 0x58, 0x68};

}  // namespace

extern "C" uint8_t lce_b195_climate_of(float t) {
    if (t >= 1.0f) return LCE_B195_WARM;
    if (t >= 0.5f) return LCE_B195_MEDIUM;
    if (t >= 0.15f) return LCE_B195_COLD;
    return LCE_B195_ICY;
}

extern "C" int lce_b195_id_allowed(uint32_t id) {
    return (id >= 168 && id <= 254) || (id >= 50 && id <= 126);
}

extern "C" lce_status lce_b195_from_desc(const lce_biome_desc *d, uint8_t id, uint16_t top_id, uint16_t top_meta,
                                         uint16_t filler_id, uint16_t filler_meta, lce_b195_biome *out,
                                         char *err, uint32_t cap) {
    if (!d || !out) { set_err(err, cap, "biomes195: null descriptor"); return LCE_E_INVALID; }
    memset(out, 0, sizeof *out);
    if (!key_ok(d->key)) { set_err(err, cap, "biomes195: biome key must be 'modid:name' (lowercase, < 64 chars)"); return LCE_E_INVALID; }
    if (!lce_b195_id_allowed(id)) { set_err(err, cap, "biomes195: biome id outside the free ranges 50..126 / 168..254"); return LCE_E_INVALID; }
    if (d->temperature_milli > 2000 || d->rainfall_milli > 1000) { set_err(err, cap, "biomes195: temperature 0..2000 milli, rainfall 0..1000 milli"); return LCE_E_INVALID; }
    if (d->height_base_milli > 2000 || d->height_variation_milli > 2000) { set_err(err, cap, "biomes195: height base/variation 0..2000 milli (land biomes only)"); return LCE_E_INVALID; }
    if (top_meta > 15 || filler_meta > 15 || top_id > 365 || filler_id > 365) { set_err(err, cap, "biomes195: top/filler block must be a block id 1..365 with data 0..15"); return LCE_E_INVALID; }
    uint32_t rgb_all = d->grass_rgb | d->foliage_rgb | d->water_rgb | d->sky_rgb;
    if (rgb_all > 0xffffffu) { set_err(err, cap, "biomes195: colours are 0xRRGGBB (0 = stock)"); return LCE_E_INVALID; }
    snprintf(out->key, sizeof out->key, "%s", d->key);
    out->id = id;
    out->temperature = (float)d->temperature_milli / 1000.0f;
    out->rainfall = (float)d->rainfall_milli / 1000.0f;
    out->depth = d->height_base_milli ? (float)d->height_base_milli / 1000.0f : 0.125f;
    out->scale = d->height_variation_milli ? (float)d->height_variation_milli / 1000.0f : 0.05f;
    out->climate = lce_b195_climate_of(out->temperature);
    out->snow = out->temperature < 0.15f;
    out->rain = d->rainfall_milli > 0;
    out->top_id = top_id; out->top_meta = top_meta;
    out->filler_id = filler_id; out->filler_meta = filler_meta;
    out->grass_rgb = d->grass_rgb; out->foliage_rgb = d->foliage_rgb;
    out->water_rgb = d->water_rgb; out->sky_rgb = d->sky_rgb;
    uint32_t c = d->weight ? (d->weight + 5u) / 10u : 0u;
    if (d->weight && c < 1) c = 1;
    if (c > 16) c = 16;
    out->copies = (uint16_t)c;
    return LCE_OK;
}

extern "C" void lce_b195_natives_bind(lce_b195_natives *n, uintptr_t base) {
    n->op_new = (void *(*)(size_t))(base + LCE_B195_RVA_OP_NEW);
    n->op_new_arr = (void *(*)(size_t))(base + LCE_B195_RVA_OP_NEW_ARR);
    n->wstr_assign = (void (*)(void *, const uint16_t *, size_t))(base + LCE_B195_RVA_WSTR_ASSIGN);
    n->plains_ctor = (void (*)(void *, int32_t, int32_t, void *))(base + LCE_B195_RVA_PLAINS_CTOR);
    n->bil_get_area = (lce_b195_intarr (*)(void *, int32_t, int32_t, int32_t, int32_t))(base + LCE_B195_RVA_BIL_GETAREA);
}

extern "C" int lce_b195_init(uintptr_t base, const lce_b195_natives *n, const lce_b195_memory *m) {
    if (g.state != LCE_B195_IDLE || g.base || !base || (base & 0x3fff) || !n || !m || !m->read || !m->write ||
        !m->cas8 || !m->patch_ro8 || !n->op_new || !n->op_new_arr || !n->wstr_assign || !n->plains_ctor || !n->bil_get_area)
        return 0;
    g.base = base; g.n = *n; g.m = *m;
    for (int i = 0; i < 256; ++i) g.by_id[i] = -1;
    g.state = LCE_B195_READY;
    return 1;
}

extern "C" int lce_b195_add(const lce_b195_biome *b, char *err, uint32_t cap) {
    if (g.state != LCE_B195_READY) { set_err(err, cap, "biomes195: not initialised or already constructed"); return 0; }
    if (!b || !key_ok(b->key) || !lce_b195_id_allowed(b->id)) { set_err(err, cap, "biomes195: bad key or id"); return 0; }
    if (g.count >= LCE_B195_MAX_BIOMES) { set_err(err, cap, "biomes195: more than 32 mod biomes"); return 0; }
    if (g.by_id[b->id] >= 0) { set_err(err, cap, "biomes195: biome id already taken by another mod biome"); return 0; }
    for (uint32_t i = 0; i < g.count; ++i)
        if (!strcmp(g.recs[i].spec.key, b->key)) { set_err(err, cap, "biomes195: duplicate biome key"); return 0; }
    lce_b195_biome s = *b;
    if (s.placeholder) { s.copies = 0; s.grass_rgb = s.foliage_rgb = s.water_rgb = s.sky_rgb = 0; }
    if (!s.climate) s.climate = lce_b195_climate_of(s.temperature);
    if (s.climate < LCE_B195_WARM || s.climate > LCE_B195_ICY || s.copies > 16 ||
        !(s.temperature >= 0.0f && s.temperature <= 2.0f) || !(s.rainfall >= 0.0f && s.rainfall <= 1.0f) ||
        !(s.depth >= 0.0f && s.depth <= 2.0f) || !(s.scale >= 0.0f && s.scale <= 2.0f)) {
        set_err(err, cap, "biomes195: climate/temperature/rainfall/height out of range"); return 0;
    }
    // Pool capacity: stock pools hold 6/6/4/4 entries (Unicorn-verified 0x89ec0).
    static const uint32_t stock_len[5] = {0, 6, 6, 4, 4};
    if (stock_len[s.climate] + g.pool_planned[s.climate] + s.copies > LCE_B195_POOL_MAX) {
        set_err(err, cap, "biomes195: climate pool full (64 entries)"); return 0;
    }
    g.pool_planned[s.climate] += s.copies;
    Rec &r = g.recs[g.count];
    memset(&r, 0, sizeof r);
    r.spec = s;
    g.by_id[s.id] = (int16_t)g.count;
    ++g.count;
    return 1;
}

extern "C" int lce_b195_construct(void) {
    if (g.state != LCE_B195_READY) return 0;
    if (!g.count) { g.state = LCE_B195_BUILT; return 1; }
    uintptr_t arr = g.base + LCE_B195_RVA_BIOME_ARRAY, plains = 0, plains_vt = 0;
    // Preconditions before the first native call: biome static init done, stock plains untouched, ids free.
    if (!rdv(g.base + LCE_B195_RVA_PLAINS_GLOBAL, &plains) || !plains || !rdv(plains, &plains_vt)) {
        fail("stock plains not constructed yet (run after the biome static init 0x7f900)"); return 0;
    }
    if (plains_vt != g.base + LCE_B195_RVA_PLAINS_VT) {
        fail("stock plains vptr is not the PlainsBiome vtable (run before lce_wg195_publish)"); return 0;
    }
    uintptr_t slot1 = 0;
    if (!rdv(arr + 8, &slot1) || slot1 != plains) { fail("biomes[1] is not Biome::plains"); return 0; }
    uint8_t colour_fields[0x10]; uint64_t plains_8 = 0;
    if (!rd(plains + 0x190, colour_fields, sizeof colour_fields) || !rdv(plains + 0x8, &plains_8)) { fail("plains fields unreadable"); return 0; }
    for (uint32_t i = 0; i < g.count; ++i) {
        uintptr_t cur = 1;
        if (!rdv(arr + 8u * g.recs[i].spec.id, &cur) || cur) { fail("a mod biome id is already used by a stock biome"); return 0; }
    }
    // Construct. From here on a failure is uncertain (native objects exist).
    for (uint32_t i = 0; i < g.count; ++i) {
        Rec &r = g.recs[i];
        const lce_b195_biome &s = r.spec;
        uint8_t *props = (uint8_t *)g.n.op_new(LCE_B195_PROPS_SIZE);
        uint8_t *obj = (uint8_t *)g.n.op_new(LCE_B195_BIOME_SIZE);
        if (!props || !obj) { fail("operator new failed"); g.state = LCE_B195_UNCERTAIN; return -1; }
        memset(props, 0, LCE_B195_PROPS_SIZE);
        memset(obj, 0, LCE_B195_BIOME_SIZE);
        uint64_t res = 7;                                   // empty SSO wstring: +8 buf, +0x18 size 0, +0x20 capacity 7
        memcpy(props + 0x20, &res, 8);
        uint16_t name[64]; size_t nl = strlen(s.key);
        for (size_t k = 0; k < nl; ++k) name[k] = (uint16_t)(uint8_t)s.key[k];
        name[nl] = 0;
        g.n.wstr_assign(props, name, nl);
        float hf[4] = {s.depth, s.scale, s.temperature, s.rainfall};
        memcpy(props + 0x28, hf, sizeof hf);
        uint32_t water = 0xffffff; memcpy(props + 0x38, &water, 4);
        props[0x3c] = s.snow ? 1 : 0;
        props[0x3d] = s.rain ? 1 : 0;
        int32_t parent = -1; memcpy(props + 0x40, &parent, 4);
        g.n.plains_ctor(obj, s.id, 0, props);               // props is consumed (freed) by the base ctor
        uintptr_t o = (uintptr_t)obj, vptr = 0, slot = 0; uint32_t bid = 0xffffffffu;
        if (!rdv(o, &vptr) || vptr != g.base + LCE_B195_RVA_PLAINS_VT || !rdv(o + 0x68, &bid) || bid != s.id ||
            !rdv(arr + 8u * s.id, &slot) || slot != o) {
            fail("PlainsBiome ctor postcheck failed (vptr/id/biomes[id])"); g.state = LCE_B195_UNCERTAIN; return -1;
        }
        bool ok = wr(o + 0x190, colour_fields, sizeof colour_fields) && wrv(o + 0x8, plains_8);
        if (s.top_id) ok = ok && wrv(o + 0x50, (uint32_t)s.top_id) && wrv(o + 0x54, (uint32_t)s.top_meta);
        if (s.filler_id) ok = ok && wrv(o + 0x58, (uint32_t)s.filler_id) && wrv(o + 0x5c, (uint32_t)s.filler_meta);
        if (!ok) { fail("mod biome field write failed"); g.state = LCE_B195_UNCERTAIN; return -1; }
        r.object = o;
        if (s.copies) {
            for (uint16_t c = 0; c < s.copies; ++c) g.pool_add[s.climate][g.pool_add_n[s.climate]++] = o;
        }
    }
    g.state = LCE_B195_BUILT;
    return 1;
}

extern "C" int lce_b195_colours(int (*adopt)(uintptr_t, uint8_t)) {
    if (g.state != LCE_B195_BUILT && g.state != LCE_B195_SELECTING) return 0;
    uint32_t done = 0;
    for (uint32_t i = 0; i < g.count; ++i) {
        Rec &r = g.recs[i];
        const lce_b195_biome &s = r.spec;
        if (r.coloured || !r.object || !(s.grass_rgb | s.foliage_rgb | s.water_rgb | s.sky_rgb)) continue;
        uintptr_t vptr = 0;
        if (!rdv(r.object, &vptr) || !rd(vptr - 16, r.vt, sizeof r.vt - 8)) { fail("mod biome vtable unreadable"); return 0; }
        // the current vtable must be PlainsBiome's or a faithful copy of it (worldgen195): same RTTI.
        uintptr_t stock[2] = {0, 0};
        if (!rd(g.base + LCE_B195_RVA_PLAINS_VT - 16, stock, sizeof stock) || r.vt[0] != stock[0] || r.vt[1] != stock[1]) {
            fail("mod biome vtable is not a PlainsBiome vtable"); return 0;
        }
        r.vt[2 + LCE_B195_BIOME_SLOTS] = 0;
        if (s.sky_rgb) r.vt[2 + 5] = (uintptr_t)&lce_b195_sky;          // +0x28
        if (s.grass_rgb) r.vt[2 + 12] = (uintptr_t)&lce_b195_grass;     // +0x60
        if (s.foliage_rgb) r.vt[2 + 13] = (uintptr_t)&lce_b195_foliage; // +0x68
        if (s.water_rgb) r.vt[2 + 21] = (uintptr_t)&lce_b195_water;     // +0xa8
        uintptr_t mine = (uintptr_t)&r.vt[2];
        if (g.m.cas8(g.m.ctx, r.object, vptr, mine) != 1) { fail("mod biome vptr changed concurrently"); return 0; }
        r.coloured = 1;
        ++done;
        if (adopt && adopt(mine, 0) != 1) { fail("worldgen195 refused the colour vtable (mod features skip this biome)"); add(g.errors); }
    }
    (void)done;
    return 1;
}

namespace {
const Rec *rec_of(const void *biome) {
    if (!biome) return nullptr;
    uint32_t id = *(const uint32_t *)((uintptr_t)biome + 0x68);
    if (id >= 256 || g.by_id[id] < 0) return nullptr;
    const Rec &r = g.recs[g.by_id[id]];
    return r.object == (uintptr_t)biome ? &r : nullptr;
}
uint32_t argb(uint32_t rgb) { return 0xff000000u | rgb; }
}  // namespace

extern "C" uint32_t lce_b195_grass(void *biome, const void *) { const Rec *r = rec_of(biome); return r ? argb(r->spec.grass_rgb) : 0xff79c05au; }
extern "C" uint32_t lce_b195_foliage(void *biome, const void *) { const Rec *r = rec_of(biome); return r ? argb(r->spec.foliage_rgb) : 0xff59ae30u; }
extern "C" uint32_t lce_b195_water(void *biome) { const Rec *r = rec_of(biome); return r ? argb(r->spec.water_rgb) : 0xff3f76e4u; }
extern "C" uint32_t lce_b195_sky(void *biome, float) { const Rec *r = rec_of(biome); return r ? argb(r->spec.sky_rgb) : 0xff78a7ffu; }

// ---- selection -----------------------------------------------------------------------------------
// Hot path (worldgen threads): direct memory access; `layer` is always a live BiomeInitLayer because
// the only way in is its own vtable slot (or a test).
extern "C" int lce_b195_patch_layer(uintptr_t layer) {
    if (__atomic_load_n(&g.state, __ATOMIC_ACQUIRE) < LCE_B195_BUILT || !layer) return 0;
    int result = 2;
    for (uint32_t c = LCE_B195_WARM; c <= LCE_B195_ICY; ++c) {
        uint32_t extra = g.pool_add_n[c];
        if (!extra) continue;
        uintptr_t *pptr = (uintptr_t *)(layer + kPoolOff[c]);
        uint32_t *plen = (uint32_t *)(layer + kPoolOff[c] + 8);
        uintptr_t old = __atomic_load_n(pptr, __ATOMIC_ACQUIRE);
        uint32_t old_n = __atomic_load_n(plen, __ATOMIC_ACQUIRE);
        if (!old || !old_n || old_n > LCE_B195_POOL_MAX) { add(g.errors); return -1; }
        const uintptr_t *entries = (const uintptr_t *)old;
        if (entries[old_n - 1] == g.pool_add[c][extra - 1]) continue;        // already ours
        uint32_t stock_n = old_n;
        if (stock_n + extra > LCE_B195_POOL_MAX) { add(g.errors); return -1; }
        uintptr_t *fresh = (uintptr_t *)g.n.op_new_arr((stock_n + extra) * sizeof(uintptr_t));
        if (!fresh) { add(g.errors); return -1; }
        for (uint32_t i = 0; i < stock_n; ++i) fresh[i] = entries[i];
        for (uint32_t i = 0; i < extra; ++i) fresh[stock_n + i] = g.pool_add[c][i];
        // Readers load len, then ptr (0x8a3c9.. : `mov 0x40(%r14),%esi` before `mov 0x38(%r14),%rcx`),
        // so publish ptr first, then len: a reader either sees old len with any ptr (index < old len,
        // valid in both arrays) or the new len with the new ptr. The stock array is never freed.
        uintptr_t expected = old;
        if (!__atomic_compare_exchange_n(pptr, &expected, (uintptr_t)fresh, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            add(g.patch_races);      // another thread patched this pool; our copy is leaked (never published)
            continue;
        }
        __atomic_store_n(plen, stock_n + extra, __ATOMIC_RELEASE);
        result = 1;
    }
    if (result == 1) add(g.layers_patched);
    return result;
}

extern "C" lce_b195_intarr lce_b195_get_area(void *self, int32_t x, int32_t z, int32_t w, int32_t h) {
    add(g.get_area_calls);
    (void)lce_b195_patch_layer((uintptr_t)self);
    return g.n.bil_get_area(self, x, z, w, h);
}

extern "C" int lce_b195_select(void) {
    if (g.state != LCE_B195_BUILT) return 0;
    uint32_t total = 0;
    for (uint32_t c = 1; c <= 4; ++c) total += g.pool_add_n[c];
    if (!total) return 1;                          // nothing generates: leave the vtable alone
    uintptr_t slot = g.base + LCE_B195_RVA_BIL_VT + 8u * LCE_B195_BIL_GETAREA_SLOT, cur = 0;
    if (!rdv(slot, &cur)) { fail("BiomeInitLayer vtable unreadable"); return 0; }
    if (cur != g.base + LCE_B195_RVA_BIL_GETAREA) { fail("BiomeInitLayer getArea slot is not stock (someone else hooked it)"); return 0; }
    __atomic_store_n(&g.state, (uint32_t)LCE_B195_SELECTING, __ATOMIC_RELEASE);
    int rc = g.m.patch_ro8(g.m.ctx, slot, g.base + LCE_B195_RVA_BIL_GETAREA, (uintptr_t)&lce_b195_get_area);
    if (rc == 1) { g.selecting = 1; return 1; }
    if (rc == 0) { __atomic_store_n(&g.state, (uint32_t)LCE_B195_BUILT, __ATOMIC_RELEASE); fail("getArea slot changed during install"); return 0; }
    __atomic_store_n(&g.state, (uint32_t)LCE_B195_UNCERTAIN, __ATOMIC_RELEASE);
    fail("RELRO protection not restored after the getArea slot install");
    return -1;
}

extern "C" int lce_b195_restore(void) {
    if (!g.selecting) return 0;
    uintptr_t slot = g.base + LCE_B195_RVA_BIL_VT + 8u * LCE_B195_BIL_GETAREA_SLOT;
    int rc = g.m.patch_ro8(g.m.ctx, slot, (uintptr_t)&lce_b195_get_area, g.base + LCE_B195_RVA_BIL_GETAREA);
    if (rc == 1) { g.selecting = 0; __atomic_store_n(&g.state, (uint32_t)LCE_B195_BUILT, __ATOMIC_RELEASE); return 1; }
    fail("restore: getArea slot not ours or protection not restored");
    return -1;
}

extern "C" void lce_b195_snapshot(lce_b195_status *out) {
    if (!out) return;
    memset(out, 0, sizeof *out);
    out->state = __atomic_load_n(&g.state, __ATOMIC_ACQUIRE);
    out->biome_count = g.count;
    for (uint32_t i = 0; i < g.count; ++i) { out->built += g.recs[i].object != 0; out->coloured += g.recs[i].coloured; }
    out->get_area_calls = __atomic_load_n(&g.get_area_calls, __ATOMIC_RELAXED);
    out->layers_patched = __atomic_load_n(&g.layers_patched, __ATOMIC_RELAXED);
    out->patch_races = __atomic_load_n(&g.patch_races, __ATOMIC_RELAXED);
    out->errors = __atomic_load_n(&g.errors, __ATOMIC_RELAXED);
    snprintf(out->last_error, sizeof out->last_error, "%s", g.last_error);
}

extern "C" int32_t lce_b195_lookup(const char *key) {
    if (!key) return -1;
    for (uint32_t i = 0; i < g.count; ++i)
        if (!strcmp(g.recs[i].spec.key, key)) return g.recs[i].spec.id;
    return -1;
}

extern "C" uintptr_t lce_b195_object(uint32_t id) {
    return id < 256 && g.by_id[id] >= 0 ? g.recs[g.by_id[id]].object : 0;
}

#ifdef LCE_B195_TEST
extern "C" void lce_b195_test_reset(void) { memset(&g, 0, sizeof g); }
#endif
