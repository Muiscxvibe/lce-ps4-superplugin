/* Mod ID space for CUSA00265 1.95. Sources (all 1.95 RVAs, image
 * c675e6ce...029f, RX segment 231a6bfb...c682):
 *  - Item::items vector header at base+0x1417720: live size 2268, capacity
 *    2398, 547 occupied (console: runtime-tests/mc195-probe-zpf5w_3j,
 *    ORE_PROBE item_count=2268 item_capacity=2398). Mod items must stay below
 *    the retained capacity: the vector is never grown.
 *  - 2268..2282 are owned by the bundled providers (Steel 2268-2278, Lucky
 *    2270/2280-2282; additive_items_195/README.md, steel_tools_195). The SDK
 *    reserves the whole 2268..2282 block so they can be re-enabled later.
 *  - Item IDs 1024..2254 are remapped by 0x385a60 (id-768): never allocate
 *    plain items there; they are only reachable as block-items.
 *  - Block registry at base+0x140ad68 has 366 slots (0..365, getter 0xb84c0
 *    caps at 365). Block 256 is refused (item 1024 crosses the decoder
 *    boundary at 0x91c50 / fallback 0xb84f0; steel_ore_native_provider_195).
 *    Lucky Block owns 253, Steel Ore owns 261 (item 1029).
 *  - A block B registers block-item B (<256) or B+768 (>=256).
 *  - Block IDs are allocated ONLY from IDs that the live ORE_PROBE observed
 *    with both the block slot and its mapped item slot empty
 *    (runtime-tests/mc195-lucky-cleanup-r24-s9v_d5zy/log-before and
 *    mc195-nbt-refresh-live-u7rfkskg: 253 254 261 262 280-290 294 314-318),
 *    minus 253/261 (Lucky/Steel). IDs missing from the static item table
 *    (8, 10, 36, 51, 64, ...) are item-less vanilla blocks and are NOT free.
 *    The retail backend re-checks every slot live before publishing.
 */
#pragma once
#include <string.h>
#include "../ledger.h"

static inline void lce195_default_id_space(lce_id_space *s) {
    static const uint32_t items[] = {2268, 2269, 2270, 2271, 2272, 2273, 2274, 2275, 2276, 2277, 2278, 2279, 2280, 2281, 2282, 253, 1029};
    static const uint32_t blocks[] = {253, 256, 261};
    s->item_min = 2283; s->item_max = 2397;
    s->block_min = 1; s->block_max = 365;
    s->reserved_item_count = 0;
    for (unsigned i = 0; i < sizeof items / sizeof items[0]; ++i) s->reserved_items[s->reserved_item_count++] = items[i];
    static const uint32_t allow[] = {254, 262, 280, 281, 282, 283, 284, 285, 286, 287, 288, 289, 290, 294, 314, 315, 316, 317, 318};
    s->block_allow_count = 0;
    for (unsigned i = 0; i < sizeof allow / sizeof allow[0]; ++i) s->block_allow[s->block_allow_count++] = allow[i];
    s->reserved_block_count = 0;
    for (unsigned i = 0; i < sizeof blocks / sizeof blocks[0]; ++i) s->reserved_blocks[s->reserved_block_count++] = blocks[i];
}

/* API 1.1 registries with saved numeric ids (ledger kinds effect/enchant/biome):
 *  - MobEffect: stock 1..31; NBT "Id" and the UpdateMobEffect/RemoveMobEffect packets carry one byte,
 *    zero-extended (0x491ccc / 0x491ee5 / 0x19e3a2 / 0xba15f3); 32..127 keeps a Java server's signed byte positive.
 *  - Enchantment: stock 0..92 (gaps); NBT and packets carry a short; the MappedRegistry grows. 100..1023.
 *  - Biome: Biome* biomes[256] (0x1409fc0); stock 72 ids up to 167. 168..254 first (no RegionHills +128
 *    mutation lookup above 127, 0x5b874e), then 50..126 (their +128 slots stay empty).
 *  Potions are stored by key (NBT "Potion"), sounds are not saved: neither uses the ledger. */
static inline void lce195_ext_id_space(lce_id_space *s) {
    memset(s->ext_range, 0, sizeof s->ext_range);
    s->ext_range[LCE_KIND_EFFECT - LCE_KIND_EXT_FIRST][0][0] = 32;  s->ext_range[LCE_KIND_EFFECT - LCE_KIND_EXT_FIRST][0][1] = 127;
    s->ext_range[LCE_KIND_ENCHANT - LCE_KIND_EXT_FIRST][0][0] = 100; s->ext_range[LCE_KIND_ENCHANT - LCE_KIND_EXT_FIRST][0][1] = 1023;
    s->ext_range[LCE_KIND_BIOME - LCE_KIND_EXT_FIRST][0][0] = 168; s->ext_range[LCE_KIND_BIOME - LCE_KIND_EXT_FIRST][0][1] = 254;
    s->ext_range[LCE_KIND_BIOME - LCE_KIND_EXT_FIRST][1][0] = 50;  s->ext_range[LCE_KIND_BIOME - LCE_KIND_EXT_FIRST][1][1] = 126;
}
