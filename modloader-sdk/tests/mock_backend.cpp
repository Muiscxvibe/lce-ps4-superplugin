#include "mock_backend.h"
#include <stdio.h>
#include "../loader/groups.h"
#include "../loader/loader.h"

static const char kStock = 's';

void mock_game_init(MockGame *g, uint32_t caps) {
    memset(g->item_slots, 0, sizeof g->item_slots);
    memset(g->block_slots, 0, sizeof g->block_slots);
    memset(g->names, 0, sizeof g->names);
    g->item_size = 2268; g->recipe_count = g->smelt_count = g->creative_count = 0;
    g->publish_calls = g->prepare_calls = 0; g->fail_prepare = 0; g->fail_publish_after = -1;
    g->item_atlas_cells = 288; g->terrain_atlas_cells = 16 * 39;
    g->caps = caps;
    g->vanilla_items = 0;
    // Occupy every vanilla ID (table) the way the retail registry does.
    for (uint32_t id = 1; id < 2398; ++id)
        if (lce_vanilla_item_exists(id)) { g->item_slots[id] = &kStock; ++g->vanilla_items; }
    for (uint32_t id = 1; id < 366; ++id) {
        uint32_t item = id < 256 ? id : id + 768;
        if (id != 256 && lce_vanilla_item_exists(item)) g->block_slots[id] = &kStock;
    }
    g->block_slots[0] = &kStock;  // air
    // other providers already in the shipping plugin: Lucky block 253 (item 253), Steel ore 261 (item 1029)
    g->block_slots[253] = &kStock; g->item_slots[253] = &kStock;
    g->block_slots[261] = &kStock; g->item_slots[1029] = &kStock;
}

int mock_item_in_use(void *user, uint32_t id) {
    MockGame *g = (MockGame *)user;
    return id >= 2398 || g->item_slots[id] != nullptr;
}
int mock_block_in_use(void *user, uint32_t id) {
    MockGame *g = (MockGame *)user;
    return id >= 366 || g->block_slots[id] != nullptr;
}

static int32_t resolve_group(void *, const char *key, int crafting) { return lce_group_lookup(key, crafting); }

static lce_status prepare(void *self, lce_plan *p, char *err, uint32_t cap) {
    MockGame *g = (MockGame *)self;
    ++g->prepare_calls;
    if (g->fail_prepare) { snprintf(err, cap, "mock: pinned signature mismatch"); return LCE_E_BACKEND; }
    // capacity checks only; assign atlas cells (backend-private bookkeeping, no game mutation)
    uint32_t icell = g->item_atlas_cells, tcell = g->terrain_atlas_cells;
    for (uint32_t i = 0; i < p->texture_count; ++i) p->textures[i].slot = (int32_t)(p->textures[i].atlas ? tcell++ : icell++);
    if (icell > 288 + 16 * 8 || tcell > 16 * 39 + 16 * 8) { snprintf(err, cap, "mock: atlas growth limit"); return LCE_E_FULL; }
    for (uint32_t i = 0; i < p->item_count; ++i) {
        uint32_t id = p->items[i].id;
        if (id >= 2398 || g->item_slots[id]) { snprintf(err, cap, "mock: item slot %u occupied", id); return LCE_E_COLLISION; }
    }
    for (uint32_t i = 0; i < p->block_count; ++i) {
        uint32_t id = p->blocks[i].id;
        if (id >= 366 || id == 256 || g->block_slots[id]) { snprintf(err, cap, "mock: block slot %u unusable", id); return LCE_E_COLLISION; }
        if (p->blocks[i].item_id && (p->blocks[i].item_id >= 2398 || g->item_slots[p->blocks[i].item_id])) { snprintf(err, cap, "mock: block item slot occupied"); return LCE_E_COLLISION; }
    }
    for (uint32_t i = 0; i < p->recipe_count; ++i) {
        if (p->recipes[i].result_id < 0) { snprintf(err, cap, "mock: unresolved recipe"); return LCE_E_NOTFOUND; }
        if (p->recipes[i].desc->shapeless && !(g->caps & (1u << 31))) {}  // mock accepts shapeless
    }
    return LCE_OK;
}

static lce_status publish(void *self, const lce_plan *p, int *partial, char *err, uint32_t cap) {
    MockGame *g = (MockGame *)self;
    ++g->publish_calls;
    int writes = 0;
    auto step = [&]() -> bool {
        if (g->fail_publish_after >= 0 && writes >= g->fail_publish_after) { *partial = writes > 0; return false; }
        ++writes;
        return true;
    };
    for (uint32_t i = 0; i < p->block_count; ++i) {
        if (!step()) { snprintf(err, cap, "mock: injected failure"); return LCE_E_BACKEND; }
        g->block_slots[p->blocks[i].id] = p->blocks[i].desc;
        if (p->blocks[i].item_id) {
            g->item_slots[p->blocks[i].item_id] = p->blocks[i].desc;
            snprintf(g->names[p->blocks[i].item_id], 128, "%s", p->blocks[i].desc->name);
        }
    }
    for (uint32_t i = 0; i < p->item_count; ++i) {
        if (!step()) { snprintf(err, cap, "mock: injected failure"); return LCE_E_BACKEND; }
        g->item_slots[p->items[i].id] = p->items[i].desc;
        snprintf(g->names[p->items[i].id], 128, "%s", p->items[i].desc->name);
        if (!p->items[i].placeholder && g->creative_count < 1024) g->creative_mod_group[g->creative_count++] = p->items[i].id;
    }
    for (uint32_t i = 0; i < p->recipe_count; ++i) {
        if (!step()) { snprintf(err, cap, "mock: injected failure"); return LCE_E_BACKEND; }
        const lce_plan_recipe &r = p->recipes[i];
        MockGame::Recipe &m = g->recipes[g->recipe_count++];
        for (int c = 0; c < 9; ++c) { m.cells[c] = r.cells[c].id; m.aux[c] = r.cells[c].aux; }
        m.result = r.result_id; m.count = r.desc->result_count; m.w = r.desc->width; m.h = r.desc->height;
        m.shapeless = r.desc->shapeless; m.group = r.craft_group;
    }
    for (uint32_t i = 0; i < p->smelt_count; ++i) {
        if (!step()) { snprintf(err, cap, "mock: injected failure"); return LCE_E_BACKEND; }
        g->smelts[g->smelt_count++] = MockGame::Smelt{p->smelts[i].input.id, p->smelts[i].input.aux, p->smelts[i].result_id};
    }
    for (uint32_t i = 0; i < p->string_count; ++i)
        if (p->strings[i].item_id >= 0 && !strcmp(p->strings[i].locale, "en_US")) snprintf(g->names[p->strings[i].item_id], 128, "%s", p->strings[i].value);
    g->item_atlas_cells += 0;  // atlas growth is simulated by prepare's slot assignment
    return LCE_OK;
}

lce_backend mock_backend(MockGame *g) {
    lce_backend b;
    memset(&b, 0, sizeof b);
    b.name = "mock195";
    b.capabilities = g->caps;
    b.self = g;
    b.resolve_group = resolve_group;
    b.prepare = prepare;
    b.publish = publish;
    return b;
}
