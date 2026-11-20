// LCE mod loader core. See loader.h for the contract.
#include "loader.h"
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "package.h"
#include "ext.h"
#include "config_store.h"

struct lce_mod_ctx {
    lce_loader *loader;
    uint32_t index;
};

namespace {

struct VanillaRow { const char *name; int32_t item; int32_t block; };
const VanillaRow kVanilla[] = {
#include "vanilla_195.inc"
};
const uint32_t kVanillaCount = sizeof kVanilla / sizeof kVanilla[0];

void copy(char *dst, size_t cap, const char *src) {
    if (!src) src = "";
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
}
bool fits(const char *s, size_t cap) { return s && strlen(s) < cap; }

struct ModRec {
    lce_mod_report rep;
    uint8_t *owned_pkg;           // malloc'd buffer from discovery, or NULL
    const uint8_t *pkg; size_t pkg_size;
    lce_package_view view;
    bool has_pkg;
    lce_mod_main_fn native;
    char deps[16][LCE_MAX_MODID]; char dep_ver[16][16]; uint32_t dep_count;
    lce_mod_ctx ctx;
    lce_cfg_store *config;        // API 1.8: parsed <root>/<modid>/config (malloc'd), NULL = none
    bool accepting;               // register_* allowed
    bool active() const { return rep.state == LCE_MOD_STAGED || rep.state == LCE_MOD_LIVE; }
};

struct STex { lce_texture_desc d; char key[LCE_MAX_KEY]; uint8_t *rgba; uint32_t mod; };
struct SItem { lce_item_desc d; char key[LCE_MAX_KEY], name[LCE_MAX_TEXT], tex[LCE_MAX_KEY], tab[LCE_MAX_KEY], armor_tex[LCE_MAX_KEY], repair[LCE_MAX_KEY]; uint32_t mod; uint32_t id; };
struct SBlock { lce_block_desc d; char key[LCE_MAX_KEY], name[LCE_MAX_TEXT], tex[6][LCE_MAX_KEY], drop[LCE_MAX_KEY], tab[LCE_MAX_KEY], tool[16]; uint32_t mod; uint32_t id, item_id; };
struct SRecipe { lce_recipe_desc d; char key[LCE_MAX_KEY], cell[9][LCE_MAX_KEY], result[LCE_MAX_KEY], tab[LCE_MAX_KEY]; uint32_t mod; };
struct SSmelt { lce_smelt_desc d; char key[LCE_MAX_KEY], input[LCE_MAX_KEY], result[LCE_MAX_KEY]; uint32_t mod; };
struct STab { lce_tab_desc d; char key[LCE_MAX_KEY], name[LCE_MAX_TEXT], icon[LCE_MAX_KEY]; uint32_t mod; };
struct SString { char locale[16], key[LCE_MAX_KEY], value[256]; uint32_t mod; };
struct Sub { uint32_t mod; lce_event ev; lce_event_fn fn; void *user; };
// API 1.1 content record: deep copy of a schema-described descriptor.
struct SExt { uint32_t type, mod; alignas(8) uint8_t desc[320]; char key[LCE_MAX_KEY]; int32_t id; };
#define LCE_LOADER_MAX_EXT 1024
#define LCE_LOADER_MAX_ORPHANS 64

// Placeholder descriptors for orphaned ledger IDs.
struct SPlaceholder { lce_item_desc item; lce_block_desc block; char key[LCE_MAX_KEY]; char name[LCE_MAX_TEXT]; };

}  // namespace

struct lce_loader {
    lce_loader_config cfg;
    lce_phase phase;
    lce_api api;
    lce_ledger ledger;
    ModRec mods[LCE_LOADER_MAX_MODS]; uint32_t mod_count;
    STex tex[LCE_LOADER_MAX_TEXTURES]; uint32_t tex_count;
    SItem items[LCE_LOADER_MAX_ITEMS]; uint32_t item_count;
    SBlock blocks[LCE_LOADER_MAX_BLOCKS]; uint32_t block_count;
    SRecipe recipes[LCE_LOADER_MAX_RECIPES]; uint32_t recipe_count;
    SSmelt smelts[LCE_LOADER_MAX_SMELTS]; uint32_t smelt_count;
    STab tabs[LCE_LOADER_MAX_TABS]; uint32_t tab_count;
    SString strings[LCE_LOADER_MAX_STRINGS]; uint32_t string_count;
    Sub subs[LCE_LOADER_MAX_SUBS]; uint32_t sub_count;
    struct { uint32_t mod; char name[24]; } channels[64]; uint32_t channel_count;   // API 1.4
    SPlaceholder placeholders[LCE_LEDGER_MAX]; uint32_t placeholder_count;
    SExt ext[LCE_LOADER_MAX_EXT]; uint32_t ext_count;
    lce_plan_ext p_ext[LCE_LOADER_MAX_EXT + LCE_LOADER_MAX_ORPHANS];
    char orphan_keys[LCE_LOADER_MAX_ORPHANS][LCE_MAX_KEY]; int32_t orphan_ids[LCE_LOADER_MAX_ORPHANS]; uint32_t orphan_count;
    void *alloc_head;   // chain of deep-copy allocations (process lifetime once published)
    // plan storage
    lce_plan plan;
    lce_plan_texture p_tex[LCE_LOADER_MAX_TEXTURES];
    lce_plan_tab p_tabs[LCE_LOADER_MAX_TABS];
    lce_plan_block p_blocks[LCE_LOADER_MAX_BLOCKS + LCE_LEDGER_MAX];
    lce_plan_item p_items[LCE_LOADER_MAX_ITEMS + LCE_LEDGER_MAX];
    lce_plan_recipe p_recipes[LCE_LOADER_MAX_RECIPES];
    lce_plan_smelt p_smelts[LCE_LOADER_MAX_SMELTS];
    lce_plan_string p_strings[LCE_LOADER_MAX_STRINGS];
    bool plan_ready;
    char ledger_text[LCE_LEDGER_MAX * 96 + 128];
    lce_script_runner_fn script_runner; void *script_user;
};

namespace {

void logf(lce_loader *l, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void logf(lce_loader *l, const char *fmt, ...) {
    if (!l->cfg.host.log) return;
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    l->cfg.host.log(l->cfg.host.user, line);
}

// Loader-owned allocation (deep copies of 1.1 descriptors, texture pixels).
void *lalloc(lce_loader *l, size_t n) {
    void **p = (void **)calloc(1, sizeof(void *) * 2 + n);
    if (!p) return nullptr;
    p[0] = l->alloc_head;
    l->alloc_head = p;
    return p + 2;
}
char *lstrdup(lce_loader *l, const char *s) {
    size_t n = strlen(s);
    char *d = (char *)lalloc(l, n + 1);
    if (d) memcpy(d, s, n + 1);
    return d;
}

void reject(lce_loader *l, ModRec &m, lce_mod_state st, lce_status code, const char *reason) {
    if (m.rep.state == LCE_MOD_REJECTED || m.rep.state == LCE_MOD_DROPPED) return;
    m.rep.state = st;
    m.rep.status = code;
    copy(m.rep.reason, sizeof m.rep.reason, reason);
    logf(l, "lce-mods: %s %s: %s (%d)", st == LCE_MOD_REJECTED ? "rejected" : "dropped", m.rep.modid, reason, (int)code);
}

// ---- key helpers -------------------------------------------------------
bool owned_by(const char *key, const char *modid) {
    size_t n = strlen(modid);
    return !strncmp(key, modid, n) && key[n] == ':';
}
bool is_vanilla(const char *key) { return !strncmp(key, "minecraft:", 10); }

int32_t vanilla_item(const char *key) {  // "minecraft:name" or "minecraft:#123"
    const char *name = key + 10;
    if (name[0] == '#') {
        char *end = nullptr;
        long v = strtol(name + 1, &end, 10);
        if (!end || *end || v <= 0 || v > 32767 || !lce_vanilla_item_exists((uint32_t)v)) return -1;
        return (int32_t)v;
    }
    return lce_vanilla_item_id(name);
}

const SItem *find_item(const lce_loader *l, const char *key) {
    for (uint32_t i = 0; i < l->item_count; ++i) if (!strcmp(l->items[i].key, key)) return &l->items[i];
    return nullptr;
}
const SBlock *find_block(const lce_loader *l, const char *key) {
    for (uint32_t i = 0; i < l->block_count; ++i) if (!strcmp(l->blocks[i].key, key)) return &l->blocks[i];
    return nullptr;
}
int find_tex(const lce_loader *l, const char *key) {
    for (uint32_t i = 0; i < l->tex_count; ++i) if (!strcmp(l->tex[i].key, key)) return (int)i;
    return -1;
}
int find_tab(const lce_loader *l, const char *key) {
    for (uint32_t i = 0; i < l->tab_count; ++i) if (!strcmp(l->tabs[i].key, key)) return (int)i;
    return -1;
}
bool mod_active(const lce_loader *l, uint32_t idx) { return idx < l->mod_count && l->mods[idx].active(); }

// Does `key` name an item that will exist (vanilla, or an active mod item / block-item)?
bool item_ref_ok(const lce_loader *l, const char *key) {
    if (!key || !*key) return false;
    if (is_vanilla(key)) return vanilla_item(key) >= 0;
    if (const SItem *it = find_item(l, key)) return mod_active(l, it->mod);
    if (const SBlock *b = find_block(l, key)) return mod_active(l, b->mod) && !(b->d.flags & LCE_BLOCK_FLAG_NO_ITEM);
    return false;
}
// Numeric item ID after claim (or -1).
int32_t item_ref_id(const lce_loader *l, const char *key) {
    if (is_vanilla(key)) return vanilla_item(key);
    if (const SItem *it = find_item(l, key)) return mod_active(l, it->mod) && it->id ? (int32_t)it->id : -1;
    if (const SBlock *b = find_block(l, key)) return mod_active(l, b->mod) && b->item_id ? (int32_t)b->item_id : -1;
    return -1;
}

lce_status check_phase(lce_mod_ctx *ctx) {
    if (!ctx || !ctx->loader) return LCE_E_INVALID;
    ModRec &m = ctx->loader->mods[ctx->index];
    if (!m.accepting || (ctx->loader->phase != LCE_PHASE_INIT && ctx->loader->phase != LCE_PHASE_REGISTER)) return LCE_E_PHASE;
    return LCE_OK;
}
// Common descriptor guard: struct_size covers the fields we rely on.
template <class T> bool size_ok(const T *d, size_t min) { return d && d->struct_size >= min && d->struct_size <= 4096; }

// Reads a descriptor into a zeroed local copy honouring struct_size (forward compat).
template <class T> T read_desc(const T *d) {
    T out;
    memset(&out, 0, sizeof out);
    memcpy(&out, d, d->struct_size < sizeof(T) ? d->struct_size : sizeof(T));
    return out;
}

bool key_taken_anywhere(const lce_loader *l, const char *key) {
    return find_item(l, key) || find_block(l, key);
}

// ---- API implementation ------------------------------------------------
void api_log(lce_mod_ctx *ctx, lce_log_level level, const char *msg) {
    if (!ctx || !ctx->loader || !msg) return;
    static const char *names[] = {"debug", "info", "warn", "error"};
    logf(ctx->loader, "lce-mods: [%s] %s: %.180s", names[(unsigned)level < 4 ? level : 3], ctx->loader->mods[ctx->index].rep.modid, msg);
}
lce_phase api_phase(lce_mod_ctx *ctx) { return ctx && ctx->loader ? ctx->loader->phase : LCE_PHASE_NONE; }

// API 1.8 config: read-only after DISCOVER, so any thread may call these.
const lce_cfg_store *cfg_of(lce_mod_ctx *ctx) { return ctx && ctx->loader && ctx->index < ctx->loader->mod_count ? ctx->loader->mods[ctx->index].config : nullptr; }
lce_status api_config_get(lce_mod_ctx *ctx, const char *key, lce_nbt_value *out) {
    if (!ctx || !key || !out) return LCE_E_INVALID;
    const lce_cfg_entry *e = lce_cfg_find(cfg_of(ctx), key);
    if (!e) return LCE_E_NOTFOUND;
    *out = e->v;
    return LCE_OK;
}
int64_t api_config_int(lce_mod_ctx *ctx, const char *key, int64_t def, int64_t min, int64_t max) { return lce_cfg_int(cfg_of(ctx), key, def, min, max); }
double api_config_double(lce_mod_ctx *ctx, const char *key, double def, double min, double max) { return lce_cfg_double(cfg_of(ctx), key, def, min, max); }
const char *api_config_string(lce_mod_ctx *ctx, const char *key, const char *def) { return lce_cfg_string(cfg_of(ctx), key, def); }
// Parses config text for mod m (DISCOVER only). Never fails the mod: problems are logged and the mod sees its defaults.
void attach_config(lce_loader *l, ModRec &m, const char *text, size_t len) {
    if (!m.config) m.config = (lce_cfg_store *)malloc(sizeof(lce_cfg_store));
    if (!m.config) { logf(l, "lce-mods: %s: config: out of memory, using defaults", m.rep.modid); return; }
    lce_cfg_parse(m.config, text, len);
    logf(l, "lce-mods: %s: config: %u value(s)", m.rep.modid, (unsigned)m.config->count);
    if (m.config->bad_lines)
        logf(l, "lce-mods: [warn] %s: config: %u line(s) skipped, first at line %u", m.rep.modid, (unsigned)m.config->bad_lines, (unsigned)m.config->first_bad_line);
    if (m.config->duplicates) logf(l, "lce-mods: [warn] %s: config: %u repeated key(s), first value kept", m.rep.modid, (unsigned)m.config->duplicates);
}

#define MODOF(ctx) (ctx->loader->mods[ctx->index])
#define REQUIRE_OWN_KEY(ctx, k) do { if (!lce_valid_key(k) || !owned_by(k, MODOF(ctx).rep.modid)) return LCE_E_INVALID; } while (0)

lce_status api_register_texture(lce_mod_ctx *ctx, const lce_texture_desc *in) {
    lce_status st = check_phase(ctx); if (st) return st;
    if (!size_ok(in, offsetof(lce_texture_desc, reserved))) return LCE_E_INVALID;
    lce_texture_desc d = read_desc(in);
    lce_loader *l = ctx->loader;
    REQUIRE_OWN_KEY(ctx, d.key);
    // atlas 0 items 16x16, 1 terrain 16x16, 2 armour layer 64x32, 3 entity skin 64x32/64x64, 4 effect/particle icon 16x16
    bool size_ok = (d.atlas <= 1 || d.atlas == 4) ? (d.width == 16 && d.height == 16)
                 : d.atlas == 2 ? (d.width == 64 && d.height == 32)
                 : d.atlas == 3 ? (d.width == 64 && (d.height == 32 || d.height == 64)) : false;
    if (!d.rgba || !size_ok) return LCE_E_UNSUPPORTED;
    if (find_tex(l, d.key) >= 0) return LCE_E_COLLISION;
    if (l->tex_count >= LCE_LOADER_MAX_TEXTURES) return LCE_E_FULL;
    STex &t = l->tex[l->tex_count];
    memset(&t, 0, sizeof t);
    copy(t.key, sizeof t.key, d.key);
    t.rgba = (uint8_t *)lalloc(l, (size_t)d.width * d.height * 4);
    if (!t.rgba) return LCE_E_FULL;
    memcpy(t.rgba, d.rgba, (size_t)d.width * d.height * 4);
    t.d = d; t.d.struct_size = sizeof t.d; t.d.key = t.key; t.d.rgba = t.rgba; t.mod = ctx->index;
    ++l->tex_count; ++MODOF(ctx).rep.textures;
    return LCE_OK;
}

lce_status api_register_item(lce_mod_ctx *ctx, const lce_item_desc *in) {
    lce_status st = check_phase(ctx); if (st) return st;
    if (!size_ok(in, offsetof(lce_item_desc, flags))) return LCE_E_INVALID;
    lce_item_desc d = read_desc(in);
    lce_loader *l = ctx->loader;
    REQUIRE_OWN_KEY(ctx, d.key);
    if (!fits(d.name, LCE_MAX_TEXT) || !*d.name || !d.texture || !lce_valid_key(d.texture)) return LCE_E_INVALID;
    if (d.creative_tab && (!lce_valid_key(d.creative_tab))) return LCE_E_INVALID;
    if (d.kind >= LCE_ITEM_KIND_COUNT || d.max_stack > 64 || d.tier > LCE_TIER_GOLD) return LCE_E_INVALID;
    bool tool = d.kind >= LCE_ITEM_SWORD;
    if (tool && (d.max_damage == 0 || (d.max_stack && d.max_stack != 1))) return LCE_E_INVALID;
    if (d.kind == LCE_ITEM_FOOD && d.food_hunger == 0) return LCE_E_INVALID;
    if (key_taken_anywhere(l, d.key)) return LCE_E_COLLISION;
    if (l->item_count >= LCE_LOADER_MAX_ITEMS) return LCE_E_FULL;
    SItem &s = l->items[l->item_count];
    memset(&s, 0, sizeof s);
    copy(s.key, sizeof s.key, d.key); copy(s.name, sizeof s.name, d.name);
    copy(s.tex, sizeof s.tex, d.texture); copy(s.tab, sizeof s.tab, d.creative_tab);
    s.d = d; s.d.struct_size = sizeof s.d; s.d.key = s.key; s.d.name = s.name; s.d.texture = s.tex;
    s.d.creative_tab = s.tab[0] ? s.tab : nullptr;
    if (!s.d.max_stack) s.d.max_stack = tool ? 1 : 64;
    // API 1.1 fields
    if ((d.armor_texture && !lce_valid_key(d.armor_texture)) || (d.repair_item && !lce_valid_key(d.repair_item))) return LCE_E_INVALID;
    copy(s.armor_tex, sizeof s.armor_tex, d.armor_texture); copy(s.repair, sizeof s.repair, d.repair_item);
    s.d.armor_texture = s.armor_tex[0] ? s.armor_tex : nullptr;
    s.d.repair_item = s.repair[0] ? s.repair : nullptr;
    bool armour = d.kind >= LCE_ITEM_HELMET;
    if (armour && (!d.armor_points || d.armor_points > 20)) return LCE_E_INVALID;
    if (d.food_effect_count > 8 || (d.food_effect_count && !d.food_effects)) return LCE_E_INVALID;
    if (d.food_effect_count) {
        lce_effect_ref *fx = (lce_effect_ref *)lalloc(l, sizeof(lce_effect_ref) * d.food_effect_count);
        if (!fx) return LCE_E_FULL;
        for (uint32_t i = 0; i < d.food_effect_count; ++i) {
            if (!d.food_effects[i].effect || !lce_valid_key(d.food_effects[i].effect) || d.food_effects[i].chance_percent > 100) return LCE_E_INVALID;
            fx[i] = d.food_effects[i];
            fx[i].effect = lstrdup(l, d.food_effects[i].effect);
        }
        s.d.food_effects = fx;
    } else {
        s.d.food_effects = nullptr;
    }
    // API 1.9 throwables (read_desc zero-fills older descriptors)
    const bool thrown = (d.flags & LCE_ITEM_FLAG_THROWABLE) != 0;
    if (!thrown && (d.projectile_speed_milli || d.projectile_inaccuracy_milli || d.projectile_damage_milli || d.projectile_fire_ticks ||
                    d.projectile_explode_milli || d.projectile_effect_count || d.projectile_effects)) return LCE_E_INVALID;
    if (d.projectile_speed_milli > 10000 || d.projectile_inaccuracy_milli > 50000 || d.projectile_damage_milli > 1000000 ||
        d.projectile_fire_ticks > 20u * 60 * 10 || d.projectile_explode_milli > 8000 || d.projectile_effect_count > 8 ||
        (d.projectile_effect_count && !d.projectile_effects)) return LCE_E_INVALID;
    // API 1.10 boat items: a generic, non-throwable item with a stock boat type
    const bool boat = (d.flags & LCE_ITEM_FLAG_BOAT) != 0;
    if (boat ? (d.kind != LCE_ITEM_GENERIC || thrown || d.boat_type > 5) : d.boat_type != 0) return LCE_E_INVALID;
    s.d.projectile_effects = nullptr;
    if (d.projectile_effect_count) {
        lce_effect_ref *px = (lce_effect_ref *)lalloc(l, sizeof(lce_effect_ref) * d.projectile_effect_count);
        if (!px) return LCE_E_FULL;
        for (uint32_t i = 0; i < d.projectile_effect_count; ++i) {
            if (!d.projectile_effects[i].effect || !lce_valid_key(d.projectile_effects[i].effect) || d.projectile_effects[i].chance_percent > 100) return LCE_E_INVALID;
            px[i] = d.projectile_effects[i];
            px[i].effect = lstrdup(l, d.projectile_effects[i].effect);
        }
        s.d.projectile_effects = px;
    }
    s.mod = ctx->index;
    ++l->item_count; ++MODOF(ctx).rep.items;
    return LCE_OK;
}

lce_status api_register_block(lce_mod_ctx *ctx, const lce_block_desc *in) {
    lce_status st = check_phase(ctx); if (st) return st;
    if (!size_ok(in, offsetof(lce_block_desc, flags))) return LCE_E_INVALID;
    lce_block_desc d = read_desc(in);
    lce_loader *l = ctx->loader;
    REQUIRE_OWN_KEY(ctx, d.key);
    if (!fits(d.name, LCE_MAX_TEXT) || !*d.name || !d.texture[0] || !lce_valid_key(d.texture[0])) return LCE_E_INVALID;
    for (int f = 1; f < 6; ++f) if (d.texture[f] && !lce_valid_key(d.texture[f])) return LCE_E_INVALID;
    if ((d.drop && !lce_valid_key(d.drop)) || (d.creative_tab && !lce_valid_key(d.creative_tab))) return LCE_E_INVALID;
    if (d.material >= LCE_MAT_COUNT || d.sound >= LCE_SOUND_COUNT || d.light_emission > 15 || d.harvest_tier > LCE_TIER_GOLD) return LCE_E_INVALID;
    if (d.drop_min > d.drop_max || d.drop_max > 64) return LCE_E_INVALID;
    if (d.hardness_milli > 100000000u || d.resistance_milli > 100000000u) return LCE_E_INVALID;
    if (key_taken_anywhere(l, d.key)) return LCE_E_COLLISION;
    if (l->block_count >= LCE_LOADER_MAX_BLOCKS) return LCE_E_FULL;
    SBlock &s = l->blocks[l->block_count];
    memset(&s, 0, sizeof s);
    copy(s.key, sizeof s.key, d.key); copy(s.name, sizeof s.name, d.name);
    for (int f = 0; f < 6; ++f) copy(s.tex[f], sizeof s.tex[f], d.texture[f] ? d.texture[f] : d.texture[0]);
    copy(s.drop, sizeof s.drop, d.drop ? d.drop : d.key);
    copy(s.tab, sizeof s.tab, d.creative_tab);
    s.d = d; s.d.struct_size = sizeof s.d; s.d.key = s.key; s.d.name = s.name;
    for (int f = 0; f < 6; ++f) s.d.texture[f] = s.tex[f];
    s.d.drop = s.drop; s.d.creative_tab = s.tab[0] ? s.tab : nullptr;
    if (!s.d.drop_max) s.d.drop_min = s.d.drop_max = 1;
    if (d.harvest_tool) {
        static const char *tools[] = {"pickaxe", "axe", "shovel", "hoe", "shears", "sword"};
        bool ok = false;
        for (const char *t : tools) ok |= !strcmp(t, d.harvest_tool);
        if (!ok) return LCE_E_INVALID;
        copy(s.tool, sizeof s.tool, d.harvest_tool);
        s.d.harvest_tool = s.tool;
    }
    if (d.placement > LCE_PLACE_AXIS || d.block_entity_slots > 54 || d.slipperiness_milli > 1000) return LCE_E_INVALID;
    if ((s.d.flags & LCE_BLOCK_FLAG_NO_ITEM) && !strcmp(s.drop, s.key)) return LCE_E_INVALID;  // would drop a non-existent item
    // API 1.7 variants / box shape (read_desc zero-fills them for older descriptors)
    if (d.variant_count > 16 || d.shape > LCE_SHAPE_BOXES) return LCE_E_INVALID;
    // API 1.11 multi-box: 1..8 boxes, one set or one per variant; deep copy
    s.d.boxes = nullptr;
    if (d.shape == LCE_SHAPE_BOXES) {
        if (!d.boxes || d.box_count < 1 || d.box_count > 8 || d.reserved3 || (d.box_sets != 1 && !(d.variant_count >= 2 && d.box_sets == d.variant_count))) return LCE_E_INVALID;
        const uint32_t n = 6u * d.box_count * d.box_sets;
        for (uint32_t k = 0; k < n; k += 6)
            for (int a = 0; a < 3; ++a) if (d.boxes[k + a] >= d.boxes[k + a + 3] || d.boxes[k + a + 3] > 16) return LCE_E_INVALID;
        uint8_t *bx = (uint8_t *)lalloc(l, n);
        if (!bx) return LCE_E_FULL;
        memcpy(bx, d.boxes, n);
        s.d.boxes = bx;
    } else if (d.box_count || d.box_sets || d.reserved3 || d.boxes) return LCE_E_INVALID;
    if (d.shape == LCE_SHAPE_BOXES) {
        if (d.box[0] | d.box[1] | d.box[2] | d.box[3] | d.box[4] | d.box[5]) return LCE_E_INVALID;   // the union is derived
    } else if (d.shape == LCE_SHAPE_BOX) {
        for (int a = 0; a < 3; ++a) if (d.box[a] >= d.box[a + 3] || d.box[a + 3] > 16) return LCE_E_INVALID;
    } else if (d.box[0] | d.box[1] | d.box[2] | d.box[3] | d.box[4] | d.box[5]) return LCE_E_INVALID;
    s.d.variant_names = nullptr; s.d.variant_textures = nullptr;
    if (d.variant_count < 2 && (d.variant_names || d.variant_textures)) return LCE_E_INVALID;
    if (d.variant_count >= 2 && d.variant_names) {
        const char **vn = (const char **)lalloc(l, sizeof(char *) * d.variant_count);
        if (!vn) return LCE_E_FULL;
        for (uint32_t k = 1; k < d.variant_count; ++k)
            if (d.variant_names[k]) { if (!fits(d.variant_names[k], LCE_MAX_TEXT) || !*d.variant_names[k] || !(vn[k] = lstrdup(l, d.variant_names[k]))) return LCE_E_INVALID; }
        s.d.variant_names = vn;
    }
    if (d.variant_count >= 2 && d.variant_textures) {
        const char **vt = (const char **)lalloc(l, sizeof(char *) * 6u * d.variant_count);
        if (!vt) return LCE_E_FULL;
        for (uint32_t k = 0; k < 6u * d.variant_count; ++k) {
            const char *t = d.variant_textures[k];
            if (!t) continue;
            if (k < 6 || !lce_valid_key(t) || !(vt[k] = lstrdup(l, t))) return LCE_E_INVALID;
        }
        s.d.variant_textures = vt;
    }
    s.mod = ctx->index;
    ++l->block_count; ++MODOF(ctx).rep.blocks;
    return LCE_OK;
}

lce_status api_register_recipe(lce_mod_ctx *ctx, const lce_recipe_desc *in) {
    lce_status st = check_phase(ctx); if (st) return st;
    if (!size_ok(in, offsetof(lce_recipe_desc, craft_tab))) return LCE_E_INVALID;
    lce_recipe_desc d = read_desc(in);
    lce_loader *l = ctx->loader;
    REQUIRE_OWN_KEY(ctx, d.key);
    if (d.width < 1 || d.width > 3 || d.height < 1 || d.height > 3 || d.shapeless > 1 || d.reserved) return LCE_E_INVALID;
    if (!d.result || !lce_valid_key(d.result) || d.result_count < 1 || d.result_count > 64 || d.result_aux < 0) return LCE_E_INVALID;
    if (d.craft_tab && !lce_valid_key(d.craft_tab)) return LCE_E_INVALID;
    unsigned used = 0;
    for (int c = 0; c < 9; ++c) {
        if (!d.cells[c].item) continue;
        if (c % 3 >= d.width || c / 3 >= d.height) return LCE_E_INVALID;  // outside the declared grid (row-major 3-wide)
        if (!lce_valid_key(d.cells[c].item) || d.cells[c].aux < -1) return LCE_E_INVALID;
        ++used;
    }
    if (!used) return LCE_E_INVALID;
    for (uint32_t i = 0; i < l->recipe_count; ++i) if (!strcmp(l->recipes[i].key, d.key)) return LCE_E_COLLISION;
    if (l->recipe_count >= LCE_LOADER_MAX_RECIPES) return LCE_E_FULL;
    SRecipe &s = l->recipes[l->recipe_count];
    memset(&s, 0, sizeof s);
    copy(s.key, sizeof s.key, d.key); copy(s.result, sizeof s.result, d.result); copy(s.tab, sizeof s.tab, d.craft_tab);
    s.d = d; s.d.struct_size = sizeof s.d; s.d.key = s.key; s.d.result = s.result; s.d.craft_tab = s.tab[0] ? s.tab : nullptr;
    for (int c = 0; c < 9; ++c) {
        copy(s.cell[c], sizeof s.cell[c], d.cells[c].item);
        s.d.cells[c].item = d.cells[c].item ? s.cell[c] : nullptr;
    }
    s.mod = ctx->index;
    ++l->recipe_count; ++MODOF(ctx).rep.recipes;
    return LCE_OK;
}

lce_status api_register_smelting(lce_mod_ctx *ctx, const lce_smelt_desc *in) {
    lce_status st = check_phase(ctx); if (st) return st;
    if (!size_ok(in, offsetof(lce_smelt_desc, xp_milli))) return LCE_E_INVALID;
    lce_smelt_desc d = read_desc(in);
    lce_loader *l = ctx->loader;
    REQUIRE_OWN_KEY(ctx, d.key);
    if (!d.input.item || !lce_valid_key(d.input.item) || !d.result || !lce_valid_key(d.result)) return LCE_E_INVALID;
    if (d.result_count < 1 || d.result_count > 64 || d.input.aux < -1 || d.result_aux < 0 || d.xp_milli > 100000) return LCE_E_INVALID;
    for (uint32_t i = 0; i < l->smelt_count; ++i)
        if (!strcmp(l->smelts[i].key, d.key) || (!strcmp(l->smelts[i].input, d.input.item) && l->smelts[i].d.input.aux == d.input.aux)) return LCE_E_COLLISION;
    if (l->smelt_count >= LCE_LOADER_MAX_SMELTS) return LCE_E_FULL;
    SSmelt &s = l->smelts[l->smelt_count];
    memset(&s, 0, sizeof s);
    copy(s.key, sizeof s.key, d.key); copy(s.input, sizeof s.input, d.input.item); copy(s.result, sizeof s.result, d.result);
    s.d = d; s.d.struct_size = sizeof s.d; s.d.key = s.key; s.d.input.item = s.input; s.d.result = s.result;
    s.mod = ctx->index;
    ++l->smelt_count; ++MODOF(ctx).rep.smelts;
    return LCE_OK;
}

lce_status api_register_tab(lce_mod_ctx *ctx, const lce_tab_desc *in) {
    lce_status st = check_phase(ctx); if (st) return st;
    if (!size_ok(in, offsetof(lce_tab_desc, reserved))) return LCE_E_INVALID;
    lce_tab_desc d = read_desc(in);
    lce_loader *l = ctx->loader;
    REQUIRE_OWN_KEY(ctx, d.key);
    if (!fits(d.name, LCE_MAX_TEXT) || !*d.name || !d.icon_item || !lce_valid_key(d.icon_item) || (!d.creative && !d.crafting)) return LCE_E_INVALID;
    if (find_tab(l, d.key) >= 0) return LCE_E_COLLISION;
    if (l->tab_count >= LCE_LOADER_MAX_TABS) return LCE_E_FULL;
    STab &s = l->tabs[l->tab_count];
    memset(&s, 0, sizeof s);
    copy(s.key, sizeof s.key, d.key); copy(s.name, sizeof s.name, d.name); copy(s.icon, sizeof s.icon, d.icon_item);
    s.d = d; s.d.struct_size = sizeof s.d; s.d.key = s.key; s.d.name = s.name; s.d.icon_item = s.icon;
    s.mod = ctx->index;
    ++l->tab_count; ++MODOF(ctx).rep.tabs;
    return LCE_OK;
}

lce_status api_register_string(lce_mod_ctx *ctx, const lce_string_desc *in) {
    lce_status st = check_phase(ctx); if (st) return st;
    if (!size_ok(in, offsetof(lce_string_desc, value) + sizeof(void *))) return LCE_E_INVALID;
    lce_string_desc d = read_desc(in);
    lce_loader *l = ctx->loader;
    REQUIRE_OWN_KEY(ctx, d.key);  // mods may not override vanilla or other mods' strings
    if (!d.locale || !fits(d.locale, 16) || strlen(d.locale) < 2 || !d.value || !fits(d.value, 256)) return LCE_E_INVALID;
    for (uint32_t i = 0; i < l->string_count; ++i)
        if (!strcmp(l->strings[i].key, d.key) && !strcmp(l->strings[i].locale, d.locale)) return LCE_E_COLLISION;
    if (l->string_count >= LCE_LOADER_MAX_STRINGS) return LCE_E_FULL;
    SString &s = l->strings[l->string_count];
    copy(s.locale, sizeof s.locale, d.locale); copy(s.key, sizeof s.key, d.key); copy(s.value, sizeof s.value, d.value);
    s.mod = ctx->index;
    ++l->string_count; ++MODOF(ctx).rep.strings;
    return LCE_OK;
}

lce_status api_register_ext(lce_mod_ctx *ctx, uint32_t type, const void *in);
lce_status api_register_entity(lce_mod_ctx *ctx, const lce_entity_desc *in) {
    lce_status st = check_phase(ctx); if (st) return st;
    if (!size_ok(in, offsetof(lce_entity_desc, spawn_weight) + sizeof in->spawn_weight)) return LCE_E_INVALID;
    lce_entity_desc d = read_desc(in);
    // The legacy descriptor has no spawn category, so cannot describe spawning safely.
    if (d.spawn_weight) return LCE_E_UNSUPPORTED;
    lce_entity_desc2 out{};
    out.struct_size = sizeof out; out.key = d.key; out.base_mob = d.base_mob;
    out.texture = d.texture; out.max_health_milli = d.max_health_milli;
    return api_register_ext(ctx, LCE_EXT_ENTITY, &out);
}
lce_status api_register_ore(lce_mod_ctx *ctx, const lce_ore_desc *in) {
    lce_status st = check_phase(ctx); if (st) return st;
    if (!size_ok(in, offsetof(lce_ore_desc, dimension) + sizeof in->dimension)) return LCE_E_INVALID;
    lce_ore_desc d = read_desc(in);
    lce_feature_desc out{};
    out.struct_size = sizeof out; out.key = d.key; out.kind = LCE_FEATURE_ORE;
    out.dimension = d.dimension; out.block = d.block; out.block2 = d.replace;
    out.size = d.vein_size; out.per_chunk = d.veins_per_chunk;
    out.min_y = d.min_y; out.max_y = d.max_y;
    return api_register_ext(ctx, LCE_EXT_FEATURE, &out);
}


// ---- API 1.1 schema-driven content ----------------------------------------
// Vanilla 1.95 effect and base potion names accepted as references. Numeric
// mapping is backend business; a backend may still refuse one.
const char *const kVanillaEffects[] = {"speed", "slowness", "haste", "mining_fatigue", "strength", "instant_health", "instant_damage",
    "jump_boost", "nausea", "regeneration", "resistance", "fire_resistance", "water_breathing", "invisibility", "blindness",
    "night_vision", "hunger", "weakness", "poison", "wither", "health_boost", "absorption", "saturation", "glowing", "levitation",
    "conduit_power", "slow_falling", "dolphins_grace"};
const char *const kVanillaPotions[] = {"water", "mundane", "thick", "awkward", "night_vision", "invisibility", "leaping", "fire_resistance",
    "swiftness", "slowness", "water_breathing", "healing", "harming", "poison", "regeneration", "strength", "weakness", "turtle_master", "slow_falling"};
bool in_names(const char *const *list, size_t n, const char *name) { for (size_t i = 0; i < n; ++i) if (!strcmp(list[i], name)) return true; return false; }

bool name_ok(const char *s) {  // command/keybind names
    size_t n = s ? strlen(s) : 0;
    if (n < 1 || n > 31) return false;
    for (size_t i = 0; i < n; ++i) if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= '0' && s[i] <= '9') || s[i] == '_')) return false;
    return true;
}
bool text_ok(const char *s) {
    if (!s) return true;
    size_t n = strlen(s);
    if (n > 255) return false;
    for (size_t i = 0; i < n; ++i) if ((unsigned char)s[i] < 0x20) return false;
    return true;
}
template <class T> T &at(uint8_t *desc, uint32_t off) { return *(T *)(desc + off); }

lce_status ext_deep_copy(lce_loader *l, const lce_ext_schema *sc, const char *modid, uint8_t *dst, const void *src, char *key_out) {
    for (uint32_t f = 0; f < sc->field_count; ++f) {
        const lce_ext_field &fd = sc->fields[f];
        switch (fd.kind) {
        case LCE_F_KEY: {
            const char *k = at<const char *>(dst, fd.offset);
            if (!k || !lce_valid_key(k) || !owned_by(k, modid)) return LCE_E_INVALID;
            copy(key_out, LCE_MAX_KEY, k);
            at<const char *>(dst, fd.offset) = lstrdup(l, k);
            break;
        }
        case LCE_F_NAME: {
            const char *k = at<const char *>(dst, fd.offset);
            if (!name_ok(k)) return LCE_E_INVALID;
            snprintf(key_out, LCE_MAX_KEY, "%s:%s", modid, k);
            at<const char *>(dst, fd.offset) = lstrdup(l, k);
            break;
        }
        case LCE_F_TEXT: {
            const char *t = at<const char *>(dst, fd.offset);
            if (!text_ok(t)) return LCE_E_INVALID;
            if (t) at<const char *>(dst, fd.offset) = lstrdup(l, t);
            break;
        }
        case LCE_F_REF: case LCE_F_REF_REQ: {
            const char *r = at<const char *>(dst, fd.offset);
            if (!r) { if (fd.kind == LCE_F_REF_REQ) return LCE_E_INVALID; break; }
            if (!lce_valid_key(r)) return LCE_E_INVALID;
            at<const char *>(dst, fd.offset) = lstrdup(l, r);
            if (fd.kind == LCE_F_REF_REQ && sc == lce_ext_schema_of(LCE_EXT_FUEL) && !key_out[0]) snprintf(key_out, LCE_MAX_KEY, "%s:fuel/%.40s", modid, strchr(r, ':') + 1);
            break;
        }
        case LCE_F_U8: case LCE_F_U16: case LCE_F_U32: case LCE_F_PTR: break;
        case LCE_F_ARR_EFFECT: case LCE_F_ARR_GOAL: case LCE_F_ARR_DROP: case LCE_F_ARR_REF: case LCE_F_BLOB: {
            uint32_t n = at<uint32_t>(dst, fd.count_offset);
            const void *in = at<const void *>(dst, fd.offset);
            uint32_t limit = fd.kind == LCE_F_BLOB ? 4u << 20 : fd.kind == LCE_F_ARR_REF ? 256u : 16u;
            if (n > limit || (n && !in)) return LCE_E_INVALID;
            if (!n) { at<const void *>(dst, fd.offset) = nullptr; break; }
            size_t elem = fd.kind == LCE_F_ARR_EFFECT ? sizeof(lce_effect_ref) : fd.kind == LCE_F_ARR_GOAL ? sizeof(lce_ai_goal)
                        : fd.kind == LCE_F_ARR_DROP ? sizeof(lce_drop) : fd.kind == LCE_F_ARR_REF ? sizeof(const char *) : 1;
            uint8_t *copy_arr = (uint8_t *)lalloc(l, elem * n);
            if (!copy_arr) return LCE_E_FULL;
            memcpy(copy_arr, in, elem * n);
            for (uint32_t i = 0; i < n; ++i) {
                if (fd.kind == LCE_F_ARR_EFFECT) {
                    lce_effect_ref &e = ((lce_effect_ref *)copy_arr)[i];
                    if (!e.effect || !lce_valid_key(e.effect) || e.chance_percent > 100 || e.duration_ticks > 20 * 60 * 60 * 24) return LCE_E_INVALID;
                    e.effect = lstrdup(l, e.effect);
                } else if (fd.kind == LCE_F_ARR_GOAL) {
                    lce_ai_goal &g = ((lce_ai_goal *)copy_arr)[i];
                    if (g.kind < LCE_AI_WANDER || g.kind > LCE_AI_AVOID_ENTITY || (g.param && !lce_valid_key(g.param))) return LCE_E_INVALID;
                    if (g.param) g.param = lstrdup(l, g.param);
                } else if (fd.kind == LCE_F_ARR_DROP) {
                    lce_drop &d = ((lce_drop *)copy_arr)[i];
                    if (!d.item || !lce_valid_key(d.item) || d.min > d.max || d.max > 64 || d.chance_percent > 100) return LCE_E_INVALID;
                    d.item = lstrdup(l, d.item);
                } else if (fd.kind == LCE_F_ARR_REF) {
                    const char *&r = ((const char **)copy_arr)[i];
                    if (!r || !lce_valid_key(r)) return LCE_E_INVALID;
                    r = lstrdup(l, r);
                }
            }
            at<const void *>(dst, fd.offset) = copy_arr;
            break;
        }
        case LCE_F_ARR_U16_XYZ: {
            const lce_structure_desc *sd = (const lce_structure_desc *)dst;
            uint64_t n = (uint64_t)sd->size_x * sd->size_y * sd->size_z;
            if (!n || n > 64u * 64u * 64u || sd->size_x > 64 || sd->size_y > 64 || sd->size_z > 64 || !sd->blocks || !sd->palette_count) return LCE_E_INVALID;
            uint16_t *b = (uint16_t *)lalloc(l, (size_t)n * 2);
            if (!b) return LCE_E_FULL;
            memcpy(b, sd->blocks, (size_t)n * 2);
            for (uint64_t i = 0; i < n; ++i) if (b[i] >= sd->palette_count) return LCE_E_INVALID;
            at<const uint16_t *>(dst, fd.offset) = b;
            break;
        }
        default: return LCE_E_INVALID;
        }
    }
    (void)src;
    return LCE_OK;
}

// type-specific range checks that the schema cannot express
lce_status ext_ranges(uint32_t type, const uint8_t *d) {
    switch (type) {
    case LCE_EXT_ENCHANT: { const lce_enchant_desc *e = (const lce_enchant_desc *)d; return e->max_level >= 1 && e->max_level <= 10 && e->weight >= 1 ? LCE_OK : LCE_E_INVALID; }
    case LCE_EXT_FUEL: { const lce_fuel_desc *f = (const lce_fuel_desc *)d; return f->burn_ticks >= 1 && f->burn_ticks <= 20 * 60 * 60 ? LCE_OK : LCE_E_INVALID; }
    case LCE_EXT_POTION: { const lce_potion_desc *p = (const lce_potion_desc *)d; return p->effect_count >= 1 ? LCE_OK : LCE_E_INVALID; }
    case LCE_EXT_FEATURE: { const lce_feature_desc *f = (const lce_feature_desc *)d;
        if (f->kind < LCE_FEATURE_ORE || f->kind > LCE_FEATURE_STRUCTURE || f->dimension > 2 || f->min_y > f->max_y || f->max_y > 255) return LCE_E_INVALID;
        if (f->kind == LCE_FEATURE_STRUCTURE ? !f->structure : !f->block) return LCE_E_INVALID;
        return f->kind == LCE_FEATURE_ORE && (!f->size || f->size > 64 || !f->per_chunk || f->per_chunk > 64) ? LCE_E_INVALID : LCE_OK; }
    case LCE_EXT_ENTITY: { const lce_entity_desc2 *e = (const lce_entity_desc2 *)d;
        return e->category <= LCE_MOB_AMBIENT && e->min_group <= e->max_group && e->max_health_milli <= 1024000 ? LCE_OK : LCE_E_INVALID; }
    case LCE_EXT_SOUND: { const lce_sound_desc *s = (const lce_sound_desc *)d; return s->clone_of || s->pcm ? LCE_OK : LCE_E_INVALID; }
    case LCE_EXT_COMMAND: { const lce_command_desc *c = (const lce_command_desc *)d; return c->fn ? LCE_OK : LCE_E_INVALID; }
    case LCE_EXT_KEYBIND: { const lce_keybind_desc *k = (const lce_keybind_desc *)d; return k->fn && k->default_button >= LCE_PAD_L3 && k->default_button <= LCE_PAD_DPAD_RIGHT_HOLD ? LCE_OK : LCE_E_INVALID; }
    default: return LCE_OK;
    }
}

lce_status api_register_ext(lce_mod_ctx *ctx, uint32_t type, const void *in) {
    lce_status st = check_phase(ctx); if (st) return st;
    const lce_ext_schema *sc = lce_ext_schema_of(type);
    if (!sc || !in) return LCE_E_INVALID;
    uint32_t given = *(const uint32_t *)in;
    if (given < 2 * sizeof(void *) || given > 4096) return LCE_E_INVALID;
    lce_loader *l = ctx->loader;
    if (l->ext_count >= LCE_LOADER_MAX_EXT) return LCE_E_FULL;
    SExt &e = l->ext[l->ext_count];
    memset(&e, 0, sizeof e);
    if (sc->desc_size > sizeof e.desc) return LCE_E_INVALID;
    memcpy(e.desc, in, given < sc->desc_size ? given : sc->desc_size);
    *(uint32_t *)e.desc = sc->desc_size;
    e.type = type; e.mod = ctx->index;
    if ((st = ext_deep_copy(l, sc, MODOF(ctx).rep.modid, e.desc, in, e.key)) != LCE_OK) return st;
    if ((st = ext_ranges(type, e.desc)) != LCE_OK) return st;
    for (uint32_t i = 0; i < l->ext_count; ++i)
        if (l->ext[i].type == type && !strcmp(l->ext[i].key, e.key)) return LCE_E_COLLISION;
    if (type == LCE_EXT_FUEL)  // one fuel value per item across all mods
        for (uint32_t i = 0; i < l->ext_count; ++i)
            if (l->ext[i].type == LCE_EXT_FUEL && !strcmp(((const lce_fuel_desc *)l->ext[i].desc)->item, ((const lce_fuel_desc *)e.desc)->item)) return LCE_E_COLLISION;
    ++l->ext_count;
    return LCE_OK;
}
lce_status api_register_effect(lce_mod_ctx *c, const lce_effect_desc *d) { return api_register_ext(c, LCE_EXT_EFFECT, d); }
lce_status api_register_potion(lce_mod_ctx *c, const lce_potion_desc *d) { return api_register_ext(c, LCE_EXT_POTION, d); }
lce_status api_register_brewing(lce_mod_ctx *c, const lce_brew_desc *d) { return api_register_ext(c, LCE_EXT_BREW, d); }
lce_status api_register_enchantment(lce_mod_ctx *c, const lce_enchant_desc *d) { return api_register_ext(c, LCE_EXT_ENCHANT, d); }
lce_status api_register_fuel(lce_mod_ctx *c, const lce_fuel_desc *d) { return api_register_ext(c, LCE_EXT_FUEL, d); }
lce_status api_register_entity2(lce_mod_ctx *c, const lce_entity_desc2 *d) { return api_register_ext(c, LCE_EXT_ENTITY, d); }
lce_status api_register_feature(lce_mod_ctx *c, const lce_feature_desc *d) { return api_register_ext(c, LCE_EXT_FEATURE, d); }
lce_status api_register_biome(lce_mod_ctx *c, const lce_biome_desc *d) { return api_register_ext(c, LCE_EXT_BIOME, d); }
lce_status api_register_structure(lce_mod_ctx *c, const lce_structure_desc *d) { return api_register_ext(c, LCE_EXT_STRUCTURE, d); }
lce_status api_register_sound(lce_mod_ctx *c, const lce_sound_desc *d) { return api_register_ext(c, LCE_EXT_SOUND, d); }
lce_status api_register_particle(lce_mod_ctx *c, const lce_particle_desc *d) { return api_register_ext(c, LCE_EXT_PARTICLE, d); }
lce_status api_register_command(lce_mod_ctx *c, const lce_command_desc *d) { return api_register_ext(c, LCE_EXT_COMMAND, d); }
lce_status api_register_keybind(lce_mod_ctx *c, const lce_keybind_desc *d) { return api_register_ext(c, LCE_EXT_KEYBIND, d); }

const SExt *find_ext(const lce_loader *l, uint32_t type, const char *key) {
    for (uint32_t i = 0; i < l->ext_count; ++i) if (l->ext[i].type == type && !strcmp(l->ext[i].key, key)) return &l->ext[i];
    return nullptr;
}
bool block_ref_ok(const lce_loader *l, const char *key);
// Does a reference of this kind resolve against vanilla + active mods?
bool ref_ok(const lce_loader *l, uint32_t kind, const char *key) {
    if (!key) return true;
    const char *name = strchr(key, ':') + 1;
    auto ext_live = [&](uint32_t type) { const SExt *x = find_ext(l, type, key); return x && mod_active(l, x->mod); };
    switch (kind) {
    case LCE_REF_ITEM: return item_ref_ok(l, key);
    case LCE_REF_BLOCK: return block_ref_ok(l, key);
    case LCE_REF_EFFECT: return is_vanilla(key) ? in_names(kVanillaEffects, sizeof kVanillaEffects / sizeof *kVanillaEffects, name) : ext_live(LCE_EXT_EFFECT);
    case LCE_REF_POTION: return is_vanilla(key) ? in_names(kVanillaPotions, sizeof kVanillaPotions / sizeof *kVanillaPotions, name) : ext_live(LCE_EXT_POTION);
    case LCE_REF_SOUND: return is_vanilla(key) || ext_live(LCE_EXT_SOUND);           // vanilla names checked by the backend's sound map
    case LCE_REF_BIOME: return is_vanilla(key) || ext_live(LCE_EXT_BIOME);
    case LCE_REF_ENTITY: return is_vanilla(key) || ext_live(LCE_EXT_ENTITY);
    case LCE_REF_PARTICLE: return is_vanilla(key) || ext_live(LCE_EXT_PARTICLE);
    case LCE_REF_STRUCTURE: return ext_live(LCE_EXT_STRUCTURE);
    case LCE_REF_FEATURE: return ext_live(LCE_EXT_FEATURE);
    case LCE_REF_TEXTURE_ENTITY: { int t = find_tex(l, key); return t >= 0 && mod_active(l, l->tex[t].mod) && l->tex[t].d.atlas == 3; }
    case LCE_REF_TEXTURE_ICON: { int t = find_tex(l, key); return t >= 0 && mod_active(l, l->tex[t].mod) && (l->tex[t].d.atlas == 0 || l->tex[t].d.atlas == 4); }
    default: return true;
    }
}
bool block_ref_ok(const lce_loader *l, const char *key) {
    if (is_vanilla(key)) return lce_vanilla_block_id(key + 10) >= 0;
    const SBlock *b = find_block(l, key);
    return b && mod_active(l, b->mod);
}

lce_status validate_ext(const lce_loader *l, const SExt &e, char *why, size_t cap) {
    const lce_ext_schema *sc = lce_ext_schema_of(e.type);
    const lce_backend *be = l->cfg.backend;
    if (!(be->capabilities & sc->capability)) {
        // Ask the backend for its precise reason (the module that proved it impossible), if it has one.
        char err[160] = {0};
        if (be->check_ext && be->check_ext(be->self, e.type, e.desc, err, sizeof err) != LCE_OK && err[0])
            snprintf(why, cap, "backend '%s' cannot register %s (%s): %s", be->name, sc->name, e.key, err);
        else snprintf(why, cap, "backend '%s' cannot register %s (%s)", be->name, sc->name, e.key);
        return LCE_E_UNSUPPORTED;
    }
    for (uint32_t f = 0; f < sc->field_count; ++f) {
        const lce_ext_field &fd = sc->fields[f];
        const uint8_t *d = e.desc;
        if (fd.kind == LCE_F_REF || fd.kind == LCE_F_REF_REQ) {
            const char *r = *(const char *const *)(d + fd.offset);
            if (!ref_ok(l, fd.ref, r)) { snprintf(why, cap, "%s %s: unknown %s '%s'", sc->name, e.key, fd.name, r); return LCE_E_NOTFOUND; }
        } else if (fd.kind == LCE_F_ARR_EFFECT || fd.kind == LCE_F_ARR_DROP || fd.kind == LCE_F_ARR_REF) {
            uint32_t n = *(const uint32_t *)(d + fd.count_offset);
            const void *arr = *(const void *const *)(d + fd.offset);
            for (uint32_t i = 0; i < n; ++i) {
                const char *r = fd.kind == LCE_F_ARR_EFFECT ? ((const lce_effect_ref *)arr)[i].effect
                              : fd.kind == LCE_F_ARR_DROP ? ((const lce_drop *)arr)[i].item : ((const char *const *)arr)[i];
                uint32_t kind = fd.kind == LCE_F_ARR_EFFECT ? (uint32_t)LCE_REF_EFFECT : fd.kind == LCE_F_ARR_DROP ? (uint32_t)LCE_REF_ITEM : (uint32_t)fd.ref;
                if (!ref_ok(l, kind, r)) { snprintf(why, cap, "%s %s: unknown %s entry '%s'", sc->name, e.key, fd.name, r); return LCE_E_NOTFOUND; }
            }
        }
    }
    if (be->check_ext) {
        char err[200] = {0};
        lce_status st = be->check_ext(be->self, e.type, e.desc, err, sizeof err);
        if (st != LCE_OK) { snprintf(why, cap, "%s %s: %s", sc->name, e.key, err[0] ? err : "not supported by backend"); return st; }
    }
    return LCE_OK;
}

int32_t api_resolve_item(lce_mod_ctx *ctx, const char *key) {
    if (!ctx || !key) return -1;
    lce_phase p = ctx->loader->phase;
    if (is_vanilla(key)) return vanilla_item(key);
    if (p != LCE_PHASE_POST_INIT && p != LCE_PHASE_RUNNING) return -1;
    return item_ref_id(ctx->loader, key);
}
int32_t api_resolve_block(lce_mod_ctx *ctx, const char *key) {
    if (!ctx || !key) return -1;
    if (is_vanilla(key)) return lce_vanilla_block_id(key + 10);
    lce_phase p = ctx->loader->phase;
    if (p != LCE_PHASE_POST_INIT && p != LCE_PHASE_RUNNING) return -1;
    const SBlock *b = find_block(ctx->loader, key);
    return b && mod_active(ctx->loader, b->mod) ? (int32_t)b->id : -1;
}

lce_status svc_give(lce_mod_ctx *, void *, const char *, uint32_t, int32_t) { return LCE_E_UNSUPPORTED; }
lce_status svc_set_block(lce_mod_ctx *, void *, int32_t, int32_t, int32_t, const char *, uint32_t) { return LCE_E_UNSUPPORTED; }
int32_t svc_get_block(lce_mod_ctx *, void *, int32_t, int32_t, int32_t) { return -1; }
lce_status svc_spawn(lce_mod_ctx *, void *, const char *, int32_t, int32_t, int32_t) { return LCE_E_UNSUPPORTED; }
lce_status svc_drop(lce_mod_ctx *, void *, int32_t, int32_t, int32_t, const char *, uint32_t) { return LCE_E_UNSUPPORTED; }
lce_status svc_chat(lce_mod_ctx *, void *, const char *) { return LCE_E_UNSUPPORTED; }
lce_status svc_effect(lce_mod_ctx *, void *, const lce_effect_ref *) { return LCE_E_UNSUPPORTED; }
lce_status svc_sound(lce_mod_ctx *, void *, int32_t, int32_t, int32_t, const char *) { return LCE_E_UNSUPPORTED; }
lce_status svc_schedule(lce_mod_ctx *, uint32_t, lce_event_fn, void *) { return LCE_E_UNSUPPORTED; }
const char *svc_translate(lce_mod_ctx *, const char *key) { return key; }
// API 1.2 services: UNSUPPORTED until a backend binds them (lce_loader_set_services)
lce_status svc_explode(lce_mod_ctx *, void *, double, double, double, float, int, int) { return LCE_E_UNSUPPORTED; }
lce_status svc_fire(lce_mod_ctx *, void *, uint32_t) { return LCE_E_UNSUPPORTED; }
lce_status svc_damage(lce_mod_ctx *, void *, uint32_t, void *) { return LCE_E_UNSUPPORTED; }
lce_status svc_heal(lce_mod_ctx *, void *, uint32_t) { return LCE_E_UNSUPPORTED; }
lce_status svc_held(lce_mod_ctx *, void *, uint32_t) { return LCE_E_UNSUPPORTED; }
lce_status svc_move(lce_mod_ctx *, void *, double, double, double) { return LCE_E_UNSUPPORTED; }
lce_status svc_ent_get(lce_mod_ctx *, void *, const char *, lce_nbt_value *) { return LCE_E_UNSUPPORTED; }
lce_status svc_ent_set(lce_mod_ctx *, void *, const char *, const lce_nbt_value *) { return LCE_E_UNSUPPORTED; }
lce_status svc_ent_remove(lce_mod_ctx *, void *, const char *) { return LCE_E_UNSUPPORTED; }
lce_status svc_send_to_server(lce_mod_ctx *, const char *, const void *, uint32_t) { return LCE_E_UNSUPPORTED; }
lce_status svc_send_payload(lce_mod_ctx *, void *, const char *, const void *, uint32_t) { return LCE_E_UNSUPPORTED; }
lce_status api_register_channel(lce_mod_ctx *ctx, const char *ch) {
    lce_status st = check_phase(ctx);
    if (st != LCE_OK) return st;
    lce_loader *l = ctx->loader;
    if (!l->cfg.backend->register_channel || !(l->cfg.backend->capabilities2 & LCE_CAP2_EV_PAYLOAD)) return LCE_E_UNSUPPORTED;
    const char *modid = l->mods[ctx->index].rep.modid;
    size_t n = ch ? strlen(ch) : 0, m = strlen(modid);
    if (n < 3 || n > 20 || strncmp(ch, modid, m) || ch[m] != ':' || !ch[m + 1] || !strncmp(ch, "mc|", 3)) return LCE_E_INVALID;
    for (size_t i = 0; i < n; ++i) {
        char c = ch[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == ':' || c == '.' || c == '-')) return LCE_E_INVALID;
    }
    for (uint32_t i = 0; i < l->channel_count; ++i) if (!strcmp(l->channels[i].name, ch)) return LCE_E_COLLISION;
    if (l->channel_count >= sizeof l->channels / sizeof l->channels[0]) return LCE_E_FULL;
    l->channels[l->channel_count].mod = ctx->index;
    copy(l->channels[l->channel_count].name, sizeof l->channels[0].name, ch);
    ++l->channel_count;
    return LCE_OK;   // forwarded to the backend after a successful publish, for live mods only
}
lce_status svc_tag_get(lce_mod_ctx *, void *, const char *, lce_nbt_value *) { return LCE_E_UNSUPPORTED; }
lce_status svc_tag_set(lce_mod_ctx *, void *, const char *, const lce_nbt_value *) { return LCE_E_UNSUPPORTED; }
lce_status svc_tag_remove(lce_mod_ctx *, void *, const char *) { return LCE_E_UNSUPPORTED; }
lce_status svc_particles(lce_mod_ctx *, void *, int32_t, double, double, double, uint32_t, double, double, double, double) { return LCE_E_UNSUPPORTED; }
lce_status svc_tinted_particles(lce_mod_ctx *, void *, uint32_t, double, double, double, uint32_t, double) { return LCE_E_UNSUPPORTED; }

// API 1.2 events are gated by the second capability word, which the backend's service/event layer reports
// through lce_loader_set_services (api.capabilities2).
uint32_t cap2_for_event(lce_event ev) {
    switch (ev) {
    case LCE_EV_BLOCK_USE: return LCE_CAP2_EV_BLOCK_USE;
    case LCE_EV_ENTITY_INTERACT: return LCE_CAP2_EV_ENTITY_INTERACT;
    case LCE_EV_BLOCK_TICK: return LCE_CAP2_EV_BLOCK_TICK;
    case LCE_EV_ENTITY_TICK: return LCE_CAP2_EV_ENTITY_TICK;
    case LCE_EV_ITEM_INVENTORY_TICK: return LCE_CAP2_EV_INVENTORY_TICK;
    case LCE_EV_BLOCK_NEIGHBOR: return LCE_CAP2_EV_NEIGHBOR;
    case LCE_EV_BLOCK_ENTITY_INSIDE: case LCE_EV_BLOCK_STEP_ON: return LCE_CAP2_EV_BLOCK_COLLIDE;
    case LCE_EV_CUSTOM_PAYLOAD: return LCE_CAP2_EV_PAYLOAD;
    case LCE_EV_CLIENT_PAYLOAD: return LCE_CAP2_EV_CLIENT_PAYLOAD;
    default: return 0;
    }
}

uint32_t cap_for_event(lce_event ev) {
    switch (ev) {
    case LCE_EV_SERVER_TICK: case LCE_EV_CLIENT_TICK: return LCE_CAP_EV_TICK;
    case LCE_EV_WORLD_LOAD: case LCE_EV_WORLD_UNLOAD: return LCE_CAP_EV_WORLD;
    case LCE_EV_BLOCK_BREAK: case LCE_EV_BLOCK_PLACE: return LCE_CAP_EV_BLOCK;
    case LCE_EV_ITEM_USE: return LCE_CAP_EV_ITEM_USE;
    case LCE_EV_ENTITY_DAMAGE: case LCE_EV_ENTITY_DEATH: case LCE_EV_EFFECT_TICK: return LCE_CAP_EV_ENTITY;
    case LCE_EV_PLAYER_JOIN: case LCE_EV_PLAYER_LEAVE: case LCE_EV_CHAT: return LCE_CAP_EV_PLAYER;
    default: return 0;
    }
}

lce_status api_subscribe(lce_mod_ctx *ctx, lce_event ev, lce_event_fn fn, void *user) {
    lce_status st = check_phase(ctx); if (st) return st;
    uint32_t cap = cap_for_event(ev), cap2 = cap2_for_event(ev);
    if (!fn || (!cap && !cap2)) return LCE_E_INVALID;
    lce_loader *l = ctx->loader;
    if (cap ? !(l->cfg.backend->capabilities & cap) : !(l->cfg.backend->capabilities2 & cap2)) return LCE_E_UNSUPPORTED;
    if (l->sub_count >= LCE_LOADER_MAX_SUBS) return LCE_E_FULL;
    l->subs[l->sub_count++] = Sub{ctx->index, ev, fn, user};
    return LCE_OK;
}

// ---- validation ----------------------------------------------------------
// Validates one active mod against the currently active set. Returns LCE_OK or
// an error with reason.
lce_status validate_mod(lce_loader *l, uint32_t mi, char *why, size_t cap) {
    const lce_backend *be = l->cfg.backend;
    const uint32_t caps = be->capabilities;
    auto need = [&](uint32_t c, const char *what) -> bool {
        if (caps & c) return true;
        snprintf(why, cap, "backend '%s' cannot register %s", be->name, what);
        return false;
    };
    ModRec &m = l->mods[mi];
    // dependencies
    for (uint32_t d = 0; d < m.dep_count; ++d) {
        bool found = false;
        for (uint32_t j = 0; j < l->mod_count; ++j) {
            if (strcmp(l->mods[j].rep.modid, m.deps[d])) continue;
            found = l->mods[j].active();
            // dotted numeric version compare: dep must be >= min
            const char *a = l->mods[j].rep.version, *b = m.dep_ver[d];
            while (found && (*a || *b)) {
                long x = strtol(a, (char **)&a, 10), y = strtol(b, (char **)&b, 10);
                if (x != y) { found = x > y; break; }
                if (*a == '.') ++a;
                if (*b == '.') ++b;
            }
        }
        if (!found) { snprintf(why, cap, "missing/failed dependency %s >= %s", m.deps[d], m.dep_ver[d][0] ? m.dep_ver[d] : "0"); return LCE_E_NOTFOUND; }
    }
    auto tab_ok = [&](const char *tab, int crafting) -> bool {
        if (!tab || !*tab) return true;
        if (is_vanilla(tab)) return be->resolve_group && be->resolve_group(be->self, tab, crafting) >= 0;
        int t = find_tab(l, tab);
        if (t < 0 || !mod_active(l, l->tabs[t].mod)) return false;
        return crafting ? l->tabs[t].d.crafting : l->tabs[t].d.creative;
    };
    auto tex_ok = [&](const char *key, uint8_t atlas) -> bool {
        int t = find_tex(l, key);
        return t >= 0 && mod_active(l, l->tex[t].mod) && l->tex[t].d.atlas == atlas;
    };
    for (uint32_t i = 0; i < l->tex_count; ++i)
        if (l->tex[i].mod == mi && !need(LCE_CAP_TEXTURES, "textures")) return LCE_E_UNSUPPORTED;
    for (uint32_t i = 0; i < l->item_count; ++i) {
        const SItem &s = l->items[i];
        if (s.mod != mi) continue;
        if (!need(LCE_CAP_ITEMS, "items")) return LCE_E_UNSUPPORTED;
        if (!tex_ok(s.tex, 0)) { snprintf(why, cap, "item %s: texture %s missing or not an item texture", s.key, s.tex); return LCE_E_NOTFOUND; }
        if (!tab_ok(s.d.creative_tab, 0)) { snprintf(why, cap, "item %s: unknown creative tab %s", s.key, s.tab); return LCE_E_NOTFOUND; }
        if (be->check_item) {
            char e[200] = {0};
            lce_status st = be->check_item(be->self, &s.d, e, sizeof e);
            if (st != LCE_OK) { snprintf(why, cap, "item %s: %s", s.key, e[0] ? e : "not supported by backend"); return st; }
        }
    }
    for (uint32_t i = 0; i < l->block_count; ++i) {
        const SBlock &s = l->blocks[i];
        if (s.mod != mi) continue;
        if (!need(LCE_CAP_BLOCKS, "blocks")) return LCE_E_UNSUPPORTED;
        for (int f = 0; f < 6; ++f)
            if (!tex_ok(s.tex[f], 1)) { snprintf(why, cap, "block %s: texture %s missing or not a terrain texture", s.key, s.tex[f]); return LCE_E_NOTFOUND; }
        if (strcmp(s.drop, s.key) && !item_ref_ok(l, s.drop)) { snprintf(why, cap, "block %s: unknown drop %s", s.key, s.drop); return LCE_E_NOTFOUND; }
        if (!tab_ok(s.d.creative_tab, 0)) { snprintf(why, cap, "block %s: unknown creative tab %s", s.key, s.tab); return LCE_E_NOTFOUND; }
    }
    for (uint32_t i = 0; i < l->recipe_count; ++i) {
        const SRecipe &s = l->recipes[i];
        if (s.mod != mi) continue;
        if (!need(LCE_CAP_RECIPES, "recipes")) return LCE_E_UNSUPPORTED;
        for (int c = 0; c < 9; ++c)
            if (s.d.cells[c].item && !item_ref_ok(l, s.cell[c])) { snprintf(why, cap, "recipe %s: unknown ingredient %s", s.key, s.cell[c]); return LCE_E_NOTFOUND; }
        if (!item_ref_ok(l, s.result)) { snprintf(why, cap, "recipe %s: unknown result %s", s.key, s.result); return LCE_E_NOTFOUND; }
        if (!tab_ok(s.d.craft_tab, 1)) { snprintf(why, cap, "recipe %s: unknown crafting tab %s", s.key, s.tab); return LCE_E_NOTFOUND; }
        if (be->check_recipe) {
            char e[200] = {0};
            lce_status st = be->check_recipe(be->self, &s.d, e, sizeof e);
            if (st != LCE_OK) { snprintf(why, cap, "recipe %s: %s", s.key, e[0] ? e : "not supported by backend"); return st; }
        }
    }
    for (uint32_t i = 0; i < l->smelt_count; ++i) {
        const SSmelt &s = l->smelts[i];
        if (s.mod != mi) continue;
        if (!need(LCE_CAP_SMELTING, "smelting recipes")) return LCE_E_UNSUPPORTED;
        if (!item_ref_ok(l, s.input) || !item_ref_ok(l, s.result)) { snprintf(why, cap, "smelting %s: unknown item", s.key); return LCE_E_NOTFOUND; }
    }
    for (uint32_t i = 0; i < l->tab_count; ++i) {
        const STab &s = l->tabs[i];
        if (s.mod != mi) continue;
        if (!need(LCE_CAP_CRAFT_TABS | (s.d.creative ? LCE_CAP_CREATIVE : 0), "tabs")) return LCE_E_UNSUPPORTED;
        if (!item_ref_ok(l, s.icon)) { snprintf(why, cap, "tab %s: unknown icon item %s", s.key, s.icon); return LCE_E_NOTFOUND; }
    }
    for (uint32_t i = 0; i < l->string_count; ++i)
        if (l->strings[i].mod == mi && !need(LCE_CAP_STRINGS, "strings")) return LCE_E_UNSUPPORTED;
    for (uint32_t i = 0; i < l->ext_count; ++i) {
        if (l->ext[i].mod != mi) continue;
        lce_status st = validate_ext(l, l->ext[i], why, cap);
        if (st != LCE_OK) return st;
    }
    for (uint32_t i = 0; i < l->item_count; ++i) {  // API 1.1 item references
        const SItem &s = l->items[i];
        if (s.mod != mi) continue;
        const uint32_t kind_cap = s.d.kind == LCE_ITEM_FOOD ? LCE_CAP_FOOD : s.d.kind >= LCE_ITEM_HELMET ? LCE_CAP_ARMOUR : s.d.kind >= LCE_ITEM_SWORD ? LCE_CAP_TOOLS : 0;
        if (kind_cap && !(caps & kind_cap)) { snprintf(why, cap, "item %s: backend '%s' cannot publish this item kind", s.key, be->name); return LCE_E_UNSUPPORTED; }
        if (s.d.repair_item && !item_ref_ok(l, s.d.repair_item)) { snprintf(why, cap, "item %s: unknown repair item %s", s.key, s.repair); return LCE_E_NOTFOUND; }
        if (s.d.armor_texture) { int t = find_tex(l, s.d.armor_texture); if (t < 0 || l->tex[t].d.atlas != 2 || !mod_active(l, l->tex[t].mod)) { snprintf(why, cap, "item %s: armour texture %s missing (atlas 2, 64x32)", s.key, s.armor_tex); return LCE_E_NOTFOUND; } }
        for (uint32_t k = 0; k < s.d.food_effect_count; ++k)
            if (!ref_ok(l, LCE_REF_EFFECT, s.d.food_effects[k].effect)) { snprintf(why, cap, "item %s: unknown food effect %s", s.key, s.d.food_effects[k].effect); return LCE_E_NOTFOUND; }
        if (s.d.fuel_ticks && !(caps & LCE_CAP_FUEL)) { snprintf(why, cap, "item %s: backend cannot register fuel", s.key); return LCE_E_UNSUPPORTED; }
        if ((s.d.flags & LCE_ITEM_FLAG_THROWABLE) && !(be->capabilities2 & LCE_CAP2_PROJECTILES)) { snprintf(why, cap, "item %s: backend has no throwable projectiles", s.key); return LCE_E_UNSUPPORTED; }
        if ((s.d.flags & LCE_ITEM_FLAG_BOAT) && !(be->capabilities2 & LCE_CAP2_BOATS)) { snprintf(why, cap, "item %s: backend has no boat items", s.key); return LCE_E_UNSUPPORTED; }
        for (uint32_t k = 0; k < s.d.projectile_effect_count; ++k)
            if (!ref_ok(l, LCE_REF_EFFECT, s.d.projectile_effects[k].effect)) { snprintf(why, cap, "item %s: unknown projectile effect %s", s.key, s.d.projectile_effects[k].effect); return LCE_E_NOTFOUND; }
    }
    for (uint32_t i = 0; i < l->block_count; ++i) {
        const SBlock &s = l->blocks[i];
        if (s.mod != mi) continue;
        if (s.d.block_entity_slots && !(caps & LCE_CAP_BLOCK_ENTITY)) { snprintf(why, cap, "block %s: backend cannot create block entities", s.key); return LCE_E_UNSUPPORTED; }
        if (s.d.shape == LCE_SHAPE_BOXES && !(be->capabilities2 & LCE_CAP2_BLOCK_BOXES)) { snprintf(why, cap, "block %s: backend has no multi-box shapes", s.key); return LCE_E_UNSUPPORTED; }
        if ((s.d.variant_count >= 2 || s.d.shape) && !(be->capabilities2 & LCE_CAP2_BLOCK_STATES)) { snprintf(why, cap, "block %s: backend has no block states/shapes", s.key); return LCE_E_UNSUPPORTED; }
        if (be->check_block) {
            char e[200] = {0};
            lce_status st = be->check_block(be->self, &s.d, e, sizeof e);
            if (st != LCE_OK) { snprintf(why, cap, "block %s: %s", s.key, e[0] ? e : "not supported by backend"); return st; }
        }
    }
    // Cross-mod smelting input collisions (two mods smelting the same input)
    for (uint32_t i = 0; i < l->smelt_count; ++i) {
        if (l->smelts[i].mod != mi) continue;
        for (uint32_t j = 0; j < l->smelt_count; ++j)
            if (l->smelts[j].mod != mi && mod_active(l, l->smelts[j].mod) && !strcmp(l->smelts[i].input, l->smelts[j].input) && l->smelts[i].d.input.aux == l->smelts[j].d.input.aux) {
                snprintf(why, cap, "smelting input %s also claimed by %s", l->smelts[i].input, l->mods[l->smelts[j].mod].rep.modid);
                return LCE_E_COLLISION;
            }
    }
    return LCE_OK;
}

int ext_ledger_kind(uint32_t type) {
    switch (type) {
    case LCE_EXT_EFFECT: return LCE_KIND_EFFECT;
    case LCE_EXT_ENCHANT: return LCE_KIND_ENCHANT;
    case LCE_EXT_BIOME: return LCE_KIND_BIOME;
    default: return -1;
    }
}

int32_t claim_round(lce_loader *l) {  // returns index of first mod whose claim failed, or -1
    lce_ledger_reset_live(&l->ledger);
    for (uint32_t i = 0; i < l->block_count; ++i) {
        SBlock &b = l->blocks[i];
        b.id = b.item_id = 0;
        if (!mod_active(l, b.mod)) continue;
        uint32_t id;
        lce_status st = lce_ledger_claim(&l->ledger, &l->cfg.space, LCE_KIND_BLOCK, b.key, &id);
        if (st != LCE_OK) { reject(l, l->mods[b.mod], LCE_MOD_DROPPED, st, st == LCE_E_FULL ? "block ID range exhausted" : "block ID collision with game/other provider"); return (int32_t)b.mod; }
        b.id = id;
        b.item_id = (b.d.flags & LCE_BLOCK_FLAG_NO_ITEM) ? 0 : lce_block_item_id(id);
    }
    for (uint32_t i = 0; i < l->item_count; ++i) {
        SItem &it = l->items[i];
        it.id = 0;
        if (!mod_active(l, it.mod)) continue;
        uint32_t id;
        lce_status st = lce_ledger_claim(&l->ledger, &l->cfg.space, LCE_KIND_ITEM, it.key, &id);
        if (st != LCE_OK) { reject(l, l->mods[it.mod], LCE_MOD_DROPPED, st, st == LCE_E_FULL ? "item ID range exhausted" : "item ID collision with game/other provider"); return (int32_t)it.mod; }
        it.id = id;
    }
    // API 1.1 registries whose numeric ids are stored in saves (effects/enchantments in item and
    // entity NBT, biomes in chunk data, potions in item data): same ledger, one id space per kind.
    for (uint32_t i = 0; i < l->ext_count; ++i) {
        SExt &e = l->ext[i];
        e.id = -1;
        const int kind = ext_ledger_kind(e.type);
        if (kind < 0 || !mod_active(l, e.mod)) continue;
        // only backends that configured an id range for this kind (retail195) get ledger ids
        const uint32_t (*rg)[2] = l->cfg.space.ext_range[kind - LCE_KIND_EXT_FIRST];
        if (!rg[0][0] && !rg[0][1] && !rg[1][0] && !rg[1][1]) continue;
        uint32_t id;
        lce_status st = lce_ledger_claim(&l->ledger, &l->cfg.space, (lce_id_kind)kind, e.key, &id);
        if (st != LCE_OK) { reject(l, l->mods[e.mod], LCE_MOD_DROPPED, st, st == LCE_E_FULL ? "effect/enchantment/biome/potion ID range exhausted" : "effect/enchantment/biome/potion ID collision with the game"); return (int32_t)e.mod; }
        e.id = (int32_t)id;
    }
    return -1;
}

int32_t plan_resolve(const void *ctx, uint32_t kind, const char *key) {
    const lce_loader *l = (const lce_loader *)ctx;
    if (!key) return -1;
    if (kind == LCE_REF_ITEM) return item_ref_id(l, key);
    if (kind == LCE_REF_BLOCK) {
        if (is_vanilla(key)) return lce_vanilla_block_id(key + 10);
        const SBlock *b = find_block(l, key);
        return b && mod_active(l, b->mod) ? (int32_t)b->id : -1;
    }
    static const uint32_t type_of[] = {0, 0, 0, LCE_EXT_EFFECT, LCE_EXT_POTION, LCE_EXT_SOUND, LCE_EXT_STRUCTURE, LCE_EXT_BIOME,
                                       LCE_EXT_FEATURE, LCE_EXT_ENTITY, 0, 0, LCE_EXT_PARTICLE};
    if (kind >= sizeof type_of / sizeof *type_of || !type_of[kind]) return -1;
    if (is_vanilla(key)) return -2;  // vanilla: backend maps by name
    for (uint32_t i = 0; i < l->plan.ext_count; ++i)
        if (l->plan.ext[i].type == type_of[kind] && !strcmp(l->plan.ext[i].key, key)) return 1000000 + (int32_t)i;
    return -1;
}

void build_plan(lce_loader *l) {
    lce_plan &p = l->plan;
    memset(&p, 0, sizeof p);
    p.textures = l->p_tex; p.tabs = l->p_tabs; p.blocks = l->p_blocks; p.items = l->p_items;
    p.recipes = l->p_recipes; p.smelts = l->p_smelts; p.strings = l->p_strings;
    const lce_backend *be = l->cfg.backend;
    int32_t tex_map[LCE_LOADER_MAX_TEXTURES];
    for (uint32_t i = 0; i < l->tex_count; ++i) {
        tex_map[i] = -1;
        if (!mod_active(l, l->tex[i].mod)) continue;
        tex_map[i] = (int32_t)p.texture_count;
        l->p_tex[p.texture_count++] = lce_plan_texture{l->tex[i].key, l->tex[i].d.atlas, l->tex[i].rgba, -1};
    }
    int32_t tab_map[LCE_LOADER_MAX_TABS];
    for (uint32_t i = 0; i < l->tab_count; ++i) {
        tab_map[i] = -1;
        if (!mod_active(l, l->tabs[i].mod)) continue;
        tab_map[i] = (int32_t)p.tab_count;
        l->p_tabs[p.tab_count++] = lce_plan_tab{&l->tabs[i].d, item_ref_id(l, l->tabs[i].icon)};
    }
    auto group = [&](const char *tab, int crafting, int32_t *mod_tab) -> int32_t {
        *mod_tab = -1;
        if (!tab || !*tab) return -1;
        if (is_vanilla(tab)) return be->resolve_group(be->self, tab, crafting);
        *mod_tab = tab_map[find_tab(l, tab)];
        return -1;
    };
    for (uint32_t i = 0; i < l->block_count; ++i) {
        const SBlock &b = l->blocks[i];
        if (!mod_active(l, b.mod)) continue;
        lce_plan_block &pb = l->p_blocks[p.block_count++];
        memset(&pb, 0, sizeof pb);
        pb.desc = &b.d; pb.id = b.id; pb.item_id = b.item_id;
        for (int f = 0; f < 6; ++f) pb.texture[f] = tex_map[find_tex(l, b.tex[f])];
        pb.drop_item = strcmp(b.drop, b.key) ? item_ref_id(l, b.drop) : (int32_t)b.item_id;
        pb.creative_group = group(b.d.creative_tab, 0, &pb.mod_tab);
    }
    for (uint32_t i = 0; i < l->item_count; ++i) {
        const SItem &it = l->items[i];
        if (!mod_active(l, it.mod)) continue;
        lce_plan_item &pi = l->p_items[p.item_count++];
        memset(&pi, 0, sizeof pi);
        pi.desc = &it.d; pi.id = it.id; pi.texture = tex_map[find_tex(l, it.tex)];
        pi.creative_group = group(it.d.creative_tab, 0, &pi.mod_tab);
    }
    // placeholders for orphaned ledger IDs
    l->placeholder_count = 0;
    // A biome id of a removed mod still sits in chunk data: hand the backend a placeholder (plan ext with
    // desc NULL, never generated) so old chunks keep loading. Independent of LCE_LOADER_PLACEHOLDERS.
    l->orphan_count = 0;
    for (uint32_t i = 0; i < l->ledger.count && (be->capabilities & LCE_CAP_BIOMES); ++i) {
        const lce_ledger_entry &e = l->ledger.entries[i];
        if (e.live || e.kind != LCE_KIND_BIOME || l->orphan_count >= LCE_LOADER_MAX_ORPHANS) continue;
        copy(l->orphan_keys[l->orphan_count], LCE_MAX_KEY, e.key);
        l->orphan_ids[l->orphan_count++] = (int32_t)e.id;
    }
    if ((l->cfg.flags & LCE_LOADER_PLACEHOLDERS)) {
        for (uint32_t i = 0; i < l->ledger.count; ++i) {
            const lce_ledger_entry &e = l->ledger.entries[i];
            if (e.live) continue;
            if (e.kind >= LCE_KIND_EXT_FIRST) continue;   // ext ids: reserved only (biomes below)
            // Never place a placeholder over something the game/another provider now owns.
            const lce_id_space &sp = l->cfg.space;
            uint32_t item_id = e.kind == LCE_KIND_ITEM ? e.id : lce_block_item_id(e.id);
            if ((sp.item_in_use && sp.item_in_use(sp.user, item_id)) ||
                (e.kind == LCE_KIND_BLOCK && sp.block_in_use && sp.block_in_use(sp.user, e.id))) {
                logf(l, "lce-mods: no placeholder for %s (ID %u now occupied)", e.key, (unsigned)e.id);
                continue;
            }
            SPlaceholder &ph = l->placeholders[l->placeholder_count++];
            memset(&ph, 0, sizeof ph);
            copy(ph.key, sizeof ph.key, e.key);
            snprintf(ph.name, sizeof ph.name, "Missing mod content (%.60s)", e.key);
            if (e.kind == LCE_KIND_ITEM) {
                ph.item.struct_size = sizeof ph.item; ph.item.key = ph.key; ph.item.name = ph.name; ph.item.max_stack = 64;
                lce_plan_item &pi = l->p_items[p.item_count++];
                memset(&pi, 0, sizeof pi);
                pi.desc = &ph.item; pi.id = e.id; pi.texture = -1; pi.creative_group = -1; pi.mod_tab = -1; pi.placeholder = 1;
            } else {
                ph.block.struct_size = sizeof ph.block; ph.block.key = ph.key; ph.block.name = ph.name;
                ph.block.hardness_milli = 1500; ph.block.resistance_milli = 6000;
                lce_plan_block &pb = l->p_blocks[p.block_count++];
                memset(&pb, 0, sizeof pb);
                pb.desc = &ph.block; pb.id = e.id; pb.item_id = lce_block_item_id(e.id);
                for (int f = 0; f < 6; ++f) pb.texture[f] = -1;
                pb.drop_item = -1; pb.creative_group = -1; pb.mod_tab = -1; pb.placeholder = 1;
            }
        }
    }
    for (uint32_t i = 0; i < l->recipe_count; ++i) {
        const SRecipe &r = l->recipes[i];
        if (!mod_active(l, r.mod)) continue;
        lce_plan_recipe &pr = l->p_recipes[p.recipe_count++];
        memset(&pr, 0, sizeof pr);
        pr.desc = &r.d;
        for (int c = 0; c < 9; ++c) {
            pr.cells[c].id = r.d.cells[c].item ? item_ref_id(l, r.cell[c]) : -1;
            pr.cells[c].aux = r.d.cells[c].aux;
        }
        pr.result_id = item_ref_id(l, r.result);
        pr.craft_group = group(r.d.craft_tab, 1, &pr.mod_tab);
    }
    for (uint32_t i = 0; i < l->smelt_count; ++i) {
        const SSmelt &s = l->smelts[i];
        if (!mod_active(l, s.mod)) continue;
        l->p_smelts[p.smelt_count++] = lce_plan_smelt{&s.d, {item_ref_id(l, s.input), s.d.input.aux}, item_ref_id(l, s.result)};
    }
    for (uint32_t i = 0; i < l->string_count; ++i) {
        const SString &s = l->strings[i];
        if (!mod_active(l, s.mod)) continue;
        l->p_strings[p.string_count++] = lce_plan_string{s.locale, s.key, s.value, item_ref_id(l, s.key)};
    }
    p.ext = l->p_ext;
    for (uint32_t i = 0; i < l->ext_count; ++i) {
        const SExt &e = l->ext[i];
        if (!mod_active(l, e.mod)) continue;
        l->p_ext[p.ext_count++] = lce_plan_ext{e.type, e.desc, e.key, ext_ledger_kind(e.type) >= 0 ? e.id : -1};
    }
    for (uint32_t i = 0; i < l->orphan_count && p.ext_count < LCE_LOADER_MAX_EXT; ++i)   // biome placeholders (desc NULL)
        l->p_ext[p.ext_count++] = lce_plan_ext{LCE_EXT_BIOME, nullptr, l->orphan_keys[i], l->orphan_ids[i]};
    p.resolve = plan_resolve;
    p.resolve_ctx = l;
}

void fail_all(lce_loader *l, lce_status st, const char *why) {
    l->phase = LCE_PHASE_FAILED;
    for (uint32_t i = 0; i < l->mod_count; ++i) {
        ModRec &m = l->mods[i];
        if (m.active()) reject(l, m, LCE_MOD_DROPPED, st, why);
    }
    logf(l, "lce-mods: load FAILED (%d): %s; no mod content was published", (int)st, why);
}

void emit_dir(void *ctx, const char *name);
struct DirList { char names[LCE_LOADER_MAX_MODS * 2][LCE_MAX_MODID]; uint32_t count; };
void emit_dir(void *ctx, const char *name) {
    DirList *d = (DirList *)ctx;
    if (d->count < LCE_LOADER_MAX_MODS * 2 && strlen(name) < LCE_MAX_MODID) copy(d->names[d->count++], LCE_MAX_MODID, name);
}

ModRec *new_mod(lce_loader *l, const char *modid, const char *version) {
    if (l->mod_count >= LCE_LOADER_MAX_MODS) return nullptr;
    ModRec &m = l->mods[l->mod_count];
    memset(&m, 0, sizeof m);
    copy(m.rep.modid, sizeof m.rep.modid, modid);
    copy(m.rep.version, sizeof m.rep.version, version);
    m.rep.state = LCE_MOD_DISCOVERED;
    m.ctx.loader = l; m.ctx.index = l->mod_count;
    for (uint32_t i = 0; i < l->mod_count; ++i)
        if (!strcmp(l->mods[i].rep.modid, modid)) { ++l->mod_count; reject(l, m, LCE_MOD_REJECTED, LCE_E_COLLISION, "duplicate mod id"); return &m; }
    ++l->mod_count;
    return &m;
}

}  // namespace

// ---------------------------------------------------------------------------
extern "C" int32_t lce_vanilla_item_id(const char *name) {
    if (!name) return -1;
    int32_t block_row = -1;
    for (uint32_t i = 0; i < kVanillaCount; ++i) {
        if (strcmp(kVanilla[i].name, name)) continue;
        if (kVanilla[i].block < 0) return kVanilla[i].item;  // prefer the separate item ("bed" -> 355)
        block_row = kVanilla[i].item;
    }
    return block_row;
}
extern "C" int32_t lce_vanilla_block_id(const char *name) {
    if (!name) return -1;
    for (uint32_t i = 0; i < kVanillaCount; ++i)
        if (kVanilla[i].block >= 0 && !strcmp(kVanilla[i].name, name)) return kVanilla[i].block;
    return -1;
}
extern "C" int lce_vanilla_item_exists(uint32_t id) {
    for (uint32_t i = 0; i < kVanillaCount; ++i) if ((uint32_t)kVanilla[i].item == id) return 1;
    return 0;
}

extern "C" lce_loader *lce_loader_create(const lce_loader_config *cfg) {
    if (!cfg || !cfg->backend || !cfg->backend->prepare || !cfg->backend->publish || !cfg->backend->name) return nullptr;
    if (cfg->space.item_min > cfg->space.item_max || cfg->space.block_min > cfg->space.block_max) return nullptr;
    lce_loader *l = (lce_loader *)calloc(1, sizeof(lce_loader));
    if (!l) return nullptr;
    l->cfg = *cfg;
    l->phase = LCE_PHASE_DISCOVER;
    lce_api &a = l->api;
    a.struct_size = sizeof a;
    a.version = LCE_MOD_API_VERSION;
    a.capabilities = cfg->backend->capabilities;
    a.log = api_log; a.phase = api_phase;
    a.register_texture = api_register_texture; a.register_item = api_register_item; a.register_block = api_register_block;
    a.register_recipe = api_register_recipe; a.register_smelting = api_register_smelting; a.register_tab = api_register_tab;
    a.register_string = api_register_string; a.register_entity = api_register_entity; a.register_ore = api_register_ore;
    a.resolve_item = api_resolve_item; a.resolve_block = api_resolve_block; a.subscribe = api_subscribe;
    a.register_effect = api_register_effect; a.register_potion = api_register_potion; a.register_brewing = api_register_brewing;
    a.register_enchantment = api_register_enchantment; a.register_fuel = api_register_fuel; a.register_entity2 = api_register_entity2;
    a.register_feature = api_register_feature; a.register_biome = api_register_biome; a.register_structure = api_register_structure;
    a.register_sound = api_register_sound; a.register_particle = api_register_particle; a.register_command = api_register_command;
    a.register_keybind = api_register_keybind;
    // runtime services are filled by the backend/host (lce_loader_set_services); until then they report UNSUPPORTED
    a.give_item = svc_give; a.set_block = svc_set_block; a.get_block = svc_get_block; a.spawn_entity = svc_spawn; a.drop_item = svc_drop;
    a.send_chat = svc_chat; a.add_effect = svc_effect; a.play_sound = svc_sound; a.schedule = svc_schedule; a.translate = svc_translate;
    a.capabilities2 = cfg->backend->capabilities2;
    a.explode = svc_explode; a.set_fire = svc_fire; a.damage_entity = svc_damage; a.heal = svc_heal;
    a.damage_held_item = svc_held; a.consume_held_item = svc_held; a.teleport = svc_move; a.set_motion = svc_move;
    a.spawn_particles = svc_particles;
    a.spawn_tinted_particles = svc_tinted_particles;
    a.item_tag_get = svc_tag_get; a.item_tag_set = svc_tag_set; a.item_tag_remove = svc_tag_remove;
    a.register_channel = api_register_channel; a.send_payload = svc_send_payload;
    a.send_to_server = svc_send_to_server;
    a.entity_nbt_get = svc_ent_get; a.entity_data_get = svc_ent_get; a.entity_data_set = svc_ent_set; a.entity_data_remove = svc_ent_remove;
    a.config_get = api_config_get; a.config_int = api_config_int; a.config_double = api_config_double; a.config_string = api_config_string;
    a.capabilities2 |= LCE_CAP2_CONFIG;   // loader-provided, every backend
    lce_ledger_init(&l->ledger);
    return l;
}

extern "C" void lce_loader_destroy(lce_loader *l) {
    if (!l) return;
    for (uint32_t i = 0; i < l->mod_count; ++i) { free(l->mods[i].owned_pkg); free(l->mods[i].config); }
    while (l->alloc_head) { void **p = (void **)l->alloc_head; l->alloc_head = p[0]; free(p); }
    free(l);
}

namespace {
// expect_modid: folder name the package was found in (NULL for in-memory packages).
lce_status add_package_impl(lce_loader *l, const uint8_t *data, size_t size, const char *source, const char *expect_modid) {
    if (!l || l->phase != LCE_PHASE_DISCOVER) return LCE_E_PHASE;
    lce_package_view v;
    char err[96];
    lce_status st = lce_package_parse(data, size, &v, err, sizeof err);
    if (st == LCE_OK && expect_modid && strcmp(lce_package_str(&v, v.meta->modid), expect_modid)) {
        st = LCE_E_INVALID;
        snprintf(err, sizeof err, "folder name differs from package mod id '%.31s'", lce_package_str(&v, v.meta->modid));
    }
    if (st != LCE_OK) {
        logf(l, "lce-mods: package %s rejected: %s", source ? source : "?", err);
        ModRec *m = new_mod(l, source && lce_valid_modid(source) ? source : "unknown_package", "0");
        if (m) reject(l, *m, LCE_MOD_REJECTED, st, err);
        return st;
    }
    ModRec *m = new_mod(l, lce_package_str(&v, v.meta->modid), lce_package_str(&v, v.meta->version));
    if (!m) return LCE_E_FULL;
    if (m->rep.state == LCE_MOD_REJECTED) return LCE_E_COLLISION;
    m->pkg = data; m->pkg_size = size; m->view = v; m->has_pkg = true;
    for (uint32_t i = 0; i < v.dep_count && i < 16; ++i) {
        copy(m->deps[i], LCE_MAX_MODID, lce_package_str(&v, v.deps[i].modid));
        copy(m->dep_ver[i], 16, lce_package_str(&v, v.deps[i].min_version));
        m->dep_count = i + 1;
    }
    // header flag bit0 (needs native.prx) is enforced in run_mods once discovery attached natives
    return LCE_OK;
}
}  // namespace

extern "C" lce_status lce_loader_add_package(lce_loader *l, const uint8_t *data, size_t size, const char *source) {
    return add_package_impl(l, data, size, source, nullptr);
}

extern "C" lce_status lce_loader_add_native(lce_loader *l, const char *modid, const char *version, lce_mod_main_fn fn) {
    if (!l || l->phase != LCE_PHASE_DISCOVER) return LCE_E_PHASE;
    if (!lce_valid_modid(modid) || !fn) return LCE_E_INVALID;
    for (uint32_t i = 0; i < l->mod_count; ++i)  // attach to an existing package of the same id
        if (!strcmp(l->mods[i].rep.modid, modid) && l->mods[i].has_pkg && !l->mods[i].native) { l->mods[i].native = fn; return LCE_OK; }
    ModRec *m = new_mod(l, modid, version ? version : "0");
    if (!m) return LCE_E_FULL;
    if (m->rep.state == LCE_MOD_REJECTED) return LCE_E_COLLISION;
    m->native = fn;
    return LCE_OK;
}

namespace {
void run_mods(lce_loader *l) {
    // Deterministic order: by mod id. (Stable ID claims depend only on the ledger,
    // but deterministic order keeps logs and first-time allocation reproducible.)
    for (uint32_t i = 1; i < l->mod_count; ++i)
        for (uint32_t j = i; j > 0 && strcmp(l->mods[j - 1].rep.modid, l->mods[j].rep.modid) > 0; --j) {
            ModRec t = l->mods[j]; l->mods[j] = l->mods[j - 1]; l->mods[j - 1] = t;
        }
    for (uint32_t i = 0; i < l->mod_count; ++i) l->mods[i].ctx.index = i;
    char err[128];
    l->phase = LCE_PHASE_INIT;
    for (uint32_t i = 0; i < l->mod_count; ++i) {
        ModRec &m = l->mods[i];
        if (m.rep.state != LCE_MOD_DISCOVERED) continue;
        if (m.has_pkg && (m.view.header->flags & 1u) && !m.native) { reject(l, m, LCE_MOD_REJECTED, LCE_E_UNSUPPORTED, "requires native code but native mods are disabled or native.prx missing"); continue; }
        if (!m.native) continue;
        m.accepting = true;
        lce_status st = m.native(&l->api, &m.ctx);
        m.accepting = false;
        if (st != LCE_OK) reject(l, m, LCE_MOD_REJECTED, st, "lce_mod_main failed");
    }
    l->phase = LCE_PHASE_REGISTER;
    for (uint32_t i = 0; i < l->mod_count; ++i) {
        ModRec &m = l->mods[i];
        if (m.rep.state != LCE_MOD_DISCOVERED) continue;
        if (m.has_pkg) {
            m.accepting = true;
            lce_status st = lce_package_register(&m.view, &l->api, &m.ctx, err, sizeof err);
            // Packaged scripts (LCE_SEC_SCRIPT) run in the same REGISTER window, so
            // their register_* calls are staged exactly like package content.
            for (uint32_t k = 0; st == LCE_OK && l->script_runner && k < m.view.script_count; ++k) {
                lce_mod_script sc{lce_package_str(&m.view, m.view.scripts[k].name), m.view.blob + m.view.scripts[k].data_offset, m.view.scripts[k].data_size};
                char e2[96] = {0};
                st = l->script_runner(l->script_user, l, i, &sc, e2, sizeof e2);
                if (st != LCE_OK) snprintf(err, sizeof err, "script %s: %s", sc.name, e2[0] ? e2 : "failed");
            }
            m.accepting = false;
            if (st != LCE_OK) { reject(l, m, LCE_MOD_REJECTED, st, err); continue; }
        }
        m.rep.state = LCE_MOD_STAGED;
    }
}
}  // namespace

extern "C" lce_status lce_loader_discover(lce_loader *l) {
    if (!l || l->phase != LCE_PHASE_DISCOVER) return LCE_E_PHASE;
    const lce_host_ops &h = l->cfg.host;
    if (l->cfg.root && h.list_dirs && h.read_file) {
        DirList *dirs = (DirList *)calloc(1, sizeof(DirList));
        uint8_t *scratch = (uint8_t *)malloc(LCE_PKG_MAX_FILE);
        if (!dirs || !scratch) { free(dirs); free(scratch); fail_all(l, LCE_E_FULL, "out of memory"); return LCE_E_FULL; }
        if (!h.list_dirs(h.user, l->cfg.root, emit_dir, dirs)) logf(l, "lce-mods: %s not readable; no mods", l->cfg.root);
        char path[256];
        for (uint32_t i = 0; i < dirs->count; ++i) {
            const char *name = dirs->names[i];
            if (!lce_valid_modid(name)) continue;  // packs, reservations, dotfiles, junk
            if (l->cfg.flags & LCE_LOADER_REQUIRE_ENABLED) {
                snprintf(path, sizeof path, "%s/%s/enabled", l->cfg.root, name);
                if (!h.exists || !h.exists(h.user, path)) { logf(l, "lce-mods: %s disabled (no enabled marker)", name); continue; }
            }
            snprintf(path, sizeof path, "%s/%s/mod.lcm", l->cfg.root, name);
            long n = h.read_file(h.user, path, scratch, LCE_PKG_MAX_FILE);
            if (n < 0) { logf(l, "lce-mods: %s: no readable mod.lcm (%ld)", name, n); continue; }
            uint8_t *buf = (uint8_t *)malloc(n ? (size_t)n : 1);
            if (!buf) continue;
            memcpy(buf, scratch, (size_t)n);
            uint32_t before = l->mod_count;
            lce_status st = add_package_impl(l, buf, (size_t)n, name, name);
            if (l->mod_count > before) l->mods[l->mod_count - 1].owned_pkg = buf; else free(buf);
            if (st != LCE_OK) continue;
            ModRec &m = l->mods[l->mod_count - 1];
            snprintf(path, sizeof path, "%s/%s/config", l->cfg.root, name);
            long cn = h.read_file(h.user, path, scratch, LCE_CFG_MAX_FILE);
            if (cn >= 0) attach_config(l, m, (const char *)scratch, (size_t)cn);
            else if (cn == -2) logf(l, "lce-mods: [warn] %s: config larger than %u bytes, using defaults", name, (unsigned)LCE_CFG_MAX_FILE);
            snprintf(path, sizeof path, "%s/%s/native.prx", l->cfg.root, name);
            if (h.exists && h.exists(h.user, path)) {
                if (!(l->cfg.flags & LCE_LOADER_ALLOW_NATIVE) || !h.load_native) { reject(l, m, LCE_MOD_REJECTED, LCE_E_UNSUPPORTED, "native.prx present but native mods are disabled"); continue; }
                m.native = h.load_native(h.user, path);
                if (!m.native) { reject(l, m, LCE_MOD_REJECTED, LCE_E_IO, "native.prx failed to load or lacks lce_mod_main"); continue; }
            }
        }
        free(scratch);
        free(dirs);
    }
    run_mods(l);
    uint32_t staged = 0;
    for (uint32_t i = 0; i < l->mod_count; ++i) staged += l->mods[i].rep.state == LCE_MOD_STAGED;
    logf(l, "lce-mods: discovered %u mod(s), %u staged", (unsigned)l->mod_count, (unsigned)staged);
    return LCE_OK;
}

extern "C" lce_status lce_loader_commit(lce_loader *l) {
    if (!l) return LCE_E_INVALID;
    if (l->phase == LCE_PHASE_DISCOVER) run_mods(l);  // add_package/add_native-only hosts
    if (l->phase != LCE_PHASE_REGISTER) return LCE_E_PHASE;
    l->phase = LCE_PHASE_COMMIT;
    const lce_host_ops &h = l->cfg.host;

    // 1. Ledger (fail closed if unreadable).
    if (l->cfg.ledger_path && h.read_file) {
        long n = h.read_file(h.user, l->cfg.ledger_path, (uint8_t *)l->ledger_text, sizeof l->ledger_text - 1);
        if (n == -2) { fail_all(l, LCE_E_LEDGER, "ID ledger too large"); return LCE_E_LEDGER; }
        lce_status st = lce_ledger_load(&l->ledger, l->ledger_text, n > 0 ? (size_t)n : 0);
        if (st != LCE_OK) { fail_all(l, LCE_E_LEDGER, "ID ledger corrupt; refusing to reallocate IDs (restore it from backup)"); return LCE_E_LEDGER; }
    }

    // 2. Validate to a fixed point, then claim IDs; repeat if a claim dropped a mod.
    char why[256];
    for (int round = 0; round < LCE_LOADER_MAX_MODS + 2; ++round) {
        bool changed = true;
        while (changed) {
            changed = false;
            for (uint32_t i = 0; i < l->mod_count; ++i) {
                if (!l->mods[i].active()) continue;
                why[0] = 0;
                lce_status st = validate_mod(l, i, why, sizeof why);
                if (st != LCE_OK) { reject(l, l->mods[i], LCE_MOD_DROPPED, st, why[0] ? why : "validation failed"); changed = true; }
            }
        }
        if (claim_round(l) < 0) break;
    }
    bool any_dropped = false;
    uint32_t active = 0;
    for (uint32_t i = 0; i < l->mod_count; ++i) { any_dropped |= !l->mods[i].active(); active += l->mods[i].active(); }
    if (any_dropped && (l->cfg.flags & LCE_LOADER_STRICT)) { fail_all(l, LCE_E_INVALID, "strict mode: a mod failed validation"); return LCE_E_INVALID; }

    // 3. Persist the ledger before anything is published.
    if (l->ledger.dirty) {
        size_t len = 0;
        if (!l->cfg.ledger_path || !h.write_file_atomic ||
            lce_ledger_serialize(&l->ledger, l->ledger_text, sizeof l->ledger_text, &len) != LCE_OK ||
            !h.write_file_atomic(h.user, l->cfg.ledger_path, (const uint8_t *)l->ledger_text, len)) {
            fail_all(l, LCE_E_IO, "could not persist ID ledger");
            return LCE_E_IO;
        }
        l->ledger.dirty = 0;
    }

    // 4. Plan, prepare (no mutation), publish.
    build_plan(l);
    if (!active && !l->plan.item_count && !l->plan.block_count) {
        l->phase = LCE_PHASE_RUNNING;
        logf(l, "lce-mods: nothing to publish");
        return LCE_OK;
    }
    char err[160] = {0};
    lce_status st = l->cfg.backend->prepare(l->cfg.backend->self, &l->plan, err, sizeof err);
    if (st != LCE_OK) { fail_all(l, st, err[0] ? err : "backend prepare refused"); return st; }
    l->plan_ready = true;
    int partial = 0;
    st = l->cfg.backend->publish(l->cfg.backend->self, &l->plan, &partial, err, sizeof err);
    if (st != LCE_OK) {
        fail_all(l, st, partial ? "backend publish PARTIAL: game state uncertain, keep worlds closed and restart" : (err[0] ? err : "backend publish refused"));
        return st;
    }
    for (uint32_t i = 0; i < l->mod_count; ++i)
        if (l->mods[i].active()) l->mods[i].rep.state = LCE_MOD_LIVE;
    for (uint32_t i = 0; i < l->channel_count; ++i)   // API 1.4: channels of live mods only
        if (l->mods[l->channels[i].mod].active() && l->cfg.backend->register_channel &&
            l->cfg.backend->register_channel(l->cfg.backend->self, l->channels[i].name) != 1)
            logf(l, "lce-mods: %s: network channel %s refused by the backend", l->mods[l->channels[i].mod].rep.modid, l->channels[i].name);
    l->phase = LCE_PHASE_RUNNING;
    logf(l, "lce-mods: published %u mod(s): %u items, %u blocks, %u recipes, %u smelting, %u textures (%u placeholders)",
         (unsigned)active, (unsigned)l->plan.item_count, (unsigned)l->plan.block_count, (unsigned)l->plan.recipe_count,
         (unsigned)l->plan.smelt_count, (unsigned)l->plan.texture_count, (unsigned)l->placeholder_count);
    return LCE_OK;
}

extern "C" lce_phase lce_loader_phase(const lce_loader *l) { return l ? l->phase : LCE_PHASE_NONE; }
extern "C" uint32_t lce_loader_mod_count(const lce_loader *l) { return l ? l->mod_count : 0; }
extern "C" const lce_mod_report *lce_loader_mod(const lce_loader *l, uint32_t i) { return l && i < l->mod_count ? &l->mods[i].rep : nullptr; }
extern "C" const lce_plan *lce_loader_plan(const lce_loader *l) { return l && l->plan_ready ? &l->plan : nullptr; }
extern "C" const lce_ledger *lce_loader_ledger(const lce_loader *l) { return l ? &l->ledger : nullptr; }
extern "C" const lce_api *lce_loader_api(const lce_loader *l) { return l ? &l->api : nullptr; }

extern "C" uint32_t lce_loader_scripts(const lce_loader *l, uint32_t i, lce_mod_script *out, uint32_t cap) {
    if (!l || i >= l->mod_count || !l->mods[i].has_pkg || !out) return 0;
    const lce_package_view &v = l->mods[i].view;
    uint32_t n = 0;
    for (uint32_t k = 0; k < v.script_count && n < cap; ++k)
        out[n++] = lce_mod_script{lce_package_str(&v, v.scripts[k].name), v.blob + v.scripts[k].data_offset, v.scripts[k].data_size};
    return n;
}
extern "C" void lce_loader_set_script_runner(lce_loader *l, lce_script_runner_fn fn, void *user) {
    if (l && l->phase == LCE_PHASE_DISCOVER) { l->script_runner = fn; l->script_user = user; }
}
extern "C" lce_status lce_loader_set_config_text(lce_loader *l, uint32_t i, const char *text, size_t len) {
    if (!l || i >= l->mod_count || (!text && len)) return LCE_E_INVALID;
    if (l->phase != LCE_PHASE_DISCOVER) return LCE_E_PHASE;
    if (len > LCE_CFG_MAX_FILE) return LCE_E_FULL;
    attach_config(l, l->mods[i], text ? text : "", len);
    return l->mods[i].config ? LCE_OK : LCE_E_FULL;
}
extern "C" lce_mod_ctx *lce_loader_mod_ctx(lce_loader *l, uint32_t i) { return l && i < l->mod_count ? &l->mods[i].ctx : nullptr; }
extern "C" void lce_loader_set_services(lce_loader *l, const lce_api *s) {
    if (!l || !s) return;
    lce_api &a = l->api;
    if (s->give_item) a.give_item = s->give_item;
    if (s->set_block) a.set_block = s->set_block;
    if (s->get_block) a.get_block = s->get_block;
    if (s->spawn_entity) a.spawn_entity = s->spawn_entity;
    if (s->drop_item) a.drop_item = s->drop_item;
    if (s->send_chat) a.send_chat = s->send_chat;
    if (s->add_effect) a.add_effect = s->add_effect;
    if (s->play_sound) a.play_sound = s->play_sound;
    if (s->schedule) a.schedule = s->schedule;
    if (s->translate) a.translate = s->translate;
    a.capabilities |= s->capabilities & LCE_CAP_SERVICES;
    if (s->struct_size >= offsetof(lce_api, set_motion) + sizeof(void *)) {   // API 1.2 table
        if (s->explode) a.explode = s->explode;
        if (s->set_fire) a.set_fire = s->set_fire;
        if (s->damage_entity) a.damage_entity = s->damage_entity;
        if (s->heal) a.heal = s->heal;
        if (s->damage_held_item) a.damage_held_item = s->damage_held_item;
        if (s->consume_held_item) a.consume_held_item = s->consume_held_item;
        if (s->teleport) a.teleport = s->teleport;
        if (s->set_motion) a.set_motion = s->set_motion;
        a.capabilities2 |= s->capabilities2 & ~LCE_CAP2_SVC_TINTED_PARTICLES;
    }
    if (s->struct_size >= offsetof(lce_api, spawn_particles) + sizeof(void *) && s->spawn_particles)   // API 1.3 table
        a.spawn_particles = s->spawn_particles;
    if (s->struct_size >= offsetof(lce_api, item_tag_remove) + sizeof(void *)) {   // API 1.4 table
        if (s->item_tag_get) a.item_tag_get = s->item_tag_get;
        if (s->item_tag_set) a.item_tag_set = s->item_tag_set;
        if (s->item_tag_remove) a.item_tag_remove = s->item_tag_remove;
    }
    if (s->struct_size >= offsetof(lce_api, send_payload) + sizeof(void *) && s->send_payload)   // API 1.4 networking table
        a.send_payload = s->send_payload;
    if (s->struct_size >= offsetof(lce_api, entity_data_remove) + sizeof(void *)) {   // API 1.5 table
        if (s->entity_nbt_get) a.entity_nbt_get = s->entity_nbt_get;
        if (s->entity_data_get) a.entity_data_get = s->entity_data_get;
        if (s->entity_data_set) a.entity_data_set = s->entity_data_set;
        if (s->entity_data_remove) a.entity_data_remove = s->entity_data_remove;
    }
    if (s->struct_size >= offsetof(lce_api, send_to_server) + sizeof(void *) && s->send_to_server)   // API 1.6 table
        a.send_to_server = s->send_to_server;
    if (s->struct_size >= offsetof(lce_api, spawn_tinted_particles) + sizeof(s->spawn_tinted_particles) &&
        s->spawn_tinted_particles) {
        a.spawn_tinted_particles = s->spawn_tinted_particles;
        a.capabilities2 |= s->capabilities2 & LCE_CAP2_SVC_TINTED_PARTICLES;
    }
}

extern "C" int lce_loader_dispatch(lce_loader *l, lce_event ev, const void *payload) {
    if (!l || l->phase != LCE_PHASE_RUNNING) return 0;
    int cancel = 0;
    for (uint32_t i = 0; i < l->sub_count; ++i) {
        const Sub &s = l->subs[i];
        if (s.ev != ev || l->mods[s.mod].rep.state != LCE_MOD_LIVE) continue;
        cancel |= s.fn(s.user, ev, payload) != 0;
    }
    return cancel;
}

extern "C" int32_t lce_loader_item_id(const lce_loader *l, const char *key) {
    if (!l || !key || (l->phase != LCE_PHASE_RUNNING && l->phase != LCE_PHASE_POST_INIT)) return -1;
    return item_ref_id(l, key);
}
extern "C" int32_t lce_loader_block_id(const lce_loader *l, const char *key) {
    if (!l || !key || (l->phase != LCE_PHASE_RUNNING && l->phase != LCE_PHASE_POST_INIT)) return -1;
    if (is_vanilla(key)) return lce_vanilla_block_id(key + 10);
    const SBlock *b = find_block(l, key);
    return b && mod_active(l, b->mod) ? (int32_t)b->id : -1;
}
