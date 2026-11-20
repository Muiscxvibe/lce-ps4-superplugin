// Host test (ASan/UBSan) for enchants195 on a fake image: control flow,
// refusals before any write, uncertainty, vtable overrides. The real retail
// behaviour is proven in tests/test_unicorn_retail.py.
#include "../enchants195.h"
#include <map>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

static int g_checks, g_fail;
#define CHECK(c, m) do { g_checks++; if (!(c)) { g_fail++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, m); } } while (0)

namespace {
const uintptr_t B = 0x40000000, SIZE = 0x1a00000;
std::vector<uint8_t> mem(SIZE);
uintptr_t brk_;
unsigned allocs, frees;
int held_v = 1, bad_ctor = 0, skip_register = 0;
std::map<int, uintptr_t> reg_by_id;
std::map<std::string, uintptr_t> reg_by_key;
// fake function addresses for the registry vtable (inside the fake image)
const uintptr_t F_BY = B + 0x2bf020, F_GID = B + 0x2bef00, F_REG = B + 0x2bec20, F_HAS = B + 0x2beb00;

uint64_t Q(uintptr_t a) { uint64_t v; memcpy(&v, &mem[a - B], 8); return v; }
void W(uintptr_t a, uint64_t v) { memcpy(&mem[a - B], &v, 8); }
void W4(uintptr_t a, uint32_t v) { memcpy(&mem[a - B], &v, 4); }
uint32_t D(uintptr_t a) { uint32_t v; memcpy(&v, &mem[a - B], 4); return v; }
int rd(void *, uintptr_t a, void *d, size_t n) { if (a < B || a + n > B + SIZE) return 0; memcpy(d, &mem[a - B], n); return 1; }
int wr(void *, uintptr_t a, const void *s, size_t n) { if (a < B || a + n > B + SIZE) return 0; memcpy(&mem[a - B], s, n); return 1; }
int held(void *) { return held_v; }
uintptr_t alloc(size_t n) { uintptr_t p = brk_; brk_ += (n + 31) & ~15ull; memset(&mem[p - B], 0, n); allocs++; return p; }
std::string rl_key(uintptr_t rl) { return std::string((const char *)&mem[Q(rl + 0x18) - B]); }   // fake RL keeps a char* at +0x18

uintptr_t fake_call(void *, uintptr_t fn, const uint64_t *r, unsigned n, const uint64_t *, unsigned) {
    uint32_t rva = (uint32_t)(fn - B);
    switch (rva) {
    case E195_RVA_ALLOC: return alloc(r[0]);
    case E195_RVA_FREE: frees++; return 0;
    case E195_RVA_WSTRING: {           // (ws, u16*, n): keep a narrow copy for the fake RL
        uintptr_t s = alloc(r[2] + 1);
        for (uint64_t i = 0; i < r[2]; i++) mem[s + i - B] = mem[r[1] + 2 * i - B];
        W(r[0] + 8, s); W(r[0] + 0x18, r[2]); W(r[0] + 0x20, 7);
        return r[0];
    }
    case E195_RVA_RL_CTOR: W(r[0], B + 0x133ff48); W(r[0] + 0x18, Q(r[1] + 8)); return r[0];
    case E195_RVA_RL_DTOR: return 0;
    case E195_RVA_BASE_CTOR: case E195_RVA_DAMAGE_CTOR: case E195_RVA_PROT_CTOR: case E195_RVA_IMPALER_CTOR: {
        uint32_t vt = rva == E195_RVA_BASE_CTOR ? E195_RVA_BASE_VT : rva == E195_RVA_DAMAGE_CTOR ? E195_RVA_DAMAGE_VT : rva == E195_RVA_PROT_CTOR ? E195_RVA_PROT_VT : E195_RVA_IMPALER_VT;
        W(r[0], bad_ctor ? 0 : B + vt);
        W(r[0] + 0x28, r[1]);
        W(r[0] + 0x10, 0x11111111);
        if (rva == E195_RVA_BASE_CTOR) { W(r[0] + 8, r[2]); W(r[0] + 0x18, r[3]); W4(r[0] + 0x20, (uint32_t)r[4]); }
        else if (rva == E195_RVA_IMPALER_CTOR) { W(r[0] + 0x18, r[2]); W4(r[0] + 0x20, (uint32_t)r[3]); }
        else { W(r[0] + 0x30, r[2]); W(r[0] + 0x18, r[3]); W4(r[0] + 0x20, (uint32_t)r[4]); }
        return r[0];
    }
    default: break;
    }
    if (fn == F_BY) { auto it = reg_by_id.find((int)r[1]); return it == reg_by_id.end() ? 0 : it->second; }
    if (fn == F_GID) { for (auto &kv : reg_by_id) if (kv.second == r[1]) return (uintptr_t)(uint32_t)kv.first; return (uintptr_t)(uint32_t)-1; }
    if (fn == F_HAS) return reg_by_key.count(rl_key(r[1])) ? 1 : 0;
    if (fn == F_REG && n == 4) { if (!skip_register) { reg_by_id[(int)r[1]] = r[3]; reg_by_key[rl_key(r[2])] = r[3]; } return 0; }
    return 0;
}

s195_env env() { return s195_env{B, nullptr, rd, wr, held, fake_call}; }

void setup() {
    std::fill(mem.begin(), mem.end(), 0);
    brk_ = B + 0x1500000; allocs = frees = 0; bad_ctor = skip_register = 0; held_v = 1;
    reg_by_id.clear(); reg_by_key.clear();
    uintptr_t reg = alloc(0x150);
    W(B + E195_RVA_REGISTRY, reg);
    W(reg, B + E195_RVA_REGISTRY_VT);
    W(B + E195_RVA_REGISTRY_VT + E195_SLOT_BY_ID, F_BY); W(B + E195_RVA_REGISTRY_VT + E195_SLOT_GET_ID, F_GID);
    W(B + E195_RVA_REGISTRY_VT + E195_SLOT_REGISTER, F_REG); W(B + E195_RVA_REGISTRY_VT + E195_SLOT_CONTAINS, F_HAS);
    // class vtables: 19 slots, slot 0 getRarity 0x24ab30, base +0x50 = 0x24aeb0
    const uint32_t vts[4] = {E195_RVA_BASE_VT, E195_RVA_DAMAGE_VT, E195_RVA_PROT_VT, E195_RVA_IMPALER_VT};
    for (uint32_t vt : vts) {
        W(B + vt - 8, B + vt + 0x1000);             // "RTTI"
        for (unsigned i = 0; i < E195_VT_SLOTS; i++) W(B + vt + 8 * i, B + 0x200000 + vt / 16 + i);
        W(B + vt, B + 0x24ab30);
    }
    W(B + E195_RVA_BASE_VT + E195_SLOT_DESC_ID, B + E195_RVA_GET_DESC_FIELD);
    // constant functions
    for (uint32_t k = 1; k <= 10; k++) { uint8_t c[6] = {0xb8, (uint8_t)k, 0, 0, 0, 0xc3}; memcpy(&mem[e195_const_fn(k)], c, 6); }
    // rarities, prot types, categories
    const uint32_t w[4] = {10, 5, 2, 1};
    for (int i = 0; i < 4; i++) { uintptr_t r = alloc(0x40); W4(r + 0x30, w[i]); W(B + E195_RVA_RARITIES + 8 * i, r); }
    for (int i = 0; i < 5; i++) W(B + E195_RVA_PROT_TYPES + 8 * i, alloc(0x10));
    const uint32_t cg[] = {0x14154b8, 0x14154c0, 0x14154c8, 0x14154d0, 0x14154d8, 0x14154e0, 0x1415480, 0x1415488, 0x1415490, 0x14154e8, 0x14154f0, 0x14154f8, 0x1415500, 0x1415508};
    const uint32_t cv[] = {0x1254ee8, 0x1254f18, 0x1254f48, 0x1254f78, 0x1254fa8, 0x1254fd8, 0x1255008, 0x1255038, 0x1255068, 0x1255098, 0x12550c8, 0x12550f8, 0x1255128, 0x1255158};
    for (int i = 0; i < 14; i++) { uintptr_t c = alloc(8); W(c, B + cv[i]); W(B + cg[i], c); }
    // stock enchantments the clones borrow slot arrays from
    const int ids[] = {0, 1, 2, 3, 4, 16, 17, 18, 80};
    for (int id : ids) {
        uintptr_t o = alloc(0x38), slots = alloc(0x20);
        W(o, B + (id == 16 || id == 17 || id == 18 ? E195_RVA_DAMAGE_VT : id == 80 ? E195_RVA_IMPALER_VT : E195_RVA_PROT_VT));
        W(o + 0x18, slots); W4(o + 0x20, id < 16 ? 4 : 1);
        reg_by_id[id] = o;
    }
}

e195_spec spec(const char *key, int id, uint32_t clone, uint32_t cat = 0, uint8_t lvl = 3, uint8_t wgt = 5, uint8_t tre = 0, uint8_t cur = 0) {
    return e195_spec{key, id, clone, cat, lvl, wgt, tre, cur, 0x4C430100u + (uint32_t)id};
}
}  // namespace

int main() {
    char err[300];
    // clone policy
    CHECK(e195_clone_from_key(nullptr, err, sizeof err) == E195_CLONE_NONE && e195_clone_from_key("", err, sizeof err) == 0, "none");
    CHECK(e195_clone_from_key("minecraft:sharpness", err, sizeof err) == E195_CLONE_SHARPNESS && e195_clone_from_key("smite", err, sizeof err) == E195_CLONE_SMITE, "damage clones");
    CHECK(e195_clone_from_key("minecraft:projectile_protection", err, sizeof err) == E195_CLONE_PROJECTILE_PROTECTION, "protection clones");
    CHECK(e195_clone_from_key("minecraft:impaling", err, sizeof err) == E195_CLONE_IMPALING, "impaling");
    const char *refused[] = {"minecraft:thorns", "minecraft:fire_aspect", "minecraft:mending", "minecraft:silk_touch", "minecraft:fortune", "minecraft:unbreaking", "minecraft:efficiency", "minecraft:looting", "minecraft:infinity", "minecraft:frost_walker"};
    for (const char *k : refused) CHECK(e195_clone_from_key(k, err, sizeof err) == -1 && strstr(err, "identity"), k);
    CHECK(e195_clone_from_key("minecraft:bogus", err, sizeof err) == -1 && strstr(err, "unknown"), "unknown");
    CHECK(e195_clone_from_key("othermod:x", err, sizeof err) == -1, "non-vanilla clone refused");
    // categories from applies_to (1 << lce_item_kind)
    CHECK(e195_category_from_applies(0) == E195_CAT_DEFAULT, "applies 0");
    CHECK(e195_category_from_applies(1u << 2) == E195_CAT_WEAPON, "sword -> weapon");
    CHECK(e195_category_from_applies((1u << 3) | (1u << 5)) == E195_CAT_DIGGER, "pick+shovel -> digger");
    CHECK(e195_category_from_applies(1u << 7) == E195_CAT_ARMOR_HEAD && e195_category_from_applies(1u << 10) == E195_CAT_ARMOR_FEET, "single armour pieces");
    CHECK(e195_category_from_applies((1u << 7) | (1u << 8)) == E195_CAT_ARMOR, "armour mix");
    CHECK(e195_category_from_applies((1u << 2) | (1u << 7)) == E195_CAT_BREAKABLE && e195_category_from_applies(1u << 6) == E195_CAT_BREAKABLE, "mixed / hoe -> breakable");
    CHECK(e195_rarity_index(10) == 0 && e195_rarity_index(5) == 1 && e195_rarity_index(2) == 2 && e195_rarity_index(1) == 3 && e195_rarity_index(7) == 1, "rarity mapping");
    CHECK(e195_const_fn(0) == 0 && e195_const_fn(11) == 0 && e195_const_fn(1) == 0x24ae30 && e195_const_fn(5) == 0x1f1ad0, "const fns");
    // spec checks
    e195_spec s = spec("gem:edge", 100, E195_CLONE_SHARPNESS);
    CHECK(e195_check(&s, err, sizeof err) == 1, "check ok");
    e195_spec bad = s; bad.key = "minecraft:edge"; CHECK(e195_check(&bad, err, sizeof err) == 0, "minecraft: key refused");
    bad = s; bad.key = "Gem:Edge"; CHECK(e195_check(&bad, err, sizeof err) == 0, "uppercase key refused");
    bad = s; bad.id = 92; CHECK(e195_check(&bad, err, sizeof err) == 0, "stock id range refused");
    bad = s; bad.id = 1024; CHECK(e195_check(&bad, err, sizeof err) == 0, "id > 1023 refused");
    bad = s; bad.max_level = 11; CHECK(e195_check(&bad, err, sizeof err) == 0, "level 11 refused");
    bad = s; bad.name_sid = 0x88507d2e; CHECK(e195_check(&bad, err, sizeof err) == 0, "stock string id refused");

    // success
    setup();
    s195_env e = env();
    e195_spec ss[4] = {spec("gem:edge", 100, E195_CLONE_SHARPNESS, 0, 7, 2), spec("gem:ward", 101, E195_CLONE_PROTECTION, E195_CAT_ARMOR_CHEST, 4, 5),
                       spec("gem:soul", 102, E195_CLONE_NONE, 0, 1, 1, 1, 1), spec("gem:tide", 103, E195_CLONE_IMPALING, 0, 10, 10)};
    e195_record rec[4];
    CHECK(e195_publish(&e, ss, 4, rec) == 1, "publish 4");
    CHECK(reg_by_id[100] == rec[0].object && reg_by_key.count("gem:edge"), "registered by id and key");
    for (int i = 0; i < 4; i++) {
        uintptr_t o = rec[i].object, vt = rec[i].vtable;
        CHECK(rec[i].state == 1 && Q(o) == vt + 16, "private vptr");
        CHECK(Q(vt + 16 + E195_SLOT_MAX_LEVEL) == B + e195_const_fn(ss[i].max_level), "max level slot");
        CHECK(Q(vt + 16 + E195_SLOT_DESC_ID) == B + E195_RVA_GET_DESC_FIELD && D(o + 0x10) == ss[i].name_sid, "name sid");
        CHECK(Q(vt + 16 + E195_SLOT_TREASURE) == B + (ss[i].treasure ? E195_RVA_RET1 : E195_RVA_TREASURE_FALSE), "treasure slot");
        CHECK(Q(vt + 16 + E195_SLOT_CURSE) == B + (ss[i].curse ? E195_RVA_RET1 : E195_RVA_CURSE_FALSE), "curse slot");
        CHECK(Q(vt + 8) == Q(B + (i == 0 ? E195_RVA_DAMAGE_VT : i == 1 ? E195_RVA_PROT_VT : i == 2 ? E195_RVA_BASE_VT : E195_RVA_IMPALER_VT) - 8), "RTTI kept");
        CHECK(e195_verify(&e, &rec[i]) == 1, "verify");
    }
    CHECK(Q(rec[1].object + 8) == Q(B + 0x14154d8) && Q(rec[1].vtable + 16 + E195_SLOT_CAN_ENCHANT) == B + E195_RVA_CAN_ENCHANT_CAT, "category override");
    CHECK(Q(rec[0].vtable + 16 + E195_SLOT_CAN_ENCHANT) != B + E195_RVA_CAN_ENCHANT_CAT, "default category keeps the class canEnchant");
    CHECK(Q(rec[2].object + 8) == Q(B + 0x14154e8), "none without category -> BREAKABLE");
    CHECK(Q(rec[0].object + 0x28) == Q(B + E195_RVA_RARITIES + 16), "weight 2 -> RARE");
    CHECK(Q(rec[1].object + 0x30) == Q(B + E195_RVA_PROT_TYPES), "protection Type ALL");

    // refusals before any write
    auto refuse = [&](const char *what, void (*mut)(), e195_spec sp) {
        setup(); if (mut) mut();
        s195_env ee = env();
        e195_record r;
        unsigned a0 = allocs;
        size_t n0 = reg_by_id.size();
        int rc = e195_publish(&ee, &sp, 1, &r);
        CHECK(rc == 0, what);
        CHECK(reg_by_id.size() == n0, what);
        CHECK(allocs == a0 || frees + 1 >= allocs - a0, what);   // module temporaries (RL, wstring, text) freed; +1 = the fake wstring copy
    };
    refuse("id taken", nullptr, spec("gem:x", 16, E195_CLONE_SHARPNESS));
    refuse("registry missing", [] { W(B + E195_RVA_REGISTRY, 0); }, spec("gem:x", 100, 1));
    refuse("registry vtable wrong", [] { W(Q(B + E195_RVA_REGISTRY), B + 0x1234); }, spec("gem:x", 100, 1));
    refuse("bootstrap not run (no sharpness)", [] { reg_by_id.erase(16); }, spec("gem:x", 100, 0));
    refuse("rarity weight mismatch", [] { W4(Q(B + E195_RVA_RARITIES + 8) + 0x30, 7); }, spec("gem:x", 100, 1, 0, 3, 5));
    refuse("category vptr mismatch", [] { W(Q(B + 0x14154d8), 0); }, spec("gem:x", 100, 5, E195_CAT_ARMOR_CHEST));
    refuse("protection type missing", [] { W(B + E195_RVA_PROT_TYPES + 16, 0); }, spec("gem:x", 100, E195_CLONE_FEATHER_FALLING));
    refuse("const fn bytes differ", [] { mem[e195_const_fn(3) + 1] = 9; }, spec("gem:x", 100, 1, 0, 3));
    refuse("key exists", [] { reg_by_key["gem:x"] = 1; }, spec("gem:x", 100, 1));
    refuse("not held", [] { held_v = 0; }, spec("gem:x", 100, 1));
    // batch: one bad spec refuses all
    setup();
    e = env();
    e195_spec two[2] = {spec("gem:a", 100, 1), spec("gem:b", 100, 1)};
    CHECK(e195_publish(&e, two, 2, rec) == 0 && !reg_by_id.count(100), "duplicate ids in a batch refused");
    // uncertainty after writes
    setup(); e = env(); bad_ctor = 1;
    e195_spec one = spec("gem:a", 100, 1);
    CHECK(e195_publish(&e, &one, 1, rec) == -1 && rec[0].state == -1 && !reg_by_id.count(100), "ctor result unexpected -> -1, not registered");
    setup(); e = env(); skip_register = 1;
    CHECK(e195_publish(&e, &one, 1, rec) == -1 && rec[0].state == -1, "register had no effect -> -1");

    printf("test_enchants195: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
