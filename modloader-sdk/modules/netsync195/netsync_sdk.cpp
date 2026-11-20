#include "netsync_sdk.h"

namespace lce_netsync {
using namespace lce_modids;

namespace {
uint16_t centi(uint32_t milli) {
    uint32_t v = milli / 10;
    return (uint16_t)(v > 0xfffe ? 0xfffe : v);
}
}  // namespace

int entries_from_sdk(const lce_ledger *ledger, const lce_plan *plan, bool published, Entry *out, unsigned cap) {
    if (!ledger || !out || ledger->count > cap || ledger->count > kMaxEntries) return -1;
    unsigned n = 0;
    for (uint32_t i = 0; i < ledger->count; ++i) {
        const lce_ledger_entry &le = ledger->entries[i];
        // Effect/enchantment/biome/potion ids are not part of LCE|ModIds v1 (the wire Kind is item|block
        // and the PC server validates it): they are never sent, so they can never alias an item id.
        if (le.kind != LCE_KIND_ITEM && le.kind != LCE_KIND_BLOCK) continue;
        Entry &e = out[n++];
        memset(&e, 0, sizeof e);
        if (le.id > 0xffff || !set_key(e, le.key)) return -1;
        e.kind = le.kind == LCE_KIND_BLOCK ? kBlock : kItem;
        e.id = (uint16_t)le.id;
        e.max_stack = 64;
        if (e.kind == kBlock) e.flags |= kBlockHasItem;  // the ledger reserves the block-item ID either way
        if (plan && published) {
            if (e.kind == kItem) {
                for (uint32_t k = 0; k < plan->item_count; ++k) {
                    const lce_plan_item &pi = plan->items[k];
                    if (pi.id != le.id || !pi.desc) continue;
                    e.flags |= kLive;
                    uint16_t ms = pi.desc->max_stack;
                    e.max_stack = (uint8_t)(ms == 0 || ms > 64 ? 64 : ms);
                    e.material = (uint8_t)pi.desc->kind;
                    e.a = pi.desc->max_damage;
                    break;
                }
            } else {
                for (uint32_t k = 0; k < plan->block_count; ++k) {
                    const lce_plan_block &pb = plan->blocks[k];
                    if (pb.id != le.id || !pb.desc) continue;
                    e.flags |= kLive;
                    if (!pb.item_id) e.flags &= (uint8_t)~kBlockHasItem;
                    if (pb.desc->flags & LCE_BLOCK_FLAG_TRANSPARENT) e.flags |= kTransparent;
                    if (pb.desc->flags & LCE_BLOCK_FLAG_NO_COLLIDE) e.flags |= kNoCollide;
                    e.material = (uint8_t)pb.desc->material;
                    e.a = centi(pb.desc->hardness_milli);
                    e.b = centi(pb.desc->resistance_milli);
                    break;
                }
            }
        }
    }
    return (int)n;
}

}  // namespace lce_netsync
