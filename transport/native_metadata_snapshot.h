#pragma once

#include <stddef.h>
#include <stdint.h>

// Read-only research probe for the pinned 1.95 client. No game hooks, writes,
// network data or account details are accessed by this helper. The caller's
// read function must reject unreadable or partially mapped ranges.
namespace lce_metadata_snapshot {
typedef bool (*Read)(void *context, uintptr_t address, void *out, size_t size);

struct Values {
    uintptr_t accessors[4];
    int32_t indices[4]; // flags, air, first living field, health
};

inline bool read_once(Read read, void *context, uintptr_t image_base, Values &out) {
    static const uintptr_t offsets[4] = {0x1415a38, 0x1415a40, 0x1418c68, 0x1418c70};
    Values candidate{};
    if (!read || !image_base) return false;
    for (unsigned i = 0; i < 4; ++i) {
        if (image_base > UINTPTR_MAX - offsets[i] ||
            !read(context, image_base + offsets[i], &candidate.accessors[i], sizeof(uintptr_t)) ||
            !candidate.accessors[i] || (candidate.accessors[i] & 3) ||
            !read(context, candidate.accessors[i], &candidate.indices[i], sizeof(int32_t)) ||
            candidate.indices[i] < 0 || candidate.indices[i] >= 255) return false;
        for (unsigned j = 0; j < i; ++j) {
            if (candidate.accessors[i] == candidate.accessors[j] ||
                candidate.indices[i] == candidate.indices[j]) return false;
        }
    }
    out = candidate;
    return true;
}

// Require identical consecutive observations to reject changing initialization.
// This is not a memory-ownership guarantee: the integration must run on the
// verified game owner thread and enforce the pinned executable signature.
inline bool stable(Read read, void *context, uintptr_t image_base, Values &out) {
    Values first{}, second{};
    if (!read_once(read, context, image_base, first) ||
        !read_once(read, context, image_base, second)) return false;
    for (unsigned i = 0; i < 4; ++i) {
        if (first.accessors[i] != second.accessors[i] || first.indices[i] != second.indices[i]) return false;
    }
    out = second;
    return true;
}
}
