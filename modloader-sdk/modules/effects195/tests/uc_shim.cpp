// Test-only shim: lets tests/test_unicorn_retail.py drive the real effects195
// publishers (this .so) with natives that execute the retail image in Unicorn.
#include "../effects195.h"
#include <string.h>

static_assert(sizeof(lce195e_natives) == 13 * sizeof(void *), "natives layout");
static lce195e_env env;
static lce195e_effect_publisher ep;
static lce195e_potion_publisher pp;

extern "C" {
void shim_set_env(void *const fns[13], void *const mem[3], const uint64_t thunks[3]) {
    memset(&env, 0, sizeof env);
    env.base = 0;  // the reconstructed ELF is mapped at VA 0 in Unicorn
    void **n = (void **)&env.n;
    for (int i = 0; i < 13; i++) n[i] = fns[i];
    env.m.ctx = nullptr;
    env.m.read = (int (*)(void *, uintptr_t, void *, size_t))mem[0];
    env.m.write = (int (*)(void *, uintptr_t, const void *, size_t))mem[1];
    env.m.held = (int (*)(void *))mem[2];
    for (int i = 0; i < 3; i++) env.thunk[i] = thunks[i];
}

int shim_publish_effects(int count, const char *const *keys, const int32_t *ids, const uint32_t *sids, const int32_t *clones,
                         const uint32_t *colours, const uint32_t *icons, const uint8_t *ben, const uint32_t *intervals) {
    static lce195e_effect_spec s[LCE195E_MAX_EFFECTS];
    static void (*const cb)(void *, int32_t, uintptr_t, int32_t) = [](void *, int32_t, uintptr_t, int32_t) {};
    if (count < 0 || count > (int)LCE195E_MAX_EFFECTS) return -9;
    for (int i = 0; i < count; i++) {
        memset(&s[i], 0, sizeof s[i]);
        s[i].key = keys[i]; s[i].id = ids[i]; s[i].name_sid = sids[i]; s[i].clone_of = clones[i];
        s[i].colour = colours[i]; s[i].icon = icons[i]; s[i].beneficial = ben[i]; s[i].tick_interval = intervals[i];
        if (intervals[i]) s[i].on_tick = cb;
    }
    memset(&ep, 0, sizeof ep);
    ep.env = &env;
    return lce195e_effects_publish(&ep, s, (unsigned)count);
}

uint64_t shim_effect_record(int i, int field) {
    const lce195e_effect &r = ep.records[i];
    switch (field) {
    case 0: return r.object;
    case 1: return r.vtable_block;
    case 2: return r.stock;
    case 3: return r.modifiers;
    default: return ep.state;
    }
}

int shim_publish_potions(int count, const char *const *keys, const uint32_t *sids6, const uint64_t *effects8, const int32_t *dur8,
                         const int32_t *amp8, const uint32_t *counts) {
    static lce195e_potion_spec s[LCE195E_MAX_POTIONS];
    if (count < 0 || count > (int)LCE195E_MAX_POTIONS) return -9;
    for (int i = 0; i < count; i++) {
        memset(&s[i], 0, sizeof s[i]);
        s[i].key = keys[i];
        memcpy(s[i].sid, sids6 + 6 * i, 24);
        s[i].effect_count = counts[i];
        for (unsigned k = 0; k < counts[i] && k < LCE195E_MAX_POTION_EFFECTS; k++)
            s[i].effects[k] = {(uintptr_t)effects8[8 * i + k], dur8[8 * i + k], amp8[8 * i + k]};
    }
    memset(&pp, 0, sizeof pp);
    pp.env = &env;
    return lce195e_potions_publish(&pp, s, (unsigned)count);
}

uint64_t shim_potion_record(int i, int field) {
    const lce195e_potion &r = pp.records[i];
    if (field == 0) return (uint64_t)(int64_t)r.id;
    if (field == 1) return r.object;
    return r.mei[field - 2];
}
}
