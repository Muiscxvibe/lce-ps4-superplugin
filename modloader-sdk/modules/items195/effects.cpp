// Effects, potions, brewing, enchantments and fuel on 1.95. What is
// implemented: stock effect/potion id resolution (used by food effects and
// brewing) and a data-only brewing-mix publisher. New effects, potion types
// and enchantments are refused with the exact blocker (STATUS.md).
#include "items195.h"
#include <stdio.h>
#include <string.h>

namespace {
void set_err(char *err, uint32_t cap, const char *m) { if (err && cap) snprintf(err, cap, "%s", m); }
bool held(lce195x_env *e) { return e->m.held(e->m.ctx) == 1; }
bool rd(lce195x_env *e, uintptr_t a, void *v, size_t n) { return a && a <= UINTPTR_MAX - n && held(e) && e->m.read(e->m.ctx, a, v, n) == 1; }
bool wr(lce195x_env *e, uintptr_t a, const void *v, size_t n) { return a && a <= UINTPTR_MAX - n && held(e) && e->m.write(e->m.ctx, a, v, n) == 1; }

// Registration order of the effects initializer 0x4781e0 (next-id counter 0x14195d0 starts at 1).
const char *const kEffects[] = {"speed", "slowness", "haste", "mining_fatigue", "strength", "instant_health", "instant_damage", "jump_boost",
                                "nausea", "regeneration", "resistance", "fire_resistance", "water_breathing", "invisibility", "blindness",
                                "night_vision", "hunger", "weakness", "poison", "wither", "health_boost", "absorption", "saturation", "glowing",
                                "levitation", "luck", "unluck", "conduit_power", "slow_falling", "bad_omen", "village_hero"};
// Registration order of the potions initializer 0x55e6d0 (counter 0x142e940 starts at 0).
const char *const kPotions[] = {"empty", "water", "mundane", "thick", "awkward", "night_vision", "long_night_vision", "invisibility",
                                "long_invisibility", "leaping", "long_leaping", "strong_leaping", "fire_resistance", "long_fire_resistance",
                                "swiftness", "long_swiftness", "strong_swiftness", "slowness", "long_slowness", "water_breathing",
                                "long_water_breathing", "healing", "strong_healing", "harming", "strong_harming", "poison", "long_poison",
                                "strong_poison", "regeneration", "long_regeneration", "strong_regeneration", "strength", "long_strength",
                                "strong_strength", "weakness", "long_weakness", "luck", "turtle_master", "long_turtle_master",
                                "strong_turtle_master", "wither", "slow_falling", "long_slow_falling"};

int32_t lookup(const char *key, const char *const *names, unsigned n, int32_t first) {
    if (!key || strncmp(key, "minecraft:", 10)) return -1;
    for (unsigned i = 0; i < n; i++) if (!strcmp(key + 10, names[i])) return first + (int32_t)i;
    return -1;
}
}  // namespace

extern "C" int32_t lce195x_stock_effect_id(const char *key) { return lookup(key, kEffects, sizeof kEffects / sizeof *kEffects, 1); }
extern "C" int32_t lce195x_stock_potion_id(const char *key) { return lookup(key, kPotions, sizeof kPotions / sizeof *kPotions, 0); }

extern "C" lce_status lce195x_effect_check(const lce_effect_desc *d, char *err, uint32_t cap) {
    (void)d;
    set_err(err, cap, "items195: new status effects are refused: the registry (0x14195c8) would accept one, but its name needs a string-table entry "
                      "(lookup 0xaac9b0 -> 0xd39880), its icon must be a stock frame 1..29, and clones of regeneration/poison/wither/hunger/instant "
                      "effects do nothing because applyEffectTick 0x490ed0 compares `this` with the stock pointers");
    return LCE_E_UNSUPPORTED;
}
extern "C" lce_status lce195x_potion_check(const lce_potion_desc *d, char *err, uint32_t cap) {
    (void)d;
    set_err(err, cap, "items195: new potion types are refused: registry 0x142e938 accepts them, but names are u32 string ids (PotionItem name 0x565e10) "
                      "and the Creative list 0xa25790 only iterates the static Potions pointers, so the potion would be unnamed and unobtainable");
    return LCE_E_UNSUPPORTED;
}
extern "C" lce_status lce195x_enchant_check(const lce_enchant_desc *d, char *err, uint32_t cap) {
    (void)d;
    set_err(err, cap, "items195: new enchantments are refused: a stock-subclass clone registered in 0x1415448 after 0x248fa0 would be listed, but it "
                      "needs a string-table name (getFullname 0x24aec0 -> 0xaac9b0) and shares the cloned enchantment's behaviour; anvil code is unaudited");
    return LCE_E_UNSUPPORTED;
}
// Mod potions (effects195): resolved by key once they are published; NULL = stock potions only.
static int32_t (*g_mod_potion)(const char *key);
extern "C" void lce195x_set_mod_potion_resolver(int32_t (*fn)(const char *key)) { g_mod_potion = fn; }
static int32_t brew_potion(const char *key, bool at_check) {
    int32_t id = lce195x_stock_potion_id(key);
    if (id >= 0 || !g_mod_potion || !key || !strncmp(key, "minecraft:", 10) || !strchr(key, ':')) return id;
    if (at_check) return 1000;   // a mod potion key: the loader validated the reference; its id exists after the potions phase
    return g_mod_potion(key);
}
extern "C" lce_status lce195x_brew_check(const lce_brew_desc *d, char *err, uint32_t cap) {
    if (!d || !d->reagent) { set_err(err, cap, "items195: brewing mix without a reagent"); return LCE_E_INVALID; }
    int32_t from = brew_potion(d->input_potion, true), to = brew_potion(d->output_potion, true);
    const bool same = d->input_potion && d->output_potion && !strcmp(d->input_potion, d->output_potion);
    if (from < 0 || to < 0) { set_err(err, cap, g_mod_potion ? "items195: brewing potion unknown (not a stock 1.95 potion or a mod potion key)" : "items195: brewing mixes can only connect stock potions in this build (effects195 not bound)"); return LCE_E_UNSUPPORTED; }
    if (same || (from == to && from < 1000) || from == 0 || to == 0) { set_err(err, cap, "items195: brewing mix from/to must differ and not be minecraft:empty"); return LCE_E_INVALID; }
    return LCE_OK;
}

extern "C" int32_t lce195x_brew_potion_id(const char *key) { return brew_potion(key, false); }

extern "C" lce_status lce195x_check_ext(uint32_t type, const void *desc, char *err, uint32_t cap) {
    switch (type) {
    case LCE_EXT_EFFECT: return lce195x_effect_check((const lce_effect_desc *)desc, err, cap);
    case LCE_EXT_POTION: return lce195x_potion_check((const lce_potion_desc *)desc, err, cap);
    case LCE_EXT_BREW: return lce195x_brew_check((const lce_brew_desc *)desc, err, cap);
    case LCE_EXT_ENCHANT: return lce195x_enchant_check((const lce_enchant_desc *)desc, err, cap);
    case LCE_EXT_FUEL: {
        const lce_fuel_desc *f = (const lce_fuel_desc *)desc;
        return lce195x_fuel_check(nullptr, f ? f->burn_ticks : 1, err, cap);
    }
    default: set_err(err, cap, "items195: content type not handled by this module"); return LCE_E_UNSUPPORTED;
    }
}

extern "C" uint32_t lce195x_capabilities(void) { return LCE_CAP_TOOLS | LCE_CAP_ARMOUR | LCE_CAP_FOOD | LCE_CAP_BREWING | LCE_CAP_SMELTING | LCE_CAP_RECIPES; }

// Mix {Potion* from, ItemInstancePredicate* ingredient, Potion* to} (0x18),
// predicate {vt 0x12a62a8, Item*, int aux} (0x18): exactly what 0x562e30
// builds (0x562e5c..0x562e84, 0x563402..0x563430) and appends to 0x142e948.
extern "C" int lce195x_brew_publish(lce195x_brew_publisher *p, const lce195x_brew *defs, unsigned count) {
    if (!p || p->state || !p->env || !defs || !count || count > LCE195X_SMELT_MAX) return 0;
    lce195x_env *e = p->env;
    const lce195x_natives &n = e->n;
    if (!e->base || !e->m.held || !e->m.read || !e->m.write || !n.alloc || !n.potion_by_id || !n.mix_vec_grow) return 0;
    p->state = 3;
    const uintptr_t vec = e->base + LCE195X_RVA_POTION_MIXES;
    uintptr_t h[4];
    if (!held(e) || !rd(e, vec, h, 32) || h[1] > h[2] || h[2] > h[3] || ((h[1] | h[2] | h[3]) & 7) || !h[1] || (h[2] - h[1]) / 8 > 1024) return 0;
    p->stock_count = (h[2] - h[1]) / 8;
    if (p->stock_count && !rd(e, h[1], p->stock, p->stock_count * 8)) return 0;
    uintptr_t from[LCE195X_SMELT_MAX], to[LCE195X_SMELT_MAX];
    for (unsigned i = 0; i < count; i++) {
        const lce195x_brew &d = defs[i];
        if (d.from_potion <= 0 || d.to_potion <= 0 || d.from_potion == d.to_potion || !d.reagent || !(d.reagent_aux == -1 || (d.reagent_aux >= 0 && d.reagent_aux <= 32767))) return 0;
        from[i] = n.potion_by_id(d.from_potion); to[i] = n.potion_by_id(d.to_potion);
        if (!from[i] || !to[i]) return 0;
        // the first matching mix wins: refuse a (from, reagent) pair that already exists (stock or batch)
        for (uint64_t j = 0; j < p->stock_count; j++) {
            uintptr_t mix[3] = {}, item = 0;
            if (!rd(e, p->stock[j], mix, 24) || !mix[1] || !rd(e, mix[1] + 8, &item, 8)) return 0;
            if (mix[0] == from[i] && item == d.reagent) return 0;
        }
        for (unsigned j = 0; j < i; j++) if (from[j] == from[i] && defs[j].reagent == d.reagent) return 0;
    }
    p->count = count; p->state = 1;
    for (unsigned i = 0; i < count; i++) {
        const lce195x_brew &d = defs[i];
        uintptr_t pred = (uintptr_t)n.alloc(0x18), mix = (uintptr_t)n.alloc(0x18);
        p->predicates[i] = pred; p->mixes[i] = mix;
        if (!pred || !mix) goto uncertain;
        {
            uintptr_t pv[3] = {e->base + LCE195X_RVA_MIX_PREDICATE_VT, d.reagent, (uint32_t)d.reagent_aux};
            uintptr_t mv[3] = {from[i], pred, to[i]};
            if (!wr(e, pred, pv, 20) || !wr(e, mix, mv, 24)) goto uncertain;
            if (!rd(e, vec, h, 32)) goto uncertain;
            if (h[2] == h[3]) { n.mix_vec_grow(vec, 1); if (!rd(e, vec, h, 32) || h[2] == h[3]) goto uncertain; }
            uintptr_t end = h[2] + 8;
            if (!wr(e, h[2], &mix, 8) || !wr(e, vec + 16, &end, 8)) goto uncertain;
        }
        p->published = i + 1;
    }
    if (!rd(e, vec, h, 32) || (h[2] - h[1]) / 8 != p->stock_count + count) goto uncertain;
    for (uint64_t i = 0; i < p->stock_count + count; i++) {
        uintptr_t v = 0;
        if (!rd(e, h[1] + 8 * i, &v, 8) || v != (i < p->stock_count ? p->stock[i] : p->mixes[i - p->stock_count])) goto uncertain;
    }
    p->state = 2;
    return 1;
uncertain:
    p->state = 4;
    return -1;
}
