#pragma once
#include <stdint.h>
// Candidate for the retail 1.95 NetworkPlayerSony primary vtable. Must remain
// resident until the original vptr is restored and all dispatches are drained.
namespace lce_player_hook {
class Vtable {
public:
    Vtable()=default;
    Vtable(const Vtable&)=delete;
    Vtable& operator=(const Vtable&)=delete;
    bool prepare(uintptr_t base, const uintptr_t *header, uintptr_t send_hook);
    const uintptr_t *address_point() const { return ready_ ? words_+2 : nullptr; }
private:
    uintptr_t words_[32]{};
    bool ready_=false;
};
}
