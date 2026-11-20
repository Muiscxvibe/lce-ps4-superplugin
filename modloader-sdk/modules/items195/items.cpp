// Item kinds for 1.95: generic, pickaxe/axe/shovel/hoe/sword (per-item Tier),
// armour (private ArmorMaterial, stock render index), food (FoodItem).
// Every native call/offset is cited in items195.h and STATUS.md.
#include "items195.h"
#include <stdio.h>
#include <string.h>

namespace {

lce195x_env *g_env;                              // process lifetime, first constructing env
lce195x_item *g_items[LCE195X_MAX_ITEMS];
unsigned g_item_count;
uintptr_t g_throw_use;                           // API 1.9: projectiles195 p195_item_use, 0 = no throwables

void set_err(char *err, uint32_t cap, const char *m) { if (err && cap) snprintf(err, cap, "%s", m); }
bool held(lce195x_env *e) { return e->m.held(e->m.ctx) == 1; }
bool rd(lce195x_env *e, uintptr_t a, void *v, size_t n) { return a && a <= UINTPTR_MAX - n && held(e) && e->m.read(e->m.ctx, a, v, n) == 1; }
bool wr(lce195x_env *e, uintptr_t a, const void *v, size_t n) { return a && a <= UINTPTR_MAX - n && held(e) && e->m.write(e->m.ctx, a, v, n) == 1; }
template <class T> bool rdv(lce195x_env *e, uintptr_t a, T want) { T v; return rd(e, a, &v, sizeof v) && !memcmp(&v, &want, sizeof v); }
template <class T> bool wrv(lce195x_env *e, uintptr_t a, T v) { return wr(e, a, &v, sizeof v); }

bool is_tool(uint16_t k) { return k == LCE_ITEM_SWORD || k == LCE_ITEM_PICKAXE || k == LCE_ITEM_AXE || k == LCE_ITEM_SHOVEL || k == LCE_ITEM_HOE; }
bool is_armour(uint16_t k) { return k >= LCE_ITEM_HELMET && k <= LCE_ITEM_BOOTS; }

// Java-equivalent tier defaults (Tier objects are runtime BSS: 0x1417820.. are
// read by 0x3aecf0 only by identity). level, uses, speed, damage, enchant.
const lce195x_tier kTier[6] = {{0, 0, 0, 0, 0}, {0, 59, 2.0f, 0.0f, 15}, {1, 131, 4.0f, 1.0f, 5}, {2, 250, 6.0f, 2.0f, 14}, {3, 1561, 8.0f, 3.0f, 10}, {0, 32, 12.0f, 0.0f, 22}};
// classification material codes stored by Items::init (+0x2c): wood 1, stone 2, iron 3, gold 4, diamond 5
const uint32_t kTierMat[6] = {0, 1, 2, 3, 5, 4};
// classification type (+0x28): sword 1, shovel 2, pickaxe 3, axe 4, hoe 5 (0x394173.., 0x39354a.., 0x393682.., 0x393416..)
uint32_t tool_type(uint16_t k) { return k == LCE_ITEM_SWORD ? 1 : k == LCE_ITEM_SHOVEL ? 2 : k == LCE_ITEM_PICKAXE ? 3 : k == LCE_ITEM_AXE ? 4 : 5; }
// armour render index (layer table 0x145d7e0: cloth, chain, iron, diamond, gold, turtle) -> stock material code
const uint32_t kRenderMat[5] = {6, 7, 3, 5, 4};
// default protections per render index (feet, legs, chest, head) and durability multiplier
const int32_t kRenderProt[5][4] = {{1, 2, 3, 1}, {1, 4, 5, 2}, {2, 5, 6, 2}, {3, 6, 8, 3}, {1, 3, 5, 2}};
const int32_t kRenderMult[5] = {5, 15, 15, 33, 7};
const int32_t kRenderEnchant[5] = {15, 12, 9, 10, 25};
const int32_t kDurTable[4] = {13, 15, 16, 11};  // 0xe72a00, feet..head

int render_index(const char *t) {
    if (!t) return 2;
    static const char *names[5][2] = {{"minecraft:leather", "minecraft:cloth"}, {"minecraft:chainmail", "minecraft:chain"}, {"minecraft:iron", 0},
                                      {"minecraft:diamond", 0}, {"minecraft:gold", "minecraft:golden"}};
    for (int i = 1; i < 5; i++)  // leather (0) excluded: the dye overlay is gated on the stock leather pointer (0xb03d4e)
        for (int j = 0; j < 2; j++) if (names[i][j] && !strcmp(names[i][j], t)) return i;
    return -1;
}
// armour kind -> equipment slot global and its index (slot+8)
uintptr_t equip_rva(uint16_t k) { return LCE195X_RVA_EQUIP_HEAD + 8u * (uint32_t)(k - LCE_ITEM_HELMET); }
uint32_t equip_index(uint16_t k) { return 3u - (uint32_t)(k - LCE_ITEM_HELMET); }

struct ClassInfo { uint32_t size, vt, slots; };
ClassInfo class_of(uint16_t k) {
    switch (k) {
    case LCE_ITEM_PICKAXE: return {0xc8, LCE195X_RVA_PICKAXE_VT, 56};
    case LCE_ITEM_AXE: return {0xc8, LCE195X_RVA_AXE_VT, 56};
    case LCE_ITEM_SHOVEL: return {0xc8, LCE195X_RVA_SHOVEL_VT, 56};
    case LCE_ITEM_SWORD: return {0xa8, LCE195X_RVA_SWORD_VT, 57};
    case LCE_ITEM_HOE: return {0xa8, LCE195X_RVA_HOE_VT, 56};
    case LCE_ITEM_FOOD: return {0xb8, LCE195X_RVA_FOOD_VT, 62};
    case LCE_ITEM_HELMET: case LCE_ITEM_CHESTPLATE: case LCE_ITEM_LEGGINGS: case LCE_ITEM_BOOTS: return {0xd0, LCE195X_RVA_ARMOUR_VT, 56};
    default: return {0xa0, LCE195X_RVA_ITEM_VT, 56};
    }
}

lce195x_tier tier_for(const lce_item_desc *d) {
    lce195x_tier t = kTier[d->tier];
    if (d->max_damage) t.uses = d->max_damage;
    if (d->mining_speed_milli) t.speed = (float)d->mining_speed_milli / 1000.0f;
    if (d->attack_bonus_milli) t.damage = (float)d->attack_bonus_milli / 1000.0f;
    if (d->enchantability) t.enchantment = d->enchantability;
    return t;
}

lce195x_item *find(uintptr_t object) {
    unsigned n = __atomic_load_n(&g_item_count, __ATOMIC_ACQUIRE);
    for (unsigned i = 0; i < n; i++) if (g_items[i]->object == object) return g_items[i];
    return nullptr;
}

bool icon(lce195x_env *e, uintptr_t item, const char *name) {
    uint16_t text[32]; size_t n = 0;
    while (name[n] && n < 31) { text[n] = (unsigned char)name[n]; n++; }
    text[n] = 0;
    if (name[n] || !rdv<uint64_t>(e, item + 0x88, 0) || !rdv<uint64_t>(e, item + 0x90, 7) || !held(e)) return false;
    e->n.wstring((void *)(item + 0x70), text, n);
    return rdv<uint64_t>(e, item + 0x88, n);
}

}  // namespace

extern "C" uint32_t lce195x_catalog_kind(const lce_item_desc *d) {
    if (!d) return 0;
    if (is_tool(d->kind)) return LCE195_TOOL;
    if (is_armour(d->kind)) return LCE195_ARMOUR;
    if (d->kind == LCE_ITEM_FOOD) return LCE195_CONSUMABLE;
    return LCE195_MATERIAL;
}

extern "C" uint32_t lce195x_catalog_max_stack(const lce_item_desc *d) {
    if (!d) return 0;
    uint32_t k = lce195x_catalog_kind(d);
    return k == LCE195_TOOL || k == LCE195_ARMOUR ? 1u : d->max_stack ? d->max_stack : 64u;
}

extern "C" lce_status lce195x_item_check(const lce_item_desc *d, char *err, uint32_t cap) {
    if (!d || d->kind >= LCE_ITEM_KIND_COUNT) { set_err(err, cap, "items195: unknown item kind"); return LCE_E_INVALID; }
    if (d->fuel_ticks) { char e2[160]; if (lce195x_fuel_check(d, d->fuel_ticks, e2, sizeof e2) != LCE_OK) { set_err(err, cap, e2); return LCE_E_UNSUPPORTED; } }
    bool food_fields = d->food_hunger || d->food_saturation_milli || d->use_ticks || d->food_effect_count || (d->flags & LCE_ITEM_FLAG_FOOD_ALWAYS);
    if (d->kind != LCE_ITEM_FOOD && food_fields) { set_err(err, cap, "items195: food fields on a non-food item"); return LCE_E_UNSUPPORTED; }
    if (!is_armour(d->kind) && (d->armor_points || d->armor_texture)) { set_err(err, cap, "items195: armour fields on a non-armour item"); return LCE_E_UNSUPPORTED; }
    if (!is_tool(d->kind) && (d->tier || d->mining_speed_milli || d->attack_bonus_milli)) { set_err(err, cap, "items195: tier/speed/attack need a tool kind"); return LCE_E_UNSUPPORTED; }
    if (d->max_stack > 64) { set_err(err, cap, "items195: max_stack > 64"); return LCE_E_INVALID; }
    if ((d->flags & LCE_ITEM_FLAG_THROWABLE) && d->kind != LCE_ITEM_GENERIC) { set_err(err, cap, "items195: THROWABLE needs a generic item (it becomes a SnowballItem)"); return LCE_E_UNSUPPORTED; }
#ifdef LCE_HAVE_BOATS_1_10
    if (d->flags & LCE_ITEM_FLAG_BOAT) {
        if (d->kind != LCE_ITEM_GENERIC || (d->flags & LCE_ITEM_FLAG_THROWABLE)) { set_err(err, cap, "items195: BOAT needs a generic, non-throwable item (it becomes a BoatItem)"); return LCE_E_UNSUPPORTED; }
        if (d->boat_type > 5) { set_err(err, cap, "items195: boat_type is 0..5 (oak spruce birch jungle acacia dark_oak)"); return LCE_E_INVALID; }
    } else if (d->boat_type) { set_err(err, cap, "items195: boat_type without LCE_ITEM_FLAG_BOAT"); return LCE_E_INVALID; }
#endif
    if (d->kind == LCE_ITEM_GENERIC) {
        if (d->max_damage) { set_err(err, cap, "items195: durability needs a tool/armour kind"); return LCE_E_UNSUPPORTED; }
        return LCE_OK;
    }
    if (is_tool(d->kind) || is_armour(d->kind)) {
        if (d->max_stack > 1) { set_err(err, cap, "items195: tools and armour stack to 1"); return LCE_E_UNSUPPORTED; }
        if (d->max_damage > 32000) { set_err(err, cap, "items195: durability > 32000"); return LCE_E_INVALID; }
    }
    if (is_tool(d->kind)) {
        if (d->tier < LCE_TIER_WOOD || d->tier > LCE_TIER_GOLD) { set_err(err, cap, "items195: tools need a tier (wood..gold)"); return LCE_E_UNSUPPORTED; }
        if (d->mining_speed_milli > 100000 || d->attack_bonus_milli < -4000 || d->attack_bonus_milli > 100000 || d->enchantability > 1000) { set_err(err, cap, "items195: tool values out of range"); return LCE_E_INVALID; }
        return LCE_OK;
    }
    if (is_armour(d->kind)) {
        if (render_index(d->armor_texture) < 0) {
            set_err(err, cap, "items195: custom armour layer textures are not possible (layer lookup 0xb040b0 indexes a fixed 6-name table by render index); use armor_texture NULL or minecraft:chainmail/iron/diamond/gold");
            return LCE_E_UNSUPPORTED;
        }
        if (d->armor_points > 30 || d->enchantability > 1000) { set_err(err, cap, "items195: armour values out of range"); return LCE_E_INVALID; }
        return LCE_OK;
    }
    // food
    if (d->max_damage) { set_err(err, cap, "items195: food has no durability"); return LCE_E_UNSUPPORTED; }
    if (!d->food_hunger || d->food_hunger > 40 || d->food_saturation_milli > 10000) { set_err(err, cap, "items195: food needs hunger 1..40, saturation <= 10"); return LCE_E_INVALID; }
    if (d->use_ticks && d->use_ticks != 32 && d->use_ticks != 16) { set_err(err, cap, "items195: eat duration is 32 or 16 ticks only (getUseDuration 0x2f5cf0 reads the +0xa6 fast-food flag)"); return LCE_E_UNSUPPORTED; }
    if (d->food_effect_count > 1) { set_err(err, cap, "items195: FoodItem holds one eat effect (+0xa8/+0xb0); more need an addEatEffect (+0x1c0) override"); return LCE_E_UNSUPPORTED; }
    if (d->food_effect_count) {
        const lce_effect_ref &r = d->food_effects[0];
        if (!r.effect || !r.duration_ticks || r.duration_ticks > 1000000 || r.chance_percent > 100) { set_err(err, cap, "items195: invalid food effect"); return LCE_E_INVALID; }
    }
    return LCE_OK;
}

extern "C" lce_status lce195x_fuel_check(const lce_item_desc *d, uint32_t burn, char *err, uint32_t cap) {
    (void)d;
    if (!burn) return LCE_OK;
    set_err(err, cap, "items195: custom furnace fuel is not yet supported by the retail backend");
    return LCE_E_UNSUPPORTED;
}

extern "C" int lce195x_item_construct(lce195x_env *e, lce195x_item *it, const lce195x_item_spec *s) {
    if (!e || !it || !s || !s->desc || !s->icon || it->state || !e->base || !e->m.held || !e->m.read || !e->m.write ||
        !e->n.alloc || !e->n.wstring || !e->n.item_by_id || !e->n.item_ctor) return 0;
    const lce_item_desc *d = s->desc;
    lce195x_natives &n = e->n;
    char why[8];
    lce_item_desc checked = *d;
    if (e->allow_fuel && checked.fuel_ticks <= 72000) checked.fuel_ticks = 0;
    if (lce195x_item_check(&checked, why, sizeof why) != LCE_OK || s->id < 2283 || s->id > 2397) return 0;
    if (__atomic_load_n(&g_item_count, __ATOMIC_ACQUIRE) >= LCE195X_MAX_ITEMS || (g_env && g_env != e)) return 0;
    const uint16_t k = d->kind;
    const bool thrown = k == LCE_ITEM_GENERIC && (d->flags & LCE_ITEM_FLAG_THROWABLE);
    const uintptr_t throw_use = __atomic_load_n(&g_throw_use, __ATOMIC_ACQUIRE);
    if (thrown && !throw_use) return 0;
#ifdef LCE_HAVE_BOATS_1_10
    const bool boat = k == LCE_ITEM_GENERIC && (d->flags & LCE_ITEM_FLAG_BOAT) && !thrown;
#else
    const bool boat = false;
#endif
    ClassInfo ci = class_of(k);
    if (thrown) ci.vt = LCE195X_RVA_SNOWBALLITEM_VT;   // Item ctor + vptr (SnowballItem has no fields of its own)
    if (boat) ci.vt = LCE195X_RVA_BOATITEM_VT;         // Item ctor + vptr + the +0x9c type, as BoatItem 0xd7640 (same 0xa0 object)
    switch (k) {   // the class constructor this kind needs must be bound
    case LCE_ITEM_PICKAXE: if (!n.pickaxe_ctor) return 0; break;
    case LCE_ITEM_AXE: if (!n.axe_ctor) return 0; break;
    case LCE_ITEM_SHOVEL: if (!n.shovel_ctor) return 0; break;
    case LCE_ITEM_SWORD: if (!n.sword_ctor) return 0; break;
    case LCE_ITEM_HOE: if (!n.set_max_damage) return 0; break;
    case LCE_ITEM_FOOD: if (!n.food_ctor || (d->food_effect_count && (!s->food_effect || !n.mei_ctor || !n.food_set_effect))) return 0; break;
    default: if (is_armour(k) && (!n.armour_ctor || !n.set_max_damage)) return 0; break;
    }
    it->state = 3;
    it->use_base = 0;
    if (!held(e) || n.item_by_id(s->id)) return 0;
    // stock class vtable: [offset-to-top 0, RTTI, slots], name slot must be stock
    uintptr_t class_vt = e->base + ci.vt;
    memset(it->vtable, 0, sizeof it->vtable);
    if (!rd(e, class_vt - 16, it->vtable, (2 + ci.slots) * 8) || it->vtable[0] || !it->vtable[1] || it->vtable[2 + LCE195X_SLOT_NAME / 8] != e->base + 0x3af070u) return 0;
    if (thrown && it->vtable[2 + LCE195X_SLOT_USE / 8] != e->base + LCE195X_RVA_SNOWBALLITEM_USE) return 0;
    if (boat && it->vtable[2 + LCE195X_SLOT_USE / 8] != e->base + LCE195X_RVA_BOATITEM_USE) return 0;
    uintptr_t foil_fn = 0;
    if (d->flags & LCE_ITEM_FLAG_FOIL) {
        foil_fn = e->base + LCE195X_RVA_FOIL_ALWAYS;
        const uint8_t expected[] = {0xb0, 0x01, 0xc3};  // mov al, 1; ret
        uint8_t code[sizeof expected];
        if (it->vtable[2 + LCE195X_SLOT_FOIL / 8] != e->base + LCE195X_RVA_FOIL_STOCK ||
            !rd(e, foil_fn, code, sizeof code) || memcmp(code, expected, sizeof code)) return 0;
    }
    const bool set_handheld = (d->flags & LCE_ITEM_FLAG_HANDHELD) && !is_tool(k);
    if (set_handheld) {
        const uintptr_t getter = e->base + LCE195X_RVA_HAND_EQUIPPED;
        const uint8_t expected[] = {0x8a, 0x47, LCE195X_OFF_HAND_EQUIPPED, 0xc3};
        uint8_t code[sizeof expected];
        if (it->vtable[2 + LCE195X_SLOT_HAND_EQUIPPED / 8] != getter ||
            !rd(e, getter, code, sizeof code) || memcmp(code, expected, sizeof code)) return 0;
    }
    // armour pre-reads (no writes): equipment slot and iron material sound
    uintptr_t equip = 0, iron = 0, sound = 0;
    int ri = -1;
    if (is_armour(k)) {
        uint32_t idx = 9;
        ri = render_index(d->armor_texture);
        if (!rd(e, e->base + equip_rva(k), &equip, 8) || !equip || !rd(e, equip + 8, &idx, 4) || idx != equip_index(k) ||
            !rd(e, e->base + LCE195X_RVA_IRON_MATERIAL, &iron, 8) || !iron || !rd(e, iron + 0x40, &sound, 8) || !sound) return 0;
    }
    it->desc = d; it->id = s->id; it->kind = lce195x_catalog_kind(d); it->slots = ci.slots; it->class_vt = class_vt;
    it->repair_item = s->repair_item; it->fuel_ticks = d->fuel_ticks;
    it->foil_fn = foil_fn;
    it->tier = tier_for(d);
    it->orig_enchant = it->vtable[2 + LCE195X_SLOT_ENCHANT_VALUE / 8];
    it->orig_repair = it->vtable[2 + LCE195X_SLOT_REPAIR / 8];
    if (!g_env) g_env = e;
    // ---- first native allocation ----
    it->state = 1;
    uintptr_t obj = (uintptr_t)n.alloc(ci.size);
    if (!obj || (obj & 15)) goto uncertain;
    it->object = obj;
    if (k == LCE_ITEM_GENERIC) {
        n.item_ctor((void *)obj);
        if (!rdv<uintptr_t>(e, obj, (thrown || boat) ? e->base + LCE195X_RVA_ITEM_VT : class_vt)) goto uncertain;
#ifdef LCE_HAVE_BOATS_1_10
        if (boat && !wrv<uint32_t>(e, obj + LCE195X_OFF_BOAT_TYPE, d->boat_type)) goto uncertain;   // 0xd7660
#endif
    } else if (k == LCE_ITEM_PICKAXE || k == LCE_ITEM_AXE || k == LCE_ITEM_SHOVEL) {
        (k == LCE_ITEM_PICKAXE ? n.pickaxe_ctor : k == LCE_ITEM_AXE ? n.axe_ctor : n.shovel_ctor)((void *)obj, &it->tier);
        // DiggerItem 0x1fcfd0: +0xc0 tier, +0x18 = 1, +0x1c uses (0x35ecd0), +0xb0 speed (0x3aecc0)
        if (!rdv<uintptr_t>(e, obj, class_vt) || !rdv<uintptr_t>(e, obj + 0xc0, (uintptr_t)&it->tier) || !rdv<uint32_t>(e, obj + 0x18, 1) ||
            !rdv<uint32_t>(e, obj + 0x1c, (uint32_t)it->tier.uses) || !rdv<float>(e, obj + 0xb0, it->tier.speed)) goto uncertain;
    } else if (k == LCE_ITEM_SWORD) {
        n.sword_ctor((void *)obj, &it->tier);
        // 0x6cefd0: +0xa0 tier, +0x18 = 1, +0x1c uses, +0x9c = damage + 4.0 (0x6cf018)
        if (!rdv<uintptr_t>(e, obj, class_vt) || !rdv<uintptr_t>(e, obj + 0xa0, (uintptr_t)&it->tier) || !rdv<uint32_t>(e, obj + 0x18, 1) ||
            !rdv<uint32_t>(e, obj + 0x1c, (uint32_t)it->tier.uses) || !rdv<float>(e, obj + 0x9c, it->tier.damage + 4.0f)) goto uncertain;
    } else if (k == LCE_ITEM_HOE) {
        // Items::init inlines HoeItem (0x395d69..0x395dd6): base ctor, vptr, +0xa0 tier, stack 1, setMaxDamage(uses), +0x9c = damage + 1
        n.item_ctor((void *)obj);
        if (!rdv<uintptr_t>(e, obj, e->base + LCE195X_RVA_ITEM_VT) || !wrv<uintptr_t>(e, obj, class_vt) || !wrv<uintptr_t>(e, obj + 0xa0, (uintptr_t)&it->tier) ||
            !wrv<uint32_t>(e, obj + 0x18, 1)) goto uncertain;
        n.set_max_damage((void *)obj, it->tier.uses);
        if (!rdv<uint32_t>(e, obj + 0x1c, (uint32_t)it->tier.uses) || !wrv<float>(e, obj + 0x9c, it->tier.damage + 1.0f)) goto uncertain;
        it->enchantability = it->tier.enchantment;  // stock hoe slot +0x138 is 0x3af3c0 (returns 0)
    } else if (is_armour(k)) {
        uint32_t idx = equip_index(k);
        for (int j = 0; j < 4; j++) it->protections[j] = kRenderProt[ri][j];
        if (d->armor_points) it->protections[idx] = d->armor_points;
        // private ArmorMaterial (0x50): +0 wstring name, +0x28 mult, +0x30 int*, +0x38 enchant, +0x40 sound, +0x48 float toughness
        uintptr_t m = (uintptr_t)it->material;
        memset(it->material, 0, sizeof it->material);
        *(uint64_t *)(m + 0x20) = 7;
        static const uint16_t nm[] = {'l', 'c', 'e', 's', 'd', 'k'};
        n.wstring((void *)m, nm, 6);
        if (*(uint64_t *)(m + 0x18) != 6) goto uncertain;
        *(int32_t *)(m + 0x28) = kRenderMult[ri];
        *(uintptr_t *)(m + 0x30) = (uintptr_t)it->protections;
        *(int32_t *)(m + 0x38) = d->enchantability ? d->enchantability : kRenderEnchant[ri];
        *(uintptr_t *)(m + 0x40) = sound;
        *(float *)(m + 0x48) = 0.0f;
        n.armour_ctor((void *)obj, (void *)m, ri, (void *)equip);
        // 0x32d04..0x32d7f: +0xa0 slot, +0xa8 defence, +0xac toughness, +0xb0 render, +0xb8 material, +0x18 = 1, +0x1c = mult * table[idx]
        if (!rdv<uintptr_t>(e, obj, class_vt) || !rdv<uintptr_t>(e, obj + 0xa0, equip) || !rdv<int32_t>(e, obj + 0xa8, it->protections[idx]) ||
            !rdv<float>(e, obj + 0xac, 0.0f) || !rdv<int32_t>(e, obj + 0xb0, ri) || !rdv<uintptr_t>(e, obj + 0xb8, m) || !rdv<uint32_t>(e, obj + 0x18, 1) ||
            !rdv<int32_t>(e, obj + 0x1c, kRenderMult[ri] * kDurTable[idx])) goto uncertain;
        if (d->max_damage) {
            n.set_max_damage((void *)obj, d->max_damage);
            if (!rdv<uint32_t>(e, obj + 0x1c, d->max_damage)) goto uncertain;
        }
    } else {  // food
        float sat = (float)d->food_saturation_milli / 1000.0f;
        n.food_ctor((void *)obj, d->food_hunger, sat, 0);
        // 0x2d69e8..0x2d6a2a: +0x9c nutrition, +0xa0 saturation, +0xa4 meat
        if (!rdv<uintptr_t>(e, obj, class_vt) || !rdv<int32_t>(e, obj + 0x9c, d->food_hunger) || !rdv<float>(e, obj + 0xa0, sat) ||
            !rdv<uint8_t>(e, obj + 0xa4, 0) || !rdv<uintptr_t>(e, obj + 0xa8, 0)) goto uncertain;
        if ((d->flags & LCE_ITEM_FLAG_FOOD_ALWAYS) && !wrv<uint8_t>(e, obj + 0xa5, 1)) goto uncertain;   // setCanAlwaysEat 0x2f6040
        if (d->use_ticks == 16 && !wrv<uint8_t>(e, obj + 0xa6, 1)) goto uncertain;                       // setFastFood 0x2f6050
        if (d->food_effect_count) {
            const lce_effect_ref &r = d->food_effects[0];
            float p = r.chance_percent ? (float)r.chance_percent / 100.0f : 1.0f;
            uintptr_t mei = (uintptr_t)n.alloc(0x18);
            if (!mei) goto uncertain;
            it->effect_instance = mei;
            n.mei_ctor((void *)mei, (void *)s->food_effect, (int)r.duration_ticks, r.amplifier);
            n.food_set_effect((void *)obj, (void *)mei, p);
            if (!rdv<uintptr_t>(e, obj + 0xa8, mei) || !rdv<float>(e, obj + 0xb0, p)) goto uncertain;
        }
        if (!wrv<uint32_t>(e, obj + 0x18, d->max_stack ? d->max_stack : 64u)) goto uncertain;
        if (d->enchantability) it->enchantability = d->enchantability;
    }
    if (k == LCE_ITEM_GENERIC) {
        if (!wrv<uint32_t>(e, obj + 0x18, d->max_stack ? d->max_stack : boat ? 1u : 64u)) goto uncertain;   // BoatItem 0xd7667: stack 1
        if (d->enchantability) it->enchantability = d->enchantability;
    }
    // classification pair at +0x28 (what Items::init stores; drives recipe grouping)
    {
        uint32_t cls[2] = {0, 0};
        if (k == LCE_ITEM_GENERIC) { cls[0] = 0x1f; cls[1] = 3; }      // backend195 material pair (candidate mapping)
        else if (is_tool(k)) { cls[0] = tool_type(k); cls[1] = kTierMat[d->tier]; }
        else if (is_armour(k)) { cls[0] = 7u + (uint32_t)(k - LCE_ITEM_HELMET); cls[1] = kRenderMat[ri]; }
        if (!wr(e, obj + 0x28, cls, 8)) goto uncertain;
    }
    if (!icon(e, obj, s->icon)) goto uncertain;
    if (set_handheld && !wrv<uint8_t>(e, obj + LCE195X_OFF_HAND_EQUIPPED, 1)) goto uncertain;
    // private vtable: copy of the class table, overrides only where needed
    if (s->name_fn) it->vtable[2 + LCE195X_SLOT_NAME / 8] = s->name_fn;
    if (it->foil_fn) it->vtable[2 + LCE195X_SLOT_FOIL / 8] = it->foil_fn;
    if (it->enchantability) it->vtable[2 + LCE195X_SLOT_ENCHANT_VALUE / 8] = (uintptr_t)&lce195x_enchant_dispatch;
    if (it->repair_item) it->vtable[2 + LCE195X_SLOT_REPAIR / 8] = (uintptr_t)&lce195x_repair_dispatch;
    if (thrown) { it->use_base = throw_use; it->vtable[2 + LCE195X_SLOT_USE / 8] = throw_use; }
    {
        unsigned slot = __atomic_load_n(&g_item_count, __ATOMIC_ACQUIRE);
        if (slot >= LCE195X_MAX_ITEMS) goto uncertain;
        g_items[slot] = it;
        __atomic_store_n(&g_item_count, slot + 1, __ATOMIC_RELEASE);   // dispatchers can resolve it before the vptr swap
        uintptr_t target = (uintptr_t)&it->vtable[2];
        if (!wrv<uintptr_t>(e, obj, target) || !rdv<uintptr_t>(e, obj, target)) goto uncertain;
    }
    it->state = 2;
    return 1;
uncertain:
    it->state = 4;
    return -1;
}

extern "C" void lce195x_set_throw_use(uintptr_t use) { __atomic_store_n(&g_throw_use, use, __ATOMIC_RELEASE); }

extern "C" int lce195x_item_identity(lce195x_env *e, const lce195x_item *it) {
    if (!e || !it || it->state != 2) return 0;
    uintptr_t vt = 0, stock[2 + LCE195X_MAX_SLOTS];
    if (!rd(e, it->object, &vt, 8) || vt != (uintptr_t)&it->vtable[2] || !rd(e, it->class_vt - 16, stock, (2 + it->slots) * 8)) return 0;
    for (unsigned j = 0; j < 2 + it->slots; j++) {
        uintptr_t want = stock[j];
        if (j == 2 + LCE195X_SLOT_NAME / 8 && it->vtable[j] != stock[j]) continue;  // caller's name dispatcher
        if (j == 2 + LCE195X_SLOT_FOIL / 8 && it->foil_fn) want = it->foil_fn;
        if (j == 2 + LCE195X_SLOT_ENCHANT_VALUE / 8 && it->enchantability) want = (uintptr_t)&lce195x_enchant_dispatch;
        if (j == 2 + LCE195X_SLOT_REPAIR / 8 && it->repair_item) want = (uintptr_t)&lce195x_repair_dispatch;
        if (j == 2 + LCE195X_SLOT_USE / 8 && it->use_base) want = it->use_base;
        if (j == 2 + LCE195X_SLOT_USE / 8 && it->use_fn) want = it->use_fn;
        if (it->vtable[j] != want) return 0;
    }
    return 1;
}

extern "C" int lce195x_item_route_use(lce195x_env *e, lce195x_item *it, uintptr_t thunk) {
    if (!e || !it || it->state != 2 || !thunk || it->use_fn || !held(e)) return 0;
    uintptr_t vt = 0;
    if (!rd(e, it->object, &vt, 8) || vt != (uintptr_t)&it->vtable[2]) return 0;
    uintptr_t *slot = &it->vtable[2 + LCE195X_SLOT_USE / 8];
    uintptr_t stock = 0;
    if (it->use_base) stock = it->use_base;   // a throwable routes over its p195_item_use (events see the use first)
    else if (!rd(e, it->class_vt + LCE195X_SLOT_USE, &stock, 8)) return 0;
    if (*slot != stock) return 0;
    it->use_fn = thunk;
    __atomic_store_n(slot, thunk, __ATOMIC_RELEASE);
    return 1;
}

// int Item::getEnchantmentValue(Item *this)
extern "C" int lce195x_enchant_dispatch(void *item) {
    lce195x_item *it = find((uintptr_t)item);
    if (it && it->enchantability) return it->enchantability;
    return it && it->orig_enchant ? ((int (*)(void *))it->orig_enchant)(item) : 0;
}

// bool Item::isValidRepairItem(Item *this, const shared_ptr<II>& target, const shared_ptr<II>& material)
// shared_ptr layout {ptr, ctrl}; II +0x38 empty flag, +0x28 Item* (0x356110).
extern "C" int lce195x_repair_dispatch(void *item, void *sp_a, void *sp_b) {
    lce195x_item *it = find((uintptr_t)item);
    if (!it) return 0;
    if (it->repair_item && sp_b) {
        uintptr_t stack = *(const uintptr_t *)sp_b;
        if (stack && !*(const uint8_t *)(stack + 0x38) && *(const uintptr_t *)(stack + 0x28) == it->repair_item) return 1;
    }
    return it->orig_repair ? ((int (*)(void *, void *, void *))it->orig_repair)(item, sp_a, sp_b) : 0;
}

extern "C" void lce195x_natives_bind(lce195x_natives *n, uintptr_t b) {
#define BIND(field, rva) n->field = reinterpret_cast<__typeof__(n->field)>(b + (rva))
    BIND(alloc, LCE195X_RVA_ALLOC); BIND(alloc_array, LCE195X_RVA_ALLOC_ARRAY); BIND(wstring, LCE195X_RVA_WSTRING); BIND(item_by_id, LCE195X_RVA_ITEM_BY_ID);
    BIND(item_ctor, LCE195X_RVA_ITEM_CTOR); BIND(set_max_damage, LCE195X_RVA_SET_MAX_DAMAGE);
    BIND(pickaxe_ctor, LCE195X_RVA_PICKAXE_CTOR); BIND(axe_ctor, LCE195X_RVA_AXE_CTOR); BIND(shovel_ctor, LCE195X_RVA_SHOVEL_CTOR); BIND(sword_ctor, LCE195X_RVA_SWORD_CTOR);
    BIND(armour_ctor, LCE195X_RVA_ARMOUR_CTOR); BIND(food_ctor, LCE195X_RVA_FOOD_CTOR); BIND(food_set_effect, LCE195X_RVA_FOOD_SET_EFFECT); BIND(mei_ctor, LCE195X_RVA_MEI_CTOR);
    BIND(stack_ctor, LCE195X_RVA_STACK_CTOR); BIND(shaped_ctor, LCE195X_RVA_SHAPED_CTOR); BIND(shapeless_ctor, LCE195X_RVA_SHAPELESS_CTOR);
    BIND(output_key, LCE195X_RVA_OUTPUT_KEY); BIND(recipe_bucket, LCE195X_RVA_RECIPE_BUCKET); BIND(recipe_vec_grow, LCE195X_RVA_RECIPE_VEC_GROW); BIND(stack_vec_grow, LCE195X_RVA_STACK_VEC_GROW);
    BIND(furnace_map[0], LCE195X_RVA_FURNACE_RESULT); BIND(furnace_map[1], LCE195X_RVA_FURNACE_XP); BIND(furnace_map[2], LCE195X_RVA_FURNACE_MASK);
    BIND(effect_by_id, LCE195X_RVA_EFFECT_BY_ID); BIND(potion_by_id, LCE195X_RVA_POTION_BY_ID); BIND(mix_vec_grow, LCE195X_RVA_MIX_VEC_GROW);
#undef BIND
}
