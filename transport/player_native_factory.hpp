#pragma once
#include <stdint.h>
namespace lce_player_native {
// The caller must hold a live game-owned SQR player throughout wrapper use.
// The wrapper does not own that player. bind requires a readable pinned image.
bool bind(uintptr_t base);
void *wrap(void *sqr_player);
// The wrapper must already be detached from all game users and hooks.
void destroy(void *wrapper);
}
