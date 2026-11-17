#pragma once
#include <stdint.h>

namespace lce_native_start {
// Bind to the pinned 1.95 image. Call on Minecraft's UI thread only after the
// custom session's player, socket and query hooks are live. The caller owns
// cancellation, failure handling and eventual teardown.
bool bind(uintptr_t image_base);
bool start_client(void *expected_platform_manager);
}
