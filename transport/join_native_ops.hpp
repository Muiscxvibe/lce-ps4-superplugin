#pragma once
#include "join_list.hpp"
#include <stdint.h>
namespace lce_join_native {
// The executable must be the pinned 1.95 build. The caller guarantees that
// base and every checked code address are readable and keeps code mapped.
// Uses one binding for the lifetime of any native rows or callbacks.
bool bind(uintptr_t base);
lce_join_list::Ops ops();
}
