// enchants195: new enchantments on 1.95 through retail constructors, private
// vtables of retail functions and the registry's own register. See enchants195.h.
#include "enchants195.h"
#include <stdio.h>
#include <string.h>

namespace {

void set_err(char *err, uint32_t cap, const char *m) { if (err && cap) snprintf(err, cap, "%s", m); }
bool held(s195_env *e) { return e->held && e->held(e->ctx) == 1; }
bool rd(s195_env *e, uintptr_t a, void *v, size_t n) { return a && a <= UINTPTR_MAX - n && held(e) && e->read(e->ctx, a, v, n) == 1; }
bool wr(s195_env *e, uintptr_t a, const void *v, size_t n) { return a && a <= UINTPTR_MAX - n && held(e) && e->write(e->ctx, a, v, n) == 1; }
bool rq(s195_env *e, uintptr_t a, uintptr_t *v) { uint64_t x = 0; if (!rd(e, a, &x, 8)) return false; *v = (uintptr_t)x; return true; }
bool wq(s195_env *e, uintptr_t a, uint64_t v) { return wr(e, a, &v, 8); }
uintptr_t callf(s195_env *e, uintptr_t fn, unsigned n, uint64_t a, uint64_t b = 0, uint64_t c = 0, uint64_t d = 0, uint64_t f = 0) {
    uint64_t r[5] = {a, b, c, d, f};
    return e->call(e->ctx, fn, r, n, nullptr, 0);
}
uintptr_t callr(s195_env *e, uint32_t rva, unsigned n, uint64_t a, uint64_t b = 0, uint64_t c = 0, uint64_t d = 0, uint64_t f = 0) {
    return callf(e, e->base + rva, n, a, b, c, d, f);
}

// ---- static tables (STATUS.md cites each) ----
struct CloneInfo {
    const char *key;
    uint32_t ctor, vt, size;
    int32_t stock_id;        // stock object whose slots array the clone shares
    int32_t arg;             // damage type, or protection Type index
};
const CloneInfo kClone[E195_CLONE_COUNT] = {
    {"", E195_RVA_BASE_CTOR, E195_RVA_BASE_VT, 0x30, -1, 0},
    {"sharpness", E195_RVA_DAMAGE_CTOR, E195_RVA_DAMAGE_VT, 0x38, 16, 0},
    {"smite", E195_RVA_DAMAGE_CTOR, E195_RVA_DAMAGE_VT, 0x38, 17, 1},
    {"bane_of_arthropods", E195_RVA_DAMAGE_CTOR, E195_RVA_DAMAGE_VT, 0x38, 18, 2},
    {"impaling", E195_RVA_IMPALER_CTOR, E195_RVA_IMPALER_VT, 0x30, 80, 0},
    {"protection", E195_RVA_PROT_CTOR, E195_RVA_PROT_VT, 0x38, 0, 0},
    {"fire_protection", E195_RVA_PROT_CTOR, E195_RVA_PROT_VT, 0x38, 1, 1},
    {"feather_falling", E195_RVA_PROT_CTOR, E195_RVA_PROT_VT, 0x38, 2, 2},
    {"blast_protection", E195_RVA_PROT_CTOR, E195_RVA_PROT_VT, 0x38, 3, 3},
    {"projectile_protection", E195_RVA_PROT_CTOR, E195_RVA_PROT_VT, 0x38, 4, 4},
};
// stock enchantments whose effect is read through the static Enchantments::X
// pointer (globals 0x14155c0..0x14156d8 filled at 0x250cba..; every extra
// reference of the global is an identity read), so a clone has no effect.
const char *const kIdentity[] = {
    "thorns", "respiration", "aqua_affinity", "depth_strider", "frost_walker", "binding_curse", "knockback", "fire_aspect",
    "looting", "efficiency", "silk_touch", "unbreaking", "fortune", "power", "punch", "flame", "infinity",
    "luck_of_the_sea", "lure", "mending", "vanishing_curse", "riptide", "loyalty", "channeling", "multishot",
    "quick_charge", "piercing", "sweeping",
};
struct CatInfo { uint32_t global, vt; };
const CatInfo kCat[E195_CAT_COUNT] = {
    {0, 0},
    {0x14154b8u, 0x1254ee8u}, {0x14154c0u, 0x1254f18u}, {0x14154c8u, 0x1254f48u}, {0x14154d0u, 0x1254f78u},
    {0x14154d8u, 0x1254fa8u}, {0x14154e0u, 0x1254fd8u}, {0x1415480u, 0x1255008u}, {0x1415488u, 0x1255038u},
    {0x1415490u, 0x1255068u}, {0x14154e8u, 0x1255098u}, {0x14154f0u, 0x12550c8u}, {0x14154f8u, 0x12550f8u},
    {0x1415500u, 0x1255128u}, {0x1415508u, 0x1255158u},
};
// retail functions "mov $N,%eax; ret" (16-aligned), N = index; pinned by tests/test_image_pins.py
const uint32_t kConst[11] = {0, 0x24ae30u, 0x48c0u, 0x158c0u, 0x3ca20u, 0x1f1ad0u, 0x12180u, 0x5db00u, 0x24d10u, 0x58420u, 0x319f0u};
const uint32_t kWeight[4] = {10, 5, 2, 1};

const char *strip_ns(const char *k) { return !strncmp(k, "minecraft:", 10) ? k + 10 : k; }
bool armour_cat(uint32_t c) { return c >= E195_CAT_ARMOR && c <= E195_CAT_ARMOR_HEAD; }

bool key_ok(const char *k) {
    if (!k) return false;
    const char *c = strchr(k, ':');
    if (!c || c == k || !c[1] || strlen(k) > 96 || !strncmp(k, "minecraft:", 10)) return false;
    for (const char *p = k; *p; p++)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_' || *p == '.' || *p == '/' || (*p == ':' && p == c))) return false;
    return true;
}

struct Pre {
    uintptr_t reg, regvt, rarity, cat, arg_ptr, slots, rl;
    uint32_t nslots;
};

uintptr_t vfn(s195_env *e, uintptr_t obj, uint32_t slot) {
    uintptr_t vt = 0, f = 0;
    return rq(e, obj, &vt) && vt && rq(e, vt + slot, &f) ? f : 0;
}

// a game wstring + ResourceLocation for key; 0 on failure (temporaries freed)
uintptr_t make_rl(s195_env *e, const char *key) {
    size_t n = strlen(key);
    uint16_t w[100];
    for (size_t i = 0; i < n; i++) w[i] = (uint8_t)key[i];
    uintptr_t text = callr(e, E195_RVA_ALLOC, 1, (n + 1) * 2), ws = callr(e, E195_RVA_ALLOC, 1, 0x28), rl = callr(e, E195_RVA_ALLOC, 1, E195_RL_SIZE);
    bool ok = text && ws && rl;
    if (ok) {
        uint16_t z = 0;
        uint8_t init[0x28];
        memset(init, 0, sizeof init);
        uint64_t seven = 7;
        memcpy(init + 0x20, &seven, 8);
        uint8_t zr[E195_RL_SIZE];
        memset(zr, 0, sizeof zr);
        ok = wr(e, text, w, n * 2) && wr(e, text + n * 2, &z, 2) && wr(e, ws, init, sizeof init) && wr(e, rl, zr, sizeof zr);
    }
    if (ok) {
        callr(e, E195_RVA_WSTRING, 3, ws, text, n);
        uint64_t size = 0;
        ok = rd(e, ws + 0x18, &size, 8) && size == n;
        if (ok) callr(e, E195_RVA_RL_CTOR, 2, rl, ws);
        uintptr_t vp = 0;
        ok = ok && rq(e, rl, &vp) && vp;
    }
    if (ws) {
        uint64_t cap = 0; uintptr_t heap = 0;
        if (rd(e, ws + 0x20, &cap, 8) && cap >= 8 && rq(e, ws + 8, &heap) && heap) callr(e, E195_RVA_FREE, 1, heap);
        callr(e, E195_RVA_FREE, 1, ws);
    }
    if (text) callr(e, E195_RVA_FREE, 1, text);
    if (!ok && rl) { callr(e, E195_RVA_FREE, 1, rl); rl = 0; }
    return rl;
}
void drop_rl(s195_env *e, uintptr_t rl) { if (rl) { callr(e, E195_RVA_RL_DTOR, 1, rl); callr(e, E195_RVA_FREE, 1, rl); } }

}  // namespace

extern "C" {

int e195_clone_from_key(const char *clone_of, char *err, uint32_t cap) {
    if (!clone_of || !*clone_of) return E195_CLONE_NONE;
    if (strncmp(clone_of, "minecraft:", 10) && strchr(clone_of, ':')) {
        set_err(err, cap, "enchants195: clone_of must be a vanilla enchantment (minecraft:...)"); return -1;
    }
    const char *k = strip_ns(clone_of);
    for (uint32_t i = 1; i < E195_CLONE_COUNT; i++) if (!strcmp(k, kClone[i].key)) return (int)i;
    for (const char *id : kIdentity)
        if (!strcmp(k, id)) {
            if (err && cap)
                snprintf(err, cap, "enchants195: clone_of %s is refused: 1.95 reads its effect through the static Enchantments::%s pointer (identity check), a clone would do nothing; use clone_of none + a script, or a damage/protection clone", k, k);
            return -1;
        }
    set_err(err, cap, "enchants195: unknown clone_of enchantment");
    return -1;
}

uint32_t e195_category_from_applies(uint16_t m) {
    const uint16_t sword = 1u << 2, pick = 1u << 3, axe = 1u << 4, shovel = 1u << 5, hoe = 1u << 6;
    const uint16_t head = 1u << 7, chest = 1u << 8, legs = 1u << 9, feet = 1u << 10, armour = head | chest | legs | feet;
    if (!m) return E195_CAT_DEFAULT;
    if (m == head) return E195_CAT_ARMOR_HEAD;
    if (m == chest) return E195_CAT_ARMOR_CHEST;
    if (m == legs) return E195_CAT_ARMOR_LEGS;
    if (m == feet) return E195_CAT_ARMOR_FEET;
    if (!(m & ~armour)) return E195_CAT_ARMOR;
    if (m == sword) return E195_CAT_WEAPON;
    if (!(m & ~(pick | axe | shovel))) return E195_CAT_DIGGER;
    (void)hoe;
    return E195_CAT_BREAKABLE;
}

uint32_t e195_rarity_index(uint8_t w) { return w >= 10 ? 0 : w >= 5 ? 1 : w >= 2 ? 2 : 3; }
uint32_t e195_const_fn(uint32_t n) { return n >= 1 && n <= 10 ? kConst[n] : 0; }

int e195_check(const e195_spec *s, char *err, uint32_t cap) {
    if (!s) { set_err(err, cap, "enchants195: no spec"); return 0; }
    if (!key_ok(s->key)) { set_err(err, cap, "enchants195: key must be modid:name ([a-z0-9_./]), not minecraft:"); return 0; }
    if (s->id < E195_ID_MIN || s->id > E195_ID_MAX) { set_err(err, cap, "enchants195: id outside 100..1023"); return 0; }
    if (s->clone >= E195_CLONE_COUNT) { set_err(err, cap, "enchants195: bad clone"); return 0; }
    if (s->category >= E195_CAT_COUNT) { set_err(err, cap, "enchants195: bad category"); return 0; }
    if (s->max_level < 1 || s->max_level > 10) { set_err(err, cap, "enchants195: max_level must be 1..10 (level text exists up to 10)"); return 0; }
    if (!s->weight) { set_err(err, cap, "enchants195: weight 0"); return 0; }
    if (!s195_sid_reserved(s->name_sid)) { set_err(err, cap, "enchants195: name_sid is not a strings195 id"); return 0; }
    return 1;
}

int e195_verify(s195_env *e, const e195_record *r) {
    if (!e || !r || r->state != 1) return 0;
    uintptr_t reg = 0, vp = 0;
    if (!rq(e, e->base + E195_RVA_REGISTRY, &reg) || !reg || !rq(e, r->object, &vp) || vp != r->vtable + 16) return 0;
    uintptr_t by = vfn(e, reg, E195_SLOT_BY_ID), gid = vfn(e, reg, E195_SLOT_GET_ID);
    if (!by || !gid) return 0;
    return callf(e, by, 2, reg, (uint64_t)r->id) == r->object && (int32_t)callf(e, gid, 2, reg, r->object) == r->id;
}

int e195_publish(s195_env *e, const e195_spec *s, uint32_t n, e195_record *out) {
    if (!e || !e->read || !e->write || !e->call || !held(e) || !s || !out || !n || n > E195_MAX) return 0;
    for (uint32_t i = 0; i < n; i++) {
        if (!e195_check(&s[i], nullptr, 0)) return 0;
        for (uint32_t j = 0; j < i; j++) if (s[j].id == s[i].id || !strcmp(s[j].key, s[i].key)) return 0;
        memset(&out[i], 0, sizeof out[i]);
    }
    const uintptr_t B = e->base;
    uintptr_t reg = 0, regvt = 0;
    if (!rq(e, B + E195_RVA_REGISTRY, &reg) || !reg || !rq(e, reg, &regvt) || regvt != B + E195_RVA_REGISTRY_VT) return 0;
    uintptr_t f_by = 0, f_gid = 0, f_reg = 0, f_has = 0;
    if (!rq(e, regvt + E195_SLOT_BY_ID, &f_by) || !rq(e, regvt + E195_SLOT_GET_ID, &f_gid) || !rq(e, regvt + E195_SLOT_REGISTER, &f_reg) ||
        !rq(e, regvt + E195_SLOT_CONTAINS, &f_has) || !f_by || !f_gid || !f_reg || !f_has) return 0;
    // the stock bootstrap must have run (sharpness at 16 is a DamageEnchantment)
    {
        uintptr_t sharp = callf(e, f_by, 2, reg, 16), vp = 0;
        if (!sharp || !rq(e, sharp, &vp) || vp != B + E195_RVA_DAMAGE_VT) return 0;
    }
    // retail constant functions really are "mov $N,%eax; ret"
    for (uint32_t i = 0; i < n; i++) {
        uint8_t code[6], want[6] = {0xb8, s[i].max_level, 0, 0, 0, 0xc3};
        if (!rd(e, B + kConst[s[i].max_level], code, 6) || memcmp(code, want, 6)) return 0;
    }
    static Pre pre[E195_MAX];
    for (uint32_t i = 0; i < n; i++) {
        Pre &p = pre[i];
        memset(&p, 0, sizeof p);
        const CloneInfo &ci = kClone[s[i].clone];
        if (callf(e, f_by, 2, reg, (uint64_t)s[i].id)) return 0;                 // id taken
        // rarity: the stock Rarity object with that weight (+0x30)
        uint32_t ri = e195_rarity_index(s[i].weight), w = 0;
        if (!rq(e, B + E195_RVA_RARITIES + 8 * ri, &p.rarity) || !p.rarity || !rd(e, p.rarity + 0x30, &w, 4) || w != kWeight[ri]) return 0;
        if (s[i].category) {
            uintptr_t cvp = 0;
            if (!rq(e, B + kCat[s[i].category].global, &p.cat) || !p.cat || !rq(e, p.cat, &cvp) || cvp != B + kCat[s[i].category].vt) return 0;
        } else if (s[i].clone == E195_CLONE_NONE) {
            uintptr_t cvp = 0;     // none without a category: BREAKABLE
            if (!rq(e, B + kCat[E195_CAT_BREAKABLE].global, &p.cat) || !p.cat || !rq(e, p.cat, &cvp) || cvp != B + kCat[E195_CAT_BREAKABLE].vt) return 0;
        }
        if (ci.ctor == E195_RVA_PROT_CTOR && (!rq(e, B + E195_RVA_PROT_TYPES + 8 * (uint32_t)ci.arg, &p.arg_ptr) || !p.arg_ptr)) return 0;
        int32_t sid = ci.stock_id >= 0 ? ci.stock_id : (armour_cat(s[i].category) ? 0 : 16);
        uintptr_t stock = callf(e, f_by, 2, reg, (uint64_t)sid);
        if (!stock || !rq(e, stock + 0x18, &p.slots) || !p.slots || !rd(e, stock + 0x20, &p.nslots, 4) || !p.nslots || p.nslots > 6) return 0;
        // class vtable sanity: the slots we keep/replace hold the expected stock functions
        uintptr_t f50 = 0, f00 = 0;
        if (!rq(e, B + ci.vt + E195_SLOT_RARITY, &f00) || f00 != B + 0x24ab30u || !rq(e, B + E195_RVA_BASE_VT + E195_SLOT_DESC_ID, &f50) || f50 != B + E195_RVA_GET_DESC_FIELD) return 0;
    }
    // keys must be absent (ResourceLocation temporaries are freed on refusal)
    for (uint32_t i = 0; i < n; i++) {
        pre[i].rl = make_rl(e, s[i].key);
        if (!pre[i].rl || (callf(e, f_has, 2, reg, pre[i].rl) & 0xff)) {
            for (uint32_t k = 0; k <= i; k++) drop_rl(e, pre[k].rl);
            return 0;
        }
    }
    int rc = 1;
    for (uint32_t i = 0; i < n; i++) {
        const CloneInfo &ci = kClone[s[i].clone];
        Pre &p = pre[i];
        e195_record &r = out[i];
        r.id = s[i].id; r.clone = s[i].clone; r.state = -1;
        uintptr_t obj = callr(e, E195_RVA_ALLOC, 1, ci.size), vt = callr(e, E195_RVA_ALLOC, 1, (2 + E195_VT_SLOTS) * 8);
        if (!obj || !vt) { rc = -1; drop_rl(e, p.rl); continue; }
        uint8_t z[0x38];
        memset(z, 0, sizeof z);
        if (!wr(e, obj, z, ci.size)) { rc = -1; drop_rl(e, p.rl); continue; }
        if (ci.ctor == E195_RVA_BASE_CTOR) callr(e, ci.ctor, 5, obj, p.rarity, p.cat, p.slots, p.nslots);
        else if (ci.ctor == E195_RVA_DAMAGE_CTOR) callr(e, ci.ctor, 5, obj, p.rarity, (uint64_t)ci.arg, p.slots, p.nslots);
        else if (ci.ctor == E195_RVA_PROT_CTOR) callr(e, ci.ctor, 5, obj, p.rarity, p.arg_ptr, p.slots, p.nslots);
        else callr(e, ci.ctor, 4, obj, p.rarity, p.slots, p.nslots);
        uintptr_t vp = 0, rar = 0;
        uint64_t table[2 + E195_VT_SLOTS];
        if (!rq(e, obj, &vp) || vp != B + ci.vt || !rq(e, obj + 0x28, &rar) || rar != p.rarity || !rd(e, B + ci.vt - 16, table, sizeof table)) { rc = -1; drop_rl(e, p.rl); continue; }
        uint64_t *slot = table + 2;
        slot[E195_SLOT_MAX_LEVEL / 8] = B + kConst[s[i].max_level];
        slot[E195_SLOT_DESC_ID / 8] = B + E195_RVA_GET_DESC_FIELD;
        slot[E195_SLOT_TREASURE / 8] = B + (s[i].treasure ? E195_RVA_RET1 : E195_RVA_TREASURE_FALSE);
        slot[E195_SLOT_CURSE / 8] = B + (s[i].curse ? E195_RVA_RET1 : E195_RVA_CURSE_FALSE);
        if (s[i].category) slot[E195_SLOT_CAN_ENCHANT / 8] = B + E195_RVA_CAN_ENCHANT_CAT;
        uint32_t sidv = s[i].name_sid;
        if (!wr(e, vt, table, sizeof table) || !wr(e, obj + 0x10, &sidv, 4) || (s[i].category && !wq(e, obj + 0x08, p.cat)) || !wq(e, obj, vt + 16)) { rc = -1; drop_rl(e, p.rl); continue; }
        r.object = obj; r.vtable = vt;
        callf(e, f_reg, 4, reg, (uint64_t)s[i].id, p.rl, obj);
        drop_rl(e, p.rl);
        r.state = 1;
        if (!e195_verify(e, &r)) { r.state = -1; rc = -1; }
    }
    return rc;
}

}  // extern "C"
