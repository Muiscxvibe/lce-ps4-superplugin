// End-to-end loader tests against the mock game and an in-memory filesystem
// laid out exactly like the console (/data/lce-mods/<id>/mod.lcm, packs/,
// reservations/).
#include <map>
#include <string>
#include <vector>
#include <string.h>
#include "../loader/loader.h"
#include "../loader/config_store.h"
#include "../loader/retail195/id_space_195.h"
#include "check.h"
#include "mock_backend.h"

static std::string g_pkgdir = "build/pkg";
static const char *kRoot = "/data/lce-mods";
static const char *kLedger = "/data/lce-mods/reservations/sdk-ids-v1.txt";

struct Fs {
    std::map<std::string, std::vector<uint8_t>> files;
    std::vector<std::string> log;
    int fail_writes = 0;
};

static std::vector<uint8_t> load(const std::string &path) {
    std::vector<uint8_t> v;
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) { fprintf(stderr, "missing fixture %s\n", path.c_str()); return v; }
    int c;
    while ((c = fgetc(f)) != EOF) v.push_back((uint8_t)c);
    fclose(f);
    return v;
}

static long fs_read(void *u, const char *path, uint8_t *buf, size_t cap) {
    Fs *fs = (Fs *)u;
    auto it = fs->files.find(path);
    if (it == fs->files.end()) return -1;
    if (it->second.size() > cap) return -2;
    memcpy(buf, it->second.data(), it->second.size());
    return (long)it->second.size();
}
static int fs_write(void *u, const char *path, const uint8_t *d, size_t n) {
    Fs *fs = (Fs *)u;
    if (fs->fail_writes) return 0;
    fs->files[path] = std::vector<uint8_t>(d, d + n);
    return 1;
}
static int fs_list(void *u, const char *dir, void (*emit)(void *, const char *), void *ctx) {
    Fs *fs = (Fs *)u;
    std::string prefix = std::string(dir) + "/";
    std::vector<std::string> seen;
    for (auto &kv : fs->files) {
        if (kv.first.compare(0, prefix.size(), prefix)) continue;
        std::string rest = kv.first.substr(prefix.size());
        size_t slash = rest.find('/');
        if (slash == std::string::npos) continue;
        std::string name = rest.substr(0, slash);
        bool dup = false;
        for (auto &s : seen) dup |= s == name;
        if (!dup) { seen.push_back(name); emit(ctx, name.c_str()); }
    }
    return 1;
}
static int fs_exists(void *u, const char *path) { return ((Fs *)u)->files.count(path) ? 1 : 0; }
static void fs_log(void *u, const char *line) { ((Fs *)u)->log.push_back(line); if (getenv("LCE_TEST_VERBOSE")) printf("  | %s\n", line); }

static void install(Fs &fs, const char *dir, const char *pkg) {
    fs.files[std::string(kRoot) + "/" + dir + "/mod.lcm"] = load(g_pkgdir + "/" + pkg + "/mod.lcm");
}

static MockGame game;
static lce_backend backend;

static lce_loader_config config(Fs &fs, uint32_t flags = LCE_LOADER_PLACEHOLDERS) {
    lce_loader_config c;
    memset(&c, 0, sizeof c);
    c.root = kRoot; c.ledger_path = kLedger; c.flags = flags;
    lce195_default_id_space(&c.space);
    c.space.item_in_use = mock_item_in_use; c.space.block_in_use = mock_block_in_use; c.space.user = &game;
    backend = mock_backend(&game);
    c.backend = &backend;
    c.host.user = &fs; c.host.read_file = fs_read; c.host.write_file_atomic = fs_write;
    c.host.list_dirs = fs_list; c.host.exists = fs_exists; c.host.log = fs_log;
    return c;
}

static const lce_mod_report *report(lce_loader *l, const char *id) {
    for (uint32_t i = 0; i < lce_loader_mod_count(l); ++i)
        if (!strcmp(lce_loader_mod(l, i)->modid, id)) return lce_loader_mod(l, i);
    return nullptr;
}

static void standard_fs(Fs &fs) {
    install(fs, "rubymod", "rubymod");
    install(fs, "rubytools", "rubytools");
    install(fs, "gem", "gem");
    install(fs, "orphan", "orphan");
    install(fs, "badver", "badver");
    fs.files["/data/lce-mods/packs/gre-flat-skin-2002/SkinPack.pck"] = {1, 2, 3};
    fs.files["/data/lce-mods/reservations/steel195-v1.bin"] = {0};
    fs.files["/data/lce-mods/worlds/staged/map-index.bin"] = {0};       // map importer
    fs.files["/data/lce-mods/downloads/maps/x.zip"] = {0};              // Mods-menu downloader
    fs.files["/data/lce-mods/selection-content-v9"] = {0};
}

static const uint32_t kAllCaps = LCE_CAP_ITEMS | LCE_CAP_BLOCKS | LCE_CAP_RECIPES | LCE_CAP_SMELTING | LCE_CAP_CRAFT_TABS |
                                 LCE_CAP_CREATIVE | LCE_CAP_TEXTURES | LCE_CAP_STRINGS | LCE_CAP_EV_TICK | LCE_CAP_EV_ITEM_USE |
                                 LCE_CAP_TOOLS | LCE_CAP_FOOD | LCE_CAP_ARMOUR | LCE_CAP_FUEL | LCE_CAP_BLOCK_ENTITY | LCE_CAP_WORLDGEN;

// ---- an in-process native mod --------------------------------------------
static int g_ticks, g_uses;
static const lce_api *g_api;
static lce_mod_ctx *g_ctx;
static int on_event(void *user, lce_event ev, const void *payload) {
    (void)user;
    if (ev == LCE_EV_SERVER_TICK) ++g_ticks;
    if (ev == LCE_EV_ITEM_USE) { ++g_uses; return ((const lce_ev_item_use *)payload)->item_id == 2283; }
    return 0;
}
static lce_status native_main(const lce_api *api, lce_mod_ctx *ctx) {
    g_api = api; g_ctx = ctx;
    if (api->version >> 16 != LCE_MOD_API_MAJOR) return LCE_E_VERSION;
    static uint8_t px[16 * 16 * 4];
    memset(px, 0x7f, sizeof px);
    lce_texture_desc t{}; t.struct_size = sizeof t; t.key = "aaa_native:i_wand"; t.width = t.height = 16; t.rgba = px;
    if (api->register_texture(ctx, &t)) return LCE_E_INVALID;
    lce_item_desc it{}; it.struct_size = sizeof it; it.key = "aaa_native:wand"; it.name = "Wand"; it.texture = "aaa_native:i_wand";
    if (api->register_item(ctx, &it)) return LCE_E_INVALID;
    // A descriptor from an older minor version (shorter struct) must still work.
    lce_item_desc old{}; old.key = "aaa_native:old"; old.name = "Old"; old.texture = "aaa_native:i_wand";
    old.struct_size = (uint32_t)offsetof(lce_item_desc, flags);
    if (api->register_item(ctx, &old)) return LCE_E_INVALID;
    // cannot register into a foreign namespace or override vanilla strings
    lce_item_desc bad = it; bad.key = "rubymod:stolen";
    if (api->register_item(ctx, &bad) != LCE_E_INVALID) return LCE_E_INVALID;
    lce_string_desc s{}; s.struct_size = sizeof s; s.locale = "en_US"; s.key = "minecraft:stone"; s.value = "Rock";
    if (api->register_string(ctx, &s) != LCE_E_INVALID) return LCE_E_INVALID;
    if (api->register_ore(ctx, nullptr) != LCE_E_INVALID) return LCE_E_INVALID;
    if (api->resolve_item(ctx, "minecraft:stick") != 280) return LCE_E_INVALID;
    if (api->resolve_item(ctx, "aaa_native:wand") != -1) return LCE_E_INVALID;  // not before commit
    if (api->subscribe(ctx, LCE_EV_SERVER_TICK, on_event, nullptr)) return LCE_E_INVALID;
    if (api->subscribe(ctx, LCE_EV_ITEM_USE, on_event, nullptr)) return LCE_E_INVALID;
    if (api->subscribe(ctx, LCE_EV_BLOCK_BREAK, on_event, nullptr) != LCE_E_UNSUPPORTED) return LCE_E_INVALID;  // backend lacks it
    return LCE_OK;
}


#ifdef LCE_HAVE_CONFIG_1_8
// ---- API 1.8 config: a native mod reads its config while registering (Forge "enable X" pattern) ----
static int64_t g_cfg_rate; static double g_cfg_speed; static const char *g_cfg_name; static lce_status g_cfg_get; static lce_nbt_value g_cfg_v;
static lce_status cfg_native_main(const lce_api *api, lce_mod_ctx *ctx) {
    if (api->struct_size < offsetof(lce_api, config_string) + sizeof(void *) || !(api->capabilities2 & LCE_CAP2_CONFIG)) return LCE_E_UNSUPPORTED;
    g_cfg_rate = api->config_int(ctx, "general.ore_rate", 8, 1, 64);
    g_cfg_speed = api->config_double(ctx, "general.speed", 1.0, 0.1, 10.0);
    g_cfg_name = api->config_string(ctx, "general.name", "default");
    g_cfg_get = api->config_get(ctx, "general.enable_wand", &g_cfg_v);
    if (api->config_int(ctx, "general.enable_wand", 1, 0, 1)) {
        static uint8_t px[16 * 16 * 4];
        lce_texture_desc t{}; t.struct_size = sizeof t; t.key = "cfgmod:i_wand"; t.width = t.height = 16; t.rgba = px;
        if (api->register_texture(ctx, &t)) return LCE_E_INVALID;
        lce_item_desc d{}; d.struct_size = sizeof d; d.key = "cfgmod:wand"; d.name = "Wand"; d.texture = "cfgmod:i_wand";
        return api->register_item(ctx, &d);
    }
    return LCE_OK;
}
#endif
static int32_t id_of(lce_loader *l, const char *key) { return lce_loader_item_id(l, key); }

int main(int argc, char **argv) {
    if (argc > 1) g_pkgdir = argv[1];

    // ===== A. first boot ===================================================
    Fs fs;
    standard_fs(fs);
    mock_game_init(&game, kAllCaps);
    uint32_t vanilla_before = game.vanilla_items;
    lce_loader_config cfg = config(fs);
    lce_loader *l = lce_loader_create(&cfg);
    CHECK(l != nullptr);
    CHECK_EQ(lce_loader_add_native(l, "aaa_native", "1.0", native_main), LCE_OK);
    CHECK_EQ(lce_loader_discover(l), LCE_OK);
    CHECK_EQ(lce_loader_mod_count(l), 6);  // packs/reservations skipped
    CHECK_EQ(lce_loader_commit(l), LCE_OK);
    CHECK_EQ(lce_loader_phase(l), LCE_PHASE_RUNNING);
    CHECK_EQ(report(l, "rubymod")->state, LCE_MOD_LIVE);
    CHECK_EQ(report(l, "rubytools")->state, LCE_MOD_LIVE);
    CHECK_EQ(report(l, "gem")->state, LCE_MOD_LIVE);
    CHECK_EQ(report(l, "aaa_native")->state, LCE_MOD_LIVE);
    CHECK_EQ(report(l, "orphan")->state, LCE_MOD_DROPPED);
    CHECK(strstr(report(l, "orphan")->reason, "ghostmod") != nullptr);
    CHECK_EQ(report(l, "badver")->state, LCE_MOD_DROPPED);
    CHECK(strstr(report(l, "badver")->reason, "9.0") != nullptr);
    // IDs: deterministic first allocation by sorted mod id, blocks first
    int32_t wand = id_of(l, "aaa_native:wand"), ruby = id_of(l, "rubymod:ruby"), sword = id_of(l, "rubytools:ruby_sword");
    int32_t ruby_block_item = id_of(l, "rubymod:ruby_block"), ruby_block = lce_loader_block_id(l, "rubymod:ruby_block");
    int32_t sapphire_block = lce_loader_block_id(l, "gem:sapphire_block");
    CHECK(wand >= 2283 && wand <= 2397);
    CHECK(ruby >= 2283 && ruby <= 2397 && ruby != wand);
    CHECK(sword >= 2283 && sword <= 2397);
    CHECK_EQ(sapphire_block, 254);  // "gem" sorts before "rubymod"
    CHECK_EQ(ruby_block, 262);
    CHECK_EQ(ruby_block_item, 1030);
    CHECK_EQ(lce_loader_block_id(l, "rubymod:ruby_ore"), 280);
    CHECK_EQ(id_of(l, "rubymod:ruby_ore"), 1048);
    CHECK_EQ(id_of(l, "orphan:x"), -1);
    CHECK_EQ(id_of(l, "minecraft:stick"), 280);
    // vanilla untouched, mod content present in the simulated game
    uint32_t vanilla_after = 0;
    for (uint32_t id = 1; id < 2398; ++id) if (lce_vanilla_item_exists(id) && game.item_slots[id]) ++vanilla_after;
    CHECK_EQ(vanilla_after, vanilla_before);
    CHECK(game.item_slots[ruby] != nullptr);
    CHECK(!strcmp(game.names[ruby], "Ruby"));
    CHECK(!strcmp(game.names[ruby_block_item], "Block of Ruby"));
    CHECK(game.block_slots[ruby_block] != nullptr);
    // recipes resolved to numeric IDs
    const lce_plan *plan = lce_loader_plan(l);
    CHECK(plan != nullptr);
    bool saw_block = false, saw_pick = false, saw_sword = false, saw_wool = false;
    for (uint32_t i = 0; i < plan->recipe_count; ++i) {
        const lce_plan_recipe &r = plan->recipes[i];
        if (!strcmp(r.desc->key, "rubymod:ruby_block")) {
            saw_block = true;
            for (int c = 0; c < 9; ++c) CHECK_EQ(r.cells[c].id, ruby);
            CHECK_EQ(r.result_id, ruby_block_item);
        }
        if (!strcmp(r.desc->key, "rubymod:ruby_pickaxe")) {
            saw_pick = true;
            CHECK_EQ(r.cells[0].id, ruby); CHECK_EQ(r.cells[4].id, 280); CHECK_EQ(r.cells[7].id, 280); CHECK_EQ(r.cells[3].id, -1);
            CHECK_EQ(r.craft_group, 1);  // MATERIALS from hints craft_tab
        }
        if (!strcmp(r.desc->key, "rubytools:ruby_sword")) { saw_sword = true; CHECK_EQ(r.cells[0].id, ruby); CHECK_EQ(r.craft_group, 0); }
        if (!strcmp(r.desc->key, "rubymod:red_wool")) { saw_wool = true; CHECK_EQ(r.result_id, 35); CHECK_EQ(r.desc->result_aux, 14); CHECK_EQ(r.desc->shapeless, 1); }
    }
    CHECK(saw_block && saw_pick && saw_sword && saw_wool);
    CHECK_EQ(plan->smelt_count, 1);
    CHECK_EQ(plan->smelts[0].result_id, ruby);
    CHECK_EQ(plan->smelts[0].input.id, 1048);
    for (uint32_t i = 0; i < plan->texture_count; ++i) CHECK(plan->textures[i].slot >= (plan->textures[i].atlas ? 624 : 288));
    // ledger persisted before publish and parseable
    CHECK(fs.files.count(kLedger) == 1);
    // events: delivered only to live subscribers; cancel propagates
    g_ticks = g_uses = 0;
    lce_ev_tick tick{sizeof(lce_ev_tick), 1};
    lce_loader_dispatch(l, LCE_EV_SERVER_TICK, &tick);
    lce_loader_dispatch(l, LCE_EV_SERVER_TICK, &tick);
    CHECK_EQ(g_ticks, 2);
    lce_ev_item_use use{}; use.struct_size = sizeof use; use.item_id = (uint32_t)wand;
    CHECK_EQ(lce_loader_dispatch(l, LCE_EV_ITEM_USE, &use), wand == 2283);
    // registration after the native returned is refused; resolve works now
    lce_item_desc late{}; late.struct_size = sizeof late; late.key = "aaa_native:late"; late.name = "L"; late.texture = "aaa_native:i_wand";
    CHECK_EQ(g_api->register_item(g_ctx, &late), LCE_E_PHASE);
    CHECK_EQ(g_api->resolve_item(g_ctx, "aaa_native:wand"), wand);
    CHECK_EQ(g_api->resolve_item(g_ctx, "rubymod:ruby_block"), ruby_block_item);
    CHECK_EQ(g_api->resolve_block(g_ctx, "minecraft:stone"), 1);
    lce_loader_destroy(l);

    // ===== B. second boot: IDs stable although a new mod sorts first =========
    std::vector<uint8_t> ledger_a = fs.files[kLedger];
    mock_game_init(&game, kAllCaps);
    cfg = config(fs);
    l = lce_loader_create(&cfg);
    lce_loader_add_native(l, "aaa_native", "1.0", native_main);
    install(fs, "aa_first", "rubytools");  // folder/id mismatch -> rejected, must not disturb anything
    lce_loader_discover(l);
    CHECK_EQ(report(l, "rubytools") != nullptr, 1);
    CHECK_EQ(lce_loader_commit(l), LCE_OK);
    CHECK_EQ(id_of(l, "rubymod:ruby"), ruby);
    CHECK_EQ(id_of(l, "aaa_native:wand"), wand);
    CHECK_EQ(id_of(l, "rubytools:ruby_sword"), sword);
    CHECK_EQ(lce_loader_block_id(l, "gem:sapphire_block"), 254);
    CHECK(fs.files[kLedger] == ledger_a);  // nothing new allocated -> not rewritten
    bool mismatch_logged = false;
    for (auto &line : fs.log) mismatch_logged |= line.find("folder name differs") != std::string::npos;
    CHECK(mismatch_logged);
    fs.files.erase("/data/lce-mods/aa_first/mod.lcm");
    lce_loader_destroy(l);

    // ===== C. mod removed: its IDs stay reserved; placeholders keep worlds loadable
    fs.files.erase("/data/lce-mods/rubytools/mod.lcm");
    mock_game_init(&game, kAllCaps);
    cfg = config(fs);
    l = lce_loader_create(&cfg);
    lce_loader_add_native(l, "aaa_native", "1.0", native_main);
    lce_loader_discover(l);
    CHECK_EQ(lce_loader_commit(l), LCE_OK);
    plan = lce_loader_plan(l);
    bool placeholder = false;
    for (uint32_t i = 0; i < plan->item_count; ++i)
        if (plan->items[i].placeholder && plan->items[i].id == (uint32_t)sword) placeholder = true;
    CHECK(placeholder);
    CHECK(game.item_slots[sword] != nullptr);
    CHECK(strstr(game.names[sword], "Missing mod content") != nullptr);
    lce_loader_destroy(l);
    // without placeholders the ID is simply left empty but still reserved
    mock_game_init(&game, kAllCaps);
    cfg = config(fs, 0);
    l = lce_loader_create(&cfg);
    lce_loader_discover(l);
    CHECK_EQ(lce_loader_commit(l), LCE_OK);
    CHECK(game.item_slots[sword] == nullptr);
    CHECK(lce_ledger_find(lce_loader_ledger(l), LCE_KIND_ITEM, "rubytools:ruby_sword") == sword);
    lce_loader_destroy(l);
    install(fs, "rubytools", "rubytools");

    // ===== D. corrupt ledger: fail closed, nothing published ================
    {
        Fs f2 = fs;
        f2.files[kLedger][12] ^= 1;
        mock_game_init(&game, kAllCaps);
        lce_loader_config c2 = config(f2);
        l = lce_loader_create(&c2);
        lce_loader_discover(l);
        CHECK_EQ(lce_loader_commit(l), LCE_E_LEDGER);
        CHECK_EQ(lce_loader_phase(l), LCE_PHASE_FAILED);
        CHECK_EQ(game.publish_calls, 0);
        CHECK_EQ(game.prepare_calls, 0);
        CHECK(f2.files[kLedger] != fs.files[kLedger]);  // left as-is for the user to restore
        lce_ev_tick t{sizeof(lce_ev_tick), 1};
        CHECK_EQ(lce_loader_dispatch(l, LCE_EV_SERVER_TICK, &t), 0);
        lce_loader_destroy(l);
    }
    // ===== E. backend prepare refuses (pin mismatch): nothing published ======
    {
        mock_game_init(&game, kAllCaps);
        game.fail_prepare = 1;
        cfg = config(fs);
        l = lce_loader_create(&cfg);
        lce_loader_discover(l);
        CHECK_EQ(lce_loader_commit(l), LCE_E_BACKEND);
        CHECK_EQ(game.publish_calls, 0);
        CHECK_EQ(report(l, "rubymod")->state, LCE_MOD_DROPPED);
        CHECK(game.item_slots[ruby] == nullptr);
        lce_loader_destroy(l);
    }
    // ===== F. partial publish is reported as failure =========================
    {
        mock_game_init(&game, kAllCaps);
        game.fail_publish_after = 2;
        cfg = config(fs);
        l = lce_loader_create(&cfg);
        lce_loader_discover(l);
        CHECK_EQ(lce_loader_commit(l), LCE_E_BACKEND);
        CHECK_EQ(lce_loader_phase(l), LCE_PHASE_FAILED);
        CHECK(strstr(report(l, "rubymod")->reason, "PARTIAL") != nullptr);
        lce_loader_destroy(l);
    }
    // ===== G. strict mode: one failing mod fails the whole load ==============
    {
        mock_game_init(&game, kAllCaps);
        cfg = config(fs, LCE_LOADER_STRICT);
        l = lce_loader_create(&cfg);
        lce_loader_discover(l);
        CHECK(lce_loader_commit(l) != LCE_OK);
        CHECK_EQ(game.publish_calls, 0);
        lce_loader_destroy(l);
    }
    // ===== H. retail-like capability set: items/recipes/textures/strings only
    {
        uint32_t retail_caps = LCE_CAP_ITEMS | LCE_CAP_RECIPES | LCE_CAP_TEXTURES | LCE_CAP_STRINGS | LCE_CAP_CRAFT_TABS | LCE_CAP_CREATIVE;
        Fs f3;
        install(f3, "rubymod", "rubymod");
        install(f3, "rubytools", "rubytools");
        mock_game_init(&game, retail_caps);
        lce_loader_config c3 = config(f3);
        l = lce_loader_create(&c3);
        lce_loader_discover(l);
        CHECK_EQ(lce_loader_commit(l), LCE_OK);  // nothing publishable -> clean empty load
        CHECK_EQ(report(l, "rubymod")->state, LCE_MOD_DROPPED);
        CHECK_EQ(report(l, "rubymod")->status, LCE_E_UNSUPPORTED);
        CHECK(strstr(report(l, "rubymod")->reason, "blocks") != nullptr);
        CHECK_EQ(report(l, "rubytools")->state, LCE_MOD_DROPPED);  // its dependency failed
        CHECK_EQ(game.publish_calls, 0);
        lce_loader_destroy(l);
    }
    // ===== I. enabled markers ===============================================
    {
        Fs f4;
        install(f4, "rubymod", "rubymod");
        install(f4, "gem", "gem");
        f4.files["/data/lce-mods/gem/enabled"] = {};
        mock_game_init(&game, kAllCaps);
        lce_loader_config c4 = config(f4, LCE_LOADER_REQUIRE_ENABLED);
        l = lce_loader_create(&c4);
        lce_loader_discover(l);
        CHECK_EQ(lce_loader_mod_count(l), 1);
        CHECK(report(l, "gem") != nullptr);
        CHECK_EQ(lce_loader_commit(l), LCE_OK);
        lce_loader_destroy(l);
    }
    // ===== J. native.prx present while native mods are disabled =============
    {
        Fs f5;
        install(f5, "gem", "gem");
        f5.files["/data/lce-mods/gem/native.prx"] = {0x7f, 'E', 'L', 'F'};
        mock_game_init(&game, kAllCaps);
        lce_loader_config c5 = config(f5);
        l = lce_loader_create(&c5);
        lce_loader_discover(l);
        CHECK_EQ(report(l, "gem")->state, LCE_MOD_REJECTED);
        CHECK_EQ(report(l, "gem")->status, LCE_E_UNSUPPORTED);
        lce_loader_destroy(l);
    }
    // ===== K. ledger write failure: nothing published ========================
    {
        Fs f6;
        install(f6, "gem", "gem");
        f6.fail_writes = 1;
        mock_game_init(&game, kAllCaps);
        lce_loader_config c6 = config(f6);
        l = lce_loader_create(&c6);
        lce_loader_discover(l);
        CHECK_EQ(lce_loader_commit(l), LCE_E_IO);
        CHECK_EQ(game.publish_calls, 0);
        lce_loader_destroy(l);
    }
    // ===== L. a game update occupies a recorded ID: that mod fails closed =====
    {
        mock_game_init(&game, kAllCaps);
        game.item_slots[ruby] = &game;  // something else now lives there
        cfg = config(fs);
        l = lce_loader_create(&cfg);
        lce_loader_discover(l);
        CHECK_EQ(lce_loader_commit(l), LCE_OK);
        CHECK_EQ(report(l, "rubymod")->state, LCE_MOD_DROPPED);
        CHECK_EQ(report(l, "rubymod")->status, LCE_E_COLLISION);
        CHECK_EQ(report(l, "gem")->state, LCE_MOD_LIVE);
        CHECK(game.item_slots[ruby] == &game);  // untouched
        lce_loader_destroy(l);
    }
    // ===== M. API 1.7 block variants and shapes (BLCK3 -> lce_block_desc deep copy) =====
    {
        Fs f7;
        install(f7, "blockstates", "blockstates");
        mock_game_init(&game, kAllCaps);
        lce_loader_config c7 = config(f7);
        l = lce_loader_create(&c7);
        lce_loader_discover(l);
        CHECK_EQ(lce_loader_commit(l), LCE_OK);
        CHECK_EQ(report(l, "blockstates")->state, LCE_MOD_DROPPED);                 // backend without LCE_CAP2_BLOCK_STATES
        CHECK(strstr(report(l, "blockstates")->reason, "block states") != nullptr);
        lce_loader_destroy(l);
        mock_game_init(&game, kAllCaps);
        c7 = config(f7);
        backend.capabilities2 |= LCE_CAP2_BLOCK_STATES;
        l = lce_loader_create(&c7);
        lce_loader_discover(l);
        CHECK_EQ(lce_loader_commit(l), LCE_OK);
        CHECK_EQ(report(l, "blockstates")->state, LCE_MOD_LIVE);
        int32_t marble = lce_loader_block_id(l, "blockstates:marble"), slab = lce_loader_block_id(l, "blockstates:marble_slab");
        int32_t pane = lce_loader_block_id(l, "blockstates:pane"), weed = lce_loader_block_id(l, "blockstates:weed");
        CHECK(marble > 0 && slab > 0 && pane > 0 && weed > 0);
        if (marble > 0 && slab > 0 && pane > 0 && weed > 0) {
            const lce_block_desc *m = (const lce_block_desc *)game.block_slots[marble], *s = (const lce_block_desc *)game.block_slots[slab];
            const lce_block_desc *p = (const lce_block_desc *)game.block_slots[pane], *w = (const lce_block_desc *)game.block_slots[weed];
            CHECK(m && s && p && w);
            if (m && s && p && w) {
                CHECK_EQ(m->variant_count, 3); CHECK_EQ(m->shape, LCE_SHAPE_CUBE);
                CHECK(m->variant_names && !m->variant_names[0] && !strcmp(m->variant_names[1], "Black Marble") && !strcmp(m->variant_names[2], "Grey Marble"));
                CHECK(m->variant_textures && !m->variant_textures[0] && !strcmp(m->variant_textures[6], "blockstates:marble_black") &&
                      !strcmp(m->variant_textures[8], "blockstates:marble") && m->variant_textures[12] &&
                      !strcmp(m->variant_textures[12], "blockstates:marble_grey"));
                CHECK_EQ(s->shape, LCE_SHAPE_BOX); CHECK(s->box[3] == 16 && s->box[4] == 8 && s->box[5] == 16 && !s->variant_names);
                CHECK((p->flags & LCE_BLOCK_FLAG_TRANSPARENT) && p->box[2] == 7 && p->box[5] == 9 && p->material == LCE_MAT_GLASS);
                CHECK((w->flags & LCE_BLOCK_FLAG_NO_COLLIDE) && w->shape == LCE_SHAPE_CUBE && w->material == LCE_MAT_PLANT);
            }
        }
        lce_loader_destroy(l);
        backend.capabilities2 &= ~LCE_CAP2_BLOCK_STATES;
    }
#ifdef LCE_HAVE_PROJECTILES_1_9
    // ===== O. API 1.9 throwables (ITEM3 -> lce_item_desc projectile_* deep copy, LCE_CAP2_PROJECTILES gate) =====
    {
        Fs f9;
        install(f9, "throwables", "throwables");
        mock_game_init(&game, kAllCaps);
        lce_loader_config c9 = config(f9);
        l = lce_loader_create(&c9);
        lce_loader_discover(l);
        CHECK_EQ(lce_loader_commit(l), LCE_OK);
        CHECK_EQ(report(l, "throwables")->state, LCE_MOD_DROPPED);                  // backend without LCE_CAP2_PROJECTILES
        CHECK(strstr(report(l, "throwables")->reason, "throwable") != nullptr);
        lce_loader_destroy(l);
        mock_game_init(&game, kAllCaps);
        c9 = config(f9);
        backend.capabilities2 |= LCE_CAP2_PROJECTILES;
        l = lce_loader_create(&c9);
        lce_loader_discover(l);
        CHECK_EQ(lce_loader_commit(l), LCE_OK);
        CHECK_EQ(report(l, "throwables")->state, LCE_MOD_LIVE);
        int32_t shard = lce_loader_item_id(l, "throwables:ruby_shard"), bomb = lce_loader_item_id(l, "throwables:fire_bomb");
        int32_t pebble = lce_loader_item_id(l, "throwables:frost_pebble");
        CHECK(shard > 0 && bomb > 0 && pebble > 0);
        if (shard > 0 && bomb > 0 && pebble > 0) {
            const lce_item_desc *a = (const lce_item_desc *)game.item_slots[shard], *b = (const lce_item_desc *)game.item_slots[bomb];
            const lce_item_desc *c = (const lce_item_desc *)game.item_slots[pebble];
            CHECK(a && b && c);
            if (a && b && c) {
                CHECK((a->flags & LCE_ITEM_FLAG_THROWABLE) && (b->flags & LCE_ITEM_FLAG_THROWABLE) && (c->flags & LCE_ITEM_FLAG_THROWABLE));
                CHECK_EQ(a->projectile_speed_milli, 2000u); CHECK_EQ(a->projectile_inaccuracy_milli, 500u); CHECK_EQ(a->projectile_damage_milli, 4000u);
                CHECK_EQ(a->projectile_effect_count, 1u);
                CHECK(a->projectile_effects && !strcmp(a->projectile_effects[0].effect, "minecraft:slowness") &&
                      a->projectile_effects[0].duration_ticks == 100 && a->projectile_effects[0].amplifier == 1 && a->projectile_effects[0].chance_percent == 100);
                CHECK_EQ(b->projectile_fire_ticks, 100u); CHECK_EQ(b->projectile_explode_milli, 1500u); CHECK_EQ(b->projectile_speed_milli, 1200u);
                CHECK(!b->projectile_effects && !b->projectile_effect_count);
                CHECK(!c->projectile_speed_milli && !c->projectile_damage_milli && !c->projectile_effects);
            }
        }
        lce_loader_destroy(l);
        backend.capabilities2 &= ~LCE_CAP2_PROJECTILES;
    }
#endif
#ifdef LCE_HAVE_BLOCK_BOXES_1_11
    // ===== Q. API 1.11 multi-box shapes (BLCK4 -> lce_block_desc boxes deep copy, LCE_CAP2_BLOCK_BOXES gate) =====
    {
        Fs fq;
        install(fq, "shapes", "shapes");
        mock_game_init(&game, kAllCaps);
        backend.capabilities2 |= LCE_CAP2_BLOCK_STATES;
        lce_loader_config cq = config(fq);
        l = lce_loader_create(&cq);
        lce_loader_discover(l);
        CHECK_EQ(lce_loader_commit(l), LCE_OK);
        CHECK_EQ(report(l, "shapes")->state, LCE_MOD_DROPPED);                 // backend without LCE_CAP2_BLOCK_BOXES
        CHECK(strstr(report(l, "shapes")->reason, "multi-box") != nullptr);
        lce_loader_destroy(l);
        mock_game_init(&game, kAllCaps);
        cq = config(fq);
        backend.capabilities2 |= LCE_CAP2_BLOCK_BOXES | LCE_CAP2_BLOCK_STATES;
        l = lce_loader_create(&cq);
        lce_loader_discover(l);
        CHECK_EQ(lce_loader_commit(l), LCE_OK);
        CHECK_EQ(report(l, "shapes")->state, LCE_MOD_LIVE);
        if (report(l, "shapes")->state != LCE_MOD_LIVE) printf("  shapes: %s\n", report(l, "shapes")->reason);
        int32_t st = lce_loader_block_id(l, "shapes:ruby_stairs"), fe = lce_loader_block_id(l, "shapes:dark_fence"), sl = lce_loader_block_id(l, "shapes:slate_slab");
        CHECK(st > 0 && fe > 0 && sl > 0);
        const lce_block_desc *a = st > 0 ? (const lce_block_desc *)game.block_slots[st] : nullptr;
        const lce_block_desc *b = fe > 0 ? (const lce_block_desc *)game.block_slots[fe] : nullptr;
        const lce_block_desc *c = sl > 0 ? (const lce_block_desc *)game.block_slots[sl] : nullptr;
        CHECK(a && b && c);
        if (a && b && c) {
            CHECK(a->shape == LCE_SHAPE_BOXES && a->box_count == 2 && a->box_sets == 4 && a->variant_count == 4 && a->boxes);
            static const uint8_t east_step[6] = {0, 8, 0, 8, 16, 16};      // rotate_boxes: variant 1 = the north step turned 90 degrees
            CHECK(a->boxes && !memcmp(a->boxes + (1 * 2 + 1) * 6, east_step, 6));
            CHECK(b->box_count == 3 && b->box_sets == 1 && b->boxes && b->boxes[0] == 6 && b->boxes[4] == 16);
            CHECK(c->box_sets == 3 && c->boxes && c->boxes[2 * 6 + 4] == 16 && c->boxes[1 * 6 + 1] == 8);
            CHECK(!a->box[0] && !a->box[3]);                                     // the union is the backend's, not the descriptor's
        }
        lce_loader_destroy(l);
        backend.capabilities2 &= ~(LCE_CAP2_BLOCK_BOXES | LCE_CAP2_BLOCK_STATES);
    }
#endif
#ifdef LCE_HAVE_BOATS_1_10
    // ===== P. API 1.10 boat items (ITEM3 boat_type -> lce_item_desc, LCE_CAP2_BOATS gate, descriptor validation) =====
    {
        Fs fb;
        install(fb, "boats", "boats");
        mock_game_init(&game, kAllCaps);
        lce_loader_config cb = config(fb);
        l = lce_loader_create(&cb);
        lce_loader_discover(l);
        CHECK_EQ(lce_loader_commit(l), LCE_OK);
        CHECK_EQ(report(l, "boats")->state, LCE_MOD_DROPPED);                  // backend without LCE_CAP2_BOATS
        CHECK(strstr(report(l, "boats")->reason, "boat") != nullptr);
        lce_loader_destroy(l);
        mock_game_init(&game, kAllCaps);
        cb = config(fb);
        backend.capabilities2 |= LCE_CAP2_BOATS;
        l = lce_loader_create(&cb);
        lce_loader_discover(l);
        CHECK_EQ(lce_loader_commit(l), LCE_OK);
        CHECK_EQ(report(l, "boats")->state, LCE_MOD_LIVE);
        const char *keys[3] = {"boats:redwood_boat", "boats:paper_boat", "boats:jungle_raft"};
        const uint32_t types[3] = {1, 2, 3}, stacks[3] = {1, 4, 1};
        for (int i = 0; i < 3; ++i) {
            int32_t id = lce_loader_item_id(l, keys[i]);
            CHECK(id > 0);
            const lce_item_desc *d = id > 0 ? (const lce_item_desc *)game.item_slots[id] : nullptr;
            CHECK(d != nullptr);
            if (d) { CHECK(d->flags & LCE_ITEM_FLAG_BOAT); CHECK_EQ(d->boat_type, types[i]); CHECK_EQ(d->max_stack, stacks[i]); CHECK(!(d->flags & LCE_ITEM_FLAG_THROWABLE)); }
        }
        lce_loader_destroy(l);
        backend.capabilities2 &= ~LCE_CAP2_BOATS;
    }
#endif
#ifdef LCE_HAVE_TINTED_PARTICLES_1_12
    {
        Fs f;
        lce_loader_config c = config(f);
        l = lce_loader_create(&c);
        const lce_api *api = lce_loader_api(l);
        CHECK(!(api->capabilities2 & LCE_CAP2_SVC_TINTED_PARTICLES));
        CHECK_EQ(api->spawn_tinted_particles(nullptr, nullptr, 0, 0, 0, 0, 1, 0), LCE_E_UNSUPPORTED);
        lce_api services{};
        services.struct_size = offsetof(lce_api, spawn_tinted_particles);
        services.capabilities2 = LCE_CAP2_SVC_TINTED_PARTICLES;
        services.spawn_tinted_particles = +[](lce_mod_ctx *, void *, uint32_t, double, double, double,
                                             uint32_t, double) -> lce_status { return LCE_OK; };
        lce_loader_set_services(l, &services);
        CHECK(!(api->capabilities2 & LCE_CAP2_SVC_TINTED_PARTICLES));
        CHECK_EQ(api->spawn_tinted_particles(nullptr, nullptr, 0, 0, 0, 0, 1, 0), LCE_E_UNSUPPORTED);
        services.struct_size = sizeof services;
        lce_loader_set_services(l, &services);
        CHECK(api->capabilities2 & LCE_CAP2_SVC_TINTED_PARTICLES);
        CHECK_EQ(api->spawn_tinted_particles(nullptr, nullptr, 0, 0, 0, 0, 1, 0), LCE_OK);
        lce_loader_destroy(l);
    }
#endif
#ifdef LCE_HAVE_CONFIG_1_8
    // ===== N. API 1.8 per-mod config: <root>/<modid>/config read in DISCOVER, before natives/scripts run =====
    {
        Fs f8;
        install(f8, "rubymod", "rubymod");
        const char *text = "[general]\nore_rate = 99\nspeed = 2.5\nname = \"Ruby\"\nenable_wand = false\nbroken line\n";
        f8.files["/data/lce-mods/rubymod/config"] = std::vector<uint8_t>(text, text + strlen(text));
        mock_game_init(&game, kAllCaps);
        lce_loader_config c8 = config(f8);
        l = lce_loader_create(&c8);
        CHECK(lce_loader_api(l)->capabilities2 & LCE_CAP2_CONFIG);
        lce_loader_discover(l);
        uint32_t ri = 0;
        for (; ri < lce_loader_mod_count(l); ++ri) if (!strcmp(lce_loader_mod(l, ri)->modid, "rubymod")) break;
        CHECK(ri < lce_loader_mod_count(l));
        lce_mod_ctx *rc = lce_loader_mod_ctx(l, ri);
        const lce_api *A = lce_loader_api(l);
        CHECK_EQ(A->config_int(rc, "general.ore_rate", 8, 1, 64), 8);         // 99 out of range -> default
        CHECK_EQ(A->config_int(rc, "general.ore_rate", 8, 1, 0), 99);
        CHECK(A->config_double(rc, "general.speed", 1, 0, 10) == 2.5);
        CHECK(!strcmp(A->config_string(rc, "general.name", "x"), "Ruby"));
        CHECK_EQ(A->config_int(rc, "general.enable_wand", 1, 0, 1), 0);
        lce_nbt_value v; memset(&v, 0, sizeof v);
        CHECK_EQ(A->config_get(rc, "general.speed", &v), LCE_OK); CHECK_EQ(v.type, LCE_NBT_DOUBLE);
        CHECK_EQ(A->config_get(rc, "nope", &v), LCE_E_NOTFOUND);
        CHECK_EQ(A->config_get(rc, nullptr, &v), LCE_E_INVALID);
        CHECK_EQ(A->config_int(nullptr, "general.ore_rate", 3, 1, 0), 3);
        bool warned = false;
        for (auto &line : f8.log) warned |= line.find("rubymod: config: 1 line(s) skipped, first at line 6") != std::string::npos;
        CHECK(warned);
        CHECK_EQ(lce_loader_set_config_text(l, ri, "a = 1", 5), LCE_E_PHASE);   // after DISCOVER
        CHECK_EQ(lce_loader_commit(l), LCE_OK);
        CHECK_EQ(report(l, "rubymod")->state, LCE_MOD_LIVE);                  // a broken line never drops the mod
        CHECK_EQ(A->config_int(rc, "general.ore_rate", 8, 1, 0), 99);          // still readable while RUNNING
        lce_loader_destroy(l);

        // natives: no file -> defaults (the wand registers); config text set before discover -> the wand is disabled
        for (int pass = 0; pass < 3; ++pass) {
            Fs f9;
            mock_game_init(&game, kAllCaps);
            lce_loader_config c9 = config(f9);
            l = lce_loader_create(&c9);
            CHECK_EQ(lce_loader_add_native(l, "cfgmod", "1.0", cfg_native_main), LCE_OK);
            const char *t = pass == 1 ? "[general]\nore_rate = 12\nspeed = 50\nenable_wand = false\nname = 'Q'\n" : "";
            if (pass) CHECK_EQ(lce_loader_set_config_text(l, 0, t, strlen(t)), LCE_OK);
            CHECK_EQ(lce_loader_set_config_text(l, 7, t, 0), LCE_E_INVALID);
            g_cfg_rate = -1; g_cfg_name = nullptr;
            lce_loader_discover(l);
            CHECK_EQ(lce_loader_commit(l), LCE_OK);
            CHECK_EQ(report(l, "cfgmod")->state, LCE_MOD_LIVE);
            CHECK_EQ(g_cfg_rate, pass == 1 ? 12 : 8);
            CHECK(g_cfg_speed == 1.0);                                          // 50 out of range
            CHECK(g_cfg_name && !strcmp(g_cfg_name, pass == 1 ? "Q" : "default"));
            CHECK_EQ(g_cfg_get, pass == 1 ? LCE_OK : LCE_E_NOTFOUND);
            CHECK_EQ(id_of(l, "cfgmod:wand") > 0, pass != 1);
            lce_loader_destroy(l);
        }
        // oversized file: defaults + a warning, the mod still loads
        Fs f10;
        install(f10, "rubymod", "rubymod");
        f10.files["/data/lce-mods/rubymod/config"] = std::vector<uint8_t>(LCE_CFG_MAX_FILE + 1, '#');
        mock_game_init(&game, kAllCaps);
        lce_loader_config c10 = config(f10);
        l = lce_loader_create(&c10);
        lce_loader_discover(l);
        bool big = false;
        for (auto &line : f10.log) big |= line.find("config larger than") != std::string::npos;
        CHECK(big);
        CHECK_EQ(lce_loader_commit(l), LCE_OK);
        CHECK_EQ(report(l, "rubymod")->state, LCE_MOD_LIVE);
        lce_loader_destroy(l);
    }
#endif
    TEST_MAIN_END("test_loader");
}
