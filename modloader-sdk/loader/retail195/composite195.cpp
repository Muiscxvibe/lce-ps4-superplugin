// Composite retail195 backend and startup phase dispatcher. See composite195.h.
#include "composite195.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../modules/events195/ev195.h"
#include "../../modules/ui195/ui195.h"
#include "../../modules/script/lce_script.h"

extern "C" int lce195_sound_set_custom(const char *key, int32_t id);   // services195 (its header clashes with backend195 names)

namespace {

lce195c *g_c;                 // one composite per process
b195_module g_blocks;         // large (stock snapshots): process lifetime
le195_module g_entities;
p195_module g_proj;           // API 1.9 throwables (per-kind vtables: process lifetime)
#ifdef LCE195C_HOST_TEST
lce195c_phase_fn g_ovr[LCE195C_PH_COUNT];
#endif

const char *const kPhaseName[LCE195C_PH_COUNT] = {"boot", "block_hook", "blocks", "items", "after_items", "recipes", "smelt_brew",
                                                  "entity_types", "spawns", "worldgen", "renderers",
                                                  "effects", "strings", "potions", "enchants", "biomes", "audio", "projectiles", "fuel"};

void set_err(char *err, uint32_t cap, const char *msg) { if (err && cap) snprintf(err, cap, "%s", msg); }
void logf(const lce195c *c, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void logf(const lce195c *c, const char *fmt, ...) {
    if (!c || !c->bind.log) return;
    char line[300];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    c->bind.log(line);
}
uint8_t code_of(int rc) { return rc == 1 ? LCE195C_DONE : rc == 0 ? LCE195C_REFUSED : LCE195C_UNCERTAIN; }
void mark(lce195c *c, uint32_t ph, int rc, const char *detail) {
    c->phase[ph] = code_of(rc);
    logf(c, "LCE_SDK phase=%s rc=%d %s", kPhaseName[ph], rc, detail && *detail ? detail : "-");
}
void none(lce195c *c, uint32_t ph) { if (!c->phase[ph]) c->phase[ph] = LCE195C_NONE; }
uint64_t tid(const lce195c *c) { return c->bind.thread_id ? c->bind.thread_id(c->bind.host) : 0; }

#ifdef LCE195C_HOST_TEST
#define CALL(ph, module, arg, real) (g_ovr[ph] ? g_ovr[ph]((void *)(module), (uintptr_t)(arg)) : (real))
#else
#define CALL(ph, module, arg, real) (real)
#endif

// ---- wrapped module memory: forward to the bound one, own the phase checks --
int bm_read(void *, uintptr_t a, void *v, size_t n) { const b195_memory &m = g_c->bind.blocks_mem; return m.read(m.ctx, a, v, n); }
int bm_write(void *, uintptr_t a, const void *v, size_t n) { const b195_memory &m = g_c->bind.blocks_mem; return m.write(m.ctx, a, v, n); }
int bm_cas(void *, uintptr_t a, uintptr_t e, uintptr_t d) { const b195_memory &m = g_c->bind.blocks_mem; return m.cas(m.ctx, a, e, d); }
uint64_t bm_tid(void *) { const b195_memory &m = g_c->bind.blocks_mem; return m.thread_id(m.ctx); }
int bm_attested(void *) { const b195_memory &m = g_c->bind.blocks_mem; return m.attested(m.ctx); }
int bm_items_held(void *) { return g_c && g_c->items_window && tid(g_c) == g_c->items_thread ? 1 : 0; }
int bm_zero(void *) { return 0; }
// terrain (gap G1): shared_terrain_195 hands its reload-lease attestations in for the duration of a bind/detach call
const lce195c_terrain_lease *g_tlease;
uint32_t g_terrain_binds, g_terrain_gen; int g_terrain_rc;
bool acquire_terrain_lease(const lce195c_terrain_lease *lease) {
    const lce195c_terrain_lease *empty = nullptr;
    return __atomic_compare_exchange_n(&g_tlease, &empty, lease, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}
int bm_terrain_held(void *) { const lce195c_terrain_lease *l = __atomic_load_n(&g_tlease, __ATOMIC_ACQUIRE); return l && l->held && l->held() == 1 ? 1 : 0; }
int bm_uploaded(void *, uintptr_t atlas, uintptr_t tex, uint32_t gen) {
    const lce195c_terrain_lease *l = __atomic_load_n(&g_tlease, __ATOMIC_ACQUIRE);
    return l && l->uploaded && l->uploaded(atlas, tex, gen) == 1 ? 1 : 0;
}
int startup_held(void *) { return g_c && g_c->late_held && tid(g_c) == g_c->items_thread ? 1 : 0; }
// strings195/enchants195: the late phase or the per-tick strings ensure (main UI thread)
int strings_held(void *) { return g_c && (g_c->late_held || g_c->ui_held) && tid(g_c) == g_c->items_thread ? 1 : 0; }
// effects195: the item window (pre_collect) or the after-items fallback (startup thread)
int effects_held(void *) { return g_c && g_c->effect_window && tid(g_c) == g_c->items_thread ? 1 : 0; }
s195_env g_senv;
lce195e_effect_publisher g_effect_pub;
lce195e_potion_publisher g_potion_pub;

bool vanilla(const char *k) { return k && !strncmp(k, "minecraft:", 10); }
int32_t block_id(const lce_plan *p, const char *key) {
    if (!key) return -1;
    if (vanilla(key)) return lce_vanilla_block_id(key + 10);
    return p->resolve ? p->resolve(p->resolve_ctx, LCE_REF_BLOCK, key) : -1;
}
const lce_plan_ext *find_ext(const lce_plan *p, uint32_t type, const char *key) {
    for (uint32_t i = 0; key && i < p->ext_count; ++i)
        if (p->ext[i].type == type && p->ext[i].key && !strcmp(p->ext[i].key, key)) return &p->ext[i];
    return nullptr;
}
const char *en_name(const lce_plan *p, const char *key, const char *fallback) {
    for (uint32_t s = 0; s < p->string_count; ++s)
        if (!strcmp(p->strings[s].key, key) && !strcmp(p->strings[s].locale, "en_US")) return p->strings[s].value;
    return fallback;
}

// ---- mod string ids (strings195) -----------------------------------------------
const char *plan_string(const lce_plan *p, const char *key, const char *locale) {
    for (uint32_t s = 0; s < p->string_count; ++s)
        if (!strcmp(p->strings[s].key, key) && !strcmp(p->strings[s].locale, locale)) return p->strings[s].value;
    return nullptr;
}
bool push_text(lce195c *c, const char *lang, const char *utf8) {
    if (!utf8 || !*utf8 || c->text_count >= LCE195C_MAX_TEXTS || !c->text_pool) return false;
    uint32_t room = c->text_pool_cap - c->text_pool_used;
    uint32_t n = s195_utf8_to_utf16(utf8, c->text_pool + c->text_pool_used, room < S195_MAX_TEXT ? room : S195_MAX_TEXT);
    if (!n) return false;
    c->texts[c->text_count++] = s195_text{lang, c->text_pool + c->text_pool_used, n};
    c->text_pool_used += n;
    return true;
}
// One string id for `key` + `suffix` (".splash", ...): en_US first (package string, else `en_template` around the
// display name), then every other locale the package translates. 0 if the tables are full.
uint32_t new_sid(lce195c *c, const lce_plan *p, const char *key, const char *suffix, const char *en_template, const char *name) {
    if (c->sid_count >= LCE195C_MAX_SIDS) return 0;
    char k[LCE_MAX_KEY + 16], buf[256];
    snprintf(k, sizeof k, "%s%s", key, suffix);
    const char *en = plan_string(p, k, "en_US");
    if (!en) {
        const char *base = en_name(p, key, name ? name : key);
        // potion descriptors name the drinkable item ("Potion of X"): the variants are built around X
        if (strstr(en_template, "Potion of ") || strstr(en_template, "Arrow of "))
            if (!strncmp(base, "Potion of ", 10)) base += 10;
        snprintf(buf, sizeof buf, en_template, base);
        en = buf;
    }
    s195_entry &e = c->sids[c->sid_count];
    e.sid = s195_sid(c->sid_count);
    e.texts = &c->texts[c->text_count];
    uint32_t first = c->text_count;
    if (!push_text(c, "en_US", en)) return 0;
    for (uint32_t i = 0; i < p->string_count; ++i)
        if (!strcmp(p->strings[i].key, k) && strcmp(p->strings[i].locale, "en_US")) (void)push_text(c, p->strings[i].locale, p->strings[i].value);
    e.text_count = c->text_count - first;
    ++c->sid_count;
    return e.sid;
}
int publish_effects(lce195c *c) {   // effects195, ids ascending; 1/0/-1
    if (!c->effect_count) return 1;
    if (c->phase[LCE195C_PH_EFFECTS] && c->phase[LCE195C_PH_EFFECTS] != LCE195C_WAITING) return c->phase[LCE195C_PH_EFFECTS] == LCE195C_DONE ? 1 : c->phase[LCE195C_PH_EFFECTS] == LCE195C_REFUSED ? 0 : -1;
    c->items_thread = tid(c);
    c->effect_window = 1;
    memset(&g_effect_pub, 0, sizeof g_effect_pub);
    g_effect_pub.env = &c->effects_env;
    int rc = CALL(LCE195C_PH_EFFECTS, &g_effect_pub, 0, lce195e_effects_publish(&g_effect_pub, c->effect_specs, c->effect_count));
    c->effect_window = 0;
    char d[96];
    snprintf(d, sizeof d, "%u status effect(s) %s, ids %d..%d", c->effect_count, rc == 1 ? "published" : rc ? "UNCERTAIN" : "refused",
             c->effect_specs[0].id, c->effect_specs[c->effect_count - 1].id);
    mark(c, LCE195C_PH_EFFECTS, rc, d);
    return rc;
}
int pre_collect(void *user) { return publish_effects((lce195c *)user); }
// effects195 on_tick (server thread, from the applyEffectTick thunk): LCE_EV_EFFECT_TICK to subscribers + scripts
void effect_tick(void *user, int32_t effect_id, uintptr_t entity, int32_t amplifier) {
    lce195c *c = g_c;
    if (!c || !c->loader) return;
    lce_ev_entity ev;
    memset(&ev, 0, sizeof ev);
    ev.struct_size = sizeof ev; ev.entity = (void *)entity; ev.type_id = effect_id; ev.amount_milli = (uint32_t)amplifier * 1000u;
    ev.source = user;   // the effect key (process-lifetime loader copy)
    (void)lce195c_event_sink(c, LCE_EV_EFFECT_TICK, &ev);
}
uintptr_t mod_effect_object(const char *key) { return g_c && g_c->phase[LCE195C_PH_EFFECTS] == LCE195C_DONE ? lce195e_effect_object(key) : 0; }

int register_channel_hook(void *, const char *channel) {
    int rc = lce195_net_register_channel(channel);
    if (rc == 1 && g_c) {
        g_c->net_channels = 1;
        if (lce195_net_client_install() == 1) g_c->net_client_installed = 1;   // API 1.6 client packet hook (data words); retried per tick
    }
    return rc;
}

// ---- backend interface --------------------------------------------------------
int32_t resolve_group(void *self, const char *key, int crafting) { lce195c *c = (lce195c *)self; return c->items.iface.resolve_group(&c->items, key, crafting); }
lce_status check_item(void *self, const lce_item_desc *d, char *err, uint32_t cap) {
    lce195c *c = (lce195c *)self;
    if (d && (d->flags & LCE_ITEM_FLAG_THROWABLE)) {   // the loader validated the field ranges; these are projectiles195's
        if (!c->bind.projectiles) { set_err(err, cap, "retail195: projectiles195 natives not bound in this build"); return LCE_E_UNSUPPORTED; }
        if (d->kind != LCE_ITEM_GENERIC) { set_err(err, cap, "projectiles195: a throwable must be a generic item (it becomes a SnowballItem)"); return LCE_E_UNSUPPORTED; }
        if ((d->projectile_speed_milli && (d->projectile_speed_milli < 100 || d->projectile_speed_milli > 4000)) || d->projectile_inaccuracy_milli > 20000) {
            set_err(err, cap, "projectiles195: projectile speed 0.1..4 (snowball 1.5), inaccuracy 0..20 (snowball 1)");
            return LCE_E_UNSUPPORTED;
        }
    }
    return lce195_backend_check_item(&c->items, d, err, cap);
}
lce_status check_recipe(void *self, const lce_recipe_desc *d, char *err, uint32_t cap) { return lce195_backend_check_recipe(&((lce195c *)self)->items, d, err, cap); }
lce_status check_block(void *self, const lce_block_desc *d, char *err, uint32_t cap) {
    lce195c *c = (lce195c *)self;
    if (!c->bind.blocks) { set_err(err, cap, "retail195: blocks195 natives not bound in this build"); return LCE_E_UNSUPPORTED; }
    return b195_backend_check_block(nullptr, d, err, cap);
}

lce_status check_feature(const lce_feature_desc *d, char *err, uint32_t cap) {
    if (d->dimension == 1) { set_err(err, cap, "worldgen195: nether features are not possible: NetherLevelSource::postProcess 0x4b8090 never calls Biome decorate"); return LCE_E_UNSUPPORTED; }
    if (d->dimension > 2) { set_err(err, cap, "worldgen195: unknown dimension"); return LCE_E_INVALID; }
    if ((d->kind == LCE_FEATURE_ORE || d->kind == LCE_FEATURE_TREE || d->kind == LCE_FEATURE_LAKE) && !d->block) { set_err(err, cap, "worldgen195: feature needs a block"); return LCE_E_INVALID; }
    if (d->kind == LCE_FEATURE_TREE && !d->block2) { set_err(err, cap, "worldgen195: a tree needs block2 (leaves)"); return LCE_E_INVALID; }
    if (d->kind == LCE_FEATURE_STRUCTURE && !d->structure) { set_err(err, cap, "worldgen195: a structure feature needs a structure"); return LCE_E_INVALID; }
    for (uint32_t i = 0; i < d->biome_count; ++i) {
        const char *key = d->biomes[i];
        if (!strncmp(key, "minecraft:", 10)) {
            if (lce_wg195_biome_lookup(key) < 0) {
                snprintf(err, cap, "worldgen195: unknown biome %s", key);
                return LCE_E_NOTFOUND;
            }
        } else if (d->dimension != 0) {
            set_err(err, cap, "worldgen195: custom biomes are generated only in the Overworld");
            return LCE_E_UNSUPPORTED;
        }
    }
    // The loader has checked custom biome keys. Their IDs are available during prepare.
    lce_feature_desc base = *d;
    base.biomes = nullptr;
    base.biome_count = 0;
    lce_wg195_feature f;
    lce_status st = lce_wg195_from_desc(&base, 1, 0, 0, -1, &f, err, cap);
    if (st != LCE_OK) return st;
    if (d->kind == LCE_FEATURE_TREE && (d->size && (d->size < 3 || d->size > 8))) { set_err(err, cap, "worldgen195: tree base height (size) must be 3..8"); return LCE_E_INVALID; }
    if (d->kind == LCE_FEATURE_LAKE && d->min_y && d->min_y < 6) { set_err(err, cap, "worldgen195: lakes need min_y >= 6"); return LCE_E_INVALID; }
    return LCE_OK;
}

lce_status check_ext(void *self, uint32_t type, const void *desc, char *err, uint32_t cap) {
    lce195c *c = (lce195c *)self;
    switch (type) {
    case LCE_EXT_EFFECT: {
        if (!c->bind.effects || !c->bind.strings_bound) return lce195x_check_ext(type, desc, err, cap);
        const lce_effect_desc *d = (const lce_effect_desc *)desc;
        lce_status st = lce195e_effect_check(d, err, cap);
        if (st != LCE_OK) return st;
        lce195e_effect_spec spec;   // dry run: clone kind, colour, icon
        return lce195e_effect_spec_from_desc(d, 32, s195_sid(0), &spec, err, cap);
    }
    case LCE_EXT_POTION:
        if (!c->bind.effects || !c->bind.strings_bound) return lce195x_check_ext(type, desc, err, cap);
        return lce195e_potion_check((const lce_potion_desc *)desc, err, cap);
    case LCE_EXT_ENCHANT: {
        if (!c->bind.strings_bound) return lce195x_check_ext(type, desc, err, cap);
        const lce_enchant_desc *d = (const lce_enchant_desc *)desc;
        int clone = e195_clone_from_key(d->clone_of, err, cap);
        if (clone < 0) return LCE_E_UNSUPPORTED;
        e195_spec sp;
        memset(&sp, 0, sizeof sp);
        sp.key = d->key; sp.id = E195_ID_MIN; sp.clone = (uint32_t)clone; sp.category = e195_category_from_applies(d->applies_to);
        sp.max_level = d->max_level; sp.weight = d->weight; sp.treasure = d->treasure; sp.name_sid = s195_sid(0);
        return e195_check(&sp, err, cap) == 1 ? LCE_OK : LCE_E_UNSUPPORTED;
    }
    case LCE_EXT_FUEL:
        if (!c->items.xenv.allow_fuel) return lce195x_check_ext(type, desc, err, cap);
        return fuel195_check((const lce_fuel_desc *)desc, err, cap);
    case LCE_EXT_BREW:
        return lce195x_check_ext(type, desc, err, cap);
    case LCE_EXT_ENTITY: {
        if (!c->bind.entities) { set_err(err, cap, "retail195: entities195 natives not bound in this build"); return LCE_E_UNSUPPORTED; }
        le195_options opt;
        memset(&opt, 0, sizeof opt);   // texture_fallback 0: a custom skin is refused, never silently replaced
        return le195_check_entity((const lce_entity_desc2 *)desc, &opt, err, cap);
    }
    case LCE_EXT_FEATURE:
        if (!c->bind.worldgen) { set_err(err, cap, "retail195: worldgen195 natives not bound in this build"); return LCE_E_UNSUPPORTED; }
        return check_feature((const lce_feature_desc *)desc, err, cap);
    case LCE_EXT_BIOME: {
        if (!c->bind.biomes) { set_err(err, cap, "retail195: biomes195 natives not bound in this build"); return LCE_E_UNSUPPORTED; }
        if (!desc) return LCE_OK;   // placeholder for a removed mod's ledger id
        lce_b195_biome b;
        return lce_b195_from_desc((const lce_biome_desc *)desc, 168, 0, 0, 0, 0, &b, err, cap);   // dry run (id/blocks resolved at prepare)
    }
    case LCE_EXT_STRUCTURE: {
        const lce_structure_desc *d = (const lce_structure_desc *)desc;
        if (!d->size_x || !d->size_y || !d->size_z || d->size_x > 16 || d->size_z > 16 || d->size_y > 255) { set_err(err, cap, "worldgen195: structure templates must fit one chunk (x,z <= 16, y <= 255)"); return LCE_E_UNSUPPORTED; }
        if (!d->palette_count || d->palette_count > 256 || !d->blocks) { set_err(err, cap, "worldgen195: structure palette must have 1..256 entries"); return LCE_E_INVALID; }
        return LCE_OK;
    }
    case LCE_EXT_SOUND: {
        const lce_sound_desc *d = (const lce_sound_desc *)desc;
        if (d && (d->pcm || d->pcm_bytes)) {
            if (!c->bind.audio) { set_err(err, cap, "retail195: audio195 natives not bound in this build: custom PCM audio refused"); return LCE_E_UNSUPPORTED; }
            a195_spec a = {d->key, d->pcm, d->pcm_bytes, d->sample_rate, d->volume_milli / 1000.0f, d->pitch_milli / 1000.0f};
            return a195_check(&a, err, cap) == 1 ? LCE_OK : LCE_E_UNSUPPORTED;
        }
        return lce195g_check_sound(d, err, cap);
    }
    case LCE_EXT_PARTICLE: {
        char e[160] = {0};
        if (lce195_ui_particle_check((const lce_particle_desc *)desc, e, sizeof e) != 1) { set_err(err, cap, e[0] ? e : "ui195: particle refused"); return LCE_E_UNSUPPORTED; }
        return LCE_OK;
    }
    case LCE_EXT_COMMAND: case LCE_EXT_KEYBIND:
        if (!c->bind.event_caps) { set_err(err, cap, "retail195: events195 not bound"); return LCE_E_UNSUPPORTED; }
        return LCE_OK;
    default:
        set_err(err, cap, "retail195: unknown content type");
        return LCE_E_UNSUPPORTED;
    }
}

lce_status prepare_features(lce195c *c, const lce_plan *p, char *err, uint32_t cap) {
    c->feature_count = c->structure_count = 0;
    for (uint32_t i = 0; i < p->ext_count; ++i) {
        if (p->ext[i].type != LCE_EXT_FEATURE) continue;
        const lce_feature_desc *d = (const lce_feature_desc *)p->ext[i].desc;
        if (c->feature_count >= LCE195C_MAX_FEATURES) { set_err(err, cap, "worldgen195: more than 32 features"); return LCE_E_FULL; }
        lce195c_feature &cf = c->features[c->feature_count];
        memset(&cf, 0, sizeof cf);
        cf.structure = -1;
        int32_t b = d->block ? block_id(p, d->block) : 0, b2 = d->block2 ? block_id(p, d->block2) : 0;
        if (b < 0 || b > 365 || b2 < 0 || b2 > 365) { snprintf(err, cap, "worldgen195: feature %s: block not resolvable", d->key); return LCE_E_NOTFOUND; }
        int32_t replace = d->kind == LCE_FEATURE_ORE && d->block2 ? b2 : 0;
        lce_feature_desc base = *d;
        base.biomes = nullptr;
        base.biome_count = 0;
        lce_status st = lce_wg195_from_desc(&base, (uint16_t)b, 0, replace, replace ? 0 : -1, &cf.f, err, cap);
        if (st != LCE_OK) return st;
        for (uint32_t j = 0; j < d->biome_count; ++j) {
            const char *key = d->biomes[j];
            int32_t id = -1;
            if (!strncmp(key, "minecraft:", 10)) id = lce_wg195_biome_lookup(key);
            else if (const lce_plan_ext *biome = find_ext(p, LCE_EXT_BIOME, key); biome && biome->desc) id = biome->id;
            if (id < 0 || id >= 256) {
                snprintf(err, cap, "worldgen195: feature %s: biome %s unresolved", d->key, key);
                return LCE_E_NOTFOUND;
            }
            cf.f.biome_mask[id >> 6] |= UINT64_C(1) << (id & 63);
        }
        if (d->kind == LCE_FEATURE_TREE) { cf.trunk_id = (uint16_t)b; cf.leaves_id = (uint16_t)b2; if (d->size) cf.f.tree_base_height = d->size; }
        if (d->kind == LCE_FEATURE_LAKE) cf.lake_id = (uint16_t)b;
        if (d->kind == LCE_FEATURE_STRUCTURE) {
            const lce_plan_ext *sx = find_ext(p, LCE_EXT_STRUCTURE, d->structure);
            if (!sx || c->structure_count >= LCE195C_MAX_STRUCTURES) { snprintf(err, cap, "worldgen195: feature %s: structure %s missing or too many structures", d->key, d->structure); return LCE_E_NOTFOUND; }
            const lce_structure_desc *s = (const lce_structure_desc *)sx->desc;
            uint32_t k = c->structure_count;
            uint16_t *pal = c->palette[k];
            for (uint32_t j = 0; j < s->palette_count && j < 256; ++j) {
                int32_t id = block_id(p, s->palette[j]);
                if (id < 0 || id > 365) { snprintf(err, cap, "worldgen195: structure %s: palette block %s unresolved", sx->key, s->palette[j]); return LCE_E_NOTFOUND; }
                pal[j] = (uint16_t)(id << 4);
            }
            lce_wg195_structure &w = c->structures[k];
            w.size_x = s->size_x; w.size_y = s->size_y; w.size_z = s->size_z; w.palette_count = (uint16_t)s->palette_count;
            w.palette = pal; w.cells = s->blocks;
            cf.structure = (int32_t)k;
            cf.f.structure = &w;
            ++c->structure_count;
        }
        ++c->feature_count;
    }
    return LCE_OK;
}

// Effects, potions, enchantments, biomes, custom audio and their string ids. Module tables only (no game memory).
lce_status prepare_registries(lce195c *c, const lce_plan *p, char *err, uint32_t cap) {
    c->sid_count = c->text_count = c->text_pool_used = 0;
    c->effect_count = c->potion_count = c->enchant_count = c->biome_count = c->sound_pcm_count = 0;
    uint32_t named = 0;
    for (uint32_t i = 0; i < p->ext_count; ++i) {
        const uint32_t t = p->ext[i].type;
        named += t == LCE_EXT_EFFECT || t == LCE_EXT_ENCHANT ? 1 : t == LCE_EXT_POTION ? 4 : 0;
    }
    if (named) {
        if (!c->bind.strings_bound) { set_err(err, cap, "strings195: not bound"); return LCE_E_UNSUPPORTED; }
        if (named > LCE195C_MAX_SIDS) { set_err(err, cap, "strings195: more than 512 mod names"); return LCE_E_FULL; }
        if (!c->text_pool) {
            c->text_pool_cap = 1u << 17;   // 128 Ki UTF-16 units, process lifetime (the Localizer copies; re-inserts read it again)
            c->text_pool = (uint16_t *)malloc(sizeof(uint16_t) * c->text_pool_cap);
            if (!c->text_pool) { set_err(err, cap, "strings195: out of memory"); return LCE_E_FULL; }
        }
    }
    static a195_spec sounds[A195_MAX_SOUNDS];
    for (uint32_t i = 0; i < p->ext_count; ++i) {
        const lce_plan_ext &x = p->ext[i];
        char e[160] = {0};
        switch (x.type) {
        case LCE_EXT_EFFECT: {
            const lce_effect_desc *d = (const lce_effect_desc *)x.desc;
            if (c->effect_count >= LCE195E_MAX_EFFECTS) { set_err(err, cap, "effects195: more than 32 mod effects"); return LCE_E_FULL; }
            if (x.id < 32 || x.id > 127) { snprintf(err, cap, "effects195: effect %s: ledger id %d outside 32..127", x.key, x.id); return LCE_E_UNSUPPORTED; }
            uint32_t sid = new_sid(c, p, x.key, "", "%s", d->name);
            lce195e_effect_spec &es = c->effect_specs[c->effect_count];
            if (!sid || lce195e_effect_spec_from_desc(d, x.id, sid, &es, e, sizeof e) != LCE_OK) { snprintf(err, cap, "effect %s: %s", x.key, e[0] ? e : "no string id"); return LCE_E_UNSUPPORTED; }
            if (c->bind.event_caps & LCE_CAP_EV_ENTITY) {   // scripts/natives see EFFECT_TICK (custom behaviour, or on top of the clone)
                es.on_tick = effect_tick; es.user = (void *)x.key;
                if (!es.clone_of && !es.tick_interval) es.tick_interval = 20;
            }
            ++c->effect_count;
            break;
        }
        case LCE_EXT_POTION: {
            const lce_potion_desc *d = (const lce_potion_desc *)x.desc;
            if (c->potion_count >= LCE195E_MAX_POTIONS) { set_err(err, cap, "effects195: more than 64 mod potions"); return LCE_E_FULL; }
            uint32_t *sid = c->potion_sid[c->potion_count];
            sid[0] = new_sid(c, p, x.key, "", "Potion of %s", d->name);
            sid[1] = new_sid(c, p, x.key, ".splash", "Splash Potion of %s", d->name);
            sid[2] = new_sid(c, p, x.key, ".lingering", "Lingering Potion of %s", d->name);
            sid[3] = new_sid(c, p, x.key, ".arrow", "Arrow of %s", d->name);
            if (!sid[0] || !sid[1] || !sid[2] || !sid[3]) { snprintf(err, cap, "potion %s: string tables full", x.key); return LCE_E_FULL; }
            c->potion_ext[c->potion_count++] = &x;
            break;
        }
        case LCE_EXT_ENCHANT: {
            const lce_enchant_desc *d = (const lce_enchant_desc *)x.desc;
            if (c->enchant_count >= E195_MAX) { set_err(err, cap, "enchants195: more than 64 mod enchantments"); return LCE_E_FULL; }
            e195_spec &sp = c->enchant_specs[c->enchant_count];
            memset(&sp, 0, sizeof sp);
            int clone = e195_clone_from_key(d->clone_of, e, sizeof e);
            sp.key = x.key; sp.id = x.id; sp.clone = clone < 0 ? 0 : (uint32_t)clone; sp.category = e195_category_from_applies(d->applies_to);
            sp.max_level = d->max_level; sp.weight = d->weight; sp.treasure = d->treasure;
            sp.name_sid = new_sid(c, p, x.key, "", "%s", d->name);
            if (clone < 0 || !sp.name_sid || e195_check(&sp, e, sizeof e) != 1) { snprintf(err, cap, "enchantment %s: %s", x.key, e[0] ? e : "no string id"); return LCE_E_UNSUPPORTED; }
            ++c->enchant_count;
            break;
        }
        case LCE_EXT_BIOME: {
            if (!c->bind.biomes) { set_err(err, cap, "biomes195: not bound"); return LCE_E_UNSUPPORTED; }
            if (!c->biome_count && lce_b195_init(c->bind.base, c->bind.biomes, &c->bind.biomes_mem) != 1) { set_err(err, cap, "biomes195: init refused"); return LCE_E_UNSUPPORTED; }
            lce_b195_biome b;
            memset(&b, 0, sizeof b);
            if (x.id < 0 || x.id > 255 || !lce_b195_id_allowed((uint32_t)x.id)) { snprintf(err, cap, "biomes195: biome %s: ledger id %d not a free biome id", x.key, x.id); return LCE_E_UNSUPPORTED; }
            if (!x.desc) {   // removed mod: keep its id loadable, never generated
                snprintf(b.key, sizeof b.key, "%s", x.key);
                b.id = (uint8_t)x.id; b.temperature = 0.8f; b.rainfall = 0.4f; b.depth = 0.125f; b.scale = 0.05f; b.rain = 1; b.placeholder = 1; b.copies = 0;
            } else {
                const lce_biome_desc *d = (const lce_biome_desc *)x.desc;
                int32_t top = d->top_block ? block_id(p, d->top_block) : 0, fill = d->filler_block ? block_id(p, d->filler_block) : 0;
                if (top < 0 || fill < 0 || top > 365 || fill > 365) { snprintf(err, cap, "biomes195: biome %s: top/filler block unresolved", x.key); return LCE_E_NOTFOUND; }
                if (lce_b195_from_desc(d, (uint8_t)x.id, (uint16_t)top, 0, (uint16_t)fill, 0, &b, e, sizeof e) != LCE_OK) { snprintf(err, cap, "biome %s: %s", x.key, e); return LCE_E_UNSUPPORTED; }
            }
            if (lce_b195_add(&b, e, sizeof e) != 1) { snprintf(err, cap, "biome %s: %s", x.key, e); return LCE_E_UNSUPPORTED; }
            ++c->biome_count;
            break;
        }
        case LCE_EXT_SOUND: {
            const lce_sound_desc *d = (const lce_sound_desc *)x.desc;
            if (!d->pcm && !d->pcm_bytes) break;
            if (c->sound_pcm_count >= A195_MAX_SOUNDS) { set_err(err, cap, "audio195: more than 64 custom sounds"); return LCE_E_FULL; }
            sounds[c->sound_pcm_count++] = a195_spec{d->key, d->pcm, d->pcm_bytes, d->sample_rate, d->volume_milli / 1000.0f, d->pitch_milli / 1000.0f};
            break;
        }
        default: break;
        }
    }
    // effects: the publisher wants ascending ids
    for (uint32_t i = 1; i < c->effect_count; ++i)
        for (uint32_t j = i; j > 0 && c->effect_specs[j - 1].id > c->effect_specs[j].id; --j) {
            lce195e_effect_spec t = c->effect_specs[j]; c->effect_specs[j] = c->effect_specs[j - 1]; c->effect_specs[j - 1] = t;
        }
    if (c->sound_pcm_count) {
        a195_release(&c->audio);
        if (!c->bind.audio || a195_configure(&c->audio, c->bind.base, c->bind.audio, &c->bind.audio_mem, sounds, c->sound_pcm_count) != 1) {
            snprintf(err, cap, "audio195: %s", c->bind.audio ? c->audio.last_error : "not bound");
            return LCE_E_UNSUPPORTED;
        }
    }
    if (c->sid_count && s195_check(c->sids, c->sid_count, err, cap) != 1) return LCE_E_INVALID;
    return LCE_OK;
}

// ---- API 1.9 throwables (projectiles195) -----------------------------------------
struct ProjHit { lce195c *c; uint32_t k; void *projectile, *entity, *level; double x, y, z; };
uint32_t proj_roll(lce195c *c) {   // xorshift32 for effect chances (no libc rand on the console link)
    uint32_t v = c->proj_rng ? c->proj_rng : 0x9e3779b9u;
    v ^= v << 13; v ^= v >> 17; v ^= v << 5;
    c->proj_rng = v;
    return v % 100u;
}
int32_t floor_i(double v) { int32_t i = (int32_t)v; return (double)i > v ? i - 1 : i; }
void proj_apply(void *user) {   // server thread, inside a level frame (events195 lce195_ev_level_call)
    ProjHit &h = *(ProjHit *)user;
    lce195c *c = h.c;
    const lce_item_desc *d = c->proj_desc[h.k];
    const lce_api *api = c->loader ? lce_loader_api(c->loader) : nullptr;
    if (api && d) {
        if (h.entity) {
            if (d->projectile_damage_milli && api->damage_entity) (void)api->damage_entity(nullptr, h.entity, d->projectile_damage_milli, nullptr);
            if (d->projectile_fire_ticks && api->set_fire) (void)api->set_fire(nullptr, h.entity, d->projectile_fire_ticks);
            for (uint32_t i = 0; i < d->projectile_effect_count && api->add_effect; ++i) {
                const lce_effect_ref &e = d->projectile_effects[i];
                if (e.chance_percent && e.chance_percent < 100 && proj_roll(c) >= e.chance_percent) continue;
                (void)api->add_effect(nullptr, h.entity, &e);
            }
        }
        if (d->projectile_explode_milli && api->explode)
            (void)api->explode(nullptr, h.level, h.x, h.y, h.z, (float)d->projectile_explode_milli / 1000.0f, d->projectile_fire_ticks ? 1 : 0, 1);
    }
    lce_ev_entity ev;
    memset(&ev, 0, sizeof ev);
    ev.struct_size = sizeof ev; ev.entity = h.entity; ev.source = h.projectile; ev.type_id = (int32_t)c->proj_item_id[h.k];
    ev.amount_milli = d ? d->projectile_damage_milli : 0; ev.level = h.level;
    ev.x = floor_i(h.x); ev.y = floor_i(h.y); ev.z = floor_i(h.z);
    (void)lce195c_event_sink(c, LCE_EV_PROJECTILE_HIT, &ev);
}
void proj_hit(void *ctx, uint32_t k, void *projectile, void *entity, double x, double y, double z) {
    lce195c *c = (lce195c *)ctx;
    uintptr_t level = 0;
    if (!c || k >= c->proj_count || !g_proj.mem.read || g_proj.mem.read(g_proj.mem.ctx, (uintptr_t)projectile + P195_F_LEVEL, &level, 8) != 1 || !level) return;
    ProjHit h = {c, k, projectile, entity, (void *)level, x, y, z};
    if (lce195_ev_level_call(level, proj_apply, &h) == 1) ++c->proj_hits;
}
lce_status prepare_projectiles(lce195c *c, const lce_plan *p, char *err, uint32_t cap) {
    c->proj_count = 0;
    static p195_kind kinds[P195_MAX_KINDS];
    for (uint32_t i = 0; i < p->item_count; ++i) {
        const lce_item_desc *d = p->items[i].desc;
        if (!d || !(d->flags & LCE_ITEM_FLAG_THROWABLE)) continue;
        if (c->proj_count >= P195_MAX_KINDS) { set_err(err, cap, "projectiles195: more than 16 throwable items"); return LCE_E_FULL; }
        p195_kind &k = kinds[c->proj_count];
        memset(&k, 0, sizeof k);
        snprintf(k.key, sizeof k.key, "%s", d->key);
        k.speed = d->projectile_speed_milli / 1000.0f; k.inaccuracy = d->projectile_inaccuracy_milli / 1000.0f;
        c->proj_desc[c->proj_count] = d;
        c->proj_item_id[c->proj_count] = p->items[i].id;
        ++c->proj_count;
    }
    if (!c->proj_count) return LCE_OK;
    if (!c->bind.projectiles || p195_configure(&g_proj, c->bind.base, c->bind.projectiles, &c->bind.projectiles_mem, kinds, c->proj_count, proj_hit, c) != 1) {
        snprintf(err, cap, "projectiles195: %s", c->bind.projectiles ? g_proj.last_error : "not bound");
        return LCE_E_UNSUPPORTED;
    }
    return LCE_OK;
}
// After the item window: the throwable items exist as SnowballItem copies (items195); record them and arm the shoot slot.
void publish_projectiles(lce195c *c) {
    if (!c->proj_count) { none(c, LCE195C_PH_PROJECTILES); return; }
    if (c->phase[LCE195C_PH_PROJECTILES]) return;
    int rc = c->phase[LCE195C_PH_ITEMS] == LCE195C_DONE ? 1 : 0;
    for (uint32_t k = 0; rc == 1 && k < c->proj_count; ++k) {
        uintptr_t obj = 0;
        for (uint32_t i = 0; i < c->items.item_count; ++i)
            if (c->items.items[i].id == c->proj_item_id[k] && c->items.items[i].x.state == 2) obj = c->items.items[i].x.object;
        if (!obj || p195_bind_item(&g_proj, k, obj) != 1) rc = 0;
    }
    if (rc == 1) rc = CALL(LCE195C_PH_PROJECTILES, &g_proj, 0, p195_install(&g_proj));
    char d[200];
    snprintf(d, sizeof d, "%u throwable(s) %s", c->proj_count,
             rc == 1 ? "armed (SnowballItem copies, Snowball vt +0x860 shoot slot)" : c->phase[LCE195C_PH_ITEMS] != LCE195C_DONE ? "refused: SDK items not registered" : g_proj.last_error);
    mark(c, LCE195C_PH_PROJECTILES, rc, d);
}

lce_status prepare(void *self, lce_plan *p, char *err, uint32_t cap) {
    lce195c *c = (lce195c *)self;
    if (c->state) { set_err(err, cap, "retail195: already prepared"); return LCE_E_PHASE; }
    // Refuse duplicate declarations and oversized tables before publishing items.
    uint32_t fuel_ids[FUEL195_MAX], fuel_count = 0;
    for (uint32_t i = 0; i < p->item_count; ++i) {
        if (!p->items[i].desc || !p->items[i].desc->fuel_ticks) continue;
        if (fuel_count == FUEL195_MAX) { set_err(err, cap, "fuel195: too many fuel items"); return LCE_E_FULL; }
        fuel_ids[fuel_count++] = p->items[i].id;
    }
    for (uint32_t i = 0; i < p->ext_count; ++i) {
        const lce_plan_ext &x = p->ext[i];
        if (x.type != LCE_EXT_FUEL) continue;
        const lce_fuel_desc *d = (const lce_fuel_desc *)x.desc;
        lce_status checked = fuel195_check(d, err, cap);
        if (checked != LCE_OK) return checked;
        int32_t id = p->resolve ? p->resolve(p->resolve_ctx, LCE_REF_ITEM, d->item) : 0;
        if (id <= 0 || id > 65535) { set_err(err, cap, "fuel195: fuel item unresolved"); return LCE_E_NOTFOUND; }
        for (uint32_t j = 0; j < fuel_count; ++j)
            if (fuel_ids[j] == (uint32_t)id) { set_err(err, cap, "fuel195: duplicate fuel for item"); return LCE_E_COLLISION; }
        if (fuel_count == FUEL195_MAX) { set_err(err, cap, "fuel195: too many fuel items"); return LCE_E_FULL; }
        fuel_ids[fuel_count++] = (uint32_t)id;
    }
    lce_status st = c->items.iface.prepare(&c->items, p, err, cap);
    if (st != LCE_OK) return st;
    c->has_blocks = p->block_count != 0;
    if (c->has_blocks) {
        if (!c->bind.blocks) { set_err(err, cap, "retail195: blocks195 not bound"); return LCE_E_UNSUPPORTED; }
        static const char *names[B195_MAX_BLOCKS];
        for (uint32_t i = 0; i < p->block_count && i < B195_MAX_BLOCKS; ++i) names[i] = en_name(p, p->blocks[i].desc->key, p->blocks[i].desc->name);
        b195_memory m = {c, bm_read, bm_write, bm_cas, bm_tid, bm_attested, bm_zero, bm_items_held, bm_terrain_held, bm_uploaded};
        if (b195_configure(&g_blocks, c->bind.base, c->bind.blocks, &m, p->blocks, p->block_count, p->textures, p->texture_count, names) != 1) {
            snprintf(err, cap, "blocks195: %s", g_blocks.last_error);
            return LCE_E_UNSUPPORTED;
        }
        // W1b: plugins load before the eboot's static constructors (console 2026-11-19), so the registry object may not
        // exist at the hook phase; blocks195 then patches the registry class vtable's register slot (RELRO) instead.
        if (c->bind.biomes_mem.patch_ro8) b195_set_patch_ro8(&g_blocks, c->bind.biomes_mem.patch_ro8, c->bind.biomes_mem.ctx);
    }
    uint32_t entities = 0;
    for (uint32_t i = 0; i < p->ext_count; ++i) entities += p->ext[i].type == LCE_EXT_ENTITY;
    c->has_entities = entities != 0;
    if (c->has_entities) {
        le195_options opt;
        memset(&opt, 0, sizeof opt);
        char e[160] = {0};
        if (!c->bind.entities || le195_configure(&g_entities, c->bind.base, c->bind.entities, &c->bind.entities_mem, &opt, p, e, sizeof e) != 1) {
            snprintf(err, cap, "entities195: %s", e[0] ? e : g_entities.last_error);
            return LCE_E_UNSUPPORTED;
        }
    }
    st = prepare_features(c, p, err, cap);
    if (st != LCE_OK) return st;
    st = prepare_registries(c, p, err, cap);
    if (st != LCE_OK) return st;
    st = prepare_projectiles(c, p, err, cap);
    if (st != LCE_OK) return st;
    c->fuel_count = fuel_count;
    c->state = 1;
    return LCE_OK;
}

lce_status publish(void *self, const lce_plan *p, int *partial, char *err, uint32_t cap) {
    lce195c *c = (lce195c *)self;
    *partial = 0;
    if (c->state != 1) { set_err(err, cap, "retail195: publish without prepare"); return LCE_E_PHASE; }
    lce_status st = c->items.iface.publish(&c->items, p, partial, err, cap);
    if (st != LCE_OK) { c->state = 3; return st; }
    char e[160] = {0};
    if (lce195g_register_plan(p, e, sizeof e) != 1) {
        // module-owned tables only (no game memory), but the item/recipe providers are armed: report PARTIAL
        *partial = 1;
        c->state = 3;
        snprintf(err, cap, "events195: %s", e[0] ? e : "plan registration refused");
        return LCE_E_BACKEND;
    }
    c->plan = p;
    c->state = 2;
    return LCE_OK;
}

void route_blocks(lce195c *c) {
    if (!(c->bind.event_caps & LCE_CAP_EV_BLOCK)) return;
    const bool use_route = (c->bind.event_caps2 & LCE_CAP2_EV_BLOCK_USE) != 0;
    for (uint32_t i = 0; i < g_blocks.count; ++i) {
        b195_block &b = g_blocks.blocks[i];
        if (!b.object) continue;
        uintptr_t *slot = &b.vtable[2 + 0x3e0 / 8];
        if (lce195_ev_route_block(b.object, b.id, *slot) == 1) { __atomic_store_n(slot, (uintptr_t)&lce195_ev_block_will_destroy_thunk, __ATOMIC_RELEASE); ++c->block_routes; }
        uintptr_t *use_slot = &b.vtable[2 + 0x2c0 / 8];   // Block::use (stock 0x940b0), events195 natives195.h
        if (use_route && lce195_ev_route_block_use(b.object, b.id, *use_slot) == 1) __atomic_store_n(use_slot, (uintptr_t)&lce195_ev_block_use_thunk, __ATOMIC_RELEASE);
        // API 1.3 block sources (not gated on event_caps2: events195 advertises them once a route exists)
        uintptr_t *t = &b.vtable[2 + 0x1f0 / 8];   // tick (random/scheduled)
        if (lce195_ev_route_block_tick(b.object, b.id, *t) == 1) {
            __atomic_store_n(t, (uintptr_t)&lce195_ev_block_tick_thunk, __ATOMIC_RELEASE);
            if (b.desc && (b.desc->flags & LCE_BLOCK_FLAG_RANDOM_TICK)) (void)lce195_ev_set_block_random_ticking(b.object, 1);
        }
        uintptr_t *nb = &b.vtable[2 + 0x208 / 8];   // neighborChanged
        if (lce195_ev_route_block_neighbor(b.object, b.id, *nb) == 1) __atomic_store_n(nb, (uintptr_t)&lce195_ev_block_neighbor_thunk, __ATOMIC_RELEASE);
        uintptr_t *so = &b.vtable[2 + 0x2c8 / 8];   // stepOn
        if (lce195_ev_route_block_step_on(b.object, b.id, *so) == 1) __atomic_store_n(so, (uintptr_t)&lce195_ev_block_step_on_thunk, __ATOMIC_RELEASE);
        uintptr_t *in = &b.vtable[2 + 0x328 / 8];   // entityInside
        if (lce195_ev_route_block_inside(b.object, b.id, *in) == 1) __atomic_store_n(in, (uintptr_t)&lce195_ev_block_inside_thunk, __ATOMIC_RELEASE);
        if (!b.item) continue;
        uintptr_t *use = &b.item_vtable[2 + 0x28 / 8];
        if (lce195_ev_route_block_item(b.item, b.id, b.object, *use) == 1) { __atomic_store_n(use, (uintptr_t)&lce195_ev_block_item_use_on_thunk, __ATOMIC_RELEASE); ++c->block_item_routes; }
    }
}

// Real module phase calls (the test seam swaps these).
int real_blocks_done() {
    if (g_blocks.hook_state == B195_HOOK_FIRED) return g_blocks.hook_publish_result;
    if (g_blocks.hook_state == B195_HOOK_ARMED) { (void)b195_hook_disarm(&g_blocks); return 0; }   // relay never fired: missed
    return g_blocks.state == B195_BLOCKS_DONE ? 1 : 0;
}

int worldgen_publish(lce195c *c) {
    if (!c->bind.worldgen) return 0;
    if (lce_wg195_init(c->bind.base, c->bind.worldgen, &c->bind.worldgen_mem) != 1) { snprintf(c->last_error, sizeof c->last_error, "worldgen195 init refused"); return 0; }
    uintptr_t reg = 0;
    const lce_wg195_memory &m = c->bind.worldgen_mem;
    for (uint32_t i = 0; i < c->feature_count; ++i) {
        lce195c_feature &cf = c->features[i];
        if (cf.f.kind == LCE_FEATURE_TREE || cf.f.kind == LCE_FEATURE_LAKE) {
            if (!reg && (m.read(m.ctx, c->bind.base + B195_RVA_REGISTRY_GLOBAL, &reg, 8) != 1 || !reg)) { snprintf(c->last_error, sizeof c->last_error, "block registry unreadable"); return 0; }
            uintptr_t blocks[2] = {0, 0}, states[2] = {0, 0};
            const uint16_t ids[2] = {cf.f.kind == LCE_FEATURE_TREE ? cf.trunk_id : cf.lake_id, cf.leaves_id};
            for (int k = 0; k < (cf.f.kind == LCE_FEATURE_TREE ? 2 : 1); ++k)
                if (m.read(m.ctx, reg + B195_REGISTRY_SLOTS_OFF + 8u * ids[k], &blocks[k], 8) != 1 || !blocks[k] ||
                    m.read(m.ctx, blocks[k] + B195_F_DEFSTATE, &states[k], 8) != 1 || !states[k]) { snprintf(c->last_error, sizeof c->last_error, "feature block %u unresolved", ids[k]); return 0; }
            if (cf.f.kind == LCE_FEATURE_TREE) { cf.f.trunk_state = (void *)states[0]; cf.f.leaves_state = (void *)states[1]; }
            else cf.f.lake_block = (void *)blocks[0];
        }
        char e[128] = {0};
        if (lce_wg195_add_feature(&cf.f, e, sizeof e) != 1) { snprintf(c->last_error, sizeof c->last_error, "feature %u: %s", i, e); return 0; }
    }
    int rc = lce_wg195_prepare();
    if (rc != 1) { snprintf(c->last_error, sizeof c->last_error, "worldgen195 prepare rc=%d", rc); return rc; }
    rc = lce_wg195_publish();
    if (rc != 1) { lce_wg195_status s; lce_wg195_snapshot(&s); snprintf(c->last_error, sizeof c->last_error, "worldgen195 publish: %s", s.last_error); }
    return rc;
}

}  // namespace

extern "C" uint32_t lce195c_capabilities(const lce195c *c) {
    if (!c) return 0;
    uint32_t caps = lce195_backend_caps(&c->items);
    if (c->bind.blocks) caps |= LCE_CAP_BLOCKS | LCE_CAP_BLOCK_ENTITY;   // dispenser- and chest-backed blocks (blocks195)
    if (c->bind.entities) caps |= LCE_CAP_ENTITIES;
    if (c->bind.worldgen) caps |= LCE_CAP_WORLDGEN | LCE_CAP_STRUCTURES;
    if (c->bind.event_caps) caps |= c->bind.event_caps | LCE_CAP_SOUNDS;   // sound clones: services195 play_sound / registry
    caps |= LCE_CAP_PARTICLES;                                           // tinted clones, stored as data (ui195)
    if (c->bind.services) caps |= LCE_CAP_SERVICES;
    if (c->bind.effects && c->bind.strings_bound) caps |= LCE_CAP_EFFECTS;
    if (c->bind.strings_bound) caps |= LCE_CAP_ENCHANTS;
    if (c->bind.biomes) caps |= LCE_CAP_BIOMES;
    if (c->items.xenv.allow_fuel) caps |= LCE_CAP_FUEL;
    return caps & ~LCE_CAP_NET_SYNC;
}

extern "C" void lce195c_init(lce195c *c, const lce195c_bindings *bind) {
    memset(c, 0, sizeof *c);
    if (bind) c->bind = *bind;
    g_c = c;
    lce195_memory mem = c->bind.mem;
    mem.startup_held = startup_held;
    lce195_backend_init(&c->items, c->bind.base, &c->bind.core, &c->bind.seams, &mem);
    if (c->bind.items) lce195_backend_bind_items(&c->items, c->bind.items);
    const fuel195_ops *f = c->bind.fuel;
    c->items.xenv.allow_fuel = f && f->read && f->write && f->cas8 && f->alloc &&
        c->bind.no_level && c->items.xenv.n.item_by_id && c->bind.base;
    if (c->bind.event_caps & LCE_CAP_EV_ITEM_USE) {
        lce195_use_route r = {lce195_ev_route_item, (uintptr_t)&lce195_ev_item_use_thunk};
        lce195_backend_set_use_route(&c->items, &r);
        c->items.inv_route = lce195_use_route{lce195_ev_route_item_inventory_tick, (uintptr_t)&lce195_ev_item_inventory_tick_thunk};
    }
    g_senv = c->bind.strings;
    g_senv.held = strings_held;
    if (c->bind.effects && c->bind.strings_bound) {
        c->effects_env = *c->bind.effects;
        c->effects_env.m = lce195e_memory{nullptr, c->bind.mem.read, c->bind.mem.write, effects_held};
        c->items.pre_collect = pre_collect; c->items.pre_user = c; c->items.mod_effect = mod_effect_object;
        lce195x_set_mod_potion_resolver(lce195e_potion_id);
    } else lce195x_set_mod_potion_resolver(nullptr);
    c->world_entry = 1;
    c->iface.name = "retail195";
    c->iface.self = c;
    c->iface.resolve_group = resolve_group;
    c->iface.check_item = check_item;
    c->iface.check_recipe = check_recipe;
    c->iface.check_block = check_block;
    c->iface.check_ext = check_ext;
    c->iface.prepare = prepare;
    c->iface.publish = publish;
    c->iface.capabilities = lce195c_capabilities(c);
    c->iface.capabilities2 = c->bind.event_caps2;
    if (c->bind.blocks) c->iface.capabilities2 |= LCE_CAP2_BLOCK_STATES | LCE_CAP2_BLOCK_BOXES;   // API 1.11: blocks195 render shape 10 + box list   // API 1.7: blocks195 validates each block
    // API 1.9: throwables need projectiles195 (items195 then builds them as SnowballItems over p195_item_use)
    lce195x_set_throw_use(c->bind.projectiles ? (uintptr_t)&p195_item_use : 0);
    lce195_ev_allow_item_use_original(c->bind.projectiles ? (uintptr_t)&p195_item_use : 0);   // ITEM_USE still fires for throwables
    if (c->bind.projectiles) c->iface.capabilities2 |= LCE_CAP2_PROJECTILES;
    c->iface.capabilities2 |= LCE_CAP2_BOATS;   // API 1.10: items195 builds BOAT items as BoatItem copies (no hook, no extra natives)
    // API 1.3 sources are routed per SDK block/item only after the item window, but mods subscribe at
    // REGISTER: advertise them whenever the route can be installed (events bound + that content kind).
    if (c->bind.event_caps & LCE_CAP_EV_BLOCK) {
        if (c->bind.blocks) c->iface.capabilities2 |= LCE_CAP2_EV_BLOCK_TICK | LCE_CAP2_EV_NEIGHBOR | LCE_CAP2_EV_BLOCK_COLLIDE;
        c->iface.capabilities2 |= LCE_CAP2_EV_INVENTORY_TICK;
    }
    // API 1.4 networking: events195 routes the host player's packet listener on the first retained tick after a
    // channel exists; mods register channels and subscribe at REGISTER, so advertise it whenever events are bound.
    if (c->bind.event_caps) {
        c->iface.capabilities2 |= LCE_CAP2_EV_PAYLOAD | LCE_CAP2_EV_CLIENT_PAYLOAD;
        c->iface.register_channel = register_channel_hook;
    }
}

extern "C" void lce195c_attach(lce195c *c, lce_loader *l) { if (c) c->loader = l; }

extern "C" int lce195c_phase_blocks_hook(lce195c *c) {
    if (!c || c->state != 2) return 0;
    if (!c->has_blocks) { none(c, LCE195C_PH_BLOCK_HOOK); return 1; }
    if (c->phase[LCE195C_PH_BLOCK_HOOK] == LCE195C_DONE) return 1;
    if (c->phase[LCE195C_PH_BLOCK_HOOK] == LCE195C_REFUSED || c->phase[LCE195C_PH_BLOCK_HOOK] == LCE195C_UNCERTAIN) return c->phase[LCE195C_PH_BLOCK_HOOK] == LCE195C_REFUSED ? 0 : -1;
    int rc = CALL(LCE195C_PH_BLOCK_HOOK, &g_blocks, 0, b195_hook_install(&g_blocks));
    if (rc == 0 && g_blocks.hook_state == B195_HOOK_NONE && strstr(g_blocks.last_error, "retry later")) {
        c->phase[LCE195C_PH_BLOCK_HOOK] = LCE195C_WAITING;   // registry global not built yet: the caller retries
        return 0;
    }
    mark(c, LCE195C_PH_BLOCK_HOOK, rc, rc == 1 ? "registry vptr -> private copy (+0x28 relay), data hook" : g_blocks.last_error);
    return rc;
}

extern "C" int lce195c_phase_after_items(lce195c *c) {
    if (!c || c->state != 2) return 0;
    lce195c_net_tick(c);
    if (c->phase[LCE195C_PH_AFTER_ITEMS]) return c->phase[LCE195C_PH_AFTER_ITEMS] == LCE195C_DONE || c->phase[LCE195C_PH_AFTER_ITEMS] == LCE195C_NONE ? 1 : c->phase[LCE195C_PH_AFTER_ITEMS] == LCE195C_REFUSED ? 0 : -1;
    // record the item/recipe outcome reached so far (the providers ran inside the shipping phases)
    uint32_t is = c->items.items_state;
    if (c->items.item_count) c->phase[LCE195C_PH_ITEMS] = is == LCE195_SDK_ITEMS_DONE ? LCE195C_DONE : is == LCE195_SDK_UNCERTAIN ? LCE195C_UNCERTAIN : is ? LCE195C_REFUSED : LCE195C_NOT_RUN;
    else none(c, LCE195C_PH_ITEMS);
    // Our collect() only BUILDS the objects; the shipping publisher (lce195_native_items_publish, 0x3af660)
    // registers them afterwards and may still refuse (STEEL_TOOLS registry=4). Check Item::items itself.
    if (c->items.item_count && c->phase[LCE195C_PH_ITEMS] == LCE195C_DONE && c->items.xenv.n.item_by_id) {
        uint32_t missing = 0, first = 0;
        for (uint32_t i = 0; i < c->items.item_count; ++i) {
            const lce195_sdk_item &it = c->items.items[i];
            if (it.x.object && c->items.xenv.n.item_by_id(it.id) != it.x.object) { if (!missing++) first = it.id; }
        }
        char d[200];
        if (missing) {
            c->phase[LCE195C_PH_ITEMS] = LCE195C_REFUSED;
            snprintf(d, sizeof d, "%u of %u SDK item(s) built but NOT in Item::items (first id %u): the shipping publisher refused them (see STEEL_TOOLS registry=)", missing, c->items.item_count, first);
            logf(c, "LCE_SDK phase=items rc=0 %s", d);
        } else {
            logf(c, "LCE_SDK phase=items rc=1 %u SDK item(s) registered in Item::items", c->items.item_count);
        }
    }
    // Mod status effects: normally published by pre_collect inside the item window; without SDK items the
    // provider never runs, so publish here (still the startup thread, before any UI scene).
    if (c->effect_count && !c->phase[LCE195C_PH_EFFECTS]) (void)publish_effects(c);
    else if (!c->effect_count) none(c, LCE195C_PH_EFFECTS);
    publish_projectiles(c);   // vtable data only, no game objects
    if (!c->has_blocks) { none(c, LCE195C_PH_BLOCKS); none(c, LCE195C_PH_AFTER_ITEMS); return 1; }
    int blocks = CALL(LCE195C_PH_BLOCKS, &g_blocks, 0, real_blocks_done());
    mark(c, LCE195C_PH_BLOCKS, blocks, blocks == 1 ? "blocks registered by the relay after id 365" : g_blocks.last_error[0] ? g_blocks.last_error : "block relay never fired (hook missed)");
    for (uint32_t i = 0; i < g_blocks.count; ++i)   // blocks195 drops a block whose construction failed, alone (never registered)
        if (g_blocks.blocks[i].dropped)
            logf(c, "LCE_SDK block %s dropped (not registered): %s", g_blocks.blocks[i].desc ? g_blocks.blocks[i].desc->key : "?", g_blocks.blocks[i].drop_reason);
    if (blocks != 1) { c->phase[LCE195C_PH_AFTER_ITEMS] = code_of(blocks < 0 ? -1 : 0); return blocks < 0 ? -1 : 0; }
    c->items_thread = tid(c);
    __atomic_store_n(&c->items_window, 1u, __ATOMIC_RELEASE);
    int rc = CALL(LCE195C_PH_AFTER_ITEMS, &g_blocks, 0, b195_items_publish(&g_blocks));
    __atomic_store_n(&c->items_window, 0u, __ATOMIC_RELEASE);
    if (rc == 1) route_blocks(c);
    char d[288];
    snprintf(d, sizeof d, "blockitems/map/names/fire %s; block event routes %u, block-item routes %u", rc == 1 ? "published" : g_blocks.last_error, c->block_routes, c->block_item_routes);
    mark(c, LCE195C_PH_AFTER_ITEMS, rc, d);
    return rc;
}

extern "C" int lce195c_phase_late(lce195c *c) {
    if (!c || c->state != 2) return 0;
    lce195c_net_tick(c);
    if (c->phase[LCE195C_PH_SMELT_BREW] && c->phase[LCE195C_PH_SMELT_BREW] != LCE195C_WAITING) return 1;   // once
    if (!c->bind.no_level || c->bind.no_level(c->bind.host) != 1) { c->phase[LCE195C_PH_SMELT_BREW] = LCE195C_WAITING; return 0; }
    c->items_thread = tid(c);
    c->late_held = 1;
    int result = 1;
    // recipes outcome (crafting pre-cache ran before the title)
    uint32_t rs = c->items.recipes_state;
    if (c->plan && c->plan->recipe_count) c->phase[LCE195C_PH_RECIPES] = rs == LCE195_SDK_RECIPES_DONE ? LCE195C_DONE : rs == LCE195_SDK_UNCERTAIN ? LCE195C_UNCERTAIN : rs ? LCE195C_REFUSED : LCE195C_NOT_RUN;
    else none(c, LCE195C_PH_RECIPES);
    // 0. mod string ids, potions (before brewing), enchantments
    if (c->sid_count) {
        s195_report r;
        memset(&r, 0, sizeof r);
        int rc = CALL(LCE195C_PH_STRINGS, &g_senv, 0, s195_ensure(&g_senv, c->sids, c->sid_count, &r));
        char d[160];
        snprintf(d, sizeof d, "%u string id(s) 0x%08x.. languages=%u inserted=%u present=%u failed=%u", c->sid_count, c->sids[0].sid, r.languages, r.inserted, r.present, r.failed);
        mark(c, LCE195C_PH_STRINGS, rc, d);
        if (rc < 0) result = -1; else if (rc == 0 && result > 0) result = 0;   // names missing: content still works, ensure retries every tick
    } else none(c, LCE195C_PH_STRINGS);
    if (c->potion_count) {
        int rc = 1;
        static lce195e_potion_spec specs[LCE195E_MAX_POTIONS];
        char e[160] = {0};
        c->effect_window = 1;
        for (uint32_t i = 0; i < c->potion_count && rc == 1; ++i) {
            uint32_t sid[6] = {c->potion_sid[i][0], c->potion_sid[i][1], c->potion_sid[i][2], c->potion_sid[i][3], 0, 0};
            if (lce195e_potion_spec_from_desc(&c->effects_env, (const lce_potion_desc *)c->potion_ext[i]->desc, sid, &specs[i], e, sizeof e) != LCE_OK) rc = 0;
            else specs[i].key = c->potion_ext[i]->key;
        }
        if (rc == 1) {
            memset(&g_potion_pub, 0, sizeof g_potion_pub);
            g_potion_pub.env = &c->effects_env;
            rc = CALL(LCE195C_PH_POTIONS, &g_potion_pub, 0, lce195e_potions_publish(&g_potion_pub, specs, c->potion_count));
        }
        c->effect_window = 0;
        char d[200];
        snprintf(d, sizeof d, "%u potion type(s) %s", c->potion_count, rc == 1 ? "published (potion, splash, lingering, tipped arrow)" : e[0] ? e : rc ? "UNCERTAIN" : "refused");
        mark(c, LCE195C_PH_POTIONS, rc, d);
        if (rc < 0) result = -1; else if (rc == 0 && result > 0) result = 0;
    } else none(c, LCE195C_PH_POTIONS);
    if (c->enchant_count) {
        int rc = CALL(LCE195C_PH_ENCHANTS, c->enchant_records, 0, e195_publish(&g_senv, c->enchant_specs, c->enchant_count, c->enchant_records));
        char d[160];
        snprintf(d, sizeof d, "%u enchantment(s) %s, ids %d..", c->enchant_count, rc == 1 ? "published via the registry +0x28" : rc ? "UNCERTAIN" : "refused", c->enchant_specs[0].id);
        mark(c, LCE195C_PH_ENCHANTS, rc, d);
        if (rc < 0) result = -1; else if (rc == 0 && result > 0) result = 0;
    } else none(c, LCE195C_PH_ENCHANTS);
    // biome objects before spawns/worldgen (both may name mod biomes)
    int biomes_built = 0;
    if (c->biome_count) {
        biomes_built = CALL(LCE195C_PH_BIOMES, nullptr, 1, lce_b195_construct());
        if (biomes_built == 1) lce_wg195_set_mod_biome_lookup(lce_b195_lookup);
        else {
            lce_b195_status bs; lce_b195_snapshot(&bs);
            mark(c, LCE195C_PH_BIOMES, biomes_built, bs.last_error);
            if (biomes_built < 0) result = -1; else if (result > 0) result = 0;
        }
    } else none(c, LCE195C_PH_BIOMES);
    // 1. furnace + brewing (data only)
    if (c->items.smelt_count || c->items.brew_count) {
        int rc = lce195_backend_publish_late(&c->items);
        char d[224];
        snprintf(d, sizeof d, "smelting %u brewing %u %s", c->items.smelt_count, c->items.brew_count, rc == 1 ? "published" : c->items.last_error);
        mark(c, LCE195C_PH_SMELT_BREW, rc, d);
        if (rc < 0) result = -1; else if (rc == 0 && result > 0) result = 0;
    } else c->phase[LCE195C_PH_SMELT_BREW] = LCE195C_NONE;
    // Install fuel hooks only after the item and block-entity registries exist.
    if (c->fuel_count) {
        fuel195_table table{};
        char e[160] = {};
        lce_status st = fuel195_collect_plan(c->plan, c->items.xenv.n.item_by_id, &table, e, sizeof e);
        fuel195_runtime *fuel = fuel195_rt();
        int rc = st == LCE_OK && c->items.xenv.allow_fuel ? fuel195_activate(fuel, c->bind.fuel, c->bind.base, &table) : 0;
        if (rc == 1) snprintf(e, sizeof e, "installed %u fuel(s)", table.count);
        else if (st == LCE_OK) snprintf(e, sizeof e, "attachment failed at %s", fuel->failed_step ? fuel->failed_step : "initial validation");
        mark(c, LCE195C_PH_FUEL, rc, e);
        if (rc != 1) {
            c->world_entry = 0;
            logf(c, "LCE_SDK WORLD_ENTRY BLOCKED: custom fuel installation rc=%d", rc);
            if (rc < 0) result = -1; else if (result > 0) result = 0;
        }
    } else none(c, LCE195C_PH_FUEL);
    // 2. entity types -> spawns -> renderers (renderers are a hard precondition of world entry)
    if (c->has_entities) {
        uintptr_t disp = c->bind.renderer_dispatcher ? c->bind.renderer_dispatcher(c->bind.host) : 0;
        if (!disp) {
            mark(c, LCE195C_PH_ENTITY_TYPES, 0, "renderer dispatcher (Minecraft+0x220) not available: custom entity types NOT registered (world entry unaffected)");
            if (result > 0) result = 0;
        } else {
            int t = CALL(LCE195C_PH_ENTITY_TYPES, &g_entities, 0, le195_publish_types(&g_entities));
            mark(c, LCE195C_PH_ENTITY_TYPES, t, t == 1 ? "registered via 0x28d9c0 after 0x28dc10" : g_entities.last_error);
            if (t == 1) {
                int s = CALL(LCE195C_PH_SPAWNS, &g_entities, 0, le195_publish_spawns(&g_entities, lce_wg195_biome_lookup));
                mark(c, LCE195C_PH_SPAWNS, s, s == 1 ? "biome spawn lists" : g_entities.last_error);
                int r = CALL(LCE195C_PH_RENDERERS, &g_entities, disp, le195_publish_renderers(&g_entities, disp));
                mark(c, LCE195C_PH_RENDERERS, r, r == 1 ? "renderer map entries for every custom mask" : g_entities.last_error);
                if (r != 1) {
                    c->world_entry = 0;   // a custom mob in a save would crash getRenderer 0xbd54c0
                    logf(c, "LCE_SDK WORLD_ENTRY BLOCKED: entity types registered but renderers rc=%d; do not open a world", r);
                    result = -1;
                }
                if (s < 0) result = -1; else if (s == 0 && result > 0) result = 0;
            } else if (t == 0 && result > 0) {
                result = 0;
            } else if (t < 0) {
                c->world_entry = 0;
                logf(c, "LCE_SDK WORLD_ENTRY BLOCKED: entity type registration uncertain");
                result = -1;
            }
        }
    }
    // 3. worldgen (biome decorate data hook)
    if (c->feature_count) {
        int w = CALL(LCE195C_PH_WORLDGEN, c, 0, worldgen_publish(c));
        char d[256];
        snprintf(d, sizeof d, "%u feature(s) %s", c->feature_count, w == 1 ? "published (Biome vptrs -> private decorate copies)" : c->last_error);
        mark(c, LCE195C_PH_WORLDGEN, w, d);
        if (w < 0) result = -1; else if (w == 0 && result > 0) result = 0;
    } else none(c, LCE195C_PH_WORLDGEN);
    // 4. mod biome colours (private vtables adopted by worldgen's decorate gate) + generation (BiomeInitLayer pools)
    if (c->biome_count && biomes_built == 1) {
        int col = CALL(LCE195C_PH_BIOMES, nullptr, 2, lce_b195_colours(c->bind.worldgen ? lce_wg195_adopt_vptr : nullptr));
        int sel = col == 1 ? CALL(LCE195C_PH_BIOMES, nullptr, 3, lce_b195_select()) : 0;
        lce_b195_status bs; lce_b195_snapshot(&bs);
        int rc = col != 1 ? col : sel;
        char d[224];
        snprintf(d, sizeof d, "built=%u coloured=%u select=%d %s", bs.built, bs.coloured, sel, rc == 1 ? "(BiomeInitLayer getArea slot, RELRO data)" : bs.last_error);
        mark(c, LCE195C_PH_BIOMES, rc, d);
        if (rc < 0) result = -1; else if (rc == 0 && result > 0) result = 0;
    }
    // 5. custom audio: SoundEvents (registry 0x1430570) + SoundEngine private vtable
    if (c->sound_pcm_count) {
        int rc = CALL(LCE195C_PH_AUDIO, &c->audio, 0, a195_register_events(&c->audio));
        if (rc == 1) {
            rc = CALL(LCE195C_PH_AUDIO, &c->audio, 1, a195_hook_engine(&c->audio));
            if (rc == 1)
                for (uint32_t i = 0; i < c->audio.count; ++i) lce195_sound_set_custom(c->audio.sounds[i].key, c->audio.sounds[i].id);
        }
        char d[200];
        snprintf(d, sizeof d, "%u custom sound(s) %s", c->sound_pcm_count, rc == 1 ? "registered (ids from 523) + SoundEngine vtable copy" : c->audio.last_error);
        mark(c, LCE195C_PH_AUDIO, rc, d);
        if (rc < 0) result = -1; else if (rc == 0 && result > 0) result = 0;
    } else none(c, LCE195C_PH_AUDIO);
    c->late_held = 0;
    logf(c, "LCE_SDK late_startup done world_entry=%u", c->world_entry);
    return result;
}

extern "C" void lce195c_ui_tick_strings(lce195c *c) {
    if (!c || c->state != 2 || !c->sid_count || c->phase[LCE195C_PH_STRINGS] == LCE195C_NOT_RUN || c->phase[LCE195C_PH_STRINGS] == LCE195C_UNCERTAIN) return;
    c->items_thread = tid(c);
    c->ui_held = 1;
    s195_report r;
    memset(&r, 0, sizeof r);
    int rc = s195_ensure(&g_senv, c->sids, c->sid_count, &r);
    c->ui_held = 0;
    if (r.inserted || r.failed || rc < 0)   // only a (re)built Localizer (language change) inserts: log it
        logf(c, "LCE_SDK strings rc=%d localizer=%#lx languages=%u inserted=%u present=%u failed=%u", rc, (unsigned long)r.localizer, r.languages, r.inserted, r.present, r.failed);
    if (rc < 0) c->phase[LCE195C_PH_STRINGS] = LCE195C_UNCERTAIN;   // stop touching the map
    else if (rc == 1) c->phase[LCE195C_PH_STRINGS] = LCE195C_DONE;
}
extern "C" void lce195c_net_tick(lce195c *c) {   // API 1.6: retry the client packet hook until it is installed
    if (c && c->net_channels && !c->net_client_installed && lce195_net_client_install() == 1) {
        c->net_client_installed = 1;
        logf(c, "LCE_SDK net: client payload hook installed");
    }
}
extern "C" int32_t lce195c_effect_id(const char *key) { return g_c && g_c->phase[LCE195C_PH_EFFECTS] == LCE195C_DONE ? lce195e_effect_id(key) : -1; }

extern "C" int lce195c_world_entry_allowed(const lce195c *c) {
    return !c || (c->world_entry && (!c->fuel_count || c->phase[LCE195C_PH_FUEL] == LCE195C_DONE));
}
extern "C" uint32_t lce195c_phase_state(const lce195c *c, uint32_t ph) { return c && ph < LCE195C_PH_COUNT ? c->phase[ph] : 0; }

extern "C" void lce195c_status_line(const lce195c *c, char *out, uint32_t cap) {
    if (!out || !cap) return;
    out[0] = 0;
    if (!c) return;
    size_t n = (size_t)snprintf(out, cap, "LCE_SDK phases");
    for (uint32_t i = 1; i < LCE195C_PH_COUNT && n < cap; ++i) n += (size_t)snprintf(out + n, cap - n, " %s=%u", kPhaseName[i], c->phase[i]);
    if (n < cap) n += (size_t)snprintf(out + n, cap - n, " world_entry=%u", c->world_entry);
    if (n < cap && c->has_blocks)
        snprintf(out + n, cap - n, " terrain_tiles=%u binds=%u rc=%d gen=%u", b195_terrain_tile_count(&g_blocks),
                 __atomic_load_n(&g_terrain_binds, __ATOMIC_RELAXED), __atomic_load_n(&g_terrain_rc, __ATOMIC_RELAXED),
                 __atomic_load_n(&g_terrain_gen, __ATOMIC_RELAXED));
}

// ---- events + scripts -------------------------------------------------------
extern "C" int lce195c_event_sink(void *self, lce_event ev, const void *payload) {
    lce195c *c = (lce195c *)self;
    if (!c || !c->loader) return 0;
    int cancel = lce_loader_dispatch(c->loader, ev, payload);
    for (uint32_t i = 0; i < c->script_count; ++i) {
        const lce_mod_report *r = lce_loader_mod(c->loader, c->scripts[i].mod);
        if (r && r->state == LCE_MOD_LIVE) cancel |= lce_script_event((lce_script *)c->scripts[i].vm, ev, payload);
    }
    return cancel;
}

namespace {
void script_log(void *user, lce_log_level, const char *msg) {
    lce195c *c = (lce195c *)user;
    logf(c, "LCE_SDK script: %s", msg ? msg : "");
}
}  // namespace

extern "C" lce_status lce195c_script_runner(void *self, lce_loader *l, uint32_t mi, const lce_mod_script *sc, char *err, uint32_t cap) {
    lce195c *c = (lce195c *)self;
    if (!c || !c->bind.scripts) { set_err(err, cap, "scripting is not linked in this build"); return LCE_E_UNSUPPORTED; }
    if (!sc || !sc->name || strcmp(sc->name, "main.lua")) return LCE_OK;   // only scripts/main.lua is an entry point
    if (c->script_count >= LCE195C_MAX_SCRIPTS) { set_err(err, cap, "more than 32 scripted mods"); return LCE_E_FULL; }
    lce_script_config cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.memory_cap = 4u << 20;     // 4 MiB per mod on the console
    cfg.host_user = c; cfg.on_log = script_log;
    lce_script *s = lce_script_new(lce_loader_api(l), lce_loader_mod_ctx(l, mi), &cfg);
    if (!s) { set_err(err, cap, "Lua VM allocation failed"); return LCE_E_FULL; }
    char e[128] = {0};
    lce_status st = lce_script_run_main(s, sc->name, (const char *)sc->data, sc->size, e, sizeof e);
    if (st != LCE_OK) { lce_script_free(s); set_err(err, cap, e[0] ? e : "script failed"); return st; }
    c->scripts[c->script_count].vm = s;
    c->scripts[c->script_count].mod = mi;
    ++c->script_count;
    return LCE_OK;
}
extern "C" uint32_t lce195c_script_count(const lce195c *c) { return c ? c->script_count : 0; }

// ---- terrain atlas (gap G1) -------------------------------------------------------
// shared_terrain_195/runtime.cpp (through sdk_boot.cpp) appends the SDK tiles after its two fixed tiles and calls
// bind/detach inside its reload lease. Tiles exist only for a prepared plan with block textures.
extern "C" uint32_t lce195c_terrain_tiles(const lce195c *c) {
    return c && c->has_blocks && (c->state == 1 || c->state == 2) ? b195_terrain_tile_count(&g_blocks) : 0;
}
extern "C" int lce195c_terrain_compose(const lce195c *c, uint32_t tile_size, unsigned mip, uint8_t *out, size_t bytes) {
    return lce195c_terrain_tiles(c) ? b195_terrain_compose(&g_blocks, tile_size, mip, out, bytes) : 0;
}
extern "C" int lce195c_terrain_bind(lce195c *c, const lce195c_terrain_lease *l, uintptr_t atlas, uint32_t generation, uint32_t width,
                                    uint32_t height, uint32_t stock_height, uint32_t tile_size, uint32_t first_index) {
    if (!lce195c_terrain_tiles(c) || !l || !l->held || !l->uploaded || !acquire_terrain_lease(l)) return 0;
    int rc = b195_terrain_bind(&g_blocks, atlas, generation, width, height, stock_height, tile_size, first_index);
    __atomic_add_fetch(&g_terrain_binds, 1, __ATOMIC_RELAXED);
    __atomic_store_n(&g_terrain_rc, rc, __ATOMIC_RELAXED);
    __atomic_store_n(&g_terrain_gen, generation, __ATOMIC_RELAXED);
    __atomic_store_n(&g_tlease, (const lce195c_terrain_lease *)nullptr, __ATOMIC_RELEASE);
    return rc;
}
extern "C" int lce195c_terrain_detach(lce195c *c, const lce195c_terrain_lease *l) {
    if (!c || !c->has_blocks) return 1;
    if (!l || !l->held || !acquire_terrain_lease(l)) return 0;
    int rc = l->held() == 1 ? b195_terrain_detach(&g_blocks) : 0;
    __atomic_store_n(&g_tlease, (const lce195c_terrain_lease *)nullptr, __ATOMIC_RELEASE);
    return rc;
}

#ifdef LCE195C_HOST_TEST
extern "C" const b195_module *g_comp_blocks_for_tests(void) { return &g_blocks; }
extern "C" const p195_module *g_comp_projectiles_for_tests(void) { return &g_proj; }
extern "C" void lce195c_test_override(uint32_t ph, lce195c_phase_fn fn) { if (ph < LCE195C_PH_COUNT) g_ovr[ph] = fn; }
#endif
