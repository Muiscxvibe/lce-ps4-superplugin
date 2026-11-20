// Host tests for effects195 (ASan/UBSan). The "game" lives in real host memory:
// base is a zeroed buffer the size of the image, fake natives model the retail
// functions' observable effects (the Unicorn suite runs the real ones).
#include "../effects195.h"
#include <map>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

static int checks, fails;
#define CHECK(c, what) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, what); } } while (0)

static unsigned char *img;
static uintptr_t B;
static int held = 1, write_fail_after = -1, writes;
static int mem_read(void *, uintptr_t a, void *v, size_t n) { memcpy(v, (const void *)a, n); return 1; }
static int mem_write(void *, uintptr_t a, const void *v, size_t n) {
    if (write_fail_after >= 0 && writes++ >= write_fail_after) return 0;
    memcpy((void *)a, v, n); return 1;
}
static int mem_held(void *) { return held; }

static std::vector<void *> allocs;
static void *f_alloc(size_t n) { void *p = calloc(1, n + 16); allocs.push_back(p); return p; }
static int releases;
static void f_release(void *) { releases++; }
// wstring {+0 ?, +8 buf/ptr, +0x18 size, +0x20 cap}
static void f_wstring(void *dst, const uint16_t *u, uint64_t n) {
    unsigned char *d = (unsigned char *)dst;
    uint64_t cap = n > 7 ? n : 7;
    if (n > 7) { uint16_t *p = (uint16_t *)f_alloc(2 * n + 2); memcpy(p, u, 2 * n); uintptr_t pp = (uintptr_t)p; memcpy(d + 8, &pp, 8); }
    else { memcpy(d + 8, u, 2 * n); }
    memcpy(d + 0x18, &n, 8); memcpy(d + 0x20, &cap, 8);
}
static std::string ws_text(const unsigned char *w) {
    uint64_t n, cap; memcpy(&n, w + 0x18, 8); memcpy(&cap, w + 0x20, 8);
    const uint16_t *p; uintptr_t pp;
    if (cap > 7) { memcpy(&pp, w + 8, 8); p = (const uint16_t *)pp; } else p = (const uint16_t *)(w + 8);
    std::string s; for (uint64_t i = 0; i < n; i++) s += (char)p[i]; return s;
}
// fake ResourceLocation: text stored in the buffer
static int rl_live;
static void f_rl_ctor(void *rl, void *ns, void *path) { std::string k = ws_text((unsigned char *)ns) + ":" + ws_text((unsigned char *)path); snprintf((char *)rl, 0x80, "%s", k.c_str()); rl_live++; }
static void f_rl_dtor(void *) { rl_live--; }
// fake MobEffect ctor: vptr = base MobEffect vt, empty modifier list, defaults as 0x490db0
struct FakeNode { uintptr_t next, prev, attr, mod; };
static void f_effect_ctor(void *o, int harmful, int colour) {
    unsigned char *e = (unsigned char *)o;
    uintptr_t vt = B + LCE195E_RVA_EFFECT_VT; memcpy(e, &vt, 8);
    FakeNode *head = (FakeNode *)f_alloc(sizeof(FakeNode)); head->next = head->prev = (uintptr_t)head;
    uintptr_t h = (uintptr_t)head; memcpy(e + 0x10, &h, 8); uint64_t z = 0; memcpy(e + 0x18, &z, 8);
    uint64_t sid = 0xffffffffu; memcpy(e + 0x58, &sid, 8);
    e[0x60] = (unsigned char)harmful; double dm = harmful ? 0.5 : 1.0; memcpy(e + 0x68, &dm, 8); e[0x70] = 0; memcpy(e + 0x74, &colour, 4);
}
static int mod_calls;
static uintptr_t f_add_mod(void *o, uintptr_t attr, int id, int op, double amount) {
    unsigned char *e = (unsigned char *)o; mod_calls++;
    unsigned char *mod = (unsigned char *)f_alloc(0x40); memcpy(mod, &amount, 8); memcpy(mod + 8, &op, 4); memcpy(mod + 0x38, &id, 4);
    uintptr_t h; memcpy(&h, e + 0x10, 8); FakeNode *head = (FakeNode *)h;
    FakeNode *nn = (FakeNode *)f_alloc(sizeof(FakeNode)); nn->attr = attr; nn->mod = (uintptr_t)mod;
    nn->next = (uintptr_t)head; nn->prev = head->prev; ((FakeNode *)head->prev)->next = (uintptr_t)nn; head->prev = (uintptr_t)nn;
    uint64_t sz; memcpy(&sz, e + 0x18, 8); sz++; memcpy(e + 0x18, &sz, 8);
    return (uintptr_t)o;
}
// registries
static std::map<int, uintptr_t> eff_by_id, pot_by_id;
static std::map<std::string, uintptr_t> eff_by_key, pot_by_key;
static int drop_register;
static uintptr_t f_effect_by_id(int id) { auto it = eff_by_id.find(id); return it == eff_by_id.end() ? 0 : it->second; }
static uintptr_t f_potion_by_id(int id) { auto it = pot_by_id.find(id); return it == pot_by_id.end() ? pot_by_id[1] : it->second; }  // defaulted: water
static uintptr_t effect_reg_obj, potion_reg_obj;
static void f_register(uintptr_t reg, int id, void *rl, uintptr_t v) {
    if (drop_register) return;
    if (reg == effect_reg_obj) { eff_by_id[id] = v; eff_by_key[(char *)rl] = v; }
    else { pot_by_id[id] = v; pot_by_key[(char *)rl] = v; }
}
static uintptr_t f_get(uintptr_t reg, void *rl) {
    auto &m = reg == effect_reg_obj ? eff_by_key : pot_by_key;
    auto it = m.find((char *)rl);
    return it == m.end() ? (reg == potion_reg_obj ? pot_by_id[1] : 0) : it->second;
}
static void f_mei(void *m, uintptr_t eff, int dur, int amp) { unsigned char *p = (unsigned char *)m; memcpy(p, &eff, 8); memcpy(p + 8, &dur, 4); memcpy(p + 12, &amp, 4); int32_t m1 = -1; memcpy(p + 0x14, &m1, 4); }
static void f_vec_grow(uintptr_t vec, int n) {
    uintptr_t *h = (uintptr_t *)(vec + 8);
    size_t used = (h[1] - h[0]) / 8; uintptr_t *nb = (uintptr_t *)f_alloc(8 * (used + n));
    if (used) memcpy(nb, (void *)h[0], 8 * used);
    h[0] = (uintptr_t)nb; h[1] = h[0] + 8 * used; h[2] = h[0] + 8 * (used + n);
}

// stock classes: fake retail functions with recorded `this`
static uintptr_t seen_tick_this, seen_ready_this, seen_instant_this; static int seen_amp;
static void stock_tick(uintptr_t self, uintptr_t, int32_t amp) { seen_tick_this = self; seen_amp = amp; }
static void stock_instant(uintptr_t self, uintptr_t, uintptr_t, uintptr_t, int32_t, double) { seen_instant_this = self; }
static int stock_ready(uintptr_t self, int32_t dur, int32_t amp) { seen_ready_this = self; return (dur % (50 >> amp)) == 0; }
static void stock_other(void) {}
static uintptr_t stock_eff[32];
static uint64_t class_vt[32][2 + 15];

static void build_game(void) {
    img = (unsigned char *)calloc(1, 0x1500000); B = (uintptr_t)img;
    effect_reg_obj = (uintptr_t)f_alloc(0x150); potion_reg_obj = (uintptr_t)f_alloc(0x1c8);
    memcpy(img + LCE195E_RVA_EFFECT_REGISTRY, &effect_reg_obj, 8);
    memcpy(img + LCE195E_RVA_POTION_REGISTRY, &potion_reg_obj, 8);
    int32_t c = 31, pc = 43; memcpy(img + LCE195E_RVA_EFFECT_COUNT, &c, 4); memcpy(img + LCE195E_RVA_POTION_COUNT, &pc, 4);
    // MobEffect base vtable inside the fake image (RTTI prefix + 15 slots), AttackDamage vtable too
    uint64_t base_vt[17] = {0, 0x1111};
    for (int i = 0; i < 15; i++) base_vt[2 + i] = (uintptr_t)&stock_other;
    base_vt[2] = (uintptr_t)&stock_tick; base_vt[3] = (uintptr_t)&stock_instant; base_vt[4] = (uintptr_t)&stock_ready;
    memcpy(img + LCE195E_RVA_EFFECT_VT - 16, base_vt, sizeof base_vt);
    base_vt[1] = 0x2222; memcpy(img + LCE195E_RVA_ATTACK_DAMAGE_VT - 16, base_vt, sizeof base_vt);
    for (int id = 1; id <= 31; id++) {
        unsigned char *e = (unsigned char *)f_alloc(0x80);
        f_effect_ctor(e, id == 2, 0xfc + id);
        uintptr_t vt = (id == 5 || id == 18) ? B + LCE195E_RVA_ATTACK_DAMAGE_VT : B + LCE195E_RVA_EFFECT_VT;
        if (id == 6) {  // a distinct class (InstantenousMobEffect) to prove the clone's class vtable is copied
            memcpy(class_vt[id], base_vt, sizeof base_vt); class_vt[id][1] = 0x3333; vt = (uintptr_t)&class_vt[id][2];
        }
        memcpy(e, &vt, 8);
        uint32_t sid = 0x1000 + id; memcpy(e + 0x58, &sid, 4);
        double dm = 0.25; memcpy(e + 0x68, &dm, 8);
        if (id == 5) { double mult = 3.0; memcpy(e + 0x78, &mult, 8); }
        if (id == 1) { f_add_mod(e, 0xa77a, 10, 2, 0.2); }
        if (id == 26) { f_add_mod(e, 0xb77b, 14, 0, 1.0); f_add_mod(e, 0xc77c, 15, 0, 2.0); }
        stock_eff[id] = (uintptr_t)e; eff_by_id[id] = (uintptr_t)e;
    }
    for (int i = 0; i < 43; i++) pot_by_id[i] = (uintptr_t)f_alloc(0x38);
    mod_calls = 0;
}

static lce195e_env make_env(void) {
    lce195e_env e = {};
    e.base = B;
    e.n.alloc = f_alloc; e.n.release = f_release; e.n.wstring = f_wstring; e.n.rl_ctor = f_rl_ctor; e.n.rl_dtor = f_rl_dtor;
    e.n.effect_ctor = f_effect_ctor; e.n.effect_add_modifier = f_add_mod; e.n.effect_by_id = f_effect_by_id; e.n.mei_ctor = f_mei;
    e.n.potion_by_id = f_potion_by_id; e.n.ptr_vec_grow = f_vec_grow; e.n.reg_register = f_register; e.n.reg_get = f_get;
    e.m = {nullptr, mem_read, mem_write, mem_held};
    e.thunk[0] = (uintptr_t)&lce195e_thunk_tick; e.thunk[1] = (uintptr_t)&lce195e_thunk_instant; e.thunk[2] = (uintptr_t)&lce195e_thunk_ready;
    return e;
}

static int cb_calls, cb_id, cb_amp; static uintptr_t cb_entity; static void *cb_user;
static void on_tick(void *user, int32_t id, uintptr_t entity, int32_t amp) { cb_calls++; cb_id = id; cb_entity = entity; cb_amp = amp; cb_user = user; }

static lce195e_effect_spec spec(const char *key, int id, int clone) {
    lce195e_effect_spec s = {}; s.key = key; s.id = id; s.name_sid = 0x4c430000u + (uint32_t)id; s.clone_of = clone; s.icon = UINT32_MAX; s.beneficial = 1; return s;
}
static uint32_t u32(uintptr_t a) { uint32_t v; memcpy(&v, (void *)a, 4); return v; }
static uintptr_t uq(uintptr_t a) { uintptr_t v; memcpy(&v, (void *)a, 8); return v; }
static double ud(uintptr_t a) { double v; memcpy(&v, (void *)a, 8); return v; }

static void reset_all(void) {
    lce195e_reset_for_tests(); eff_by_id.clear(); eff_by_key.clear(); pot_by_id.clear(); pot_by_key.clear();
    held = 1; write_fail_after = -1; writes = 0; drop_register = 0; rl_live = 0; free(img); build_game();
}

static void test_tables(void) {
    CHECK(lce195e_stock_effect_id("minecraft:speed") == 1 && lce195e_stock_effect_id("minecraft:village_hero") == 31, "stock effect ids");
    CHECK(lce195e_stock_effect_id("speed") == -1 && lce195e_stock_effect_id("minecraft:nope") == -1, "stock effect id misses");
    CHECK(lce195e_stock_potion_id("minecraft:awkward") == 4 && lce195e_stock_potion_id("minecraft:long_slow_falling") == 42, "stock potion ids");
    CHECK(lce195e_clone_kind(10) == LCE195E_CLONE_FORWARD && lce195e_clone_kind(1) == LCE195E_CLONE_ATTRIBUTE && lce195e_clone_kind(16) == LCE195E_CLONE_IDENTITY, "clone kinds");
    CHECK(lce195e_clone_kind(0) == LCE195E_CLONE_NONE && lce195e_clone_kind(32) == LCE195E_CLONE_NONE, "clone kind range");
    CHECK(lce195e_stock_icon(1) == 14 && lce195e_stock_icon(6) == 0 && lce195e_stock_icon(31) == 29, "icons");
    CHECK(lce195e_stock_colour(1) == 0xfd && lce195e_stock_colour(28) == 0x11a && lce195e_stock_colour(29) == 0x119, "colour enums");
    CHECK(lce195e_nearest_colour(0x7cafc6) == 0xfd, "nearest exact speed");
    CHECK(lce195e_nearest_colour(0x22fe4d) == 0x104, "nearest jump green");
    CHECK(lce195e_nearest_colour(0xcd5caa) == 0x106, "nearest regen pink");
}

static void test_desc(void) {
    char err[400];
    lce_effect_desc d = {}; d.struct_size = sizeof d; d.key = "gem:glow"; d.name = "Glow"; d.color_rgb = 0x7cafc6; d.beneficial = 1; d.clone_of = "minecraft:regeneration";
    lce195e_effect_spec s;
    CHECK(lce195e_effect_spec_from_desc(&d, 32, 0x4c430001, &s, err, sizeof err) == LCE_OK, "desc ok");
    CHECK(s.clone_of == 10 && s.colour == 0xfd && s.icon == UINT32_MAX && s.beneficial == 1 && s.id == 32, "desc mapped");
    d.icon_texture = "minecraft:speed";
    CHECK(lce195e_effect_spec_from_desc(&d, 32, 1, &s, err, sizeof err) == LCE_OK && s.icon == 14, "stock icon frame reuse");
    d.icon_texture = "gem:textures/glow.png";
    CHECK(lce195e_effect_check(&d, err, sizeof err) == LCE_E_UNSUPPORTED && strstr(err, "fourj.MobEffect"), "custom icon texture refused with reason");
    d.icon_texture = nullptr; d.clone_of = "minecraft:night_vision";
    CHECK(lce195e_effect_check(&d, err, sizeof err) == LCE_E_UNSUPPORTED && strstr(err, "identity"), "identity clone refused");
    d.clone_of = "minecraft:speed"; d.instant = 1;
    CHECK(lce195e_effect_check(&d, err, sizeof err) == LCE_E_UNSUPPORTED, "instant needs an instant clone");
    d.clone_of = "minecraft:instant_health";
    CHECK(lce195e_effect_check(&d, err, sizeof err) == LCE_OK, "instant clone ok");
    d.clone_of = nullptr; d.instant = 0;
    CHECK(lce195e_effect_check(&d, err, sizeof err) == LCE_OK, "no clone ok (script effect)");
    d.key = "minecraft:mine";
    CHECK(lce195e_effect_check(&d, err, sizeof err) == LCE_E_INVALID, "minecraft: key refused");
    d.key = "Gem:Bad";
    CHECK(lce195e_effect_check(&d, err, sizeof err) == LCE_E_INVALID, "uppercase key refused");
    d.key = "gem:glow";
    CHECK(lce195e_effect_spec_from_desc(&d, 31, 1, &s, err, sizeof err) == LCE_E_INVALID, "id 31 refused");
    CHECK(lce195e_effect_spec_from_desc(&d, 128, 1, &s, err, sizeof err) == LCE_E_INVALID, "id 128 refused");
    CHECK(lce195e_effect_spec_from_desc(&d, 40, 0, &s, err, sizeof err) == LCE_E_INVALID, "sid 0 refused");

    lce_effect_ref refs[2] = {{"minecraft:speed", 3600, 1, 0, {0, 0}}, {"gem:glow", 200, 0, 0, {0, 0}}};
    lce_potion_desc pd = {}; pd.struct_size = sizeof pd; pd.key = "gem:glow_potion"; pd.effects = refs; pd.effect_count = 2;
    CHECK(lce195e_potion_check(&pd, err, sizeof err) == LCE_OK, "potion desc ok");
    refs[0].effect = "minecraft:bogus";
    CHECK(lce195e_potion_check(&pd, err, sizeof err) == LCE_E_INVALID, "bogus stock effect refused");
    refs[0].effect = "minecraft:speed"; refs[0].duration_ticks = 0;
    CHECK(lce195e_potion_check(&pd, err, sizeof err) == LCE_E_INVALID, "zero duration refused");
    refs[0].duration_ticks = 3600; pd.effect_count = 9;
    CHECK(lce195e_potion_check(&pd, err, sizeof err) == LCE_E_INVALID, "9 effects refused");
}

static void test_effects(void) {
    reset_all();
    lce195e_env e = make_env();
    static lce195e_effect_publisher p;
    lce195e_effect_spec s[4] = {spec("gem:regen2", 32, 10), spec("gem:fast", 33, 1), spec("gem:strong", 34, 5), spec("gem:mark", 40, 0)};
    s[3].on_tick = on_tick; s[3].user = &p; s[3].tick_interval = 20; s[3].colour = 0x104; s[3].icon = 6; s[3].beneficial = 0;
    s[0].on_tick = on_tick; s[0].user = &e;
    memset(&p, 0, sizeof p); p.env = &e;
    int rc = lce195e_effects_publish(&p, s, 4);
    CHECK(rc == 1 && p.state == 2 && p.published == 4, "publish 4 effects");
    CHECK(u32(B + LCE195E_RVA_EFFECT_COUNT) == 40, "count global covers the highest id");
    CHECK(rl_live == 0, "every ResourceLocation destroyed");
    for (int i = 0; i < 4; i++) {
        uintptr_t o = p.records[i].object;
        CHECK(o && f_effect_by_id(s[i].id) == o && eff_by_key[s[i].key] == o, "registered by id and key");
        CHECK(u32(o + 0x58) == s[i].name_sid, "name sid");
        CHECK(uq(o) == p.records[i].vtable_block + 16, "private vptr");
        CHECK(uq(p.records[i].vtable_block + 16) == e.thunk[0] && uq(p.records[i].vtable_block + 24) == e.thunk[1] && uq(p.records[i].vtable_block + 32) == e.thunk[2], "slots 0/1/2 -> thunks");
        CHECK(uq(p.records[i].vtable_block + 16 + 8 * 7) == (uintptr_t)&stock_other, "other slots copied");
    }
    uintptr_t r0 = p.records[0].object, r1 = p.records[1].object, r2 = p.records[2].object, r3 = p.records[3].object;
    CHECK(u32(r0 + 0x5c) == 11 && u32(r0 + 0x74) == 0xfd + 9 - 0 && ud(r0 + 0x68) == 0.25, "regen clone: icon/colour/duration modifier");
    CHECK(((unsigned char *)r0)[0x60] == 0 && ((unsigned char *)r0)[0x70] == 1, "beneficial flags");
    CHECK(((unsigned char *)r3)[0x60] == 1 && ((unsigned char *)r3)[0x70] == 0 && u32(r3 + 0x5c) == 6 && u32(r3 + 0x74) == 0x104, "custom effect flags/icon/colour");
    CHECK(uq(r1 + 0x18) == 1 && p.records[1].modifiers == 1, "speed modifier copied");
    {
        uintptr_t head = uq(r1 + 0x10), node = uq(head), mod = uq(node + 0x18);
        CHECK(uq(node + 0x10) == 0xa77a && ud(mod) == 0.2 && u32(mod + 8) == 2 && u32(mod + 0x38) == 10, "modifier attr/amount/op/id");
    }
    CHECK(ud(r2 + 0x78) == 3.0 && uq(p.records[2].vtable_block + 8) == 0x2222, "attack-damage clone: multiplier + class vtable RTTI");
    CHECK(uq(p.records[3].vtable_block + 8) == 0x1111 && p.records[3].stock == 0, "no-clone effect uses the MobEffect class");
    CHECK(lce195e_effect_object("gem:fast") == r1 && lce195e_effect_id("gem:mark") == 40 && lce195e_effect_id("minecraft:wither") == 20 && !lce195e_effect_object("gem:none"), "lookup");

    // thunks
    uintptr_t ent = 0xe171, sp[2] = {ent, 0};
    cb_calls = 0; seen_tick_this = 0;
    lce195e_thunk_tick(r0, (uintptr_t)sp, 1);
    CHECK(seen_tick_this == stock_eff[10] && seen_amp == 1, "tick forwarded with this = stock regeneration");
    CHECK(cb_calls == 1 && cb_id == 32 && cb_entity == ent && cb_amp == 1 && cb_user == &e, "on_tick callback with entity");
    seen_ready_this = 0;
    CHECK(lce195e_thunk_ready(r0, 50, 0) == 1 && seen_ready_this == stock_eff[10], "ready forwarded");
    CHECK(lce195e_thunk_ready(r0, 49, 0) == 0, "ready forwarded false");
    CHECK(lce195e_thunk_ready(r3, 40, 0) == 1 && lce195e_thunk_ready(r3, 41, 0) == 0 && lce195e_thunk_ready(r3, 0, 0) == 0, "custom interval 20");
    seen_tick_this = 0; cb_calls = 0;
    lce195e_thunk_tick(r3, (uintptr_t)sp, 0);
    CHECK(seen_tick_this == 0 && cb_calls == 1 && cb_id == 40, "custom effect tick: callback only");
    seen_instant_this = 0;
    lce195e_thunk_instant(r0, 1, 2, (uintptr_t)sp, 0, 1.0);
    CHECK(seen_instant_this == stock_eff[10], "instant forwarded");
    seen_tick_this = 0;
    lce195e_thunk_tick(stock_eff[10], (uintptr_t)sp, 0);
    CHECK(seen_tick_this == 0, "thunk ignores unknown this");
    uintptr_t so = 0;
    CHECK(lce195e_forward_target(r0, 2, &so) == (uintptr_t)&stock_ready && so == stock_eff[10], "forward target");
    CHECK(lce195e_forward_target(r3, 0, &so) == 0 && so == 0, "no forward for custom");
    CHECK(lce195e_forward_target(r0, 3, &so) == 0, "slot 3 not forwarded");

    // second publish refused
    static lce195e_effect_publisher p2; memset(&p2, 0, sizeof p2); p2.env = &e;
    lce195e_effect_spec s2 = spec("gem:late", 50, 0);
    CHECK(lce195e_effects_publish(&p2, &s2, 1) == 0, "one batch per process");
    CHECK(lce195e_effects_publish(&p, s, 4) == 0, "publisher not reusable");
}

static int refuse(lce195e_effect_spec *s, unsigned n, const char *what, void (*setup)(lce195e_env *) = nullptr) {
    reset_all();
    lce195e_env e = make_env();
    if (setup) setup(&e);
    static lce195e_effect_publisher p; memset(&p, 0, sizeof p); p.env = &e;
    size_t before = allocs.size();
    uintptr_t id32 = f_effect_by_id(32); uint32_t cnt = u32(B + LCE195E_RVA_EFFECT_COUNT);
    int rc = lce195e_effects_publish(&p, s, n);
    CHECK(rc == 0, what);
    CHECK(allocs.size() == before && f_effect_by_id(32) == id32 && u32(B + LCE195E_RVA_EFFECT_COUNT) == cnt, "refusal wrote/allocated nothing");
    return rc;
}

static void test_effect_refusals(void) {
    lce195e_effect_spec a[2] = {spec("gem:a", 33, 0), spec("gem:b", 32, 0)};
    refuse(a, 2, "ids must ascend");
    lce195e_effect_spec b[1] = {spec("gem:a", 31, 0)}; refuse(b, 1, "id 31 refused");
    lce195e_effect_spec c[1] = {spec("gem:a", 128, 0)}; refuse(c, 1, "id 128 refused");
    lce195e_effect_spec d[1] = {spec("gem:a", 32, 16)}; refuse(d, 1, "identity clone refused at publish");
    lce195e_effect_spec f[2] = {spec("gem:a", 32, 0), spec("gem:a", 33, 0)}; refuse(f, 2, "duplicate key");
    lce195e_effect_spec g[1] = {spec("gem:a", 32, 0)}; g[0].name_sid = 0; refuse(g, 1, "sid 0");
    lce195e_effect_spec h[1] = {spec("gem:a", 32, 0)}; h[0].icon = 30; refuse(h, 1, "icon frame > 29");
    lce195e_effect_spec i[1] = {spec("gem:a", 32, 0)}; i[0].colour = 0x50; refuse(i, 1, "colour not an effect enum");
    lce195e_effect_spec j[1] = {spec("gem:a", 32, 0)}; j[0].tick_interval = 5; refuse(j, 1, "interval without callback");
    refuse(g, 0, "count 0");
    lce195e_effect_spec k[1] = {spec("gem:a", 32, 0)};
    refuse(k, 1, "id already used", [](lce195e_env *) { eff_by_id[32] = 0x1234; });
    refuse(k, 1, "not held", [](lce195e_env *) { held = 0; });
    refuse(k, 1, "no thunks", [](lce195e_env *e) { e->thunk[1] = 0; });
    refuse(k, 1, "counter moved (another owner)", [](lce195e_env *) { int32_t c = 35; memcpy(img + LCE195E_RVA_EFFECT_COUNT, &c, 4); });
    refuse(k, 1, "registry missing", [](lce195e_env *) { uintptr_t z = 0; memcpy(img + LCE195E_RVA_EFFECT_REGISTRY, &z, 8); });
}

static void test_effect_uncertain(void) {
    reset_all();
    lce195e_env e = make_env();
    static lce195e_effect_publisher p; memset(&p, 0, sizeof p); p.env = &e;
    lce195e_effect_spec s[1] = {spec("gem:a", 32, 10)};
    drop_register = 1;
    CHECK(lce195e_effects_publish(&p, s, 1) == -1 && p.state == 4, "register not taking -> uncertain");
    CHECK(lce195e_effect_object("gem:a") == 0, "uncertain batch is not visible to thunks/lookups");
    CHECK(lce195e_effects_publish(&p, s, 1) == 0, "never retried");
    reset_all();
    e = make_env(); memset(&p, 0, sizeof p); p.env = &e;
    write_fail_after = 2;
    CHECK(lce195e_effects_publish(&p, s, 1) == -1, "write failure mid-construction -> uncertain");
}

static void test_potions(void) {
    reset_all();
    lce195e_env e = make_env();
    static lce195e_effect_publisher ep; memset(&ep, 0, sizeof ep); ep.env = &e;
    lce195e_effect_spec es[1] = {spec("gem:glow", 32, 10)};
    CHECK(lce195e_effects_publish(&ep, es, 1) == 1, "effect for potions");
    lce_effect_ref refs[2] = {{"minecraft:speed", 3600, 1, 0, {0, 0}}, {"gem:glow", 200, 2, 0, {0, 0}}};
    lce_potion_desc pd = {}; pd.struct_size = sizeof pd; pd.key = "gem:glow_potion"; pd.effects = refs; pd.effect_count = 2;
    const uint32_t sids[6] = {0x4c430100, 0x4c430101, 0x4c430102, 0x4c430103, 0, 0};
    lce195e_potion_spec ps[2]; char err[300];
    CHECK(lce195e_potion_spec_from_desc(&e, &pd, sids, &ps[0], err, sizeof err) == LCE_OK, "potion spec");
    CHECK(ps[0].sid[4] == 0x4c430100 && ps[0].sid[5] == 0x2453270cu, "sid defaults");
    CHECK(ps[0].effects[0].effect == stock_eff[1] && ps[0].effects[1].effect == ep.records[0].object && ps[0].effects[1].amplifier == 2, "effects resolved");
    lce_potion_desc pd2 = pd; pd2.key = "gem:base"; pd2.effect_count = 0;
    CHECK(lce195e_potion_spec_from_desc(&e, &pd2, sids, &ps[1], err, sizeof err) == LCE_OK, "base potion spec");
    refs[1].effect = "gem:unknown";
    lce195e_potion_spec bad;
    CHECK(lce195e_potion_spec_from_desc(&e, &pd, sids, &bad, err, sizeof err) == LCE_E_UNSUPPORTED, "unregistered mod effect refused");
    const uint32_t nosid[6] = {1, 0, 1, 1, 0, 0};
    CHECK(lce195e_potion_spec_from_desc(&e, &pd2, nosid, &bad, err, sizeof err) == LCE_E_INVALID, "missing name sid refused");

    static lce195e_potion_publisher pp; memset(&pp, 0, sizeof pp); pp.env = &e;
    CHECK(lce195e_potions_publish(&pp, ps, 2) == 1 && pp.state == 2, "publish 2 potions");
    CHECK(pp.records[0].id == 43 && pp.records[1].id == 44 && u32(B + LCE195E_RVA_POTION_COUNT) == 45, "ids follow the counter");
    uintptr_t o = pp.records[0].object;
    CHECK(f_potion_by_id(43) == o && pot_by_key["gem:glow_potion"] == o, "registered by id and key");
    CHECK(u32(o) == 0x4c430100 && u32(o + 4) == 0x4c430101 && u32(o + 8) == 0x4c430102 && u32(o + 0xc) == 0x4c430103 && u32(o + 0x14) == 0x2453270cu, "six sids");
    uintptr_t b0 = uq(o + 0x20), e0 = uq(o + 0x28);
    CHECK(e0 - b0 == 16 && uq(b0) == pp.records[0].mei[0] && uq(b0 + 8) == pp.records[0].mei[1], "MEI vector");
    CHECK(uq(pp.records[0].mei[1]) == ep.records[0].object && u32(pp.records[0].mei[1] + 8) == 200 && u32(pp.records[0].mei[1] + 12) == 2, "MEI fields");
    CHECK(uq(pp.records[1].object + 0x20) == 0 && uq(pp.records[1].object + 0x28) == 0, "base potion has no effects");
    CHECK(lce195e_potion_id("gem:glow_potion") == 43 && lce195e_potion_id("gem:base") == 44 && lce195e_potion_id("minecraft:awkward") == 4 && lce195e_potion_id("gem:x") == -1, "brew resolver");
    CHECK(rl_live == 0, "potion RLs destroyed");

    // refusals
    reset_all(); e = make_env();
    memset(&pp, 0, sizeof pp); pp.env = &e;
    lce195e_potion_spec dup[2] = {ps[1], ps[1]};
    size_t before = allocs.size();
    CHECK(lce195e_potions_publish(&pp, dup, 2) == 0 && allocs.size() == before, "duplicate potion key refused before allocation");
    memset(&pp, 0, sizeof pp); pp.env = &e;
    pot_by_key["gem:base"] = 0x9999;
    CHECK(lce195e_potions_publish(&pp, &ps[1], 1) == 0, "existing registry key refused");
    pot_by_key.erase("gem:base");
    memset(&pp, 0, sizeof pp); pp.env = &e;
    int32_t early = 20; memcpy(img + LCE195E_RVA_POTION_COUNT, &early, 4);
    CHECK(lce195e_potions_publish(&pp, &ps[1], 1) == 0, "before the potion initializer: refused");
    int32_t ok = 43; memcpy(img + LCE195E_RVA_POTION_COUNT, &ok, 4);
    memset(&pp, 0, sizeof pp); pp.env = &e; held = 0;
    CHECK(lce195e_potions_publish(&pp, &ps[1], 1) == 0, "not held");
    held = 1; memset(&pp, 0, sizeof pp); pp.env = &e; drop_register = 1;
    CHECK(lce195e_potions_publish(&pp, &ps[1], 1) == -1 && pp.state == 4, "register not taking -> uncertain");
    CHECK(lce195e_potion_id("gem:base") == -1, "uncertain potions invisible to the brew resolver");
}

int main(void) {
    build_game();
    test_tables();
    test_desc();
    test_effects();
    test_effect_refusals();
    test_effect_uncertain();
    test_potions();
    printf("test_effects195: %d checks, %d failures\n", checks, fails);
    for (void *p : allocs) free(p);
    free(img);
    return fails ? 1 : 0;
}
