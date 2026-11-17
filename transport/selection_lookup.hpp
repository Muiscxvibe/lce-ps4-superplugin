#pragma once
#include "join_list.hpp"

namespace lce_selection_lookup {
// The retail adapter owns this vector. Validate its shape before indexing it;
// callers must only pass an adapter that the game's selection method supplied.
inline void *record(const lce_join_list::Vector *list, int index) {
    if (!list || index < 0) return nullptr;
    uintptr_t begin = reinterpret_cast<uintptr_t>(list->begin);
    uintptr_t end = reinterpret_cast<uintptr_t>(list->end);
    if (!begin || end < begin || (end - begin) % sizeof(void *) ||
        (end - begin) / sizeof(void *) > 1024 ||
        static_cast<size_t>(index) >= (end - begin) / sizeof(void *)) return nullptr;
    return list->begin[index];
}
}
