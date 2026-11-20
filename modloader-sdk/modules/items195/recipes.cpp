// Recipe publisher ABI 2 for 1.95: shaped + shapeless, output count/aux,
// aux-specific ingredients, opt-in shared output buckets. RVAs: items195.h,
// evidence: STATUS.md section "Recipes".
//
// Native model (all proven by disassembly of the stock adders):
//  - addShapedRecipy 0x5aa520 / addShapelessRecipy 0x5ab2c0 build the recipe,
//    write +0xc classification and +0x10 (-1 default), compute the output key
//    0x3b86f0 ((id<<16)|aux), call operator[] 0x5ab110 (find-or-insert) on the
//    owner's output-key map and APPEND to that bucket unconditionally
//    (0x5aaf13..0x5aaf6b, 0x5ab7c2..0x5ab80d), then append to the list vector
//    owner+0x50+list*0x20 (list 0 for the 0x5a9f20/0x5a9ff0 wrappers).
//  - Crafting (Recipes::getItemFor 0x5ab990) iterates the list vector
//    (0x5abe1a..0x5abe7a), not the map: a bucket with several recipes is the
//    native data model.
//  - Matching: aux -1 is a wildcard, otherwise exact (shaped 0x601bb4..0x601bd3,
//    shapeless 0x6032de..0x603304).
#include "items195.h"
#include <string.h>

namespace {

struct Pub { uintptr_t recipe; unsigned category; const char *key; };   // key: the provider's "modid:name" (process lifetime)
Pub g_published[512];
unsigned g_published_count;
uintptr_t g_published_owner;
lce195x_snapshot_fn g_r72_snapshot;

struct Provider { char id[64]; void *ctx; lce195x_recipe_provider_fn fn; unsigned status; };
Provider g_providers[16];
unsigned g_provider_count, g_phase, g_active;  // phase 0 open, 1 registering, 2 publishing, 3 done, 4 failed

bool held(lce195x_env *e) { return e->m.held(e->m.ctx) == 1; }
bool rd(lce195x_env *e, uintptr_t a, void *v, size_t n) { return a && a <= UINTPTR_MAX - n && held(e) && e->m.read(e->m.ctx, a, v, n) == 1; }
bool wr(lce195x_env *e, uintptr_t a, const void *v, size_t n) { return a && a <= UINTPTR_MAX - n && held(e) && e->m.write(e->m.ctx, a, v, n) == 1; }

// vector<T*> header as used by every recipe container: +8 begin, +0x10 end, +0x18 cap
bool header(lce195x_env *e, uintptr_t v, uintptr_t h[4]) {
    return rd(e, v, h, 32) && h[1] <= h[2] && h[2] <= h[3] && !((h[1] | h[2] | h[3]) & 7) && h[3] - h[1] <= 8192 * 8 && (h[1] || (!h[2] && !h[3]));
}
// exactly the stock append: grow(vec,1) when full, store, end += 8
bool append(lce195x_env *e, uintptr_t v, uintptr_t value, void (*grow)(uintptr_t, int)) {
    uintptr_t h[4];
    if (!header(e, v, h)) return false;
    if (h[2] == h[3]) { grow(v, 1); if (!header(e, v, h) || h[2] == h[3]) return false; }
    uintptr_t end = h[2] + 8;
    return wr(e, h[2], &value, 8) && wr(e, v + 16, &end, 8);
}
// Output-key map (owner+8 list head, +0x10 size; node: next +0, key +0x10, vector +0x18).
// found: 0 absent, 1 present (*size = bucket entries); false = map malformed.
bool bucket_size(lce195x_env *e, uintptr_t owner, unsigned key, int *found, uintptr_t *size) {
    uintptr_t head = 0, count = 0, node = 0;
    *found = 0; *size = 0;
    if (!rd(e, owner + 8, &head, 8) || !head || !rd(e, owner + 0x10, &count, 8) || count > 16384 || !rd(e, head, &node, 8)) return false;
    for (uintptr_t i = 0; i < count; i++) {
        uintptr_t next = 0; unsigned actual = 0;
        if (!node || node == head || !rd(e, node, &next, 8) || !rd(e, node + 0x10, &actual, 4)) return false;
        if (actual == key) {
            uintptr_t h[4];
            if (*found || !header(e, node + 0x18, h)) return false;
            *found = 1; *size = (h[2] - h[1]) / 8;
        }
        node = next;
    }
    return node == head;
}
bool stack_ok(lce195x_env *e, uintptr_t s, uintptr_t item, uint32_t count, uint32_t aux) {
    uintptr_t actual = 0; uint32_t c = 0, a = 0; uint8_t empty = 1;
    return rd(e, s + 0x28, &actual, 8) && rd(e, s + 0x10, &c, 4) && rd(e, s + 0x3c, &a, 4) && rd(e, s + 0x38, &empty, 1) &&
           actual == item && c == count && a == aux && !empty;
}
bool identifier(const char *s, size_t cap, bool key) {
    if (!s || !s[0]) return false;
    unsigned sep = 0;
    for (size_t i = 0; i < cap; i++) {
        unsigned char c = (unsigned char)s[i];
        if (!c) return !key || (sep == 1 && i > 2 && s[i - 1] != ':');
        if (key && c == ':') { if (!i || ++sep != 1) return false; continue; }
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' || (key && ((c >= 'A' && c <= 'Z') || c == '/')))) return false;
    }
    return false;
}
bool aux_ok(int32_t a) { return a == -1 || (a >= 0 && a <= 32767); }

bool validate(lce195x_recipe_publisher *p, const lce195x_recipe &d) {
    lce195x_env *e = p->env;
    uint32_t cls[2] = {0, 0};
    if (d.abi != LCE195X_RECIPE_ABI || d.size != sizeof d || !identifier(d.provider, 64, false) || !identifier(d.key, 96, true) ||
        (g_active && strcmp(d.provider, g_providers[g_active - 1].id)) || !d.output || !d.output_id || d.output_id > 65535 ||
        d.output_aux < 0 || d.output_aux > 32767 || !d.output_count || d.output_count > 64 || d.classification > 255 ||
        d.group > 7 || d.category >= 7 || d.shapeless > 1 || d.shared_output > 1 ||
        e->n.item_by_id(d.output_id) != d.output || !rd(e, d.output + 0x28, cls, 8) || cls[0] != d.classification)
        return false;
    unsigned used;
    if (d.shapeless) {
        if (!d.ingredient_count || d.ingredient_count > 9 || d.width || d.height) return false;
        used = d.ingredient_count;
    } else {
        if (!d.width || d.width > 3 || !d.height || d.height > 3 || d.ingredient_count) return false;
        used = d.width * d.height;
    }
    bool any = false;
    for (unsigned j = 0; j < 9; j++) {
        const lce195x_ingredient &c = d.cells[j];
        if (j >= used) { if (c.id || c.item || c.aux) return false; continue; }
        if (!!c.id != !!c.item) return false;
        if (!c.item) { if (d.shapeless || c.aux) return false; continue; }  // shapeless has no holes
        if (!aux_ok(c.aux) || c.id > 65535 || e->n.item_by_id(c.id) != c.item) return false;
        any = true;
    }
    return any;
}

}  // namespace

extern "C" int lce195x_recipes_publish(lce195x_recipe_publisher *p, const lce195x_recipe *defs, unsigned count) {
    if (!p || p->state || !p->env || !p->owner || !defs || !count || count > LCE195X_RECIPE_MAX) return 0;
    lce195x_env *e = p->env;
    const lce195x_natives &n = e->n;
    if (!e->m.held || !e->m.read || !e->m.write || !n.alloc || !n.alloc_array || !n.item_by_id || !n.stack_ctor || !n.shaped_ctor ||
        !n.shapeless_ctor || !n.output_key || !n.recipe_bucket || !n.recipe_vec_grow || !n.stack_vec_grow) return 0;
    p->state = 3;
    uintptr_t h[4];
    if (!held(e) || !header(e, p->owner + 0x50, h) || (g_published_owner && g_published_owner != p->owner) || g_published_count > 512 - count) return 0;
    size_t stock_count = (h[2] - h[1]) / 8;
    if (stock_count > 8192 - count || (stock_count && !rd(e, h[1], p->stock, stock_count * 8))) return 0;
    for (unsigned i = 0; i < count; i++) {
        const lce195x_recipe &d = defs[i];
        if (!validate(p, d)) return 0;
        unsigned key = (d.output_id << 16) | (uint32_t)d.output_aux;
        int found = 0; uintptr_t size = 0;
        if (!bucket_size(e, p->owner, key, &found, &size)) return 0;
        if (!d.shared_output && found) return 0;                      // r72 rule: unique output unless opted in
        for (unsigned j = 0; j < i; j++) {
            if (!strcmp(defs[j].key, d.key)) return 0;
            unsigned other = (defs[j].output_id << 16) | (uint32_t)defs[j].output_aux;
            if (other == key && !(d.shared_output && defs[j].shared_output)) return 0;
        }
    }
    // ---- first native allocation: from here every failure is uncertain ----
    p->count = count; p->state = 1;
    for (unsigned i = 0; i < count; i++) {
        const lce195x_recipe &d = defs[i];
        unsigned used = d.shapeless ? d.ingredient_count : d.width * d.height;
        uintptr_t values[9] = {};
        for (unsigned j = 0; j < used; j++) {
            if (!d.cells[j].item) continue;
            uintptr_t s = (uintptr_t)n.alloc(0xa8);
            p->inputs[i][j] = s;
            if (!s) goto uncertain;
            n.stack_ctor((void *)s, (void *)d.cells[j].item, 1, d.cells[j].aux);
            if (!stack_ok(e, s, d.cells[j].item, 1, (uint32_t)d.cells[j].aux)) goto uncertain;
            values[j] = s;
        }
        p->outputs[i] = (uintptr_t)n.alloc(0xa8);
        if (!p->outputs[i]) goto uncertain;
        n.stack_ctor((void *)p->outputs[i], (void *)d.output, (int)d.output_count, d.output_aux);
        if (!stack_ok(e, p->outputs[i], d.output, d.output_count, (uint32_t)d.output_aux)) goto uncertain;
        unsigned key = n.output_key((void *)p->outputs[i]);
        if (key != ((d.output_id << 16) | (uint32_t)d.output_aux)) goto uncertain;
        int found = 0; uintptr_t before = 0;
        if (!bucket_size(e, p->owner, key, &found, &before) || (found && !d.shared_output)) goto uncertain;
        if (d.shapeless) {
            // vector<ItemInstance*> object (0x20, zeroed like 0x5ab2f0..0x5ab305), appended as 0x5ab56a..0x5ab5a5
            p->cells[i] = (uintptr_t)n.alloc(0x20);
            uint8_t zero[0x20] = {};
            if (!p->cells[i] || !wr(e, p->cells[i], zero, sizeof zero)) goto uncertain;
            for (unsigned j = 0; j < used; j++) if (!append(e, p->cells[i], values[j], n.stack_vec_grow)) goto uncertain;
            if (!header(e, p->cells[i], h) || h[2] - h[1] != used * 8) goto uncertain;
            p->recipes[i] = (uintptr_t)n.alloc(0x30);
            if (!p->recipes[i]) goto uncertain;
            n.shapeless_ctor((void *)p->recipes[i], (void *)p->outputs[i], (void *)p->cells[i], (int)d.group);
            uintptr_t vt = 0, ptrs[2] = {}; unsigned grp = ~0u;
            if (!rd(e, p->recipes[i], &vt, 8) || vt != e->base + LCE195X_RVA_SHAPELESS_VT || !rd(e, p->recipes[i] + 0x1c, &grp, 4) || grp != d.group ||
                !rd(e, p->recipes[i] + 0x20, ptrs, 16) || ptrs[0] != p->outputs[i] || ptrs[1] != p->cells[i]) goto uncertain;
        } else {
            p->cells[i] = (uintptr_t)n.alloc_array(used * 8);
            if (!p->cells[i] || !wr(e, p->cells[i], values, used * 8)) goto uncertain;
            p->recipes[i] = (uintptr_t)n.alloc(0x40);
            if (!p->recipes[i]) goto uncertain;
            n.shaped_ctor((void *)p->recipes[i], (int)d.width, (int)d.height, (void **)p->cells[i], (void *)p->outputs[i], (int)d.group);
            uintptr_t vt = 0, ptrs[2] = {}; unsigned dims[3] = {};
            if (!rd(e, p->recipes[i], &vt, 8) || vt != e->base + LCE195X_RVA_SHAPED_VT || !rd(e, p->recipes[i] + 0x1c, dims, 12) ||
                dims[0] != d.width || dims[1] != d.height || dims[2] != d.group || !rd(e, p->recipes[i] + 0x28, ptrs, 16) ||
                ptrs[0] != p->cells[i] || ptrs[1] != p->outputs[i]) goto uncertain;
        }
        {   // what the stock adders write after the ctor (0x5aaee9/0x5aaef3, 0x5ab7a2/0x5ab7aa)
            unsigned minus = ~0u;
            if (!wr(e, p->recipes[i] + 0xc, &d.classification, 4) || !wr(e, p->recipes[i] + 0x10, &minus, 4)) goto uncertain;
        }
        p->buckets[i] = n.recipe_bucket(p->owner, &key);
        if (!p->buckets[i] || !header(e, p->buckets[i], h) || (h[2] - h[1]) / 8 != before) goto uncertain;
        if (!append(e, p->buckets[i], p->recipes[i], n.recipe_vec_grow) || !append(e, p->owner + 0x50, p->recipes[i], n.recipe_vec_grow)) goto uncertain;
        {
            uintptr_t last = 0;
            if (!header(e, p->buckets[i], h) || (h[2] - h[1]) / 8 != before + 1 || !rd(e, h[2] - 8, &last, 8) || last != p->recipes[i]) goto uncertain;
        }
        p->published = i + 1;
    }
    // ---- postcheck: stock list order intact, ours appended in order ----
    if (!header(e, p->owner + 0x50, h) || h[2] - h[1] != (stock_count + count) * 8) goto uncertain;
    for (size_t i = 0; i < stock_count + count; i++) {
        uintptr_t v = 0;
        if (!rd(e, h[1] + 8 * i, &v, 8) || v != (i < stock_count ? p->stock[i] : p->recipes[i - stock_count])) goto uncertain;
    }
    for (unsigned i = 0; i < count; i++) {
        g_published[g_published_count + i].recipe = p->recipes[i];
        g_published[g_published_count + i].category = defs[i].category;
        g_published[g_published_count + i].key = defs[i].key;
    }
    g_published_owner = p->owner;
    __atomic_store_n(&g_published_count, g_published_count + count, __ATOMIC_RELEASE);
    p->state = 2;
    return 1;
uncertain:
    p->state = 4;
    return -1;
}

extern "C" int lce195x_recipe_provider_register(const char *id, void *ctx, lce195x_recipe_provider_fn fn) {
    if (!identifier(id, 64, false) || !fn) return 0;
    unsigned expected = 0;
    if (!__atomic_compare_exchange_n(&g_phase, &expected, 1u, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return 0;
    bool ok = g_provider_count < 16;
    for (unsigned i = 0; ok && i < g_provider_count; i++) if (!strcmp(g_providers[i].id, id)) ok = false;
    if (ok) {
        Provider &p = g_providers[g_provider_count++];
        memcpy(p.id, id, strlen(id) + 1); p.ctx = ctx; p.fn = fn; p.status = 0;
    }
    __atomic_store_n(&g_phase, 0u, __ATOMIC_RELEASE);
    return ok ? 1 : 0;
}

// Runs every provider once. A provider that publishes nothing may return 1
// (unlike r72: an SDK plan can legitimately contain only items).
extern "C" int lce195x_recipe_providers_publish(uintptr_t owner) {
    if (!owner) return 0;
    unsigned expected = 0;
    if (!__atomic_compare_exchange_n(&g_phase, &expected, 2u, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return 0;
    unsigned completed = 0;
    for (unsigned i = 0; i < g_provider_count; i++) {
        g_active = i + 1;
        __atomic_store_n(&g_providers[i].status, 1u, __ATOMIC_RELEASE);
        int rc = g_providers[i].fn(g_providers[i].ctx, owner);
        g_active = 0;
        __atomic_store_n(&g_providers[i].status, rc == 1 ? 2u : rc < 0 ? 4u : 3u, __ATOMIC_RELEASE);
        if (rc != 1) { __atomic_store_n(&g_phase, 4u, __ATOMIC_RELEASE); return rc < 0 || completed ? -1 : 0; }
        completed++;
    }
    __atomic_store_n(&g_phase, 3u, __ATOMIC_RELEASE);
    return 1;
}

extern "C" unsigned lce195x_recipe_provider_status(const char *id) {
    if (!id) return 0;
    for (unsigned i = 0; i < g_provider_count; i++) if (!strcmp(g_providers[i].id, id)) return __atomic_load_n(&g_providers[i].status, __ATOMIC_ACQUIRE);
    return 0;
}

extern "C" unsigned lce195x_recipe_snapshot(uintptr_t owner, uintptr_t *out, unsigned *cats, unsigned cap) {
    unsigned count = __atomic_load_n(&g_published_count, __ATOMIC_ACQUIRE);
    if (!owner || !out || !cats || !count || owner != g_published_owner || count > 512 || cap < count || __atomic_load_n(&g_phase, __ATOMIC_ACQUIRE) == 2) return 0;
    for (unsigned i = 0; i < count; i++) {
        if (!g_published[i].recipe || g_published[i].category >= 7) return 0;
        out[i] = g_published[i].recipe; cats[i] = g_published[i].category;
    }
    return count;
}

/* Owner key ("modid:name") of a recipe this publisher added, nullptr for any
 * other recipe (stock, r72 Steel/Lucky). Read-only, any thread: entries are
 * written before g_published_count is released and never change afterwards.
 * Used by the plugin's server gate to keep undeclared mods' recipes hidden. */
extern "C" const char *lce195x_recipe_key_of(uintptr_t recipe) {
    if (!recipe) return nullptr;
    const unsigned count = __atomic_load_n(&g_published_count, __ATOMIC_ACQUIRE);
    for (unsigned i = 0; i < count && i < 512; i++)
        if (g_published[i].recipe == recipe) return g_published[i].key;
    return nullptr;
}

extern "C" void lce195x_bind_r72_snapshot(lce195x_snapshot_fn fn) { g_r72_snapshot = fn; }

// r72 returns 0 both for "none" and for refusal; either way its list is all or
// nothing, so a 0 just contributes no r72 entries. Ours are appended after.
extern "C" unsigned lce195x_recipe_snapshot_all(uintptr_t owner, uintptr_t *out, unsigned *cats, unsigned cap) {
    if (!owner || !out || !cats || !cap) return 0;
    unsigned a = g_r72_snapshot ? g_r72_snapshot(owner, out, cats, cap) : 0;
    if (a > cap) return 0;
    unsigned mine = __atomic_load_n(&g_published_count, __ATOMIC_ACQUIRE);
    if (!mine) return a;
    if (cap - a < mine) return 0;                                   // never a partial list
    unsigned b = lce195x_recipe_snapshot(owner, out + a, cats + a, cap - a);
    if (b != mine) return 0;
    for (unsigned i = 0; i < a + b; i++)
        for (unsigned j = i + 1; j < a + b; j++) if (out[i] == out[j]) return 0;  // registry rejects duplicates anyway
    return a + b;
}
