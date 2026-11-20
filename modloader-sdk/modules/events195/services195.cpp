// services195: lce_api 1.1 runtime services on 1.95. See services195.h.
#include "services195.h"
#include "util195.h"
#include <math.h>
#include <string.h>

namespace {
#include "sound_ids_195.inc"

constexpr uint32_t kStrings = 2048, kSounds = 128;
struct Clone { char key[LCE_MAX_KEY]; uint16_t id; float volume, pitch; };
struct S {
    uint32_t ready;
    lce195_natives n;
    lce195_world_ops ops;
    lce195_resolver r;
    const lce195_l10n_entry *strings; uint32_t string_count;
    Clone sounds[kSounds]; uint32_t sound_count;
};
S s;

const lce195_host *host() { return lce195_ev_host(); }
bool rd(uintptr_t a, void *v, size_t n) { const lce195_host *h = host(); return h && a && a <= UINTPTR_MAX - n && h->read(h->opaque, a, v, n) == 1; }
uintptr_t base() { return s.n.base; }

// The frame the caller runs in, still current. NULL = refuse.
const lce195_frame *frame() {
    const lce195_frame *f = lce195_ev_frame();
    const lce195_host *h = host();
    if (!s.ready || !f || !h || f->view.image != base() || h->view_current(h->opaque, &f->view) != 1) return nullptr;
    return f;
}
const lce195_frame *frame_player(void *player) {
    const lce195_frame *f = frame();
    if (!f || !f->has_player || (player && (uintptr_t)player != f->view.player_object)) return nullptr;
    return f;
}
const lce195_frame *frame_level(void *level) {
    const lce195_frame *f = frame();
    return f && (!level || (uintptr_t)level == f->view.level) ? f : nullptr;
}
lce_status result(int rc) { return rc == 1 ? LCE_OK : rc == 0 ? LCE_E_INVALID : LCE_E_BACKEND; }
bool xyz_ok(int32_t x, int32_t y, int32_t z) { return x >= -30000000 && x <= 30000000 && z >= -30000000 && z <= 30000000 && y >= 0 && y < 256; }
int32_t chunk(int32_t v) { return v >= 0 ? v / 16 : -1 - (int32_t)((-(int64_t)v - 1) / 16); }

// Native wstring (40 bytes) built by the game's own assign.
struct WString {
    uint64_t w[5] = {0, 0, 0, 0, 7};
    ~WString() { if (w[4] > 7 && w[1]) s.n.free((void *)(uintptr_t)w[1]); }
};
// Release one strong reference of a 1.95 _Ref_count (uses +8, weaks +12; slot0 destroy, slot1 delete).
void release(uintptr_t ctrl) {
    if (!ctrl) return;
    if (s.n.atomic_sub(ctrl + 8, 1, 5) != 1) return;
    uintptr_t vt = *(uintptr_t *)ctrl;
    ((void (*)(uintptr_t))(*(uintptr_t *)vt))(ctrl);
    if (s.n.atomic_sub(ctrl + 12, 1, 5) == 1) { vt = *(uintptr_t *)ctrl; ((void (*)(uintptr_t))(*(uintptr_t *)(vt + 8)))(ctrl); }
}
struct Shared { lce195_pair p{}; ~Shared() { release(p.control); } };

// Registry lookup (ResourceLocation key), pattern of r72 registry_providers lookup().
bool effect_lookup(const char *name, uintptr_t *out) {
    char canon[128]; size_t n = strlen(name);
    if (!n || n > 100) return false;
    if (!strchr(name, ':')) { memcpy(canon, "minecraft:", 10); memcpy(canon + 10, name, n + 1); } else memcpy(canon, name, n + 1);
    uint16_t units[128]; n = strlen(canon);
    for (size_t i = 0; i < n; ++i) { char c = canon[i]; if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == ':' || c == '.' || c == '/' || c == '-')) return false; units[i] = (uint8_t)c; }
    WString str; s.n.ws_assign(str.w, units, n);
    uint64_t key[14] = {};
    s.n.key_ctor(key, str.w);
    struct KeyGuard { uint64_t *k; ~KeyGuard() { s.n.key_dtor(k); } } guard{key};
    uintptr_t reg = 0, vt = 0, fn[4] = {};
    static const unsigned off[4] = {0x20, 0, 0x38, 0x40};
    static const uintptr_t want[4] = {LCE195_RVA_EFFECT_CONTAINS, LCE195_RVA_EFFECT_GET, LCE195_RVA_EFFECT_ID, LCE195_RVA_EFFECT_BYID};
    if (!rd(base() + LCE195_RVA_EFFECT_REGISTRY, &reg, 8) || !reg || !rd(reg, &vt, 8) || vt != base() + LCE195_RVA_EFFECT_REG_VT) return false;
    for (int i = 0; i < 4; ++i) if (!rd(vt + off[i], &fn[i], 8) || fn[i] != base() + want[i]) return false;
    if (((unsigned char (*)(uintptr_t, const void *))fn[0])(reg, key) != 1) return false;
    uintptr_t obj = ((uintptr_t (*)(uintptr_t, const void *))fn[1])(reg, key);
    if (!obj) return false;
    int id = ((int (*)(uintptr_t, uintptr_t))fn[2])(reg, obj);
    if (id < 1 || id > 127 || ((uintptr_t (*)(uintptr_t, int))fn[3])(reg, id) != obj) return false;
    *out = obj; return true;
}
} // namespace

namespace {
// Exact 1.95 bytes of the API 1.2 natives (from the console-proven r72 nbt_health/nbt_fire/nbt_item_count pins).
// A native bound to the real image whose bytes differ is unbound (its service then reports UNSUPPORTED).
struct Pin { uintptr_t rva; uint8_t n; uint8_t b[16]; };
const Pin kPins[] = {
    {LCE195_RVA_HEALTH_GET, 16, {0x48,0x8d,0x0d,0x99,0x47,0xff,0x00,0x48,0x8b,0x87,0x60,0x02,0x00,0x00,0x48,0x8b}},
    {LCE195_RVA_HEALTH_SET, 16, {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x53,0x50,0x48,0x8d,0x05,0x6f,0x47,0xff}},
    {LCE195_RVA_HEALTH_MAX, 16, {0x55,0x48,0x89,0xe5,0x48,0x8d,0x0d,0x15,0x81,0x00,0x01,0x48,0x8b,0x07,0x48,0x8b}},
    {LCE195_RVA_IS_ALIVE, 16, {0x80,0xbf,0xfa,0x01,0x00,0x00,0x00,0x74,0x03,0x31,0xc0,0xc3,0x55,0x48,0x89,0xe5}},
    {LCE195_RVA_FIRE_CLEAR, 11, {0xc7,0x87,0xc0,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0xc3}},
    {LCE195_RVA_FIRE_COUNTDOWN, 14, {0x8b,0x83,0xc0,0x01,0x00,0x00,0xff,0xc8,0x89,0x83,0xc0,0x01,0x00,0x00}},
    {LCE195_RVA_STACK_SET_COUNT, 16, {0x48,0x8d,0x0d,0xd1,0x46,0x0b,0x01,0x89,0x77,0x10,0xb0,0x01,0x48,0x39,0x39,0x74}},
    {LCE195_RVA_INV_SET_CHANGED, 7, {0xff,0x87,0x38,0x01,0x00,0x00,0xc3}},
    {LCE195_RVA_STACK_SET_DAMAGE, 14, {0x89,0xf0,0xc1,0xf8,0x1f,0xc4,0xe2,0x78,0xf2,0xc6,0x89,0x47,0x3c,0xc3}},
    {LCE195_RVA_STACK_MAX_DAMAGE, 16, {0x80,0x7f,0x38,0x00,0x74,0x0e,0x48,0x8d,0x3d,0x13,0xf1,0x05,0x01,0x48,0x8b,0x07}},
    {LCE195_RVA_STACK_HURT, 16, {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x81,0xec}},
    {LCE195_RVA_INVENTORY_ADD, 16, {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x53,0x48,0x83,0xec,0x28,0x4c,0x8b,0x3d}},
    {LCE195_RVA_DMG_PLAYER_ATTACK, 16, {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x53,0x48,0x83,0xec,0x28,0x4c,0x8b,0x3d}},
    {LCE195_RVA_DMG_MOB_ATTACK, 16, {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x53,0x48,0x83,0xec,0x28,0x4c,0x8b,0x3d}},
    {LCE195_RVA_DMG_MOB_ATTACK_IDS, 16, {0xbe,0x1d,0x00,0x00,0x00,0xba,0x1d,0x00,0x00,0x00,0x4c,0x89,0xf7,0x48,0x89,0xd9}},
    {LCE195_RVA_WEAK_LOCK, 16, {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec}},
    {LCE195_RVA_WEAK_LOCK_BODY, 16, {0x48,0x8b,0x45,0xc0,0xbe,0x05,0x00,0x00,0x00,0x48,0x8d,0x58,0x08,0x48,0x89,0xdf}},
    {LCE195_RVA_SEND_PARTICLES, 16, {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x81,0xec}},
    {LCE195_RVA_SEND_PARTICLES_BODY, 16, {0x49,0x89,0xff,0xbf,0x78,0x00,0x00,0x00,0x41,0x89,0xcd,0x41,0x89,0xd6,0x49,0x89}},
    {LCE195_RVA_PARTICLE_GET_ID, 4, {0x8b,0x47,0x28,0xc3}},
    {LCE195_RVA_SEND_TINTED_PARTICLES, 16, {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x81,0xec}},
    {LCE195_RVA_TINTED_PARTICLES_PACKET_CALL, 16, {0xc5,0x7a,0x11,0x4c,0x24,0x08,0xc5,0x7a,0x11,0x04,0x24,0xe8,0x57,0xf9,0x48,0xff}},
    {LCE195_RVA_TINTED_PARTICLES_PACKET_COLOR, 14, {0xc6,0x43,0x68,0x01,0xc5,0xfa,0x11,0x43,0x6c,0xc5,0xfa,0x11,0x53,0x70}},
    {LCE195_RVA_TINTED_PARTICLE_TYPE_REF, 7, {0x48,0x8d,0x05,0xff,0x02,0xbb,0x00}},
    {LCE195_RVA_TAG_GET_OR_CREATE, 16, {0x55,0x48,0x89,0xe5,0x41,0x56,0x53,0x49,0x89,0xfe,0x49,0x8b,0x5e,0x30,0x48,0x85}},
    {LCE195_RVA_TAG_GET, 16, {0x55,0x48,0x89,0xe5,0x41,0x56,0x53,0x48,0x83,0xec,0x20,0x4c,0x8b,0x35,0xd6,0x46}},
    {LCE195_RVA_TAG_REMOVE, 16, {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x53,0x48,0x83,0xec,0x58,0x4c,0x8b,0x3d}},
    {LCE195_RVA_TAG_PUT_INT, 16, {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec}},
    {LCE195_RVA_TAG_PUT_DOUBLE, 16, {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec}},
    {LCE195_RVA_TAG_PUT_STRING, 16, {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec}},
    {0x1b2647u, 5, {0xe8,0x64,0x7d,0x1c,0x00}},
    {0x1b7982u, 5, {0xe8,0x39,0xe4,0x05,0x00}},
    {0x1af417u, 5, {0xe8,0x04,0xc2,0x4d,0x00}},
    {0x1b8688u, 5, {0xe8,0x23,0x00,0x00,0x00}},
    {0x3ba923u, 5, {0xe8,0xc8,0xc4,0xdf,0xff}},
    {0x1b742au, 5, {0xe8,0x01,0xdb,0x02,0x00}},
    {LCE195_RVA_PAYLOAD_CTOR, 16, {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x54,0x53,0x41,0x89,0xce,0x49,0x89}},
    {LCE195_RVA_DMG_PLAYER_ATTACK_IDS, 16, {0xbe,0x1e,0x00,0x00,0x00,0xba,0x3c,0x00,0x00,0x00,0x4c,0x89,0xf7,0x48,0x89,0xd9}},
    // API 1.5 entity NBT: saveWithoutId + its tag-set read, CompoundTag ctor, the tag set's erase/insert and Entity::load's use of them
    {LCE195_RVA_ENTITY_SAVE, 16, {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec}},
    {LCE195_RVA_ENTITY_SAVE_TAGS, 8, {0x49,0x8b,0x84,0x24,0x68,0x03,0x00,0x00}},
    {LCE195_RVA_COMPOUND_CTOR, 16, {0x55,0x48,0x89,0xe5,0x41,0x56,0x53,0x48,0x83,0xec,0x70,0x4c,0x8b,0x35,0xe6,0x4c}},
    {LCE195_RVA_TAGSET_INSERT, 16, {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec}},
    {LCE195_RVA_TAGSET_ERASE, 16, {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x53,0x50,0x48,0x89,0xf3,0x49,0x89,0xfe}},
    {LCE195_RVA_TAGSET_ERASE_BODY, 6, {0x80,0x7b,0x19,0x00,0x75,0x3b}},
    {LCE195_RVA_TAGSET_ERASE_CAP, 5, {0x48,0x83,0x7b,0x40,0x08}},
    {LCE195_RVA_LOAD_TAGS_CLEAR, 14, {0x4c,0x8d,0xbb,0x60,0x03,0x00,0x00,0x4c,0x89,0xff,0x48,0x8b,0x70,0x08}},
    {LCE195_RVA_LOAD_TAGS_ERASE_CALL, 5, {0xe8,0xb7,0x76,0x04,0x00}},
    {LCE195_RVA_LOAD_TAGS_INSERT_CALL, 5, {0xe8,0x52,0x76,0x04,0x00}},
    // API 1.6 client send: ServerboundCustomPayloadPacket ctor, ClientPacketListener::send and its "done" early-out
    {LCE195_RVA_SB_PAYLOAD_CTOR, 16, {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x54,0x53,0x41,0x89,0xce,0x49,0x89}},
    {0x5fbde6u, 8, {0x4c,0x89,0x7b,0x50,0x44,0x89,0x73,0x58}},                                   // data +0x50, len +0x58
    {LCE195_RVA_CLIENT_SEND, 16, {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x53,0x48,0x83,0xec,0x28,0x4c,0x8b,0x3d}},
    {0xb9147bu, 6, {0x80,0x7f,0x08,0x00,0x74,0x18}},                                             // cmp byte [listener+8],0
};
bool pin_ok(uintptr_t rva) {
    for (const Pin &p : kPins) {
        if (p.rva != rva) continue;
        uint8_t got[16];
        return rd(base() + rva, got, p.n) && !memcmp(got, p.b, p.n);
    }
    return false;
}
template <class F> void unbind_unless_pinned(F &fn, uintptr_t rva) {
    if (fn && (uintptr_t)fn == base() + rva && !pin_ok(rva)) fn = nullptr;
}
} // namespace
namespace {
void verify_pins() {
    unbind_unless_pinned(s.n.health_get, LCE195_RVA_HEALTH_GET);
    unbind_unless_pinned(s.n.health_set, LCE195_RVA_HEALTH_SET);
    unbind_unless_pinned(s.n.health_max, LCE195_RVA_HEALTH_MAX);
    unbind_unless_pinned(s.n.is_alive, LCE195_RVA_IS_ALIVE);
    unbind_unless_pinned(s.n.fire_clear, LCE195_RVA_FIRE_CLEAR);
    if (s.n.fire_clear && (uintptr_t)s.n.fire_clear == base() + LCE195_RVA_FIRE_CLEAR && !pin_ok(LCE195_RVA_FIRE_COUNTDOWN)) s.n.fire_clear = nullptr;
    unbind_unless_pinned(s.n.stack_set_count, LCE195_RVA_STACK_SET_COUNT);
    unbind_unless_pinned(s.n.inventory_set_changed, LCE195_RVA_INV_SET_CHANGED);
    unbind_unless_pinned(s.n.stack_set_damage, LCE195_RVA_STACK_SET_DAMAGE);
    unbind_unless_pinned(s.n.stack_max_damage, LCE195_RVA_STACK_MAX_DAMAGE);
    unbind_unless_pinned(s.n.stack_hurt, LCE195_RVA_STACK_HURT);
    unbind_unless_pinned(s.n.inventory_add, LCE195_RVA_INVENTORY_ADD);
    unbind_unless_pinned(s.n.player_attack_source, LCE195_RVA_DMG_PLAYER_ATTACK);
    if (s.n.player_attack_source && (uintptr_t)s.n.player_attack_source == base() + LCE195_RVA_DMG_PLAYER_ATTACK &&
        !pin_ok(LCE195_RVA_DMG_PLAYER_ATTACK_IDS)) s.n.player_attack_source = nullptr;
    unbind_unless_pinned(s.n.mob_attack_source, LCE195_RVA_DMG_MOB_ATTACK);
    if (s.n.mob_attack_source && (uintptr_t)s.n.mob_attack_source == base() + LCE195_RVA_DMG_MOB_ATTACK &&
        !pin_ok(LCE195_RVA_DMG_MOB_ATTACK_IDS)) s.n.mob_attack_source = nullptr;
    unbind_unless_pinned(s.n.send_particles, LCE195_RVA_SEND_PARTICLES);
    if (s.n.send_particles && (uintptr_t)s.n.send_particles == base() + LCE195_RVA_SEND_PARTICLES &&
        !(pin_ok(LCE195_RVA_SEND_PARTICLES_BODY) && pin_ok(LCE195_RVA_PARTICLE_GET_ID))) s.n.send_particles = nullptr;
    unbind_unless_pinned(s.n.send_tinted_particles, LCE195_RVA_SEND_TINTED_PARTICLES);
    if ((uintptr_t)s.n.send_tinted_particles == base() + LCE195_RVA_SEND_TINTED_PARTICLES &&
        !(pin_ok(LCE195_RVA_TINTED_PARTICLES_PACKET_CALL) && pin_ok(LCE195_RVA_TINTED_PARTICLES_PACKET_COLOR) &&
          pin_ok(LCE195_RVA_TINTED_PARTICLE_TYPE_REF) && pin_ok(LCE195_RVA_PARTICLE_GET_ID)))
        s.n.send_tinted_particles = nullptr;
    // item NBT: each native's prologue and the tag-type call inside it (IntTag/DoubleTag/StringTag ctor, erase, map find)
    unbind_unless_pinned(s.n.tag_get_or_create, LCE195_RVA_TAG_GET_OR_CREATE);
    unbind_unless_pinned(s.n.tag_get, LCE195_RVA_TAG_GET);
    unbind_unless_pinned(s.n.tag_remove, LCE195_RVA_TAG_REMOVE);
    unbind_unless_pinned(s.n.tag_put_int, LCE195_RVA_TAG_PUT_INT);
    unbind_unless_pinned(s.n.tag_put_double, LCE195_RVA_TAG_PUT_DOUBLE);
    unbind_unless_pinned(s.n.tag_put_string, LCE195_RVA_TAG_PUT_STRING);
    if ((uintptr_t)s.n.tag_get == base() + LCE195_RVA_TAG_GET &&
        !(pin_ok(0x1b2647u) && pin_ok(0x1b7982u) && pin_ok(0x1af417u) && pin_ok(0x1b8688u) && pin_ok(0x3ba923u) && pin_ok(0x1b742au))) {
        s.n.tag_get_or_create = nullptr; s.n.tag_get = nullptr; s.n.tag_remove = nullptr;
        s.n.tag_put_int = nullptr; s.n.tag_put_double = nullptr; s.n.tag_put_string = nullptr;
    }
    unbind_unless_pinned(s.n.compound_ctor, LCE195_RVA_COMPOUND_CTOR);
    unbind_unless_pinned(s.n.entity_save, LCE195_RVA_ENTITY_SAVE);
    if (s.n.entity_save && (uintptr_t)s.n.entity_save == base() + LCE195_RVA_ENTITY_SAVE && !pin_ok(LCE195_RVA_ENTITY_SAVE_TAGS)) s.n.entity_save = nullptr;
    unbind_unless_pinned(s.n.tagset_insert, LCE195_RVA_TAGSET_INSERT);
    unbind_unless_pinned(s.n.tagset_erase, LCE195_RVA_TAGSET_ERASE);
    if ((uintptr_t)s.n.tagset_erase == base() + LCE195_RVA_TAGSET_ERASE &&
        !(pin_ok(LCE195_RVA_TAGSET_ERASE_BODY) && pin_ok(LCE195_RVA_TAGSET_ERASE_CAP) && pin_ok(LCE195_RVA_LOAD_TAGS_CLEAR) &&
          pin_ok(LCE195_RVA_LOAD_TAGS_ERASE_CALL) && pin_ok(LCE195_RVA_LOAD_TAGS_INSERT_CALL))) {
        s.n.tagset_erase = nullptr; s.n.tagset_insert = nullptr;
    }
    unbind_unless_pinned(s.n.payload_ctor, LCE195_RVA_PAYLOAD_CTOR);
    unbind_unless_pinned(s.n.sb_payload_ctor, LCE195_RVA_SB_PAYLOAD_CTOR);
    if (s.n.sb_payload_ctor && (uintptr_t)s.n.sb_payload_ctor == base() + LCE195_RVA_SB_PAYLOAD_CTOR && !pin_ok(0x5fbde6u)) s.n.sb_payload_ctor = nullptr;
    unbind_unless_pinned(s.n.client_send, LCE195_RVA_CLIENT_SEND);
    if (s.n.client_send && (uintptr_t)s.n.client_send == base() + LCE195_RVA_CLIENT_SEND && !pin_ok(0xb9147bu)) s.n.client_send = nullptr;
    unbind_unless_pinned(s.n.weak_lock, LCE195_RVA_WEAK_LOCK);
    if (s.n.weak_lock && (uintptr_t)s.n.weak_lock == base() + LCE195_RVA_WEAK_LOCK && !pin_ok(LCE195_RVA_WEAK_LOCK_BODY)) s.n.weak_lock = nullptr;
}
} // namespace

extern "C" int lce195_services_init(const lce195_natives *n, const lce195_world_ops *ops, const lce195_resolver *r) {
    if (s.ready || !n || !n->base || !ops || !r || !r->item || !r->block) return 0;
    s.n = *n; s.ops = *ops; s.r = *r; s.ready = 1;
    verify_pins();
    return 1;
}

extern "C" void lce195_services_fill(lce_api *a) {
    if (!a || a->struct_size < offsetof(lce_api, translate) + sizeof(void *)) return;
    a->give_item = lce195_give_item; a->set_block = lce195_set_block; a->get_block = lce195_get_block;
    a->spawn_entity = lce195_spawn_entity; a->drop_item = lce195_drop_item; a->send_chat = lce195_send_chat;
    a->add_effect = lce195_add_effect; a->play_sound = lce195_play_sound; a->schedule = lce195_schedule;
    a->translate = lce195_translate;
    if (a->struct_size < offsetof(lce_api, set_motion) + sizeof(void *)) return;
    a->explode = lce195_explode; a->set_fire = lce195_set_fire; a->heal = lce195_heal; a->consume_held_item = lce195_consume_held_item;
    a->damage_entity = lce195_damage_entity; a->damage_held_item = lce195_damage_held_item; a->teleport = lce195_teleport;
    a->set_motion = lce195_set_motion;
    a->capabilities2 |= lce195_services_capabilities2() & ~LCE_CAP2_SVC_TINTED_PARTICLES;
    if (a->struct_size >= offsetof(lce_api, spawn_tinted_particles) + sizeof(a->spawn_tinted_particles) &&
        s.n.send_tinted_particles) {
        a->spawn_tinted_particles = lce195_spawn_tinted_particles;
        a->capabilities2 |= LCE_CAP2_SVC_TINTED_PARTICLES;
    }
#ifdef LCE_HAVE_SVC_1_3
    if (a->struct_size < offsetof(lce_api, spawn_particles) + sizeof(void *)) return;
    a->spawn_particles = lce195_spawn_particles;
#endif
#ifdef LCE_HAVE_NBT_1_4
    if (a->struct_size < offsetof(lce_api, item_tag_remove) + sizeof(void *)) return;
    a->item_tag_get = lce195_item_tag_get; a->item_tag_set = lce195_item_tag_set; a->item_tag_remove = lce195_item_tag_remove;
#endif
#ifdef LCE_HAVE_NET_1_4
    if (a->struct_size < offsetof(lce_api, send_payload) + sizeof(void *)) return;
    a->send_payload = lce195_send_payload;
#endif
#ifdef LCE_HAVE_NET_1_6
    if (a->struct_size >= offsetof(lce_api, send_to_server) + sizeof(void *)) a->send_to_server = lce195_send_to_server;
#endif
#ifdef LCE_HAVE_ENTITY_NBT_1_5
    if (a->struct_size < offsetof(lce_api, entity_data_remove) + sizeof(void *)) return;
    a->entity_nbt_get = lce195_entity_nbt_get; a->entity_data_get = lce195_entity_data_get;
    a->entity_data_set = lce195_entity_data_set; a->entity_data_remove = lce195_entity_data_remove;
#endif
}

namespace { bool nbt_ready(); }
extern "C" uint32_t lce195_services_capabilities2(void) {
    if (!s.ready) return 0;
    uint32_t c = 0;
    if (s.ops.explode && s.ops.loaded) c |= LCE_CAP2_SVC_EXPLODE;
    if (s.n.fire_clear && host() && host()->write_word) c |= LCE_CAP2_SVC_FIRE;
    if (s.n.health_get && s.n.health_set && s.n.health_max && s.n.is_alive) c |= LCE_CAP2_SVC_HEAL;
    if (s.n.stack_set_count && s.n.inventory_set_changed) c |= LCE_CAP2_SVC_HELD_ITEM;
    c |= LCE_CAP2_SVC_DAMAGE | LCE_CAP2_SVC_TELEPORT | LCE_CAP2_SVC_MOTION;   // virtual calls, checked per call
    if (!(s.n.stack_max_damage && s.n.stack_hurt && s.n.stack_set_damage && s.n.stack_set_count && s.n.inventory_set_changed)) c &= ~LCE_CAP2_SVC_HELD_ITEM;
    if (s.ops.set_block && s.ops.spawn && s.ops.loaded) c |= LCE_CAP2_SVC_WORLD;   // give/get_block may stay unbound
#ifdef LCE_HAVE_SVC_1_3
    if (s.n.send_particles) c |= LCE_CAP2_SVC_PARTICLES;
#endif
    if (s.n.send_tinted_particles) c |= LCE_CAP2_SVC_TINTED_PARTICLES;
#ifdef LCE_HAVE_NBT_1_4
    if (nbt_ready() && s.n.inventory_set_changed) c |= LCE_CAP2_SVC_ITEM_NBT;
#endif
#ifdef LCE_HAVE_NET_1_4
    if (s.n.payload_ctor && s.n.alloc_array) c |= LCE_CAP2_SVC_PAYLOAD;
#endif
    if (s.n.compound_ctor && s.n.entity_save && s.n.tag_get) c |= LCE_CAP2_SVC_ENTITY_NBT;
    if (s.n.tagset_insert && s.n.tagset_erase) c |= LCE_CAP2_SVC_ENTITY_DATA;
    if (s.n.sb_payload_ctor && s.n.client_send && s.n.alloc_array) c |= LCE_CAP2_SVC_SEND_TO_SERVER;
    return c;
}

// ---- world -----------------------------------------------------------------
namespace {
// An owned ItemInstance (count, aux) bound to a _Ref_count, as debug_give_195 / lucky item.cpp build it. 0 on failure.
struct OwnedStack { Shared sp; uintptr_t raw = 0; };
bool make_stack(OwnedStack &o, uintptr_t item, int32_t count, int32_t aux) {
    uintptr_t raw = (uintptr_t)s.n.alloc(LCE195_STACK_SIZE);
    if (!raw) return false;
    memset((void *)raw, 0, LCE195_STACK_SIZE);
    s.n.stack_ctor(raw, item, count, aux);
    uintptr_t ctrl = (uintptr_t)s.n.alloc(24);
    if (!ctrl) { s.n.stack_dtor(raw); s.n.free((void *)raw); return false; }
    *(uintptr_t *)ctrl = base() + LCE195_RVA_STACK_REFCOUNT_VT;
    *(uint32_t *)(ctrl + 8) = 1; *(uint32_t *)(ctrl + 12) = 1; *(uintptr_t *)(ctrl + 16) = raw;
    s.n.stack_bind(&o.sp.p, raw, ctrl);
    uintptr_t got = 0; int32_t n = 0, a = -1; uint8_t empty = 1;
    o.raw = raw;
    return o.sp.p.object == raw && o.sp.p.control == ctrl && rd(raw + 0x28, &got, 8) && got == item && rd(raw + 0x10, &n, 4) &&
           n == count && rd(raw + 0x3c, &a, 4) && a == aux && rd(raw + 0x38, &empty, 1) && !empty;
}
// GiveItemCommand path (0x3335a0): Inventory::add(inv, stack) per max-size stack; whatever does not fit is dropped at the
// player's feet through Block::popResource (the command drops it as an item entity too).
lce_status give_native(const lce195_frame *f, uint32_t id, uint32_t count, int32_t aux) {
    if (!f->has_player || !s.n.inventory_add || !s.n.item_by_id) return LCE_E_UNSUPPORTED;
    uintptr_t it = s.n.item_by_id(id), air = 0, inv = 0, p = f->view.player_object, vt = 0, ivt = 0;
    int32_t max = 0;
    if (!it || !rd(base() + 0x1417850, &air, 8) || it == air || !rd(it + 0x18, &max, 4) || max < 1 || max > 64) return LCE_E_INVALID;
    if (!rd(p, &vt, 8) || vt != base() + LCE195_RVA_SERVERPLAYER_VT || !rd(p + LCE195_OFF_PLAYER_INVENTORY, &inv, 8) || !inv ||
        !rd(inv, &ivt, 8) || ivt != base() + LCE195_RVA_INVENTORY_VT) return LCE_E_BACKEND;
    double pos[3];
    if (!rd(p + LCE195_OFF_ENTITY_POS, pos, sizeof pos)) return LCE_E_BACKEND;
    const int32_t at[3] = {(int32_t)floor(pos[0]), (int32_t)floor(pos[1]), (int32_t)floor(pos[2])};
    for (uint32_t left = count; left;) {
        int32_t n = (int32_t)(left > (uint32_t)max ? (uint32_t)max : left);
        OwnedStack o;
        if (!make_stack(o, it, n, aux)) return LCE_E_BACKEND;
        s.n.inventory_add(inv, &o.sp.p);                        // moves what fits; the rest stays in the stack
        int32_t rest = 0; uint8_t empty = 1;
        if (!rd(o.raw + LCE195_OFF_STACK_COUNT, &rest, 4) || !rd(o.raw + LCE195_OFF_STACK_EMPTY, &empty, 1)) return LCE_E_BACKEND;
        if (!empty && rest > 0) s.n.pop_resource(f->view.level, at, &o.sp.p);
        left -= (uint32_t)n;
        if (!frame()) return LCE_E_BACKEND;
    }
    return LCE_OK;
}
} // namespace
extern "C" lce_status lce195_give_item(lce_mod_ctx *, void *player, const char *item, uint32_t count, int32_t aux) {
    const lce195_frame *f = frame_player(player);
    if (!f) return LCE_E_PHASE;
    if (!item || !count || count > 2304 || aux > 32767) return LCE_E_INVALID;
    int32_t id = s.r.item(s.r.user, item);
    if (id <= 0) return LCE_E_NOTFOUND;
    if (s.ops.give) return result(s.ops.give(s.ops.opaque, &f->view, (uint32_t)id, count, aux < 0 ? 0u : (uint32_t)aux));
    return give_native(f, (uint32_t)id, count, aux < 0 ? 0 : aux);
}
extern "C" lce_status lce195_set_block(lce_mod_ctx *, void *level, int32_t x, int32_t y, int32_t z, const char *block, uint32_t data) {
    const lce195_frame *f = frame_level(level);
    if (!f) return LCE_E_PHASE;
    if (!s.ops.set_block) return LCE_E_UNSUPPORTED;
    if (!block || data > 15 || !xyz_ok(x, y, z)) return LCE_E_INVALID;
    int32_t id = s.r.block(s.r.user, block);
    if (id < 0 || id > 4095) return LCE_E_NOTFOUND;
    return result(s.ops.set_block(s.ops.opaque, &f->view, x, y, z, (uint16_t)id, (uint8_t)data));
}
extern "C" int32_t lce195_get_block(lce_mod_ctx *, void *level, int32_t x, int32_t y, int32_t z) {
    const lce195_frame *f = frame_level(level);
    uint16_t id = 0; uint8_t meta = 0;
    if (!f || !s.ops.get_block || !xyz_ok(x, y, z) || s.ops.get_block(s.ops.opaque, &f->view, x, y, z, &id, &meta) != 1) return -1;
    return (int32_t)(id | ((uint32_t)meta << 12));   // native state encoding (0x91bc0)
}
extern "C" lce_status lce195_spawn_entity(lce_mod_ctx *, void *level, const char *entity, int32_t x, int32_t y, int32_t z) {
    const lce195_frame *f = frame_level(level);
    if (!f) return LCE_E_PHASE;
    if (!s.ops.spawn) return LCE_E_UNSUPPORTED;
    if (!entity || !xyz_ok(x, y, z) || strlen(entity) > 64) return LCE_E_INVALID;
    if (s.ops.loaded && s.ops.loaded(s.ops.opaque, &f->view, chunk(x), chunk(z)) != 1) return LCE_E_INVALID;
    return result(s.ops.spawn(s.ops.opaque, &f->view, entity, x + 0.5, (double)y, z + 0.5, 1));
}
extern "C" lce_status lce195_drop_item(lce_mod_ctx *, void *level, int32_t x, int32_t y, int32_t z, const char *item, uint32_t count) {
    const lce195_frame *f = frame_level(level);
    if (!f) return LCE_E_PHASE;
    if (!s.ops.loaded) return LCE_E_UNSUPPORTED;
    if (!item || !count || count > 64 || !xyz_ok(x, y, z)) return LCE_E_INVALID;
    int32_t id = s.r.item(s.r.user, item);
    if (id <= 0) return LCE_E_NOTFOUND;
    uintptr_t it = s.n.item_by_id((unsigned)id), air = 0;
    uint32_t max = 0;
    if (!it || !rd(base() + 0x1417850, &air, 8) || it == air || !rd(it + 0x18, &max, 4) || max < 1 || max > 64 || count > max) return LCE_E_INVALID;
    if (s.ops.loaded(s.ops.opaque, &f->view, chunk(x), chunk(z)) != 1) return LCE_E_INVALID;
    // Same construction as debug_give_195 make() / lucky item.cpp (console-proven).
    uintptr_t raw = (uintptr_t)s.n.alloc(LCE195_STACK_SIZE);
    if (!raw) return LCE_E_BACKEND;
    memset((void *)raw, 0, LCE195_STACK_SIZE);
    s.n.stack_ctor(raw, it, (int32_t)count, 0);
    uintptr_t ctrl = (uintptr_t)s.n.alloc(24);
    if (!ctrl) { s.n.stack_dtor(raw); s.n.free((void *)raw); return LCE_E_BACKEND; }
    *(uintptr_t *)ctrl = base() + LCE195_RVA_STACK_REFCOUNT_VT;
    *(uint32_t *)(ctrl + 8) = 1; *(uint32_t *)(ctrl + 12) = 1; *(uintptr_t *)(ctrl + 16) = raw;
    Shared sp; s.n.stack_bind(&sp.p, raw, ctrl);
    uintptr_t got = 0; int32_t n = 0, aux = -1; uint8_t empty = 1;
    if (sp.p.object != raw || sp.p.control != ctrl || !rd(raw + 0x28, &got, 8) || got != it || !rd(raw + 0x10, &n, 4) ||
        n != (int32_t)count || !rd(raw + 0x3c, &aux, 4) || aux || !rd(raw + 0x38, &empty, 1) || empty) return LCE_E_BACKEND;
    const int32_t pos[3] = {x, y, z};
    s.n.pop_resource(f->view.level, pos, &sp.p);
    return frame() ? LCE_OK : LCE_E_BACKEND;
}

// ---- API 1.2: explode / fire / heal / held item ------------------------------------
namespace {
// Target entity: the frame's player, or a living entity of the frame's level (mod-spawned mobs, event entities).
bool living_target(const lce195_frame *f, uintptr_t e) {
    uintptr_t vt = 0, level = 0, slot = 0;
    const lce195_host *h = host();
    if (!f || !h || !e || !rd(e, &vt, 8) || !vt || !h->is_code || !rd(vt, &slot, 8) || h->is_code(h->opaque, slot) != 1) return false;
    if (e == f->view.player_object) return true;
    // LivingEntity-derived: getHealth slot +0x9b8 is the pinned LivingEntity::getHealth (0x4244d0)
    return rd(e + LCE195_OFF_ENTITY_LEVEL, &level, 8) && level == f->view.level && rd(vt + LCE195_OFF_GET_HEALTH, &slot, 8) &&
           slot == base() + LCE195_RVA_HEALTH_GET;
}
} // namespace
extern "C" lce_status lce195_explode(lce_mod_ctx *, void *level, double x, double y, double z, float radius, int fire, int destroy) {
    const lce195_frame *f = frame_level(level);
    if (!f) return LCE_E_PHASE;
    if (!s.ops.explode || !s.ops.loaded) return LCE_E_UNSUPPORTED;
    if (!(radius > 0.0f && radius <= 16.0f) || !(x == x && y == y && z == z) || !xyz_ok((int32_t)floor(x), (int32_t)floor(y), (int32_t)floor(z)))
        return LCE_E_INVALID;
    // The r72 helper requires every chunk within 2r+1 blocks to be loaded (never generates one).
    int32_t r = (int32_t)ceil(2.0 * radius + 1.0);
    for (int32_t cx = chunk((int32_t)floor(x) - r); cx <= chunk((int32_t)floor(x) + r); ++cx)
        for (int32_t cz = chunk((int32_t)floor(z) - r); cz <= chunk((int32_t)floor(z) + r); ++cz)
            if (s.ops.loaded(s.ops.opaque, &f->view, cx, cz) != 1) return LCE_E_INVALID;
    return result(s.ops.explode(s.ops.opaque, &f->view, x, y, z, radius, fire ? 1 : 0, destroy ? 1 : 0));
}
extern "C" lce_status lce195_set_fire(lce_mod_ctx *, void *entity, uint32_t ticks) {
    const lce195_frame *f = frame();
    if (!f) return LCE_E_PHASE;
    const lce195_host *h = host();
    if (!s.n.fire_clear || !h->write_word) return LCE_E_UNSUPPORTED;
    uintptr_t e = entity ? (uintptr_t)entity : f->view.player_object;
    if (!living_target(f, e) || ticks > 20u * 60 * 60) return LCE_E_INVALID;
    uint8_t removed = 1;
    if (!rd(e + LCE195_OFF_ENTITY_REMOVED, &removed, 1) || removed) return LCE_E_INVALID;
    int32_t now = 0;
    if (!rd(e + LCE195_OFF_FIRE_TICKS, &now, 4)) return LCE_E_BACKEND;
    if (!ticks) {
        s.n.fire_clear(e);                                   // native clearFire (0x2762a0)
        return rd(e + LCE195_OFF_FIRE_TICKS, &now, 4) && now == 0 ? LCE_OK : LCE_E_BACKEND;
    }
    if (now >= (int32_t)ticks) return LCE_OK;                // like Entity::setSecondsOnFire: never shortens
    // +0x1c0 is 8-aligned: CAS the containing word, keeping the high half (the next field) unchanged.
    uintptr_t word = 0;
    if (!rd(e + LCE195_OFF_FIRE_TICKS, &word, 8)) return LCE_E_BACKEND;
    uintptr_t want = (word & ~(uintptr_t)0xffffffffu) | (uint32_t)ticks;
    if (h->write_word(h->opaque, e + LCE195_OFF_FIRE_TICKS, word, want) != 1) return LCE_E_BACKEND;
    return rd(e + LCE195_OFF_FIRE_TICKS, &now, 4) && now == (int32_t)ticks ? LCE_OK : LCE_E_BACKEND;
}
extern "C" lce_status lce195_heal(lce_mod_ctx *, void *entity, uint32_t milli) {
    const lce195_frame *f = frame();
    if (!f) return LCE_E_PHASE;
    if (!s.n.health_get || !s.n.health_set || !s.n.health_max || !s.n.is_alive) return LCE_E_UNSUPPORTED;
    uintptr_t e = entity ? (uintptr_t)entity : f->view.player_object;
    if (!living_target(f, e) || !milli || milli > 1024000u) return LCE_E_INVALID;
    if (!s.n.is_alive(e)) return LCE_E_INVALID;               // Java heal: no effect on dead entities
    float cur = s.n.health_get(e), max = s.n.health_max(e);
    if (!(cur == cur) || !(max > 0.0f) || max > 1024.0f) return LCE_E_BACKEND;
    float want = cur + milli / 1000.0f;
    if (want > max) want = max;
    if (want == cur) return LCE_OK;
    s.n.health_set(e, want);                                  // native setHealth (clamps, marks dirty, notifies)
    float got = s.n.health_get(e);
    return got == want ? LCE_OK : LCE_E_BACKEND;
}
namespace {
// The player's selected hotbar stack, with the r72 nbt_item_count_195 ownership checks. 0 ok, else an lce_status.
struct Held { uintptr_t inv, stack; int32_t count; };
lce_status held_stack(uintptr_t p, Held &h) {
    uintptr_t vt = 0, inv = 0, ictl = 0, ivt = 0, back = 0, items = 0, stack = 0, sctl = 0, svt = 0, empty = 0;
    int32_t selected = -1, have = 0, strong = 0; uint8_t is_empty = 1;
    if (!rd(p, &vt, 8) || vt != base() + LCE195_RVA_SERVERPLAYER_VT ||
        !rd(p + LCE195_OFF_PLAYER_INVENTORY, &inv, 8) || !rd(p + LCE195_OFF_PLAYER_INVENTORY + 8, &ictl, 8) || !inv || !ictl ||
        !rd(ictl, &ivt, 8) || ivt != base() + LCE195_RVA_INVENTORY_REF_VT || !rd(ictl + 8, &strong, 4) || strong <= 0 ||
        !rd(inv, &ivt, 8) || ivt != base() + LCE195_RVA_INVENTORY_VT || !rd(inv + LCE195_OFF_INV_PLAYER, &back, 8) || back != p ||
        !rd(inv + LCE195_OFF_INV_SELECTED, &selected, 4) || selected < 0 || selected > 8 || !rd(inv + LCE195_OFF_INV_ITEMS, &items, 8) || !items)
        return LCE_E_BACKEND;
    if (!rd(items + 24u * (uint32_t)selected, &stack, 8) || !rd(items + 24u * (uint32_t)selected + 8, &sctl, 8) || !stack || !sctl ||
        !rd(sctl, &svt, 8) || svt != base() + LCE195_RVA_STACK_REFCOUNT_VT || !rd(base() + LCE195_RVA_EMPTY_STACK, &empty, 8))
        return LCE_E_BACKEND;
    if (stack == empty || !rd(stack + LCE195_OFF_STACK_EMPTY, &is_empty, 1) || is_empty) return LCE_E_INVALID;   // empty hand
    if (!rd(stack + LCE195_OFF_STACK_COUNT, &have, 4)) return LCE_E_BACKEND;
    h = Held{inv, stack, have};
    return LCE_OK;
}
} // namespace
extern "C" lce_status lce195_consume_held_item(lce_mod_ctx *, void *player, uint32_t count) {
    const lce195_frame *f = frame_player(player);
    if (!f) return LCE_E_PHASE;
    if (!s.n.stack_set_count || !s.n.inventory_set_changed) return LCE_E_UNSUPPORTED;
    if (!count || count > 64) return LCE_E_INVALID;
    Held h{};
    lce_status st = held_stack(f->view.player_object, h);
    if (st != LCE_OK) return st;
    if (h.count < (int32_t)count) return LCE_E_INVALID;
    s.n.stack_set_count(h.stack, h.count - (int32_t)count);   // native setCount (0 marks the stack empty, like shrink)
    s.n.inventory_set_changed(h.inv);                         // revision bump: the container sync sends the slot
    int32_t now = -1;
    return rd(h.stack + LCE195_OFF_STACK_COUNT, &now, 4) && now == h.count - (int32_t)count ? LCE_OK : LCE_E_BACKEND;
}
// ItemStack.hurtAndBreak without the player-specific break broadcast: native hurt(stack, n, level random) applies Unbreaking;
// when it reports broken, shrink by one and reset the damage (as Java does).
extern "C" lce_status lce195_damage_held_item(lce_mod_ctx *, void *player, uint32_t amount) {
    const lce195_frame *f = frame_player(player);
    if (!f) return LCE_E_PHASE;
    if (!s.n.stack_max_damage || !s.n.stack_hurt || !s.n.stack_set_damage || !s.n.stack_set_count || !s.n.inventory_set_changed) return LCE_E_UNSUPPORTED;
    if (!amount || amount > 32767) return LCE_E_INVALID;
    Held h{};
    lce_status st = held_stack(f->view.player_object, h);
    if (st != LCE_OK) return st;
    uintptr_t rng = 0;
    if (s.n.stack_max_damage(h.stack) <= 0) return LCE_E_INVALID;                // no durability
    if (!rd(f->view.level + LCE195_OFF_LEVEL_RANDOM, &rng, 8) || !rng) return LCE_E_BACKEND;
    if (s.n.stack_hurt(h.stack, (int32_t)amount, rng)) {
        s.n.stack_set_count(h.stack, h.count - 1);
        s.n.stack_set_damage(h.stack, 0);
    }
    s.n.inventory_set_changed(h.inv);
    return LCE_OK;
}
namespace {
bool entity_target(const lce195_frame *f, uintptr_t e) {
    uintptr_t vt = 0, level = 0, slot = 0;
    const lce195_host *h = host();
    if (!f || !h || !e || !rd(e, &vt, 8) || !vt || !h->is_code || !rd(vt, &slot, 8) || h->is_code(h->opaque, slot) != 1) return false;
    if (e == f->view.player_object) return true;
    return rd(e + LCE195_OFF_ENTITY_LEVEL, &level, 8) && level == f->view.level;
}
bool vslot(uintptr_t e, uint32_t off, uintptr_t *fn) {
    uintptr_t vt = 0;
    return rd(e, &vt, 8) && rd(vt + off, fn, 8) && host()->is_code && host()->is_code(host()->opaque, *fn) == 1;
}
} // namespace
extern "C" lce_status lce195_teleport(lce_mod_ctx *, void *entity, double x, double y, double z) {
    const lce195_frame *f = frame();
    if (!f) return LCE_E_PHASE;
    uintptr_t e = entity ? (uintptr_t)entity : (f->has_player ? f->view.player_object : 0), fn = 0;
    if (!entity_target(f, e) || !(x == x && y == y && z == z) || !xyz_ok((int32_t)floor(x), (int32_t)floor(y), (int32_t)floor(z)))
        return LCE_E_INVALID;
    if (s.ops.loaded && s.ops.loaded(s.ops.opaque, &f->view, chunk((int32_t)floor(x)), chunk((int32_t)floor(z))) != 1) return LCE_E_INVALID;
    if (!vslot(e, LCE195_OFF_TELEPORT, &fn)) return LCE_E_BACKEND;
    ((void (*)(uintptr_t, double, double, double))fn)(e, x, y, z);   // virtual teleportTo (ServerPlayer: connection teleport)
    double pos[3];
    return rd(e + LCE195_OFF_ENTITY_POS, pos, sizeof pos) && pos[0] == x && pos[1] == y && pos[2] == z ? LCE_OK : LCE_E_BACKEND;
}
extern "C" lce_status lce195_set_motion(lce_mod_ctx *, void *entity, double dx, double dy, double dz) {
    const lce195_frame *f = frame();
    if (!f) return LCE_E_PHASE;
    uintptr_t e = entity ? (uintptr_t)entity : (f->has_player ? f->view.player_object : 0), fn = 0;
    if (!entity_target(f, e) || !(dx >= -10 && dx <= 10 && dy >= -10 && dy <= 10 && dz >= -10 && dz <= 10)) return LCE_E_INVALID;
    double cur[3];
    if (!rd(e + LCE195_OFF_DELTA, cur, sizeof cur) || !vslot(e, LCE195_OFF_PUSH, &fn)) return LCE_E_BACKEND;
    // virtual push adds to deltaMovement and sets hasImpulse (the tracker then sends the motion packet)
    ((void (*)(uintptr_t, double, double, double))fn)(e, dx - cur[0], dy - cur[1], dz - cur[2]);
    double now[3];
    if (!rd(e + LCE195_OFF_DELTA, now, sizeof now)) return LCE_E_BACKEND;
    for (int i = 0; i < 3; ++i) if (fabs(now[i] - (i == 0 ? dx : i == 1 ? dy : dz)) > 1e-9) return LCE_E_BACKEND;
    return LCE_OK;
}

// LivingEntity::hurt (+0x248) with DamageSource::GENERIC (the static at 0x1413b68, checked against the DamageSource vtable).
// An attacker would need an EntityDamageSource (not constructed yet): refused rather than silently attributed to nobody.
extern "C" lce_status lce195_damage_entity(lce_mod_ctx *, void *entity, uint32_t milli, void *attacker) {
    const lce195_frame *f = frame();
    if (!f) return LCE_E_PHASE;
    const uintptr_t att = (uintptr_t)attacker;
    const bool by_player = att && f->has_player && att == f->view.player_object && f->view.player_control;
    if (att && by_player && !s.n.player_attack_source) return LCE_E_UNSUPPORTED;
    if (att && !by_player && (!s.n.mob_attack_source || !s.n.weak_lock)) return LCE_E_UNSUPPORTED;
    uintptr_t e = entity ? (uintptr_t)entity : (f->has_player && !att ? f->view.player_object : 0);
    if (!e || !living_target(f, e) || !milli || milli > 4096000u || (att && e == att)) return LCE_E_INVALID;
    if (att && !by_player && !living_target(f, att)) return LCE_E_INVALID;   // mob attackers: a living entity of this level
    uintptr_t src = 0, svt = 0, vt = 0, hurt = 0, del = 0;
    uint8_t removed = 1;
    if (!rd(e, &vt, 8) || !rd(vt + LCE195_OFF_HURT, &hurt, 8) || !host()->is_code || host()->is_code(host()->opaque, hurt) != 1)
        return LCE_E_BACKEND;
    if (!rd(e + LCE195_OFF_ENTITY_REMOVED, &removed, 1) || removed) return LCE_E_INVALID;
    if (!att) {
        if (!rd(base() + LCE195_RVA_DMG_GENERIC, &src, 8) || !src || !rd(src, &svt, 8) || svt != base() + LCE195_RVA_DAMAGESOURCE_VT)
            return LCE_E_BACKEND;
        // bool hurt(Entity*, DamageSource*, float) - the same call CactusBlock::entityInside 0x1596fb makes
        return ((uint8_t (*)(uintptr_t, uintptr_t, float))hurt)(e, src, milli / 1000.0f) ? LCE_OK : LCE_E_INVALID;
    }
    Shared mob;   // released after the source is gone (the source holds its own strong copy meanwhile)
    if (by_player) {
        // Player::attack / Parrot's cookie: src = playerAttack(&player); hurt; delete src.
        lce195_pair pp{f->view.player_object, f->view.player_control};
        src = s.n.player_attack_source(&pp);
    } else {
        // Mob::doHurtTarget (Cat 0x165cbf..0x165e38): lock the attacker's weak_ptr<this>, mobAttack(&sp), hurt, delete.
        uintptr_t wobj = 0, wctl = 0;
        uint8_t att_removed = 1;
        if (!rd(att + LCE195_OFF_ENTITY_WEAK_THIS, &wobj, 8) || !rd(att + LCE195_OFF_ENTITY_WEAK_THIS + 8, &wctl, 8) || wobj != att || !wctl)
            return LCE_E_INVALID;   // not shared-owned (yet): no safe control block
        if (!rd(att + LCE195_OFF_ENTITY_REMOVED, &att_removed, 1) || att_removed) return LCE_E_INVALID;
        s.n.weak_lock(&mob.p, wobj, wctl, 0);
        if (mob.p.object != att || mob.p.control != wctl) return LCE_E_INVALID;   // expired
        src = s.n.mob_attack_source(&mob.p);
    }
    if (!src) return LCE_E_BACKEND;
    if (!rd(src, &svt, 8) || svt != base() + LCE195_RVA_ENTITY_DAMAGESOURCE_VT || !rd(svt + 8, &del, 8) || host()->is_code(host()->opaque, del) != 1)
        return LCE_E_BACKEND;   // unexpected object: leak it rather than free through an unknown table
    uint8_t took = ((uint8_t (*)(uintptr_t, uintptr_t, float))hurt)(e, src, milli / 1000.0f);
    ((void (*)(uintptr_t))del)(src);   // deleting destructor (EntityDamageSource 0x2ba860 releases its copy)
    return took ? LCE_OK : LCE_E_INVALID;   // false: invulnerable, hurt cooldown, PvP off or cancelled
}

// ---- item NBT (API 1.3): the main-hand stack's CompoundTag --------------------------------------------
namespace {
bool nbt_key(const char *k, uint16_t *u, size_t *n) {
    size_t l = k ? strlen(k) : 0;
    if (!l || l > 64) return false;
    for (size_t i = 0; i < l; ++i) {
        char c = k[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == ':' || c == '.' || c == '-')) return false;
        u[i] = (uint8_t)c;
    }
    *n = l; return true;
}
bool nbt_ready() { return s.n.tag_get_or_create && s.n.tag_get && s.n.tag_remove && s.n.tag_put_int && s.n.tag_put_double && s.n.tag_put_string; }
// Held stack of the frame's player (or `player` when it is the frame's), plus its existing tag (0 = none).
lce_status nbt_target(void *player, Held &h, uintptr_t *tag) {
    const lce195_frame *f = frame_player(player);
    if (!f) return LCE_E_PHASE;
    if (!f->has_player) return LCE_E_INVALID;
    if (!nbt_ready() || !s.n.inventory_set_changed) return LCE_E_UNSUPPORTED;
    lce_status st = held_stack(f->view.player_object, h);
    if (st != LCE_OK) return st;
    *tag = 0;
    if (!rd(h.stack + LCE195_OFF_STACK_TAG, tag, 8)) return LCE_E_BACKEND;
    uintptr_t vt = 0;
    if (*tag && (!rd(*tag, &vt, 8) || vt != base() + LCE195_RVA_COMPOUNDTAG_VT)) return LCE_E_BACKEND;
    return LCE_OK;
}
// Scalar Tag* -> lce_nbt_value (byte/short/int/long -> INT, float/double -> DOUBLE, string -> ASCII STRING).
lce_status tag_value(uintptr_t t, lce_nbt_value *out) {
    uintptr_t vt = 0;
    if (!rd(t, &vt, 8)) return LCE_E_BACKEND;
    const uintptr_t b = base();
    int8_t i8; int16_t i16; int32_t i32; int64_t i64; float f32; double f64;
    if (vt == b + LCE195_RVA_BYTETAG_VT && rd(t + 8, &i8, 1)) { out->type = LCE_NBT_INT; out->i = i8; }
    else if (vt == b + LCE195_RVA_SHORTTAG_VT && rd(t + 8, &i16, 2)) { out->type = LCE_NBT_INT; out->i = i16; }
    else if (vt == b + LCE195_RVA_INTTAG_VT && rd(t + 8, &i32, 4)) { out->type = LCE_NBT_INT; out->i = i32; }
    else if (vt == b + LCE195_RVA_LONGTAG_VT && rd(t + 8, &i64, 8)) { out->type = LCE_NBT_INT; out->i = i64; }
    else if (vt == b + LCE195_RVA_FLOATTAG_VT && rd(t + 8, &f32, 4)) { out->type = LCE_NBT_DOUBLE; out->d = f32; }
    else if (vt == b + LCE195_RVA_DOUBLETAG_VT && rd(t + 8, &f64, 8)) { out->type = LCE_NBT_DOUBLE; out->d = f64; }
    else if (vt == b + LCE195_RVA_STRINGTAG_VT) {   // wstring at +8: buffer +0x10 (inline when cap < 8), size +0x20, cap +0x28
        uint64_t size = 0, cap = 0; uintptr_t buf = t + 0x10;
        if (!rd(t + 0x20, &size, 8) || !rd(t + 0x28, &cap, 8) || size > 4096 || (cap >= 8 && !rd(t + 0x10, &buf, 8))) return LCE_E_BACKEND;
        size_t w = 0;
        for (uint64_t i = 0; i < size && w + 1 < sizeof out->s; ++i) {
            uint16_t c = 0;
            if (!rd(buf + 2 * i, &c, 2)) return LCE_E_BACKEND;
            out->s[w++] = c < 0x80 ? (char)c : '?';   // ASCII view; non-ASCII units become '?'
        }
        out->s[w] = 0; out->type = LCE_NBT_STRING;
    } else return LCE_E_INVALID;   // compound/list/array: not exposed
    return LCE_OK;
}
} // namespace
extern "C" lce_status lce195_item_tag_get(lce_mod_ctx *, void *player, const char *key, lce_nbt_value *out) {
    uint16_t u[64]; size_t n = 0;
    if (!out || !nbt_key(key, u, &n)) return LCE_E_INVALID;
    memset(out, 0, sizeof *out);
    Held h{}; uintptr_t tag = 0;
    lce_status st = nbt_target(player, h, &tag);
    if (st != LCE_OK) return st;
    if (!tag) return LCE_E_NOTFOUND;
    WString k; s.n.ws_assign(k.w, u, n);
    uintptr_t t = s.n.tag_get(tag, k.w);
    if (!t) return LCE_E_NOTFOUND;
    return tag_value(t, out);
}
extern "C" lce_status lce195_item_tag_set(lce_mod_ctx *, void *player, const char *key, const lce_nbt_value *v) {
    uint16_t u[64]; size_t n = 0;
    if (!v || !nbt_key(key, u, &n)) return LCE_E_INVALID;
    if (v->type == LCE_NBT_INT && (v->i < INT32_MIN || v->i > INT32_MAX)) return LCE_E_INVALID;
    if (v->type == LCE_NBT_DOUBLE && !(v->d == v->d)) return LCE_E_INVALID;
    size_t sl = 0;
    if (v->type == LCE_NBT_STRING) while (sl < sizeof v->s && v->s[sl]) ++sl;
    if (v->type == LCE_NBT_STRING && sl >= sizeof v->s) return LCE_E_INVALID;
    if (v->type != LCE_NBT_INT && v->type != LCE_NBT_DOUBLE && v->type != LCE_NBT_STRING) return LCE_E_INVALID;
    Held h{}; uintptr_t tag = 0;
    lce_status st = nbt_target(player, h, &tag);
    if (st != LCE_OK) return st;
    tag = s.n.tag_get_or_create(h.stack);
    uintptr_t vt = 0;
    if (!tag || !rd(tag, &vt, 8) || vt != base() + LCE195_RVA_COMPOUNDTAG_VT) return LCE_E_BACKEND;
    WString k; s.n.ws_assign(k.w, u, n);
    if (v->type == LCE_NBT_INT) s.n.tag_put_int(tag, k.w, (int32_t)v->i);
    else if (v->type == LCE_NBT_DOUBLE) s.n.tag_put_double(tag, k.w, v->d);
    else {
        uint16_t su[256];
        for (size_t i = 0; i < sl; ++i) su[i] = (uint8_t)v->s[i];
        WString val; s.n.ws_assign(val.w, su, sl);
        s.n.tag_put_string(tag, k.w, val.w);
    }
    s.n.inventory_set_changed(h.inv);   // the container broadcast resends the slot
    return LCE_OK;
}
extern "C" lce_status lce195_item_tag_remove(lce_mod_ctx *, void *player, const char *key) {
    uint16_t u[64]; size_t n = 0;
    if (!nbt_key(key, u, &n)) return LCE_E_INVALID;
    Held h{}; uintptr_t tag = 0;
    lce_status st = nbt_target(player, h, &tag);
    if (st != LCE_OK) return st;
    if (!tag) return LCE_E_NOTFOUND;
    WString k; s.n.ws_assign(k.w, u, n);
    if (!s.n.tag_get(tag, k.w)) return LCE_E_NOTFOUND;
    s.n.tag_remove(tag, k.w);
    s.n.inventory_set_changed(h.inv);
    return LCE_OK;
}

// ---- entity NBT (API 1.5) ------------------------------------------------------------------------------
namespace {
// The target entity of the frame (NULL = its player), any entity of the frame's level.
uintptr_t nbt_entity(const lce195_frame *f, void *entity) {
    uintptr_t e = entity ? (uintptr_t)entity : (f->has_player ? f->view.player_object : 0);
    return e && entity_target(f, e) ? e : 0;
}
// A temporary CompoundTag owned by the module, deleted through its own vtable (+0x30 deleting dtor) on every path.
struct TempTag {
    uintptr_t t = 0;
    ~TempTag() {
        uintptr_t vt = 0, del = 0;
        if (t && rd(t, &vt, 8) && vt == base() + LCE195_RVA_COMPOUNDTAG_VT && rd(vt + 0x30, &del, 8) && host()->is_code &&
            host()->is_code(host()->opaque, del) == 1) ((void (*)(uintptr_t))del)(t);
    }
};
// One path segment: key [A-Za-z0-9_:.-]{1,64}, optional [n] / [#].
const char *nbt_segment(const char *p, uint16_t *u, size_t *n, int64_t *index) {
    size_t l = 0;
    *index = -1;
    while (*p && *p != '/' && *p != '[') {
        char c = *p++;
        if (l >= 64 || !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == ':' || c == '.' || c == '-'))
            return nullptr;
        u[l++] = (uint8_t)c;
    }
    if (!l) return nullptr;
    *n = l;
    if (*p == '[') {
        ++p;
        if (*p == '#' && p[1] == ']') { *index = -2; p += 2; }
        else {
            int64_t v = 0; int digits = 0;
            while (*p >= '0' && *p <= '9' && digits < 6) { v = v * 10 + (*p++ - '0'); ++digits; }
            if (!digits || *p != ']') return nullptr;
            *index = v; ++p;
        }
    }
    if (*p == '/') { ++p; if (!*p) return nullptr; }
    else if (*p) return nullptr;
    return p;
}
} // namespace
extern "C" lce_status lce195_entity_nbt_get(lce_mod_ctx *, void *entity, const char *path, lce_nbt_value *out) {
    if (!out || !path || !*path || strlen(path) > 256) return LCE_E_INVALID;
    memset(out, 0, sizeof *out);
    const lce195_frame *f = frame();
    if (!f) return LCE_E_PHASE;
    if (!s.n.compound_ctor || !s.n.entity_save || !s.n.tag_get) return LCE_E_UNSUPPORTED;
    uintptr_t e = nbt_entity(f, entity);
    if (!e) return LCE_E_INVALID;
    { uint16_t u[64]; size_t n; int64_t ix;   // validate the whole path before saving anything
      for (const char *p = path; *p;) if (!(p = nbt_segment(p, u, &n, &ix))) return LCE_E_INVALID; }
    TempTag tmp;
    tmp.t = (uintptr_t)s.n.alloc(LCE195_COMPOUND_SIZE);
    if (!tmp.t) return LCE_E_BACKEND;
    memset((void *)tmp.t, 0, LCE195_COMPOUND_SIZE);
    s.n.compound_ctor(tmp.t);
    uintptr_t vt = 0;
    if (!rd(tmp.t, &vt, 8) || vt != base() + LCE195_RVA_COMPOUNDTAG_VT) { tmp.t = 0; return LCE_E_BACKEND; }   // unknown object: leak it
    if (s.n.entity_save(e, tmp.t) != tmp.t) return LCE_E_BACKEND;   // the call Entity::save makes; returns its tag
    uintptr_t cur = tmp.t;
    for (const char *p = path; *p;) {
        uint16_t u[64]; size_t n = 0; int64_t ix = -1;
        p = nbt_segment(p, u, &n, &ix);
        if (!rd(cur, &vt, 8) || vt != base() + LCE195_RVA_COMPOUNDTAG_VT) return LCE_E_INVALID;   // a key into a non-compound
        WString k; s.n.ws_assign(k.w, u, n);
        uintptr_t t = s.n.tag_get(cur, k.w);
        if (!t) return LCE_E_NOTFOUND;
        if (ix != -1) {
            uintptr_t lb = 0, le = 0;
            if (!rd(t, &vt, 8) || vt != base() + LCE195_RVA_LISTTAG_VT) return LCE_E_INVALID;
            if (!rd(t + 0x10, &lb, 8) || !rd(t + 0x18, &le, 8) || le < lb || (le - lb) % 8 || (le - lb) / 8 > (1u << 20)) return LCE_E_BACKEND;
            const uint64_t count = (le - lb) / 8;
            if (ix == -2) {
                if (*p) return LCE_E_INVALID;   // [#] ends the path
                out->type = LCE_NBT_INT; out->i = (int64_t)count;
                return LCE_OK;
            }
            if ((uint64_t)ix >= count) return LCE_E_NOTFOUND;
            if (!rd(lb + 8 * (uint64_t)ix, &t, 8) || !t) return LCE_E_BACKEND;
        }
        cur = t;
    }
    return tag_value(cur, out);
}

namespace {
// Persistent mod data = entity scoreboard tags "lce|<key>=<i|d|s><value>" in the Entity +0x360 std::set<wstring>.
constexpr uint32_t kDataUnits = 32768, kDataValue = 200, kNodeUnits = 4096;
uint16_t g_units[kDataUnits], g_node[kNodeUnits];
uint32_t g_off[LCE195_MAX_ENTITY_TAGS], g_len[LCE195_MAX_ENTITY_TAGS];
struct TagSet { uintptr_t set, head; uint64_t size; };
bool tagset(uintptr_t e, TagSet &t) {
    uint8_t nil = 0;
    t.set = e + LCE195_OFF_ENTITY_TAGS;
    return rd(t.set + 8, &t.head, 8) && t.head && rd(t.head + 0x19, &nil, 1) && nil == 1 && rd(t.set + 0x10, &t.size, 8) &&
           t.size <= 4 * LCE195_MAX_ENTITY_TAGS;
}
// In-order successor in a Dinkumware tree (the head is the nil sentinel). 0 on a read failure.
uintptr_t tree_next(uintptr_t n) {
    uintptr_t r = 0, p = 0; uint8_t nil = 1;
    if (!rd(n + 0x10, &r, 8) || !rd(r + 0x19, &nil, 1)) return 0;
    if (!nil) {
        for (uint32_t i = 0; i < 64; ++i) {
            uintptr_t l = 0;
            if (!rd(r, &l, 8) || !rd(l + 0x19, &nil, 1)) return 0;
            if (nil) return r;
            r = l;
        }
        return 0;
    }
    for (uint32_t i = 0; i < 64; ++i) {
        uintptr_t pr = 0;
        if (!rd(n + 8, &p, 8) || !rd(p + 0x19, &nil, 1)) return 0;
        if (nil || !rd(p + 0x10, &pr, 8) || pr != n) return p;
        n = p;
    }
    return 0;
}
// Node value units (wstring at +0x20: buffer +0x28 inline when cap +0x40 < 8, size +0x38).
bool node_units(uintptr_t n, uint16_t *out, uint32_t cap, uint32_t *len) {
    uint64_t size = 0, c = 0; uintptr_t buf = n + 0x28;
    if (!rd(n + 0x38, &size, 8) || !rd(n + 0x40, &c, 8) || size > cap || size > c || (c >= 8 && !rd(n + 0x28, &buf, 8))) return false;
    if (size && !rd(buf, out, 2 * size)) return false;
    *len = (uint32_t)size; return true;
}
// Walk the set in order; fn(units, len) per entry until it returns false. false on a malformed tree.
template <class F> bool tagset_walk(const TagSet &t, F fn) {
    uintptr_t n = 0;
    if (!rd(t.head, &n, 8)) return false;   // head->left = leftmost
    uint64_t seen = 0;
    while (n && n != t.head) {
        uint32_t l = 0;
        if (++seen > t.size || !node_units(n, g_node, kNodeUnits, &l)) return false;
        if (!fn(g_node, l)) return true;
        n = tree_next(n);
    }
    return n == t.head && seen == t.size;
}
uint32_t data_prefix(const uint16_t *key, size_t kn, uint16_t *out) {   // "lce|<key>="
    uint32_t n = 0;
    for (const char *p = "lce|"; *p; ++p) out[n++] = (uint8_t)*p;
    for (size_t i = 0; i < kn; ++i) out[n++] = key[i];
    out[n++] = '=';
    return n;
}
bool has_prefix(const uint16_t *u, uint32_t l, const uint16_t *pre, uint32_t pn) {
    if (l < pn) return false;
    for (uint32_t i = 0; i < pn; ++i) if (u[i] != pre[i]) return false;
    return true;
}
uint32_t encode_value(const lce_nbt_value *v, uint16_t *out) {
    uint32_t n = 0;
    if (v->type == LCE_NBT_INT) {
        out[n++] = 'i';
        const int64_t x = v->i; char d[24]; int k = 0;
        uint64_t m = x < 0 ? (uint64_t)(-(x + 1)) + 1 : (uint64_t)x;
        do { d[k++] = (char)('0' + m % 10); m /= 10; } while (m);
        if (x < 0) out[n++] = '-';
        while (k) out[n++] = (uint8_t)d[--k];
    } else if (v->type == LCE_NBT_DOUBLE) {   // exact: the IEEE bits in hex
        out[n++] = 'd';
        uint64_t bits; memcpy(&bits, &v->d, 8);
        for (int i = 15; i >= 0; --i) out[n++] = (uint8_t)"0123456789abcdef"[(bits >> (4 * i)) & 15];
    } else {
        out[n++] = 's';
        for (size_t i = 0; v->s[i]; ++i) out[n++] = (uint8_t)v->s[i];
    }
    return n;
}
bool decode_value(const uint16_t *u, uint32_t l, lce_nbt_value *out) {
    memset(out, 0, sizeof *out);
    if (!l) return false;
    if (u[0] == 'i') {
        uint32_t i = 1; const bool neg = l > 1 && u[1] == '-';
        if (neg) ++i;
        if (i >= l || l - i > 10) return false;
        int64_t m = 0;
        for (; i < l; ++i) { if (u[i] < '0' || u[i] > '9') return false; m = m * 10 + (u[i] - '0'); }
        out->type = LCE_NBT_INT; out->i = neg ? -m : m;
        return true;
    }
    if (u[0] == 'd') {
        if (l != 17) return false;
        uint64_t bits = 0;
        for (uint32_t i = 1; i < 17; ++i) {
            const uint16_t c = u[i];
            const int h = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
            if (h < 0) return false;
            bits = bits << 4 | (uint64_t)h;
        }
        out->type = LCE_NBT_DOUBLE; memcpy(&out->d, &bits, 8);
        return true;
    }
    if (u[0] == 's' && l - 1 < sizeof out->s) {
        for (uint32_t i = 1; i < l; ++i) out->s[i - 1] = u[i] < 0x80 ? (char)u[i] : '?';
        out->type = LCE_NBT_STRING;
        return true;
    }
    return false;
}
// Insert one entry with the native insert (it copies the string), as Entity::load 0x27daf9 does.
void tagset_add(const TagSet &t, const uint16_t *u, uint32_t n) {
    WString w; s.n.ws_assign(w.w, u, n);
    uint64_t out[8] = {};
    s.n.tagset_insert(out, t.set, 0, w.w);
}
struct DataTarget { const lce195_frame *f; uintptr_t e; TagSet t; };
lce_status data_target(void *entity, bool write, DataTarget &d) {
    d.f = frame();
    if (!d.f) return LCE_E_PHASE;
    if (!s.n.tagset_insert || !s.n.tagset_erase) return LCE_E_UNSUPPORTED;
    d.e = nbt_entity(d.f, entity);
    if (!d.e) return LCE_E_INVALID;
    uintptr_t vt = 0;
    if (write && (!rd(d.e, &vt, 8) || vt == base() + LCE195_RVA_SERVERPLAYER_VT || (d.f->has_player && d.e == d.f->view.player_object)))
        return LCE_E_INVALID;   // never players (their tags are theirs, and a player save would carry mod data across worlds)
    return tagset(d.e, d.t) ? LCE_OK : LCE_E_BACKEND;
}
// Replace the whole set by every entry not starting with `skip`, plus `add` (an units, 0 = none): the natives and the head
// reset of Entity::load 0x27da18..0x27db13. Every entry is copied out and checked before the set is touched.
lce_status tagset_rebuild(const TagSet &t, const uint16_t *skip, uint32_t sn, const uint16_t *add, uint32_t an) {
    uint32_t count = 0, used = 0; bool room = true;
    const bool ok = tagset_walk(t, [&](const uint16_t *u, uint32_t l) {
        if (has_prefix(u, l, skip, sn)) return true;
        if (count >= LCE195_MAX_ENTITY_TAGS || used + l > kDataUnits) { room = false; return false; }
        memcpy(g_units + used, u, 2 * (size_t)l); g_off[count] = used; g_len[count] = l; used += l; ++count;
        return true;
    });
    if (!ok) return LCE_E_BACKEND;
    if (!room || (an && count >= LCE195_MAX_ENTITY_TAGS)) return LCE_E_FULL;
    uintptr_t root = 0;
    if (!rd(t.head + 8, &root, 8) || !root) return LCE_E_BACKEND;
    s.n.tagset_erase(t.set, root);                    // _Erase(set, root): frees every node and its string
    const uintptr_t h = t.head; const uint64_t zero = 0;
    memcpy((void *)(h + 8), &h, 8); memcpy((void *)h, &h, 8); memcpy((void *)(h + 0x10), &h, 8);
    memcpy((void *)(t.set + 0x10), &zero, 8);
    for (uint32_t i = 0; i < count; ++i) tagset_add(t, g_units + g_off[i], g_len[i]);
    if (an) tagset_add(t, add, an);
    uint64_t size = 0;
    return rd(t.set + 0x10, &size, 8) && size == count + (an ? 1u : 0u) ? LCE_OK : LCE_E_BACKEND;
}
} // namespace
extern "C" lce_status lce195_entity_data_get(lce_mod_ctx *, void *entity, const char *key, lce_nbt_value *out) {
    uint16_t k[64], pre[80]; size_t kn = 0;
    if (!out || !nbt_key(key, k, &kn)) return LCE_E_INVALID;
    memset(out, 0, sizeof *out);
    DataTarget d{};
    lce_status st = data_target(entity, false, d);
    if (st != LCE_OK) return st;
    const uint32_t pn = data_prefix(k, kn, pre);
    lce_status found = LCE_E_NOTFOUND;
    if (!tagset_walk(d.t, [&](const uint16_t *u, uint32_t l) {
            if (!has_prefix(u, l, pre, pn)) return true;
            found = decode_value(u + pn, l - pn, out) ? LCE_OK : LCE_E_INVALID;
            return false;
        })) return LCE_E_BACKEND;
    return found;
}
extern "C" lce_status lce195_entity_data_set(lce_mod_ctx *, void *entity, const char *key, const lce_nbt_value *v) {
    uint16_t k[64], pre[80], full[80 + 24 + kDataValue]; size_t kn = 0;
    if (!v || !nbt_key(key, k, &kn)) return LCE_E_INVALID;
    if (v->type == LCE_NBT_INT && (v->i < INT32_MIN || v->i > INT32_MAX)) return LCE_E_INVALID;
    if (v->type == LCE_NBT_DOUBLE && !(v->d == v->d)) return LCE_E_INVALID;
    if (v->type == LCE_NBT_STRING) {
        size_t sl = 0;
        while (sl < sizeof v->s && v->s[sl]) { if ((unsigned char)v->s[sl] < 0x20 || (unsigned char)v->s[sl] > 0x7e) return LCE_E_INVALID; ++sl; }
        if (sl > kDataValue) return LCE_E_INVALID;
    } else if (v->type != LCE_NBT_INT && v->type != LCE_NBT_DOUBLE) return LCE_E_INVALID;
    DataTarget d{};
    lce_status st = data_target(entity, true, d);
    if (st != LCE_OK) return st;
    const uint32_t pn = data_prefix(k, kn, pre);
    memcpy(full, pre, 2 * (size_t)pn);
    const uint32_t fn = pn + encode_value(v, full + pn);
    bool exists = false, same = false;
    if (!tagset_walk(d.t, [&](const uint16_t *u, uint32_t l) {
            if (!has_prefix(u, l, pre, pn)) return true;
            exists = true; same = l == fn && !memcmp(u, full, 2 * (size_t)fn);
            return false;
        })) return LCE_E_BACKEND;
    if (same) return LCE_OK;
    if (exists) return tagset_rebuild(d.t, pre, pn, full, fn);
    if (d.t.size >= LCE195_MAX_ENTITY_TAGS) return LCE_E_FULL;   // Entity::load keeps only the first 1024
    tagset_add(d.t, full, fn);
    uint64_t size = 0;
    return rd(d.t.set + 0x10, &size, 8) && size == d.t.size + 1 ? LCE_OK : LCE_E_BACKEND;
}
extern "C" lce_status lce195_entity_data_remove(lce_mod_ctx *, void *entity, const char *key) {
    uint16_t k[64], pre[80]; size_t kn = 0;
    if (!nbt_key(key, k, &kn)) return LCE_E_INVALID;
    DataTarget d{};
    lce_status st = data_target(entity, true, d);
    if (st != LCE_OK) return st;
    const uint32_t pn = data_prefix(k, kn, pre);
    bool exists = false;
    if (!tagset_walk(d.t, [&](const uint16_t *u, uint32_t l) { exists = has_prefix(u, l, pre, pn); return !exists; })) return LCE_E_BACKEND;
    if (!exists) return LCE_E_NOTFOUND;
    return tagset_rebuild(d.t, pre, pn, nullptr, 0);
}

// ---- particles (API 1.3) ------------------------------------------------------
namespace {
// Read-only walk of ParticleType::byId's unordered_map node list (byId itself inserts on a miss).
uintptr_t particle_by_id(int32_t id) {
    uintptr_t head = 0, n = 0, type = 0;
    int32_t key = 0, got = -1;
    if (!rd(base() + LCE195_RVA_PARTICLE_MAP + 8, &head, 8) || !head || !rd(head, &n, 8)) return 0;
    for (uint32_t i = 0; n && n != head && i < 1024; ++i) {
        if (!rd(n + 0x10, &key, 4)) return 0;
        if (key == id) return rd(n + 0x18, &type, 8) && type && rd(type + LCE195_OFF_PARTICLE_ID, &got, 4) && got == id ? type : 0;
        if (!rd(n, &n, 8)) return 0;
    }
    return 0;
}
bool sane(double v) { return v == v && v > -1.0e9 && v < 1.0e9; }
bool valid_particle_position(double x, double y, double z) {
    return sane(x) && sane(y) && sane(z) && y >= -64 && y <= 320 &&
           xyz_ok((int32_t)floor(x), 64, (int32_t)floor(z));
}
} // namespace
extern "C" lce_status lce195_spawn_particles(lce_mod_ctx *, void *level, int32_t type_id, double x, double y, double z, uint32_t count,
                                             double dx, double dy, double dz, double speed) {
    const lce195_frame *f = frame_level(level);
    if (!f) return LCE_E_PHASE;
    if (!s.n.send_particles) return LCE_E_UNSUPPORTED;
    if (!valid_particle_position(x, y, z) ||
        count > 1000u || !(dx >= 0 && dx <= 64 && dy >= 0 && dy <= 64 && dz >= 0 && dz <= 64) || !(speed >= 0 && speed <= 100))
        return LCE_E_INVALID;
    uintptr_t type = particle_by_id(type_id);
    if (!type) return LCE_E_NOTFOUND;
    s.n.send_particles(f->view.level, type, 0, (int32_t)count, nullptr, 0, x, y, z, dx, dy, dz, speed);   // count 0 = one directed particle
    return LCE_OK;
}

extern "C" lce_status lce195_spawn_tinted_particles(lce_mod_ctx *, void *level, uint32_t rgb, double x, double y, double z,
                                                   uint32_t count, double spread) {
    const lce195_frame *f = frame_level(level);
    if (!f || !f->view.level || !lce195_ev_on_server_thread()) return LCE_E_PHASE;
    if (!s.n.send_tinted_particles) return LCE_E_UNSUPPORTED;
    if (!valid_particle_position(x, y, z) || rgb > 0xffffffu || count < 1 || count > 1000 ||
        !(spread >= 0 && spread <= 64)) return LCE_E_INVALID;
    uintptr_t type = 0;
    int32_t id = -1;
    if (!rd(base() + LCE195_RVA_TINTED_PARTICLE_TYPE, &type, sizeof type) ||
        !type || type > UINTPTR_MAX - LCE195_OFF_PARTICLE_ID ||
        !rd(type + LCE195_OFF_PARTICLE_ID, &id, sizeof id) || id < 0 || particle_by_id(id) != type)
        return LCE_E_BACKEND;
    const double red = ((rgb >> 16) & 0xffu) / 255.0;
    const double green = ((rgb >> 8) & 0xffu) / 255.0;
    const double blue = (rgb & 0xffu) / 255.0;
    // The client uses RGB as particle parameters when the color flag is set.
    s.n.send_tinted_particles(f->view.level, type, (int32_t)count, nullptr, 0,
                             x, y, z, spread, spread, spread, 0.0, red, green, blue);
    return LCE_OK;
}

// ---- chat --------------------------------------------------------------------
extern "C" lce_status lce195_send_chat(lce_mod_ctx *, void *player, const char *message) {
    const lce195_frame *f = player ? frame_player(player) : frame();
    if (!f) return LCE_E_PHASE;
    uint16_t units[257];
    int len = message ? lce195u::utf8_to_16(message, units, sizeof units / 2) : -1;
    if (len <= 0) return LCE_E_INVALID;
    uintptr_t target = 0, server = 0;
    if (player) {
        if (!rd((uintptr_t)player + LCE195_OFF_PLAYER_CONN, &target, 8) || !target) return LCE_E_INVALID;
    } else if (!rd(base() + LCE195_RVA_SERVER, &server, 8) || server != f->view.server ||
               !rd(server + LCE195_OFF_SERVER_PLAYERS, &target, 8) || !target) return LCE_E_INVALID;
    WString text; s.n.ws_assign(text.w, units, (uint64_t)len);
    // As PlayerList 0xcc34af..0xcc3532: new 0xe0, ctor(msg, type, -1), _Ref_count, enable_shared.
    void *pkt = s.n.alloc(LCE195_CHAT_SIZE);
    if (!pkt) return LCE_E_BACKEND;
    s.n.chat_ctor(pkt, text.w, LCE195_CHAT_TYPE_CUSTOM, -1);
    uintptr_t ctrl = (uintptr_t)s.n.alloc(0x18), vt = 0;
    if (!ctrl) return LCE_E_BACKEND;   // packet leaks (0xe0 bytes) rather than guess its destructor
    *(uintptr_t *)ctrl = base() + LCE195_RVA_PACKET_REFCOUNT_VT + 0x10;
    *(uint32_t *)(ctrl + 8) = 1; *(uint32_t *)(ctrl + 12) = 1; *(uintptr_t *)(ctrl + 16) = (uintptr_t)pkt;
    Shared sp; s.n.enable_shared(&sp.p, (uintptr_t)pkt, ctrl);
    if (sp.p.object != (uintptr_t)pkt || sp.p.control != ctrl || !rd((uintptr_t)pkt, &vt, 8) || vt != base() + LCE195_RVA_CHAT_VT)
        return LCE_E_BACKEND;
    if (player) s.n.conn_send(target, &sp.p); else s.n.broadcast_all(target, &sp.p);
    return LCE_OK;
}

// ---- mod networking (API 1.4): server -> client custom payload --------------------------------------
extern "C" int lce195_net_channel_ok(const char *c) {
    size_t n = c ? strlen(c) : 0;
    if (n < 3 || n > 20 || !strchr(c, ':')) return 0;   // "modid:name", Java's 20-char channel limit
    for (size_t i = 0; i < n; ++i) {
        char x = c[i];
        if (!((x >= 'a' && x <= 'z') || (x >= '0' && x <= '9') || x == '_' || x == ':' || x == '.' || x == '-')) return 0;
    }
    return strncmp(c, "mc|", 3) != 0;   // vanilla channels are not for mods
}
extern "C" lce_status lce195_send_payload(lce_mod_ctx *, void *player, const char *channel, const void *data, uint32_t len) {
    const lce195_frame *f = player ? frame_player(player) : frame();
    if (!f) return LCE_E_PHASE;
    if (!lce195_net_channel_ok(channel) || len > 32000u || (len && !data)) return LCE_E_INVALID;
    if (!s.n.payload_ctor || !s.n.alloc_array) return LCE_E_UNSUPPORTED;
    uintptr_t target = 0, server = 0;
    if (player) {
        if (!rd((uintptr_t)player + LCE195_OFF_PLAYER_CONN, &target, 8) || !target) return LCE_E_INVALID;
    } else if (!rd(base() + LCE195_RVA_SERVER, &server, 8) || server != f->view.server ||
               !rd(server + LCE195_OFF_SERVER_PLAYERS, &target, 8) || !target) return LCE_E_INVALID;
    uint16_t units[24]; size_t cn = strlen(channel);
    for (size_t i = 0; i < cn; ++i) units[i] = (uint8_t)channel[i];
    WString ch; s.n.ws_assign(ch.w, units, cn);
    uint8_t *buf = nullptr;
    if (len) {   // the packet owns the buffer and frees it with delete[] (dtor 0x18bab0)
        buf = (uint8_t *)s.n.alloc_array(len);
        if (!buf) return LCE_E_BACKEND;
        memcpy(buf, data, len);
    }
    void *pkt = s.n.alloc(LCE195_PAYLOAD_SIZE);
    if (!pkt) return LCE_E_BACKEND;   // buf leaks (no delete[] native) rather than guess
    memset(pkt, 0, LCE195_PAYLOAD_SIZE);
    s.n.payload_ctor(pkt, ch.w, buf, (int32_t)len);
    uintptr_t ctrl = (uintptr_t)s.n.alloc(0x18), vt = 0;
    if (!ctrl) return LCE_E_BACKEND;
    *(uintptr_t *)ctrl = base() + LCE195_RVA_PACKET_REFCOUNT_VT + 0x10;   // destroy = virtual delete, as the chat path
    *(uint32_t *)(ctrl + 8) = 1; *(uint32_t *)(ctrl + 12) = 1; *(uintptr_t *)(ctrl + 16) = (uintptr_t)pkt;
    Shared sp; s.n.enable_shared(&sp.p, (uintptr_t)pkt, ctrl);
    if (sp.p.object != (uintptr_t)pkt || sp.p.control != ctrl || !rd((uintptr_t)pkt, &vt, 8) || vt != base() + LCE195_RVA_PAYLOAD_VT)
        return LCE_E_BACKEND;
    if (player) s.n.conn_send(target, &sp.p); else s.n.broadcast_all(target, &sp.p);
    return LCE_OK;
}

// ---- client -> server custom payload (API 1.6) ----------------------------------------------------------
namespace {
lce195u::MsgQueue<32, 65536> g_tx, g_tx_snap;   // send_to_server outside a client handler (remote client)
lce_status client_send_now(uintptr_t listener, const char *channel, const void *data, uint32_t len) {
    uint16_t units[24]; size_t cn = strlen(channel);
    for (size_t i = 0; i < cn; ++i) units[i] = (uint8_t)channel[i];
    WString ch; s.n.ws_assign(ch.w, units, cn);
    uint8_t *buf = nullptr;
    if (len) {   // owned by the packet: its dtor 0x5fbc50 frees +0x50 with delete[]
        buf = (uint8_t *)s.n.alloc_array(len);
        if (!buf) return LCE_E_BACKEND;
        memcpy(buf, data, len);
    }
    void *pkt = s.n.alloc(LCE195_SB_PAYLOAD_SIZE);
    if (!pkt) return LCE_E_BACKEND;
    memset(pkt, 0, LCE195_SB_PAYLOAD_SIZE);
    s.n.sb_payload_ctor(pkt, ch.w, buf, (int32_t)len);
    uintptr_t ctrl = (uintptr_t)s.n.alloc(0x18), vt = 0;
    if (!ctrl) return LCE_E_BACKEND;
    *(uintptr_t *)ctrl = base() + LCE195_RVA_PACKET_REFCOUNT_VT + 0x10;   // destroy = virtual delete (as the server paths)
    *(uint32_t *)(ctrl + 8) = 1; *(uint32_t *)(ctrl + 12) = 1; *(uintptr_t *)(ctrl + 16) = (uintptr_t)pkt;
    Shared sp; s.n.enable_shared(&sp.p, (uintptr_t)pkt, ctrl);
    if (sp.p.object != (uintptr_t)pkt || sp.p.control != ctrl || !rd((uintptr_t)pkt, &vt, 8) || vt != base() + LCE195_RVA_SB_PAYLOAD_VT)
        return LCE_E_BACKEND;
    s.n.client_send(listener, &sp.p);
    return LCE_OK;
}
} // namespace
extern "C" lce_status lce195_send_to_server(lce_mod_ctx *, const char *channel, const void *data, uint32_t len) {
    if (!s.ready) return LCE_E_PHASE;
    if (!lce195_net_channel_ok(channel) || !lce195_net_channel_registered(channel) || len > 32000u || (len && !data)) return LCE_E_INVALID;
    if (!s.n.sb_payload_ctor || !s.n.client_send || !s.n.alloc_array) return LCE_E_UNSUPPORTED;
    if (const uintptr_t l = lce195_ev_client_listener()) return client_send_now(l, channel, data, len);
    if (lce195_ev_host_server_active()) return lce195_ev_loopback_payload(channel, data, len) ? LCE_OK : LCE_E_FULL;
    return g_tx.push(0, channel, data, len) ? LCE_OK : LCE_E_FULL;
}
extern "C" void lce195_net_client_flush(uintptr_t listener) {
    if (!s.ready || !listener || listener != lce195_ev_client_listener() || !s.n.sb_payload_ctor || !s.n.client_send) return;
    if (!__atomic_load_n(&g_tx.count, __ATOMIC_ACQUIRE) || !g_tx.take(g_tx_snap)) return;
    for (uint32_t i = 0; i < g_tx_snap.count; ++i) {
        const auto &m = g_tx_snap.m[i];
        (void)client_send_now(listener, m.ch, g_tx_snap.buf + m.off, m.len);
    }
}

// ---- effects -------------------------------------------------------------------
// Players use ServerPlayer::addEffect 0xd05800; any other living entity of the frame's level uses LivingEntity::addEffect
// 0x423400 (the same slot +0x968 in LivingEntity/Mob/Zombie/Cow vtables, same (entity, inst, source) ABI).
extern "C" lce_status lce195_add_effect(lce_mod_ctx *, void *entity, const lce_effect_ref *e) {
    const lce195_frame *f = frame();
    if (!f) return LCE_E_PHASE;
    if (!e || !e->effect || !e->duration_ticks || e->duration_ticks > 0x7fffffffu || e->amplifier > 127) return LCE_E_INVALID;
    uintptr_t player = entity ? (uintptr_t)entity : (f->has_player ? f->view.player_object : 0), vt = 0, add = 0, get = 0, effect = 0, evt = 0, instant = 0;
    if (!player || !living_target(f, player)) return entity ? LCE_E_INVALID : LCE_E_PHASE;
    const bool is_player = rd(player, &vt, 8) && vt == base() + LCE195_RVA_SERVERPLAYER_VT;
    if (is_player && !(f->has_player && player == f->view.player_object)) return LCE_E_PHASE;   // another player: not this frame's
    if (!vt || !rd(vt + LCE195_OFF_ADD_EFFECT, &add, 8) || add != base() + (is_player ? LCE195_RVA_ADD_EFFECT : LCE195_RVA_LIVING_ADD_EFFECT) ||
        !rd(vt + LCE195_OFF_GET_EFFECT, &get, 8) || get != base() + LCE195_RVA_GET_EFFECT) return LCE_E_BACKEND;
    if (!effect_lookup(e->effect, &effect)) return LCE_E_NOTFOUND;
    // Instant effects need applyInstantenousEffect; never make them timed (r72 rule).
    if (!rd(effect, &evt, 8) || !rd(evt + 0x18, &instant, 8) || instant < base() || instant >= base() + LCE195_RVA_TEXT_END ||
        ((unsigned char (*)(uintptr_t))instant)(effect)) return LCE_E_UNSUPPORTED;
    uintptr_t inst = (uintptr_t)s.n.alloc(24);
    if (!inst) return LCE_E_BACKEND;
    s.n.effect_inst_ctor(inst, effect, (int)e->duration_ticks, (int)e->amplifier);
    uintptr_t source[2] = {0, 0};
    ((void (*)(uintptr_t, uintptr_t, const uintptr_t *))add)(player, inst, source);
    uintptr_t seen = ((uintptr_t (*)(uintptr_t, uintptr_t))get)(player, effect);
    if (seen != inst) s.n.free((void *)inst);   // merged/refused: ownership stayed with us
    return seen ? LCE_OK : LCE_E_BACKEND;
}

// ---- sounds ------------------------------------------------------------------------
extern "C" int32_t lce195_sound_id(const char *name) {
    if (!name) return -1;
    if (!strncmp(name, "minecraft:", 10)) name += 10;
    size_t lo = 0, hi = sizeof kSounds195 / sizeof kSounds195[0];
    while (lo < hi) {
        size_t mid = (lo + hi) / 2; int c = strcmp(kSounds195[mid].name, name);
        if (!c) return kSounds195[mid].id;
        if (c < 0) lo = mid + 1; else hi = mid;
    }
    return -1;
}
extern "C" int lce195_sound_register(const char *modid, const lce_sound_desc *d, char *err, uint32_t cap) {
    auto fail = [&](const char *m) { lce195u::copy(err, cap, m); return 0; };
    if (!modid || !d || !d->key || strncmp(d->key, modid, strlen(modid)) || d->key[strlen(modid)] != ':') return fail("sound key must be <modid>:<name>");
    if (d->pcm || d->pcm_bytes) return fail("custom PCM audio is not possible on 1.95 (sound bank format not implemented); use clone_of");
    int32_t id = lce195_sound_id(d->clone_of);
    if (id < 0) return fail("clone_of is not a 1.95 sound event");
    if (s.sound_count >= kSounds) return fail("too many sounds");
    for (uint32_t i = 0; i < s.sound_count; ++i) if (!strcmp(s.sounds[i].key, d->key)) return fail("duplicate sound key");
    float vol = d->volume_milli ? d->volume_milli / 1000.0f : 1.0f, pitch = d->pitch_milli ? d->pitch_milli / 1000.0f : 1.0f;
    if (vol <= 0 || vol > 4 || pitch < 0.5f || pitch > 2.0f) return fail("volume 0..4, pitch 0.5..2");
    Clone &c = s.sounds[s.sound_count++];
    lce195u::copy(c.key, sizeof c.key, d->key); c.id = (uint16_t)id; c.volume = vol; c.pitch = pitch;
    return 1;
}
extern "C" int lce195_sound_set_custom(const char *key, int32_t id) {
    // audio195 mod SoundEvent (real registry id >= 523): play_sound looks the key up like a clone; the engine
    // hook applies the descriptor volume/pitch, so the service passes 1.0/1.0.
    if (!key || id < 523 || id > 65535 || s.sound_count >= kSounds) return 0;
    for (uint32_t i = 0; i < s.sound_count; ++i) if (!strcmp(s.sounds[i].key, key)) return 0;
    Clone &c = s.sounds[s.sound_count++];
    lce195u::copy(c.key, sizeof c.key, key); c.id = (uint16_t)id; c.volume = 1.0f; c.pitch = 1.0f;
    return 1;
}
extern "C" lce_status lce195_play_sound(lce_mod_ctx *, void *level, int32_t x, int32_t y, int32_t z, const char *sound) {
    const lce195_frame *f = frame_level(level);
    if (!f) return LCE_E_PHASE;
    if (!sound || !xyz_ok(x, y, z)) return LCE_E_INVALID;
    float vol = 1.0f, pitch = 1.0f; int32_t id = -1;
    for (uint32_t i = 0; i < s.sound_count && id < 0; ++i) if (!strcmp(s.sounds[i].key, sound)) { id = s.sounds[i].id; vol = s.sounds[i].volume; pitch = s.sounds[i].pitch; }
    if (id < 0) id = lce195_sound_id(sound);
    if (id < 0) return LCE_E_NOTFOUND;
    uintptr_t begin = 0, end = 0, event = 0, lvt = 0, play = 0;
    if (!rd(base() + LCE195_RVA_SOUND_REGISTRY + 0xe8, &begin, 8) || !rd(base() + LCE195_RVA_SOUND_REGISTRY + 0xf0, &end, 8) ||
        !begin || end < begin || (uint64_t)id >= (end - begin) / 8 || !rd(begin + 8 * (uint64_t)id, &event, 8) || !event) return LCE_E_BACKEND;
    if (!rd(f->view.level, &lvt, 8) || lvt != base() + LCE195_RVA_SERVERLEVEL_VT || !rd(lvt + LCE195_OFF_LEVEL_PLAYSOUND, &play, 8) ||
        play != base() + LCE195_RVA_LEVEL_PLAYSOUND) return LCE_E_BACKEND;
    const lce195_pair nobody{0, 0};
    // ABI of 0x54ecff: (level, except*, SoundEvent*, int source, x, y, z, volume, pitch, range 16.0)
    ((void (*)(uintptr_t, const lce195_pair *, uintptr_t, int, double, double, double, float, float, float))play)(
        f->view.level, &nobody, event, 0, x + 0.5, y + 0.5, z + 0.5, vol, pitch, 16.0f);
    return LCE_OK;
}

// ---- schedule / translate ---------------------------------------------------------
extern "C" lce_status lce195_schedule(lce_mod_ctx *, uint32_t ticks, lce_event_fn fn, void *user) {
    if (!fn || !ticks) return LCE_E_INVALID;
    return lce195_ev_schedule(ticks, fn, user) ? LCE_OK : LCE_E_FULL;
}

extern "C" int lce195_l10n_load(const lce195_l10n_entry *e, uint32_t n) {
    if (n > kStrings || (n && !e)) return 0;
    s.strings = e; s.string_count = n;
    return 1;
}
extern "C" const char *lce195_l10n_from_xc(unsigned lang, unsigned locale) {
    switch (lang) {   // XC_LANGUAGE_* as written by 0xafc2e0 from the selector table 0xeeb750/0xeeb7b0
    case 1: return locale == 35 ? "en_GB" : "en_US";
    case 2: return "ja_JP";
    case 3: return "de_DE";
    case 4: return "fr_FR";
    case 5: return locale == 240 ? "es_MX" : "es_ES";
    case 6: return "it_IT";
    case 7: return "ko_KR";
    case 8: return "zh_TW";
    case 9: return locale == 4 ? "pt_BR" : "pt_PT";
    case 11: return "pl_PL";
    case 12: return "ru_RU";
    case 13: return "sv_SE";
    case 14: return "tr_TR";
    case 15: return "nb_NO";
    case 16: return "nl_NL";
    case 17: return "zh_CN";
    case 241: return "fi_FI";
    case 243: return "da_DK";
    default: return nullptr;   // 0 = system default; 242 (sk/cs/el, unresolved) -> system
    }
}
extern "C" const char *lce195_l10n_from_sce(int v) {
    static const char *const t[] = {"ja_JP", "en_US", "fr_FR", "es_ES", "de_DE", "it_IT", "nl_NL", "pt_PT", "ru_RU", "ko_KR",
                                    "zh_TW", "zh_CN", "fi_FI", "sv_SE", "da_DK", "nb_NO", "pl_PL", "pt_BR", "en_GB", "tr_TR",
                                    "es_MX", "ar_SA", "fr_CA", "cs_CZ", "hu_HU", "el_GR", "ro_RO", "th_TH", "vi_VN", "id_ID"};
    return v >= 0 && v < (int)(sizeof t / sizeof t[0]) ? t[v] : nullptr;
}
extern "C" const char *lce195_l10n_locale(void) {
    if (!s.ready) return "en_US";
    uintptr_t settings = 0; uint8_t lang = 0, loc = 0;
    // Pad 0 settings (primary profile; per-pad choice unverified).
    if (rd(base() + LCE195_RVA_GAME_SETTINGS, &settings, 8) && settings && rd(settings + LCE195_OFF_SETTINGS_LANG, &lang, 1) &&
        rd(settings + LCE195_OFF_SETTINGS_LOCALE, &loc, 1)) {
        const char *c = lce195_l10n_from_xc(lang, loc);
        if (c) return c;
    }
    const char *c = s.n.system_language ? lce195_l10n_from_sce(s.n.system_language()) : nullptr;
    return c ? c : "en_US";
}
extern "C" const char *lce195_translate(lce_mod_ctx *, const char *key) {
    if (!key) return nullptr;
    const char *loc = lce195_l10n_locale(), *en = nullptr, *lang_only = nullptr;
    for (uint32_t i = 0; i < s.string_count; ++i) {
        const lce195_l10n_entry &e = s.strings[i];
        if (!e.key || !e.locale || !e.value || strcmp(e.key, key)) continue;
        if (!strcmp(e.locale, loc)) return e.value;
        if (!lang_only && !strncmp(e.locale, loc, 2)) lang_only = e.value;   // fr_CA -> fr_FR
        if (!en && !strcmp(e.locale, "en_US")) en = e.value;
    }
    return lang_only ? lang_only : en ? en : key;
}

extern "C" void lce195_natives_bind_rvas(lce195_natives *n, uintptr_t b) {
    if (!n || !b) return;
    *n = lce195_natives{};
    n->base = b;
    n->alloc = (void *(*)(size_t))(b + LCE195_RVA_NEW);
    n->free = (void (*)(void *))(b + LCE195_RVA_DELETE);
    n->atomic_sub = (int (*)(uintptr_t, int, int))(b + LCE195_RVA_ATOMIC_SUB);
    n->ws_assign = (void (*)(void *, const uint16_t *, uint64_t))(b + LCE195_RVA_WSTRING_ASSIGN);
    n->chat_ctor = (void (*)(void *, const void *, int, int))(b + LCE195_RVA_CHAT_CTOR);
    n->enable_shared = (void (*)(lce195_pair *, uintptr_t, uintptr_t))(b + LCE195_RVA_ENABLE_SHARED);
    n->broadcast_all = (void (*)(uintptr_t, lce195_pair *))(b + LCE195_RVA_BROADCAST_ALL);
    n->conn_send = (void (*)(uintptr_t, lce195_pair *))(b + LCE195_RVA_CONN_SEND);
    n->key_ctor = (void (*)(void *, const void *))(b + LCE195_RVA_KEY_CTOR);
    n->key_dtor = (void (*)(void *))(b + LCE195_RVA_KEY_DTOR);
    n->effect_inst_ctor = (void (*)(uintptr_t, uintptr_t, int, int))(b + LCE195_RVA_EFFECT_INST_CTOR);
    n->item_by_id = (uintptr_t (*)(unsigned))(b + LCE195_RVA_ITEM_BY_ID);
    n->stack_ctor = (void (*)(uintptr_t, uintptr_t, int32_t, int32_t))(b + LCE195_RVA_STACK_CTOR);
    n->stack_dtor = (void (*)(uintptr_t))(b + LCE195_RVA_STACK_DTOR);
    n->stack_bind = (void (*)(lce195_pair *, uintptr_t, uintptr_t))(b + LCE195_RVA_STACK_BIND);
    n->pop_resource = (void (*)(uintptr_t, const int32_t *, const lce195_pair *))(b + LCE195_RVA_POP_RESOURCE);
    n->system_language = nullptr;   // bound by r72_bind195.cpp (sceSystemServiceParamGetInt)
    n->health_get = (float (*)(uintptr_t))(b + LCE195_RVA_HEALTH_GET);
    n->health_set = (void (*)(uintptr_t, float))(b + LCE195_RVA_HEALTH_SET);
    n->health_max = (float (*)(uintptr_t))(b + LCE195_RVA_HEALTH_MAX);
    n->is_alive = (uint8_t (*)(uintptr_t))(b + LCE195_RVA_IS_ALIVE);
    n->fire_clear = (void (*)(uintptr_t))(b + LCE195_RVA_FIRE_CLEAR);
    n->stack_set_count = (void (*)(uintptr_t, int32_t))(b + LCE195_RVA_STACK_SET_COUNT);
    n->inventory_set_changed = (void (*)(uintptr_t))(b + LCE195_RVA_INV_SET_CHANGED);
    n->inventory_add = (uint8_t (*)(uintptr_t, const lce195_pair *))(b + LCE195_RVA_INVENTORY_ADD);
    n->stack_max_damage = (int32_t (*)(uintptr_t))(b + LCE195_RVA_STACK_MAX_DAMAGE);
    n->stack_hurt = (uint8_t (*)(uintptr_t, int32_t, uintptr_t))(b + LCE195_RVA_STACK_HURT);
    n->stack_set_damage = (void (*)(uintptr_t, int32_t))(b + LCE195_RVA_STACK_SET_DAMAGE);
    n->player_attack_source = (uintptr_t (*)(const lce195_pair *))(b + LCE195_RVA_DMG_PLAYER_ATTACK);
    n->mob_attack_source = (uintptr_t (*)(const lce195_pair *))(b + LCE195_RVA_DMG_MOB_ATTACK);
    n->weak_lock = (void (*)(lce195_pair *, uintptr_t, uintptr_t, int))(b + LCE195_RVA_WEAK_LOCK);
    n->tag_get_or_create = (uintptr_t (*)(uintptr_t))(b + LCE195_RVA_TAG_GET_OR_CREATE);
    n->tag_get = (uintptr_t (*)(uintptr_t, const void *))(b + LCE195_RVA_TAG_GET);
    n->tag_remove = (void (*)(uintptr_t, const void *))(b + LCE195_RVA_TAG_REMOVE);
    n->tag_put_int = (void (*)(uintptr_t, const void *, int32_t))(b + LCE195_RVA_TAG_PUT_INT);
    n->tag_put_double = (void (*)(uintptr_t, const void *, double))(b + LCE195_RVA_TAG_PUT_DOUBLE);
    n->tag_put_string = (void (*)(uintptr_t, const void *, const void *))(b + LCE195_RVA_TAG_PUT_STRING);
    n->payload_ctor = (void (*)(void *, const void *, uint8_t *, int32_t))(b + LCE195_RVA_PAYLOAD_CTOR);
    n->alloc_array = (void *(*)(size_t))(b + LCE195_RVA_NEW_ARRAY);
    n->send_particles = (void (*)(uintptr_t, uintptr_t, uint8_t, int32_t, const int32_t *, int32_t, double, double, double, double, double,
                                  double, double))(b + LCE195_RVA_SEND_PARTICLES);
    n->compound_ctor = (void (*)(uintptr_t))(b + LCE195_RVA_COMPOUND_CTOR);
    n->entity_save = (uintptr_t (*)(uintptr_t, uintptr_t))(b + LCE195_RVA_ENTITY_SAVE);
    n->tagset_insert = (void (*)(void *, uintptr_t, int, const void *))(b + LCE195_RVA_TAGSET_INSERT);
    n->tagset_erase = (void (*)(uintptr_t, uintptr_t))(b + LCE195_RVA_TAGSET_ERASE);
    n->sb_payload_ctor = (void (*)(void *, const void *, uint8_t *, int32_t))(b + LCE195_RVA_SB_PAYLOAD_CTOR);
    n->client_send = (void (*)(uintptr_t, lce195_pair *))(b + LCE195_RVA_CLIENT_SEND);
    n->send_tinted_particles = reinterpret_cast<decltype(n->send_tinted_particles)>(b + LCE195_RVA_SEND_TINTED_PARTICLES);
}

#ifdef LCE195_HOST_TEST
extern "C" void lce195_services_reset_for_tests(void) { g_tx.count = g_tx.used = 0; s.ready = 0; s.string_count = 0; s.strings = nullptr; s.sound_count = 0; }
#endif
