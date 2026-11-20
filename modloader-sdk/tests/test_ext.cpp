// API 1.1 content through the core: package replay, deep copies, reference
// validation, capability drops, collisions, scripts, services.
#include <stdlib.h>
#include <string.h>
#include <vector>
#include "../loader/loader.h"
#include "../loader/package.h"
#include "../loader/sha256.h"
#include "../loader/retail195/id_space_195.h"
#include "check.h"
#include "mock_backend.h"

static std::vector<uint8_t> load(const char *path) {
    std::vector<uint8_t> v; FILE *f = fopen(path, "rb"); if (!f) { fprintf(stderr, "missing %s\n", path); return v; }
    int c; while ((c = fgetc(f)) != EOF) v.push_back((uint8_t)c); fclose(f); return v;
}
static MockGame game;
static lce_backend be;
static uint8_t ledger[1 << 16]; static size_t ledger_len;
static lce_loader *make(uint32_t caps) {
    mock_game_init(&game, caps);
    be = mock_backend(&game);
    lce_loader_config c; memset(&c, 0, sizeof c);
    lce195_default_id_space(&c.space);
    c.space.item_in_use = mock_item_in_use; c.space.block_in_use = mock_block_in_use; c.space.user = &game;
    c.backend = &be; c.ledger_path = "/mem";
    ledger_len = 0;
    c.host.read_file = [](void *, const char *, uint8_t *b, size_t cap) -> long { if (!ledger_len) return -1; if (ledger_len > cap) return -2; memcpy(b, ledger, ledger_len); return (long)ledger_len; };
    c.host.write_file_atomic = [](void *, const char *, const uint8_t *d, size_t n) -> int { memcpy(ledger, d, n); ledger_len = n; return 1; };
    if (getenv("LCE_TEST_VERBOSE")) c.host.log = [](void *, const char *l) { fprintf(stderr, "  | %s\n", l); };
    return lce_loader_create(&c);
}
static const lce_mod_report *rep(lce_loader *l, const char *id) {
    for (uint32_t i = 0; i < lce_loader_mod_count(l); ++i) if (!strcmp(lce_loader_mod(l, i)->modid, id)) return lce_loader_mod(l, i);
    return nullptr;
}
static const uint32_t ALL = 0xffffffffu & ~LCE_CAP_SERVICES;

static lce_status native_legacy(const lce_api *api, lce_mod_ctx *ctx) {
    char entity_key[] = "legacy:ghost";
    char ore_key[] = "legacy:ore";
    char base[] = "minecraft:zombie";
    lce_entity_desc e{}; e.struct_size = sizeof e; e.key = entity_key; e.base_mob = base;
    e.max_health_milli = 30000; e.spawn_weight = 1;
    if (api->register_entity(ctx, &e) != LCE_E_UNSUPPORTED) return LCE_E_INVALID;
    e.spawn_weight = 0;
    if (api->register_entity(ctx, &e) != LCE_OK) return LCE_E_INVALID;
    if (api->register_entity(ctx, &e) != LCE_E_COLLISION) return LCE_E_INVALID;
    lce_ore_desc o{}; o.struct_size = sizeof o; o.key = ore_key;
    o.block = "minecraft:stone"; o.replace = "minecraft:dirt";
    o.vein_size = 5; o.veins_per_chunk = 3; o.min_y = 4; o.max_y = 48;
    if (api->register_ore(ctx, &o) != LCE_OK) return LCE_E_INVALID;
    if (api->register_ore(ctx, &o) != LCE_E_COLLISION) return LCE_E_INVALID;
    o.struct_size = 0;
    if (api->register_ore(ctx, &o) != LCE_E_INVALID) return LCE_E_INVALID;
    memset(entity_key, 'x', sizeof entity_key - 1);
    memset(ore_key, 'x', sizeof ore_key - 1);
    memset(base, 'x', sizeof base - 1);
    return LCE_OK;
}

static int cmd(void *, void *, int, const char *const *, char *, uint32_t) { return 0; }
static int key(void *, void *, int) { return 0; }
static lce_status native_extras(const lce_api *api, lce_mod_ctx *ctx) {
    lce_command_desc c{}; c.struct_size = sizeof c; c.name = "gems"; c.usage = "/gems"; c.fn = cmd;
    if (api->register_command(ctx, &c)) return LCE_E_INVALID;
    lce_command_desc bad = c; bad.name = "Bad Name";
    if (api->register_command(ctx, &bad) != LCE_E_INVALID) return LCE_E_INVALID;
    lce_keybind_desc k{}; k.struct_size = sizeof k; k.key = "native_extras:dash"; k.name = "Dash"; k.default_button = LCE_PAD_R3; k.fn = key;
    if (api->register_keybind(ctx, &k)) return LCE_E_INVALID;
    // deep copy: descriptor arrays live on the stack and are clobbered afterwards
    lce_effect_ref fx[1]; char name[32]; strcpy(name, "minecraft:speed");
    fx[0] = lce_effect_ref{name, 600, 1, 0, {0, 0}};
    lce_potion_desc p{}; p.struct_size = sizeof p; p.key = "native_extras:zoom"; p.name = "Zoom"; p.effects = fx; p.effect_count = 1;
    if (api->register_potion(ctx, &p)) return LCE_E_INVALID;
    memset(name, 'x', sizeof name - 1); fx[0].duration_ticks = 1;
    // structure with an out-of-range palette index is refused at registration
    const char *pal[] = {"minecraft:stone"}; uint16_t blocks[2] = {0, 3};
    lce_structure_desc sd{}; sd.struct_size = sizeof sd; sd.key = "native_extras:bad"; sd.size_x = 2; sd.size_y = 1; sd.size_z = 1;
    sd.palette = pal; sd.palette_count = 1; sd.blocks = blocks;
    if (api->register_structure(ctx, &sd) != LCE_E_INVALID) return LCE_E_INVALID;
    // fuel for coal (allcontent's package claims stick later, in REGISTER)
    lce_fuel_desc f{}; f.struct_size = sizeof f; f.item = "minecraft:coal"; f.burn_ticks = 2000;
    if (api->register_fuel(ctx, &f)) return LCE_E_INVALID;
    return LCE_OK;
}
static lce_status native_badref(const lce_api *api, lce_mod_ctx *ctx) {
    lce_effect_ref fx{"minecraft:flying", 100, 0, 0, {0, 0}};  // not a 1.95 effect
    lce_potion_desc p{}; p.struct_size = sizeof p; p.key = "native_badref:fly"; p.name = "Fly"; p.effects = &fx; p.effect_count = 1;
    return api->register_potion(ctx, &p);
}
static lce_status native_fuelclash(const lce_api *api, lce_mod_ctx *ctx) {
    // natives run in INIT in mod-id order (before packages replay): native_extras claimed coal first
    lce_fuel_desc f{}; f.struct_size = sizeof f; f.item = "minecraft:coal"; f.burn_ticks = 10;
    return api->register_fuel(ctx, &f) == LCE_E_COLLISION ? LCE_OK : LCE_E_INVALID;
}

// Exercise fuel registration through the public API, including a failed
// registration followed by successful registrations in the same mod.
static lce_status native_fuel_bounds(const lce_api *api, lce_mod_ctx *ctx) {
    char item[] = "minecraft:coal";
    lce_fuel_desc f{}; f.struct_size = sizeof f; f.item = item;
    f.burn_ticks = 0;
    if (api->register_fuel(ctx, &f) != LCE_E_INVALID) return LCE_E_INVALID;
    f.burn_ticks = 72001;
    if (api->register_fuel(ctx, &f) != LCE_E_INVALID) return LCE_E_INVALID;
    f.burn_ticks = 1;
    if (api->register_fuel(ctx, &f) != LCE_OK) return LCE_E_INVALID;
    f.burn_ticks = 72000;
    if (api->register_fuel(ctx, &f) != LCE_E_COLLISION) return LCE_E_INVALID;
    f.item = "minecraft:stick";
    if (api->register_fuel(ctx, &f) != LCE_OK) return LCE_E_INVALID;
    memset(item, 'x', sizeof item - 1); // the registered item must be copied
    return LCE_OK;
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "build/pkg";
    char path[256]; snprintf(path, sizeof path, "%s/allcontent/mod.lcm", dir);
    std::vector<uint8_t> pkg = load(path);
    CHECK(!pkg.empty());

    // Legacy descriptors use the same validation and staging as modern content.
    {
        lce_loader *legacy = make(ALL);
        CHECK_EQ(lce_loader_add_native(legacy, "legacy", "1", native_legacy), LCE_OK);
        CHECK_EQ(lce_loader_commit(legacy), LCE_OK);
        CHECK_EQ(rep(legacy, "legacy")->state, LCE_MOD_LIVE);
        const lce_plan *plan = lce_loader_plan(legacy);
        unsigned found = 0;
        for (uint32_t i = 0; i < plan->ext_count; ++i) {
            if (plan->ext[i].type == LCE_EXT_ENTITY) {
                const lce_entity_desc2 *e = (const lce_entity_desc2 *)plan->ext[i].desc;
                CHECK(!strcmp(e->key, "legacy:ghost") && !strcmp(e->base_mob, "minecraft:zombie"));
                CHECK_EQ(e->max_health_milli, 30000u);
                ++found;
            } else if (plan->ext[i].type == LCE_EXT_FEATURE) {
                const lce_feature_desc *o = (const lce_feature_desc *)plan->ext[i].desc;
                CHECK(!strcmp(o->key, "legacy:ore") && !strcmp(o->block2, "minecraft:dirt"));
                CHECK(o->kind == LCE_FEATURE_ORE && o->size == 5 && o->per_chunk == 3 && o->min_y == 4 && o->max_y == 48);
                ++found;
            }
        }
        CHECK_EQ(found, 2u);
        lce_loader_destroy(legacy);
    }

    // Fuel declarations retain their bounds and item references in the plan.
    {
        lce_loader *fuel = make(ALL);
        CHECK_EQ(lce_loader_add_native(fuel, "fuel_bounds", "1", native_fuel_bounds), LCE_OK);
        CHECK_EQ(lce_loader_commit(fuel), LCE_OK);
        CHECK_EQ(rep(fuel, "fuel_bounds")->state, LCE_MOD_LIVE);
        const lce_plan *plan = lce_loader_plan(fuel);
        unsigned found = 0;
        for (uint32_t i = 0; i < plan->ext_count; ++i) {
            if (plan->ext[i].type != LCE_EXT_FUEL) continue;
            const lce_fuel_desc *f = (const lce_fuel_desc *)plan->ext[i].desc;
            if (!strcmp(f->item, "minecraft:coal") && f->burn_ticks == 1) ++found;
            if (!strcmp(f->item, "minecraft:stick") && f->burn_ticks == 72000) ++found;
        }
        CHECK_EQ(found, 2u);
        lce_loader_destroy(fuel);
    }

    // ---- A: full-capability backend --------------------------------------
    lce_loader *l = make(ALL);
    CHECK_EQ(lce_loader_add_package(l, pkg.data(), pkg.size(), "allcontent"), LCE_OK);
    CHECK_EQ(lce_loader_add_native(l, "native_extras", "1", native_extras), LCE_OK);
    CHECK_EQ(lce_loader_add_native(l, "native_badref", "1", native_badref), LCE_OK);
    CHECK_EQ(lce_loader_add_native(l, "native_fuelclash", "1", native_fuelclash), LCE_OK);
    CHECK_EQ(lce_loader_commit(l), LCE_OK);
    CHECK_EQ(rep(l, "allcontent")->state, LCE_MOD_LIVE);
    CHECK_EQ(rep(l, "native_extras")->state, LCE_MOD_LIVE);
    CHECK_EQ(rep(l, "native_fuelclash")->state, LCE_MOD_LIVE);
    CHECK_EQ(rep(l, "native_badref")->state, LCE_MOD_DROPPED);
    CHECK(strstr(rep(l, "native_badref")->reason, "minecraft:flying") != nullptr);
    const lce_plan *p = lce_loader_plan(l);
    CHECK(p != nullptr);
    unsigned per[LCE_EXT_TYPE_COUNT] = {0};
    const lce_potion_desc *zoom = nullptr; const lce_entity_desc2 *golem = nullptr; const lce_structure_desc *shrine = nullptr;
    const lce_sound_desc *chime = nullptr; const lce_feature_desc *ore = nullptr;
    for (uint32_t i = 0; i < p->ext_count; ++i) {
        ++per[p->ext[i].type];
        if (!strcmp(p->ext[i].key, "native_extras:zoom")) zoom = (const lce_potion_desc *)p->ext[i].desc;
        if (!strcmp(p->ext[i].key, "allcontent:gem_golem")) golem = (const lce_entity_desc2 *)p->ext[i].desc;
        if (!strcmp(p->ext[i].key, "allcontent:shrine")) shrine = (const lce_structure_desc *)p->ext[i].desc;
        if (!strcmp(p->ext[i].key, "allcontent:chime")) chime = (const lce_sound_desc *)p->ext[i].desc;
        if (!strcmp(p->ext[i].key, "allcontent:gem_ore_gen")) ore = (const lce_feature_desc *)p->ext[i].desc;
    }
    CHECK_EQ(per[LCE_EXT_EFFECT], 1); CHECK_EQ(per[LCE_EXT_POTION], 2); CHECK_EQ(per[LCE_EXT_BREW], 1); CHECK_EQ(per[LCE_EXT_ENCHANT], 1);
    CHECK_EQ(per[LCE_EXT_FUEL], 2); CHECK_EQ(per[LCE_EXT_ENTITY], 1); CHECK_EQ(per[LCE_EXT_FEATURE], 2); CHECK_EQ(per[LCE_EXT_BIOME], 1);
    CHECK_EQ(per[LCE_EXT_STRUCTURE], 1); CHECK_EQ(per[LCE_EXT_SOUND], 2); CHECK_EQ(per[LCE_EXT_PARTICLE], 1);
    CHECK_EQ(per[LCE_EXT_COMMAND], 1); CHECK_EQ(per[LCE_EXT_KEYBIND], 1);
    CHECK(zoom && zoom->effect_count == 1 && !strcmp(zoom->effects[0].effect, "minecraft:speed") && zoom->effects[0].duration_ticks == 600);
    CHECK(golem && golem->goal_count == 3 && golem->goals[2].kind == LCE_AI_FOLLOW_TEMPT_ITEM && !strcmp(golem->goals[2].param, "allcontent:gem"));
    CHECK(golem && golem->drop_count == 1 && golem->drops[0].max == 3 && golem->max_health_milli == 60000 && golem->egg_primary_rgb == 0x2244ff);
    CHECK(golem && golem->spawn_biome_count == 1 && !strcmp(golem->spawn_biomes[0], "minecraft:plains"));
    CHECK(shrine && shrine->size_x == 3 && shrine->palette_count == 2 && shrine->blocks[4] == 2 - 1 && shrine->blocks[0] == 0);
    CHECK(chime && chime->pcm_bytes == 2205 * 2 && chime->sample_rate == 22050);
    CHECK(ore && ore->kind == LCE_FEATURE_ORE && ore->size == 6 && ore->per_chunk == 4);
    // resolver: items/blocks numeric, mod content indexes, vanilla marked -2
    CHECK(p->resolve(p->resolve_ctx, LCE_REF_ITEM, "allcontent:gem") >= 2283);
    CHECK(p->resolve(p->resolve_ctx, LCE_REF_BLOCK, "allcontent:gem_ore") > 0);
    CHECK(p->resolve(p->resolve_ctx, LCE_REF_STRUCTURE, "allcontent:shrine") >= 1000000);
    CHECK_EQ(p->resolve(p->resolve_ctx, LCE_REF_BIOME, "minecraft:plains"), -2);
    CHECK_EQ(p->resolve(p->resolve_ctx, LCE_REF_EFFECT, "allcontent:nope"), -1);
    // item 1.1 fields survived
    bool pick = false, helmet = false, stew = false, chest = false;
    for (uint32_t i = 0; i < p->item_count; ++i) {
        const lce_item_desc *d = p->items[i].desc;
        if (!strcmp(d->key, "allcontent:gem_pick")) pick = d->mining_speed_milli == 9000 && d->repair_item && !strcmp(d->repair_item, "allcontent:gem") && d->enchantability == 12;
        if (!strcmp(d->key, "allcontent:gem_helmet")) helmet = d->armor_points == 3 && d->armor_texture && !strcmp(d->armor_texture, "allcontent:armor_layer");
        if (!strcmp(d->key, "allcontent:gem_stew")) stew = d->food_effect_count == 2 && !strcmp(d->food_effects[1].effect, "allcontent:sparkle") && d->food_effects[1].chance_percent == 50;
    }
    for (uint32_t i = 0; i < p->block_count; ++i)
        if (!strcmp(p->blocks[i].desc->key, "allcontent:gem_chest")) chest = p->blocks[i].desc->block_entity_slots == 27 && p->blocks[i].desc->placement == LCE_PLACE_FACING_PLAYER;
    CHECK(pick && helmet && stew && chest);
    // scripts
    lce_mod_script sc[4];
    uint32_t mi = 0;
    for (uint32_t i = 0; i < lce_loader_mod_count(l); ++i) if (!strcmp(lce_loader_mod(l, i)->modid, "allcontent")) mi = i;
    CHECK_EQ(lce_loader_scripts(l, mi, sc, 4), 1);
    CHECK(!strcmp(sc[0].name, "main.lua") && sc[0].size == 67 && !memcmp(sc[0].data, "-- allcontent", 13));
    // services default to UNSUPPORTED until a backend provides them
    const lce_api *api = lce_loader_api(l);
    CHECK_EQ(api->give_item(lce_loader_mod_ctx(l, mi), nullptr, "allcontent:gem", 1, 0), LCE_E_UNSUPPORTED);
    lce_api svc{}; svc.give_item = [](lce_mod_ctx *, void *, const char *, uint32_t, int32_t) -> lce_status { return LCE_OK; }; svc.capabilities = LCE_CAP_SERVICES;
    lce_loader_set_services(l, &svc);
    CHECK_EQ(api->give_item(lce_loader_mod_ctx(l, mi), nullptr, "allcontent:gem", 1, 0), LCE_OK);
    CHECK(api->capabilities & LCE_CAP_SERVICES);
    lce_loader_destroy(l);

    // ---- B: backend without effects/worldgen: the whole package is dropped with the reason
    l = make(ALL & ~LCE_CAP_EFFECTS);
    lce_loader_add_package(l, pkg.data(), pkg.size(), "allcontent");
    CHECK_EQ(lce_loader_commit(l), LCE_OK);
    CHECK_EQ(rep(l, "allcontent")->state, LCE_MOD_DROPPED);
    CHECK(strstr(rep(l, "allcontent")->reason, "effect") != nullptr);
    lce_loader_destroy(l);

    // ---- C: corrupted EXT section (rehashed) is refused by the parser
    {
        std::vector<uint8_t> c = pkg;
        lce_package_view v; CHECK_EQ(lce_package_parse(c.data(), c.size(), &v, nullptr, 0), LCE_OK);
        CHECK_EQ(v.ext_count, 13);
        size_t off = (const uint8_t *)v.ext - c.data();
        uint32_t bad = 99; memcpy(&c[off], &bad, 4);  // unknown type
        lce_sha256(c.data() + 64, c.size() - 64, c.data() + 24);
        CHECK_EQ(lce_package_parse(c.data(), c.size(), &v, nullptr, 0), LCE_E_INVALID);
        c = pkg; uint32_t slots = 63; memcpy(&c[off + 4], &slots, 4);  // slot count lie
        lce_sha256(c.data() + 64, c.size() - 64, c.data() + 24);
        CHECK_EQ(lce_package_parse(c.data(), c.size(), &v, nullptr, 0), LCE_E_INVALID);
    }
    TEST_MAIN_END("test_ext");
}
