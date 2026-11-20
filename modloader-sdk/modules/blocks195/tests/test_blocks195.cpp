// blocks195 on a fake game image built from the REAL retail 1.95 image bytes:
// the three PT_LOAD segments of legacy-195-analysis.elf are copied to host
// memory at base+VA and every R_X86_64_RELATIVE record is applied with that
// base, so the vtables (OreBlock 0x1298cf0, registry 0x1218840, BlockItem
// 0x1218a50, JigsawBlock, FireBlock), pinned code bytes and float constants the
// module checks are the retail ones. Native functions are host fakes with the
// observable effects documented in STATUS.md; game objects live in a host arena.
#include <map>
#include <string>
#include <vector>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../blocks195.h"
#ifdef LCE_HAVE_BLOCK_BOXES_1_11
#define DESC_1_11_TAIL , 0, 0, 0, nullptr
#else
#define DESC_1_11_TAIL
#endif
#include "../../../loader/retail195/id_space_195.h"
#include "../../../integration/sdk_creative_blocks.h"
#include "../../../tests/check.h"

static const char *IMAGE = "/home/codey/minecraft-ps4-research/private-direct-legacy-capture/legacy-195-analysis.elf";
static const size_t IMAGE_SPAN = 0x1500000;
static uint8_t *g_image;
static uintptr_t B;  // base
static uint64_t g_tid = 77;

// ---- arena ----------------------------------------------------------------
alignas(16) static uint8_t g_heap[8 << 20];
static size_t g_used;
static void *arena(size_t n) { n = (n + 15) & ~(size_t)15; if (g_used + n > sizeof g_heap) { fprintf(stderr, "arena full\n"); abort(); } void *p = g_heap + g_used; g_used += n; memset(p, 0, n); return p; }

static uintptr_t U(uintptr_t a) { return *(uintptr_t *)a; }
static void W(uintptr_t a, uintptr_t v) { *(uintptr_t *)a = v; }
template <class T> static T F(uintptr_t a) { T v; memcpy(&v, (const void *)a, sizeof v); return v; }

// ---- fake natives ------------------------------------------------------------
static int f_allocs, f_bad_ctor, f_skip_id, f_name_orig, f_item_name_orig, f_res_orig, f_count_orig;
static void *n_alloc(size_t n) { ++f_allocs; return arena(n); }
// wstring: +8 SSO (cap 7) or heap pointer, +0x18 length, +0x20 capacity
static void n_wstring(void *dst, const uint16_t *u, uint64_t n) {
    uintptr_t d = (uintptr_t)dst;
    if (n < 8) { memcpy((void *)(d + 8), u, n * 2); ((uint16_t *)(d + 8))[n] = 0; W(d + 0x20, 7); }
    else { uint16_t *h = (uint16_t *)arena((n + 1) * 2); memcpy(h, u, n * 2); W(d + 8, (uintptr_t)h); W(d + 0x20, n); }
    W(d + 0x18, n);
}
static std::string narrow(uintptr_t w) {
    uintptr_t len = U(w + 0x18), cap = U(w + 0x20);
    const uint16_t *c = cap < 8 ? (const uint16_t *)(w + 8) : (const uint16_t *)U(w + 8);
    std::string s; for (uintptr_t i = 0; i < len; ++i) s += (char)c[i]; return s;
}
static void make_w(uintptr_t at, const std::string &s) { uint16_t u[128]; for (size_t i = 0; i < s.size(); ++i) u[i] = (uint8_t)s[i]; W(at + 0x20, 7); n_wstring((void *)at, u, s.size()); }
static void n_key(void *key, const void *wstr) {
    std::string s = narrow((uintptr_t)wstr), ns = "minecraft", path = s;
    size_t c = s.find(':'); if (c != std::string::npos) { ns = s.substr(0, c); path = s.substr(c + 1); }
    make_w((uintptr_t)key + 0x18, path); make_w((uintptr_t)key + 0x40, ns);
}
static std::string keystr(const void *k) { return narrow((uintptr_t)k + 0x40) + ":" + narrow((uintptr_t)k + 0x18); }
static std::map<std::string, uintptr_t> g_block_keys, g_item_keys;
static std::vector<uint32_t> g_order;
static void n_register(uintptr_t reg, uint32_t id, const void *key, uintptr_t block) {
    g_order.push_back(id);
    g_block_keys[keystr(key)] = block;
    if (f_skip_id) return;  // misbehaving register for the partial-path test
    *(uint32_t *)(block + B195_F_ID) = id;
    W(reg + B195_REGISTRY_SLOTS_OFF + 8 * id, block);
}
static uint8_t n_present(uintptr_t, const void *k) { return g_block_keys.count(keystr(k)) ? 1 : 0; }
static uintptr_t n_by_key(uintptr_t, const void *k) { auto it = g_block_keys.find(keystr(k)); if (it == g_block_keys.end()) abort(); return it->second; }
static uintptr_t g_stone, g_mats[8];
static void block_base(void *o, uintptr_t mat) {
    uintptr_t b = (uintptr_t)o;
    W(b, B + 0x1217e50 + 0x10);  // Block vtable (0x92150 writes this)
    W(b + B195_F_MATERIAL, mat); W(b + B195_F_SOUND, U(B + 0x1431e00));
    float six = 0.6f; memcpy((void *)(b + B195_F_FRICTION), &six, 4);
}
// Fake 0x924c0: builds the state definition through vt +0x478 (ColoredBlock 0x1a9950 -> 16 states whose +8 is the data value,
// anything else -> one state), then +0x12c = vt +0x1d0 (isSolidRender), as the real tail does through state +0xf8.
static std::map<uintptr_t, std::vector<uintptr_t>> g_states;
static int f_statedef_calls;
static void n_init_states(void *o) {
    uintptr_t b = (uintptr_t)o, vt = U(b);
    *(uint8_t *)(b + 0x158) = 1;
    if (vt == B + B195_RVA_ORE_VTABLE || vt == B + B195_RVA_CHEST_VTABLE || (vt > 0x10000 && (vt < B || vt >= B + IMAGE_SPAN))) {   // OreBlock or a module private vtable
        bool colored = U(vt + 0x478) == B + B195_RVA_COLORED_STATEDEF;
        f_statedef_calls += colored;
        auto &v = g_states[b];
        v.clear();
        // Like the real StateDefinition, the possible-states order is not the data order: states[0] (what 0x924c0 makes the
        // default) is NOT data 0 for a ColoredBlock (console 2026-11-19: "variant default state is not data 0").
        for (unsigned i = 0; i < (colored ? 16u : 1u); ++i) { uintptr_t st = (uintptr_t)arena(16); W(st + 8, colored ? (i * 7u + 3u) % 16u : i); v.push_back(st); }
        W(b + B195_F_STATEDEF, (uintptr_t)arena(16)); W(b + B195_F_DEFSTATE, v[0]);
        uintptr_t solid = U(vt + 0x1d0);
        *(uint8_t *)(b + 0x12c) = !(solid == B + B195_RVA_RET_FALSE || solid == B + 0x353450);
    }
}
// state whose data value is d (what setValue(COLOR, byId(d)) finds); the real 0x1a9850 starts from defaultBlockState()
static uintptr_t state_for(uintptr_t b, unsigned d) { for (uintptr_t st : g_states[b]) if (U(st + 8) == d) return st; return 0; }
static uintptr_t n_state_from_data(void *o, int data) {
    auto &v = g_states[(uintptr_t)o];
    if (v.empty() || !U((uintptr_t)o + B195_F_DEFSTATE)) return 0;
    uintptr_t st = state_for((uintptr_t)o, (unsigned)data < v.size() ? (unsigned)data : 0u);
    return st ? st : v[0];
}
static int n_data_from_state(void *, uintptr_t st) { return (int)U(st + 8); }
static uintptr_t g_face_marker = 0xfacefaceu;
static int f_face_orig;
static uintptr_t n_face_sprite(void *, uintptr_t, uintptr_t) { ++f_face_orig; return g_face_marker; }
static void *n_box(double a, double b, double c, double d, double e, double f) {
    double *p = (double *)arena(0x30); p[0] = a; p[1] = b; p[2] = c; p[3] = d; p[4] = e; p[5] = f; return p;
}
static void n_ore_ctor(void *o) { block_base(o, U(B + 0x1418ec0)); W((uintptr_t)o, f_bad_ctor ? 0x1234 : B + B195_RVA_ORE_VTABLE); n_init_states(o); }
static void n_block_ctor(void *o, uintptr_t mat) { block_base(o, mat); }
static int f_chest_ctor;
static void n_chest_ctor(void *o, int type) {   // fake ChestBlock(type) 0x173060: BaseEntityBlock(WOOD) + vptrs + 0x924c0 + type
    uintptr_t b = (uintptr_t)o;
    ++f_chest_ctor;
    block_base(o, U(B + 0x1418890));
    W(b, B + B195_RVA_CHEST_VTABLE); W(b + B195_F_ENTITY_VPTR, B + B195_RVA_CHEST_ENTITY_VT); *(uint8_t *)(b + B195_F_HAS_BLOCK_ENTITY) = 1;
    n_init_states(o);
    *(int32_t *)(b + B195_F_CHEST_TYPE) = type;
}
// fake newBlockEntity 0x175800 / 0x208ef0 (sret shared_ptr {be, ctrl}) and setCustomName 0x597b30
static int f_new_chest_be, f_new_disp_be, f_set_name;
static std::string g_set_name, g_set_second;
static int g_set_b1 = -1, g_set_b2 = -1;
static void *fake_be(void *out, uintptr_t vt) {
    uintptr_t be = (uintptr_t)arena(0x100);
    W(be, vt); W(be + 0x68, (uintptr_t)arena(0xc0));   // name record
    W((uintptr_t)out, be); W((uintptr_t)out + 8, (uintptr_t)arena(0x18));
    return out;
}
static void *n_chest_new_be(void *out, void *, void *, int) { ++f_new_chest_be; return fake_be(out, B + B195_RVA_CHEST_BE_VTABLE); }
static void *n_disp_new_be(void *out, void *, void *, int) { ++f_new_disp_be; return fake_be(out, B + 0x1239b40u); }
static void n_set_custom_name(void *be, const void *name, const void *second, int b1, int b2) {
    ++f_set_name; g_set_name = narrow((uintptr_t)name); g_set_second = narrow((uintptr_t)second); g_set_b1 = b1; g_set_b2 = b2;
    W(U((uintptr_t)be + 0x68) + 0x38, 1);
}
static void *n_hard(void *o, float h) { uintptr_t b = (uintptr_t)o; memcpy((void *)(b + 0x140), &h, 4); float r = F<float>(b + 0x144); if (h * 5.0f > r) { r = h * 5.0f; memcpy((void *)(b + 0x144), &r, 4); } return o; }
static void *n_res(void *o, float r) { r *= 3.0f; memcpy((void *)((uintptr_t)o + 0x144), &r, 4); return o; }
static void *n_sound(void *o, uintptr_t s) { W((uintptr_t)o + 0x160, s); return o; }
static void *n_light(void *o, float f) { int32_t v = (int32_t)(f * 15.0f); memcpy((void *)((uintptr_t)o + 0x138), &v, 4); return o; }
static std::map<uintptr_t, std::string> g_texname;
static void *n_texture(void *o, void *w) { g_texname[(uintptr_t)o] = narrow((uintptr_t)w); return o; }
static void *n_block_name(void *out, void *) { ++f_name_orig; return out; }
static uintptr_t g_byblock_marker = 0xb10cb10c;
static uintptr_t n_get_resource(void *, uintptr_t, uintptr_t, int) { ++f_res_orig; return g_byblock_marker; }
static int n_count(void *, int, uintptr_t) { ++f_count_orig; return 1; }
static int n_next_int(uintptr_t, int bound) { return bound - 1; }
static uintptr_t *g_items;  // Item::items storage
static uintptr_t n_item_by_id(unsigned id) { return id < 2398 ? g_items[id] : 0; }
static uint8_t n_item_present(uintptr_t, const void *k) { return g_item_keys.count(keystr(k)) ? 1 : 0; }
static void n_item_register(uintptr_t, uint32_t id, const void *k, uintptr_t item) { g_items[id] = item; g_item_keys[keystr(k)] = item; }
static void n_block_item(void *it, uintptr_t block) {
    uintptr_t i = (uintptr_t)it;
    W(i, B + B195_RVA_BLOCK_ITEM_VTABLE); W(i + 0xa0, block); W(i + 0xa8, 0); *(uint32_t *)(i + 0x18) = 64; *(uint32_t *)(i + 0x1c) = 0;
}
// list-backed map at base+0x1417788: +8 head sentinel, +16 count; node +0 next, +16 key, +24 value
static uintptr_t n_map_slot(uintptr_t map, const uintptr_t *block) {
    uintptr_t head = U(map + 8), node = U(head);
    for (uintptr_t i = 0; i < U(map + 16); ++i, node = U(node)) if (U(node + 16) == *block) return node + 24;
    uintptr_t n = (uintptr_t)arena(32);
    W(n, U(head)); W(head, n); W(n + 16, *block); W(n + 24, 0); W(map + 16, U(map + 16) + 1);
    return n + 24;
}
static void *n_item_name(void *out, void *, const void *) { ++f_item_name_orig; return out; }
static std::map<std::pair<uintptr_t, uintptr_t>, int *> g_fire;
static int *n_fire_slot(uintptr_t map, const uintptr_t *block) { int *&p = g_fire[{map, *block}]; if (!p) p = (int *)arena(4); return p; }
static void n_sprite_bind(uintptr_t p, uintptr_t tex, uint64_t, int x, int y, int w, int h, int) {
    W(p + 0x58, tex); W(p + 0x68, (uintptr_t)arena(16)); int32_t v[4] = {x, y, w, h}; memcpy((void *)(p + 0x80), v, 16);
}

// ---- memory services --------------------------------------------------------------
static int g_blocks_held, g_items_held = 1, g_terrain_held = 1, g_uploaded = 1;
static int m_read(void *, uintptr_t a, void *v, size_t n) { memcpy(v, (const void *)a, n); return 1; }
static int m_write(void *, uintptr_t a, const void *v, size_t n) { memcpy((void *)a, v, n); return 1; }
static int m_cas(void *, uintptr_t a, uintptr_t e, uintptr_t d) { return __atomic_compare_exchange_n((uintptr_t *)a, &e, d, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE) ? 1 : 0; }
static uint64_t m_tid(void *) { return g_tid; }
static int m_one(void *) { return 1; }
static int m_blocks(void *) { return g_blocks_held; }
static int m_items(void *) { return g_items_held; }
static int m_terrain(void *) { return g_terrain_held; }
static int m_uploaded(void *, uintptr_t, uintptr_t, uint32_t) { return g_uploaded; }

static b195_natives natives() {
    b195_natives n{};
    n.alloc = n_alloc; n.wstring = n_wstring; n.key_ctor = n_key; n.reg_register = n_register; n.reg_present = n_present; n.reg_by_key = n_by_key;
    n.ore_ctor = n_ore_ctor; n.block_ctor = n_block_ctor; n.block_init_states = n_init_states; n.set_hardness = n_hard; n.set_resistance = n_res;
    n.set_sound = n_sound; n.set_light = n_light; n.set_texture = n_texture; n.block_name = n_block_name; n.get_resource = n_get_resource;
    n.resource_count = n_count; n.next_int = n_next_int; n.item_by_id = n_item_by_id; n.item_present = n_item_present; n.item_register = n_item_register;
    n.block_item_ctor = n_block_item; n.map_slot = n_map_slot; n.item_name = n_item_name; n.fire_slot = n_fire_slot; n.sprite_bind = n_sprite_bind;
    n.state_from_data = n_state_from_data; n.data_from_state = n_data_from_state; n.face_sprite = n_face_sprite; n.box = n_box; n.chest_ctor = n_chest_ctor;
    n.chest_new_be = n_chest_new_be; n.disp_new_be = n_disp_new_be; n.set_custom_name = n_set_custom_name;
    return n;
}
static b195_memory memory() { return b195_memory{nullptr, m_read, m_write, m_cas, m_tid, m_one, m_blocks, m_items, m_terrain, m_uploaded}; }

// ---- image + world ---------------------------------------------------------------------
static bool load_image() {
    FILE *f = fopen(IMAGE, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> elf((size_t)sz);
    if (fread(elf.data(), 1, elf.size(), f) != elf.size()) { fclose(f); return false; }
    fclose(f);
    g_image = (uint8_t *)aligned_alloc(0x4000, IMAGE_SPAN);
    memset(g_image, 0, IMAGE_SPAN);
    B = (uintptr_t)g_image;
    const struct { size_t va, off, n; } seg[] = {{0, 0x4000, 0x10c0354}, {0x1200000, 0x10c8000, 0x14d928}, {0x1400000, 0x1218000, 0x8ac8}};
    for (auto &s : seg) memcpy(g_image + s.va, elf.data() + s.off, s.n);
    unsigned rel = 0;
    for (size_t o = 0x12aaf70 + 0xb550; o < 0x12aaf70 + 0xb550 + 0x3aa2b0; o += 24) {
        uint64_t at, info; int64_t add;
        memcpy(&at, &elf[o], 8); memcpy(&info, &elf[o + 8], 8); memcpy(&add, &elf[o + 16], 8);
        if ((info & 0xffffffff) == 8 && at + 8 <= IMAGE_SPAN) { W(B + at, B + (uint64_t)add); ++rel; }
    }
    return rel > 1000;
}

static const uint32_t k_free[] = {253, 254, 261, 262, 280, 281, 282, 283, 284, 285, 286, 287, 288, 289, 290, 294, 314, 315, 316, 317, 318};
static bool is_free(uint32_t id) { for (uint32_t f : k_free) if (f == id) return true; return false; }
static uintptr_t g_reg, g_fire_obj;
static std::map<uint32_t, uintptr_t> g_stock;  // id -> stock block

static uintptr_t stock_key(const std::string &s) { uintptr_t w = (uintptr_t)arena(0x30), k = (uintptr_t)arena(0x70); make_w(w, s); n_key((void *)k, (void *)w); return k; }
// One stock registration through the registry object's CURRENT vtable (exactly
// how 0x957a0 calls +0x28). A slot holding the pinned stock RVA is "stock code".
static void stock_register(uint32_t id, bool through_vtable = true) {
    uintptr_t blk = (uintptr_t)arena(B195_BLOCK_SIZE);
    W(blk, id == 365 ? B + B195_RVA_JIGSAW_VTABLE : id == 51 ? B + B195_RVA_FIRE_VTABLE : B + B195_RVA_ORE_VTABLE);
    if (id == 51) g_fire_obj = blk;
    g_stock[id] = blk;
    uintptr_t key = stock_key(id == 365 ? "jigsaw" : "stock_" + std::to_string(id));
    uintptr_t fn = through_vtable ? U(U(g_reg) + 8 * B195_REGISTRY_REGISTER_SLOT) : B + B195_RVA_REGISTER;
    if (fn == B + B195_RVA_REGISTER) n_register(g_reg, id, (void *)key, blk);
    else ((void (*)(uintptr_t, uint32_t, const void *, uintptr_t))fn)(g_reg, id, (void *)key, blk);
}
static void stock_init(bool last_through_vtable = true) {
    for (uint32_t id = 0; id < 365; ++id) if (!is_free(id)) stock_register(id);
    stock_register(365, last_through_vtable);
}

static void world(bool registry = true) {
    b195_test_reset();
    g_used = 0; g_block_keys.clear(); g_item_keys.clear(); g_order.clear(); g_stock.clear(); g_fire.clear(); g_texname.clear();
    f_allocs = f_bad_ctor = f_skip_id = f_name_orig = f_item_name_orig = f_res_orig = f_count_orig = 0;
    g_blocks_held = 0; g_items_held = 1; g_terrain_held = 1; g_uploaded = 1;
    // material / sound globals (BSS in the image)
    const uint32_t mats[] = {0x1418ec0, 0x1418890, 0x14183a8, 0x14188f0, 0x1419028, 0x1419040, 0x1418ff8, 0x1419018};
    for (int i = 0; i < 8; ++i) { g_mats[i] = (uintptr_t)arena(64); W(B + mats[i], g_mats[i]); }
    g_stone = g_mats[0];
    for (uint32_t s = 0x1431de8; s <= 0x1431e20; s += 8) W(B + s, (uintptr_t)arena(32));
    g_reg = 0;
    W(B + B195_RVA_REGISTRY_GLOBAL, 0);
    if (registry) { g_reg = (uintptr_t)arena(0xcc0); W(g_reg, B + B195_RVA_REGISTRY_VTABLE); W(B + B195_RVA_REGISTRY_GLOBAL, g_reg); }
    // Item::items: 2268 live, capacity 2398, stock items everywhere except the mapped holes
    g_items = (uintptr_t *)arena(2398 * 8);
    uintptr_t *h = (uintptr_t *)(B + B195_RVA_ITEM_VECTOR);
    h[0] = (uintptr_t)g_items; h[1] = (uintptr_t)(g_items + 2268); h[2] = (uintptr_t)(g_items + 2398);
    for (uint32_t i = 1; i < 600; ++i) if (!is_free(i)) g_items[i] = (uintptr_t)arena(0xa0);
    uintptr_t map = B + B195_RVA_BLOCK_ITEM_MAP, head = (uintptr_t)arena(32);
    W(head, head); W(map + 8, head); W(map + 16, 0);
    W(B + B195_RVA_FIRE_GLOBAL, 0);
}
static void after_stock_globals() { W(B + B195_RVA_FIRE_GLOBAL, g_fire_obj); }  // 0xc1520 sets Blocks::FIRE after Block init

// ---- plan ---------------------------------------------------------------------------------
static uint8_t g_rgba[6][1024];
static lce_plan_texture g_tex[7];
static lce_block_desc g_desc[4];
static lce_plan_block g_plan[4];
static void plan() {
    for (int t = 0; t < 6; ++t) { memset(g_rgba[t], 0x10 * (t + 1), 1024); g_tex[t] = {"rubymod:t", 1, g_rgba[t], -1}; }
    g_tex[6] = {"rubymod:icon", 0, g_rgba[0], -1};
    memset(g_desc, 0, sizeof g_desc);
    memset(g_plan, 0, sizeof g_plan);
    // A: stone ore, custom drop 2..4 x iron ingot (265), resistance 5
    g_desc[0] = lce_block_desc{sizeof(lce_block_desc), "rubymod:ruby_ore", "Ruby Ore", {}, "minecraft:iron_ingot", nullptr, 3000, 5000,
                               LCE_MAT_STONE, LCE_SOUND_STONE, 0, 1, 2, 4, 0, 0, 0, 0, 0, "pickaxe", 0, 0, {}, nullptr, nullptr DESC_1_11_TAIL};
    // B: wood log, 2 face textures, light 7, flammable 20
    g_desc[1] = lce_block_desc{sizeof(lce_block_desc), "rubymod:ruby_log", "Ruby Log \xc3\xa9", {}, nullptr, nullptr, 2000, 0,
                               LCE_MAT_WOOD, LCE_SOUND_WOOD, 7, 0, 0, 0, 0, 0, 0, 20, 0, "axe", 0, 0, {}, nullptr, nullptr DESC_1_11_TAIL};
    // C: metal, slippery, light 15
    g_desc[2] = lce_block_desc{sizeof(lce_block_desc), "rubymod:ice_brick", "Ice Brick", {}, nullptr, nullptr, 500, 0,
                               LCE_MAT_METAL, LCE_SOUND_GLASS, 15, 0, 0, 0, 0, 0, 0, 0, 980, nullptr, 0, 0, {}, nullptr, nullptr DESC_1_11_TAIL};
    // D: dirt, no item
    g_desc[3] = lce_block_desc{sizeof(lce_block_desc), "rubymod:ghost", "Ghost", {}, nullptr, nullptr, 0, 0,
                               LCE_MAT_DIRT, LCE_SOUND_GRAVEL, 0, 0, 0, 0, LCE_BLOCK_FLAG_NO_ITEM, 0, 0, 0, 0, nullptr, 0, 0, {}, nullptr, nullptr DESC_1_11_TAIL};
    const uint32_t ids[4] = {254, 262, 280, 281};
    for (int i = 0; i < 4; ++i) {
        g_plan[i].desc = &g_desc[i]; g_plan[i].id = ids[i];
        g_plan[i].item_id = i == 3 ? 0 : (ids[i] < 256 ? ids[i] : ids[i] + 768);
        for (int f = 0; f < 6; ++f) g_plan[i].texture[f] = i == 1 ? (f < 2 ? 1 : 2) : i == 0 ? 0 : i + 1;
        g_plan[i].drop_item = (int32_t)g_plan[i].item_id;
        g_plan[i].creative_group = -1; g_plan[i].mod_tab = -1;
    }
    g_plan[0].drop_item = 265;
}

static b195_module M;
static const uint32_t k_glass_slots_test[][2] = {{0x0d0, 0x333d60}, {0x100, 0x333d70}, {0x158, 0x333d90}, {0x188, 0x353460}, {0x1d0, 0x353450}};
static int configure(uint32_t n = 4) {
    b195_natives nat = natives(); b195_memory mem = memory();
    return b195_configure(&M, B, &nat, &mem, g_plan, n, g_tex, 7, nullptr);
}
static unsigned vtable_diffs(const uintptr_t *copy, uint32_t rva, unsigned slots) {
    unsigned d = 0; const uintptr_t *orig = (const uintptr_t *)(B + rva - 16);
    for (unsigned i = 0; i < slots + 2; ++i) d += copy[i] != orig[i];
    return d;
}
static std::string wout(uintptr_t out) { return narrow(out); }

// ---- tests -----------------------------------------------------------------------------------
static void test_check_desc() {
    char e[160];
    lce_block_desc d{}; d.struct_size = sizeof d; d.key = "a:b"; d.name = "x";
    CHECK_EQ(b195_check_desc(&d, e, sizeof e), 1);
    CHECK_EQ(b195_backend_check_block(nullptr, &d, e, sizeof e), LCE_OK);
    lce_block_desc x = d; x.block_entity_slots = 54; CHECK_EQ(b195_check_desc(&x, e, sizeof e), 0); CHECK(strstr(e, "block_entity_slots must be 9") != nullptr);
    x = d; x.block_entity_slots = 27; CHECK_EQ(b195_check_desc(&x, e, sizeof e), 0); CHECK(strstr(e, "material must be wood") != nullptr);
    x.material = LCE_MAT_WOOD; CHECK_EQ(b195_check_desc(&x, e, sizeof e), 1);
    x.variant_count = 2; CHECK_EQ(b195_check_desc(&x, e, sizeof e), 0);
    x.variant_count = 0; x.flags = LCE_BLOCK_FLAG_TRANSPARENT; CHECK_EQ(b195_check_desc(&x, e, sizeof e), 0);
    CHECK_EQ(b195_backend_check_block(nullptr, &x, e, sizeof e), LCE_E_UNSUPPORTED);
    x = d; x.block_entity_slots = 9; CHECK_EQ(b195_check_desc(&x, e, sizeof e), 1);   // storage block (DispenserBlockEntity)
    x = d; x.placement = LCE_PLACE_FACING_PLAYER; CHECK_EQ(b195_check_desc(&x, e, sizeof e), 0);
    x = d; x.flags = LCE_BLOCK_FLAG_TRANSPARENT; CHECK_EQ(b195_check_desc(&x, e, sizeof e), 1);   // API 1.7: GlassBlock render set
    x = d; x.flags = LCE_BLOCK_FLAG_NO_COLLIDE; CHECK_EQ(b195_check_desc(&x, e, sizeof e), 1);
    x = d; x.flags = LCE_BLOCK_FLAG_REPLACEABLE; CHECK_EQ(b195_check_desc(&x, e, sizeof e), 0);
    x = d; x.material = LCE_MAT_GLASS; x.flags = LCE_BLOCK_FLAG_TRANSPARENT; CHECK_EQ(b195_check_desc(&x, e, sizeof e), 1);
    x = d; x.material = LCE_MAT_PLANT; x.flags = LCE_BLOCK_FLAG_NO_COLLIDE; CHECK_EQ(b195_check_desc(&x, e, sizeof e), 1);
    // API 1.7 variants and shapes
    static const char *const vt_ok[12] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "a:v1"};
    static const char *const vt_bad[12] = {"a:v0"};
    x = d; x.variant_count = 16; CHECK_EQ(b195_check_desc(&x, e, sizeof e), 1);
    x = d; x.variant_count = 17; CHECK_EQ(b195_check_desc(&x, e, sizeof e), 0); CHECK(strstr(e, "16 variants") != nullptr);
    x = d; x.variant_count = 2; x.variant_textures = vt_ok; CHECK_EQ(b195_check_desc(&x, e, sizeof e), 1);
    x = d; x.variant_count = 2; x.variant_textures = vt_bad; CHECK_EQ(b195_check_desc(&x, e, sizeof e), 0);
    x = d; x.variant_textures = vt_ok; CHECK_EQ(b195_check_desc(&x, e, sizeof e), 0);                 // textures without variants
    x = d; x.shape = LCE_SHAPE_BOX; x.box[3] = 16; x.box[4] = 8; x.box[5] = 16; CHECK_EQ(b195_check_desc(&x, e, sizeof e), 1);   // bottom slab
    x = d; x.shape = LCE_SHAPE_BOX; x.box[3] = 16; x.box[4] = 16; x.box[5] = 16; CHECK_EQ(b195_check_desc(&x, e, sizeof e), 0);  // full cube
    x = d; x.shape = LCE_SHAPE_BOX; x.box[0] = 8; x.box[3] = 8; x.box[4] = 8; x.box[5] = 8; CHECK_EQ(b195_check_desc(&x, e, sizeof e), 0); // empty x
    x = d; x.shape = LCE_SHAPE_BOX; x.box[3] = 17; x.box[4] = 8; x.box[5] = 16; CHECK_EQ(b195_check_desc(&x, e, sizeof e), 0);
    x = d; x.shape = 2; CHECK_EQ(b195_check_desc(&x, e, sizeof e), 0);
    x = d; x.material = LCE_MAT_GLASS; CHECK_EQ(b195_check_desc(&x, e, sizeof e), 0);
    x = d; x.material = LCE_MAT_PLANT; CHECK_EQ(b195_check_desc(&x, e, sizeof e), 0);
    x = d; x.harvest_tier = LCE_TIER_IRON; CHECK_EQ(b195_check_desc(&x, e, sizeof e), 0);
    x = d; x.harvest_tier = LCE_TIER_WOOD; CHECK_EQ(b195_check_desc(&x, e, sizeof e), 1);
    x = d; x.harvest_tool = "axe"; CHECK_EQ(b195_check_desc(&x, e, sizeof e), 0);          // stone + axe
    x = d; x.harvest_tool = "shovel"; x.material = LCE_MAT_DIRT; CHECK_EQ(b195_check_desc(&x, e, sizeof e), 0);
    x = d; x.harvest_tool = "axe"; x.material = LCE_MAT_WOOD; CHECK_EQ(b195_check_desc(&x, e, sizeof e), 1);
    x = d; x.drop_min = 5; x.drop_max = 2; CHECK_EQ(b195_check_desc(&x, e, sizeof e), 0);
    x = d; x.light_emission = 16; CHECK_EQ(b195_check_desc(&x, e, sizeof e), 0);
    x = d; x.slipperiness_milli = 1200; CHECK_EQ(b195_check_desc(&x, e, sizeof e), 0);
    x = d; x.sound = LCE_SOUND_COUNT; CHECK_EQ(b195_check_desc(&x, e, sizeof e), 0);
}

static void test_configure() {
    world(); plan();
    CHECK_EQ(configure(), 1);
    CHECK_EQ(b195_status(&M), B195_CONFIGURED);
    CHECK_EQ(b195_terrain_tile_count(&M), 5u);   // A:t0, B:t1+t2, C:t3, D:t4
    CHECK_EQ(M.blocks[1].per_face, 1); CHECK_EQ(M.blocks[0].per_face, 0);
    CHECK_EQ(M.blocks[0].drop_override, 1); CHECK_EQ(M.blocks[0].count_override, 1); CHECK_EQ(M.blocks[1].drop_override, 0);
    // a second module in the same process is refused
    static b195_module other; b195_natives nat = natives(); b195_memory mem = memory();
    CHECK_EQ(b195_configure(&other, B, &nat, &mem, g_plan, 1, g_tex, 7, nullptr), 0);
    // every allowlisted id configures, reserved / non-allowlisted ids do not
    lce_id_space s; memset(&s, 0, sizeof s); lce195_default_id_space(&s);
    unsigned ok = 0;
    for (uint32_t i = 0; i < s.block_allow_count; ++i) {
        world(); plan(); g_plan[0].id = s.block_allow[i]; g_plan[0].item_id = s.block_allow[i] < 256 ? s.block_allow[i] : s.block_allow[i] + 768;
        ok += configure(1) == 1;
    }
    CHECK_EQ(ok, s.block_allow_count);
    CHECK_EQ(s.block_allow_count, B195_MAX_BLOCKS);
    const uint32_t bad[] = {253, 256, 261, 1, 300, 365};
    for (uint32_t id : bad) { world(); plan(); g_plan[0].id = id; g_plan[0].item_id = id < 256 ? id : id + 768; CHECK_EQ(configure(1), 0); }
    world(); plan(); g_plan[0].item_id = 999; CHECK_EQ(configure(1), 0);                          // wrong block-item id
    world(); plan(); g_plan[1].id = 254; g_plan[1].item_id = 254; CHECK_EQ(configure(2), 0);       // duplicate id
    world(); plan(); g_desc[1].key = "rubymod:ruby_ore"; CHECK_EQ(configure(2), 0);                // duplicate key
    world(); plan(); g_plan[0].texture[3] = 6; CHECK_EQ(configure(1), 0);                          // item-atlas texture on a face
    world(); plan(); g_desc[0].key = "minecraft:stone2"; CHECK_EQ(configure(1), 0);
    world(); plan(); g_plan[0].drop_item = -1; CHECK_EQ(configure(1), 0);
    world(); plan(); g_plan[0].placeholder = 1; CHECK_EQ(configure(1), 0);
    world(); plan(); b195_natives nat2 = natives(); nat2.fire_slot = nullptr; b195_memory mem2 = memory();
    CHECK_EQ(b195_configure(&M, B, &nat2, &mem2, g_plan, 4, g_tex, 7, nullptr), 0);
}

static void test_full_data_hook() {
    world(); plan();
    g_desc[3].block_entity_slots = 9;   // "Ghost" is a storage block
    CHECK_EQ(configure(), 1);
    // registry vptr swap (data write only)
    CHECK_EQ(b195_hook_install(&M), 1);
    uintptr_t priv = (uintptr_t)&M.reg_vtable[2];
    CHECK(U(g_reg) == priv);
    CHECK_EQ(vtable_diffs(M.reg_vtable, B195_RVA_REGISTRY_VTABLE, B195_REGISTRY_SLOTS), 1u);   // only +0x28
    CHECK(M.reg_vtable[1] == B + B195_RVA_REGISTRY_RTTI);                                       // RTTI kept
    CHECK(M.reg_vtable[2 + 5] == (uintptr_t)&b195_register_relay);
    CHECK_EQ(b195_hook_install(&M), 0);                                                          // one-shot
    stock_init();                                                                                // the game's Block init
    CHECK_EQ(M.hook_state, B195_HOOK_FIRED);
    CHECK_EQ(M.hook_publish_result, 1);
    CHECK(U(g_reg) == B + B195_RVA_REGISTRY_VTABLE);                                             // stock vptr restored
    CHECK_EQ(b195_status(&M), B195_BLOCKS_DONE);
    // registration order: 345 stock, 365 last of them, then ours in plan order
    CHECK_EQ(g_order.size(), 349u);
    CHECK_EQ(g_order[344], 365u);
    CHECK_EQ(g_order[345], 254u); CHECK_EQ(g_order[346], 262u); CHECK_EQ(g_order[347], 280u); CHECK_EQ(g_order[348], 281u);
    unsigned occupied = 0;
    for (unsigned i = 0; i <= 365; ++i) occupied += U(g_reg + 0x150 + 8 * i) != 0;
    CHECK_EQ(occupied, 349u);
    b195_block &a = M.blocks[0], &b = M.blocks[1], &c = M.blocks[2], &d = M.blocks[3];
    CHECK(U(g_reg + 0x150 + 8 * 254) == a.object);
    CHECK_EQ(F<uint32_t>(a.object + B195_F_ID), 254u);
    CHECK(U(a.object) == (uintptr_t)&a.vtable[2]);
    CHECK_EQ(vtable_diffs(a.vtable, B195_RVA_ORE_VTABLE, B195_ORE_SLOTS), 3u);   // +0x380 name, +0x240 drop, +0x360 count
    CHECK_EQ(vtable_diffs(b.vtable, B195_RVA_ORE_VTABLE, B195_ORE_SLOTS), 1u);   // name only
    CHECK(a.vtable[2 + 0x258 / 8] == B + B195_RVA_ORE_SPAWN);                    // spawnResources stays stock
    CHECK(a.vtable[1] == B + B195_RVA_ORE_RTTI);
    // storage block: BaseEntityBlock layout over the real DispenserBlock / RTTI bytes
    CHECK_EQ(a.slots, 0u); CHECK_EQ(d.slots, 9u);
    CHECK_EQ(F<uint8_t>(a.object + B195_F_HAS_BLOCK_ENTITY), 0); CHECK_EQ(F<uint8_t>(d.object + B195_F_HAS_BLOCK_ENTITY), 1);
    CHECK(U(d.object + B195_F_ENTITY_VPTR) == (uintptr_t)&d.entity_vtable[2]);
    CHECK(d.entity_vtable[0] == (uintptr_t)-0x1e8 && d.entity_vtable[1] == (uintptr_t)d.typeinfo && d.entity_vtable[4] == (uintptr_t)&b195_new_block_entity_dispatch);
    {   // newBlockEntity through the private table: stock 0x208ef0 (fake), then the BE is named with the block's display name
        typedef void *(*nbe_fn)(void *, void *, void *, int);
        uintptr_t sp[2] = {0, 0};
        f_new_disp_be = f_set_name = 0;
        nbe_fn fn = (nbe_fn)U(U(d.object + B195_F_ENTITY_VPTR) + 0x10);
        CHECK(fn((void *)sp, (void *)(d.object + B195_F_ENTITY_VPTR), (void *)0x1e7e1, 3) == (void *)sp);
        CHECK_EQ(f_new_disp_be, 1); CHECK_EQ(f_set_name, 1); CHECK(sp[0] != 0);
        CHECK(g_set_name == narrow((uintptr_t)d.title_text) && !g_set_name.empty() && g_set_second.empty() && g_set_b1 == 0 && g_set_b2 == 1);
        CHECK(U(sp[0]) == B + 0x1239b40u);   // a dispenser BE keeps its vtable (no renderer to cull)
    }
    CHECK(U(B + 0x1239608 + 0x510) == B + 0x208ef0 && U(B + 0x1239608 + 0x4f0) == (uintptr_t)-0x1e8);   // same as the dispenser's
    CHECK(d.vtable[1] == (uintptr_t)d.typeinfo && d.typeinfo[2] == B + B195_RVA_BASE_ENTITY_RTTI && d.typeinfo[0] == U(B + B195_RVA_ORE_RTTI));
    CHECK(!strcmp((const char *)d.typeinfo[1], "N6lcesdk16ModStorageBlockE"));
    CHECK_EQ(vtable_diffs(d.vtable, B195_RVA_ORE_VTABLE, B195_ORE_SLOTS), 8u);   // RTTI + name + the 6 dispenser container slots
    for (uint32_t off : {0x228u, 0x2c0u, 0x340u, 0x398u, 0x408u, 0x410u}) CHECK(d.vtable[2 + off / 8] == U(B + 0x1239608 + off));
    CHECK(d.vtable[2 + 0x2d0 / 8] == U(B + B195_RVA_ORE_VTABLE + 0x2d0) && d.vtable[2 + 0x478 / 8] == U(B + B195_RVA_ORE_VTABLE + 0x478));   // no FACING
    CHECK(d.vtable[2 + 0x1f0 / 8] == U(B + B195_RVA_ORE_VTABLE + 0x1f0) && d.vtable[2 + 0x208 / 8] == U(B + B195_RVA_ORE_VTABLE + 0x208));   // no dispensing
    // properties
    CHECK(F<float>(a.object + B195_F_HARDNESS) == 3.0f); CHECK(F<float>(a.object + B195_F_RESISTANCE) == 15.0f);
    CHECK(F<float>(b.object + B195_F_RESISTANCE) == 10.0f);                      // hardness 2 -> 2*5
    CHECK(U(a.object + B195_F_MATERIAL) == g_mats[0]); CHECK(U(b.object + B195_F_MATERIAL) == g_mats[1]);
    CHECK(U(c.object + B195_F_MATERIAL) == g_mats[2]); CHECK(U(d.object + B195_F_MATERIAL) == g_mats[3]);
    CHECK(U(b.object + B195_F_SOUND) == U(B + 0x1431de8)); CHECK(U(c.object + B195_F_SOUND) == U(B + 0x1431e10));
    CHECK_EQ(F<int32_t>(b.object + B195_F_LIGHT), 7); CHECK_EQ(F<int32_t>(c.object + B195_F_LIGHT), 15); CHECK_EQ(F<int32_t>(a.object + B195_F_LIGHT), 0);
    CHECK(F<float>(c.object + B195_F_FRICTION) == 0.98f); CHECK(F<float>(a.object + B195_F_FRICTION) == 0.6f);
    CHECK_EQ(F<uint8_t>(b.object + B195_F_PER_FACE), 1); CHECK_EQ(F<uint8_t>(a.object + B195_F_PER_FACE), 0);
    CHECK(g_texname[a.object] == "lcesdk_b254");
    // names through the private slot
    alignas(16) uint8_t out[0x30] = {};
    CHECK(b195_block_name_dispatch(out, (void *)a.object) == out);
    CHECK(wout((uintptr_t)out) == "Ruby Ore");
    CHECK(b195_block_name_dispatch(out, (void *)b.object) == out);
    CHECK_EQ(F<uint64_t>((uintptr_t)out + 0x18), 10u);                            // "Ruby Log e-acute": UTF-16 kept
    CHECK_EQ(((const uint16_t *)U((uintptr_t)out + 8))[9], 0xe9);
    b195_block_name_dispatch(out, (void *)g_stock[1]);
    CHECK_EQ(f_name_orig, 1);
    // drops before items: no Item* yet -> forward
    CHECK(b195_get_resource_dispatch((void *)a.object, 0, 1, 0) == g_byblock_marker);
    CHECK_EQ(b195_resource_count_dispatch((void *)a.object, 0, 0x5000), 4);      // 2 + nextInt(3) (fake returns bound-1)
    CHECK_EQ(b195_resource_count_dispatch((void *)b.object, 0, 0x5000), 1);      // forwarded
    CHECK_EQ(f_count_orig, 1);

    // ---- items phase ----
    after_stock_globals();
    CHECK_EQ(b195_items_publish(&M), 1);
    CHECK_EQ(b195_status(&M), B195_ITEMS_DONE);
    CHECK(g_items[254] == a.item && a.item); CHECK(g_items[1030] == b.item); CHECK(g_items[1048] == c.item); CHECK(!d.item);
    CHECK_EQ(U(B + B195_RVA_BLOCK_ITEM_MAP + 16), 3u);
    CHECK(U(a.item) == (uintptr_t)&a.item_vtable[2]);
    CHECK_EQ(vtable_diffs(a.item_vtable, B195_RVA_BLOCK_ITEM_VTABLE, B195_BLOCK_ITEM_SLOTS), 1u);
    CHECK(U(a.item + 0xa0) == a.object);
    CHECK(b195_item_name_dispatch(out, (void *)c.item, nullptr) == out); CHECK(wout((uintptr_t)out) == "Ice Brick");
    b195_item_name_dispatch(out, (void *)g_items[1], nullptr); CHECK_EQ(f_item_name_orig, 1);
    CHECK(b195_get_resource_dispatch((void *)a.object, 0, 1, 0) == g_items[265]);
    CHECK(b195_get_resource_dispatch((void *)b.object, 0, 1, 0) == g_byblock_marker);
    // fire odds for B (flammability 20 -> encouragement 5, like planks 5/20)
    auto fire = [&](uintptr_t off) { auto it = g_fire.find(std::make_pair(g_fire_obj + off, b.object)); return it == g_fire.end() ? -1 : *it->second; };
    CHECK_EQ(fire(0x1e8), 5);
    CHECK_EQ(fire(0x238), 20);
    CHECK_EQ(g_fire.size(), 2u);
    CHECK_EQ(b195_items_publish(&M), 0);   // one-shot
    b195_mapped_item mi[8];
    CHECK_EQ(b195_mapped_items(&M, mi, 8), 3u);
    CHECK(mi[0].block_id == 254 && mi[0].item_id == 254 && mi[0].block == a.object && mi[0].item == a.item && !strcmp(mi[0].key, "rubymod:ruby_ore"));
    CHECK(mi[1].name_vtable == (uintptr_t)&b.item_vtable[2] && mi[1].name_dispatch == (uintptr_t)&b195_item_name_dispatch);
    CHECK(!strcmp(mi[1].display_name, "Ruby Log ?"));

    // ---- terrain ----
    std::vector<uint8_t> t16(5 * 16 * 16 * 4), t8(5 * 8 * 8 * 4);
    CHECK_EQ(b195_terrain_compose(&M, 16, 0, t16.data(), t16.size()), 1);
    CHECK_EQ(b195_terrain_compose(&M, 16, 1, t8.data(), t8.size()), 1);
    CHECK_EQ(b195_terrain_compose(&M, 16, 0, t16.data(), t16.size() - 4), 0);
    CHECK(t16[1024 * 2] == 0x30 && t16[1024 * 1] == 0x20 && t8[256 * 4] == 0x50);
    uintptr_t atlas = (uintptr_t)arena(0x100), tex = (uintptr_t)arena(0x40), fallback = (uintptr_t)arena(0xb8);
    *(uint32_t *)(atlas + 8) = 0; W(atlas + 0xe8, fallback); W(atlas + 0xf0, tex); *(uint32_t *)(tex + 0xc) = 256; *(uint32_t *)(tex + 0x10) = 640;
    for (unsigned i = 0; i < 4; ++i) {   // what the stock registrar +0x438 leaves for unknown names
        W(M.blocks[i].object + B195_F_SPRITE, fallback);
        for (unsigned f = 0; f < 6; ++f) W(M.blocks[i].object + B195_F_FACE_SPRITES + 8 * f, fallback);
    }
    g_terrain_held = 0; CHECK_EQ(b195_terrain_bind(&M, atlas, 1, 256, 640, 624, 16, 2), 0); g_terrain_held = 1;
    g_uploaded = 0; CHECK_EQ(b195_terrain_bind(&M, atlas, 1, 256, 640, 624, 16, 2), 0); g_uploaded = 1;
    CHECK_EQ(b195_terrain_bind(&M, atlas, 1, 256, 624, 624, 16, 2), 0);        // atlas not grown
    CHECK_EQ(b195_terrain_bind(&M, atlas, 1, 256, 640, 624, 16, 2), 1);
    uintptr_t s0 = U(a.object + B195_F_SPRITE);
    CHECK(s0 != fallback && U(s0) == B + B195_RVA_SPRITE_VTABLE);
    CHECK_EQ(F<int32_t>(s0 + 0x80), 32); CHECK_EQ(F<int32_t>(s0 + 0x84), 624);
    CHECK(U(b.object + B195_F_FACE_SPRITES) == U(b.object + B195_F_FACE_SPRITES + 8));          // down/up share t1
    CHECK(U(b.object + B195_F_FACE_SPRITES + 16) != U(b.object + B195_F_FACE_SPRITES));         // sides t2
    CHECK(U(b.object + B195_F_SPRITE) == fallback);                                             // single slot untouched
    CHECK(wout(s0 + 8) == "lcesdk_t0");
    CHECK_EQ(b195_terrain_bind(&M, atlas, 2, 256, 640, 624, 16, 2), 0);   // bound: detach first
    g_terrain_held = 0;
    CHECK_EQ(b195_terrain_detach(&M), 0);
    CHECK_EQ(U(a.object + B195_F_SPRITE), s0);
    g_terrain_held = 1;
    CHECK_EQ(b195_terrain_detach(&M), 1);
    CHECK(U(a.object + B195_F_SPRITE) == fallback && U(b.object + B195_F_FACE_SPRITES + 40) == fallback);
    CHECK_EQ(b195_terrain_bind(&M, atlas, 1, 256, 640, 624, 16, 2), 0);   // generation must grow
    CHECK_EQ(b195_terrain_bind(&M, atlas, 2, 256, 640, 624, 16, 2), 1);
    W(a.object + B195_F_SPRITE, 0x4242);                                    // someone else replaced it
    CHECK_EQ(b195_terrain_detach(&M), -1);
    CHECK(U(a.object + B195_F_SPRITE) == 0x4242);                           // foreign pointer preserved
}

static void test_hook_refusals() {
    // registry not constructed yet (plugin earlier than static ctor 0x150cd0)
    world(false); plan(); CHECK_EQ(configure(), 1);
    CHECK_EQ(b195_hook_install(&M), 0); CHECK_EQ(M.hook_state, B195_HOOK_NONE); CHECK(strstr(M.last_error, "retry") != nullptr);
    g_reg = (uintptr_t)arena(0xcc0); W(g_reg, B + B195_RVA_REGISTRY_VTABLE); W(B + B195_RVA_REGISTRY_GLOBAL, g_reg);
    CHECK_EQ(b195_hook_install(&M), 1);                                     // retry succeeds
    CHECK_EQ(b195_hook_disarm(&M), 1); CHECK(U(g_reg) == B + B195_RVA_REGISTRY_VTABLE); CHECK_EQ(M.hook_state, B195_HOOK_DISARMED);
    // too late: stock init already registered 365
    world(); plan(); CHECK_EQ(configure(), 1); stock_init();
    size_t used = g_used;
    CHECK_EQ(b195_hook_install(&M), 0); CHECK(U(g_reg) == B + B195_RVA_REGISTRY_VTABLE); CHECK_EQ(g_used, used);
    // foreign vptr
    world(); plan(); CHECK_EQ(configure(), 1); W(g_reg, 0x777); CHECK_EQ(b195_hook_install(&M), 0); CHECK(U(g_reg) == 0x777);
    // missed: 365 registered through a stale vptr (race) -> relay never fires; disarm restores and reports
    world(); plan(); CHECK_EQ(configure(), 1); CHECK_EQ(b195_hook_install(&M), 1);
    stock_init(false);
    CHECK_EQ(M.hook_state, B195_HOOK_ARMED); CHECK_EQ(b195_status(&M), B195_CONFIGURED);
    CHECK_EQ(b195_hook_disarm(&M), 1); CHECK_EQ(M.hook_state, B195_HOOK_MISSED); CHECK(U(g_reg) == B + B195_RVA_REGISTRY_VTABLE);
    after_stock_globals();
    CHECK_EQ(b195_items_publish(&M), 0);                                    // no blocks -> no items
    CHECK(U(g_reg + 0x150 + 8 * 254) == 0);
}

static void test_block_refusals() {
    // .text-window path: refused when the caller does not hold the phase
    world(); plan(); CHECK_EQ(configure(), 1); stock_init();
    CHECK_EQ(b195_blocks_publish(&M), 0); CHECK_EQ(b195_status(&M), B195_REFUSED);
    // stock init not finished (no 365)
    world(); plan(); CHECK_EQ(configure(), 1);
    for (uint32_t id = 0; id < 365; ++id) if (!is_free(id)) stock_register(id);
    g_blocks_held = 1; size_t used = g_used;
    CHECK_EQ(b195_blocks_publish(&M), 0); CHECK_EQ(g_used, used); CHECK_EQ(f_allocs, 0);
    // occupied id: nothing allocated, nothing registered
    world(); plan(); CHECK_EQ(configure(), 1);
    stock_init(); uintptr_t squat = (uintptr_t)arena(0x1e8); W(g_reg + 0x150 + 8 * 262, squat);
    g_blocks_held = 1; size_t n_order = g_order.size();
    CHECK_EQ(b195_blocks_publish(&M), 0); CHECK_EQ(f_allocs, 0); CHECK_EQ(g_order.size(), n_order); CHECK(U(g_reg + 0x150 + 8 * 254) == 0);
    // pin mismatch: OreBlock +0x380 not the stock name function
    world(); plan(); CHECK_EQ(configure(), 1); stock_init(); g_blocks_held = 1;
    uintptr_t *slot = (uintptr_t *)(B + B195_RVA_ORE_VTABLE + 0x380), save = *slot; *slot = B + 0x1234;
    CHECK_EQ(b195_blocks_publish(&M), 0); CHECK_EQ(f_allocs, 0); *slot = save;
    // storage blocks: any DispenserBlock / RTTI / creation-site pin mismatch refuses before allocating; plain blocks ignore them
    for (uintptr_t at : {B + 0x1239608 + 0x2c0, B + 0x1239608 + 0x510, B + 0x1242080 + 0x10, B + 0x12126c0 + 0x30}) {
        world(); plan(); g_desc[3].block_entity_slots = 9; CHECK_EQ(configure(), 1); stock_init(); g_blocks_held = 1;
        uintptr_t keep = U(at); W(at, keep + 8);
        CHECK_EQ(b195_blocks_publish(&M), 0); CHECK_EQ(f_allocs, 0); CHECK(strstr(M.last_error, "storage blocks") != nullptr);
        W(at, keep);
    }
    { world(); plan(); g_desc[3].block_entity_slots = 9; CHECK_EQ(configure(), 1); stock_init(); g_blocks_held = 1;
      uint8_t *p = (uint8_t *)(B + B195_RVA_BE_CHECK + 2), keep = *p; *p = 0x4b;
      CHECK_EQ(b195_blocks_publish(&M), 0); CHECK_EQ(f_allocs, 0); *p = keep; }
    { world(); plan(); CHECK_EQ(configure(), 1); stock_init(); g_blocks_held = 1;   // no storage block: the dispenser pins are not needed
      uintptr_t at = B + 0x1239608 + 0x2c0, keep = U(at); W(at, keep + 8);
      CHECK_EQ(b195_blocks_publish(&M), 1); W(at, keep); }
    // code pin mismatch: one byte of the OreBlock ctor
    world(); plan(); CHECK_EQ(configure(), 1); stock_init(); g_blocks_held = 1;
    g_image[B195_RVA_ORE_CTOR + 9] ^= 1; CHECK_EQ(b195_blocks_publish(&M), 0); g_image[B195_RVA_ORE_CTOR + 9] ^= 1; CHECK_EQ(f_allocs, 0);
    // key already registered under another id: refused after the plugin-owned keys, before any block exists
    world(); plan(); CHECK_EQ(configure(), 1); stock_init(); g_block_keys["rubymod:ice_brick"] = g_stock[1]; g_blocks_held = 1;
    CHECK_EQ(b195_blocks_publish(&M), 0); CHECK_EQ(f_allocs, 0); CHECK(U(g_reg + 0x150 + 8 * 254) == 0);
    // a construction check fails for the stone (OreBlock ctor) block only -> only that block is dropped (never registered),
    // the others publish (console 2026-11-19: one failing variant check used to kill every SDK block)
    world(); plan(); CHECK_EQ(configure(), 1); stock_init(); g_blocks_held = 1; f_bad_ctor = 1; n_order = g_order.size();
    CHECK_EQ(b195_blocks_publish(&M), 1); CHECK_EQ(b195_status(&M), B195_BLOCKS_DONE);
    CHECK(M.dropped >= 1 && M.dropped < M.count);
    for (uint32_t i = 0; i < M.count; ++i) {
        const b195_block &b = M.blocks[i];
        if (b.dropped) CHECK(!b.object && U(g_reg + 0x150 + 8 * b.id) == 0 && strstr(b.drop_reason, "OreBlock construction") != nullptr);
        else CHECK(b.object && U(g_reg + 0x150 + 8 * b.id) == b.object);
    }
    CHECK_EQ(g_order.size(), n_order + (M.count - M.dropped));
    CHECK(strstr(M.last_error, "dropped") != nullptr);
    after_stock_globals(); CHECK_EQ(b195_items_publish(&M), 1);
    uint32_t creative_count = 0;
    for (uint32_t i = 0; i < M.count; ++i) {
        const b195_block &b = M.blocks[i];
        if (b.dropped) CHECK(b195_creative_key(&M, 0, b.item_id) == nullptr);
        else creative_count += b.item_id != 0;
    }
    CHECK_EQ(b195_creative_entries(&M, nullptr, 0), (int)creative_count);
    // every block fails -> uncertain, nothing registered
    world(); plan();
    CHECK_EQ(configure(1), 1);   // the stone block alone
    stock_init(); g_blocks_held = 1; f_bad_ctor = 1; n_order = g_order.size();
    CHECK_EQ(b195_blocks_publish(&M), -1); CHECK_EQ(b195_status(&M), B195_UNCERTAIN); CHECK_EQ(g_order.size(), n_order);
    CHECK_EQ(M.dropped, M.count);
    CHECK_EQ(b195_items_publish(&M), 0);
    // registration postcheck fails -> partial (-1)
    world(); plan(); CHECK_EQ(configure(), 1); stock_init(); g_blocks_held = 1; f_skip_id = 1;
    CHECK_EQ(b195_blocks_publish(&M), -1); CHECK_EQ(b195_status(&M), B195_UNCERTAIN); CHECK_EQ(g_order.size(), 346u);
    // .text-window path succeeds with blocks_held
    world(); plan(); CHECK_EQ(configure(), 1); stock_init(); g_blocks_held = 1;
    CHECK_EQ(b195_blocks_publish(&M), 1); CHECK_EQ(b195_blocks_publish(&M), 0);
}

static void test_item_refusals() {
    auto blocks_done = [] { world(); plan(); CHECK_EQ(configure(), 1); CHECK_EQ(b195_hook_install(&M), 1); stock_init(); CHECK_EQ(b195_status(&M), B195_BLOCKS_DONE); after_stock_globals(); };
    blocks_done(); g_items_held = 0; int allocs = f_allocs;
    CHECK_EQ(b195_items_publish(&M), 0); CHECK_EQ(b195_status(&M), B195_UNCERTAIN); CHECK_EQ(f_allocs, allocs);   // blocks are live: never "clean"
    blocks_done(); g_items[1030] = (uintptr_t)arena(0xa0); allocs = f_allocs;
    CHECK_EQ(b195_items_publish(&M), 0); CHECK_EQ(f_allocs, allocs); CHECK(g_items[254] == 0);
    blocks_done(); g_item_keys["rubymod:ruby_ore"] = 1;
    CHECK_EQ(b195_items_publish(&M), 0); CHECK(g_items[254] == 0);
    blocks_done(); n_map_slot(B + B195_RVA_BLOCK_ITEM_MAP, &M.blocks[2].object);   // stale map entry
    CHECK_EQ(b195_items_publish(&M), 0); CHECK(g_items[254] == 0);
    blocks_done(); g_items[265] = 0;                                                  // drop item missing
    CHECK_EQ(b195_items_publish(&M), 0);
    blocks_done(); W(B + B195_RVA_FIRE_GLOBAL, g_stock[1]);                           // Blocks::FIRE is not slot 51
    CHECK_EQ(b195_items_publish(&M), 0);
    blocks_done(); uintptr_t *v = (uintptr_t *)(B + B195_RVA_BLOCK_ITEM_VTABLE + 0x108), save = *v; *v = 0;
    CHECK_EQ(b195_items_publish(&M), 0); *v = save;
}

static int g_protects, g_protect_fail;
static int t_protect(void *, uintptr_t, size_t, int) { ++g_protects; return g_protect_fail; }
static int t_cas8(void *, uintptr_t a, uint64_t e, uint64_t d) { return __atomic_compare_exchange_n((uint64_t *)a, &e, d, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE) ? 1 : 0; }
static int t_read8(void *, uintptr_t a, uint64_t *o) { memcpy(o, (void *)a, 8); return 1; }
static void test_text_oneshot() {
    b195_text_patch p{};
    const uint8_t *w = g_image + B195_RVA_LAST_STOCK_CALL;   // real bytes at 0xb74d8
    uintptr_t region = B + 0xb74d8 + 0x10000000;
    CHECK_EQ(b195_text_plan(B, w, region, 0x4000, &p), 1);
    CHECK(p.word == B + 0xb74e0 && p.original_target == B + 0xcdab00);
    uint64_t real; memcpy(&real, g_image + 0xb74e0, 8); CHECK(p.before == real);
    CHECK((p.after & ~UINT64_C(0xffffff)) == (p.before & ~UINT64_C(0xffffff)));     // only rel32 bytes 1..3 change
    int32_t rel = (int32_t)(0x1d | (uint32_t)((p.after & 0xffffff) << 8));
    CHECK((uintptr_t)(B + 0xb74e3 + (intptr_t)rel) == p.relay);
    uint8_t bad[16]; memcpy(bad, w, 16); bad[3] ^= 1; CHECK_EQ(b195_text_plan(B, bad, region, 0x4000, &p), 0);
    alignas(8) static uint64_t page[2];
    b195_text_patch q = p; q.word = (uintptr_t)&page[0];
    b195_text_ops ops{nullptr, t_protect, t_cas8, t_read8};
    page[0] = q.after; CHECK_EQ(b195_text_restore(&q, &ops), 1); CHECK(page[0] == q.before); CHECK_EQ(g_protects, 2);
    CHECK_EQ(b195_text_restore(&q, &ops), 1); CHECK_EQ(g_protects, 2);                 // already stock: no mprotect
    page[0] = 0x1122334455667788ull; CHECK_EQ(b195_text_restore(&q, &ops), 0); CHECK(page[0] == 0x1122334455667788ull);
    page[0] = q.after; g_protect_fail = -1; CHECK_EQ(b195_text_restore(&q, &ops), -1); g_protect_fail = 0;
}


static void test_creative_entries() {
    world(); plan();
    g_desc[2].variant_count = 16;
    const lce_plan_block first = g_plan[0];
    g_plan[0] = g_plan[2];
    g_plan[2] = first;
    CHECK_EQ(b195_creative_entries(nullptr, nullptr, 0), 0);
    CHECK_EQ(configure(), 1);
    CHECK_EQ(b195_creative_entries(&M, nullptr, 0), 0);
    CHECK_EQ(b195_creative_entries(&M, nullptr, 1), -1);
    CHECK_EQ(b195_hook_install(&M), 1);
    stock_init();
    CHECK_EQ(b195_creative_entries(&M, nullptr, 0), 0);
    CHECK(b195_creative_key(&M, 0, 1048) == nullptr);
    after_stock_globals();
    CHECK_EQ(b195_items_publish(&M), 1);

    const b195_block &variant = M.blocks[0];
    const b195_block &plain = M.blocks[1];
    CHECK_EQ(b195_creative_entries(&M, nullptr, 0), 18);
    lce_sdk_creative_block_entry rows[19]{};
    rows[0].aux = 99;
    rows[18].aux = 99;
    CHECK_EQ(b195_creative_entries(&M, rows, 17), -1);
    CHECK_EQ(rows[0].aux, 99u);
    CHECK_EQ(b195_creative_entries(&M, rows, 18), 18);
    for (uint32_t i = 0; i < 16; ++i) {
        CHECK(rows[i].item == variant.item && rows[i].item_id == 1048 && rows[i].aux == i && rows[i].key == variant.desc->key);
    }
    CHECK(rows[16].item == plain.item && rows[16].item_id == 1030 && rows[16].aux == 0 && rows[16].key == plain.desc->key);
    CHECK(rows[17].item == M.blocks[2].item && rows[17].item_id == 254 && rows[17].aux == 0);
    CHECK_EQ(rows[18].aux, 99u);
    CHECK(b195_creative_key(&M, variant.item, 1048) == variant.desc->key);
    CHECK(b195_creative_key(&M, variant.item, 0) == variant.desc->key);
    CHECK(b195_creative_key(&M, 0, 1048) == variant.desc->key);
    CHECK(b195_creative_key(&M, variant.item, 1030) == nullptr);
    CHECK(b195_creative_key(&M, 0, 1049) == nullptr);   // registered no-item block
    CHECK(b195_creative_key(&M, 0, 0) == nullptr);

    rows[0].aux = 99;
    g_items[1048] = plain.item;
    CHECK_EQ(b195_creative_entries(&M, rows, 18), -1);
    CHECK(b195_creative_key(&M, variant.item, 1048) == nullptr);
    g_items[1048] = variant.item;
    const uintptr_t vtable = U(variant.item);
    W(variant.item, U(plain.item));
    CHECK_EQ(b195_creative_entries(&M, rows, 18), -1);
    W(variant.item, vtable);
    const char *key = g_desc[1].key;
    g_desc[1].key = variant.desc->key;
    CHECK_EQ(b195_creative_entries(&M, rows, 18), -1);
    g_desc[1].key = key;
    M.blocks[1].item_id = variant.item_id;
    CHECK_EQ(b195_creative_entries(&M, rows, 18), -1);
    CHECK(b195_creative_key(&M, variant.item, 1048) == nullptr);
    M.blocks[1].item_id = 1030;
    M.blocks[0].variants = 17;
    CHECK_EQ(b195_creative_entries(&M, rows, 18), -1);
    M.blocks[0].variants = 16;
    M.state = B195_UNCERTAIN;
    CHECK_EQ(b195_creative_entries(&M, rows, 18), -1);
    CHECK(b195_creative_key(&M, variant.item, 1048) == nullptr);
    CHECK_EQ(rows[0].aux, 99u);
}

// The storage-block RTTI trick under the Itanium C++ ABI (__dynamic_cast is libc++abi on the PS4): a module-made
// __si_class_type_info {si vptr, unique name, &BaseEntityBlock} on a Block-sized object with an EntityBlock vptr at +0x1e8 must make
// dynamic_cast<EntityBlock*>(Block*) return object+0x1e8 (what 0x410823 / 0x40b6e4 rely on), and a cast to an unrelated class fail.
#include <typeinfo>
namespace rt {
struct Block { virtual ~Block() {} char pad[0x1e0]; };
struct EntityBlock { virtual ~EntityBlock() {} virtual int newBlockEntity() { return 1; } };
struct BaseEntityBlock : Block, EntityBlock {};
struct Si : BaseEntityBlock {};                       // only used to borrow the host's __si_class_type_info vptr
struct Other : Block {};
}
static void test_storage_rtti() {
    static_assert(sizeof(rt::Block) == 0x1e8, "Block size");
    rt::BaseEntityBlock probe;
    CHECK((uintptr_t)static_cast<rt::EntityBlock *>(&probe) - (uintptr_t)&probe == 0x1e8);   // same layout as the game's BaseEntityBlock
    static rt::Si si; (void)si;
    const uintptr_t si_vptr = *(const uintptr_t *)&typeid(rt::Si);
    static const char name[] = "N6lcesdk16ModStorageBlockE";
    static uintptr_t ti[3] = {si_vptr, (uintptr_t)name, (uintptr_t)&typeid(rt::BaseEntityBlock)};
    // primary / secondary vtables copied from a real BaseEntityBlock with the RTTI slot replaced (as b.vtable / b.entity_vtable)
    const uintptr_t *pv = *(uintptr_t *const *)&probe, *sv = *(uintptr_t *const *)static_cast<rt::EntityBlock *>(&probe);
    static uintptr_t prim[2 + 4], sec[2 + 4];
    prim[0] = 0; prim[1] = (uintptr_t)ti; for (int i = 0; i < 2; ++i) prim[2 + i] = pv[i];
    sec[0] = (uintptr_t)-0x1e8; sec[1] = (uintptr_t)ti; for (int i = 0; i < 3; ++i) sec[2 + i] = sv[i];
    alignas(16) static uint8_t obj[0x1f0];
    *(uintptr_t *)obj = (uintptr_t)&prim[2]; *(uintptr_t *)(obj + 0x1e8) = (uintptr_t)&sec[2];
    rt::Block *b = reinterpret_cast<rt::Block *>(obj);
    rt::EntityBlock *e = dynamic_cast<rt::EntityBlock *>(b);
    CHECK((uintptr_t)e == (uintptr_t)obj + 0x1e8);
    CHECK(e && e->newBlockEntity() == 1);                          // the secondary vtable's +0x10 slot is reached
    CHECK(dynamic_cast<rt::Other *>(b) == nullptr);
    CHECK(dynamic_cast<rt::BaseEntityBlock *>(b) == reinterpret_cast<rt::BaseEntityBlock *>(obj));
    CHECK(strcmp(typeid(*b).name(), name) == 0);
}
#ifdef LCE_HAVE_BLOCK_BOXES_1_11
// API 1.11 multi-box: a 4-facing stair (one box set per variant), a 3-box fence post (one set), and the refusals.
static void test_boxes() {
    lce_block_desc x; char e[200];
    memset(&x, 0, sizeof x); x.struct_size = sizeof x; x.key = "t:s"; x.name = "S"; x.texture[0] = "t:t"; x.material = LCE_MAT_STONE;
    static const uint8_t one[6] = {0, 0, 0, 16, 8, 16}, bad[6] = {0, 8, 0, 16, 8, 16};
    x.shape = LCE_SHAPE_BOXES; x.box_count = 1; x.box_sets = 1; x.boxes = one; CHECK_EQ(b195_check_desc(&x, e, sizeof e), 1);
    x.boxes = bad; CHECK_EQ(b195_check_desc(&x, e, sizeof e), 0);
    x.boxes = one; x.box_count = 9; CHECK_EQ(b195_check_desc(&x, e, sizeof e), 0);
    x.box_count = 1; x.box_sets = 2; CHECK_EQ(b195_check_desc(&x, e, sizeof e), 0);          // two sets without two variants
    x.box_sets = 1; x.boxes = nullptr; CHECK_EQ(b195_check_desc(&x, e, sizeof e), 0);
    x.shape = LCE_SHAPE_CUBE; x.boxes = one; CHECK_EQ(b195_check_desc(&x, e, sizeof e), 0);   // boxes without the shape
    world(); plan();
    // stairs: variant v = bottom half + the top step turned v x 90 degrees (north, east, south, west)
    static const uint8_t stairs[4][2][6] = {{{0, 0, 0, 16, 8, 16}, {0, 8, 8, 16, 16, 16}}, {{0, 0, 0, 16, 8, 16}, {0, 8, 0, 8, 16, 16}},
                                            {{0, 0, 0, 16, 8, 16}, {0, 8, 0, 16, 16, 8}}, {{0, 0, 0, 16, 8, 16}, {8, 8, 0, 16, 16, 16}}};
    static const uint8_t post[3][6] = {{6, 0, 6, 10, 16, 10}, {0, 12, 7, 16, 15, 9}, {0, 6, 7, 16, 9, 9}};
    g_desc[0].drop = nullptr; g_desc[0].drop_min = g_desc[0].drop_max = 0; g_plan[0].drop_item = (int32_t)g_plan[0].item_id;
    g_desc[0].variant_count = 4; g_desc[0].shape = LCE_SHAPE_BOXES; g_desc[0].box_count = 2; g_desc[0].box_sets = 4; g_desc[0].boxes = &stairs[0][0][0];
    g_desc[1].shape = LCE_SHAPE_BOXES; g_desc[1].box_count = 3; g_desc[1].box_sets = 1; g_desc[1].boxes = &post[0][0];
    CHECK_EQ(configure(), 1);
    b195_block &a = M.blocks[0], &b = M.blocks[1];
    CHECK_EQ(a.box_count, 2u); CHECK_EQ(a.box_sets, 4u); CHECK_EQ(b.box_sets, 1u);
    CHECK(a.box[0] == 0 && a.box[4] == 16 && a.box[5] == 16);                          // set 0 union: the full cube outline
    CHECK_EQ(a.solid_mask, 1u);                                                       // only the bottom is solid in every facing
    CHECK_EQ(b.solid_mask, 0u);
    CHECK_EQ(b195_hook_install(&M), 1);
    stock_init();
    CHECK_EQ(M.hook_publish_result, 1);
    for (const b195_block *k : {&a, &b}) {
        CHECK(k->vtable[2 + 0xe8 / 8] == B + B195_RVA_STAIR_RENDER_SHAPE && k->vtable[2 + 0x190 / 8] == (uintptr_t)&b195_box_list_dispatch &&
              k->vtable[2 + 0x1b0 / 8] == B + B195_RVA_STAIR_COLLISION && k->vtable[2 + 0x178 / 8] == (uintptr_t)&b195_shape_dispatch);
    }
    for (unsigned v = 0; v < 4; ++v) {   // the list the game reads: {AABB **data; uint32 count}, per state
        const uintptr_t st = state_for(a.object, v);
        const uintptr_t l = b195_box_list_dispatch((void *)a.object, st, 0, 0);
        CHECK(l == (uintptr_t)&a.box_list[v]);
        const uintptr_t *data = (const uintptr_t *)U(l);
        CHECK_EQ(*(const uint32_t *)(l + 8), 2u);
        for (unsigned k = 0; k < 2; ++k) {
            const double *bb = (const double *)data[k];
            for (int c = 0; c < 6; ++c) CHECK(bb[c] == stairs[v][k][c] / 16.0);
        }
        const double *un = (const double *)b195_shape_dispatch((void *)a.object, st, 0, 0);
        CHECK(un && un[1] == 0 && un[4] == 1.0);
    }
    const uintptr_t lp = b195_box_list_dispatch((void *)b.object, 0, 0, 0);
    CHECK_EQ(*(const uint32_t *)(lp + 8), 3u);
    const double *ub = (const double *)b195_shape_dispatch((void *)b.object, 0, 0, 0);
    CHECK(ub && ub[0] == 0 && ub[2] == 6 / 16.0 && ub[3] == 1.0 && ub[5] == 10 / 16.0);   // post + rails union
    CHECK_EQ(*(const uint32_t *)(b195_box_list_dispatch((void *)g_stock[1], 0, 0, 0) + 8), 0u);   // not ours: an empty list
    uint8_t dir[6][16] = {};
    for (int f = 0; f < 6; ++f) *(int32_t *)(dir[f] + 8) = f;
    CHECK_EQ(b195_face_shape_dispatch((void *)a.object, 0, 0, 0, (uintptr_t)dir[0]), 0);
    CHECK_EQ(b195_face_shape_dispatch((void *)a.object, 0, 0, 0, (uintptr_t)dir[1]), 8);
    b195_test_reset();
    // a foreign StairBlock +0x1b0 refuses before any construction
    world(); plan();
    g_desc[1].shape = LCE_SHAPE_BOXES; g_desc[1].box_count = 3; g_desc[1].box_sets = 1; g_desc[1].boxes = &post[0][0];
    CHECK_EQ(configure(), 1);
    uintptr_t keep = U(B + B195_RVA_STAIR_VTABLE + 0x1b0); W(B + B195_RVA_STAIR_VTABLE + 0x1b0, B + 0x10);
    CHECK_EQ(b195_hook_install(&M), 1);
    int allocs = f_allocs;
    stock_init();
    CHECK_EQ(M.hook_publish_result, 0); CHECK_EQ(f_allocs, allocs); CHECK(strstr(M.last_error, "multi-box") != nullptr);
    W(B + B195_RVA_STAIR_VTABLE + 0x1b0, keep);
    b195_test_reset();
}
#endif
// API 1.7: a 3-variant "marble" (stone, variant textures + names), a bottom slab box, a transparent pane-like box and a
// no-collide plant through the full data hook, items and terrain phases.
static void test_states_shapes() {
    world(); plan();
    static const char *const vnames[3] = {nullptr, "Marble (Black)", nullptr};
    static const char *const vtex[18] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                                         "rubymod:t", "rubymod:t", nullptr, nullptr, nullptr, nullptr,   // v1: top/bottom -> tile of g_tex[0]
                                         "rubymod:icon2"};                                               // v2: missing texture key (fixed below)
    g_desc[0].drop = nullptr; g_desc[0].drop_min = g_desc[0].drop_max = 0; g_plan[0].drop_item = (int32_t)g_plan[0].item_id;
    g_desc[0].variant_count = 3; g_desc[0].variant_names = vnames; g_desc[0].variant_textures = vtex;
    CHECK_EQ(configure(), 0); CHECK(strstr(M.last_error, "variant texture missing") != nullptr);
    b195_test_reset();
    static const char *const vtex2[18] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                                          "rubymod:t4", "rubymod:t4", nullptr, nullptr, nullptr, nullptr, "rubymod:t5"};
    g_tex[5].key = "rubymod:t5"; g_tex[4].key = "rubymod:t4";
    g_desc[0].variant_textures = vtex2;
    g_desc[1].shape = LCE_SHAPE_BOX; g_desc[1].box[3] = 16; g_desc[1].box[4] = 8; g_desc[1].box[5] = 16;     // bottom slab
    g_desc[2].shape = LCE_SHAPE_BOX; g_desc[2].box[3] = 16; g_desc[2].box[4] = 16; g_desc[2].box[5] = 8;
    g_desc[2].flags = LCE_BLOCK_FLAG_TRANSPARENT; g_desc[2].material = LCE_MAT_GLASS;
    g_desc[3].flags |= LCE_BLOCK_FLAG_NO_COLLIDE; g_desc[3].material = LCE_MAT_PLANT;
    CHECK_EQ(configure(), 1);
    b195_block &a = M.blocks[0], &b = M.blocks[1], &c = M.blocks[2], &d = M.blocks[3];
    CHECK_EQ(a.variants, 3u); CHECK_EQ(b.variants, 0u);
    CHECK(a.vtile[1][0] == a.vtile[1][1] && a.vtile[1][0] != a.tile[0] && a.vtile[1][2] == a.vtile[1][0]);   // NULL -> the variant's face 0
    CHECK(a.vtile[2][3] == a.vtile[2][0] && M.tiles[a.vtile[2][0]].plan_texture == 5);
    CHECK_EQ(a.vname_len[1], 14u); CHECK_EQ(a.vname_len[2], 0u);
    f_statedef_calls = 0;
    CHECK_EQ(b195_hook_install(&M), 1);
    stock_init();
    CHECK_EQ(M.hook_publish_result, 1);
    CHECK_EQ(b195_status(&M), B195_BLOCKS_DONE);
    CHECK_EQ(f_statedef_calls, 1);                                         // the ColoredBlock state definition ran once, for marble
    // variants: private vtable installed before the state tail; ColoredBlock slots; 16 states, default data 0
    CHECK(a.vtable[2 + 0x478 / 8] == B + B195_RVA_COLORED_STATEDEF && a.vtable[2 + 0x50 / 8] == B + B195_RVA_COLORED_TO_DATA &&
          a.vtable[2 + 0x268 / 8] == B + B195_RVA_COLORED_DROP_AUX && a.vtable[2 + 0x48 / 8] == (uintptr_t)&b195_state_from_data_dispatch &&
          a.vtable[2 + 0x458 / 8] == (uintptr_t)&b195_face_sprite_dispatch);
    CHECK_EQ(vtable_diffs(a.vtable, B195_RVA_ORE_VTABLE, B195_ORE_SLOTS), 6u);   // name + 5 state slots
    CHECK(U(a.object + B195_F_DEFSTATE) == state_for(a.object, 0));
    CHECK(g_states[a.object][0] != state_for(a.object, 0));                // 0x924c0 left states[0] (not WHITE): replaced like 0x1a96d0
    CHECK(b195_state_from_data_dispatch((void *)a.object, 2) == state_for(a.object, 2));
    CHECK(b195_state_from_data_dispatch((void *)a.object, 3) == state_for(a.object, 0));    // past the variants -> 0
    CHECK(b195_state_from_data_dispatch((void *)a.object, -1) == state_for(a.object, 0));
    CHECK_EQ(*(uint8_t *)(a.object + 0x12c), 1);                                           // still an opaque full cube
    // shapes
    CHECK(b.vtable[2 + 0x178 / 8] == (uintptr_t)&b195_shape_dispatch && b.vtable[2 + 0xd0 / 8] == B + B195_RVA_RET_FALSE &&
          b.vtable[2 + 0x1d0 / 8] == B + B195_RVA_RET_FALSE && b.vtable[2 + 0x1a0 / 8] == (uintptr_t)&b195_face_shape_dispatch);
    CHECK_EQ(vtable_diffs(b.vtable, B195_RVA_ORE_VTABLE, B195_ORE_SLOTS), 5u);
    CHECK_EQ(*(uint8_t *)(b.object + 0x12c), 0);
    const double *bb = (const double *)b195_shape_dispatch((void *)b.object, 0, 0, 0);
    CHECK(bb && bb[0] == 0 && bb[1] == 0 && bb[2] == 0 && bb[3] == 1.0 && bb[4] == 0.5 && bb[5] == 1.0);
    CHECK(b195_shape_dispatch((void *)g_stock[1], 0, 0, 0) == U(B + B195_RVA_FULL_SHAPE));
    uint8_t dir[6][16] = {};
    for (int f = 0; f < 6; ++f) *(int32_t *)(dir[f] + 8) = f;
    auto shape = [&](const b195_block &x, int f) { return b195_face_shape_dispatch((void *)x.object, 0, 0, 0, (uintptr_t)dir[f]); };
    CHECK_EQ(shape(b, 0), 0); CHECK_EQ(shape(b, 1), 8); CHECK_EQ(shape(b, 2), 8); CHECK_EQ(shape(b, 5), 8);   // slab: solid bottom only
    CHECK_EQ(shape(c, 0), 8); CHECK_EQ(shape(c, 2), 0); CHECK_EQ(shape(c, 3), 8); CHECK_EQ(shape(c, 4), 8);   // half depth: north only
    CHECK_EQ(shape(d, 0), 8);
    for (auto &g : k_glass_slots_test) if (g[0] != 0xd0 && g[0] != 0x1d0) CHECK(c.vtable[2 + g[0] / 8] == B + g[1]);
    CHECK(c.vtable[2 + 0xd0 / 8] == B + B195_RVA_RET_FALSE);                              // the box wins over glass +0xd0
    CHECK(d.vtable[2 + 0x1c8 / 8] == B + B195_RVA_NO_COLLISION && d.vtable[2 + 0x178 / 8] == B + 0x92b40u);
    CHECK_EQ(*(uint8_t *)(d.object + 0x12c), 0);
    // items: marble is stacked by data and places its aux
    after_stock_globals();
    CHECK_EQ(b195_items_publish(&M), 1);
    CHECK(a.item_vtable[2 + 0x58 / 8] == B + B195_RVA_MULTI_LEVEL_DATA && b.item_vtable[2 + 0x58 / 8] == B + 0x3aeea0u);
    CHECK_EQ(*(uint8_t *)(a.item + B195_F_ITEM_STACKED), 1); CHECK_EQ(*(uint8_t *)(b.item + B195_F_ITEM_STACKED), 0);
    alignas(16) uint8_t out[0x28] = {}, stack[0xa8] = {};
    const uintptr_t shared_stack[2] = {(uintptr_t)stack, (uintptr_t)arena(0x18)};
    const uintptr_t empty_shared_stack[2] = {};
    auto name = (void *(*)(void *, void *, const void *))U(U(a.item) + 0x108);
    auto check_name = [&](int32_t aux, const char *expected) {
        memcpy(stack + B195_F_STACK_AUX, &aux, sizeof aux);
        CHECK(name(out, (void *)a.item, shared_stack) == out);
        CHECK(wout((uintptr_t)out) == expected);
    };
    check_name(0, "Ruby Ore");
    check_name(1, "Marble (Black)");
    check_name(2, "Ruby Ore");   // aux 2 has a texture and no name override
    check_name(-1, "Ruby Ore");
    check_name(9, "Ruby Ore");
    CHECK(name(out, (void *)a.item, empty_shared_stack) == out && wout((uintptr_t)out) == "Ruby Ore");
    CHECK(name(out, (void *)a.item, nullptr) == out && wout((uintptr_t)out) == "Ruby Ore");
    // terrain: variant sprites come from the module's tiles of the bound generation; variant 0 and unbound -> stock getter
    f_face_orig = 0;
    CHECK(b195_face_sprite_dispatch((void *)a.object, (uintptr_t)dir[1], state_for(a.object, 1)) == g_face_marker);   // not bound yet
    uintptr_t atlas = (uintptr_t)arena(0x100), tex = (uintptr_t)arena(0x40), fallback = (uintptr_t)arena(0xb8);
    *(uint32_t *)(atlas + 8) = 0; W(atlas + 0xe8, fallback); W(atlas + 0xf0, tex); *(uint32_t *)(tex + 0xc) = 256; *(uint32_t *)(tex + 0x10) = 640;
    for (unsigned i = 0; i < 4; ++i) {
        W(M.blocks[i].object + B195_F_SPRITE, fallback);
        for (unsigned f = 0; f < 6; ++f) W(M.blocks[i].object + B195_F_FACE_SPRITES + 8 * f, fallback);
    }
    CHECK_EQ(b195_terrain_bind(&M, atlas, 1, 256, 640, 624, 16, 2), 1);
    CHECK(b195_face_sprite_dispatch((void *)a.object, (uintptr_t)dir[1], state_for(a.object, 1)) == M.sprites[0][a.vtile[1][1]]);
    CHECK(b195_face_sprite_dispatch((void *)a.object, (uintptr_t)dir[4], state_for(a.object, 2)) == M.sprites[0][a.vtile[2][4]]);
    CHECK(b195_face_sprite_dispatch((void *)a.object, (uintptr_t)dir[1], state_for(a.object, 0)) == g_face_marker);
    CHECK(b195_face_sprite_dispatch((void *)a.object, (uintptr_t)dir[1], state_for(a.object, 5)) == g_face_marker);   // past variants
    CHECK_EQ(f_face_orig, 3);
    CHECK_EQ(b195_terrain_detach(&M), 1);
    b195_test_reset();
    g_tex[5].key = g_tex[4].key = "rubymod:t";
    // pin refusals: a foreign ColoredBlock slot or GlassBlock slot refuses before any construction
    struct { uint32_t at; int which; } bad[] = {{B195_RVA_COLORED_VTABLE + 0x478, 0}, {B195_RVA_GLASS_VTABLE + 0x188, 1}, {B195_RVA_FLOWER_VTABLE + 0x1c8, 1}};
    for (auto &x : bad) {
        world(); plan();
        if (x.which == 0) g_desc[0].variant_count = 2; else { g_desc[0].flags = LCE_BLOCK_FLAG_TRANSPARENT; }
        CHECK_EQ(configure(), 1);
        uintptr_t keep = U(B + x.at); W(B + x.at, B + 0x10);
        CHECK_EQ(b195_hook_install(&M), 1);
        int allocs = f_allocs;
        stock_init();
        CHECK_EQ(M.hook_publish_result, 0); CHECK_EQ(b195_status(&M), B195_REFUSED); CHECK_EQ(f_allocs, allocs);
        W(B + x.at, keep);
        b195_test_reset();
    }
    // the ColoredBlock default-state pins (0x7a040 store, 0x1a9779 call) refuse a variant block before any construction
    for (uint32_t at : {0x7a043u, 0x1a977au}) {
        world(); plan(); g_desc[0].variant_count = 2;
        CHECK_EQ(configure(), 1);
        g_image[at] ^= 1;
        CHECK_EQ(b195_hook_install(&M), 1);
        int allocs = f_allocs;
        stock_init();
        CHECK_EQ(M.hook_publish_result, 0); CHECK_EQ(b195_status(&M), B195_REFUSED); CHECK_EQ(f_allocs, allocs);
        g_image[at] ^= 1;
        b195_test_reset();
    }
}

// 27-slot storage: the mod block is a ChestBlock (real vtable/RTTI/EntityBlock table) with Block's cube render slots.
static void test_chest_storage() {
    world(); plan();
    g_desc[1].block_entity_slots = 27;   // the wooden "Ruby Log"
    CHECK_EQ(configure(), 1);
    f_chest_ctor = 0;
    CHECK_EQ(b195_hook_install(&M), 1);
    stock_init();
    CHECK_EQ(M.hook_publish_result, 1);
    CHECK_EQ(f_chest_ctor, 1);
    b195_block &b = M.blocks[1];
    CHECK_EQ(b.slots, 27u);
    CHECK(U(b.object) == (uintptr_t)&b.vtable[2]);
    CHECK(b.vtable[1] == B + B195_RVA_CHEST_RTTI && b.vtable[0] == 0);
    CHECK_EQ(vtable_diffs(b.vtable, B195_RVA_CHEST_VTABLE, B195_CHEST_VT_SLOTS), 8u);    // 7 cube slots + name
    CHECK(b.vtable[2 + 0x2c0 / 8] == B + 0x174ba0u && b.vtable[2 + 0x228 / 8] == B + 0x174a00u && b.vtable[2 + 0x4d8 / 8] == B + 0x1755f0u);
    CHECK(b.vtable[2 + 0xe8 / 8] == B + 0x929d0u && b.vtable[2 + 0x438 / 8] == B + 0x94ea0u && b.vtable[2 + 0x1d0 / 8] == B + 0x93680u);
    CHECK(U(b.object + B195_F_ENTITY_VPTR) == (uintptr_t)&b.entity_vtable[2]);
    CHECK(b.entity_vtable[0] == (uintptr_t)-0x1e8 && b.entity_vtable[1] == B + B195_RVA_CHEST_RTTI && b.entity_vtable[2] == B + 0x191fd0u &&
          b.entity_vtable[3] == B + 0x191fe0u && b.entity_vtable[4] == (uintptr_t)&b195_new_block_entity_dispatch);
    {   // newBlockEntity: stock 0x175800 (fake), title, then the private ChestBlockEntity vtable (only +0x70 differs)
        typedef void *(*nbe_fn)(void *, void *, void *, int);
        uintptr_t sp[2] = {0, 0};
        f_new_chest_be = f_set_name = 0;
        nbe_fn fn = (nbe_fn)b.entity_vtable[4];
        CHECK(fn((void *)sp, (void *)(b.object + B195_F_ENTITY_VPTR), (void *)0x1e7e1, 0) == (void *)sp);
        CHECK_EQ(f_new_chest_be, 1); CHECK_EQ(f_set_name, 1); CHECK(g_set_name == narrow((uintptr_t)b.title_text) && !g_set_name.empty());
        CHECK(U(sp[0]) == (uintptr_t)&b.be_vtable[B195_CHEST_BE_PREFIX]);
        unsigned diffs = 0;
        for (unsigned i = 0; i < B195_CHEST_BE_PREFIX + B195_CHEST_BE_SLOTS; ++i)
            diffs += b.be_vtable[i] != U(B + B195_RVA_CHEST_BE_VTABLE - 8 * B195_CHEST_BE_PREFIX + 8 * i);
        CHECK_EQ(diffs, 1u);
        CHECK(b.be_vtable[B195_CHEST_BE_PREFIX + 0x70 / 8] == (uintptr_t)&b195_be_view_distance);
        CHECK(b.be_vtable[B195_CHEST_BE_PREFIX] == U(B + B195_RVA_CHEST_BE_VTABLE));   // getType (renderer key, save id) unchanged
        CHECK(b195_be_view_distance((void *)sp[0]) < 0.0);
        CHECK(b.be_vtable[3] == B + B195_RVA_CHEST_BE_RTTI);
    }
    CHECK_EQ(F<int32_t>(b.object + B195_F_CHEST_TYPE), 0);
    CHECK_EQ(*(uint8_t *)(b.object + 0x12c), 1); CHECK_EQ(F<int32_t>(b.object + 0x130), 0xff);
    CHECK_EQ(F<int32_t>(b.object + 0x138), 7);                                          // light still applied
    CHECK(g_texname[b.object] == "lcesdk_b262");
    after_stock_globals();
    CHECK_EQ(b195_items_publish(&M), 1);
    CHECK(b.item && U(b.item + 0xa0) == b.object);
    b195_test_reset();
    // pin refusal: a foreign ChestBlock use slot
    world(); plan();
    g_desc[1].block_entity_slots = 27;
    CHECK_EQ(configure(), 1);
    uintptr_t keep = U(B + B195_RVA_CHEST_VTABLE + 0x2c0); W(B + B195_RVA_CHEST_VTABLE + 0x2c0, B + 0x10);
    CHECK_EQ(b195_hook_install(&M), 1);
    f_chest_ctor = 0;
    stock_init();
    CHECK_EQ(M.hook_publish_result, 0); CHECK_EQ(f_chest_ctor, 0);
    W(B + B195_RVA_CHEST_VTABLE + 0x2c0, keep);
    b195_test_reset();
    // pin refusal: a foreign ChestBlockEntity +0x70 (the render cull would not hold)
    world(); plan();
    g_desc[1].block_entity_slots = 27;
    CHECK_EQ(configure(), 1);
    keep = U(B + B195_RVA_CHEST_BE_VTABLE + 0x70); W(B + B195_RVA_CHEST_BE_VTABLE + 0x70, B + 0x10);
    CHECK_EQ(b195_hook_install(&M), 1);
    f_chest_ctor = 0;
    stock_init();
    CHECK_EQ(M.hook_publish_result, 0); CHECK_EQ(f_chest_ctor, 0);
    W(B + B195_RVA_CHEST_BE_VTABLE + 0x70, keep);
    b195_test_reset();
}

// W1b (console 2026-11-19, mixed Lucky/Steel + 4 SDK mods: "blocks_hook=0 waiting=1", "retry=0", then
// "phase=blocks rc=0 registry not constructed yet": GoldHEN loads the plugin before the eboot's static
// constructors, so the registry object does not exist at either SDK call site and no SDK block was published).
// With patch_ro8 the hook patches the register slot of the registry class vtable instead.
static int g_ro8_rc = 1, g_ro8_calls;
static int f_ro8(void *, uintptr_t at, uint64_t e, uint64_t d) {
    ++g_ro8_calls;
    if (g_ro8_rc != 1) return g_ro8_rc;
    if (U(at) != e) return 0;
    W(at, d); return 1;
}
static void test_hook_relro_slot() {
    const uintptr_t slot = B + B195_RVA_REGISTRY_VTABLE + 8 * B195_REGISTRY_REGISTER_SLOT;
    CHECK(U(slot) == B + B195_RVA_REGISTER);
    // the registry does not exist yet: armed through the class vtable slot
    world(false); plan(); CHECK_EQ(configure(), 1);
    b195_set_patch_ro8(&M, f_ro8, nullptr);
    g_ro8_calls = 0;
    CHECK_EQ(b195_hook_install(&M), 1);
    CHECK_EQ(M.hook_state, B195_HOOK_ARMED); CHECK_EQ(M.hook_mode, (uint32_t)B195_HOOK_MODE_SLOT); CHECK_EQ(g_ro8_calls, 1);
    CHECK(U(slot) == (uintptr_t)&b195_register_relay);
    CHECK_EQ(b195_hook_install(&M), 0);                                     // one-shot
    // the static ctor 0x150cd0 builds the registry (stock vptr); an unrelated object of the class registering 365 does not fire
    g_reg = (uintptr_t)arena(0xcc0); W(g_reg, B + B195_RVA_REGISTRY_VTABLE);
    uintptr_t other = (uintptr_t)arena(0xcc0); W(other, B + B195_RVA_REGISTRY_VTABLE);
    { uintptr_t keep = g_reg; g_reg = other; stock_register(365); g_reg = keep; }
    CHECK_EQ(M.hook_state, B195_HOOK_ARMED);
    W(B + B195_RVA_REGISTRY_GLOBAL, g_reg);
    stock_init();                                                            // the game's Block init through the patched slot
    CHECK_EQ(M.hook_state, B195_HOOK_FIRED); CHECK_EQ(M.hook_publish_result, 1);
    CHECK(U(slot) == B + B195_RVA_REGISTER);                                // slot restored before the publish
    CHECK(U(g_reg) == B + B195_RVA_REGISTRY_VTABLE);                        // object vptr never touched
    CHECK_EQ(b195_status(&M), B195_BLOCKS_DONE);
    CHECK_EQ(g_order.size(), 350u);                                          // 1 (other) + 345 stock + 4 SDK
    CHECK_EQ(g_order[349], 281u);
    CHECK(U(g_reg + 0x150 + 8 * 254) != 0);
    after_stock_globals();
    CHECK_EQ(b195_items_publish(&M), 1);
    // refusals: slot already foreign, RELRO CAS mismatch, protection not restored
    world(false); plan(); CHECK_EQ(configure(), 1);
    CHECK_EQ(b195_hook_install(&M), 0); CHECK(strstr(M.last_error, "retry") != nullptr);   // no patch_ro8: old behaviour
    world(false); plan(); CHECK_EQ(configure(), 1); b195_set_patch_ro8(&M, f_ro8, nullptr);
    W(slot, 0x5555);
    CHECK_EQ(b195_hook_install(&M), 0); CHECK_EQ(M.hook_state, B195_HOOK_NONE); CHECK(U(slot) == 0x5555);   // slot pin
    W(slot, B + B195_RVA_REGISTER);
    g_ro8_rc = 0; CHECK_EQ(b195_hook_install(&M), 0); CHECK_EQ(M.hook_state, B195_HOOK_NONE);
    g_ro8_rc = -1; CHECK_EQ(b195_hook_install(&M), -1); CHECK_EQ(b195_status(&M), B195_UNCERTAIN); g_ro8_rc = 1;
    CHECK(U(slot) == B + B195_RVA_REGISTER);
    // missed: 365 registered without the vtable (stale pointer) -> disarm restores the slot and reports MISSED
    world(false); plan(); CHECK_EQ(configure(), 1); b195_set_patch_ro8(&M, f_ro8, nullptr);
    CHECK_EQ(b195_hook_install(&M), 1);
    g_reg = (uintptr_t)arena(0xcc0); W(g_reg, B + B195_RVA_REGISTRY_VTABLE); W(B + B195_RVA_REGISTRY_GLOBAL, g_reg);
    stock_init(false);
    CHECK_EQ(M.hook_state, B195_HOOK_ARMED);
    CHECK_EQ(b195_hook_disarm(&M), 1); CHECK_EQ(M.hook_state, B195_HOOK_MISSED); CHECK(U(slot) == B + B195_RVA_REGISTER);
    // disarm before the registry exists: slot restored, plain DISARMED
    world(false); plan(); CHECK_EQ(configure(), 1); b195_set_patch_ro8(&M, f_ro8, nullptr);
    CHECK_EQ(b195_hook_install(&M), 1);
    CHECK_EQ(b195_hook_disarm(&M), 1); CHECK_EQ(M.hook_state, B195_HOOK_DISARMED); CHECK(U(slot) == B + B195_RVA_REGISTER);
}

int main(int argc, char **argv) {
    if (argc > 1) IMAGE = argv[1];
    if (!load_image()) { printf("test_blocks195: SKIPPED (retail image %s not readable)\n", IMAGE); return 77; }
    test_check_desc();
    test_configure();
    test_full_data_hook();
    test_hook_refusals();
    test_hook_relro_slot();
    test_block_refusals();
    test_item_refusals();
    test_creative_entries();
    test_text_oneshot();
    test_storage_rtti();
    b195_test_reset();
    test_states_shapes();
#ifdef LCE_HAVE_BLOCK_BOXES_1_11
    test_boxes();
#endif
    test_chest_storage();
    free(g_image);
    TEST_MAIN_END("test_blocks195");
}
