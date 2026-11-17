#pragma once
#include <stdint.h>
namespace lce_native_game_started {
// Checks the pinned 1.95 App getter and setter. A true result is a game
// loading signal, not proof of an accepted login or usable world.
bool bind(uintptr_t image_base);
bool read(bool *started);
}
