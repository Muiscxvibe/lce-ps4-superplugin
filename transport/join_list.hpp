#pragma once
#include <stddef.h>
#include <stdint.h>

// PS4 LCE 1.95 only. The caller supplies the game's allocator, vector growth
// and GameSessionData constructor, after validating the target executable.
// All functions run on the game UI thread; ownership transfers to the game
// vector on success. This module does not install a hook or start a connection.
namespace lce_join_list {
struct Vector { void *allocator; void **begin; void **end; void **capacity; };
struct Ops {
    void *(*alloc_object)(size_t);
    void *(*alloc_array)(size_t);
    void (*dealloc)(void *);
    void (*init_session_data)(void *);
    bool (*reserve_extra)(Vector *, size_t);
};
enum Result { Added, BackendUnavailable, InvalidInput, OutOfMemory, InvalidVector };

Result append(Vector *list, const char *label, uint32_t endpoint_index,
              uint8_t player_count, bool backend_ready, const Ops &ops);
bool endpoint_index(const void *record, uint32_t *out_index);
// Stack-lifetime record for the native synchronous join path. It may only be
// used where retail code is proven to pass the record through without saving.
bool make_join_record(void *record,size_t size,uint32_t endpoint_index);
}
