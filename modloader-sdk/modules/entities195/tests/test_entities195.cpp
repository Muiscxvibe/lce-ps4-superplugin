// Host test for entities195 on a fake 1.95 image: game memory is a calloc'd
// region laid out at base+RVA (registry id->mask vector, vanilla vtables with
// the exact GetType / sound-getter code bytes the module checks, Biome::biomes
// with MobSpawnerData lists). Natives are fakes that model the observable
// effect of the real functions (0x28d9c0 fills REGISTRY+0xe8, 0x2d6260 appends
// a WrappedGoal, 0xbd5220 is operator[] of the renderer map).
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <map>
#include "../entities195.h"
#include "../../../tests/check.h"

static uintptr_t g_base;
static uint64_t *g_ids;            // REGISTRY+0xe8 storage
static size_t g_ids_n;
static std::vector<void *> g_heap;
static void *fake_alloc(size_t n) { void *p = aligned_alloc(16, (n + 15) & ~(size_t)15); memset(p, 0, n); g_heap.push_back(p); return p; }
static int g_frees;
static void fake_free(void *p) { ++g_frees; (void)p; }   // heap is reclaimed at exit
static void fake_wstring(void *dst, const uint16_t *u, uint64_t n) {
    uint64_t *w = (uint64_t *)dst; uint16_t *h = (uint16_t *)fake_alloc((n + 1) * 2); memcpy(h, u, n * 2);
    w[1] = (uint64_t)h; w[3] = n; w[4] = n > 7 ? n : 7;
}
static std::string wstr(const void *ws) {
    const uint64_t *w = (const uint64_t *)ws; std::string s; const uint16_t *h = (const uint16_t *)w[1];
    for (uint64_t i = 0; i < w[3]; ++i) s += (char)h[i];
    return s;
}
struct Reg { le195_factory_fn f; int id; std::string name, old; uint64_t mask; };
static std::vector<Reg> g_regs;
static void fake_register(le195_factory_fn f, int id, const void *name, uint64_t mask, const void *old) {
    g_regs.push_back({f, id, wstr(name), wstr(old), mask});
    if ((size_t)id < g_ids_n) g_ids[id] = mask;
}
struct Egg { uint32_t code, c1, c2; std::string name; };
static std::vector<Egg> g_eggs;
static int g_key_dtors;
static void fake_egg(void *out, uint32_t code, const void *name, uint32_t c1, uint32_t c2, uint32_t extra) { (void)out; (void)extra; g_eggs.push_back({code, c1, c2, wstr(name)}); }
static void fake_key_dtor(void *) { ++g_key_dtors; }
static std::map<uint64_t, void *> g_render;
static uintptr_t g_dispatcher_seen;
static void **fake_render_index(void *disp, const uint64_t *mask) { g_dispatcher_seen = (uintptr_t)disp; return &g_render[*mask]; }
struct GoalRec { uintptr_t sel; int prio; void *goal; int del; };
static std::vector<GoalRec> g_goals;
static void fake_add_goal(void *sel, int prio, void *goal, int del) { g_goals.push_back({(uintptr_t)sel, prio, goal, del}); }
static std::vector<std::string> g_ctors;
static double g_last_speed; static const void *g_last_ti;
static void g_float(void *, void *) { g_ctors.push_back("float"); }
static void g_stroll(void *, void *, double s) { g_ctors.push_back("stroll"); g_last_speed = s; }
static void g_look(void *, void *, const void *ti, float d, float, int, int) { g_ctors.push_back(d == 8.0f ? "look" : "look?"); g_last_ti = ti; }
static void g_look_around(void *, void *) { g_ctors.push_back("around"); }
static void g_melee(void *, void *, double s, int) { g_ctors.push_back("melee"); g_last_speed = s; }
static void g_target(void *, void *, const void *ti, int, int, int) { g_ctors.push_back("target"); g_last_ti = ti; }
static void g_hurt(void *, void *, void *, void *, void *) { g_ctors.push_back("hurt"); }
static void g_avoid(void *, void *, const void *ti, float, double, double, float) { g_ctors.push_back("avoid"); g_last_ti = ti; }
static void g_breed(void *, void *, double) { g_ctors.push_back("breed"); }
static int mem_read(void *, uintptr_t a, void *v, size_t n) { memcpy(v, (const void *)a, n); return 1; }
static int mem_write(void *, uintptr_t a, const void *v, size_t n) { memcpy((void *)a, v, n); return 1; }

// ---- fake entity: vanilla vtable slots used by the thunk -------------------
struct FakeInst { uintptr_t vt; double base; };
static FakeInst g_inst[3];
static uintptr_t g_inst_vt[8];
static void inst_set(uintptr_t inst, double v) { ((FakeInst *)inst)->base = v; }
static uintptr_t ent_get_attr(uintptr_t, uintptr_t key) {
    for (int i = 0; i < 3; ++i) if (key == 0x5000u + (uintptr_t)i) return (uintptr_t)&g_inst[i];
    return 0;
}
static float g_health_set;
static float ent_max_health(uintptr_t) { return (float)g_inst[0].base; }
static void ent_set_health(uintptr_t, float h) { g_health_set = h; }
static uint64_t g_level_seen;
static std::map<uintptr_t, uint32_t> g_vt_of_factory;   // factory addr -> vanilla vtable RVA
static void *fake_call_factory(uintptr_t f, void *level) {
    g_level_seen = (uintptr_t)level;
    auto it = g_vt_of_factory.find(f);
    if (it == g_vt_of_factory.end()) return nullptr;
    uintptr_t *obj = (uintptr_t *)fake_alloc(0xc00);
    obj[0] = g_base + it->second;
    return obj;
}

static const le195_base_mob *row(const char *n) { return le195_base_lookup(n); }

// Lay out a vanilla primary vtable at base+vt with RTTI, GetType code and the slots the thunk uses.
static uint8_t *g_code;   // "code" area inside the fake image (base+0x100000)
static size_t g_code_used;
static uintptr_t emit(const uint8_t *b, size_t n) { uintptr_t a = (uintptr_t)(g_code + g_code_used); memcpy(g_code + g_code_used, b, n); g_code_used += (n + 15) & ~(size_t)15; return a; }
static void lay_vtable(const le195_base_mob *r) {
    uintptr_t *vt = (uintptr_t *)(g_base + r->vtable - 16);
    vt[0] = 0; vt[1] = g_base + 0x1300000 + (r->vtable & 0xfff0);   // typeinfo pointer (nonzero)
    for (uint32_t i = 0; i < r->slots; ++i) vt[2 + i] = g_base + 0x200000 + 16 * i;
    uint8_t gt[6] = {0xb8, 0, 0, 0, 0, 0xc3}; uint32_t imm = (uint32_t)r->mask; memcpy(gt + 1, &imm, 4);
    vt[2 + LE195_SLOT_GET_TYPE / 8] = emit(gt, 6);
    const uint8_t snd[11] = {0x48, 0x8d, 0x05, 1, 2, 3, 4, 0x48, 0x8b, 0x00, 0xc3};
    vt[2 + LE195_SLOT_SOUND_HURT / 8] = emit(snd, 11);
    vt[2 + LE195_SLOT_SOUND_DEATH / 8] = emit(snd, 11);
    vt[2 + LE195_SLOT_SOUND_AMBIENT / 8] = emit(snd, 11);
    vt[2 + LE195_SLOT_GET_ATTRIBUTE / 8] = (uintptr_t)&ent_get_attr;
    vt[2 + LE195_SLOT_MAX_HEALTH / 8] = (uintptr_t)&ent_max_health;
    vt[2 + LE195_SLOT_SET_HEALTH / 8] = (uintptr_t)&ent_set_health;
    g_vt_of_factory[g_base + r->factory] = r->vtable;
}

static le195_natives natives() {
    le195_natives n{fake_alloc, fake_free, fake_wstring, fake_register, fake_egg, fake_key_dtor, fake_render_index, fake_add_goal,
                    g_float, g_stroll, g_look, g_look_around, g_melee, g_target, g_hurt, g_avoid, g_breed};
    return n;
}

static lce_entity_desc2 desc(const char *key, const char *base) {
    lce_entity_desc2 d; memset(&d, 0, sizeof d); d.struct_size = sizeof d; d.key = key; d.base_mob = base; d.name = "X";
    return d;
}

// Biome with spawn lists; entries are MobSpawnerData-shaped.
static uintptr_t make_entry(uint64_t mask, uint32_t w, uint32_t mn, uint32_t mx) {
    uint8_t *e = (uint8_t *)fake_alloc(0x48); uintptr_t vt = g_base + LE195_RVA_SPAWNER_VT;
    memcpy(e, &vt, 8); memcpy(e + 8, &w, 4); memcpy(e + 0x10, &mask, 8); memcpy(e + 0x18, &mn, 4); memcpy(e + 0x1c, &mx, 4);
    return (uintptr_t)e;
}
static void set_list(uintptr_t biome, uint32_t off, std::vector<uintptr_t> entries, size_t cap) {
    uintptr_t *buf = (uintptr_t *)fake_alloc(cap * 8 + 8);
    for (size_t i = 0; i < entries.size(); ++i) buf[i] = entries[i];
    uintptr_t v[3] = {(uintptr_t)buf, (uintptr_t)(buf + entries.size()), (uintptr_t)(buf + cap)};
    memcpy((void *)(biome + off), v, sizeof v);
}
static size_t list_len(uintptr_t biome, uint32_t off) { uintptr_t *v = (uintptr_t *)(biome + off); return (v[1] - v[0]) / 8; }
static uintptr_t list_at(uintptr_t biome, uint32_t off, size_t i) { uintptr_t *v = (uintptr_t *)(biome + off); return ((uintptr_t *)v[0])[i]; }
static int32_t biome_lookup(const char *k) { if (!strcmp(k, "minecraft:desert")) return 2; if (!strcmp(k, "minecraft:plains")) return 1; return -1; }

int main() {
    uint8_t *image = (uint8_t *)aligned_alloc(4096, 0x1500000);
    memset(image, 0, 0x1500000);
    g_base = (uintptr_t)image; g_code = image + 0x100000;
    g_ids_n = 201; g_ids = (uint64_t *)calloc(g_ids_n, 8);
    g_ids[50] = 0x5d540041; g_ids[90] = 0x2d680003; g_ids[200] = 0x800000e; g_ids[54] = 0x5d550040;
    uintptr_t *idvec = (uintptr_t *)(g_base + LE195_RVA_REGISTRY + LE195_REG_ID_VEC);
    idvec[0] = (uintptr_t)g_ids; idvec[1] = (uintptr_t)(g_ids + g_ids_n); idvec[2] = idvec[1];
    for (uint32_t i = 0; i < 3; ++i) *(uintptr_t *)(g_base + LE195_RVA_ATTR_MAX_HEALTH + (i == 0 ? 0 : i == 1 ? 0x18 : 0x28)) = 0x5000u + i;
    g_inst_vt[LE195_ATTR_SET_BASE / 8] = (uintptr_t)&inst_set;
    for (FakeInst &f : g_inst) { f.vt = (uintptr_t)g_inst_vt; f.base = 20; }

    // ---- the pinned table ---------------------------------------------------
    CHECK_EQ(le195_base_mob_count, 56);
    CHECK(row("minecraft:zombie") && row("zombie")->id == 54 && row("zombie")->mask == 0x5d550040ull);
    CHECK(row("iron_golem") == row("villager_golem") && row("cod") == row("fish") && row("evoker") == row("evocation_illager"));
    CHECK(!row("ender_dragon") && !row("wither") && !row("pillager") && !row("minecraft:steve") && !row(nullptr));
    for (uint32_t i = 0; i < le195_base_mob_count; ++i) {
        const le195_base_mob &b = le195_base_mobs[i];
        CHECK(b.slots <= LE195_MAX_SLOTS && b.slots >= 470);
        CHECK((b.mask & LE195_MASK_LIVING) == LE195_MASK_LIVING && (b.mask & 0x08000000) && !(b.mask >> 32));
        CHECK(b.id < LE195_FIRST_ID || b.id >= LE195_FIRST_ID + 50);
    }
    // instanceof invariance: for every custom index and every known mask T,
    // (M & T) == T  <=>  (B & T) == T  (no false or lost instanceof).
    static const uint64_t kTests[] = {0x4d540000, 0x4d540440, 0x4d541040, 0x5d108000, 0x5d544000, 0x5d550040, 0x800000a, 0x8a020000, 0xa080000,
        0xa800000, 0xc000000, 0xc800000, 0xd000000, 0xd400000, 0xd502000, 0xd600000, 0xd602000, 0xd680000, 0xd690000, 0xd698000, 0xd6c0000,
        0x15d540041ull, 0x24d540042ull, 0x25d540041ull, 0x15d540040ull, 0x24d540041ull};
    int inv_bad = 0;
    for (uint32_t i = 0; i < le195_base_mob_count; ++i)
        for (uint64_t k = 1; k <= LE195_MAX_TYPES; ++k) {
            uint64_t B = le195_base_mobs[i].mask, M = B | (k << LE195_CUSTOM_SHIFT);
            for (uint64_t T : kTests) if (((M & T) == T) != ((B & T) == T)) ++inv_bad;
            for (uint32_t j = 0; j < le195_base_mob_count; ++j) { uint64_t T = le195_base_mobs[j].mask; if (((M & T) == T) != ((B & T) == T) || M == T) ++inv_bad; }
        }
    CHECK_EQ(inv_bad, 0);

    // ---- check_entity refusals ----------------------------------------------
    le195_options opt{0, {0, 0, 0}};
    char err[160];
    lce_entity_desc2 d = desc("mod:a", "minecraft:steve");
    CHECK_EQ(le195_check_entity(&d, &opt, err, sizeof err), LCE_E_UNSUPPORTED);
    d = desc("mod:a", "minecraft:zombie"); d.width_milli = 600;
    CHECK_EQ(le195_check_entity(&d, &opt, err, sizeof err), LCE_E_UNSUPPORTED);
    lce_drop drop{"minecraft:bone", 1, 2, 100, 0};
    d = desc("mod:a", "minecraft:zombie"); d.drops = &drop; d.drop_count = 1;
    CHECK_EQ(le195_check_entity(&d, &opt, err, sizeof err), LCE_E_UNSUPPORTED);
    d = desc("mod:a", "minecraft:zombie"); d.texture = "mod:skin";
    CHECK_EQ(le195_check_entity(&d, &opt, err, sizeof err), LCE_E_UNSUPPORTED);
    le195_options fb{1, {0, 0, 0}};
    CHECK_EQ(le195_check_entity(&d, &fb, err, sizeof err), LCE_OK);
    lce_ai_goal panic{LCE_AI_PANIC, 1, 1250, nullptr};
    d = desc("mod:a", "minecraft:pig"); d.goals = &panic; d.goal_count = 1;
    CHECK_EQ(le195_check_entity(&d, &opt, err, sizeof err), LCE_E_UNSUPPORTED);
    lce_ai_goal melee{LCE_AI_MELEE_ATTACK, 2, 1000, nullptr};
    d.goals = &melee;
    CHECK_EQ(le195_check_entity(&d, &opt, err, sizeof err), LCE_E_UNSUPPORTED);   // pig has no attack attribute
    lce_ai_goal breed{LCE_AI_BREED, 2, 1000, nullptr};
    d = desc("mod:a", "minecraft:zombie"); d.goals = &breed; d.goal_count = 1;
    CHECK_EQ(le195_check_entity(&d, &opt, err, sizeof err), LCE_E_UNSUPPORTED);   // zombie is not an Animal
    d = desc("mod:a", "minecraft:pig"); d.attack_damage_milli = 3000;
    CHECK_EQ(le195_check_entity(&d, &opt, err, sizeof err), LCE_E_UNSUPPORTED);
    d = desc("mod:a", "minecraft:zombie"); d.sound_ambient = "mod:custom_growl";
    CHECK_EQ(le195_check_entity(&d, &opt, err, sizeof err), LCE_E_UNSUPPORTED);
    d.sound_ambient = "minecraft:entity.husk.hurt";   // wrong slot kind
    CHECK_EQ(le195_check_entity(&d, &opt, err, sizeof err), LCE_E_UNSUPPORTED);
    d.sound_ambient = "minecraft:entity.husk.ambient";
    CHECK_EQ(le195_check_entity(&d, &opt, err, sizeof err), LCE_OK);
    d = desc("mod:a", "minecraft:zombie"); d.max_health_milli = 5000000;
    CHECK_EQ(le195_check_entity(&d, &opt, err, sizeof err), LCE_E_INVALID);
    d = desc("mod:a", "minecraft:zombie"); d.spawn_weight = 5; d.category = 9;
    CHECK_EQ(le195_check_entity(&d, &opt, err, sizeof err), LCE_E_INVALID);

    // ---- configure ----------------------------------------------------------
    lay_vtable(row("zombie")); lay_vtable(row("pig")); lay_vtable(row("husk")); lay_vtable(row("cow")); lay_vtable(row("ocelot"));
    lce_ai_goal zg[3] = {{LCE_AI_SWIM, 0, 0, nullptr}, {LCE_AI_TARGET_PLAYER, 2, 0, nullptr}, {LCE_AI_AVOID_ENTITY, 3, 1100, "minecraft:ocelot"}};
    lce_entity_desc2 ruby = desc("ruby:ruby_zombie", "minecraft:zombie");
    ruby.max_health_milli = 40000; ruby.move_speed_milli = 300; ruby.attack_damage_milli = 5000;
    ruby.goals = zg; ruby.goal_count = 3; ruby.sound_ambient = "minecraft:entity.husk.ambient"; ruby.sound_death = "minecraft:husk";
    ruby.spawn_weight = 50; ruby.category = LCE_MOB_MONSTER; ruby.egg_primary_rgb = 0xff0000;
    lce_ai_goal pg[2] = {{LCE_AI_BREED, 2, 1000, nullptr}, {LCE_AI_WANDER, 6, 1250, nullptr}};
    const char *biomes_desert[] = {"minecraft:desert"};
    lce_entity_desc2 hog = desc("ruby:hog", "pig");
    hog.goals = pg; hog.goal_count = 2; hog.spawn_weight = 7; hog.category = LCE_MOB_CREATURE; hog.min_group = 2; hog.max_group = 3;
    hog.spawn_biomes = biomes_desert; hog.spawn_biome_count = 1;
    lce_plan_ext ext[3] = {{LCE_EXT_ENTITY, &ruby, ruby.key, -1}, {LCE_EXT_FEATURE, nullptr, "x:f", -1}, {LCE_EXT_ENTITY, &hog, hog.key, -1}};
    lce_plan plan; memset(&plan, 0, sizeof plan); plan.ext = ext; plan.ext_count = 3;
    le195_natives n = natives();
    le195_memory mem{nullptr, mem_read, mem_write};
    static le195_module mod;
    le195_natives broken = n; broken.goal_breed = nullptr;
    CHECK_EQ(le195_configure(&mod, g_base, &broken, &mem, &opt, &plan, err, sizeof err), 0);   // unbound native
    lce_entity_desc2 dup = ruby; lce_plan_ext ext2[2] = {{LCE_EXT_ENTITY, &ruby, ruby.key, -1}, {LCE_EXT_ENTITY, &dup, dup.key, -1}};
    lce_plan plan2 = plan; plan2.ext = ext2; plan2.ext_count = 2;
    CHECK_EQ(le195_configure(&mod, g_base, &n, &mem, &opt, &plan2, err, sizeof err), 0);         // duplicate key
    CHECK_EQ(le195_publish_types(&mod), 0);                                                       // not configured
    CHECK_EQ(le195_configure(&mod, g_base, &n, &mem, &opt, &plan, err, sizeof err), 1);
    CHECK_EQ(mod.count, 2);
    CHECK_EQ(ext[0].id, 150); CHECK_EQ(ext[2].id, 151); CHECK_EQ(ext[1].id, -1);
    CHECK(le195_type_mask(&mod, "ruby:ruby_zombie") == (0x5d550040ull | (1ull << 34)));
    CHECK(le195_type_mask(&mod, "ruby:hog") == (0x2d680003ull | (2ull << 34)));
    CHECK_EQ(le195_type_id(&mod, "ruby:hog"), 151); CHECK_EQ(le195_type_id(&mod, "nope:x"), -1);

    // ---- publish_types refusals (nothing written) ---------------------------
    CHECK(le195_create_impl(0, nullptr) == nullptr);   // thunks inert before publish
    g_ids[50] = 0;
    CHECK_EQ(le195_publish_types(&mod), 0); CHECK(g_regs.empty());
    g_ids[50] = 0x5d540041;
    mod.types_state = 0;
    g_ids[151] = 0x1234;   // id taken
    CHECK_EQ(le195_publish_types(&mod), 0); CHECK(g_regs.empty());
    g_ids[151] = 0; mod.types_state = 0;
    idvec[1] = (uintptr_t)(g_ids + 100);   // stock initializer not run yet
    CHECK_EQ(le195_publish_types(&mod), 0); CHECK(g_regs.empty());
    idvec[1] = (uintptr_t)(g_ids + g_ids_n); mod.types_state = 0;
    uintptr_t *zvt = (uintptr_t *)(g_base + row("zombie")->vtable - 16);
    uintptr_t keep = zvt[2 + LE195_SLOT_GET_TYPE / 8];
    zvt[2 + LE195_SLOT_GET_TYPE / 8] = emit((const uint8_t *)"\xb8\x40\x00\x55\x5d\x90", 6);   // not "...; ret"
    CHECK_EQ(le195_publish_types(&mod), 0); CHECK(g_regs.empty());
    zvt[2 + LE195_SLOT_GET_TYPE / 8] = keep; mod.types_state = 0;
    uintptr_t *hvt = (uintptr_t *)(g_base + row("husk")->vtable - 16);
    uintptr_t hkeep = hvt[2 + LE195_SLOT_SOUND_AMBIENT / 8];
    hvt[2 + LE195_SLOT_SOUND_AMBIENT / 8] = emit((const uint8_t *)"\x55\x48\x89\xe5\x48\x8b\x07\x48\x8b\x00\xc3", 11);   // uses this
    CHECK_EQ(le195_publish_types(&mod), 0); CHECK(g_regs.empty());
    hvt[2 + LE195_SLOT_SOUND_AMBIENT / 8] = hkeep; mod.types_state = 0;

    // ---- publish_types ------------------------------------------------------
    CHECK_EQ(le195_publish_types(&mod), 1);
    CHECK_EQ(g_regs.size(), 2);
    CHECK(g_regs[0].name == "ruby:ruby_zombie" && g_regs[0].old == "Zombie" && g_regs[0].id == 150 && g_regs[0].f == le195_factory(0));
    CHECK(g_regs[1].name == "ruby:hog" && g_regs[1].old == "Pig" && g_regs[1].mask == (0x2d680003ull | (2ull << 34)));
    CHECK(g_ids[150] == g_regs[0].mask && g_ids[151] == g_regs[1].mask);
    CHECK_EQ(g_eggs.size(), 1);   // only ruby has egg colours
    CHECK(g_eggs[0].code == row("zombie")->egg_code && g_eggs[0].c1 == 292 && g_eggs[0].c2 == 293 && g_eggs[0].name == "ruby:ruby_zombie");
    CHECK_EQ(g_key_dtors, 1);
    CHECK_EQ(le195_publish_types(&mod), 0);   // once only

    // ---- factory thunk ------------------------------------------------------
    le195_call_factory = fake_call_factory;
    void *level = (void *)0x1234560;
    void *z = le195_factory(0)(level);
    CHECK(z != nullptr); CHECK_EQ(g_level_seen, (uintptr_t)level);
    uintptr_t zv = *(uintptr_t *)z;
    CHECK(zv != g_base + row("zombie")->vtable);
    const uintptr_t *pv = (const uintptr_t *)zv;
    CHECK(pv[-2] == 0 && pv[-1] == zvt[1]);                               // RTTI prefix kept
    CHECK(pv[LE195_SLOT_GET_TYPE / 8] == (uintptr_t)&le195_get_type);
    CHECK(((uint64_t(*)(const void *))pv[LE195_SLOT_GET_TYPE / 8])(z) == le195_type_mask(&mod, "ruby:ruby_zombie"));
    CHECK(pv[LE195_SLOT_SOUND_AMBIENT / 8] == hvt[2 + LE195_SLOT_SOUND_AMBIENT / 8]);
    CHECK(pv[LE195_SLOT_SOUND_DEATH / 8] == hvt[2 + LE195_SLOT_SOUND_DEATH / 8]);
    CHECK(pv[LE195_SLOT_SOUND_HURT / 8] == zvt[2 + LE195_SLOT_SOUND_HURT / 8]);   // untouched
    int diff = 0;
    for (uint32_t i = 0; i < row("zombie")->slots; ++i)
        if (pv[i] != zvt[2 + i] && i != LE195_SLOT_GET_TYPE / 8 && i != LE195_SLOT_SOUND_AMBIENT / 8 && i != LE195_SLOT_SOUND_DEATH / 8) ++diff;
    CHECK_EQ(diff, 0);
    CHECK(g_inst[0].base == 40.0 && g_health_set == 40.0f);             // max health + heal to full
    CHECK(g_inst[1].base == 0.3 && g_inst[2].base == 5.0);               // speed, attack
    CHECK_EQ(g_goals.size(), 3);
    CHECK(g_ctors.size() == 3 && g_ctors[0] == "float" && g_ctors[1] == "target" && g_ctors[2] == "avoid");
    CHECK(g_goals[0].sel == (uintptr_t)z + 0x800 && g_goals[1].sel == (uintptr_t)z + 0x850 && g_goals[2].sel == (uintptr_t)z + 0x800);
    CHECK(g_goals[1].prio == 2 && g_goals[0].del == 1);
    CHECK(g_last_ti == (const void *)zvt[1] || g_last_ti == (const void *)((uintptr_t *)(g_base + row("ocelot")->vtable - 16))[1]);
    CHECK(g_last_ti == (const void *)((uintptr_t *)(g_base + row("ocelot")->vtable - 16))[1]);   // AVOID ocelot -> Ocelot typeinfo
    CHECK_EQ(mod.types[0].created, 1);
    void *h = le195_factory(1)(level);
    CHECK(h && ((uint64_t(*)(const void *))(*(uintptr_t **)h)[LE195_SLOT_GET_TYPE / 8])(h) == le195_type_mask(&mod, "ruby:hog"));
    CHECK(g_ctors.size() == 5 && g_ctors[3] == "breed" && g_ctors[4] == "stroll" && g_last_speed == 1.25);
    // the vanilla factory produced an unexpected class: object returned untouched
    g_vt_of_factory[g_base + row("pig")->factory] = row("cow")->vtable;
    void *odd = le195_factory(1)(level);
    CHECK(odd && *(uintptr_t *)odd == g_base + row("cow")->vtable && mod.types[1].mismatched == 1);
    g_vt_of_factory[g_base + row("pig")->factory] = row("pig")->vtable;
    CHECK(le195_factory(5)(level) == nullptr);                           // unused thunk

    // ---- spawns -------------------------------------------------------------
    uintptr_t *bio = (uintptr_t *)(g_base + LE195_RVA_BIOMES);
    uintptr_t b1 = (uintptr_t)fake_alloc(0x200), b2 = (uintptr_t)fake_alloc(0x200), b3 = (uintptr_t)fake_alloc(0x200);
    bio[1] = b1; bio[2] = b2; bio[3] = b3;
    for (uintptr_t b : {b1, b2, b3}) for (uint32_t off : {LE195_LIST_MONSTER, LE195_LIST_CREATURE, LE195_LIST_WATER, LE195_LIST_AMBIENT}) set_list(b, off, {}, 0);
    set_list(b1, LE195_LIST_MONSTER, {make_entry(0x5d550040, 95, 4, 4), make_entry(0x5d540041, 100, 4, 4)}, 2);   // full: growth path
    set_list(b2, LE195_LIST_MONSTER, {make_entry(0x5d550043, 80, 4, 4)}, 4);                                       // husk only
    set_list(b3, LE195_LIST_MONSTER, {make_entry(0x5d550040, 95, 4, 4)}, 4);
    CHECK_EQ(le195_publish_spawns(&mod, nullptr), 0);          // explicit biome list without a resolver
    mod.spawns_state = 0;
    uintptr_t bad = make_entry(0x5d550040, 1, 1, 1); *(uintptr_t *)bad = 0x42;
    set_list(b3, LE195_LIST_MONSTER, {make_entry(0x5d550040, 95, 4, 4), bad}, 4);
    CHECK_EQ(le195_publish_spawns(&mod, biome_lookup), 0);     // not a MobSpawnerData: refused before writes
    CHECK_EQ(list_len(b1, LE195_LIST_MONSTER), 2);
    set_list(b3, LE195_LIST_MONSTER, {make_entry(0x5d550040, 95, 4, 4)}, 4); mod.spawns_state = 0;
    int frees = g_frees;
    CHECK_EQ(le195_publish_spawns(&mod, biome_lookup), 1);
    CHECK_EQ(list_len(b1, LE195_LIST_MONSTER), 3); CHECK_EQ(list_len(b2, LE195_LIST_MONSTER), 1); CHECK_EQ(list_len(b3, LE195_LIST_MONSTER), 2);
    CHECK_EQ(g_frees, frees + 2);                              // old full buffers released after growth (b1 monster, b2 creature)
    uintptr_t e = list_at(b1, LE195_LIST_MONSTER, 2);
    CHECK(*(uintptr_t *)e == g_base + LE195_RVA_SPAWNER_VT && *(uint32_t *)(e + 8) == 50 && *(uint64_t *)(e + 0x10) == le195_type_mask(&mod, "ruby:ruby_zombie"));
    CHECK(*(uint32_t *)(e + 0x18) == 4 && *(uint32_t *)(e + 0x1c) == 4 && *(uint64_t *)(e + 0x40) == 15);   // base group size, empty string cap
    CHECK(list_at(b1, LE195_LIST_MONSTER, 0) != 0 && *(uint64_t *)(list_at(b1, LE195_LIST_MONSTER, 1) + 0x10) == 0x5d540041);   // stock entries kept in order
    CHECK_EQ(list_len(b2, LE195_LIST_CREATURE), 1); CHECK_EQ(list_len(b1, LE195_LIST_CREATURE), 0);
    uintptr_t he = list_at(b2, LE195_LIST_CREATURE, 0);
    CHECK(*(uint32_t *)(he + 8) == 7 && *(uint32_t *)(he + 0x18) == 2 && *(uint32_t *)(he + 0x1c) == 3);
    CHECK_EQ(le195_publish_spawns(&mod, biome_lookup), 0);   // once only

    // ---- renderers ----------------------------------------------------------
    static uintptr_t zr_obj[4], pr_obj[4];
    zr_obj[0] = g_base + 0x1348300 + 0x10; pr_obj[0] = g_base + 0x133dcf8;
    uintptr_t disp = (uintptr_t)fake_alloc(0x140);
    CHECK_EQ(le195_publish_renderers(&mod, disp), 0);          // no base renderers registered
    CHECK(g_render.count(le195_type_mask(&mod, "ruby:ruby_zombie")) == 0);
    mod.renderers_state = 0;
    g_render[0x5d550040] = zr_obj; g_render[0x2d680003] = pr_obj;
    CHECK_EQ(le195_publish_renderers(&mod, disp), 1);
    CHECK(g_render[le195_type_mask(&mod, "ruby:ruby_zombie")] == zr_obj && g_render[le195_type_mask(&mod, "ruby:hog")] == pr_obj);
    CHECK_EQ(g_dispatcher_seen, disp);

    for (void *p : g_heap) free(p);
    free(g_ids); free(image);
    TEST_MAIN_END("test_entities195");
}
