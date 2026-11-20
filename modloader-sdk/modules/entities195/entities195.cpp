// entities195: custom entity types as clones of vanilla 1.95 mobs. See
// entities195.h for the mechanism and phase contract, STATUS.md for evidence.
#include "entities195.h"
#include <stdio.h>
#include <string.h>

const le195_base_mob le195_base_mobs[] = {
#include "base_mobs195.inc"
};
const uint32_t le195_base_mob_count = sizeof le195_base_mobs / sizeof le195_base_mobs[0];

namespace {

le195_module *g_mod;  // process lifetime; one module per process
alignas(16) uintptr_t g_vt[LE195_MAX_TYPES][2 + LE195_MAX_SLOTS];

void set_err(char *err, uint32_t cap, const char *msg) { if (err && cap) snprintf(err, cap, "%s", msg); }
void fail(le195_module *m, const char *msg) { snprintf(m->last_error, sizeof m->last_error, "%s", msg); }

bool rd(const le195_module *m, uintptr_t a, void *v, size_t n) { return a && m->mem.read(m->mem.ctx, a, v, n) == 1; }
bool wr(const le195_module *m, uintptr_t a, const void *v, size_t n) { return a && m->mem.write(m->mem.ctx, a, v, n) == 1; }
bool rq(const le195_module *m, uintptr_t a, uintptr_t *v) { return rd(m, a, v, 8); }

const char *bare(const char *key) { return key && !strncmp(key, "minecraft:", 10) ? key + 10 : key; }

struct Alias { const char *java, *native; };
const Alias kAliases[] = {
    {"iron_golem", "villager_golem"}, {"snow_golem", "snowman"}, {"tropical_fish", "tropicalfish"}, {"cod", "fish"},
    {"evoker", "evocation_illager"}, {"vindicator", "vindication_illager"}, {"illusioner", "illusion_illager"},
    {"zombified_piglin", "zombie_pigman"}, {"mooshroom", "mooshroom"},
};

// Small std::wstring as built at every 0x67840 call site (words[4] = SSO cap 7).
struct WStr {
    const le195_module *m; uint64_t w[5];
    explicit WStr(const le195_module *mod) : m(mod), w{0, 0, 0, 0, 7} {}
    void set(const char *s) {
        uint16_t u[LCE_MAX_KEY]; uint64_t n = 0;
        while (s[n] && n < LCE_MAX_KEY - 1) { u[n] = (unsigned char)s[n]; ++n; }
        m->n.wstring(w, u, n);
    }
    ~WStr() { if (w[4] > 7 && w[1]) m->n.free((void *)w[1]); }
};

bool goal_supported(const le195_base_mob *b, uint16_t kind, char *err, uint32_t cap) {
    switch (kind) {
    case LCE_AI_WANDER: case LCE_AI_LOOK_AT_PLAYER: case LCE_AI_SWIM: case LCE_AI_TARGET_PLAYER: case LCE_AI_AVOID_ENTITY: return true;
    case LCE_AI_MELEE_ATTACK:
        if ((b->mask & LE195_MASK_MONSTER) == LE195_MASK_MONSTER) return true;
        set_err(err, cap, "entities195: MELEE_ATTACK needs a Monster base (attack-damage attribute)"); return false;
    case LCE_AI_BREED:
        if ((b->mask & LE195_MASK_ANIMAL) == LE195_MASK_ANIMAL) return true;
        set_err(err, cap, "entities195: BREED needs an Animal base"); return false;
    default:
        set_err(err, cap, "entities195: PANIC/FLEE_SUN/TEMPT goal constructors are not callable on 1.95 (inlined or by-value item set); STATUS.md");
        return false;
    }
}

// "minecraft:entity.<mob>.<ambient|hurt|death>" or "minecraft:<mob>" -> base row
const le195_base_mob *sound_source(const char *key, uint32_t which) {
    static const char *const kind[3] = {"hurt", "death", "ambient"};
    const char *s = bare(key);
    if (!s || s == key) return nullptr;  // mod sound keys: no custom audio (G11)
    char mob[48];
    if (!strncmp(s, "entity.", 7)) {
        const char *dot = strrchr(s + 7, '.');
        if (!dot || (size_t)(dot - (s + 7)) >= sizeof mob || strcmp(dot + 1, kind[which])) return nullptr;
        memcpy(mob, s + 7, (size_t)(dot - (s + 7))); mob[dot - (s + 7)] = 0;
        return le195_base_lookup(mob);
    }
    return le195_base_lookup(s);
}

uint32_t list_of(uint16_t category) {
    switch (category) {
    case LCE_MOB_CREATURE: return LE195_LIST_CREATURE;
    case LCE_MOB_MONSTER: return LE195_LIST_MONSTER;
    case LCE_MOB_WATER: return LE195_LIST_WATER;
    case LCE_MOB_AMBIENT: return LE195_LIST_AMBIENT;
    default: return 0;
    }
}

// ---- runtime: the code the game calls ---------------------------------------
void attr(le195_module *m, le195_type &t, uintptr_t obj, uintptr_t vt, uint32_t key_rva, double value) {
    uintptr_t key = 0, fn = 0, inst = 0, ivt = 0, set = 0;
    if (!rq(m, m->base + key_rva, &key) || !key || !rq(m, vt + LE195_SLOT_GET_ATTRIBUTE, &fn) || !fn) { ++t.attr_missing; return; }
    inst = ((uintptr_t(*)(uintptr_t, uintptr_t))fn)(obj, key);
    if (!inst || !rq(m, inst, &ivt) || !rq(m, ivt + LE195_ATTR_SET_BASE, &set) || !set) { ++t.attr_missing; return; }
    ((void (*)(uintptr_t, double))set)(inst, value);
}

void add_goals(le195_module *m, le195_type &t, uintptr_t obj) {
    const le195_natives &n = m->n;
    for (uint32_t i = 0; i < t.goal_count; ++i) {
        const lce_ai_goal &g = t.goals[i];
        double speed = g.speed_milli ? g.speed_milli / 1000.0 : 1.0;
        const void *ti = (const void *)(t.goal_ti[i] ? m->base + t.goal_ti[i] : m->base + LE195_RVA_TI_PLAYER);
        size_t size = 0; uintptr_t sel = obj + LE195_GOAL_SELECTOR_OFF;
        switch (g.kind) {
        case LCE_AI_SWIM: size = 0x18; break;
        case LCE_AI_WANDER: case LCE_AI_BREED: case LCE_AI_LOOK_AT_PLAYER: size = 0x48; break;
        case LCE_AI_MELEE_ATTACK: size = 0x60; break;
        case LCE_AI_TARGET_PLAYER: size = 0x80; sel = obj + LE195_TARGET_SELECTOR_OFF; break;
        case LCE_AI_AVOID_ENTITY: size = 0x68; break;
        default: ++t.goal_failed; continue;
        }
        void *goal = n.alloc(size);
        if (!goal) { ++t.goal_failed; continue; }
        memset(goal, 0, size);
        void *mob = (void *)obj;
        switch (g.kind) {
        case LCE_AI_SWIM: n.goal_float(goal, mob); break;
        case LCE_AI_WANDER: n.goal_stroll(goal, mob, speed); break;
        case LCE_AI_BREED: n.goal_breed(goal, mob, speed); break;
        case LCE_AI_LOOK_AT_PLAYER: n.goal_look(goal, mob, ti, 8.0f, 0.02f, 0x168, 0x168); break;  // Creeper's arguments
        case LCE_AI_MELEE_ATTACK: n.goal_melee(goal, mob, speed, 0); break;
        case LCE_AI_TARGET_PLAYER: n.goal_target(goal, mob, ti, 1, 0, 1); break;
        case LCE_AI_AVOID_ENTITY: n.goal_avoid(goal, mob, ti, 6.0f, speed, speed * 1.2, 3.0f); break;  // Creeper vs Ocelot
        }
        n.add_goal((void *)sel, g.priority, goal, 1);  // selector owns it (canDelete)
    }
}

}  // namespace

extern "C" void *le195_create_impl(uint32_t k, void *level);

namespace {
template <unsigned I> void *thunk(void *level) { return le195_create_impl(I, level); }
const le195_factory_fn kThunks[LE195_MAX_TYPES] = {
    thunk<0>, thunk<1>, thunk<2>, thunk<3>, thunk<4>, thunk<5>, thunk<6>, thunk<7>,
    thunk<8>, thunk<9>, thunk<10>, thunk<11>, thunk<12>, thunk<13>, thunk<14>, thunk<15>,
};
}  // namespace

extern "C" {

// Per-process hook so tests (and the console binding) can route the vanilla
// factory call: the image address is base+RVA, which is only executable on the console.
void *(*le195_call_factory)(uintptr_t factory, void *level) = nullptr;

void *le195_create_impl(uint32_t k, void *level) {
    le195_module *m = g_mod;
    if (!m || m->types_state != LE195_TYPES_DONE || k >= m->count || !le195_call_factory) return nullptr;
    le195_type &t = m->types[k];
    void *obj = le195_call_factory(m->base + t.base->factory, level);
    if (!obj) return nullptr;
    uintptr_t o = (uintptr_t)obj, vt = 0, mine = (uintptr_t)&g_vt[k][2], now = 0;
    // Unexpected class: hand the vanilla object back untouched (it saves as the base mob).
    if (!rq(m, o, &vt) || vt != m->base + t.base->vtable) { ++t.mismatched; return obj; }
    if (!wr(m, o, &mine, 8) || !rq(m, o, &now) || now != mine) { ++t.mismatched; return obj; }
    const lce_entity_desc2 *d = t.desc;
    if (d->max_health_milli) {
        attr(m, t, o, mine, LE195_RVA_ATTR_MAX_HEALTH, d->max_health_milli / 1000.0);
        uintptr_t getmax = 0, sethp = 0;
        if (rq(m, mine + LE195_SLOT_MAX_HEALTH, &getmax) && getmax && rq(m, mine + LE195_SLOT_SET_HEALTH, &sethp) && sethp)
            ((void (*)(uintptr_t, float))sethp)(o, ((float (*)(uintptr_t))getmax)(o));
        else ++t.attr_missing;
    }
    if (d->move_speed_milli) attr(m, t, o, mine, LE195_RVA_ATTR_SPEED, d->move_speed_milli / 1000.0);
    if (d->attack_damage_milli) attr(m, t, o, mine, LE195_RVA_ATTR_ATTACK, d->attack_damage_milli / 1000.0);
    add_goals(m, t, o);
    ++t.created;
    return obj;
}

uint64_t le195_get_type(const void *self) {
    const le195_module *m = g_mod;
    uintptr_t vt = *(const uintptr_t *)self;
    uintptr_t first = (uintptr_t)&g_vt[0][2];
    const size_t stride = sizeof g_vt[0];
    if (m && vt >= first && (vt - first) % stride == 0) {
        size_t k = (vt - first) / stride;
        if (k < m->count) return m->types[k].mask;
    }
    return 0;  // unreachable: only installed in our own vtables
}

le195_factory_fn le195_factory(uint32_t i) { return i < LE195_MAX_TYPES ? kThunks[i] : nullptr; }

const le195_base_mob *le195_base_lookup(const char *key) {
    const char *s = bare(key);
    if (!s || !*s) return nullptr;
    for (const Alias &a : kAliases) if (!strcmp(s, a.java)) { s = a.native; break; }
    for (uint32_t i = 0; i < le195_base_mob_count; ++i) if (!strcmp(s, le195_base_mobs[i].name)) return &le195_base_mobs[i];
    return nullptr;
}

lce_status le195_check_entity(const lce_entity_desc2 *d, const le195_options *opt, char *err, uint32_t cap) {
    if (!d || !d->key || !strchr(d->key, ':') || strlen(d->key) >= LCE_MAX_KEY) { set_err(err, cap, "entities195: bad key"); return LCE_E_INVALID; }
    const le195_base_mob *b = le195_base_lookup(d->base_mob);
    if (!b) { set_err(err, cap, "entities195: base_mob is not a clonable vanilla 1.95 LivingEntity (STATUS.md lists them)"); return LCE_E_UNSUPPORTED; }
    if (d->width_milli || d->height_milli) { set_err(err, cap, "entities195: custom hitbox size is not supported (dimensions are set in each native ctor)"); return LCE_E_UNSUPPORTED; }
    if (d->drop_count) { set_err(err, cap, "entities195: custom drops need a loot table (+0xd80 returns a native ResourceLocation*; no loot-table registry hook)"); return LCE_E_UNSUPPORTED; }
    if (d->texture && !(opt && opt->texture_fallback)) { set_err(err, cap, "entities195: custom mob skins are not uploadable on 1.95 yet (memory-texture path unverified); set texture_fallback to use the base skin"); return LCE_E_UNSUPPORTED; }
    if (d->max_health_milli > 1024000u || d->move_speed_milli > 1024000u || d->attack_damage_milli > 2048000u) { set_err(err, cap, "entities195: attribute outside the native RangedAttribute limits"); return LCE_E_INVALID; }
    if (d->attack_damage_milli && (b->mask & LE195_MASK_MONSTER) != LE195_MASK_MONSTER) { set_err(err, cap, "entities195: attack damage needs a Monster base"); return LCE_E_UNSUPPORTED; }
    if (d->goal_count > LE195_MAX_GOALS || (d->goal_count && !d->goals)) { set_err(err, cap, "entities195: at most 8 extra goals"); return LCE_E_INVALID; }
    for (uint32_t i = 0; i < d->goal_count; ++i) {
        if (!goal_supported(b, d->goals[i].kind, err, cap)) return LCE_E_UNSUPPORTED;
        if (d->goals[i].param && (d->goals[i].kind == LCE_AI_TARGET_PLAYER || d->goals[i].kind == LCE_AI_AVOID_ENTITY) &&
            strcmp(bare(d->goals[i].param), "player") && !le195_base_lookup(d->goals[i].param)) {
            set_err(err, cap, "entities195: goal target must be minecraft:player or a vanilla mob"); return LCE_E_UNSUPPORTED;
        }
    }
    const char *snd[3] = {d->sound_hurt, d->sound_death, d->sound_ambient};
    for (uint32_t s = 0; s < 3; ++s)
        if (snd[s] && !sound_source(snd[s], s)) { set_err(err, cap, "entities195: sounds can only reuse a vanilla mob's sound (minecraft:entity.<mob>.<hurt|death|ambient>); custom audio is gap G11"); return LCE_E_UNSUPPORTED; }
    if (d->spawn_weight) {
        if (!list_of(d->category)) { set_err(err, cap, "entities195: unknown mob category"); return LCE_E_INVALID; }
        if (d->min_group > d->max_group) { set_err(err, cap, "entities195: min_group > max_group"); return LCE_E_INVALID; }
        if (d->spawn_biome_count && !d->spawn_biomes) { set_err(err, cap, "entities195: spawn_biomes missing"); return LCE_E_INVALID; }
    }
    return LCE_OK;
}

int le195_configure(le195_module *m, uintptr_t base, const le195_natives *n, const le195_memory *mem,
                    const le195_options *opt, lce_plan *plan, char *err, uint32_t cap) {
    if (!m || !n || !mem || !plan || !base) { set_err(err, cap, "entities195: missing arguments"); return 0; }
    if (g_mod && g_mod != m) { set_err(err, cap, "entities195: another module instance is active"); return 0; }
    memset(m, 0, sizeof *m);
    m->base = base; m->n = *n; m->mem = *mem;
    if (opt) m->opt = *opt;
    const void *const *need = (const void *const *)&m->n;
    for (size_t i = 0; i < sizeof m->n / sizeof(void *); ++i)
        if (!need[i]) { set_err(err, cap, "entities195: natives not bound"); return 0; }
    if (!m->mem.read || !m->mem.write) { set_err(err, cap, "entities195: memory not bound"); return 0; }
    for (uint32_t i = 0; i < plan->ext_count; ++i) {
        lce_plan_ext &e = plan->ext[i];
        if (e.type != LCE_EXT_ENTITY) continue;
        const lce_entity_desc2 *d = (const lce_entity_desc2 *)e.desc;
        char why[160];
        if (le195_check_entity(d, &m->opt, why, sizeof why) != LCE_OK) { set_err(err, cap, why); return 0; }
        if (m->count >= LE195_MAX_TYPES) { set_err(err, cap, "entities195: more than 16 custom entity types"); return 0; }
        for (uint32_t j = 0; j < m->count; ++j)
            if (!strcmp(m->types[j].desc->key, d->key)) { set_err(err, cap, "entities195: duplicate entity key"); return 0; }
        le195_type &t = m->types[m->count];
        t.desc = d; t.base = le195_base_lookup(d->base_mob);
        if (t.base->slots > LE195_MAX_SLOTS) { set_err(err, cap, "entities195: base vtable larger than the private copy"); return 0; }
        t.id = LE195_FIRST_ID + m->count;
        t.mask = t.base->mask | ((uint64_t)(m->count + 1) << LE195_CUSTOM_SHIFT);
        t.list_off = d->spawn_weight ? list_of(d->category) : 0;
        const char *snd[3] = {d->sound_hurt, d->sound_death, d->sound_ambient};
        for (uint32_t s = 0; s < 3; ++s) if (snd[s]) t.sound_slot_src[s] = sound_source(snd[s], s)->vtable;
        t.goal_count = d->goal_count;
        for (uint32_t g = 0; g < d->goal_count; ++g) {
            t.goals[g] = d->goals[g];
            const le195_base_mob *tb = d->goals[g].param ? le195_base_lookup(d->goals[g].param) : nullptr;
            t.goal_ti[g] = 0;
            if (tb && strcmp(bare(d->goals[g].param), "player")) t.goal_ti[g] = tb->vtable;  // resolved to typeinfo at publish
        }
        e.id = (int32_t)t.id;
        ++m->count;
    }
    m->state = LE195_CONFIGURED;
    g_mod = m;
    return 1;
}

int le195_publish_types(le195_module *m) {
    if (!m || g_mod != m || m->state != LE195_CONFIGURED || m->types_state) return 0;
    if (!m->count) { m->types_state = LE195_TYPES_DONE; return 1; }
    // 1. The stock initializer must have run: id->mask vector populated.
    uintptr_t reg = m->base + LE195_RVA_REGISTRY, vb = 0, ve = 0;
    if (!rq(m, reg + LE195_REG_ID_VEC, &vb) || !rq(m, reg + LE195_REG_ID_VEC + 8, &ve) || !vb || ve < vb || (ve - vb) / 8 < 201) {
        fail(m, "registry not populated (call after 0x28dc10)"); m->types_state = LE195_REFUSED; return 0;
    }
    uint64_t creeper = 0, pig = 0, crystal = 0;
    if (!rd(m, vb + 50 * 8, &creeper, 8) || creeper != UINT64_C(0x5d540041) || !rd(m, vb + 90 * 8, &pig, 8) || pig != UINT64_C(0x2d680003) ||
        !rd(m, vb + 200 * 8, &crystal, 8) || crystal != UINT64_C(0x800000e)) {
        fail(m, "registry contents differ from 1.95"); m->types_state = LE195_REFUSED; return 0;
    }
    size_t count = (ve - vb) / 8;
    for (size_t i = 0; i < count; ++i) {
        uint64_t v = 0;
        if (!rd(m, vb + i * 8, &v, 8)) { fail(m, "registry read"); m->types_state = LE195_REFUSED; return 0; }
        for (uint32_t k = 0; k < m->count; ++k)
            if (v == m->types[k].mask || (i == m->types[k].id && v)) { fail(m, "custom id or mask already registered"); m->types_state = LE195_REFUSED; return 0; }
    }
    // 2. Private vtables: RTTI prefix + every primary slot; verify GetType, replace +0x60 and sounds.
    for (uint32_t k = 0; k < m->count; ++k) {
        le195_type &t = m->types[k];
        uintptr_t *vt = g_vt[k];
        memset(vt, 0, sizeof g_vt[k]);
        if (!rd(m, m->base + t.base->vtable - 16, vt, (2 + t.base->slots) * 8) || vt[0] || !vt[1]) { fail(m, "vanilla vtable unreadable"); m->types_state = LE195_REFUSED; return 0; }
        uint8_t code[6] = {0};
        uint32_t imm = (uint32_t)t.base->mask;
        if (!rd(m, vt[2 + LE195_SLOT_GET_TYPE / 8], code, 6) || code[0] != 0xb8 || memcmp(code + 1, &imm, 4) || code[5] != 0xc3) {
            fail(m, "vanilla GetType is not 'mov eax, mask; ret'"); m->types_state = LE195_REFUSED; return 0;
        }
        static const uint32_t kSoundSlots[3] = {LE195_SLOT_SOUND_HURT, LE195_SLOT_SOUND_DEATH, LE195_SLOT_SOUND_AMBIENT};
        for (uint32_t s = 0; s < 3; ++s) {
            if (!t.sound_slot_src[s]) continue;
            uintptr_t fn = 0; uint8_t b[11] = {0};
            // Only a this-free getter ("lea rax,[rip+X]; mov rax,[rax]; ret") may move between classes.
            if (!rq(m, m->base + t.sound_slot_src[s] + kSoundSlots[s], &fn) || !rd(m, fn, b, sizeof b) ||
                b[0] != 0x48 || b[1] != 0x8d || b[2] != 0x05 || b[7] != 0x48 || b[8] != 0x8b || b[9] != 0x00 || b[10] != 0xc3) {
                fail(m, "sound source getter is not a constant SoundEvent getter"); m->types_state = LE195_REFUSED; return 0;
            }
            vt[2 + kSoundSlots[s] / 8] = fn;
        }
        vt[2 + LE195_SLOT_GET_TYPE / 8] = (uintptr_t)&le195_get_type;
        for (uint32_t g = 0; g < t.goal_count; ++g) {
            if (!t.goal_ti[g]) continue;
            uintptr_t ti = 0;   // typeinfo of the named class = vtable[-1]
            if (!rq(m, m->base + t.goal_ti[g] - 8, &ti) || ti < m->base) { fail(m, "goal typeinfo unreadable"); m->types_state = LE195_REFUSED; return 0; }
            t.goal_ti[g] = ti - m->base;
        }
    }
    // 3. Register. From here on failures are uncertain.
    m->types_state = LE195_TYPES_DONE;   // thunks may run as soon as the first type is live
    for (uint32_t k = 0; k < m->count; ++k) {
        le195_type &t = m->types[k];
        WStr name(m), old(m);
        name.set(t.desc->key);
        old.set(t.base->old_name);  // display name: entity.<OldName>.name of the base (no custom localisation)
        m->n.register_type(kThunks[k], (int)t.id, name.w, t.mask, old.w);
        uint64_t got = 0;
        if (!rq(m, reg + LE195_REG_ID_VEC, &vb) || !rd(m, vb + (uintptr_t)t.id * 8, &got, 8) || got != t.mask) {
            fail(m, "registration readback failed"); m->types_state = LE195_UNCERTAIN; return -1;
        }
        if ((t.desc->egg_primary_rgb || t.desc->egg_secondary_rgb) && t.base->egg_code) {
            // Colours are eMinecraftColour enums (colour table), not RGB: the base mob's pair is reused.
            alignas(32) uint8_t egg[0xa0];
            memset(egg, 0, sizeof egg);
            m->n.register_egg(egg, t.base->egg_code, name.w, t.base->egg_c1, t.base->egg_c2, 0);
            m->n.key_dtor(egg + 8);
        }
    }
    return 1;
}

int le195_publish_spawns(le195_module *m, int32_t (*biome_id)(const char *key)) {
    if (!m || g_mod != m || m->types_state != LE195_TYPES_DONE || m->spawns_state) return 0;
    uintptr_t biomes[256];
    if (!rd(m, m->base + LE195_RVA_BIOMES, biomes, sizeof biomes)) { fail(m, "biome array unreadable"); m->spawns_state = LE195_REFUSED; return 0; }
    // Plan every append first (validation without writes).
    struct Add { uint32_t type; uint32_t biome; uint32_t min, max; };
    static Add adds[LE195_MAX_TYPES * 256];
    uint32_t na = 0;
    for (uint32_t k = 0; k < m->count; ++k) {
        const le195_type &t = m->types[k];
        if (!t.list_off) continue;
        const lce_entity_desc2 *d = t.desc;
        for (uint32_t b = 0; b < 256; ++b) {
            if (!biomes[b]) continue;
            bool want = false;
            uint32_t mn = d->min_group, mx = d->max_group;
            if (d->spawn_biomes) {
                for (uint32_t s = 0; s < d->spawn_biome_count; ++s) {
                    int32_t id = biome_id ? biome_id(d->spawn_biomes[s]) : -1;
                    if (id < 0 || id > 255 || !biomes[id]) { fail(m, "unknown spawn biome"); m->spawns_state = LE195_REFUSED; return 0; }
                    if ((uint32_t)id == b) want = true;
                }
            }
            // Validate the list; NULL biomes = every biome whose same list spawns the base mob.
            uintptr_t v[3];
            if (!rd(m, biomes[b] + t.list_off, v, sizeof v) || v[1] < v[0] || v[2] < v[1] || ((v[1] - v[0]) & 7) || (v[1] - v[0]) / 8 > 64) {
                fail(m, "biome spawn list malformed"); m->spawns_state = LE195_REFUSED; return 0;
            }
            for (uintptr_t e = v[0]; e < v[1]; e += 8) {
                uintptr_t entry = 0, evt = 0; uint64_t mask = 0; uint32_t mm[2] = {0, 0};
                if (!rq(m, e, &entry) || !rq(m, entry, &evt) || evt != m->base + LE195_RVA_SPAWNER_VT || !rd(m, entry + 0x10, &mask, 8) || !rd(m, entry + 0x18, mm, 8)) {
                    fail(m, "spawn entry is not a MobSpawnerData"); m->spawns_state = LE195_REFUSED; return 0;
                }
                if (!d->spawn_biomes && mask == t.base->mask) { want = true; if (!mn && !mx) { mn = mm[0]; mx = mm[1]; } }
            }
            if (!want) continue;
            if (!mn && !mx) { mn = 1; mx = 4; }
            if (na >= sizeof adds / sizeof adds[0]) { fail(m, "too many spawn entries"); m->spawns_state = LE195_REFUSED; return 0; }
            adds[na++] = {k, b, mn, mx};
        }
    }
    // Apply. Growth reallocates exactly like libc++ (new buffer via the game allocator, old freed).
    for (uint32_t i = 0; i < na; ++i) {
        const le195_type &t = m->types[adds[i].type];
        uintptr_t list = biomes[adds[i].biome] + t.list_off, v[3];
        uint8_t *e = (uint8_t *)m->n.alloc(LE195_SPAWNER_SIZE);
        if (!e || !rd(m, list, v, sizeof v)) { fail(m, "spawn append failed"); m->spawns_state = i ? LE195_UNCERTAIN : LE195_REFUSED; return i ? -1 : 0; }
        memset(e, 0, LE195_SPAWNER_SIZE);
        uintptr_t vt = m->base + LE195_RVA_SPAWNER_VT; uint32_t w = t.desc->spawn_weight, mm[2] = {adds[i].min, adds[i].max};
        uint64_t cap15 = 15;
        memcpy(e, &vt, 8); memcpy(e + 8, &w, 4); memcpy(e + 0x10, &t.mask, 8); memcpy(e + 0x18, mm, 8); memcpy(e + 0x40, &cap15, 8);
        if (v[1] == v[2]) {
            size_t n = (v[1] - v[0]) / 8, ncap = n ? n * 2 : 4;
            uintptr_t *nb = (uintptr_t *)m->n.alloc(ncap * 8);
            if (!nb || (n && !rd(m, v[0], nb, n * 8))) { fail(m, "spawn list growth failed"); m->spawns_state = LE195_UNCERTAIN; return -1; }
            nb[n] = (uintptr_t)e;
            uintptr_t nv[3] = {(uintptr_t)nb, (uintptr_t)(nb + n + 1), (uintptr_t)(nb + ncap)};
            // begin first, then end/cap: the list is only read by spawning, which has not started.
            if (!wr(m, list, &nv[0], 8) || !wr(m, list + 8, &nv[1], 8) || !wr(m, list + 16, &nv[2], 8)) { fail(m, "spawn list write failed"); m->spawns_state = LE195_UNCERTAIN; return -1; }
            if (v[0]) m->n.free((void *)v[0]);
        } else {
            uintptr_t ep = (uintptr_t)e, end = v[1] + 8;
            if (!wr(m, v[1], &ep, 8) || !wr(m, list + 8, &end, 8)) { fail(m, "spawn list write failed"); m->spawns_state = LE195_UNCERTAIN; return -1; }
        }
    }
    m->spawns_state = LE195_TYPES_DONE;
    return 1;
}

int le195_publish_renderers(le195_module *m, uintptr_t dispatcher) {
    if (!m || g_mod != m || m->types_state != LE195_TYPES_DONE || m->renderers_state || !dispatcher) return 0;
    void *base_r[LE195_MAX_TYPES];
    for (uint32_t k = 0; k < m->count; ++k) {
        // operator[] on an existing key only returns the slot; every clonable base has a stock renderer.
        void **slot = m->n.render_index((void *)dispatcher, &m->types[k].base->mask);
        uintptr_t r = slot ? (uintptr_t)*slot : 0, rvt = 0;
        if (!r || !rq(m, r, &rvt) || rvt < m->base + 0x1200000 || rvt >= m->base + 0x134d928) {
            fail(m, "base renderer missing"); m->renderers_state = k ? LE195_UNCERTAIN : LE195_REFUSED; return k ? -1 : 0;
        }
        base_r[k] = (void *)r;
    }
    for (uint32_t k = 0; k < m->count; ++k) {
        void **slot = m->n.render_index((void *)dispatcher, &m->types[k].mask);
        if (!slot) { fail(m, "renderer insert failed"); m->renderers_state = LE195_UNCERTAIN; return -1; }
        *slot = base_r[k];   // shared stock renderer object: same model and skin as the base mob
    }
    m->renderers_state = LE195_TYPES_DONE;
    return 1;
}

int32_t le195_type_id(const le195_module *m, const char *key) {
    if (!m || !key) return -1;
    for (uint32_t k = 0; k < m->count; ++k) if (!strcmp(m->types[k].desc->key, key)) return (int32_t)m->types[k].id;
    return -1;
}

uint64_t le195_type_mask(const le195_module *m, const char *key) {
    if (!m || !key) return 0;
    for (uint32_t k = 0; k < m->count; ++k) if (!strcmp(m->types[k].desc->key, key)) return m->types[k].mask;
    return 0;
}

#ifndef LE195_TEST
namespace {
uintptr_t g_bind_base;
void *call_factory_native(uintptr_t f, void *level) { return ((void *(*)(void *))f)(level); }
}
void le195_natives_bind(le195_natives *n, uintptr_t b) {
    g_bind_base = b;
    n->alloc = (void *(*)(size_t))(b + LE195_RVA_ALLOC);
    n->free = (void (*)(void *))(b + LE195_RVA_FREE);
    n->wstring = (void (*)(void *, const uint16_t *, uint64_t))(b + LE195_RVA_WSTRING);
    n->register_type = (void (*)(le195_factory_fn, int, const void *, uint64_t, const void *))(b + LE195_RVA_REGISTER);
    n->register_egg = (void (*)(void *, uint32_t, const void *, uint32_t, uint32_t, uint32_t))(b + LE195_RVA_REGISTER_EGG);
    n->key_dtor = (void (*)(void *))(b + LE195_RVA_KEY_DTOR);
    n->render_index = (void **(*)(void *, const uint64_t *))(b + LE195_RVA_RENDER_INDEX);
    n->add_goal = (void (*)(void *, int, void *, int))(b + LE195_RVA_ADD_GOAL);
    n->goal_float = (void (*)(void *, void *))(b + LE195_RVA_GOAL_FLOAT);
    n->goal_stroll = (void (*)(void *, void *, double))(b + LE195_RVA_GOAL_STROLL);
    n->goal_look = (void (*)(void *, void *, const void *, float, float, int, int))(b + LE195_RVA_GOAL_LOOK);
    n->goal_look_around = (void (*)(void *, void *))(b + LE195_RVA_GOAL_LOOK_AROUND);
    n->goal_melee = (void (*)(void *, void *, double, int))(b + LE195_RVA_GOAL_MELEE);
    n->goal_target = (void (*)(void *, void *, const void *, int, int, int))(b + LE195_RVA_GOAL_TARGET);
    n->goal_hurt_by = (void (*)(void *, void *, void *, void *, void *))(b + LE195_RVA_GOAL_HURT_BY);
    n->goal_avoid = (void (*)(void *, void *, const void *, float, double, double, float))(b + LE195_RVA_GOAL_AVOID);
    n->goal_breed = (void (*)(void *, void *, double))(b + LE195_RVA_GOAL_BREED);
    le195_call_factory = call_factory_native;
}
#endif

}  // extern "C"
