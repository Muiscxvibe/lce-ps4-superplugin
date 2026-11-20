// Fake 1.95 effect/potion "game" for the composite end-to-end test: the same models of the retail
// functions as modules/effects195/tests/test_effects195.cpp (the Unicorn suite there runs the real ones).
#pragma once
#include "../modules/effects195/effects195.h"
#include <map>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
namespace fx {
static unsigned char *img;
static uintptr_t B;
static int held = 1, write_fail_after = -1, writes;
static int mem_read(void *, uintptr_t a, void *v, size_t n) { memcpy(v, (const void *)a, n); return 1; }
static int mem_write(void *, uintptr_t a, const void *v, size_t n) {
    if (write_fail_after >= 0 && writes++ >= write_fail_after) return 0;
    memcpy((void *)a, v, n); return 1;
}
static int mem_held(void *) { return held; }

static std::vector<void *> &allocs = *new std::vector<void *>;   // never destroyed: the fake game lives until exit
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


static void reset_effects_game(void) {
    lce195e_reset_for_tests(); eff_by_id.clear(); eff_by_key.clear(); pot_by_id.clear(); pot_by_key.clear();
    held = 1; write_fail_after = -1; writes = 0; drop_register = 0; rl_live = 0; free(img); img = nullptr; build_game();
}
}  // namespace fx
