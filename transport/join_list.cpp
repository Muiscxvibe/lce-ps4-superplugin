#include "join_list.hpp"
#include <string.h>

namespace lce_join_list {
static_assert(sizeof(void *) == 8 && sizeof(Vector) == 0x20, "unexpected native vector ABI");
static_assert(offsetof(Vector, begin) == 8 && offsetof(Vector, end) == 16 &&
              offsetof(Vector, capacity) == 24, "unexpected native vector layout");
namespace {
constexpr size_t kRecordSize = 0x208;
constexpr size_t kLabelPtr = 0xb0, kLabelLength = 0xb8;
constexpr size_t kSessionData = 0xbc, kJoinable = 0x1f9;
constexpr size_t kPlayerCount = 0x1fa, kReady = 0x1fb;
const uint8_t kMarker[12] = {'L','C','E','P','C','S','E','R','V','E','R',1};
}

bool endpoint_index(const void *record, uint32_t *out_index) {
    if (!record || !out_index || memcmp(record, kMarker, sizeof kMarker)) return false;
    memcpy(out_index, static_cast<const uint8_t *>(record) + sizeof kMarker, sizeof *out_index);
    return true;
}
bool make_join_record(void *record,size_t size,uint32_t index) {
    if (!record || size<kRecordSize || index>=8) return false;
    memset(record,0,kRecordSize);
    memcpy(record,kMarker,sizeof kMarker);
    memcpy(static_cast<uint8_t *>(record)+sizeof kMarker,&index,sizeof index);
    return true;
}

Result append(Vector *list, const char *label, uint32_t index,
              uint8_t player_count, bool ready, const Ops &ops) {
    if (!ready) return BackendUnavailable;
    if (!list || !label || !ops.alloc_object || !ops.alloc_array || !ops.dealloc ||
        !ops.init_session_data || !ops.reserve_extra || player_count > 8 || index >= 8)
        return InvalidInput;
    size_t len = 0;
    while (len < 16 && label[len]) {
        const unsigned char ch = static_cast<unsigned char>(label[len]);
        if (ch < 32 || ch > 126) return InvalidInput;
        ++len;
    }
    if (!len) return InvalidInput;
    const uintptr_t begin = reinterpret_cast<uintptr_t>(list->begin);
    const uintptr_t end = reinterpret_cast<uintptr_t>(list->end);
    const uintptr_t capacity = reinterpret_cast<uintptr_t>(list->capacity);
    if ((begin || end || capacity) && (!begin || begin > end || end > capacity ||
        (end - begin) % sizeof(void *) || (capacity - begin) % sizeof(void *)))
        return InvalidVector;

    uint8_t *record = static_cast<uint8_t *>(ops.alloc_object(kRecordSize));
    if (!record) return OutOfMemory;
    memset(record, 0, kRecordSize);
    ops.init_session_data(record + kSessionData);
    uint16_t *wide = static_cast<uint16_t *>(ops.alloc_array((len + 1) * sizeof(uint16_t)));
    if (!wide) { ops.dealloc(record); return OutOfMemory; }
    for (size_t i = 0; i < len; ++i) wide[i] = static_cast<unsigned char>(label[i]);
    wide[len] = 0;
    memcpy(record, kMarker, sizeof kMarker);
    memcpy(record + sizeof kMarker, &index, sizeof index);
    memcpy(record + kLabelPtr, &wide, sizeof wide);
    record[kLabelLength] = static_cast<uint8_t>(len);
    const uint16_t net_version = 2415;
    memcpy(record + kSessionData, &net_version, sizeof net_version);
    record[kJoinable] = 1; record[kPlayerCount] = player_count; record[kReady] = 1;

    if (list->end == list->capacity && !ops.reserve_extra(list, 1)) {
        ops.dealloc(wide); ops.dealloc(record); return OutOfMemory;
    }
    // The reserve callback must provide at least one valid slot.
    if (!list->end || !list->capacity ||
        reinterpret_cast<uintptr_t>(list->end) >= reinterpret_cast<uintptr_t>(list->capacity)) {
        ops.dealloc(wide); ops.dealloc(record); return InvalidVector;
    }
    *list->end++ = record;
    return Added;
}
}
