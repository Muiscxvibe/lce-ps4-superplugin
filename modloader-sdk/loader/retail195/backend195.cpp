// Retail 1.95 backend, item/recipe component: adapts an SDK plan onto the
// shipping content stack's provider seams and modules/items195. See
// backend195.h for the contract and cited RVAs.
#include "backend195.h"
#include <stdio.h>
#include <string.h>
#include "../groups.h"

namespace {

lce195_backend *g_active;  // process-lifetime; one backend per process
// Large publisher state (stock snapshots): process lifetime, one per process.
lce195x_recipe_publisher g_recipe_pub;
lce195x_furnace_publisher g_furnace_pub;
lce195x_brew_publisher g_brew_pub;

void set_err(char *err, uint32_t cap, const char *msg) { if (err && cap) snprintf(err, cap, "%s", msg); }

// UTF-8 -> UTF-16 (BMP only; others become '?'). Returns units written.
uint32_t utf16(const char *s, uint16_t *out, uint32_t cap) {
    uint32_t n = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p && n + 1 < cap) {
        uint32_t c = *p++;
        if (c >= 0xc0 && c < 0xe0 && (*p & 0xc0) == 0x80) c = ((c & 0x1f) << 6) | (*p++ & 0x3f);
        else if (c >= 0xe0 && c < 0xf0 && (p[0] & 0xc0) == 0x80 && (p[1] & 0xc0) == 0x80) { c = ((c & 0x0f) << 12) | ((p[0] & 0x3f) << 6) | (p[1] & 0x3f); p += 2; }
        else if (c >= 0x80) { while ((*p & 0xc0) == 0x80) ++p; c = '?'; }
        out[n++] = (uint16_t)c;
    }
    out[n] = 0;
    return n;
}

bool rd(lce195_backend *b, uintptr_t a, void *v, size_t n) { return a && b->mem.read(b->mem.ctx, a, v, n) == 1; }

bool is_tool(uint16_t k) { return k == LCE_ITEM_SWORD || k == LCE_ITEM_PICKAXE || k == LCE_ITEM_AXE || k == LCE_ITEM_SHOVEL || k == LCE_ITEM_HOE; }
bool is_armour(uint16_t k) { return k >= LCE_ITEM_HELMET && k <= LCE_ITEM_BOOTS; }

// items195 "held" = the phase this backend currently owns, on its thread.
int xheld(void *o) {
    lce195_backend *b = (lce195_backend *)o;
    switch (__atomic_load_n(&b->held_phase, __ATOMIC_ACQUIRE)) {
    case 1: return b->window && lce195_items_window_held(b->window) == 1 ? 1 : 0;
    case 2: return b->mem.crafting_held && b->mem.crafting_held(b->mem.ctx) == 1 ? 1 : 0;
    case 3: return b->mem.startup_held && b->mem.startup_held(b->mem.ctx) == 1 ? 1 : 0;
    default: return 0;
    }
}
int xread(void *o, uintptr_t a, void *v, size_t n) { lce195_backend *b = (lce195_backend *)o; return b->mem.read(b->mem.ctx, a, v, n); }
int xwrite(void *o, uintptr_t a, const void *v, size_t n) { lce195_backend *b = (lce195_backend *)o; return b->mem.write(b->mem.ctx, a, v, n); }

bool recipes_bound(const lce195_backend *b) {
    const lce195x_natives &n = b->xenv.n;
    return b->seams.recipe_provider_register && n.alloc && n.alloc_array && n.item_by_id && n.stack_ctor && n.shaped_ctor && n.shapeless_ctor &&
           n.output_key && n.recipe_bucket && n.recipe_vec_grow && n.stack_vec_grow && b->mem.crafting_held;
}

// ---- backend interface ----------------------------------------------------
int32_t resolve_group(void *, const char *key, int crafting) { return lce_group_lookup(key, crafting); }

lce_status prepare(void *self, lce_plan *p, char *err, uint32_t cap) {
    lce195_backend *b = (lce195_backend *)self;
    if (b->state != LCE195_SDK_IDLE) { set_err(err, cap, "retail195: already armed"); return LCE_E_PHASE; }
    if (!b->base || !b->natives.alloc || !b->natives.item_ctor || !b->natives.wstring || !b->natives.item_by_id || !b->natives.name_original ||
        !b->seams.items_add_provider || !b->seams.catalog_add || !b->mem.read || !b->mem.write) {
        set_err(err, cap, "retail195: backend not bound (missing natives/seams)");
        return LCE_E_BACKEND;
    }
    const uint32_t caps = lce195_backend_caps(b);
    if (p->recipe_count && !(caps & LCE_CAP_RECIPES)) { set_err(err, cap, "retail195: recipe publisher not bound"); return LCE_E_BACKEND; }
    if (p->smelt_count && !(caps & LCE_CAP_SMELTING)) { set_err(err, cap, "retail195: furnace publisher not bound"); return LCE_E_BACKEND; }
    uint32_t real = 0;
    for (uint32_t i = 0; i < p->item_count; ++i) {
        const lce_plan_item &it = p->items[i];
        if (it.placeholder) {
            // Placeholders would need a generic icon owner and a second item phase;
            // refuse loudly instead of silently leaving an ID unbacked.
            set_err(err, cap, "retail195: placeholder items not supported yet (gap G7); keep the mod installed");
            return LCE_E_UNSUPPORTED;
        }
        if (it.id < 2283 || it.id > 2397 || it.texture < 0 || (uint32_t)it.texture >= p->texture_count || p->textures[it.texture].atlas != 0) {
            set_err(err, cap, "retail195: item outside the SDK item window or without an item icon");
            return LCE_E_INVALID;
        }
        char e[160];
        if (lce195_backend_check_item(self, it.desc, e, sizeof e) != LCE_OK) { set_err(err, cap, e); return LCE_E_UNSUPPORTED; }
        // A repair material that is itself an SDK item must be constructed first.
        const char *rep = it.desc->repair_item;
        if (rep && strncmp(rep, "minecraft:", 10)) {
            int32_t rid = p->resolve ? p->resolve(p->resolve_ctx, LCE_REF_ITEM, rep) : -1;
            bool before = false;
            for (uint32_t j = 0; j < i; ++j) before |= (int32_t)p->items[j].id == rid;
            if (!before) { snprintf(err, cap, "retail195: item %s: repair item %s must be declared before it", it.desc->key, rep); return LCE_E_UNSUPPORTED; }
        }
        ++real;
    }
    if (real > LCE195_SDK_MAX_ITEMS) { set_err(err, cap, "retail195: more than 32 SDK items (shared 64-entry catalog)"); return LCE_E_FULL; }
    if (p->recipe_count > LCE195_SDK_MAX_RECIPES) { set_err(err, cap, "retail195: more than 128 recipes (one ABI-2 batch)"); return LCE_E_FULL; }
    for (uint32_t i = 0; i < p->recipe_count; ++i) {
        char e[160];
        if (lce195_backend_check_recipe(self, p->recipes[i].desc, e, sizeof e) != LCE_OK) { set_err(err, cap, e); return LCE_E_UNSUPPORTED; }
        for (uint32_t j = 0; j < i; ++j)
            if (!strcmp(p->recipes[j].desc->key, p->recipes[i].desc->key)) { set_err(err, cap, "retail195: duplicate recipe key"); return LCE_E_COLLISION; }
    }
    if (p->smelt_count > LCE195_SDK_MAX_SMELTS) { set_err(err, cap, "retail195: more than 64 smelting recipes"); return LCE_E_FULL; }
    for (uint32_t i = 0; i < p->smelt_count; ++i)
        for (uint32_t j = 0; j < i; ++j)
            if (p->smelts[j].input.id == p->smelts[i].input.id) {
                // FurnaceRecipes lookups are linear scans keyed by Item (0x3226f0): a second entry for one input is dead.
                set_err(err, cap, "retail195: two smelting recipes for one input item (the native lookup would only see the first)");
                return LCE_E_COLLISION;
            }
    uint32_t brews = 0;
    for (uint32_t i = 0; i < p->ext_count; ++i) brews += p->ext[i].type == LCE_EXT_BREW;
    if (brews > LCE195_SDK_MAX_BREWS) { set_err(err, cap, "retail195: more than 64 brewing mixes"); return LCE_E_FULL; }
    if (brews && !(caps & LCE_CAP_BREWING)) { set_err(err, cap, "retail195: brewing publisher not bound"); return LCE_E_BACKEND; }
    // Atlas cells are assigned by the shared atlas owner (appended 16px rows after
    // the 288 stock sprites); we only reserve unique names here.
    for (uint32_t i = 0; i < p->texture_count; ++i) p->textures[i].slot = -1;
    return LCE_OK;
}

// ---- items provider callbacks (run inside the shipping content stack) -----
int prov_prepare(void *o, uintptr_t base) {
    lce195_backend *b = (lce195_backend *)o;
    return b->state == LCE195_SDK_ARMED && base == b->base ? 1 : 0;
}

int prov_collect(void *o, lce195_items_window *w, lce195_item_catalog *c) {
    lce195_backend *b = (lce195_backend *)o;
    if (b->state != LCE195_SDK_ARMED || b->items_state || !w || !c || lce195_items_window_held(w) != 1) return 0;
    b->items_state = 1;
    b->window = w;
    __atomic_store_n(&b->held_phase, 1u, __ATOMIC_RELEASE);
    const lce195x_natives &n = b->xenv.n;
    int rc = 1;
    if (b->pre_collect) b->pre_collect_rc = b->pre_collect(b->pre_user);   // mod status effects first (food items reference them)
    for (uint32_t i = 0; i < b->item_count; ++i) {
        lce195_sdk_item &it = b->items[i];
        // Slot must still be vacant in the live registry (lce195x_item_construct re-checks it).
        if (n.item_by_id(it.id)) { snprintf(b->last_error, sizeof b->last_error, "item id %u occupied at collect", it.id); rc = i ? -1 : 0; break; }
        lce195x_item_spec s;
        memset(&s, 0, sizeof s);
        s.desc = it.desc; s.id = it.id; s.icon = it.icon; s.name_fn = (uintptr_t)&lce195_sdk_name_dispatch;
        if (it.repair_index >= 0) s.repair_item = b->items[it.repair_index].x.object;
        else if (it.repair_vanilla > 0) s.repair_item = n.item_by_id((unsigned)it.repair_vanilla);
        if (it.effect_id > 0) s.food_effect = n.effect_by_id ? n.effect_by_id(it.effect_id) : 0;
        else if (it.effect_id == -2) s.food_effect = b->mod_effect ? b->mod_effect(it.effect_key) : 0;
        if ((it.repair_index >= 0 || it.repair_vanilla > 0) && !s.repair_item) { snprintf(b->last_error, sizeof b->last_error, "item %u: repair item unresolved", it.id); rc = i ? -1 : 0; break; }
        int k = lce195x_item_construct(&b->xenv, &it.x, &s);
        if (k != 1) { snprintf(b->last_error, sizeof b->last_error, "item %s: items195 constructor %s", it.desc->key, k ? "uncertain" : "refused"); rc = k < 0 || i ? -1 : 0; break; }
        // Optional ITEM_USE event: route our private +0x40 slot through events195.
        if (b->use_route.route && b->use_route.thunk) {
            uintptr_t original = it.x.vtable[2 + LCE195X_SLOT_USE / 8];
            if (b->use_route.route(it.x.object, it.id, original) == 1 && lce195x_item_route_use(&b->xenv, &it.x, b->use_route.thunk) == 1) it.use_routed = 1;
        }
        // API 1.3 ITEM_INVENTORY_TICK: private slot +0xc8 inventoryTick (stock 0x3af000 `ret`) through events195.
        if (b->inv_route.route && b->inv_route.thunk) {
            uintptr_t *slot = &it.x.vtable[2 + 0xc8 / 8];
            if (b->inv_route.route(it.x.object, it.id, *slot) == 1) __atomic_store_n(slot, b->inv_route.thunk, __ATOMIC_RELEASE);
        }
        lce195_additive_item e;
        memset(&e, 0, sizeof e);
        e.abi = LCE195_ADDITIVE_ABI; e.size = sizeof e;
        e.provider = LCE195_SDK_PROVIDER; e.key = it.desc->key; e.display_name = it.ascii_name; e.icon = it.icon;
        e.id = it.id; e.max_stack = lce195x_catalog_max_stack(it.desc); e.kind = lce195x_catalog_kind(it.desc); e.item = it.x.object;
        if (b->seams.catalog_add(c, &e) != 1 || lce195_items_window_held(w) != 1) {
            snprintf(b->last_error, sizeof b->last_error, "item %s: catalog refused the entry", it.desc->key);
            rc = -1;
            break;
        }
    }
    __atomic_store_n(&b->held_phase, 0u, __ATOMIC_RELEASE);
    b->window = nullptr;
    b->items_state = rc == 1 ? LCE195_SDK_ITEMS_DONE : rc == 0 ? LCE195_SDK_REFUSED : LCE195_SDK_UNCERTAIN;
    return rc;
}

int prov_reserved(void *o, const char *provider, uint32_t id, const char *key) {
    lce195_backend *b = (lce195_backend *)o;
    if (!provider || !key || strcmp(provider, LCE195_SDK_PROVIDER)) return 0;
    for (uint32_t i = 0; i < b->item_count; ++i)
        if (b->items[i].id == id && !strcmp(b->items[i].desc->key, key)) return 1;
    return 0;
}

int prov_identity(void *o) {
    lce195_backend *b = (lce195_backend *)o;
    if (b->items_state != LCE195_SDK_ITEMS_DONE) return 0;
    for (uint32_t i = 0; i < b->item_count; ++i) {
        const lce195_sdk_item &it = b->items[i];
        // identity reads go through the plain checked reader (any thread)
        uintptr_t vt = 0;
        if (!rd(b, it.x.object, &vt, 8) || vt != (uintptr_t)&it.x.vtable[2] || it.x.vtable[2 + LCE195X_SLOT_NAME / 8] != (uintptr_t)&lce195_sdk_name_dispatch) return 0;
        if (it.use_routed && it.x.vtable[2 + LCE195X_SLOT_USE / 8] != b->use_route.thunk) return 0;
    }
    return 1;
}

const uint8_t *prov_icon(void *o, const char *name) {
    lce195_backend *b = (lce195_backend *)o;
    if (!name) return nullptr;
    for (uint32_t i = 0; i < b->item_count; ++i)
        if (!strcmp(b->items[i].icon, name)) return b->items[i].rgba;
    return nullptr;
}

// ---- recipe provider (ABI 2, items195 registry) ------------------------------
int recipe_provider(void *o, uintptr_t owner) {
    lce195_backend *b = (lce195_backend *)o;
    if (b->state != LCE195_SDK_ARMED || b->recipes_state || !owner || !b->mem.crafting_held || b->mem.crafting_held(b->mem.ctx) != 1) return 0;
    b->recipes_state = 1;
    __atomic_store_n(&b->held_phase, 2u, __ATOMIC_RELEASE);
    const lce_plan *p = b->plan;
    uint32_t n = 0;
    int rc = 1;
    for (uint32_t i = 0; i < p->recipe_count; ++i) {
        if (lce195x_recipe_from_plan(&b->xenv, &p->recipes[i], LCE195_SDK_PROVIDER, &b->recipes[n]) != 1) {
            snprintf(b->last_error, sizeof b->last_error, "recipe %s: an input/output item is not registered", p->recipes[i].desc->key);
            rc = 0;
            break;
        }
        ++n;
    }
    b->recipe_count = n;
    if (rc == 1 && n) {
        memset(&g_recipe_pub, 0, sizeof g_recipe_pub);
        g_recipe_pub.env = &b->xenv; g_recipe_pub.owner = owner;
        rc = lce195x_recipes_publish(&g_recipe_pub, b->recipes, n);
        if (rc != 1) snprintf(b->last_error, sizeof b->last_error, "recipes: ABI-2 publisher %s (state %u, published %u)", rc ? "uncertain" : "refused", g_recipe_pub.state, g_recipe_pub.published);
    }
    __atomic_store_n(&b->held_phase, 0u, __ATOMIC_RELEASE);
    b->recipes_state = rc == 1 ? LCE195_SDK_RECIPES_DONE : rc == 0 ? LCE195_SDK_REFUSED : LCE195_SDK_UNCERTAIN;
    return rc;
}

lce_status publish(void *self, const lce_plan *p, int *partial, char *err, uint32_t cap) {
    lce195_backend *b = (lce195_backend *)self;
    *partial = 0;
    if (g_active && g_active != b) { set_err(err, cap, "retail195: another backend instance is active"); return LCE_E_PHASE; }
    b->plan = p;
    b->item_count = 0;
    for (uint32_t i = 0; i < p->item_count; ++i) {
        lce195_sdk_item &it = b->items[b->item_count];
        memset(&it, 0, sizeof it);
        it.desc = p->items[i].desc; it.id = p->items[i].id;
        it.repair_index = -1; it.repair_vanilla = -1; it.effect_id = -1;
        snprintf(it.icon, sizeof it.icon, "lcesdk_%u", (unsigned)it.id);
        it.rgba = p->textures[p->items[i].texture].rgba;
        if (it.desc->repair_item) {
            int32_t rid = p->resolve ? p->resolve(p->resolve_ctx, LCE_REF_ITEM, it.desc->repair_item) : -1;
            for (uint32_t j = 0; j < b->item_count; ++j) if ((int32_t)b->items[j].id == rid) it.repair_index = (int32_t)j;
            if (it.repair_index < 0) it.repair_vanilla = rid;
        }
        if (it.desc->kind == LCE_ITEM_FOOD && it.desc->food_effect_count) {
            it.effect_key = it.desc->food_effects[0].effect;
            it.effect_id = lce195x_stock_effect_id(it.effect_key);
            if (it.effect_id < 1 && b->mod_effect && it.effect_key && strncmp(it.effect_key, "minecraft:", 10)) it.effect_id = -2;
        }
        // display name: en_US string for this item if the mod provided one
        const char *name = it.desc->name;
        for (uint32_t s = 0; s < p->string_count; ++s)
            if (p->strings[s].item_id == (int32_t)it.id && !strcmp(p->strings[s].locale, "en_US")) name = p->strings[s].value;
        it.name_len = utf16(name, it.name, LCE195_SDK_NAME_UNITS);
        // ASCII fallback for the shared catalog (it rejects bytes outside 32..126)
        uint32_t k = 0;
        for (const unsigned char *q = (const unsigned char *)it.desc->name; *q && k + 1 < sizeof it.ascii_name; ++q) {
            if ((*q & 0xc0) == 0x80) continue;  // UTF-8 continuation byte
            it.ascii_name[k++] = (*q >= 32 && *q <= 126) ? (char)*q : '?';
        }
        it.ascii_name[k] = 0;
        if (!k) snprintf(it.ascii_name, sizeof it.ascii_name, "item %u", (unsigned)it.id);
        ++b->item_count;
    }
    b->smelt_count = p->smelt_count;
    b->brew_count = 0;
    for (uint32_t i = 0; i < p->ext_count; ++i) b->brew_count += p->ext[i].type == LCE_EXT_BREW;
    // Arming is the publish step: registration happens later inside the shipping
    // plugin's existing one-shot startup phases. Nothing here touches game memory.
    b->state = LCE195_SDK_ARMED;
    g_active = b;
    if (b->item_count) {
        mc195_items_provider prov;
        memset(&prov, 0, sizeof prov);
        prov.opaque = b; prov.prepare = prov_prepare; prov.collect = prov_collect; prov.reserved = prov_reserved;
        prov.identity = prov_identity; prov.icon = prov_icon;
        if (b->seams.items_add_provider(&prov) != 1) { b->state = LCE195_SDK_REFUSED; set_err(err, cap, "retail195: items provider bridge refused (already preparing or full)"); return LCE_E_BACKEND; }
    }
    if (p->recipe_count && b->seams.recipe_provider_register(LCE195_SDK_PROVIDER, b, recipe_provider) != 1) {
        // The items provider is already registered (process lifetime): uncertain, not clean.
        *partial = b->item_count != 0;
        b->state = LCE195_SDK_REFUSED;
        set_err(err, cap, "retail195: recipe provider registration refused");
        return LCE_E_BACKEND;
    }
    return LCE_OK;
}

}  // namespace

extern "C" lce_status lce195_backend_check_item(void *self, const lce_item_desc *d, char *err, uint32_t cap) {
    lce195_backend *b = (lce195_backend *)self;
    char e[200] = {0};
    if (!d) { set_err(err, cap, "retail195: missing item descriptor"); return LCE_E_INVALID; }
    lce_item_desc checked = *d;
    if (b->xenv.allow_fuel && checked.fuel_ticks <= 72000) checked.fuel_ticks = 0;
    if (lce195x_item_check(&checked, e, sizeof e) != LCE_OK) { set_err(err, cap, e); return LCE_E_UNSUPPORTED; }
    const lce195x_natives &n = b->xenv.n;
    bool bound = d->kind == LCE_ITEM_GENERIC ? true
               : is_tool(d->kind) ? (d->kind == LCE_ITEM_HOE || (d->kind == LCE_ITEM_PICKAXE ? !!n.pickaxe_ctor : d->kind == LCE_ITEM_AXE ? !!n.axe_ctor : d->kind == LCE_ITEM_SHOVEL ? !!n.shovel_ctor : !!n.sword_ctor)) && n.set_max_damage
               : is_armour(d->kind) ? n.armour_ctor && n.set_max_damage
               : n.food_ctor != nullptr;
    if (!bound) { set_err(err, cap, "retail195: the class constructor for this item kind is not bound in this build"); return LCE_E_UNSUPPORTED; }
    if (d->kind == LCE_ITEM_FOOD && d->food_effect_count) {
        const char *fe = d->food_effects[0].effect;
        if (lce195x_stock_effect_id(fe) < 1 && !(b->mod_effect && fe && strncmp(fe, "minecraft:", 10) && strchr(fe, ':'))) {
            set_err(err, cap, "retail195: a food effect must be a stock 1.95 effect (minecraft:...): mod status effects are refused (items195 effects row)");
            return LCE_E_UNSUPPORTED;
        }
        if (!n.effect_by_id || !n.mei_ctor || !n.food_set_effect) { set_err(err, cap, "retail195: food effects need MobEffect natives (not bound)"); return LCE_E_UNSUPPORTED; }
    }
    return LCE_OK;
}

extern "C" lce_status lce195_backend_check_recipe(void *self, const lce_recipe_desc *d, char *err, uint32_t cap) {
    (void)self;
    if (!d->result_count || d->result_count > 64) { set_err(err, cap, "retail195: result count must be 1..64"); return LCE_E_INVALID; }
    if (d->result_aux < 0) { set_err(err, cap, "retail195: result data value must be 0..32767"); return LCE_E_INVALID; }
    if (d->width < 1 || d->width > 3 || d->height < 1 || d->height > 3) { set_err(err, cap, "retail195: recipe grid must be 1..3 x 1..3"); return LCE_E_INVALID; }
    unsigned used = 0;
    for (int c = 0; c < 9; ++c) {
        if (!d->cells[c].item) continue;
        ++used;
        if (d->cells[c].aux < -1) { set_err(err, cap, "retail195: ingredient data value must be -1 (any) or 0..32767"); return LCE_E_INVALID; }
    }
    if (!used) { set_err(err, cap, "retail195: recipe without ingredients"); return LCE_E_INVALID; }
    return LCE_OK;
}

extern "C" uintptr_t lce195_backend_item_object(const lce195_backend *b, uint32_t id) {
    if (!b) return 0;
    for (uint32_t i = 0; i < b->item_count; ++i)
        if (b->items[i].id == id && b->items[i].x.state == 2) return b->items[i].x.object;
    return 0;
}

extern "C" int lce195_backend_publish_late(lce195_backend *b) {
    if (!b || b->state != LCE195_SDK_ARMED || !b->plan) return 0;
    if (!b->mem.startup_held || b->mem.startup_held(b->mem.ctx) != 1) return 0;
    const lce_plan *p = b->plan;
    int result = 1;
    __atomic_store_n(&b->held_phase, 3u, __ATOMIC_RELEASE);
    if (b->smelt_count && !b->smelt_state) {
        b->smelt_state = 1;
        uintptr_t owner = 0;
        int rc = 1;
        if (!rd(b, b->base + LCE195_RVA_FURNACE_GLOBAL, &owner, 8) || !owner) { rc = 0; snprintf(b->last_error, sizeof b->last_error, "smelting: FurnaceRecipes singleton [0x1416a80] not constructed"); }
        for (uint32_t i = 0; rc == 1 && i < p->smelt_count && i < LCE195_SDK_MAX_SMELTS; ++i)
            if (lce195x_smelt_from_plan(&b->xenv, &p->smelts[i], &b->smelts[i]) != 1) { rc = 0; snprintf(b->last_error, sizeof b->last_error, "smelting %s: item unresolved", p->smelts[i].desc->key); }
        if (rc == 1) {
            memset(&g_furnace_pub, 0, sizeof g_furnace_pub);
            g_furnace_pub.env = &b->xenv; g_furnace_pub.owner = owner;
            rc = lce195x_furnace_publish(&g_furnace_pub, b->smelts, p->smelt_count);
            if (rc != 1) snprintf(b->last_error, sizeof b->last_error, "smelting: furnace publisher %s", rc ? "uncertain" : "refused");
        }
        b->smelt_state = rc == 1 ? LCE195_SDK_RECIPES_DONE : rc == 0 ? LCE195_SDK_REFUSED : LCE195_SDK_UNCERTAIN;
        if (rc != 1) result = rc < 0 ? -1 : 0;
    }
    if (b->brew_count && !b->brew_state) {
        b->brew_state = 1;
        uint32_t k = 0;
        int rc = 1;
        for (uint32_t i = 0; rc == 1 && i < p->ext_count; ++i) {
            if (p->ext[i].type != LCE_EXT_BREW) continue;
            if (k >= LCE195_SDK_MAX_BREWS || lce195x_brew_from_plan(&b->xenv, p, &p->ext[i], &b->brews[k]) != 1) { rc = 0; snprintf(b->last_error, sizeof b->last_error, "brewing %s: reagent/potion unresolved", p->ext[i].key); break; }
            ++k;
        }
        if (rc == 1 && k) {
            memset(&g_brew_pub, 0, sizeof g_brew_pub);
            g_brew_pub.env = &b->xenv;
            rc = lce195x_brew_publish(&g_brew_pub, b->brews, k);
            if (rc != 1) snprintf(b->last_error, sizeof b->last_error, "brewing: mix publisher %s", rc ? "uncertain" : "refused");
        }
        b->brew_state = rc == 1 ? LCE195_SDK_RECIPES_DONE : rc == 0 ? LCE195_SDK_REFUSED : LCE195_SDK_UNCERTAIN;
        if (rc != 1 && result >= 0) result = rc < 0 ? -1 : 0;
    }
    __atomic_store_n(&b->held_phase, 0u, __ATOMIC_RELEASE);
    return result;
}

extern "C" void *lce195_sdk_name_dispatch(void *out, void *item, void *stack) {
    lce195_backend *b = __atomic_load_n(&g_active, __ATOMIC_ACQUIRE);
    if (!b || !out) return nullptr;
    for (uint32_t i = 0; i < b->item_count; ++i) {
        const lce195_sdk_item &it = b->items[i];
        if (it.x.object != (uintptr_t)item || !it.x.object) continue;
        // Initialise the out wstring like lce195_names_dispatch does before the native ctor.
        *(uint16_t *)((uintptr_t)out + 8) = 0;
        *(uint64_t *)((uintptr_t)out + 0x18) = 0;
        *(uint64_t *)((uintptr_t)out + 0x20) = 7;
        b->natives.wstring(out, it.name, it.name_len);
        return out;
    }
    return b->natives.name_original(out, item, stack);
}

extern "C" uint32_t lce195_backend_caps(const lce195_backend *b) {
    if (!b) return 0;
    uint32_t c = LCE_CAP_ITEMS | LCE_CAP_TEXTURES | LCE_CAP_STRINGS | LCE_CAP_CREATIVE | LCE_CAP_CRAFT_TABS;
    const lce195x_natives &n = b->xenv.n;
    if (recipes_bound(b)) c |= LCE_CAP_RECIPES;
    if (n.set_max_damage && n.pickaxe_ctor && n.axe_ctor && n.shovel_ctor && n.sword_ctor) c |= LCE_CAP_TOOLS;
    if (n.set_max_damage && n.armour_ctor) c |= LCE_CAP_ARMOUR;
    if (n.food_ctor) c |= LCE_CAP_FOOD;
    if (b->mem.startup_held && n.alloc && n.stack_ctor && n.furnace_map[0] && n.furnace_map[1] && n.furnace_map[2]) c |= LCE_CAP_SMELTING;
    if (b->mem.startup_held && n.alloc && n.potion_by_id && n.mix_vec_grow && n.item_by_id) c |= LCE_CAP_BREWING;
    // Never FUEL, EFFECTS or ENCHANTS: items195 proves them impossible as data (STATUS.md).
    return c & (lce195x_capabilities() | LCE_CAP_ITEMS | LCE_CAP_TEXTURES | LCE_CAP_STRINGS | LCE_CAP_CREATIVE | LCE_CAP_CRAFT_TABS);
}

extern "C" void lce195_backend_bind_items(lce195_backend *b, const lce195x_natives *n) {
    if (!b || !n) return;
    lce195x_natives &x = b->xenv.n;
#define TAKE(f) if (n->f) x.f = n->f
    TAKE(alloc); TAKE(alloc_array); TAKE(wstring); TAKE(item_by_id); TAKE(item_ctor); TAKE(set_max_damage);
    TAKE(pickaxe_ctor); TAKE(axe_ctor); TAKE(shovel_ctor); TAKE(sword_ctor); TAKE(armour_ctor); TAKE(food_ctor); TAKE(food_set_effect); TAKE(mei_ctor);
    TAKE(stack_ctor); TAKE(shaped_ctor); TAKE(shapeless_ctor); TAKE(output_key); TAKE(recipe_bucket); TAKE(recipe_vec_grow); TAKE(stack_vec_grow);
    TAKE(furnace_map[0]); TAKE(furnace_map[1]); TAKE(furnace_map[2]); TAKE(effect_by_id); TAKE(potion_by_id); TAKE(mix_vec_grow);
#undef TAKE
    b->iface.capabilities = lce195_backend_caps(b);
}

extern "C" void lce195_backend_set_use_route(lce195_backend *b, const lce195_use_route *r) {
    if (b && r) b->use_route = *r;
}

extern "C" void lce195_backend_init(lce195_backend *b, uintptr_t base, const lce195_natives *n, const lce195_seams *s, const lce195_memory *m) {
    memset(b, 0, sizeof *b);
    b->base = base;
    if (n) b->natives = *n;
    if (s) b->seams = *s;
    if (m) b->mem = *m;
    b->xenv.base = base;
    b->xenv.n.alloc = b->natives.alloc;
    b->xenv.n.item_ctor = b->natives.item_ctor;
    b->xenv.n.wstring = b->natives.wstring;
    b->xenv.n.item_by_id = b->natives.item_by_id;
    b->xenv.m.ctx = b; b->xenv.m.read = xread; b->xenv.m.write = xwrite; b->xenv.m.held = xheld;
    b->iface.name = "retail195";
    b->iface.self = b;
    b->iface.resolve_group = resolve_group;
    b->iface.check_item = lce195_backend_check_item;
    b->iface.check_recipe = lce195_backend_check_recipe;
    b->iface.prepare = prepare;
    b->iface.publish = publish;
    b->iface.capabilities = lce195_backend_caps(b);
}

extern "C" uint32_t lce195_backend_status(const lce195_backend *b) {
    if (!b) return LCE195_SDK_IDLE;
    if (b->items_state == LCE195_SDK_UNCERTAIN || b->recipes_state == LCE195_SDK_UNCERTAIN) return LCE195_SDK_UNCERTAIN;
    if (b->items_state == LCE195_SDK_REFUSED || b->recipes_state == LCE195_SDK_REFUSED || b->state == LCE195_SDK_REFUSED) return LCE195_SDK_REFUSED;
    if (b->recipes_state == LCE195_SDK_RECIPES_DONE) return LCE195_SDK_RECIPES_DONE;
    if (b->items_state == LCE195_SDK_ITEMS_DONE) return LCE195_SDK_ITEMS_DONE;
    return b->state;
}

#ifndef LCE195_HOST_TEST
extern "C" void lce195_natives_bind(lce195_natives *n, uintptr_t base) {
    n->alloc = (void *(*)(size_t))(base + LCE195_RVA_ALLOC);
    n->item_ctor = (void (*)(void *))(base + LCE195_RVA_ITEM_CTOR);
    n->wstring = (void (*)(void *, const uint16_t *, uint64_t))(base + LCE195_RVA_WSTRING);
    n->item_by_id = (uintptr_t(*)(unsigned))(base + LCE195_RVA_ITEM_BY_ID);
    n->name_original = (void *(*)(void *, void *, void *))(base + LCE195_RVA_ITEM_NAME);
}
#endif
