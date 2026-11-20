// events195 world operations bound to the console-proven r72 Lucky Block world helpers
// (r72-session-candidate/loader/lucky_block_world_events_195: world.cpp, chunks.cpp,
// registry.cpp, entity.cpp, explosion.cpp; linked into super195.elf by build.sh line 200).
//
// Each helper takes an mc195_world_context whose scope_held() must prove the
// retained native server scope. Here that is the events195 frame of the
// current mod callback: same thread, same Level, view still current
// (mc195_lucky_server_current). No pointer outlives the call.
//
// get_block uses the same native reader pair the r72 placement readback uses
// (world.cpp: ServerLevel vtable +0x38 getBlockState 0x3f2820, state encoder
// 0x91bc0 = id | meta << 12). give_item stays unbound: the r72 give path
// (debug_give_195) requires a physical pad authority ticket.
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "../modules/entities195/entities195.h"
#include "../modules/events195/ev195.h"
#include "../modules/events195/services195.h"
#include "../loader/retail195/host_ps4.h"
#include "../third_party/r72/lucky_block_world_events_195/server_scope.h"
#include "../third_party/r72/lucky_block_world_events_195/world.h"
#include "../third_party/r72/lucky_block_world_events_195/chunks.h"
#include "../third_party/r72/lucky_block_world_events_195/explosion.h"

extern "C" {
// Mirror of r72 entity.h (sha256 41bbd33e...93d2); only the event-free spawn is used (data NULL, nbt_root 0).
typedef struct mc195_event_action mc195_event_action;
typedef struct mc195_world_spawn {
    const char *id;
    double x, y, z;
    float yaw, pitch;
    const mc195_event_action *data;
    uint32_t nbt_root;
} mc195_world_spawn;
typedef int (*mc195_world_after_spawn)(void *, const mc195_world_context *, uintptr_t object, uintptr_t control);
int mc195_world_spawn_entity_ex(const mc195_world_context *, const mc195_world_spawn *, void *, mc195_world_after_spawn);
int mc195_world_spawn_entity_exact(const mc195_world_context *, const mc195_world_spawn *, uintptr_t expected_vtable)
    __attribute__((weak));
}

namespace {
uintptr_t g_base;
const lce_plan *g_plan;

int held(void *, uintptr_t level) {
    const lce195_frame *f = lce195_ev_frame();
    return f && f->active && level && level == f->view.level &&
           mc195_lucky_server_current((const mc195_lucky_server_view *)(const void *)&f->view) == 1 ? 1 : 0;
}
uint64_t tid(void *) { return lce_ps4_thread_id(); }
int rd(void *, uintptr_t a, void *v, size_t n) { return lce_ps4_mem_read(nullptr, a, v, n); }
struct Ctx { mc195_world_context w; };
int loaded_cb(void *p, uintptr_t level, int32_t x, int32_t z) {
    const mc195_world_context *w = &((Ctx *)p)->w;
    uintptr_t chunk = 0;
    return level == w->level && held(nullptr, level) == 1 && mc195_world_loaded_chunk(w, x, z, &chunk) == 1 ? 1 : 0;
}
uintptr_t state_cb(void *p, uint16_t id, uint8_t meta) { return mc195_world_registered_state(&((Ctx *)p)->w, id, meta); }

void make(Ctx &c, const lce195_view *v) {
    memset(&c, 0, sizeof c);
    c.w.abi = 1; c.w.size = sizeof c.w; c.w.base = g_base; c.w.level = v->level; c.w.opaque = &c;
    c.w.scope_held = held; c.w.thread_id = tid; c.w.read = rd; c.w.loaded = loaded_cb; c.w.resolve_state = state_cb;
}
bool usable(const lce195_view *v) { return g_base && v && v->image == g_base && v->level && held(nullptr, v->level) == 1; }

int op_set_block(void *, const lce195_view *v, int32_t x, int32_t y, int32_t z, uint16_t id, uint8_t meta) {
    if (!usable(v)) return 0;
    Ctx c; make(c, v);
    mc195_world_place p{};
    p.pos = mc195_world_pos{x, y, z}; p.block_id = id; p.metadata = meta;
    return mc195_world_place_block_update(&c.w, &p, 1);   // notify=1: native flags 3, like Level::setBlock
}
int op_loaded(void *, const lce195_view *v, int32_t cx, int32_t cz) {
    if (!usable(v)) return 0;
    Ctx c; make(c, v);
    uintptr_t chunk = 0;
    return mc195_world_loaded_chunk(&c.w, cx, cz, &chunk) == 1 ? 1 : 0;
}
int track(void *, const mc195_world_context *, uintptr_t object, uintptr_t) {
    // Mod-spawned entities get the events195 damage/death routes (best effort: a refusal leaves the entity unrouted).
    (void)lce195_ev_track_entity(object);
    return 1;
}
int op_spawn(void *, const lce195_view *v, const char *key, double x, double y, double z, int want_track) {
    if (!usable(v) || !key) return 0;
    Ctx c; make(c, v);
    mc195_world_spawn s{};
    s.id = key; s.x = x; s.y = y; s.z = z;
    if (g_plan) {
        for (uint32_t i = 0; i < g_plan->ext_count; ++i) {
            const lce_plan_ext &x = g_plan->ext[i];
            if (x.type != LCE_EXT_ENTITY || strcmp(x.key, key)) continue;
            uintptr_t vt = le195_spawn_vtable((const lce_entity_desc2 *)x.desc);
            if (!vt || !mc195_world_spawn_entity_exact) return 0;
            // The events195 tracker copies the primary vtable and only handles stock slot addresses.
            return mc195_world_spawn_entity_exact(&c.w, &s, vt);
        }
    }
    return mc195_world_spawn_entity_ex(&c.w, &s, nullptr, want_track ? track : nullptr);
}
int op_get(void *, const lce195_view *v, int32_t x, int32_t y, int32_t z, uint16_t *id, uint8_t *meta) {
    if (!usable(v) || !id || !meta || y < 0 || y > 255) return 0;
    Ctx c; make(c, v);
    uintptr_t vt = 0, getter = 0, chunk = 0, svt = 0;
    int32_t cx = x >= 0 ? x / 16 : -1 - (int32_t)((-(int64_t)x - 1) / 16), cz = z >= 0 ? z / 16 : -1 - (int32_t)((-(int64_t)z - 1) / 16);
    if (lce_ps4_mem_read(nullptr, v->level, &vt, 8) != 1 || vt != g_base + 0x1340d50u ||
        lce_ps4_mem_read(nullptr, vt + 0x38, &getter, 8) != 1 || getter != g_base + 0x3f2820u ||
        mc195_world_loaded_chunk(&c.w, cx, cz, &chunk) != 1) return 0;
    const mc195_world_pos pos{x, y, z};
    uintptr_t state = ((uintptr_t (*)(uintptr_t, const mc195_world_pos *))getter)(v->level, &pos);
    if (!state || lce_ps4_mem_read(nullptr, state, &svt, 8) != 1 || !svt || held(nullptr, v->level) != 1) return 0;
    unsigned enc = ((unsigned (*)(uintptr_t))(g_base + 0x91bc0u))(state);
    *id = (uint16_t)(enc & 0xfffu); *meta = (uint8_t)((enc >> 12) & 0xfu);
    return 1;
}
int op_explode(void *, const lce195_view *v, double x, double y, double z, float radius, int fire, int destroy) {
    if (!usable(v)) return 0;
    Ctx c; make(c, v);
    mc195_world_explosion e{};
    e.x = x; e.y = y; e.z = z; e.radius = radius; e.fire = fire ? 1 : 0; e.destroy_blocks = destroy ? 1 : 0;
    return mc195_world_explode(&c.w, &e);
}
}  // namespace

extern "C" const void *lce_sdk_world_ops(uintptr_t base, const lce_plan *plan) {
    static lce195_world_ops ops;
    g_base = base;
    g_plan = plan;
    memset(&ops, 0, sizeof ops);
    ops.set_block = op_set_block;
    ops.get_block = op_get;
    ops.spawn = op_spawn;
    ops.loaded = op_loaded;
    ops.explode = op_explode;
    return &ops;
}
