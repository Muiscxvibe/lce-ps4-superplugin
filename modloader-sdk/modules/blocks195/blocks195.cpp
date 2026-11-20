// blocks195: generic N-block publisher for CUSA00265 1.95. See blocks195.h for
// the phase contract and STATUS.md for the evidence behind every RVA/offset.
#include "blocks195.h"
#include <stdio.h>
#include <string.h>

namespace {

b195_module *g_m;  // one configured module per process (dispatchers need it)

// The live-probed allowlist (runtime-tests/mc195-lucky-cleanup-r24-s9v_d5zy,
// mc195-nbt-refresh-live-u7rfkskg), identical to loader/retail195/id_space_195.h.
// Statically confirmed: these + 253/261 are exactly the IDs never passed to the
// registry in Block init 0x957a0 (345 registrations, ids 1..365).
const uint32_t k_allow[B195_MAX_BLOCKS] = {254, 262, 280, 281, 282, 283, 284, 285, 286, 287, 288, 289, 290, 294, 314, 315, 316, 317, 318};

// lce_block_material -> Material* global (first argument of Block::Block 0x91fb0/0x92150).
const uint32_t k_material[LCE_MAT_COUNT] = {
    0x1418ec0u,  // STONE: OreBlock ctor 0x503a86, stone ctor 0x67fce8
    0x1418890u,  // WOOD:  planks ctor 0x53ddf8
    0x14183a8u,  // METAL: iron/gold/diamond blocks 0x99aa8, 0x99d19 (Block 0x92150)
    0x14188f0u,  // DIRT:  dirt ctor 0x207418
    0x1419028u,  // SAND:  FallingBlock() 0x2a0f16 (sand ctor 0x5c3790)
    0x1419040u,  // GLASS: glass (id 20) / glowstone (id 89) ctor argument
    0x1418ff8u,  // PLANT: Bush() 0x138d56
    0x1419018u,  // WOOL:  wool (id 35) ctor argument
};
// GLASS and PLANT are refused: OreBlock renders an opaque solid full cube.
const uint8_t k_material_ok[LCE_MAT_COUNT] = {1, 1, 1, 1, 1, 0, 0, 1};
// lce_block_sound -> SoundType* global passed to setSoundType 0x92540.
const uint32_t k_sound[LCE_SOUND_COUNT] = {
    0x1431e00u,  // STONE  (id 1)
    0x1431de8u,  // WOOD   (ids 5, 50, 54)
    0x1431df0u,  // GRAVEL (ids 3, 13)
    0x1431df8u,  // GRASS  (ids 2, 6, 31)
    0x1431e08u,  // METAL  (ids 66, 101, 167)
    0x1431e10u,  // GLASS  (ids 20, 79, 89)
    0x1431e18u,  // CLOTH  (id 35)
    0x1431e20u,  // SAND   (id 12)
};
// Pinned code (read from the image, compared before any native call).
const uint8_t k_ore_ctor[52] = {0x55,0x48,0x89,0xe5,0x53,0x50,0x48,0x8d,0x05,0x33,0x54,0xf1,0x00,0x48,0x89,0xfb,0x48,0x8b,0x30,0xe8,0x18,0xe5,0xb8,0xff,
                                0x48,0x8d,0x05,0x41,0x52,0xd9,0x00,0x48,0x89,0xdf,0x48,0x83,0xc0,0x10,0x48,0x89,0x03,0x48,0x83,0xc4,0x08,0x5b,0x5d,0xe9,0x0c,0xea,0xb8,0xff};
const uint8_t k_pro_register[16] = {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec};  // 0xb8090
const uint8_t k_pro_block_ctor[16] = {0x55,0x48,0x89,0xe5,0x41,0x56,0x53,0x48,0x8d,0x05,0x92,0x5e,0x18,0x01,0x48,0x89}; // 0x91fb0
// 0x924c0 .. 0x924ee: +0x158 = 1, +0x188 = vt +0x478 (StateDefinition), +0x190 = states[0] (the default it registers)
const uint8_t k_pro_init_states[46] = {0x55,0x48,0x89,0xe5,0x53,0x50,0x48,0x89,0xfb,0xc6,0x83,0x58,0x01,0x00,0x00,0x01,
                                       0x48,0x8b,0x03,0xff,0x90,0x78,0x04,0x00,0x00,0x48,0x89,0x83,0x88,0x01,0x00,0x00,
                                       0x48,0x8b,0x40,0x28,0x48,0x8b,0x38,0x48,0x89,0xbb,0x90,0x01,0x00,0x00};
// API 1.7 variants re-do what ColoredBlock::ColoredBlock 0x1a96d0 does after 0x924c0: registerDefaultState 0x7a040 is a
// plain store to +0x190, called at 0x1a9779 after any() 0x7a050 (0x1a9714) and setValue(COLOR, WHITE) (0x1a9762).
const uint8_t k_register_default[8] = {0x48,0x89,0xb7,0x90,0x01,0x00,0x00,0xc3};                  // 0x7a040
const uint8_t k_colored_calls[3][5] = {{0xe8,0xb3,0x8d,0xee,0xff}, {0xe8,0x37,0x09,0xed,0xff}, {0xe8,0xc2,0x08,0xed,0xff}};   // 0x1a9708 0x1a9714 0x1a9779
const uint8_t k_pro_block_item[16] = {0x55,0x48,0x89,0xe5,0x41,0x56,0x53,0x49,0x89,0xf6,0x48,0x89,0xfb,0xe8,0x9e,0x7b}; // 0x7d1d0
const uint8_t k_pro_item_register[16] = {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x81,0xec}; // 0x3af660
const uint8_t k_pro_map_slot[16] = {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec}; // 0x3af480
const uint8_t k_pro_fire_slot[16] = {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec}; // 0x1475c0
const uint8_t k_pro_sprite_bind[16] = {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x50,0x48,0x89}; // 0xd53b40
// Stock registry vtable (base+0x1218840) slot values.
const uint32_t k_registry_slots[B195_REGISTRY_SLOTS] = {0xb8120, 0x13bd30, 0x13bdf0, 0x13be00, 0xb82c0, 0xb8090, 0x13bea0, 0xb8480, 0xb84a0, 0xb84c0};
// OreBlock vtable slots the module calls directly or replaces (offset, stock RVA).
const uint32_t k_ore_pins[][2] = {
    {0x098, 0x92540}, {0x0a8, 0x92560}, {0x0b0, 0x92580}, {0x108, 0x92a00}, {0x240, 0x503b00}, {0x258, 0x503c70},
    {0x360, 0x503bb0}, {0x380, 0x94650}, {0x388, 0x94690}, {0x390, 0x946a0}, {0x418, 0x94890}, {0x438, 0x94ea0}, {0x458, 0x95340},
};

// Storage blocks: DispenserBlock primary slots copied into the private vtable (offset, RVA), checked against the stock table.
const uint32_t k_container_slots[][2] = {
    {0x228, 0x209570}, {0x2c0, 0x2085a0}, {0x340, 0x43140}, {0x398, 0x53010}, {0x408, 0x209750}, {0x410, 0x209760},
};
const uint8_t k_be_check[9] = {0x80, 0xb8, 0x4a, 0x01, 0x00, 0x00, 0x00, 0x74, 0x27};     // 0x4107fd cmp byte [rax+0x14a],0; je
const uint8_t k_be_ctor_flag[7] = {0xc6, 0x83, 0x4a, 0x01, 0x00, 0x00, 0x01};              // 0x424f9 mov byte [rbx+0x14a],1
const char k_container_type_name[] = "N6lcesdk16ModStorageBlockE";  // unique: no game code casts to it

// Chest storage (27): ChestBlock slots the module relies on (offset, RVA) and the cube slots it restores (offset, chest RVA, Block RVA).
const uint32_t k_chest_slots[][2] = {{0x228, 0x174a00}, {0x2c0, 0x174ba0}, {0x340, 0x43140}, {0x398, 0x53010}, {0x408, 0x175e10},
                                     {0x410, 0x175e20}, {0x478, 0x1761d0}, {0x4d8, 0x1755f0}, {0x380, 0x94650}, {0x240, 0x93760},
                                     {0x360, 0x945e0}, {0x098, 0x92540}, {0x0a8, 0x92560}, {0x0b0, 0x92580}, {0x108, 0x92a00}, {0x418, 0x94890},
                                     {0x458, 0x95340}};
const uint32_t k_chest_cube[][3] = {{0x0d0, 0x173170, 0x92970}, {0x0d8, 0x173180, 0x929a0}, {0x0e8, 0x176300, 0x929d0}, {0x178, 0x173190, 0x92b40},
                                    {0x1a0, 0x176250, 0x934c0}, {0x1d0, 0x173160, 0x93680}, {0x438, 0x176260, 0x94ea0}};
const uint8_t k_chest_ctor[16] = {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec};   // 0x173060
const uint8_t k_chest_alloc[12] = {0xbf,0xf8,0x01,0x00,0x00, 0xe8,0x25,0x1c,0xdd,0x00, 0x48,0x89};                    // 0x9b3a1
// Storage titles and the chest-model cull (blocks195.h "Storage titles"): code and slot pins.
const uint8_t k_set_custom_name[9] = {0x48, 0x8b, 0x7f, 0x68, 0xe9, 0xb7, 0xab, 0x5e, 0x00};   // 0x597b30 mov rdi,[rdi+0x68]; jmp 0xb826f0
const uint8_t k_title_reads_name[4] = {0x49, 0x8b, 0x77, 0x68};                              // 0x598489 mov rsi,[r15+0x68]
const uint8_t k_view_dist[4] = {0xc5, 0xfb, 0x10, 0x05};                                     // 0xbad20 vmovsd xmm0,[rip+..] (ret at +8)
const uint8_t k_render_cull[7] = {0xc5, 0xf9, 0x2e, 0x45, 0x98, 0x0f, 0x86};                 // 0xb2ba35 vucomisd xmm0,[rbp-0x68]; jbe
const uint32_t k_title_vtables[][2] = {{0x1225850u + 0x30, 0x5985b0u}, {0x1239d70u + 0x30, 0x5985b0u}};   // InteractionObject +0x30 (chest, dispenser)

// API 1.7 variants: ColoredBlock (vtable 0x122d060) slots (offset, RVA) and their first 16 code bytes.
[[maybe_unused]] const uint32_t k_colored_slots[][2] = {{0x048, B195_RVA_COLORED_FROM_DATA}, {0x050, B195_RVA_COLORED_TO_DATA},
                                       {0x268, B195_RVA_COLORED_DROP_AUX}, {0x478, B195_RVA_COLORED_STATEDEF}};
[[maybe_unused]] const uint8_t k_colored_code[4][16] = {
    {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec},   // 0x1a9850
    {0x55,0x48,0x89,0xe5,0x53,0x50,0x48,0x8d,0x05,0xfb,0x6a,0x26,0x01,0x48,0x8b,0x0e},   // 0x1a9900
    {0x55,0x48,0x89,0xe5,0x53,0x50,0x48,0x8d,0x05,0x4b,0x6c,0x26,0x01,0x48,0x8b,0x0e},   // 0x1a97b0
    {0x55,0x48,0x89,0xe5,0x41,0x56,0x53,0x8a,0x05,0xbb,0x6a,0x26,0x01,0x49,0x89,0xfe}};  // 0x1a9950
[[maybe_unused]] const uint8_t k_face_getter[16] = {0x55,0x48,0x89,0xe5,0x53,0x50,0x48,0x89,0xfb,0x80,0xbb,0xe0,0x01,0x00,0x00,0x00};  // 0x95340
[[maybe_unused]] const uint8_t k_level_data[3] = {0x89, 0xf0, 0xc3};                                       // 0x49f950 mov eax,esi; ret
[[maybe_unused]] const uint8_t k_set_stacked[8] = {0x48, 0x89, 0xf8, 0x40, 0x88, 0x77, 0x31, 0xc3};        // 0x3a5a70
[[maybe_unused]] const uint8_t k_dir_index[4] = {0x8b, 0x47, 0x08, 0xc3};                                  // 0x200600
[[maybe_unused]] const uint8_t k_stack_aux[4] = {0x8b, 0x47, 0x3c, 0xc3};                                  // 0x3557c0
// API 1.7 shapes.
[[maybe_unused]] const uint8_t k_box[16] = {0x55,0x48,0x89,0xe5,0x48,0x83,0xec,0x60,0xbf,0x30,0x00,0x00,0x00,0xc5,0xfb,0x11};  // 0x170, alloc 0x30
[[maybe_unused]] const uint8_t k_ret_false[3] = {0x31, 0xc0, 0xc3};
[[maybe_unused]] const uint8_t k_ret_true[3] = {0xb0, 0x01, 0xc3};
// GlassBlock (0x125dce0) render/occlusion slots copied for TRANSPARENT (offset, RVA, 3 code bytes or none).
[[maybe_unused]] const uint32_t k_glass_slots[][2] = {{0x0d0, 0x333d60}, {0x100, 0x333d70}, {0x158, 0x333d90}, {0x188, 0x353460}, {0x1d0, 0x353450}};
// OreBlock stock values of every slot API 1.7 replaces (the private vtable is a copy, so these are what we overwrite).
[[maybe_unused]] const uint32_t k_ore_state_pins[][2] = {{0x048, 0x91eb0}, {0x050, 0x91ec0}, {0x0d0, 0x92970}, {0x100, 0x92990}, {0x158, 0x92ad0},
                                        {0x178, 0x92b40}, {0x188, 0x92c90}, {0x1a0, 0x934c0}, {0x1c8, 0x93660}, {0x1d0, 0x93680},
                                        {0x268, 0x503df0}, {0x478, 0x953c0}};
[[maybe_unused]] const uint32_t k_ore_box_pins[][2] = {{0x0e8, 0x929d0}, {0x190, B195_RVA_BOX_LIST_DEFAULT}, {0x1b0, B195_RVA_ADD_COLLISION}};

// API 1.11 multi-box: StairBlock slots reused for BOXES blocks, and the render path that consumes +0x190 (see blocks195.h).
[[maybe_unused]] const uint8_t k_stair_shape[6] = {0xb8, 0x0a, 0x00, 0x00, 0x00, 0xc3};                     // 0x678ad0 mov eax,0xa; ret
[[maybe_unused]] const uint8_t k_stair_collision[16] = {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x50,0x8a,0x45};  // 0x6765c0
[[maybe_unused]] const uint8_t k_stair_collision_list[6] = {0xff, 0x90, 0x90, 0x01, 0x00, 0x00};          // 0x676605 call [rax+0x190]
[[maybe_unused]] const uint8_t k_stair_collision_add[6] = {0xff, 0x90, 0xb8, 0x01, 0x00, 0x00};           // 0x67663d call [rax+0x1b8]
[[maybe_unused]] const uint8_t k_tess_list[6] = {0xff, 0x93, 0x90, 0x01, 0x00, 0x00};                     // 0xb3b193 call [rbx+0x190]

// A box covers face f (D U N S W E) of the cell completely.
bool face_solid(const uint8_t *x, int f) {
    bool xs = x[0] == 0 && x[3] == 16, ys = x[1] == 0 && x[4] == 16, zs = x[2] == 0 && x[5] == 16;
    switch (f) {
        case 0: return x[1] == 0 && xs && zs;
        case 1: return x[4] == 16 && xs && zs;
        case 2: return x[2] == 0 && xs && ys;
        case 3: return x[5] == 16 && xs && ys;
        case 4: return x[0] == 0 && ys && zs;
        case 5: return x[3] == 16 && ys && zs;
        default: return false;
    }
}
void union_of(const uint8_t (*bx)[6], uint32_t n, uint8_t out[6]) {
    for (int a = 0; a < 3; ++a) { out[a] = 16; out[a + 3] = 0; }
    for (uint32_t k = 0; k < n; ++k)
        for (int a = 0; a < 3; ++a) { if (bx[k][a] < out[a]) out[a] = bx[k][a]; if (bx[k][a + 3] > out[a + 3]) out[a + 3] = bx[k][a + 3]; }
}

bool special(const b195_block &b) { return b.variants || b.shape || b.no_collide || b.transparent; }
void fail(b195_module *m, const char *msg) { snprintf(m->last_error, sizeof m->last_error, "%s", msg); }

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

bool rd(b195_module *m, uintptr_t a, void *v, size_t n) { return a && a <= UINTPTR_MAX - n && m->mem.read(m->mem.ctx, a, v, n) == 1; }
bool wr(b195_module *m, uintptr_t a, const void *v, size_t n) { return a && a <= UINTPTR_MAX - n && m->mem.write(m->mem.ctx, a, v, n) == 1; }
bool q(b195_module *m, uintptr_t a, uintptr_t *v) { return rd(m, a, v, 8); }
bool is(b195_module *m, uintptr_t a, uintptr_t want) { uintptr_t v = 0; return q(m, a, &v) && v == want; }
bool code(b195_module *m, uint32_t rva, const uint8_t *want, size_t n) {
    uint8_t got[64];
    return n <= sizeof got && rd(m, m->base + rva, got, n) && !memcmp(got, want, n);
}
bool fpin(b195_module *m, uint32_t rva, float want) { float f = 0; return rd(m, m->base + rva, &f, 4) && f == want; }
uint64_t tid(b195_module *m) { return m->mem.thread_id(m->mem.ctx); }
bool attested(b195_module *m) { return m->mem.attested(m->mem.ctx) == 1; }
bool blocks_held(b195_module *m) {
    if (!attested(m)) return false;
    if (__atomic_load_n(&m->phase, __ATOMIC_ACQUIRE) == 1 && m->phase_thread && tid(m) == m->phase_thread) return true;
    return m->mem.blocks_held && m->mem.blocks_held(m->mem.ctx) == 1;
}
bool items_held(b195_module *m) { return attested(m) && m->mem.items_held(m->mem.ctx) == 1; }
bool terrain_held(b195_module *m) { return attested(m) && m->mem.terrain_held && m->mem.terrain_held(m->mem.ctx) == 1; }

bool key_text(const char *k) {
    if (!k) return false;
    size_t n = strlen(k), colon = 0, colons = 0;
    if (n < 3 || n > 63) return false;
    for (size_t i = 0; i < n; ++i) {
        char c = k[i];
        if (c == ':') { ++colons; colon = i; continue; }
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '/' || c == '-')) return false;
    }
    return colons == 1 && colon > 0 && colon + 1 < n && strncmp(k, "minecraft:", 10);
}

bool tool_is(const char *t, const char *want) { return t && !strcmp(t, want); }

// ---- natives-backed helpers ---------------------------------------------
// wstring (game layout): +8 SSO units / heap pointer, +0x18 length, +0x20 capacity.
bool wide_equals(b195_module *m, uintptr_t a, const char *s, size_t n) {
    uintptr_t len = 0, cap = 0, chars = 0;
    if (!q(m, a + 0x18, &len) || !q(m, a + 0x20, &cap) || len != n || cap < len || cap > 4096) return false;
    if (cap < 8) chars = a + 8; else if (!q(m, a + 8, &chars) || !chars) return false;
    for (size_t i = 0; i < n; ++i) { uint16_t c = 0; if (!rd(m, chars + 2 * i, &c, 2) || c != (unsigned char)s[i]) return false; }
    return true;
}
// ResourceLocation (lucky key.c / additive_block_items native.cpp): path at +0x18, namespace at +0x40.
bool key_matches(b195_module *m, const b195_block &b) {
    const char *k = b.desc->key, *c = strchr(k, ':');
    return c && wide_equals(m, (uintptr_t)b.key + 0x40, k, (size_t)(c - k)) && wide_equals(m, (uintptr_t)b.key + 0x18, c + 1, strlen(c + 1));
}
void make_wstring(b195_module *m, uint8_t *storage, const char *ascii) {
    uint16_t u[64];
    uint32_t n = 0;
    for (; ascii[n] && n < 63; ++n) u[n] = (unsigned char)ascii[n];
    memset(storage, 0, 0x30);
    *(uint64_t *)(storage + 0x20) = 7;   // empty SSO wstring, as the stock code initialises it
    m->n.wstring(storage, u, n);
}

// Block->BlockItem map walk (list-backed; r72 additive_block_items_195 mapping()).
bool map_count(b195_module *m, uintptr_t block, uintptr_t expected, unsigned *matches) {
    uintptr_t map = m->base + B195_RVA_BLOCK_ITEM_MAP, head = 0, node = 0, count = 0;
    *matches = 0;
    if (!q(m, map + 8, &head) || !head || !q(m, map + 16, &count) || count > 8192 || !q(m, head, &node)) return false;
    for (uintptr_t i = 0; i < count; ++i) {
        uintptr_t next = 0, k = 0, v = 0;
        if (!node || node == head || !q(m, node, &next) || !q(m, node + 16, &k) || !q(m, node + 24, &v)) return false;
        if (k == block) { if (v != expected) return false; ++*matches; }
        node = next;
    }
    return node == head;
}

uint32_t live_items(b195_module *m) { return (uint32_t)((m->item_header[1] - m->item_header[0]) / 8); }
bool item_header(b195_module *m, uintptr_t h[3]) {
    return rd(m, m->base + B195_RVA_ITEM_VECTOR, h, 24) && h[0] && h[0] <= h[1] && h[1] <= h[2] && !((h[0] | h[1] | h[2]) & 7) &&
           (h[1] - h[0]) / 8 >= 2268 && (h[2] - h[0]) / 8 <= B195_ITEM_VECTOR_CAP;
}

b195_block *find_block(b195_module *m, uintptr_t obj) {
    for (uint32_t i = 0; i < m->count; ++i) if (m->blocks[i].object == obj) return &m->blocks[i];
    return nullptr;
}

// DispenserBlock layout pins: secondary EntityBlock vtable, RTTI chain, BaseEntityBlock vmi bases. 1 ok.
bool container_pins(b195_module *m, uintptr_t *si_vptr) {
    const uintptr_t B = m->base, D = B + B195_RVA_DISPENSER_VTABLE;
    for (auto &p : k_container_slots) if (!is(m, D + p[0], B + p[1])) return false;
    uintptr_t sec[5], ti[3], base_ti[6], ore_ti = 0;
    if (!rd(m, D + 0x4f0, sec, sizeof sec) || sec[0] != (uintptr_t)-(intptr_t)B195_F_ENTITY_VPTR || sec[1] != B + B195_RVA_DISPENSER_RTTI ||
        sec[2] != B + B195_RVA_DISP_SEC_DTOR || sec[3] != B + B195_RVA_DISP_SEC_DDTOR || sec[4] != B + B195_RVA_DISP_NEW_BE) return false;
    if (!is(m, D - 8, B + B195_RVA_DISPENSER_RTTI) || !rd(m, B + B195_RVA_DISPENSER_RTTI, ti, sizeof ti) || ti[2] != B + B195_RVA_BASE_ENTITY_RTTI)
        return false;
    // the si typeinfo vptr is an import (resolved at load): it must be the same one OreBlock's si typeinfo uses
    if (!q(m, B + B195_RVA_ORE_RTTI, &ore_ti) || !ti[0] || ti[0] != ore_ti) return false;
    if (!rd(m, B + B195_RVA_BASE_ENTITY_RTTI + 0x10, base_ti, sizeof base_ti) || base_ti[0] != 0x200000000ull ||
        base_ti[1] != B + B195_RVA_BLOCK_RTTI || base_ti[2] != 2 || base_ti[3] != B + B195_RVA_ENTITY_BLOCK_RTTI ||
        base_ti[4] != ((uintptr_t)B195_F_ENTITY_VPTR << 8 | 2)) return false;
    if (!code(m, B195_RVA_BE_CHECK, k_be_check, sizeof k_be_check) || !code(m, B195_RVA_BE_CTOR_FLAG, k_be_ctor_flag, sizeof k_be_ctor_flag))
        return false;
    *si_vptr = ti[0];
    return true;
}

// ChestBlock layout pins (27-slot storage). 1 ok.
bool chest_pins(b195_module *m) {
    const uintptr_t B = m->base, C = B + B195_RVA_CHEST_VTABLE;
    uintptr_t hdr[2], sec[5], ti[3];
    if (!rd(m, C - 16, hdr, sizeof hdr) || hdr[0] || hdr[1] != B + B195_RVA_CHEST_RTTI) return false;
    for (auto &p : k_chest_slots) if (!is(m, C + p[0], B + p[1])) return false;
    for (auto &p : k_chest_cube) if (!is(m, C + p[0], B + p[1]) || !is(m, B + 0x1217e60u + p[0], B + p[2])) return false;
    if (!rd(m, C + 0x4e0, sec, sizeof sec) || sec[0] != (uintptr_t)-(intptr_t)B195_F_ENTITY_VPTR || sec[1] != B + B195_RVA_CHEST_RTTI ||
        sec[4] != B + B195_RVA_CHEST_NEW_BE || C + 0x4f0 != B + B195_RVA_CHEST_ENTITY_VT) return false;
    if (!rd(m, B + B195_RVA_CHEST_RTTI, ti, sizeof ti) || ti[2] != B + B195_RVA_BASE_ENTITY_RTTI) return false;
    return code(m, B195_RVA_CHEST_CTOR, k_chest_ctor, sizeof k_chest_ctor) && code(m, B195_RVA_CHEST_ALLOC_SITE, k_chest_alloc, sizeof k_chest_alloc) &&
           code(m, B195_RVA_BE_CHECK, k_be_check, sizeof k_be_check);
}

// Storage title pins (any storage block) and the ChestBlockEntity vtable / render-cull pins (27-slot). 1 ok.
bool title_pins(b195_module *m, bool chest) {
    const uintptr_t B = m->base;
    for (auto &p : k_title_vtables) if (!is(m, B + p[0], B + p[1])) return false;
    if (!code(m, B195_RVA_SET_CUSTOM_NAME, k_set_custom_name, sizeof k_set_custom_name) || !code(m, 0x598489u, k_title_reads_name, sizeof k_title_reads_name))
        return false;
    if (!chest) return true;
    const uintptr_t V = B + B195_RVA_CHEST_BE_VTABLE;
    uintptr_t pre[B195_CHEST_BE_PREFIX], next = 0;
    uint8_t ret = 0;
    return rd(m, V - 8 * B195_CHEST_BE_PREFIX, pre, sizeof pre) && pre[0] == 0xf8 && pre[1] == 0xf8 && pre[2] == 0 && pre[3] == B + B195_RVA_CHEST_BE_RTTI &&
           q(m, V + 8 * B195_CHEST_BE_SLOTS, &next) && next == 0xb8 && is(m, V + B195_F_BE_VIEW_DIST, B + B195_RVA_BE_VIEW_DIST) &&
           code(m, B195_RVA_BE_VIEW_DIST, k_view_dist, sizeof k_view_dist) && rd(m, B + B195_RVA_BE_VIEW_DIST + 8, &ret, 1) && ret == 0xc3 &&
           code(m, B195_RVA_BE_RENDER_CULL, k_render_cull, sizeof k_render_cull);
}

// Storage blocks: the container title wstrings (display name and the empty second string setCustomName takes).
void make_title(b195_module *m, b195_block &b) {
    memset(b.title_text, 0, sizeof b.title_text); *(uint64_t *)(b.title_text + 0x20) = 7;
    memset(b.empty_text, 0, sizeof b.empty_text); *(uint64_t *)(b.empty_text + 0x20) = 7;
    m->n.wstring(b.title_text, b.name, b.name_len);
    m->n.wstring(b.empty_text, b.name, 0);
}

// ---- block phase --------------------------------------------------------
int refuse_blocks(b195_module *m, const char *why) { fail(m, why); m->blocks_state = B195_REFUSED; m->state = B195_REFUSED; return 0; }
int uncertain_blocks(b195_module *m, const char *why) { fail(m, why); m->blocks_state = B195_UNCERTAIN; m->state = B195_UNCERTAIN; return -1; }

bool apply_properties(b195_module *m, b195_block &b, uintptr_t snd) {
    const lce_block_desc *d = b.desc;
    void *o = (void *)b.object;
    float h = (float)d->hardness_milli / 1000.0f;
    m->n.set_hardness(o, h);
    float want_res = h * 5.0f;
    if (d->resistance_milli) { float r = (float)d->resistance_milli / 1000.0f; m->n.set_resistance(o, r); want_res = r * 3.0f; }
    m->n.set_sound(o, snd);
    if (d->light_emission) m->n.set_light(o, ((float)d->light_emission + 0.5f) / 15.0f);
    if (d->slipperiness_milli) { float s = (float)d->slipperiness_milli / 1000.0f; if (!wr(m, b.object + B195_F_FRICTION, &s, 4)) return false; }
    make_wstring(m, b.texture_text, b.texture_name);
    m->n.set_texture(o, b.texture_text);
    if (b.per_face) { uint8_t one = 1; if (!wr(m, b.object + B195_F_PER_FACE, &one, 1)) return false; }
    float gh = 0, gr = 0, gs = 0;
    int32_t light = -1;
    uintptr_t gsnd = 0;
    return rd(m, b.object + B195_F_HARDNESS, &gh, 4) && gh == h && rd(m, b.object + B195_F_RESISTANCE, &gr, 4) && gr == want_res &&
           q(m, b.object + B195_F_SOUND, &gsnd) && gsnd == snd && rd(m, b.object + B195_F_LIGHT, &light, 4) && light == (int32_t)d->light_emission &&
           rd(m, b.object + B195_F_FRICTION, &gs, 4) && gs == (d->slipperiness_milli ? (float)d->slipperiness_milli / 1000.0f : 0.6f);
}

// 27-slot storage: ChestBlock(0) on a 0x1f8 object, then a private copy of its vtable with Block's cube render/shape slots.
int construct_chest(b195_module *m, b195_block &b, uintptr_t mat, uintptr_t snd) {
    const uintptr_t B = m->base, C = B + B195_RVA_CHEST_VTABLE;
    void *o = m->n.alloc(B195_CHEST_SIZE);
    if (!o || ((uintptr_t)o & 15)) { fail(m, "chest block allocation"); return -1; }
    memset(o, 0, B195_CHEST_SIZE);
    b.object = (uintptr_t)o;
    m->n.chest_ctor(o, 0);
    uintptr_t vt = 0, ev = 0, f_mat = 0, def = 0, st = 0;
    int32_t type = -1;
    uint8_t has_be = 0, faces = 1;
    if (!q(m, b.object, &vt) || vt != C || !q(m, b.object + B195_F_ENTITY_VPTR, &ev) || ev != B + B195_RVA_CHEST_ENTITY_VT ||
        !rd(m, b.object + B195_F_CHEST_TYPE, &type, 4) || type != 0 || !rd(m, b.object + B195_F_HAS_BLOCK_ENTITY, &has_be, 1) || has_be != 1 ||
        !q(m, b.object + B195_F_MATERIAL, &f_mat) || f_mat != mat || !q(m, b.object + B195_F_STATEDEF, &def) || !def ||
        !q(m, b.object + B195_F_DEFSTATE, &st) || !st || !rd(m, b.object + B195_F_PER_FACE, &faces, 1) || faces) {
        fail(m, "ChestBlock construction check"); return -1;
    }
    if (!rd(m, C - 16, b.vtable, sizeof b.vtable)) { fail(m, "ChestBlock vtable copy"); return -1; }
    for (auto &p : k_chest_cube) b.vtable[2 + p[0] / 8] = B + p[2];
    b.vtable[2 + 0x380 / 8] = (uintptr_t)&b195_block_name_dispatch;
    if (b.drop_override) b.vtable[2 + 0x240 / 8] = (uintptr_t)&b195_get_resource_dispatch;
    if (b.count_override) b.vtable[2 + 0x360 / 8] = (uintptr_t)&b195_resource_count_dispatch;
    if (!apply_properties(m, b, snd)) { fail(m, "chest block property check"); return -1; }
    // what 0x924c0 would have derived from the cube's +0x1d0 (solid render): +0x12c = 1, +0x130 = 0xff (blocks light)
    uint8_t solid = 1; int32_t lb = 0xff;
    // private EntityBlock table (newBlockEntity wraps 0x175800) and ChestBlockEntity table (+0x70 view distance culls the model)
    if (!rd(m, C + 0x4e0, b.entity_vtable, sizeof b.entity_vtable) ||
        !rd(m, B + B195_RVA_CHEST_BE_VTABLE - 8 * B195_CHEST_BE_PREFIX, b.be_vtable, sizeof b.be_vtable)) {
        fail(m, "ChestBlock entity/BE vtable copy"); return -1;
    }
    b.entity_vtable[4] = (uintptr_t)&b195_new_block_entity_dispatch;
    b.be_vtable[B195_CHEST_BE_PREFIX + B195_F_BE_VIEW_DIST / 8] = (uintptr_t)&b195_be_view_distance;
    make_title(m, b);
    const uintptr_t pev = (uintptr_t)&b.entity_vtable[2];
    if (!wr(m, b.object + B195_F_ENTITY_VPTR, &pev, 8) || !is(m, b.object + B195_F_ENTITY_VPTR, pev)) { fail(m, "chest block private EntityBlock vtable"); return -1; }
    const uintptr_t mine = (uintptr_t)&b.vtable[2];
    if (!wr(m, b.object + 0x12c, &solid, 1) || !wr(m, b.object + 0x130, &lb, 4) || !wr(m, b.object, &mine, 8) || !is(m, b.object, mine)) {
        fail(m, "chest block private vtable"); return -1;
    }
    return 1;
}

// One block's construction (native calls on a fresh object). nullptr = built; otherwise the reason. A failed block was
// never registered, so the game cannot reach it: the caller drops only that block (the object leaks) and keeps the others.
const char *construct_one(b195_module *m, b195_block &b, const uintptr_t *ore, uintptr_t si_vptr) {
    const uintptr_t B = m->base;
    const lce_block_desc *d = b.desc;
    uintptr_t mat = 0, snd = 0;
    q(m, B + b.material_global, &mat);
    q(m, B + b.sound_global, &snd);
    b.slots = d->block_entity_slots;
    if (b.slots == B195_CHEST_STORAGE_SLOTS) {   // the mod block is a ChestBlock with Block's cube render slots
        int rc = construct_chest(m, b, mat, snd);
        if (rc != 1) return m->last_error;
        return nullptr;
    }
    const size_t size = b.slots ? B195_CONTAINER_SIZE : B195_BLOCK_SIZE;   // storage: + the EntityBlock subobject
    void *o = m->n.alloc(size);
    if (!o || ((uintptr_t)o & 15)) return "block allocation";
    memset(o, 0, size);   // base ctor leaves +0x198/+0x19c untouched (lucky construct.cpp)
    b.object = (uintptr_t)o;
    // private vtable: RTTI prefix + 155 slots; name/drop/count, storage and API 1.7 slots differ. API 1.7 blocks get it
    // before 0x924c0 builds the state definition (vt +0x478) and the light-blocking bytes (+0x1d0), like ColoredBlock 0x1a96d0.
    memcpy(b.vtable, ore, (2 + B195_ORE_SLOTS) * sizeof(uintptr_t));
    b.vtable[2 + 0x380 / 8] = (uintptr_t)&b195_block_name_dispatch;
    if (b.drop_override) b.vtable[2 + 0x240 / 8] = (uintptr_t)&b195_get_resource_dispatch;
    if (b.count_override) b.vtable[2 + 0x360 / 8] = (uintptr_t)&b195_resource_count_dispatch;
    if (b.slots) {
        b.typeinfo[0] = si_vptr; b.typeinfo[1] = (uintptr_t)k_container_type_name; b.typeinfo[2] = B + B195_RVA_BASE_ENTITY_RTTI;
        b.vtable[1] = (uintptr_t)b.typeinfo;
        for (auto &p : k_container_slots) b.vtable[2 + p[0] / 8] = B + p[1];
    }
    if (b.transparent) for (auto &p : k_glass_slots) b.vtable[2 + p[0] / 8] = B + p[1];
    if (b.shape || b.no_collide) {
        b.vtable[2 + 0xd0 / 8] = B + B195_RVA_RET_FALSE;
        b.vtable[2 + 0x1d0 / 8] = B + B195_RVA_RET_FALSE;
        b.vtable[2 + 0x1a0 / 8] = (uintptr_t)&b195_face_shape_dispatch;
    }
    if (b.shape) b.vtable[2 + 0x178 / 8] = (uintptr_t)&b195_shape_dispatch;
    if (b.no_collide) b.vtable[2 + 0x1c8 / 8] = B + B195_RVA_NO_COLLISION;
    if (b.box_count) {   // API 1.11: render shape 10 draws every box of +0x190; collision adds every box (StairBlock +0x1b0)
        b.vtable[2 + 0xe8 / 8] = B + B195_RVA_STAIR_RENDER_SHAPE;
        b.vtable[2 + 0x190 / 8] = (uintptr_t)&b195_box_list_dispatch;
        if (!b.no_collide) b.vtable[2 + 0x1b0 / 8] = B + B195_RVA_STAIR_COLLISION;
    }
    if (b.variants) {
        b.vtable[2 + 0x478 / 8] = B + B195_RVA_COLORED_STATEDEF;
        b.vtable[2 + 0x48 / 8] = (uintptr_t)&b195_state_from_data_dispatch;
        b.vtable[2 + 0x50 / 8] = B + B195_RVA_COLORED_TO_DATA;
        b.vtable[2 + 0x458 / 8] = (uintptr_t)&b195_face_sprite_dispatch;
        if (!b.drop_override) b.vtable[2 + 0x268 / 8] = B + B195_RVA_COLORED_DROP_AUX;
    }
    const uintptr_t mine = (uintptr_t)&b.vtable[2];
    if (b.shape) {
        const double u = 1.0 / 16.0;
        void *bb = m->n.box(b.box[0] * u, b.box[1] * u, b.box[2] * u, b.box[3] * u, b.box[4] * u, b.box[5] * u);
        double got[6] = {};
        if (!bb || !rd(m, (uintptr_t)bb, got, sizeof got)) return "shape AABB";
        for (int a = 0; a < 6; ++a) if (got[a] != b.box[a] * u) return "shape AABB readback";
        b.aabb = (uintptr_t)bb;
        b.union_aabb[0] = b.aabb;
        for (uint32_t s = 0; s < b.box_sets; ++s) {   // API 1.11: every box of every set, then the set's union (outline)
            for (uint32_t k = 0; k < b.box_count; ++k) {
                const uint8_t *x = b.boxes[s][k];
                void *p = m->n.box(x[0] * u, x[1] * u, x[2] * u, x[3] * u, x[4] * u, x[5] * u);
                if (!p || !rd(m, (uintptr_t)p, got, sizeof got)) return "box list AABB";
                for (int a = 0; a < 6; ++a) if (got[a] != x[a] * u) return "box list AABB readback";
                b.box_aabb[s][k] = (uintptr_t)p;
            }
            b.box_list[s].data = (uintptr_t)b.box_aabb[s]; b.box_list[s].count = b.box_count; b.box_list[s].pad = 0;
            if (s) {
                uint8_t un[6];
                union_of(b.boxes[s], b.box_count, un);
                void *p = m->n.box(un[0] * u, un[1] * u, un[2] * u, un[3] * u, un[4] * u, un[5] * u);
                if (!p) return "box union AABB";
                b.union_aabb[s] = (uintptr_t)p;
            }
        }
    }
    if (special(b)) {   // Block::Block, the private vptr, then the state tail (the OreBlock/ColoredBlock ctor pattern)
        m->n.block_ctor(o, mat);
        if (!wr(m, b.object, &mine, 8)) return "private vptr before states";
        m->n.block_init_states(o);
        if (b.variants) {
            // ColoredBlock::ColoredBlock 0x1a96d0 does not keep 0x924c0's default (states[0] of the StateDefinition,
            // 0x924e0..0x924e7, which is not data 0 on the console): it takes any() 0x7a050, sets COLOR = WHITE
            // (vt +0x158 at 0x1a9762) and registerDefaultState 0x7a040 (a plain store to +0x190) at 0x1a9779.
            // getStateFromData(0) 0x1a9850 is that same state (defaultBlockState 0x7a4d0 + setValue(COLOR, byId(0))).
            uintptr_t white = m->n.state_from_data(o, 0);
            if (!white || !wr(m, b.object + B195_F_DEFSTATE, &white, 8) || !is(m, b.object + B195_F_DEFSTATE, white))
                return "variant default state (registerDefaultState)";
        }
    } else if (d->material == LCE_MAT_STONE) m->n.ore_ctor(o);
    else {  // exactly the OreBlock ctor (pinned bytes above) with another Material*
        m->n.block_ctor(o, mat);
        uintptr_t vt = B + B195_RVA_ORE_VTABLE;
        if (!wr(m, b.object, &vt, 8)) return "ore vptr";
        m->n.block_init_states(o);
    }
    uintptr_t vt = 0, f_mat = 0, def = 0, st = 0;
    uint8_t faces = 1;
    float fr = 0;
    if (!q(m, b.object, &vt) || vt != (special(b) ? mine : B + B195_RVA_ORE_VTABLE) || !q(m, b.object + B195_F_MATERIAL, &f_mat) || f_mat != mat ||
        !q(m, b.object + B195_F_STATEDEF, &def) || !def || !q(m, b.object + B195_F_DEFSTATE, &st) || !st ||
        !rd(m, b.object + B195_F_PER_FACE, &faces, 1) || faces || !rd(m, b.object + B195_F_FRICTION, &fr, 4) || fr != 0.6f)
        return "OreBlock construction check";
    float h = (float)d->hardness_milli / 1000.0f;
    m->n.set_hardness(o, h);
    float want_res = h * 5.0f;
    if (d->resistance_milli) { float r = (float)d->resistance_milli / 1000.0f; m->n.set_resistance(o, r); want_res = r * 3.0f; }
    m->n.set_sound(o, snd);
    if (d->light_emission) m->n.set_light(o, ((float)d->light_emission + 0.5f) / 15.0f);
    if (d->slipperiness_milli) { float s = (float)d->slipperiness_milli / 1000.0f; if (!wr(m, b.object + B195_F_FRICTION, &s, 4)) return "friction"; }
    make_wstring(m, b.texture_text, b.texture_name);
    m->n.set_texture(o, b.texture_text);
    if (b.per_face) { uint8_t one = 1; if (!wr(m, b.object + B195_F_PER_FACE, &one, 1)) return "per-face flag"; }
    float gh = 0, gr = 0, gs = 0;
    int32_t light = -1;
    uintptr_t gsnd = 0;
    if (!rd(m, b.object + B195_F_HARDNESS, &gh, 4) || gh != h || !rd(m, b.object + B195_F_RESISTANCE, &gr, 4) || gr != want_res ||
        !q(m, b.object + B195_F_SOUND, &gsnd) || gsnd != snd || !rd(m, b.object + B195_F_LIGHT, &light, 4) || light != (int32_t)d->light_emission ||
        !rd(m, b.object + B195_F_FRICTION, &gs, 4) || gs != (d->slipperiness_milli ? (float)d->slipperiness_milli / 1000.0f : 0.6f))
        return "block property check";
    if (special(b)) {   // what 0x924c0 derived from our slots
        uint8_t solid = 1;
        if ((b.shape || b.no_collide || b.transparent) && (!rd(m, b.object + 0x12c, &solid, 1) || solid))
            return "non-full block still marked solid-render (+0x12c)";
        if (b.variants) {   // states 0..n-1 distinct, data round-trips, default = variant 0
            if (m->n.data_from_state(o, st) != 0) return "variant default state is not data 0";
            for (uint32_t v = 0; v < b.variants; ++v) {
                uintptr_t s = m->n.state_from_data(o, (int)v);
                if (!s || m->n.data_from_state(o, s) != (int)v || (v && s == st)) return "variant state round trip";
            }
        }
    }
    if (b.slots) {   // BaseEntityBlock layout (see blocks195.h): RTTI, EntityBlock subobject, hasBlockEntity, dispenser slots
        b.entity_vtable[0] = (uintptr_t)-(intptr_t)B195_F_ENTITY_VPTR; b.entity_vtable[1] = (uintptr_t)b.typeinfo;
        b.entity_vtable[2] = B + B195_RVA_DISP_SEC_DTOR; b.entity_vtable[3] = B + B195_RVA_DISP_SEC_DDTOR;
        b.entity_vtable[4] = (uintptr_t)&b195_new_block_entity_dispatch;   // wraps 0x208ef0 and names the BE
        make_title(m, b);
        uintptr_t ev = (uintptr_t)&b.entity_vtable[2];
        uint8_t one = 1, flag = 0;
        if (!wr(m, b.object + B195_F_ENTITY_VPTR, &ev, 8) || !wr(m, b.object + B195_F_HAS_BLOCK_ENTITY, &one, 1) ||
            !is(m, b.object + B195_F_ENTITY_VPTR, ev) || !rd(m, b.object + B195_F_HAS_BLOCK_ENTITY, &flag, 1) || flag != 1)
            return "storage block entity layout";
    }
    if (!wr(m, b.object, &mine, 8) || !is(m, b.object, mine)) return "private vtable";
    return nullptr;
}

int publish_blocks(b195_module *m) {
    if (m->state != B195_CONFIGURED || m->blocks_state) return 0;
    if (!blocks_held(m)) return refuse_blocks(m, "block phase not held");
    const uintptr_t B = m->base;
    uintptr_t reg = 0;
    // P1: registry identity and pins (reads only)
    if (!q(m, B + B195_RVA_REGISTRY_GLOBAL, &reg) || !reg || (reg & 7) || reg > UINTPTR_MAX - 0xcc0) return refuse_blocks(m, "block registry absent");
    if (!is(m, reg, B + B195_RVA_REGISTRY_VTABLE)) return refuse_blocks(m, "block registry vptr is not stock");
    for (unsigned i = 0; i < B195_REGISTRY_SLOTS; ++i)
        if (!is(m, B + B195_RVA_REGISTRY_VTABLE + 8 * i, B + k_registry_slots[i])) return refuse_blocks(m, "registry vtable pin");
    uintptr_t ore[2 + B195_ORE_SLOTS];
    if (!rd(m, B + B195_RVA_ORE_VTABLE - 16, ore, sizeof ore) || ore[0] || ore[1] != B + B195_RVA_ORE_RTTI) return refuse_blocks(m, "OreBlock vtable header pin");
    for (auto &p : k_ore_pins) if (ore[2 + p[0] / 8] != B + p[1]) return refuse_blocks(m, "OreBlock vtable slot pin");
    if (!code(m, B195_RVA_ORE_CTOR, k_ore_ctor, sizeof k_ore_ctor) || !code(m, B195_RVA_REGISTER, k_pro_register, 16) ||
        !code(m, B195_RVA_BLOCK_CTOR, k_pro_block_ctor, 16) || !code(m, B195_RVA_BLOCK_INIT_STATES, k_pro_init_states, sizeof k_pro_init_states))
        return refuse_blocks(m, "native code pin");
    bool variants = false;
    for (uint32_t i = 0; i < m->count; ++i) variants |= m->blocks[i].variants != 0;
    if (variants && (!code(m, 0x7a040u, k_register_default, sizeof k_register_default) || !code(m, 0x1a9708u, k_colored_calls[0], 5) ||
                     !code(m, 0x1a9714u, k_colored_calls[1], 5) || !code(m, 0x1a9779u, k_colored_calls[2], 5)))
        return refuse_blocks(m, "ColoredBlock default-state pin (0x1a96d0 / 0x7a040)");
    if (!fpin(m, B195_RVA_FLOAT_15, 15.0f) || !fpin(m, B195_RVA_FLOAT_3, 3.0f) || !fpin(m, B195_RVA_FLOAT_5, 5.0f)) return refuse_blocks(m, "setter constant pin");
    // P2: stock registration complete, our slots vacant
    if (!rd(m, reg + B195_REGISTRY_SLOTS_OFF, m->stock_slots, sizeof m->stock_slots)) return refuse_blocks(m, "registry slots unreadable");
    unsigned occupied = 0;
    for (unsigned i = 0; i <= B195_BLOCK_ID_MAX; ++i) occupied += m->stock_slots[i] != 0;
    if (!m->stock_slots[B195_BLOCK_ID_MAX] || !is(m, m->stock_slots[B195_BLOCK_ID_MAX], B + B195_RVA_JIGSAW_VTABLE) || occupied < B195_STOCK_BLOCKS)
        return refuse_blocks(m, "stock block registration not complete (id 365 / 345 blocks)");
    for (uint32_t i = 0; i < m->count; ++i) {
        b195_block &b = m->blocks[i];
        uintptr_t mat = 0, snd = 0;
        if (m->stock_slots[b.id]) return refuse_blocks(m, "block id occupied");
        if (!q(m, B + b.material_global, &mat) || !mat || !q(m, B + b.sound_global, &snd) || !snd) return refuse_blocks(m, "material/sound global empty");
    }
    // P3: keys (plugin-owned storage only), then key vacancy
    for (uint32_t i = 0; i < m->count; ++i) {
        b195_block &b = m->blocks[i];
        make_wstring(m, b.key_text, b.desc->key);
        memset(b.key, 0, sizeof b.key);
        m->n.key_ctor(b.key, b.key_text);
        if (!key_matches(m, b)) return refuse_blocks(m, "ResourceLocation did not parse as ns:path");
        if (m->n.reg_present(reg, b.key)) return refuse_blocks(m, "block key already registered");
        for (uint32_t j = 0; j < i; ++j) if (m->blocks[j].id == b.id) return refuse_blocks(m, "duplicate id");
    }
    uintptr_t si_vptr = 0;
    bool any_container = false, any_chest = false;
    for (uint32_t i = 0; i < m->count; ++i) any_chest |= m->blocks[i].desc->block_entity_slots == B195_CHEST_STORAGE_SLOTS;
    for (uint32_t i = 0; i < m->count; ++i) any_container |= m->blocks[i].desc->block_entity_slots == B195_CONTAINER_SLOTS;
    if (any_container && !container_pins(m, &si_vptr)) return refuse_blocks(m, "DispenserBlock/BaseEntityBlock layout pin (storage blocks)");
    if (any_chest && !chest_pins(m)) return refuse_blocks(m, "ChestBlock layout pin (27-slot storage blocks)");
    if ((any_container || any_chest) && !title_pins(m, any_chest))
        return refuse_blocks(m, "storage title / ChestBlockEntity render pin (setCustomName 0x597b30, BE vtable 0x1225618, cull 0xb2ba35)");
    bool any_variant = false, any_shape = false;
    for (uint32_t i = 0; i < m->count; ++i) { any_variant |= m->blocks[i].variants != 0; any_shape |= special(m->blocks[i]); }
    if (any_shape) {
        for (auto &p : k_ore_state_pins) if (ore[2 + p[0] / 8] != B + p[1]) return refuse_blocks(m, "OreBlock state/shape slot pin");
        if (!is(m, B + B195_RVA_FLOWER_VTABLE + 0x1c8, B + B195_RVA_NO_COLLISION) || !code(m, B195_RVA_NO_COLLISION, k_ret_false, 3) ||
            !code(m, B195_RVA_RET_FALSE, k_ret_false, 3) || !code(m, B195_RVA_BOX, k_box, sizeof k_box) || !code(m, B195_RVA_DIR_INDEX, k_dir_index, 4))
            return refuse_blocks(m, "shape native pin (0x170 box / 0x929d0 / FlowerBlock +0x1c8 / Direction +8)");
        for (auto &p : k_glass_slots) if (!is(m, B + B195_RVA_GLASS_VTABLE + p[0], B + p[1])) return refuse_blocks(m, "GlassBlock slot pin");
        if (!code(m, 0x333d60, k_ret_true, 3) || !code(m, 0x333d70, k_ret_true, 3) || !code(m, 0x333d90, k_ret_false, 3) || !code(m, 0x353450, k_ret_false, 3))
            return refuse_blocks(m, "GlassBlock code pin");
    }
#ifdef LCE_HAVE_BLOCK_BOXES_1_11
    bool any_boxes = false;
    for (uint32_t i = 0; i < m->count; ++i) any_boxes |= m->blocks[i].box_count != 0;
    if (any_boxes) {   // the StairBlock slots we copy, the OreBlock slots we replace, and the stock consumers of +0x190
        for (auto &p : k_ore_box_pins) if (ore[2 + p[0] / 8] != B + p[1]) return refuse_blocks(m, "OreBlock box-list slot pin");
        int32_t rel = 0;
        if (!is(m, B + B195_RVA_STAIR_VTABLE + 0xe8, B + B195_RVA_STAIR_RENDER_SHAPE) || !is(m, B + B195_RVA_STAIR_VTABLE + 0x1b0, B + B195_RVA_STAIR_COLLISION) ||
            !code(m, B195_RVA_STAIR_RENDER_SHAPE, k_stair_shape, sizeof k_stair_shape) || !code(m, B195_RVA_STAIR_COLLISION, k_stair_collision, sizeof k_stair_collision) ||
            !code(m, 0x676605, k_stair_collision_list, 6) || !code(m, 0x67663d, k_stair_collision_add, 6) || !code(m, 0xb3b193, k_tess_list, 6) ||
            !rd(m, B + B195_RVA_TESS_SHAPE10_CALL + 1, &rel, 4) || B195_RVA_TESS_SHAPE10_CALL + 5 + rel != B195_RVA_TESS_MULTIBOX)
            return refuse_blocks(m, "multi-box pin (StairBlock +0xe8/+0x1b0, render shape 10 -> 0xb3b160)");
    }
#endif
    if (any_variant) {
        for (unsigned k = 0; k < 4; ++k)
            if (!is(m, B + B195_RVA_COLORED_VTABLE + k_colored_slots[k][0], B + k_colored_slots[k][1]) || !code(m, k_colored_slots[k][1], k_colored_code[k], 16))
                return refuse_blocks(m, "ColoredBlock state slot pin");
        if (!code(m, B195_RVA_FACE_GETTER, k_face_getter, 16)) return refuse_blocks(m, "face texture getter pin");
    }
    if (!blocks_held(m)) return refuse_blocks(m, "block phase lost");
    m->registry = reg;
    // C: construct (native calls on fresh objects); any failure from here is uncertain
    m->blocks_state = 1;
    for (uint32_t i = 0; i < m->count; ++i) {
        b195_block &b = m->blocks[i];
        const char *why = construct_one(m, b, ore, si_vptr);
        if (!why) continue;
        // console 2026-11-19: one variant check failing used to kill every SDK block (all of them are refused unregistered)
        b.dropped = 1; b.object = 0; ++m->dropped;
        snprintf(b.drop_reason, sizeof b.drop_reason, "%.95s", why);
        char e[sizeof m->last_error];
        snprintf(e, sizeof e, "block %.48s dropped: %.95s", b.desc && b.desc->key ? b.desc->key : "?", why);
        fail(m, e);
    }
    if (m->dropped == m->count) return uncertain_blocks(m, m->last_error);
    // R: register through the stock registry method (0xb8090)
    for (uint32_t i = 0; i < m->count; ++i) {
        b195_block &b = m->blocks[i];
        if (b.dropped) continue;
        if (!blocks_held(m) || !is(m, reg + B195_REGISTRY_SLOTS_OFF + 8 * b.id, 0) || m->n.reg_present(reg, b.key))
            return uncertain_blocks(m, "slot or key taken before registration");
        m->n.reg_register(reg, b.id, b.key, b.object);
        uint32_t id = 0;
        if (!is(m, reg + B195_REGISTRY_SLOTS_OFF + 8 * b.id, b.object) || !rd(m, b.object + B195_F_ID, &id, 4) || id != b.id ||
            m->n.reg_present(reg, b.key) != 1 || m->n.reg_by_key(reg, b.key) != b.object)
            return uncertain_blocks(m, "registration check");
    }
    for (unsigned i = 0; i <= B195_BLOCK_ID_MAX; ++i) {
        uintptr_t want = m->stock_slots[i];
        for (uint32_t j = 0; j < m->count; ++j) if (m->blocks[j].id == i && !m->blocks[j].dropped) want = m->blocks[j].object;
        if (!is(m, reg + B195_REGISTRY_SLOTS_OFF + 8 * i, want)) return uncertain_blocks(m, "stock registry slot changed");
    }
    m->blocks_state = B195_BLOCKS_DONE;
    m->state = B195_BLOCKS_DONE;
    return 1;
}

// ---- items phase ----------------------------------------------------------
int refuse_items(b195_module *m, const char *why) {
    // Blocks are already live: a refused items phase is still a partial publication.
    fail(m, why); m->items_state = B195_REFUSED; m->state = B195_UNCERTAIN; return 0;
}
int uncertain_items(b195_module *m, const char *why) { fail(m, why); m->items_state = B195_UNCERTAIN; m->state = B195_UNCERTAIN; return -1; }

}  // namespace

extern "C" int b195_items_publish(b195_module *m) {
    if (!m || m->state != B195_BLOCKS_DONE || m->items_state) return 0;
    if (!items_held(m)) return refuse_items(m, "items window not held");
    const uintptr_t B = m->base;
    uintptr_t bi[2 + B195_BLOCK_ITEM_SLOTS];
    if (!rd(m, B + B195_RVA_BLOCK_ITEM_VTABLE - 16, bi, sizeof bi) || bi[0] || !bi[1] || bi[2 + 0x108 / 8] != B + B195_RVA_ITEM_NAME)
        return refuse_items(m, "BlockItem vtable pin");
    if (!code(m, B195_RVA_BLOCK_ITEM_CTOR, k_pro_block_item, 16) || !code(m, B195_RVA_ITEM_REGISTER, k_pro_item_register, 16) ||
        !code(m, B195_RVA_BLOCK_ITEM_MAP_SLOT, k_pro_map_slot, 16))
        return refuse_items(m, "item native code pin");
    if (!item_header(m, m->item_header) || !rd(m, m->item_header[0], m->stock_items, live_items(m) * 8)) return refuse_items(m, "Item::items header");
    bool any_variant = false;
    for (uint32_t i = 0; i < m->count; ++i) any_variant |= m->blocks[i].variants && m->blocks[i].item_id;
    if (any_variant && (bi[2 + 0x58 / 8] != B + 0x3aeea0u || !is(m, B + B195_RVA_MULTI_ITEM_VTABLE + 0x58, B + B195_RVA_MULTI_LEVEL_DATA) ||
                        !code(m, B195_RVA_MULTI_LEVEL_DATA, k_level_data, 3) || !code(m, B195_RVA_SET_STACKED, k_set_stacked, 8) ||
                        !code(m, B195_RVA_STACK_AUX, k_stack_aux, 4)))
        return refuse_items(m, "variant block-item pin (BlockItem/MultiBlockItem +0x58, stacked-by-data +0x31, aux +0x3c)");
    uintptr_t fire = 0;
    bool any_fire = false;
    for (uint32_t i = 0; i < m->count; ++i) any_fire |= m->blocks[i].flammable != 0;
    if (any_fire) {
        if (!code(m, B195_RVA_FIRE_ODDS_SLOT, k_pro_fire_slot, 16) || !q(m, B + B195_RVA_FIRE_GLOBAL, &fire) || !fire ||
            !is(m, fire, B + B195_RVA_FIRE_VTABLE) || !is(m, m->registry + B195_REGISTRY_SLOTS_OFF + 8 * 51, fire))
            return refuse_items(m, "FireBlock identity (Blocks::FIRE / slot 51)");
    }
    for (uint32_t i = 0; i < m->count; ++i) {
        b195_block &b = m->blocks[i];
        if (b.dropped) continue;   // never registered (construction failed)
        uint32_t id = 0;
        if (!is(m, B + B195_RVA_REGISTRY_GLOBAL, m->registry) || !is(m, m->registry + B195_REGISTRY_SLOTS_OFF + 8 * b.id, b.object) ||
            !is(m, b.object, (uintptr_t)&b.vtable[2]) || !rd(m, b.object + B195_F_ID, &id, 4) || id != b.id)
            return refuse_items(m, "published block changed");
        if (!b.item_id) continue;
        unsigned matches = 0;
        if (b.item_id >= live_items(m) || m->stock_items[b.item_id] || m->n.item_by_id(b.item_id) ||
            m->n.item_present(B + B195_RVA_ITEM_REGISTRY, b.key) || !map_count(m, b.object, 0, &matches) || matches)
            return refuse_items(m, "block-item slot, key or map entry occupied");
        if (b.drop_override) {
            bool ours = false;
            for (uint32_t j = 0; j < m->count; ++j) ours |= m->blocks[j].item_id && !m->blocks[j].dropped && (int32_t)m->blocks[j].item_id == b.drop_item;
            if (!ours && !m->n.item_by_id((unsigned)b.drop_item)) return refuse_items(m, "drop item not registered");
        }
    }
    if (!items_held(m)) return refuse_items(m, "items window lost");
    m->items_state = 1;
    for (uint32_t i = 0; i < m->count; ++i) {
        b195_block &b = m->blocks[i];
        if (b.dropped) continue;   // never registered (construction failed)
        if (!b.item_id) continue;
        void *it = m->n.alloc(B195_BLOCK_ITEM_SIZE);
        if (!it || ((uintptr_t)it & 15)) return uncertain_items(m, "BlockItem allocation");
        m->n.block_item_ctor(it, b.object);
        b.item = (uintptr_t)it;
        uintptr_t vt = 0, blk = 0, spr = 1;
        uint32_t max = 0, dmg = 1;
        if (!q(m, b.item, &vt) || vt != B + B195_RVA_BLOCK_ITEM_VTABLE || !q(m, b.item + 0xa0, &blk) || blk != b.object ||
            !q(m, b.item + 0xa8, &spr) || spr || !rd(m, b.item + 0x18, &max, 4) || max != 64 || !rd(m, b.item + 0x1c, &dmg, 4) || dmg)
            return uncertain_items(m, "BlockItem construction check");
        if (m->n.item_by_id(b.item_id) || m->n.item_present(B + B195_RVA_ITEM_REGISTRY, b.key)) return uncertain_items(m, "item slot taken before registration");
        m->n.item_register(B + B195_RVA_ITEM_REGISTRY, b.item_id, b.key, b.item);
        if (m->n.item_by_id(b.item_id) != b.item || m->n.item_present(B + B195_RVA_ITEM_REGISTRY, b.key) != 1 ||
            !is(m, m->item_header[0] + 8 * b.item_id, b.item))
            return uncertain_items(m, "item registration check");
        uintptr_t key_block = b.object;
        uintptr_t slot = m->n.map_slot(B + B195_RVA_BLOCK_ITEM_MAP, &key_block);
        unsigned matches = 0;
        if (!slot || (slot & 7) || m->mem.cas(m->mem.ctx, slot, 0, b.item) != 1 || !map_count(m, b.object, b.item, &matches) || matches != 1)
            return uncertain_items(m, "block->item map");
        memcpy(b.item_vtable, bi, sizeof bi);
        b.item_vtable[2 + 0x108 / 8] = (uintptr_t)&b195_item_name_dispatch;
        if (b.variants) {   // MultiBlockItem pattern: level data = aux, stacks keep their aux (setStackedByData)
            b.item_vtable[2 + 0x58 / 8] = B + B195_RVA_MULTI_LEVEL_DATA;
            uint8_t stacked = 1, one = 1;
            if (!rd(m, b.item + B195_F_ITEM_STACKED, &stacked, 1) || stacked || !wr(m, b.item + B195_F_ITEM_STACKED, &one, 1) ||
                !rd(m, b.item + B195_F_ITEM_STACKED, &stacked, 1) || stacked != 1)
                return uncertain_items(m, "variant block-item stacked-by-data");
        }
        if (m->mem.cas(m->mem.ctx, b.item, B + B195_RVA_BLOCK_ITEM_VTABLE, (uintptr_t)&b.item_vtable[2]) != 1 || !is(m, b.item, (uintptr_t)&b.item_vtable[2]))
            return uncertain_items(m, "BlockItem private vtable");
    }
    for (uint32_t i = 0; i < m->count; ++i) {
        b195_block &b = m->blocks[i];
        if (b.dropped) continue;   // never registered (construction failed)
        if (b.drop_override) {
            b.drop_ptr = m->n.item_by_id((unsigned)b.drop_item);
            if (!b.drop_ptr) return uncertain_items(m, "drop item lookup");
        }
        if (b.flammable) {
            // FireBlock::init 0x2ae7f0: operator[](fire+0x1e8,&block)=encouragement; operator[](fire+0x238,&block)=flammability
            int32_t flam = b.desc->flammability, enc = flam / 4 ? flam / 4 : 1;
            uintptr_t key_block = b.object;
            int *e = m->n.fire_slot(fire + B195_FIRE_ENCOURAGE_OFF, &key_block);
            if (!e || !wr(m, (uintptr_t)e, &enc, 4)) return uncertain_items(m, "fire encouragement");
            int *f = m->n.fire_slot(fire + B195_FIRE_FLAMMABLE_OFF, &key_block);
            if (!f || !wr(m, (uintptr_t)f, &flam, 4)) return uncertain_items(m, "fire flammability");
            b.fire_object = fire;
        }
    }
    uintptr_t h[3];
    if (!item_header(m, h) || memcmp(h, m->item_header, sizeof h)) return uncertain_items(m, "Item::items header changed");
    for (uint32_t i = 0; i < live_items(m); ++i) {
        uintptr_t want = m->stock_items[i];
        for (uint32_t j = 0; j < m->count; ++j) if (m->blocks[j].item_id == i && m->blocks[j].item_id && !m->blocks[j].dropped) want = m->blocks[j].item;
        if (!is(m, h[0] + 8 * i, want)) return uncertain_items(m, "stock Item::items entry changed");
    }
    m->items_state = B195_ITEMS_DONE;
    m->state = B195_ITEMS_DONE;
    return 1;
}

extern "C" int b195_blocks_publish(b195_module *m) { return m ? publish_blocks(m) : 0; }

// ---- data hook (W1) ---------------------------------------------------------
extern "C" int b195_hook_install(b195_module *m) {
    if (!m || m->state != B195_CONFIGURED || m->hook_state != B195_HOOK_NONE || !attested(m)) return 0;
    const uintptr_t B = m->base, stock = B + B195_RVA_REGISTRY_VTABLE;
    uintptr_t reg = 0;
    if (!q(m, B + B195_RVA_REGISTRY_GLOBAL, &reg) || (reg & 7)) { fail(m, "registry global unreadable"); return 0; }
    if (!reg && !m->patch_ro8) { fail(m, "registry not constructed yet (static ctor 0x150cd0); retry later"); return 0; }
    if (reg && !is(m, reg, stock)) { fail(m, "registry vptr is not stock"); return 0; }
    uintptr_t copy[2 + B195_REGISTRY_SLOTS];
    if (!rd(m, stock - 16, copy, sizeof copy) || copy[0] || copy[1] != B + B195_RVA_REGISTRY_RTTI) { fail(m, "registry vtable header pin"); return 0; }
    for (unsigned i = 0; i < B195_REGISTRY_SLOTS; ++i) if (copy[2 + i] != B + k_registry_slots[i]) { fail(m, "registry vtable slot pin"); return 0; }
    if (!code(m, B195_RVA_REGISTER, k_pro_register, 16)) { fail(m, "register code pin"); return 0; }
    if (!reg) {
        // W1b: the registry object does not exist yet (plugins load before the static constructors). Patch the
        // class vtable's register slot (RELRO); the relay restores it at id 365. Every registration still goes to
        // the stock 0xb8090 first. Expected = the relocated stock value, so an unrelocated image refuses.
        const uintptr_t slot = stock + 8 * B195_REGISTRY_REGISTER_SLOT;
        m->hook_registry = 0; m->hook_stock_vtable = stock; m->hook_original_register = copy[2 + B195_REGISTRY_REGISTER_SLOT];
        m->hook_mode = B195_HOOK_MODE_SLOT;
        __atomic_store_n(&m->hook_state, (uint32_t)B195_HOOK_ARMED, __ATOMIC_RELEASE);
        const int rc = m->patch_ro8(m->patch_ro8_ctx, slot, B + B195_RVA_REGISTER, (uintptr_t)&b195_register_relay);
        if (rc == 1) return 1;
        __atomic_store_n(&m->hook_state, (uint32_t)B195_HOOK_NONE, __ATOMIC_RELEASE);
        if (rc == 0) { fail(m, "registry vtable register slot changed (not stock)"); return 0; }
        m->state = B195_UNCERTAIN; fail(m, "registry vtable slot: RELRO protection not restored"); return -1;
    }
    m->hook_mode = B195_HOOK_MODE_VPTR;
    if (!is(m, reg + B195_REGISTRY_SLOTS_OFF + 8 * B195_BLOCK_ID_MAX, 0)) { fail(m, "stock block init already past id 365"); return 0; }
    for (uint32_t i = 0; i < m->count; ++i)
        if (!is(m, reg + B195_REGISTRY_SLOTS_OFF + 8 * m->blocks[i].id, 0)) { fail(m, "SDK block id already occupied"); return 0; }
    memcpy(m->reg_vtable, copy, sizeof copy);
    m->reg_vtable[2 + B195_REGISTRY_REGISTER_SLOT] = (uintptr_t)&b195_register_relay;
    m->hook_registry = reg; m->hook_stock_vtable = stock; m->hook_original_register = copy[2 + B195_REGISTRY_REGISTER_SLOT];
    __atomic_store_n(&m->hook_state, (uint32_t)B195_HOOK_ARMED, __ATOMIC_RELEASE);
    if (m->mem.cas(m->mem.ctx, reg, stock, (uintptr_t)&m->reg_vtable[2]) != 1) {
        __atomic_store_n(&m->hook_state, (uint32_t)B195_HOOK_NONE, __ATOMIC_RELEASE);
        fail(m, "registry vptr changed under us");
        return 0;
    }
    if (!is(m, reg, (uintptr_t)&m->reg_vtable[2])) { fail(m, "registry vptr readback"); return -1; }
    return 1;
}

extern "C" void b195_set_patch_ro8(b195_module *m, int (*fn)(void *, uintptr_t, uint64_t, uint64_t), void *ctx) {
    if (m && m->hook_state == B195_HOOK_NONE) { m->patch_ro8 = fn; m->patch_ro8_ctx = ctx; }
}

extern "C" int b195_hook_disarm(b195_module *m) {
    if (!m) return 0;
    uint32_t s = __atomic_load_n(&m->hook_state, __ATOMIC_ACQUIRE);
    if (s != B195_HOOK_ARMED) return 1;
    uint32_t armed = B195_HOOK_ARMED;
    if (!__atomic_compare_exchange_n(&m->hook_state, &armed, (uint32_t)B195_HOOK_DISARMED, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return 1;
    if (m->hook_mode == B195_HOOK_MODE_SLOT) {
        const uintptr_t slot = m->hook_stock_vtable + 8 * B195_REGISTRY_REGISTER_SLOT;
        const int rc = m->patch_ro8(m->patch_ro8_ctx, slot, (uintptr_t)&b195_register_relay, m->hook_original_register);
        uintptr_t reg = 0, s365 = 0;
        if (rc == 1 && q(m, m->base + B195_RVA_REGISTRY_GLOBAL, &reg) && reg && q(m, reg + B195_REGISTRY_SLOTS_OFF + 8 * B195_BLOCK_ID_MAX, &s365) && s365)
            __atomic_store_n(&m->hook_state, (uint32_t)B195_HOOK_MISSED, __ATOMIC_RELEASE);
        if (rc == 1) return 1;
        uintptr_t cur = 0;
        return q(m, slot, &cur) && cur == m->hook_original_register ? 1 : -1;
    }
    if (m->mem.cas(m->mem.ctx, m->hook_registry, (uintptr_t)&m->reg_vtable[2], m->hook_stock_vtable) == 1) {
        uintptr_t s365 = 0;
        if (q(m, m->hook_registry + B195_REGISTRY_SLOTS_OFF + 8 * B195_BLOCK_ID_MAX, &s365) && s365)
            __atomic_store_n(&m->hook_state, (uint32_t)B195_HOOK_MISSED, __ATOMIC_RELEASE);
        return 1;
    }
    return is(m, m->hook_registry, m->hook_stock_vtable) ? 1 : -1;
}

extern "C" void b195_register_relay(uintptr_t reg, uint32_t id, const void *key, uintptr_t block) {
    b195_module *m = __atomic_load_n(&g_m, __ATOMIC_ACQUIRE);
    // Always forward first: the stock register 0xb8090 (pinned == copy slot +0x28).
    m->n.reg_register(reg, id, key, block);
    if (id != B195_BLOCK_ID_MAX) return;
    const bool slot_mode = m->hook_mode == B195_HOOK_MODE_SLOT;
    if (slot_mode) {
        // the class slot is shared by every object of the class: only the block registry global's id 365 fires
        uintptr_t global = 0;
        if (!q(m, m->base + B195_RVA_REGISTRY_GLOBAL, &global) || reg != global || !is(m, reg, m->hook_stock_vtable)) return;
    } else if (reg != m->hook_registry) {
        return;
    }
    uint32_t armed = B195_HOOK_ARMED;
    if (!__atomic_compare_exchange_n(&m->hook_state, &armed, (uint32_t)B195_HOOK_FIRED, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return;
    // One-shot: give the registry (object vptr or class slot) its stock state back before anything else.
    if (slot_mode) {
        m->hook_registry = reg;
        const uintptr_t slot = m->hook_stock_vtable + 8 * B195_REGISTRY_REGISTER_SLOT;
        if (m->patch_ro8(m->patch_ro8_ctx, slot, (uintptr_t)&b195_register_relay, m->hook_original_register) != 1) {
            m->hook_publish_result = -1; m->state = B195_UNCERTAIN; fail(m, "registry vtable slot restore failed"); return;
        }
    } else if (m->mem.cas(m->mem.ctx, reg, (uintptr_t)&m->reg_vtable[2], m->hook_stock_vtable) != 1) {
        m->hook_publish_result = -1; m->state = B195_UNCERTAIN; fail(m, "registry vptr restore failed"); return;
    }
    uint64_t t = tid(m);
    m->phase_thread = t;
    __atomic_store_n(&m->phase, 1u, __ATOMIC_RELEASE);
    m->hook_publish_result = t ? publish_blocks(m) : refuse_blocks(m, "no thread id in relay");
    __atomic_store_n(&m->phase, 0u, __ATOMIC_RELEASE);
    m->phase_thread = 0;
}

// ---- dispatchers -------------------------------------------------------------
extern "C" void *b195_block_name_dispatch(void *out, void *block) {
    b195_module *m = __atomic_load_n(&g_m, __ATOMIC_ACQUIRE);
    if (!m || !out) return nullptr;
    if (b195_block *b = find_block(m, (uintptr_t)block)) {
        // empty SSO wstring before the native assign (lucky block_names.cpp)
        *(uint16_t *)((uintptr_t)out + 8) = 0; *(uint64_t *)((uintptr_t)out + 0x18) = 0; *(uint64_t *)((uintptr_t)out + 0x20) = 7;
        m->n.wstring(out, b->name, b->name_len);
        return out;
    }
    return m->n.block_name(out, block);
}

extern "C" uintptr_t b195_get_resource_dispatch(void *block, uintptr_t state, uintptr_t random, int fortune) {
    b195_module *m = __atomic_load_n(&g_m, __ATOMIC_ACQUIRE);
    b195_block *b = find_block(m, (uintptr_t)block);
    if (b && b->drop_override && b->drop_ptr) return b->drop_ptr;
    return m->n.get_resource(block, state, random, fortune);
}

extern "C" int b195_resource_count_dispatch(void *block, int fortune, uintptr_t random) {
    b195_module *m = __atomic_load_n(&g_m, __ATOMIC_ACQUIRE);
    b195_block *b = find_block(m, (uintptr_t)block);
    if (b && b->count_override && random) {
        int lo = b->desc->drop_min, hi = b->desc->drop_max;
        return lo + (hi > lo ? m->n.next_int(random, hi - lo + 1) : 0);
    }
    return m->n.resource_count(block, fortune, random);
}

extern "C" void *b195_item_name_dispatch(void *out, void *item, void *stack) {
    b195_module *m = __atomic_load_n(&g_m, __ATOMIC_ACQUIRE);
    if (!m || !out) return nullptr;
    for (uint32_t i = 0; i < m->count; ++i) {
        const b195_block &b = m->blocks[i];
        if (!b.item || b.item != (uintptr_t)item) continue;
        *(uint16_t *)((uintptr_t)out + 8) = 0; *(uint64_t *)((uintptr_t)out + 0x18) = 0; *(uint64_t *)((uintptr_t)out + 0x20) = 7;
        int32_t aux = (b.variants && stack) ? *(const int32_t *)((uintptr_t)stack + B195_F_STACK_AUX) : 0;   // getAuxValue 0x3557c0
        if (aux > 0 && aux < (int32_t)b.variants && b.vname_len[aux]) m->n.wstring(out, b.vname[aux], b.vname_len[aux]);
        else m->n.wstring(out, b.name, b.name_len);
        return out;
    }
    return m->n.item_name(out, item, stack);
}

// ---- storage dispatchers: newBlockEntity wrapper (title, chest-model cull) and the culling view distance ----
extern "C" void *b195_new_block_entity_dispatch(void *out, void *entity_block, void *level, int data) {
    b195_module *m = __atomic_load_n(&g_m, __ATOMIC_ACQUIRE);
    b195_block *b = m ? find_block(m, (uintptr_t)entity_block - B195_F_ENTITY_VPTR) : nullptr;
    if (!b || !b->slots) {   // unreachable: only storage blocks carry this table; hand back an empty shared_ptr
        if (out) memset(out, 0, 16);
        return out;
    }
    const bool chest = b->slots == B195_CHEST_STORAGE_SLOTS;
    (chest ? m->n.chest_new_be : m->n.disp_new_be)(out, entity_block, level, data);
    const uintptr_t be = out ? *(const uintptr_t *)out : 0;
    if (!be) return out;
    if (b->name_len && *(const uintptr_t *)(be + B195_F_BE_NAME)) m->n.set_custom_name((void *)be, b->title_text, b->empty_text, 0, 1);
    if (chest && *(const uintptr_t *)be == m->base + B195_RVA_CHEST_BE_VTABLE)
        *(uintptr_t *)be = (uintptr_t)&b->be_vtable[B195_CHEST_BE_PREFIX];
    return out;
}

extern "C" double b195_be_view_distance(void *) { return -1.0; }   // below any distanceToSqr: 0xb2b9d0 never renders the BE

// ---- API 1.7 dispatchers (variants and shapes) ----------------------------------
extern "C" uintptr_t b195_state_from_data_dispatch(void *block, int data) {
    b195_module *m = __atomic_load_n(&g_m, __ATOMIC_ACQUIRE);
    b195_block *b = m ? find_block(m, (uintptr_t)block) : nullptr;
    if (b && b->variants && ((unsigned)data >= b->variants)) data = 0;   // data values past the mod's variants map to variant 0
    return m ? m->n.state_from_data(block, data) : 0;
}

extern "C" uintptr_t b195_face_sprite_dispatch(void *block, uintptr_t face, uintptr_t state) {
    b195_module *m = __atomic_load_n(&g_m, __ATOMIC_ACQUIRE);
    if (!m) return 0;
    b195_block *b = find_block(m, (uintptr_t)block);
    if (b && b->variants && face && state && __atomic_load_n(&m->terrain_state, __ATOMIC_ACQUIRE) == 2 && m->generations) {
        int f = *(const int32_t *)(face + 8);                // Direction::get3DDataValue 0x200600
        int v = m->n.data_from_state(block, state);
        if (f >= 0 && f < 6 && v > 0 && v < (int)b->variants) {
            uintptr_t s = m->sprites[m->generations - 1][b->vtile[v][f]];
            if (s) return s;
        }
    }
    return m->n.face_sprite(block, face, state);
}

namespace {
uint32_t box_set(b195_module *m, const b195_block *b, void *block, uintptr_t state) {
    if (b->box_sets < 2 || !state || !m->n.data_from_state) return 0;
    int v = m->n.data_from_state(block, state);   // ColoredBlock +0x50 0x1a9900 (variant blocks only)
    return v >= 0 && (uint32_t)v < b->box_sets ? (uint32_t)v : 0;
}
}
const struct { uintptr_t data; uint32_t count, pad; } k_no_boxes = {0, 0, 0};
extern "C" uintptr_t b195_box_list_dispatch(void *block, uintptr_t state, uintptr_t, uintptr_t) {
    b195_module *m = __atomic_load_n(&g_m, __ATOMIC_ACQUIRE);
    b195_block *b = m ? find_block(m, (uintptr_t)block) : nullptr;
    if (!b || !b->box_count) return (uintptr_t)&k_no_boxes;   // never for our vtables: every consumer handles count 0
    return (uintptr_t)&b->box_list[box_set(m, b, block, state)];
}

extern "C" uintptr_t b195_shape_dispatch(void *block, uintptr_t state, uintptr_t, uintptr_t) {
    b195_module *m = __atomic_load_n(&g_m, __ATOMIC_ACQUIRE);
    b195_block *b = m ? find_block(m, (uintptr_t)block) : nullptr;
    if (b && b->box_count) { uintptr_t a = b->union_aabb[box_set(m, b, block, state)]; if (a) return a; }
    if (b && b->aabb) return b->aabb;
    return m ? *(const uintptr_t *)(m->base + B195_RVA_FULL_SHAPE) : 0;   // Block +0x178 0x92b40: the full-cube AABB
}

extern "C" int b195_face_shape_dispatch(void *block, uintptr_t, uintptr_t, uintptr_t, uintptr_t face) {
    b195_module *m = __atomic_load_n(&g_m, __ATOMIC_ACQUIRE);
    b195_block *b = m ? find_block(m, (uintptr_t)block) : nullptr;
    if (!b || !face || b->no_collide || !b->shape) return B195_SHAPE_UNDEFINED;
    const int32_t f = *(const int32_t *)(face + 8);   // D U N S W E (Direction 3D data value)
    if (b->box_count) return f >= 0 && f < 6 && (b->solid_mask >> f & 1) ? B195_SHAPE_SOLID : B195_SHAPE_UNDEFINED;
    return face_solid(b->box, f) ? B195_SHAPE_SOLID : B195_SHAPE_UNDEFINED;
}

// ---- configuration ------------------------------------------------------------
extern "C" int b195_check_desc(const lce_block_desc *d, char *err, uint32_t cap) {
    auto no = [&](const char *why) { if (err && cap) snprintf(err, cap, "retail 1.95 blocks: %s", why); return 0; };
    if (!d) return no("null descriptor");
    if (d->block_entity_slots && d->block_entity_slots != B195_CONTAINER_SLOTS && d->block_entity_slots != B195_CHEST_STORAGE_SLOTS)
        return no("storage blocks: block_entity_slots must be 9 (DispenserBlockEntity, 3x3 screen) or 27 (ChestBlockEntity, 3-row chest screen); "
                  "54 is two 27-slot blocks side by side, other sizes need another block entity");
    if (d->block_entity_slots == B195_CHEST_STORAGE_SLOTS) {
        if (d->material != LCE_MAT_WOOD) return no("27-slot storage blocks are ChestBlocks: material must be wood (the ChestBlock ctor fixes it)");
        if (d->flags & (LCE_BLOCK_FLAG_TRANSPARENT | LCE_BLOCK_FLAG_NO_COLLIDE)) return no("27-slot storage blocks are opaque cubes");
#ifdef LCE_HAVE_BLOCK_STATES_1_7
        if (d->struct_size >= offsetof(lce_block_desc, variant_textures) + sizeof(void *) && (d->variant_count >= 2 || d->shape))
            return no("27-slot storage blocks cannot have variants or a box (the ChestBlock FACING state owns +0x478)");
#endif
    }
    if (d->placement != LCE_PLACE_FIXED) return no("facing/axis placement needs a directional block class (not implemented)");
    if (d->flags & LCE_BLOCK_FLAG_REPLACEABLE) return no("replaceable blocks need the material/replace virtuals (not implemented)");
    if (d->material >= LCE_MAT_COUNT) return no("unknown material");
    if (!k_material_ok[d->material] && !(d->material == LCE_MAT_GLASS && (d->flags & LCE_BLOCK_FLAG_TRANSPARENT)) &&
        !(d->material == LCE_MAT_PLANT && (d->flags & LCE_BLOCK_FLAG_NO_COLLIDE)))
        return no("material GLASS needs flag transparent and PLANT needs flag no_collide (an opaque cube of them would not match the material)");
#ifdef LCE_HAVE_BLOCK_STATES_1_7
    if (d->struct_size >= offsetof(lce_block_desc, variant_textures) + sizeof(void *)) {
        if (d->variant_count > B195_MAX_VARIANTS) return no("more than 16 variants (the 1.95 state property has 16 values)");
        if (d->variant_count < 2 && (d->variant_names || d->variant_textures)) return no("variant names/textures without variant_count >= 2");
#ifdef LCE_HAVE_BLOCK_BOXES_1_11
        if (d->shape == LCE_SHAPE_BOXES) {
            if (d->struct_size < offsetof(lce_block_desc, boxes) + sizeof(void *)) return no("shape boxes needs the API 1.11 descriptor");
            if (!d->boxes || d->box_count < 1 || d->box_count > B195_MAX_BOXES) return no("shape boxes: 1..8 boxes");
            if (d->box_sets != 1 && !(d->variant_count >= 2 && d->box_sets == d->variant_count))
                return no("shape boxes: box_sets is 1 (every state) or variant_count (one set per variant)");
            for (uint32_t k = 0; k < (uint32_t)d->box_sets * d->box_count; ++k)
                for (int a = 0; a < 3; ++a)
                    if (d->boxes[k * 6 + a] >= d->boxes[k * 6 + a + 3] || d->boxes[k * 6 + a + 3] > 16) return no("boxes: min < max <= 16 per axis");
        } else if (d->struct_size >= offsetof(lce_block_desc, boxes) + sizeof(void *) && (d->box_count || d->box_sets || d->boxes)) {
            return no("box_count/box_sets/boxes without shape boxes");
        }
        if (d->shape > LCE_SHAPE_BOXES) return no("unknown shape");
#else
        if (d->shape > LCE_SHAPE_BOX) return no("unknown shape");
#endif
        if (d->shape == LCE_SHAPE_BOX) {
            bool full = true;
            for (int a = 0; a < 3; ++a) {
                if (d->box[a] >= d->box[a + 3] || d->box[a + 3] > 16) return no("box: min < max <= 16 per axis");
                full &= d->box[a] == 0 && d->box[a + 3] == 16;
            }
            if (full) return no("box is the full cube (use shape cube)");
        }
        if (d->variant_textures)
            for (int f = 0; f < 6; ++f) if (d->variant_textures[f]) return no("variant_textures row 0 must be empty (variant 0 uses texture[])");
    }
#endif
    if (d->sound >= LCE_SOUND_COUNT) return no("unknown sound");
    if (d->light_emission > 15) return no("light emission above 15");
    if (d->harvest_tier > LCE_TIER_WOOD) return no("harvest tiers above wood are hard-coded per block in the tool classes (not implemented)");
    if (d->harvest_tool) {
        bool rock = d->material == LCE_MAT_STONE || d->material == LCE_MAT_METAL;
        if (!(tool_is(d->harvest_tool, "pickaxe") && rock) && !(tool_is(d->harvest_tool, "axe") && d->material == LCE_MAT_WOOD))
            return no("harvest tool must follow the material (pickaxe: stone/metal, axe: wood)");
    }
    if (d->drop_min > d->drop_max || d->drop_max > 64) return no("drop_min/drop_max out of range");
    if (d->hardness_milli > 1000000u || d->resistance_milli > 10000000u) return no("hardness/resistance out of range");
    if (d->slipperiness_milli > 1000u) return no("slipperiness above 1.0");
    return 1;
}

extern "C" lce_status b195_backend_check_block(void *, const lce_block_desc *d, char *err, uint32_t cap) {
    return b195_check_desc(d, err, cap) ? LCE_OK : LCE_E_UNSUPPORTED;
}

extern "C" int b195_configure(b195_module *m, uintptr_t base, const b195_natives *n, const b195_memory *mem,
                              const lce_plan_block *blocks, uint32_t count, const lce_plan_texture *textures, uint32_t tcount,
                              const char *const *names) {
    if (!m) return 0;
    memset(m, 0, sizeof *m);
    m->state = B195_REFUSED;
    if (!base || !n || !mem) { fail(m, "missing base/natives/memory"); return 0; }
    m->base = base; m->n = *n; m->mem = *mem;
    const void *const *np = (const void *const *)n;
    for (size_t i = 0; i < sizeof *n / sizeof(void *); ++i) if (!np[i]) { fail(m, "natives not bound"); return 0; }
    if (!mem->read || !mem->write || !mem->cas || !mem->thread_id || !mem->attested || !mem->items_held) { fail(m, "memory services not bound"); return 0; }
    if (!blocks || !count || count > B195_MAX_BLOCKS) { fail(m, "1..19 blocks"); return 0; }
    for (uint32_t i = 0; i < count; ++i) {
        const lce_plan_block &p = blocks[i];
        b195_block &b = m->blocks[i];
        char e[160];
        if (!p.desc || !b195_check_desc(p.desc, e, sizeof e)) { fail(m, p.desc ? e : "null descriptor"); return 0; }
        bool allowed = false;
        for (uint32_t a : k_allow) allowed |= a == p.id;
        if (!allowed) { fail(m, "block id outside the live-probed allowlist"); return 0; }
        uint32_t mapped = p.id < 256 ? p.id : p.id + 768;
        if ((p.desc->flags & LCE_BLOCK_FLAG_NO_ITEM) ? p.item_id != 0 : p.item_id != mapped) { fail(m, "block-item id must be id or id+768"); return 0; }
        if (!key_text(p.desc->key)) { fail(m, "block key must be lowercase ns:path, not minecraft:"); return 0; }
        if (p.placeholder) { fail(m, "placeholder blocks are not supported (gap G7)"); return 0; }
        if (p.drop_item < 0) { fail(m, "unresolved drop item"); return 0; }
        for (uint32_t j = 0; j < i; ++j)
            if (m->blocks[j].id == p.id || !strcmp(m->blocks[j].desc->key, p.desc->key)) { fail(m, "duplicate block id or key"); return 0; }
        b.desc = p.desc; b.id = p.id; b.item_id = p.item_id; b.drop_item = p.drop_item;
        b.drop_override = p.drop_item != (int32_t)p.item_id && p.drop_item != 0;
        b.count_override = p.desc->drop_min || p.desc->drop_max;
        b.flammable = p.desc->flammability != 0;
        b.material_global = k_material[p.desc->material];
        b.sound_global = k_sound[p.desc->sound];
        for (int f = 0; f < 6; ++f) {
            int32_t t = p.texture[f];
            if (t < 0 || (uint32_t)t >= tcount || !textures || textures[t].atlas != 1 || !textures[t].rgba) { fail(m, "face texture missing or not a terrain texture"); return 0; }
            uint32_t k = 0;
            while (k < m->tile_count && m->tiles[k].plan_texture != t) ++k;
            if (k == m->tile_count) {
                if (k >= B195_MAX_TILES) { fail(m, "too many terrain tiles"); return 0; }
                m->tiles[k].plan_texture = t; m->tiles[k].rgba = textures[t].rgba; ++m->tile_count;
            }
            b.tile[f] = k;
            if (b.tile[f] != b.tile[0]) b.per_face = 1;
        }
        b.no_collide = (p.desc->flags & LCE_BLOCK_FLAG_NO_COLLIDE) != 0;
        b.transparent = (p.desc->flags & LCE_BLOCK_FLAG_TRANSPARENT) != 0;
#ifdef LCE_HAVE_BLOCK_STATES_1_7
        if (p.desc->struct_size >= offsetof(lce_block_desc, variant_textures) + sizeof(void *)) {
            const lce_block_desc *d = p.desc;
            b.variants = d->variant_count >= 2 ? d->variant_count : 0;
            b.shape = d->shape;
            memcpy(b.box, d->box, 6);
#ifdef LCE_HAVE_BLOCK_BOXES_1_11
            if (d->shape == LCE_SHAPE_BOXES && d->struct_size >= offsetof(lce_block_desc, boxes) + sizeof(void *)) {
                b.box_count = d->box_count; b.box_sets = d->box_sets;
                for (uint32_t s = 0; s < b.box_sets; ++s) memcpy(b.boxes[s], d->boxes + (size_t)s * d->box_count * 6, (size_t)d->box_count * 6);
                union_of(b.boxes[0], b.box_count, b.box);   // set 0's union: the +0x178 outline (and the single-box AABB path)
                b.solid_mask = 0x3f;   // a face is solid only if every set has a box covering it
                for (uint32_t s = 0; s < b.box_sets; ++s)
                    for (int f = 0; f < 6; ++f) {
                        bool any = false;
                        for (uint32_t k = 0; k < b.box_count; ++k) any |= face_solid(b.boxes[s][k], f);
                        if (!any) b.solid_mask &= (uint8_t)~(1u << f);
                    }
            }
#endif
            for (uint32_t v = 1; v < b.variants; ++v) {
                for (int f = 0; f < 6; ++f) {
                    const char *key = d->variant_textures ? (d->variant_textures[v * 6 + f] ? d->variant_textures[v * 6 + f] : d->variant_textures[v * 6]) : nullptr;
                    if (!key) { b.vtile[v][f] = (uint16_t)b.tile[f]; continue; }
                    int32_t t = -1;
                    for (uint32_t j = 0; j < tcount && textures; ++j) if (textures[j].key && !strcmp(textures[j].key, key)) { t = (int32_t)j; break; }
                    if (t < 0 || textures[t].atlas != 1 || !textures[t].rgba) { fail(m, "variant texture missing or not a terrain texture"); return 0; }
                    uint32_t k = 0;
                    while (k < m->tile_count && m->tiles[k].plan_texture != t) ++k;
                    if (k == m->tile_count) {
                        if (k >= B195_MAX_TILES) { fail(m, "too many terrain tiles"); return 0; }
                        m->tiles[k].plan_texture = t; m->tiles[k].rgba = textures[t].rgba; ++m->tile_count;
                    }
                    b.vtile[v][f] = (uint16_t)k;
                }
                const char *vn = d->variant_names ? d->variant_names[v] : nullptr;
                if (vn && *vn) b.vname_len[v] = (uint8_t)utf16(vn, b.vname[v], 48);
            }
        }
#endif
        const char *name = names && names[i] ? names[i] : p.desc->name;
        if (!name || !*name) { fail(m, "empty display name"); return 0; }
        b.name_len = utf16(name, b.name, B195_NAME_UNITS);
        uint32_t k = 0;
        for (const unsigned char *s = (const unsigned char *)name; *s && k + 1 < sizeof b.ascii_name; ++s) {
            if ((*s & 0xc0) == 0x80) continue;
            b.ascii_name[k++] = (*s >= 32 && *s <= 126) ? (char)*s : '?';
        }
        b.ascii_name[k] = 0;
        snprintf(b.texture_name, sizeof b.texture_name, "lcesdk_b%u", (unsigned)p.id);
    }
    m->count = count;
    b195_module *none = nullptr;
    if (!__atomic_compare_exchange_n(&g_m, &none, m, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) { fail(m, "another blocks195 module is configured"); return 0; }
    m->state = B195_CONFIGURED;
    return 1;
}

extern "C" uint32_t b195_status(const b195_module *m) { return m ? m->state : (uint32_t)B195_IDLE; }

extern "C" uint32_t b195_mapped_items(const b195_module *m, b195_mapped_item *out, uint32_t cap) {
    if (!m || m->state != B195_ITEMS_DONE || !out) return 0;
    uint32_t n = 0;
    for (uint32_t i = 0; i < m->count && n < cap; ++i) {
        const b195_block &b = m->blocks[i];
        if (!b.item) continue;
        out[n++] = {B195_PROVIDER, b.desc->key, b.ascii_name, b.texture_name, b.id, b.item_id, b.object, b.item,
                    (uintptr_t)&b.item_vtable[2], (uintptr_t)&b195_item_name_dispatch};
    }
    return n;
}

// ---- terrain ----------------------------------------------------------------------
extern "C" uint32_t b195_terrain_tile_count(const b195_module *m) { return m ? m->tile_count : 0; }

extern "C" int b195_terrain_compose(const b195_module *m, uint32_t tile, unsigned mip, uint8_t *out, size_t bytes) {
    if (!m || !out || mip > 3 || tile < 16 || tile > 256 || (tile & (tile - 1))) return 0;
    uint32_t side = tile >> mip;
    if (!side || bytes != (size_t)m->tile_count * side * side * 4) return 0;
    for (uint32_t k = 0; k < m->tile_count; ++k)
        for (uint32_t y = 0; y < side; ++y)
            for (uint32_t x = 0; x < side; ++x) {
                uint32_t sx = x * 16 / side, sy = y * 16 / side;
                memcpy(out + ((size_t)k * side * side + (size_t)y * side + x) * 4, m->tiles[k].rgba + (sy * 16 + sx) * 4, 4);
            }
    return 1;
}

namespace {
bool face_slot(const b195_block &b, unsigned f, uintptr_t *at) {
    *at = b.object + (b.per_face ? B195_F_FACE_SPRITES + 8 * f : B195_F_SPRITE);
    return b.per_face || f == 0;
}
bool sprite_ok(b195_module *m, uintptr_t p, uintptr_t tex, uint32_t x, uint32_t y, uint32_t tile) {
    uintptr_t bound = 0, data = 0;
    uint32_t v[4] = {};
    return is(m, p, m->base + B195_RVA_SPRITE_VTABLE) && q(m, p + 0x58, &bound) && bound == tex && q(m, p + 0x68, &data) && data &&
           rd(m, p + 0x80, v, 16) && v[0] == x && v[1] == y && v[2] == tile && v[3] == tile;
}
}  // namespace

extern "C" int b195_terrain_bind(b195_module *m, uintptr_t atlas, uint32_t gen, uint32_t width, uint32_t height,
                                 uint32_t stock_height, uint32_t tile, uint32_t first) {
    if (!m || (m->state != B195_ITEMS_DONE && m->state != B195_BLOCKS_DONE) || m->terrain_state == 2 || m->terrain_state == 3 ||
        !atlas || gen <= m->generation || m->generations >= B195_TERRAIN_GENERATIONS || !m->tile_count || !terrain_held(m) ||
        !m->mem.terrain_uploaded || tile < 16 || tile > 256 || !width || width % tile || stock_height % tile)
        return 0;
    uint32_t cols = width / tile, last = first + m->tile_count - 1;
    if (stock_height + (last / cols + 1) * tile > height) return 0;
    uint32_t type = 99, tw = 0, th = 0;
    uintptr_t tex = 0, fallback = 0;
    if (!rd(m, atlas + 8, &type, 4) || type != 0 || !q(m, atlas + 0xf0, &tex) || !tex || !q(m, atlas + 0xe8, &fallback) || !fallback ||
        !rd(m, tex + 0xc, &tw, 4) || !rd(m, tex + 0x10, &th, 4) || tw != width || th != height ||
        m->mem.terrain_uploaded(m->mem.ctx, atlas, tex, gen) != 1 || !code(m, B195_RVA_SPRITE_BIND, k_pro_sprite_bind, 16))
        return 0;
    for (uint32_t i = 0; i < m->count; ++i) {
        b195_block &b = m->blocks[i];
        if (b.dropped) continue;   // never registered (construction failed)
        uint8_t per = 0;
        uint32_t id = 0;
        if (!is(m, b.object, (uintptr_t)&b.vtable[2]) || !rd(m, b.object + B195_F_ID, &id, 4) || id != b.id ||
            !rd(m, b.object + B195_F_PER_FACE, &per, 1) || per != b.per_face)
            return 0;
        for (unsigned f = 0; f < 6; ++f) { uintptr_t at; if (face_slot(b, f, &at) && !is(m, at, fallback)) return 0; }
    }
    unsigned slot = m->generations++;
    m->terrain_state = 1;
    for (uint32_t k = 0; k < m->tile_count; ++k) {
        uint32_t idx = first + k, x = (idx % cols) * tile, y = stock_height + (idx / cols) * tile;
        uintptr_t p = (uintptr_t)m->n.alloc(B195_SPRITE_SIZE);
        m->sprites[slot][k] = p;
        if (!p || !terrain_held(m)) { m->terrain_state = 3; return -1; }
        memset((void *)p, 0, B195_SPRITE_SIZE);
        *(uintptr_t *)p = m->base + B195_RVA_SPRITE_VTABLE;
        *(uint64_t *)(p + 0x28) = 7; *(uint64_t *)(p + 0x50) = 7;    // two empty wstrings (+8, +0x30)
        char nm[24];
        snprintf(nm, sizeof nm, "lcesdk_t%u", (unsigned)k);
        uint16_t u[24];
        uint32_t len = 0;
        for (; nm[len]; ++len) u[len] = (unsigned char)nm[len];
        m->n.wstring((void *)(p + 8), u, len);
        m->n.wstring((void *)(p + 0x30), u, len);
        *(float *)(p + 0xa0) = 0.06875f / (float)width; *(float *)(p + 0xa4) = 0.06875f / (float)height;   // additive_terrain_195 inset
        m->n.sprite_bind(p, tex, 0, (int)x, (int)y, (int)tile, (int)tile, 0);
        if (!sprite_ok(m, p, tex, x, y, tile)) { m->terrain_state = 3; return -1; }
    }
    for (uint32_t i = 0; i < m->count; ++i) {
        b195_block &b = m->blocks[i];
        if (b.dropped) continue;   // never registered (construction failed)
        for (unsigned f = 0; f < 6; ++f) {
            uintptr_t at;
            if (!face_slot(b, f, &at)) continue;
            if (m->mem.cas(m->mem.ctx, at, fallback, m->sprites[slot][b.tile[f]]) != 1) { m->terrain_state = 3; return -1; }
            b.fallback[f] = fallback;
        }
    }
    m->atlas = atlas; m->texture = tex; m->generation = gen;
    m->terrain_state = 2;
    return 1;
}

extern "C" int b195_terrain_detach(b195_module *m) {
    if (!m) return 0;
    if (m->terrain_state != 2 && m->terrain_state != 3) return 1;
    if (!m->generations) return 0;
    unsigned slot = m->generations - 1;
    int rc = 1;
    for (uint32_t i = 0; i < m->count; ++i) {
        b195_block &b = m->blocks[i];
        if (b.dropped) continue;   // never registered (construction failed)
        for (unsigned f = 0; f < 6; ++f) {
            uintptr_t at, cur = 0;
            if (!face_slot(b, f, &at) || !b.fallback[f]) continue;
            if (!q(m, at, &cur)) { rc = -1; continue; }
            if (cur == b.fallback[f]) continue;
            // Only our own sprite is replaced; a foreign pointer is preserved and reported.
            if (cur != m->sprites[slot][b.tile[f]] || m->mem.cas(m->mem.ctx, at, cur, b.fallback[f]) != 1) rc = -1;
        }
    }
    if (rc == 1) { m->atlas = m->texture = 0; if (m->terrain_state == 2) m->terrain_state = 0; }
    return rc;
}

// ---- one-shot .text fallback --------------------------------------------------------
extern "C" int b195_text_plan(uintptr_t base, const uint8_t w[16], uintptr_t region, size_t size, b195_text_patch *out) {
    // 0xb74d8: call r13 | mov rdi,r14 | call 0xcdab00 (e8 1d 36 c2 00) | mov rdi,r15 | e8 15..
    static const uint8_t guard[16] = {0x41,0xff,0xd5,0x4c,0x89,0xf7,0xe8,0x1d,0x36,0xc2,0x00,0x4c,0x89,0xff,0xe8,0x15};
    if (!base || base > UINTPTR_MAX - 0xcdab00u || !w || !region || size < 14 || region > UINTPTR_MAX - size || !out || memcmp(w, guard, 16)) return 0;
    uint64_t old = 0;
    for (unsigned i = 0; i < 8; ++i) old |= (uint64_t)w[8 + i] << (8 * i);
    uintptr_t next = base + 0xb74e3u;
    for (size_t i = 0; i + 14 <= size; ++i) {
        uintptr_t relay = region + i;
        int64_t d;
        if (relay >= next) { if (relay - next > INT32_MAX) continue; d = (int64_t)(relay - next); }
        else { if (next - relay > (uintptr_t)INT32_MAX + 1u) continue; d = -(int64_t)(next - relay); }
        uint32_t disp = (uint32_t)(int32_t)d;
        if ((disp & 0xffu) != 0x1du) continue;   // low rel32 byte lives in the untouched word at 0xb74d8
        *out = {base + 0xb74e0u, relay, base + 0xcdab00u, old, (old & ~UINT64_C(0xffffff)) | (disp >> 8)};
        return 1;
    }
    return 0;
}

extern "C" int b195_text_restore(const b195_text_patch *p, const b195_text_ops *o) {
    if (!p || !o || !o->protect || !o->cas8 || !o->read8 || !p->word || (p->word & 7) || p->before == p->after) return 0;
    uint64_t cur = 0;
    if (!o->read8(o->ctx, p->word, &cur)) return 0;
    if (cur == p->before) return 1;    // already stock
    if (cur != p->after) return 0;     // not our patch: leave it alone
    uintptr_t page = p->word & ~(uintptr_t)0x3fff;
    if (o->protect(o->ctx, page, 0x4000, 3) != 0) return -1;
    int ok = o->cas8(o->ctx, p->word, p->after, p->before);
    int back = o->protect(o->ctx, page, 0x4000, 5);
    if (ok != 1 || back != 0) return -1;
    return o->read8(o->ctx, p->word, &cur) && cur == p->before ? 1 : -1;
}

#ifdef B195_HOST_TEST
extern "C" void b195_test_reset(void) { __atomic_store_n(&g_m, (b195_module *)nullptr, __ATOMIC_RELEASE); }
#else
extern "C" void b195_natives_bind(b195_natives *n, uintptr_t b) {
    n->alloc = (void *(*)(size_t))(b + B195_RVA_ALLOC);
    n->wstring = (void (*)(void *, const uint16_t *, uint64_t))(b + B195_RVA_WSTRING);
    n->key_ctor = (void (*)(void *, const void *))(b + B195_RVA_KEY_CTOR);
    n->reg_register = (void (*)(uintptr_t, uint32_t, const void *, uintptr_t))(b + B195_RVA_REGISTER);
    n->reg_present = (uint8_t (*)(uintptr_t, const void *))(b + B195_RVA_PRESENT);
    n->reg_by_key = (uintptr_t (*)(uintptr_t, const void *))(b + B195_RVA_BY_KEY);
    n->ore_ctor = (void (*)(void *))(b + B195_RVA_ORE_CTOR);
    n->block_ctor = (void (*)(void *, uintptr_t))(b + B195_RVA_BLOCK_CTOR);
    n->block_init_states = (void (*)(void *))(b + B195_RVA_BLOCK_INIT_STATES);
    n->set_hardness = (void *(*)(void *, float))(b + B195_RVA_SET_HARDNESS);
    n->set_resistance = (void *(*)(void *, float))(b + B195_RVA_SET_RESISTANCE);
    n->set_sound = (void *(*)(void *, uintptr_t))(b + B195_RVA_SET_SOUND);
    n->set_light = (void *(*)(void *, float))(b + B195_RVA_SET_LIGHT);
    n->set_texture = (void *(*)(void *, void *))(b + B195_RVA_SET_TEXTURE);
    n->block_name = (void *(*)(void *, void *))(b + B195_RVA_BLOCK_NAME);
    n->get_resource = (uintptr_t (*)(void *, uintptr_t, uintptr_t, int))(b + B195_RVA_ORE_GET_RESOURCE);
    n->resource_count = (int (*)(void *, int, uintptr_t))(b + B195_RVA_ORE_RESOURCE_COUNT);
    n->next_int = (int (*)(uintptr_t, int))(b + B195_RVA_RANDOM_NEXT_INT);
    n->item_by_id = (uintptr_t (*)(unsigned))(b + B195_RVA_ITEM_BY_ID);
    n->item_present = (uint8_t (*)(uintptr_t, const void *))(b + B195_RVA_ITEM_PRESENT);
    n->item_register = (void (*)(uintptr_t, uint32_t, const void *, uintptr_t))(b + B195_RVA_ITEM_REGISTER);
    n->block_item_ctor = (void (*)(void *, uintptr_t))(b + B195_RVA_BLOCK_ITEM_CTOR);
    n->map_slot = (uintptr_t (*)(uintptr_t, const uintptr_t *))(b + B195_RVA_BLOCK_ITEM_MAP_SLOT);
    n->item_name = (void *(*)(void *, void *, void *))(b + B195_RVA_ITEM_NAME);
    n->fire_slot = (int *(*)(uintptr_t, const uintptr_t *))(b + B195_RVA_FIRE_ODDS_SLOT);
    n->sprite_bind = (void (*)(uintptr_t, uintptr_t, uint64_t, int, int, int, int, int))(b + B195_RVA_SPRITE_BIND);
    n->state_from_data = (uintptr_t (*)(void *, int))(b + B195_RVA_COLORED_FROM_DATA);
    n->data_from_state = (int (*)(void *, uintptr_t))(b + B195_RVA_COLORED_TO_DATA);
    n->face_sprite = (uintptr_t (*)(void *, uintptr_t, uintptr_t))(b + B195_RVA_FACE_GETTER);
    n->box = (void *(*)(double, double, double, double, double, double))(b + B195_RVA_BOX);
    n->chest_ctor = (void (*)(void *, int))(b + B195_RVA_CHEST_CTOR);
    n->chest_new_be = (void *(*)(void *, void *, void *, int))(b + B195_RVA_CHEST_NEW_BE);
    n->disp_new_be = (void *(*)(void *, void *, void *, int))(b + B195_RVA_DISP_NEW_BE);
    n->set_custom_name = (void (*)(void *, const void *, const void *, int, int))(b + B195_RVA_SET_CUSTOM_NAME);
}
#endif
