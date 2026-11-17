#pragma once

#include <stddef.h>
#include <stdint.h>

// Offline research helper for the pinned 1.95 image. The integration must
// verify the executable, run on the game owner thread, and reject unmapped
// memory. This helper is not part of the installed diagnostic.
namespace lce_metadata_snapshot_extended {
typedef bool (*Read)(void *context, uintptr_t address, void *out, size_t size);
enum Slot {
    flags, air, custom_name, custom_name_visible, living_first, health,
    effect_color, effect_ambient, arrow_count, baby, positive_age, in_love,
    pig_saddle, pig_extra, sheep_wool, mob_use_state,
    creeper_swell, creeper_powered, spider_climbing, bat_resting, zombie_baby,
    rabbit_variant, horse_variant,
    slot_count
};
struct Values {
    uintptr_t accessors[slot_count];
    int32_t indices[slot_count];
};

inline uintptr_t offset(unsigned slot) {
    static const uintptr_t offsets[slot_count] = {
        0x1415a38, 0x1415a40, 0x1415a48, 0x1415a50, 0x1418c68,
        0x1418c70, 0x1418c78, 0x1418c80, 0x1418c88, 0x1408f28,
        0x1408f30, 0x1408f40, 0x142e228, 0x142e230, 0x142ff90,
        0x1419510, 0x1413948, 0x1413950, 0x1432060, 0x1409c80, 0x1434940,
        0x142f1e0, 0x1417248
    };
    return slot<slot_count ? offsets[slot] : 0;
}

// Per-slot diagnostic only: independently observed values must not be used
// for packet encoding until inherited field collisions have been resolved.
enum DetailStatus { valid, global_unreadable, pointer_invalid, value_unreadable,
                    value_invalid, changed };
struct Details { int32_t indices[slot_count]; DetailStatus status[slot_count]; };

inline void inspect(Read read, void *context, uintptr_t image_base, Details &out) {
    for (unsigned i=0; i<slot_count; ++i) {
        out.indices[i]=-1;
        out.status[i]=global_unreadable;
        if (!read || !image_base || image_base>UINTPTR_MAX-offset(i)) continue;
        uintptr_t first=0,second=0;
        int32_t a=-1,b=-1;
        if (!read(context,image_base+offset(i),&first,sizeof first)) continue;
        if (!first || (first&3)) { out.status[i]=pointer_invalid; continue; }
        if (!read(context,first,&a,sizeof a)) { out.status[i]=value_unreadable; continue; }
        if (a<0 || a>=255) { out.status[i]=value_invalid; continue; }
        if (!read(context,image_base+offset(i),&second,sizeof second) ||
            !second || (second&3) || !read(context,second,&b,sizeof b) ||
            first!=second || a!=b) { out.status[i]=changed; continue; }
        out.indices[i]=a;
        out.status[i]=valid;
    }
}

// Sibling classes may allocate the same numeric ID; inherited fields and
// fields defined on the same class may not collide. The shared Mob use-state
// accessor is inherited even though only Witch currently maps an old field.
inline unsigned class_mask(unsigned slot) {
    constexpr unsigned pig=1, sheep=2, creeper=4, spider=8, bat=16, witch=32, zombie=64, rabbit=128, horse=256;
    constexpr unsigned all=pig|sheep|creeper|spider|bat|witch|zombie|rabbit|horse;
    if (slot<=arrow_count || slot==mob_use_state) return all;
    if (slot>=baby && slot<=in_love) return pig|sheep|rabbit|horse;
    if (slot==pig_saddle || slot==pig_extra) return pig;
    if (slot==sheep_wool) return sheep;
    if (slot==creeper_swell || slot==creeper_powered) return creeper;
    if (slot==spider_climbing) return spider;
    if (slot==bat_resting) return bat;
    if (slot==zombie_baby) return zombie;
    if (slot==rabbit_variant) return rabbit;
    if (slot==horse_variant) return horse;
    return 0;
}

inline bool read_once(Read read, void *context, uintptr_t image_base, Values &out) {
    Values candidate{};
    if (!read || !image_base) return false;
    for (unsigned i=0; i<slot_count; ++i) {
        if (image_base > UINTPTR_MAX-offset(i) ||
            !read(context,image_base+offset(i),&candidate.accessors[i],sizeof(uintptr_t)) ||
            !candidate.accessors[i] || (candidate.accessors[i]&3) ||
            !read(context,candidate.accessors[i],&candidate.indices[i],sizeof(int32_t)) ||
            candidate.indices[i]<0 || candidate.indices[i]>=255) return false;
        for (unsigned j=0; j<i; ++j) {
            // Unrelated classes can share the same accessor object as well
            // as the same numeric index. A collision only matters where both
            // fields exist on the same concrete class.
            if ((class_mask(i)&class_mask(j)) &&
                (candidate.accessors[i]==candidate.accessors[j] ||
                 candidate.indices[i]==candidate.indices[j])) return false;
        }
    }
    out=candidate;
    return true;
}

inline bool stable(Read read, void *context, uintptr_t image_base, Values &out) {
    Values first{},second{};
    if (!read_once(read,context,image_base,first) || !read_once(read,context,image_base,second)) return false;
    for (unsigned i=0; i<slot_count; ++i) {
        if (first.accessors[i]!=second.accessors[i] || first.indices[i]!=second.indices[i]) return false;
    }
    out=second;
    return true;
}
}
