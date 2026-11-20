// Mock backend: a simulated 1.95 game state with the same shapes the retail
// backend relies on (item vector of 2268 slots / 2398 capacity, 366 block
// slots, 256x288 item atlas, 256x624 terrain atlas, recipe list, furnace map).
#pragma once
#include <stdint.h>
#include <string.h>
#include "../loader/backend.h"

struct MockGame {
    // "game memory"
    const void *item_slots[2398];   // non-null = occupied
    uint32_t item_size = 2268;      // live vector size (grows inside capacity only)
    const void *block_slots[366];
    uint32_t item_atlas_cells = 288, terrain_atlas_cells = 16 * 39;
    struct Recipe { int32_t cells[9]; int16_t aux[9]; int32_t result, count; int w, h, shapeless, group; } recipes[2048];
    uint32_t recipe_count = 0;
    struct Smelt { int32_t in; int16_t aux; int32_t out; } smelts[512];
    uint32_t smelt_count = 0;
    char names[2398][128];
    uint32_t creative_mod_group[1024]; uint32_t creative_count = 0;
    uint32_t publish_calls = 0, prepare_calls = 0;
    // fault injection
    int fail_prepare = 0;          // prepare returns BACKEND
    int fail_publish_after = -1;   // publish writes N objects then fails (partial)
    uint32_t caps;
    uint32_t vanilla_items = 0;
};

void mock_game_init(MockGame *g, uint32_t caps);
lce_backend mock_backend(MockGame *g);
int mock_item_in_use(void *user, uint32_t id);
int mock_block_in_use(void *user, uint32_t id);
