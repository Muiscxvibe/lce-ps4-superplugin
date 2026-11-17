#pragma once
#include <stddef.h>
#include <stdint.h>

namespace lce_native_keyboard {
// Process-lifetime game-thread owner. The game owns and ticks the dialog.
// Completion is deferred: do not start the next request inside its callback.
class Keyboard {
public:
    enum class Result { Pending, Accepted, Cancelled };
    bool bind(uintptr_t image);
    bool start(int pad,bool password,const uint16_t *title);
    Result take(uint16_t *out,size_t capacity);
private:
    static int completed(void *context,bool accepted);
    void *manager() const;
    uintptr_t image_=0;
    uint16_t value_[257]{};
    Result result_=Result::Pending;
    bool active_=false, ready_=false, password_=false;
};
}
