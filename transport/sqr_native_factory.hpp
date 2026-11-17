#pragma once
#include <stdint.h>
namespace lce_sqr_native {
// All calls run on the game owner thread and require the pinned 1.95 image.
// Local construction invokes the game's voice initialization. UID points to
// a complete native 28-byte PlayerUID. Manager and UID must remain valid for
// construction; no manager slot registration is performed here.
bool bind(uintptr_t base);
void *create(void *manager, bool local, int user_index, uint8_t small_id,
             const void *native_uid);
// Only after wrappers, sockets, callbacks and all dispatches are drained.
void destroy(void *player);
}
