// effects195: new status effects and potion types on 1.95 (see effects195.h, STATUS.md).
#include "effects195.h"
#include <stdio.h>
#include <string.h>

namespace {
void set_err(char *err, uint32_t cap, const char *m) { if (err && cap) snprintf(err, cap, "%s", m); }

struct EnvIo {
    lce195e_env *e;
    bool held() const { return e->m.held && e->m.held(e->m.ctx) == 1; }
    bool rd(uintptr_t a, void *v, size_t n) const { return a && a <= UINTPTR_MAX - n && held() && e->m.read(e->m.ctx, a, v, n) == 1; }
    bool wr(uintptr_t a, const void *v, size_t n) const { return a && a <= UINTPTR_MAX - n && held() && e->m.write(e->m.ctx, a, v, n) == 1; }
    bool rq(uintptr_t a, uintptr_t *v) const { return rd(a, v, 8); }
};

// ---- stock tables (Unicorn-extracted from the real initializers, pinned in tests) ----
// Registration order of 0x4781e0, ids 1..31.
const char *const kEffects[] = {"speed", "slowness", "haste", "mining_fatigue", "strength", "instant_health", "instant_damage", "jump_boost",
                                "nausea", "regeneration", "resistance", "fire_resistance", "water_breathing", "invisibility", "blindness",
                                "night_vision", "hunger", "weakness", "poison", "wither", "health_boost", "absorption", "saturation", "glowing",
                                "levitation", "luck", "unluck", "conduit_power", "slow_falling", "bad_omen", "village_hero"};
// +0x5c icon frame per stock id (index 0 unused).
const uint8_t kIcon[32] = {0, 14, 13, 3, 7, 15, 0, 0, 6, 8, 11, 12, 2, 16, 5, 1, 9, 4, 17, 10, 18, 19, 20, 0, 21, 22, 23, 24, 27, 26, 28, 29};
// +0x74 eMinecraftColour per stock id.
const uint16_t kColour[32] = {0, 0xfd, 0xfe, 0xff, 0x100, 0x101, 0x102, 0x103, 0x104, 0x105, 0x106, 0x107, 0x108, 0x109, 0x10a, 0x10b,
                              0x10c, 0x10d, 0x10e, 0x10f, 0x110, 0x111, 0x112, 0x113, 0x114, 0x115, 0x116, 0x117, 0x11a, 0x119, 0x11b, 0x11c};
// colours.col (app0/Common/res/TitleUpdate/res) RGB of the Effect_* enums 0xfd..0x11c; 0x114 Glowing is absent there (Java value).
const uint32_t kEnumRgb[32] = {0x7cafc6, 0x5a6c81, 0xd9c043, 0x4a4217, 0x932423, 0xf82423, 0x430a09, 0x22ff4c, 0x551d4a, 0xcd5cab, 0x99453a,
                               0xe49a3a, 0x2e5299, 0x7f8392, 0x1f1f23, 0x1f1fa1, 0x587653, 0x484d48, 0x4e9331, 0x352a27, 0xf87d23, 0x2552a5,
                               0xf82423, 0x94a061, 0xceffff, 0x339900, 0xc0a44d, 0x755b63, 0xffefd1, 0x7ccafc, 0xffefd1, 0x44ff44};
// Clone behaviour per stock id (STATUS.md "clone kinds").
const uint8_t kKind[32] = {0,
    LCE195E_CLONE_ATTRIBUTE, LCE195E_CLONE_ATTRIBUTE, LCE195E_CLONE_ATTRIBUTE, LCE195E_CLONE_ATTRIBUTE, LCE195E_CLONE_ATTRIBUTE, /* 1..5 */
    LCE195E_CLONE_FORWARD, LCE195E_CLONE_FORWARD,                                  /* 6 instant_health, 7 instant_damage */
    LCE195E_CLONE_IDENTITY, LCE195E_CLONE_IDENTITY,                                /* 8 jump, 9 nausea */
    LCE195E_CLONE_FORWARD,                                                         /* 10 regeneration */
    LCE195E_CLONE_IDENTITY, LCE195E_CLONE_IDENTITY, LCE195E_CLONE_IDENTITY, LCE195E_CLONE_IDENTITY, LCE195E_CLONE_IDENTITY, LCE195E_CLONE_IDENTITY, /* 11..16 */
    LCE195E_CLONE_FORWARD,                                                         /* 17 hunger */
    LCE195E_CLONE_ATTRIBUTE,                                                       /* 18 weakness */
    LCE195E_CLONE_FORWARD, LCE195E_CLONE_FORWARD,                                  /* 19 poison, 20 wither */
    LCE195E_CLONE_FORWARD, LCE195E_CLONE_FORWARD, LCE195E_CLONE_FORWARD,           /* 21 health_boost, 22 absorption, 23 saturation */
    LCE195E_CLONE_IDENTITY, LCE195E_CLONE_IDENTITY,                                /* 24 glowing, 25 levitation */
    LCE195E_CLONE_ATTRIBUTE, LCE195E_CLONE_ATTRIBUTE,                              /* 26 luck, 27 unluck */
    LCE195E_CLONE_IDENTITY, LCE195E_CLONE_IDENTITY, LCE195E_CLONE_IDENTITY, LCE195E_CLONE_IDENTITY}; /* 28..31 */

// Registration order of 0x55e6d0, ids 0..42.
const char *const kPotions[] = {"empty", "water", "mundane", "thick", "awkward", "night_vision", "long_night_vision", "invisibility",
                                "long_invisibility", "leaping", "long_leaping", "strong_leaping", "fire_resistance", "long_fire_resistance",
                                "swiftness", "long_swiftness", "strong_swiftness", "slowness", "long_slowness", "water_breathing",
                                "long_water_breathing", "healing", "strong_healing", "harming", "strong_harming", "poison", "long_poison",
                                "strong_poison", "regeneration", "long_regeneration", "strong_regeneration", "strength", "long_strength",
                                "strong_strength", "weakness", "long_weakness", "luck", "turtle_master", "long_turtle_master",
                                "strong_turtle_master", "wither", "slow_falling", "long_slow_falling"};
const uint32_t kArrowTooltipSid = 0x2453270cu;  // "An arrow that applies a potion effect based on what it is tipped with."

int32_t lookup(const char *key, const char *const *names, unsigned n, int32_t first) {
    if (!key || strncmp(key, "minecraft:", 10)) return -1;
    for (unsigned i = 0; i < n; i++) if (!strcmp(key + 10, names[i])) return first + (int32_t)i;
    return -1;
}

bool valid_key(const char *k) {
    if (!k) return false;
    const char *c = strchr(k, ':');
    size_t n = strlen(k);
    if (!c || c == k || !c[1] || n >= 64 || !strncmp(k, "minecraft:", 10)) return false;
    for (const char *p = k; *p; p++)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_' || *p == '.' || *p == '-' || *p == '/' || (*p == ':' && p == c)))
            return false;
    return true;
}

// ---- published tables (read by the thunks on the server thread) ----
lce195e_effect g_eff[LCE195E_MAX_EFFECTS];
char g_eff_key[LCE195E_MAX_EFFECTS][64];
volatile unsigned g_eff_count;
lce195e_potion g_pot[LCE195E_MAX_POTIONS];
volatile unsigned g_pot_count;

const lce195e_effect *find_effect(uintptr_t self) {
    unsigned n = g_eff_count;
    for (unsigned i = 0; i < n; i++) if (g_eff[i].object == self) return &g_eff[i];
    return nullptr;
}

// ResourceLocation from "ns:path" through the retail wstring + ctor. Scratch buffers are
// ours (static); the registry copies the key into its own nodes (Unicorn-verified).
struct Rl { alignas(16) unsigned char ns[0x40], path[0x40], rl[0x80]; };
bool make_rl(lce195e_env *e, Rl &r, const char *key) {
    const char *c = strchr(key, ':');
    uint16_t a[64], b[64];
    size_t na = (size_t)(c - key), nb = strlen(c + 1);
    if (na >= 64 || nb >= 64) return false;
    for (size_t i = 0; i < na; i++) a[i] = (uint8_t)key[i];
    for (size_t i = 0; i < nb; i++) b[i] = (uint8_t)c[1 + i];
    memset(&r, 0, sizeof r);
    uint64_t cap7 = 7;                       // empty SSO wstring, exactly as the inline code sets it up
    memcpy(r.ns + 0x20, &cap7, 8); memcpy(r.path + 0x20, &cap7, 8);
    e->n.wstring(r.ns, a, na); e->n.wstring(r.path, b, nb);
    e->n.rl_ctor(r.rl, r.ns, r.path);
    return true;
}
void free_ws(lce195e_env *e, unsigned char *ws) {
    uint64_t cap, ptr;
    memcpy(&cap, ws + 0x20, 8); memcpy(&ptr, ws + 8, 8);
    if (cap >= 8 && ptr && e->n.release) e->n.release((void *)(uintptr_t)ptr);
}
void drop_rl(lce195e_env *e, Rl &r) { e->n.rl_dtor(r.rl); free_ws(e, r.ns); free_ws(e, r.path); }
}  // namespace

extern "C" {

int32_t lce195e_stock_effect_id(const char *key) { return lookup(key, kEffects, sizeof kEffects / sizeof *kEffects, 1); }
int32_t lce195e_stock_potion_id(const char *key) { return lookup(key, kPotions, sizeof kPotions / sizeof *kPotions, 0); }
int lce195e_clone_kind(int32_t id) { return id >= 1 && id <= 31 ? (int)kKind[id] : (int)LCE195E_CLONE_NONE; }
uint32_t lce195e_stock_icon(int32_t id) { return id >= 1 && id <= 31 ? kIcon[id] : 0; }
uint32_t lce195e_stock_colour(int32_t id) { return id >= 1 && id <= 31 ? kColour[id] : 0; }

uint32_t lce195e_nearest_colour(uint32_t rgb) {
    uint32_t best = 0xfd; long bestd = -1;
    for (unsigned i = 0; i < 32; i++) {
        long dr = (long)((rgb >> 16) & 0xff) - (long)((kEnumRgb[i] >> 16) & 0xff);
        long dg = (long)((rgb >> 8) & 0xff) - (long)((kEnumRgb[i] >> 8) & 0xff);
        long db = (long)(rgb & 0xff) - (long)(kEnumRgb[i] & 0xff);
        long dd = dr * dr + dg * dg + db * db;
        if (bestd < 0 || dd < bestd) { bestd = dd; best = 0xfd + i; }
    }
    return best;
}

/* ============================== effects ================================= */
lce_status lce195e_effect_check(const lce_effect_desc *d, char *err, uint32_t cap) {
    if (!d || !valid_key(d->key)) { set_err(err, cap, "effects195: effect key must be \"modid:name\" (lowercase, < 64 chars, not minecraft:)"); return LCE_E_INVALID; }
    int32_t clone = 0;
    if (d->clone_of) {
        clone = lce195e_stock_effect_id(d->clone_of);
        if (clone < 0) { set_err(err, cap, "effects195: clone_of must be a stock 1.95 effect (minecraft:speed .. minecraft:village_hero)"); return LCE_E_UNSUPPORTED; }
        if (kKind[clone] == LCE195E_CLONE_IDENTITY) {
            set_err(err, cap, "effects195: this vanilla effect works only through identity checks (hasEffect(MobEffects::X) in game code), so a clone would "
                              "show an icon but do nothing; clone a forwarded effect (regeneration, poison, wither, hunger, saturation, instant_health, "
                              "instant_damage, absorption, health_boost) or an attribute effect (speed, slowness, haste, mining_fatigue, strength, weakness, "
                              "luck, unluck), or use no clone_of with a tick script");
            return LCE_E_UNSUPPORTED;
        }
    }
    bool clone_instant = clone == 6 || clone == 7 || clone == 23;
    if (d->instant && !clone_instant) { set_err(err, cap, "effects195: instant effects need clone_of minecraft:instant_health, instant_damage or saturation (InstantenousMobEffect class)"); return LCE_E_UNSUPPORTED; }
    if (d->icon_texture && lce195e_stock_effect_id(d->icon_texture) < 0) {
        set_err(err, cap, "effects195: effect icons are frames of the fourj.MobEffect UI movie (no runtime atlas); icon_texture must be a stock effect key "
                          "such as minecraft:speed (its frame is reused), or NULL for the clone's icon");
        return LCE_E_UNSUPPORTED;
    }
    return LCE_OK;
}

lce_status lce195e_effect_spec_from_desc(const lce_effect_desc *d, int32_t id, uint32_t name_sid, lce195e_effect_spec *out, char *err, uint32_t cap) {
    lce_status st = lce195e_effect_check(d, err, cap);
    if (st != LCE_OK) return st;
    if (!out || id < LCE195E_EFFECT_ID_MIN || id > LCE195E_EFFECT_ID_MAX || !name_sid || name_sid == 0xffffffffu) {
        set_err(err, cap, "effects195: effect id must be 32..127 and the name string id nonzero"); return LCE_E_INVALID;
    }
    memset(out, 0, sizeof *out);
    out->key = d->key; out->id = id; out->name_sid = name_sid;
    out->clone_of = d->clone_of ? lce195e_stock_effect_id(d->clone_of) : 0;
    out->colour = d->color_rgb ? lce195e_nearest_colour(d->color_rgb) : 0;
    out->icon = d->icon_texture ? kIcon[lce195e_stock_effect_id(d->icon_texture)] : UINT32_MAX;
    out->beneficial = d->beneficial ? 1 : 0;
    out->tick_interval = 0;
    return LCE_OK;
}

uintptr_t lce195e_effect_object(const char *key) {
    unsigned n = g_eff_count;
    for (unsigned i = 0; key && i < n; i++) if (!strcmp(g_eff_key[i], key)) return g_eff[i].object;
    return 0;
}
int32_t lce195e_effect_id(const char *key) {
    unsigned n = g_eff_count;
    for (unsigned i = 0; key && i < n; i++) if (!strcmp(g_eff_key[i], key)) return g_eff[i].id;
    int32_t s = lce195e_stock_effect_id(key);
    return s;
}

int lce195e_effects_publish(lce195e_effect_publisher *p, const lce195e_effect_spec *specs, unsigned count) {
    if (!p || p->state || !p->env || !specs || !count || count > LCE195E_MAX_EFFECTS || g_eff_count) return 0;
    lce195e_env *e = p->env;
    const lce195e_natives &n = e->n;
    EnvIo io{e};
    if (!e->m.read || !e->m.write || !e->m.held || !n.alloc || !n.wstring || !n.rl_ctor || !n.rl_dtor || !n.effect_ctor ||
        !n.effect_add_modifier || !n.effect_by_id || !n.reg_register || !e->thunk[0] || !e->thunk[1] || !e->thunk[2]) return 0;
    p->state = 3;
    if (!io.held()) return 0;
    uintptr_t reg = 0;
    int32_t counter = 0;
    if (!io.rq(e->base + LCE195E_RVA_EFFECT_REGISTRY, &reg) || !reg || (reg & 7) ||
        !io.rd(e->base + LCE195E_RVA_EFFECT_COUNT, &counter, 4) || counter < 31 || counter >= LCE195E_EFFECT_ID_MIN) return 0;
    p->counter_before = counter;
    // ---- validate everything before the first native allocation ----
    struct Plan { uintptr_t stock, stock_vt, fn[3]; uint64_t vt[2 + LCE195E_EFF_VT_SLOTS]; double durmod; uint32_t icon, colour; bool attack; uint64_t extra; } plan[LCE195E_MAX_EFFECTS];
    int32_t last = counter;
    for (unsigned i = 0; i < count; i++) {
        const lce195e_effect_spec &s = specs[i];
        Plan &pl = plan[i];
        memset(&pl, 0, sizeof pl);
        if (!valid_key(s.key) || s.id <= last || s.id < LCE195E_EFFECT_ID_MIN || s.id > LCE195E_EFFECT_ID_MAX || !s.name_sid || s.name_sid == 0xffffffffu) return 0;
        if (s.clone_of < 0 || s.clone_of > 31 || (s.clone_of && kKind[s.clone_of] == LCE195E_CLONE_IDENTITY)) return 0;
        if (s.on_tick == nullptr && s.tick_interval) return 0;
        for (unsigned j = 0; j < i; j++) if (!strcmp(specs[j].key, s.key)) return 0;
        last = s.id;
        if (n.effect_by_id(s.id)) return 0;                                    // the id must be free
        pl.colour = s.colour ? s.colour : (s.clone_of ? kColour[s.clone_of] : 0xfd);
        if (pl.colour < 0xfd || pl.colour > 0x11c) return 0;
        pl.icon = s.icon != UINT32_MAX ? s.icon : (s.clone_of ? kIcon[s.clone_of] : 0);
        if (pl.icon > 29) return 0;
        uintptr_t vt = e->base + LCE195E_RVA_EFFECT_VT;
        pl.durmod = 1.0;
        if (s.clone_of) {
            pl.stock = n.effect_by_id(s.clone_of);
            if (!pl.stock || !io.rq(pl.stock, &vt) || !vt || !io.rd(pl.stock + LCE195E_EFF_DURMOD, &pl.durmod, 8)) return 0;
            pl.attack = vt == e->base + LCE195E_RVA_ATTACK_DAMAGE_VT;
            if (pl.attack && !io.rd(pl.stock + 0x78, &pl.extra, 8)) return 0;
            uint32_t sid = 0;
            if (!io.rd(pl.stock + LCE195E_EFF_NAME_SID, &sid, 4) || sid == 0xffffffffu) return 0;   // a real stock effect
        }
        pl.stock_vt = vt;
        if (!io.rd(vt - 16, pl.vt, sizeof pl.vt)) return 0;
        for (unsigned k = 0; k < 3; k++) pl.fn[k] = s.clone_of ? (uintptr_t)pl.vt[2 + k] : 0;
        for (unsigned k = 0; k < LCE195E_EFF_VT_SLOTS; k++) if (!pl.vt[2 + k]) return 0;
    }
    // ---- construct + register (from here on: -1 on any failure) ----
    p->count = count; p->state = 1;
    lce195e_effect *rec = p->records;
    for (unsigned i = 0; i < count; i++) {
        const lce195e_effect_spec &s = specs[i];
        Plan &pl = plan[i];
        lce195e_effect &r = rec[i];
        memset(&r, 0, sizeof r);
        r.id = s.id; r.stock = pl.stock; r.tick_interval = s.tick_interval; r.on_tick = s.on_tick; r.user = s.user;
        for (unsigned k = 0; k < 3; k++) r.stock_fn[k] = pl.fn[k];
        void *obj = n.alloc(LCE195E_EFFECT_SIZE);
        void *blk = n.alloc(8 * (2 + LCE195E_EFF_VT_SLOTS));
        if (!obj || !blk) { p->state = 4; return -1; }
        r.object = (uintptr_t)obj; r.vtable_block = (uintptr_t)blk;
        n.effect_ctor(obj, s.beneficial ? 0 : 1, (int)pl.colour);
        uint8_t ben = s.beneficial ? 1 : 0;
        if (!io.wr(r.object + LCE195E_EFF_NAME_SID, &s.name_sid, 4) || !io.wr(r.object + LCE195E_EFF_ICON, &pl.icon, 4) ||
            !io.wr(r.object + LCE195E_EFF_DURMOD, &pl.durmod, 8) || !io.wr(r.object + LCE195E_EFF_BENEFIT, &ben, 1)) { p->state = 4; return -1; }
        if (pl.attack && !io.wr(r.object + 0x78, &pl.extra, 8)) { p->state = 4; return -1; }
        // attribute modifiers of the clone: list nodes {next, prev, Attribute*, AttributeModifier*}; modifier {+0 amount, +8 op, +0x38 id}
        if (pl.stock) {
            uintptr_t head = 0, node = 0;
            if (!io.rq(pl.stock + 0x10, &head) || !io.rq(head, &node)) { p->state = 4; return -1; }
            for (unsigned guard = 0; node != head; guard++) {
                uintptr_t attr = 0, mod = 0; double amount = 0; int32_t op = 0, mid = 0;
                if (guard >= 16 || !io.rq(node + 0x10, &attr) || !io.rq(node + 0x18, &mod) || !attr || !mod ||
                    !io.rd(mod, &amount, 8) || !io.rd(mod + 8, &op, 4) || !io.rd(mod + 0x38, &mid, 4)) { p->state = 4; return -1; }
                n.effect_add_modifier(obj, attr, mid, op, amount);
                r.modifiers++;
                if (!io.rq(node, &node)) { p->state = 4; return -1; }
            }
            uint64_t sz = 0;
            if (!io.rd(r.object + 0x18, &sz, 8) || sz != r.modifiers) { p->state = 4; return -1; }
        }
        // private vtable: RTTI prefix + 15 slots of the clone class, slots 0/1/2 -> thunks
        uint64_t v[2 + LCE195E_EFF_VT_SLOTS];
        memcpy(v, pl.vt, sizeof v);
        for (unsigned k = 0; k < 3; k++) v[2 + k] = e->thunk[k];
        uintptr_t vptr = r.vtable_block + 16;
        if (!io.wr(r.vtable_block, v, sizeof v) || !io.wr(r.object, &vptr, 8)) { p->state = 4; return -1; }
        Rl key;
        if (!make_rl(e, key, s.key)) { p->state = 4; return -1; }
        n.reg_register(reg, s.id, key.rl, r.object);
        drop_rl(e, key);
        if (n.effect_by_id(s.id) != r.object) { p->state = 4; return -1; }
        snprintf(g_eff_key[i], sizeof g_eff_key[i], "%s", s.key);
        g_eff[i] = r;
        p->published = i + 1;
    }
    // the HUD allocates int[count+1] from this global: it must cover the highest id
    int32_t top = specs[count - 1].id, back = 0;
    if (!io.wr(e->base + LCE195E_RVA_EFFECT_COUNT, &top, 4) || !io.rd(e->base + LCE195E_RVA_EFFECT_COUNT, &back, 4) || back != top) { p->state = 4; return -1; }
    for (unsigned i = 0; i < count; i++) {
        uintptr_t vp = 0;
        if (n.effect_by_id(rec[i].id) != rec[i].object || !io.rq(rec[i].object, &vp) || vp != rec[i].vtable_block + 16) { p->state = 4; return -1; }
    }
    for (int32_t id = 1; id <= 31; id++) if (!n.effect_by_id(id)) { p->state = 4; return -1; }   // stock ids untouched
    g_eff_count = count;
    p->state = 2;
    return 1;
}

uintptr_t lce195e_forward_target(uintptr_t self, unsigned slot, uintptr_t *self_out) {
    const lce195e_effect *r = find_effect(self);
    if (self_out) *self_out = r ? r->stock : 0;
    if (!r || slot > 2 || !r->stock) return 0;
    return r->stock_fn[slot];
}

void lce195e_thunk_tick(uintptr_t self, uintptr_t mob_sp, int32_t amplifier) {
    const lce195e_effect *r = find_effect(self);
    if (!r) return;
    if (r->on_tick) r->on_tick(r->user, r->id, mob_sp ? *(const uintptr_t *)mob_sp : 0, amplifier);
    if (r->stock && r->stock_fn[0]) ((void (*)(uintptr_t, uintptr_t, int32_t))r->stock_fn[0])(r->stock, mob_sp, amplifier);
}

void lce195e_thunk_instant(uintptr_t self, uintptr_t a, uintptr_t b, uintptr_t mob_sp, int32_t amplifier, double scale) {
    const lce195e_effect *r = find_effect(self);
    if (r && r->stock && r->stock_fn[1])
        ((void (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, int32_t, double))r->stock_fn[1])(r->stock, a, b, mob_sp, amplifier, scale);
}

int lce195e_thunk_ready(uintptr_t self, int32_t duration, int32_t amplifier) {
    const lce195e_effect *r = find_effect(self);
    if (!r) return 0;
    if (r->tick_interval) return duration > 0 && duration % (int32_t)r->tick_interval == 0;
    if (r->stock && r->stock_fn[2]) return ((int (*)(uintptr_t, int32_t, int32_t))r->stock_fn[2])(r->stock, duration, amplifier) & 1;
    return 0;
}

/* ============================== potions ================================= */
lce_status lce195e_potion_check(const lce_potion_desc *d, char *err, uint32_t cap) {
    if (!d || !valid_key(d->key)) { set_err(err, cap, "effects195: potion key must be \"modid:name\" (lowercase, < 64 chars, not minecraft:)"); return LCE_E_INVALID; }
    if (d->effect_count > LCE195E_MAX_POTION_EFFECTS || (d->effect_count && !d->effects)) { set_err(err, cap, "effects195: a potion has at most 8 effects"); return LCE_E_INVALID; }
    for (uint32_t i = 0; i < d->effect_count; i++) {
        const lce_effect_ref &r = d->effects[i];
        if (!r.effect || (!strncmp(r.effect, "minecraft:", 10) ? lce195e_stock_effect_id(r.effect) < 0 : !valid_key(r.effect))) {
            set_err(err, cap, "effects195: potion effect must be a stock 1.95 effect or a mod effect key"); return LCE_E_INVALID;
        }
        if (!r.duration_ticks || r.duration_ticks > 20u * 60 * 60 * 8 || r.amplifier > 127) { set_err(err, cap, "effects195: potion effect duration 1..576000 ticks, amplifier 0..127"); return LCE_E_INVALID; }
    }
    return LCE_OK;
}

lce_status lce195e_potion_spec_from_desc(lce195e_env *e, const lce_potion_desc *d, const uint32_t sid[6], lce195e_potion_spec *out, char *err, uint32_t cap) {
    lce_status st = lce195e_potion_check(d, err, cap);
    if (st != LCE_OK) return st;
    if (!e || !out || !sid || !sid[0] || !sid[1] || !sid[2] || !sid[3]) { set_err(err, cap, "effects195: potion needs four name string ids"); return LCE_E_INVALID; }
    memset(out, 0, sizeof *out);
    out->key = d->key;
    for (unsigned i = 0; i < 4; i++) out->sid[i] = sid[i];
    out->sid[4] = sid[4] ? sid[4] : sid[0];
    out->sid[5] = sid[5] ? sid[5] : kArrowTooltipSid;
    for (uint32_t i = 0; i < d->effect_count; i++) {
        const lce_effect_ref &r = d->effects[i];
        int32_t stock = lce195e_stock_effect_id(r.effect);
        uintptr_t eff = stock > 0 ? (e->n.effect_by_id ? e->n.effect_by_id(stock) : 0) : lce195e_effect_object(r.effect);
        if (!eff) { set_err(err, cap, "effects195: potion effect is not registered (mod effects publish in the item window, before potions)"); return LCE_E_UNSUPPORTED; }
        out->effects[i] = {eff, (int32_t)r.duration_ticks, (int32_t)r.amplifier};
    }
    out->effect_count = d->effect_count;
    return LCE_OK;
}

int32_t lce195e_potion_id(const char *key) {
    unsigned n = g_pot_count;
    for (unsigned i = 0; key && i < n; i++) if (!strcmp(g_pot[i].key, key)) return g_pot[i].id;
    return lce195e_stock_potion_id(key);
}

int lce195e_potions_publish(lce195e_potion_publisher *p, const lce195e_potion_spec *specs, unsigned count) {
    if (!p || p->state || !p->env || !specs || !count || count > LCE195E_MAX_POTIONS || g_pot_count) return 0;
    lce195e_env *e = p->env;
    const lce195e_natives &n = e->n;
    EnvIo io{e};
    if (!e->m.read || !e->m.write || !e->m.held || !n.alloc || !n.wstring || !n.rl_ctor || !n.rl_dtor || !n.mei_ctor ||
        !n.potion_by_id || !n.ptr_vec_grow || !n.reg_register || !n.reg_get) return 0;
    p->state = 3;
    if (!io.held()) return 0;
    uintptr_t reg = 0;
    int32_t counter = 0;
    if (!io.rq(e->base + LCE195E_RVA_POTION_REGISTRY, &reg) || !reg || (reg & 7) ||
        !io.rd(e->base + LCE195E_RVA_POTION_COUNT, &counter, 4) || counter < 43 || counter > 4096) return 0;
    // DefaultedMappedRegistry: byId out of range and get() of an unknown key answer the default
    // (minecraft:water, key at 0x142e930). The next id must still be unassigned.
    const uintptr_t dflt = n.potion_by_id(0x3fffffff);
    if (!dflt || !n.potion_by_id(42) || n.potion_by_id(42) == dflt || n.potion_by_id(counter) != dflt) return 0;
    p->counter_before = counter;
    for (unsigned i = 0; i < count; i++) {
        const lce195e_potion_spec &s = specs[i];
        if (!valid_key(s.key) || s.effect_count > LCE195E_MAX_POTION_EFFECTS) return 0;
        for (unsigned k = 0; k < 6; k++) if (!s.sid[k] || s.sid[k] == 0xffffffffu) return 0;
        for (unsigned k = 0; k < s.effect_count; k++)
            if (!s.effects[k].effect || s.effects[k].duration <= 0 || s.effects[k].amplifier < 0 || s.effects[k].amplifier > 127) return 0;
        for (unsigned j = 0; j < i; j++) if (!strcmp(specs[j].key, s.key)) return 0;
        // the key must not exist yet: the defaulted registry answers unknown keys with its default (empty)
        Rl k;
        if (!make_rl(e, k, s.key)) return 0;
        uintptr_t got = n.reg_get(reg, k.rl);
        drop_rl(e, k);
        if (got != dflt) return 0;
    }
    p->count = count; p->state = 1;
    for (unsigned i = 0; i < count; i++) {
        const lce195e_potion_spec &s = specs[i];
        lce195e_potion &r = p->records[i];
        memset(&r, 0, sizeof r);
        r.id = counter + (int32_t)i;
        snprintf(r.key, sizeof r.key, "%s", s.key);
        void *obj = n.alloc(LCE195E_POTION_SIZE);
        if (!obj) { p->state = 4; return -1; }
        r.object = (uintptr_t)obj;
        uint8_t zero[LCE195E_POTION_SIZE] = {};
        if (!io.wr(r.object, zero, sizeof zero) || !io.wr(r.object, s.sid, sizeof s.sid)) { p->state = 4; return -1; }
        const uintptr_t vec = r.object + LCE195E_POTION_VEC;
        if (s.effect_count) {
            n.ptr_vec_grow(vec, (int)s.effect_count);
            uintptr_t h[3] = {};
            if (!io.rd(vec + 8, h, 24) || !h[0] || h[1] != h[0] || h[2] < h[0] + 8 * s.effect_count) { p->state = 4; return -1; }
            for (unsigned k = 0; k < s.effect_count; k++) {
                void *mei = n.alloc(0x18);
                if (!mei) { p->state = 4; return -1; }
                n.mei_ctor(mei, s.effects[k].effect, s.effects[k].duration, s.effects[k].amplifier);
                r.mei[k] = (uintptr_t)mei;
            }
            uintptr_t end = h[0] + 8 * s.effect_count;
            if (!io.wr(h[0], r.mei, 8 * s.effect_count) || !io.wr(vec + 0x10, &end, 8)) { p->state = 4; return -1; }
        }
        Rl k;
        if (!make_rl(e, k, s.key)) { p->state = 4; return -1; }
        n.reg_register(reg, r.id, k.rl, r.object);
        drop_rl(e, k);
        int32_t next = r.id + 1;
        if (!io.wr(e->base + LCE195E_RVA_POTION_COUNT, &next, 4)) { p->state = 4; return -1; }
        if (n.potion_by_id(r.id) != r.object) { p->state = 4; return -1; }
        Rl k2;
        if (!make_rl(e, k2, s.key)) { p->state = 4; return -1; }
        uintptr_t got = n.reg_get(reg, k2.rl);
        drop_rl(e, k2);
        if (got != r.object) { p->state = 4; return -1; }
        g_pot[i] = r;
        p->published = i + 1;
    }
    for (int32_t id = 0; id < counter; id++) {
        uintptr_t v = n.potion_by_id(id);
        if (!v || (id != 1 && v == dflt)) { p->state = 4; return -1; }   // stock ids untouched (1 is water, the default itself)
    }
    g_pot_count = count;
    p->state = 2;
    return 1;
}

void lce195e_reset_for_tests(void) { g_eff_count = 0; g_pot_count = 0; memset(g_eff, 0, sizeof g_eff); memset(g_pot, 0, sizeof g_pot); }

/* ============================== console binding ========================= */
#ifndef LCE195E_HOST
namespace {
uintptr_t slot(uintptr_t obj, unsigned off) { return *(const uintptr_t *)(*(const uintptr_t *)obj + off); }
void reg_register(uintptr_t reg, int id, void *rl, uintptr_t v) { ((void (*)(uintptr_t, int, void *, uintptr_t))slot(reg, LCE195E_REG_REGISTER))(reg, id, rl, v); }
uintptr_t reg_get(uintptr_t reg, void *rl) { return ((uintptr_t(*)(uintptr_t, void *))slot(reg, LCE195E_REG_GET))(reg, rl); }
}  // namespace
void lce195e_natives_bind(lce195e_env *e, uintptr_t base) {
    lce195e_natives &n = e->n;
    memset(&n, 0, sizeof n);
    e->base = base;
    n.alloc = (void *(*)(size_t))(base + LCE195E_RVA_ALLOC);
    n.release = (void (*)(void *))(base + LCE195E_RVA_FREE);
    n.wstring = (void (*)(void *, const uint16_t *, uint64_t))(base + LCE195E_RVA_WSTRING);
    n.rl_ctor = (void (*)(void *, void *, void *))(base + LCE195E_RVA_RL_CTOR);
    n.rl_dtor = (void (*)(void *))(base + LCE195E_RVA_RL_DTOR);
    n.effect_ctor = (void (*)(void *, int, int))(base + LCE195E_RVA_EFFECT_CTOR);
    n.effect_add_modifier = (uintptr_t(*)(void *, uintptr_t, int, int, double))(base + LCE195E_RVA_EFFECT_ADD_MOD);
    n.effect_by_id = (uintptr_t(*)(int))(base + LCE195E_RVA_EFFECT_BY_ID);
    n.mei_ctor = (void (*)(void *, uintptr_t, int, int))(base + LCE195E_RVA_MEI_CTOR);
    n.potion_by_id = (uintptr_t(*)(int))(base + LCE195E_RVA_POTION_BY_ID);
    n.ptr_vec_grow = (void (*)(uintptr_t, int))(base + LCE195E_RVA_PTR_VEC_GROW);
    n.reg_register = reg_register;
    n.reg_get = reg_get;
    e->thunk[0] = (uintptr_t)&lce195e_thunk_tick;
    e->thunk[1] = (uintptr_t)&lce195e_thunk_instant;
    e->thunk[2] = (uintptr_t)&lce195e_thunk_ready;
}
#endif

}  // extern "C"
