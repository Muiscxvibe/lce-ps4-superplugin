#include <string.h>
#include "../modules/entities195/entities195.h"
#include "../modules/events195/services195.h"
#include "../loader/retail195/host_ps4.h"
#include "../third_party/r72/lucky_block_world_events_195/chunks.h"
#include "../third_party/r72/lucky_block_world_events_195/explosion.h"
#include "../third_party/r72/lucky_block_world_events_195/server_scope.h"
#include "check.h"

struct mc195_event_action;
struct mc195_world_spawn {
    const char *id;
    double x, y, z;
    float yaw, pitch;
    const mc195_event_action *data;
    uint32_t nbt_root;
};
typedef int (*mc195_world_after_spawn)(void *, const mc195_world_context *, uintptr_t, uintptr_t);
extern "C" const void *lce_sdk_world_ops(uintptr_t, const lce_plan *);

namespace {
constexpr uintptr_t kBase = 0x100000, kLevel = 0x200000, kVtable = 0x300000;
lce195_frame frame;
lce_entity_desc2 owned;
const lce_entity_desc2 *probed;
bool published;
unsigned stock_calls, custom_calls, tracking_calls;
const char *requested;
}

extern "C" const lce195_frame *lce195_ev_frame() { return &frame; }
extern "C" uint64_t lce_ps4_thread_id() { return 2; }
extern "C" int lce_ps4_mem_read(void *, uintptr_t, void *, size_t) { return 0; }
extern "C" int mc195_lucky_server_current(const mc195_lucky_server_view *) { return 1; }
extern "C" int mc195_world_loaded_chunk(const mc195_world_context *, int32_t, int32_t, uintptr_t *) { return 1; }
extern "C" uintptr_t mc195_world_registered_state(const mc195_world_context *, uint16_t, uint8_t) { return 0; }
extern "C" int mc195_world_place_block_update(const mc195_world_context *, const mc195_world_place *, unsigned) { return 0; }
extern "C" int mc195_world_explode(const mc195_world_context *, const mc195_world_explosion *) { return 0; }
extern "C" int lce195_ev_track_entity(uintptr_t entity) {
    CHECK_EQ(entity, 42u);
    ++tracking_calls;
    return 1;
}
extern "C" uintptr_t le195_spawn_vtable(const lce_entity_desc2 *d) {
    probed = d;
    return published && d == &owned ? kVtable : 0;
}
extern "C" int mc195_world_spawn_entity_ex(const mc195_world_context *c, const mc195_world_spawn *s,
                                           void *opaque, mc195_world_after_spawn after) {
    ++stock_calls;
    CHECK(strcmp(s->id, requested) == 0);
    CHECK_EQ(c->scope_held(c->opaque, c->level), 1);
    return after ? after(opaque, c, 42, 99) : 1;
}
#ifndef LCE_TEST_NO_EXACT_SPAWN
extern "C" int mc195_world_spawn_entity_exact(const mc195_world_context *c, const mc195_world_spawn *s, uintptr_t vt) {
    ++custom_calls;
    CHECK_EQ(c->scope_held(c->opaque, c->level), 1);
    CHECK(strcmp(s->id, owned.key) == 0 && s->x == 2.5 && s->y == 64 && s->z == -1.5);
    CHECK_EQ(vt, kVtable);
    return 1;
}
#endif

int main() {
    static const char key[] = "test:clone";
    owned.key = key;
    lce_entity_desc2 copy = owned;
    lce_plan_ext ext[] = {{LCE_EXT_FEATURE, &copy, key, -1}, {LCE_EXT_ENTITY, &owned, key, 150}};
    lce_plan p{};
    p.ext = ext;
    p.ext_count = 2;
    const auto *ops = static_cast<const lce195_world_ops *>(lce_sdk_world_ops(kBase, &p));
    frame.view.image = kBase;
    frame.view.level = kLevel;
    CHECK_EQ(ops->spawn(ops->opaque, &frame.view, key, 2.5, 64, -1.5, 1), 0);
    CHECK(probed == nullptr && !custom_calls && !stock_calls);
    frame.active = 1;
    CHECK_EQ(ops->spawn(ops->opaque, &frame.view, key, 2.5, 64, -1.5, 1), 0);
    CHECK(probed == &owned && !custom_calls && !stock_calls);
    published = true;
#ifdef LCE_TEST_NO_EXACT_SPAWN
    CHECK_EQ(ops->spawn(ops->opaque, &frame.view, key, 2.5, 64, -1.5, 1), 0);
    CHECK(!custom_calls && !stock_calls && !tracking_calls);
#else
    CHECK_EQ(ops->spawn(ops->opaque, &frame.view, key, 2.5, 64, -1.5, 1), 1);
    CHECK(custom_calls == 1 && !stock_calls && !tracking_calls);
#endif
    ext[1].desc = &copy;
    CHECK_EQ(ops->spawn(ops->opaque, &frame.view, key, 2.5, 64, -1.5, 1), 0);
    CHECK(probed == &copy && !stock_calls && !tracking_calls);
    requested = "minecraft:zombie";
    CHECK_EQ(ops->spawn(ops->opaque, &frame.view, requested, 2.5, 64, -1.5, 1), 1);
    CHECK(stock_calls == 1 && tracking_calls == 1);
    requested = "test:clone_extra";
    CHECK_EQ(ops->spawn(ops->opaque, &frame.view, requested, 2.5, 64, -1.5, 0), 1);
    CHECK(stock_calls == 2 && tracking_calls == 1);
    TEST_MAIN_END("test_mob_spawn_route195");
}
