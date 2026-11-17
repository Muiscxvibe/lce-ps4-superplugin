#include "native_keyboard.hpp"
#include "auth_client.h"
#include <string.h>

namespace lce_native_keyboard {
namespace {
constexpr uintptr_t kGlobal=0x147c978, kVtable=0x1349080;
constexpr uintptr_t kRequest=0xdba4b0, kGetText=0xdba730;
constexpr size_t kState=0x10e8;
constexpr uint16_t kEmpty[]={0};
}
bool Keyboard::bind(uintptr_t image) {
    if (image_ || !image || image>UINTPTR_MAX-kGlobal) return false;
    const unsigned char request[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,
        0x41,0x55,0x41,0x54,0x53,0x50,0x48,0x89,0xfb};
    const unsigned char get[]={0x8d,0x42,0x01,0x4c,0x8d,0x87,0x20,0x11,0,0};
    if (memcmp(reinterpret_cast<void *>(image+kRequest),request,sizeof request) ||
        memcmp(reinterpret_cast<void *>(image+kGetText),get,sizeof get)) return false;
    auto *table=reinterpret_cast<uintptr_t *>(image+kVtable);
    if (table[0x140/8]!=image+kRequest || table[0x150/8]!=image+kGetText) return false;
    image_=image;
    return true;
}
void *Keyboard::manager() const {
    if (!image_) return nullptr;
    void *input=*reinterpret_cast<void **>(image_+kGlobal);
    if (!input || *reinterpret_cast<uintptr_t **>(input)!=reinterpret_cast<uintptr_t *>(image_+kVtable)) return nullptr;
    return input;
}
bool Keyboard::start(int pad,bool password,const uint16_t *title) {
    void *input=manager();
    if (!input || pad<0 || pad>=4 || !title || active_ || ready_ ||
        *reinterpret_cast<int *>(static_cast<uint8_t *>(input)+kState)!=0) return false;
    lce_auth_wipe(value_,sizeof value_);
    password_=password; result_=Result::Pending; active_=true;
    // Verified argument order from the target's call site and request body.
    // A successful request changes state 0 -> 1; the native return value is
    // zero even when busy, so it cannot be used as an acceptance indication.
    using Request=int (*)(void *,const uint16_t *,const uint16_t *,int,unsigned,
                          int (*)(void *,bool),void *,int);
    reinterpret_cast<Request>(image_+kRequest)(input,title,kEmpty,pad,256,completed,this,password?2:3);
    if (*reinterpret_cast<int *>(static_cast<uint8_t *>(input)+kState)!=1) {
        active_=false; password_=false; return false;
    }
    return true;
}
int Keyboard::completed(void *context,bool accepted) {
    auto *self=static_cast<Keyboard *>(context);
    if (!self || !self->active_) return 0;
    void *input=self->manager();
    if (accepted && input) {
        // Retail GetText takes a maximum length as its third argument.
        using GetText=void (*)(void *,uint16_t *,int);
        reinterpret_cast<GetText>(self->image_+kGetText)(input,self->value_,256);
        self->value_[256]=0;
        self->result_=Result::Accepted;
    } else self->result_=Result::Cancelled;
    // The target invokes this callback after terminating its native dialog.
    // Remove password text from its shared 0x1120 input buffer as well.
    if (input && self->password_)
        lce_auth_wipe(static_cast<uint8_t *>(input)+0x1120,sizeof self->value_);
    self->active_=false; self->ready_=true;
    return 0;
}
Keyboard::Result Keyboard::take(uint16_t *out,size_t capacity) {
    // The retail tick resets state to zero without a callback if native IME
    // initialization fails. Otherwise the account flow would wait forever.
    // A normal completion invokes completed() before resetting that state.
    if (active_) {
        void *input=manager();
        if (!input || *reinterpret_cast<int *>(static_cast<uint8_t *>(input)+kState)==0) {
            if (input && password_)
                lce_auth_wipe(static_cast<uint8_t *>(input)+0x1120,sizeof value_);
            lce_auth_wipe(value_,sizeof value_);
            active_=false; ready_=true; result_=Result::Cancelled;
        }
    }
    if (!ready_ || !out || !capacity) return Result::Pending;
    size_t n=0;
    while (n<256 && value_[n]) ++n;
    if (result_==Result::Accepted && capacity<=n) return Result::Pending;
    Result result=result_;
    if (result==Result::Accepted) memcpy(out,value_,(n+1)*sizeof *out);
    else out[0]=0;
    lce_auth_wipe(value_,sizeof value_);
    ready_=false; password_=false; result_=Result::Pending;
    return result;
}
}
