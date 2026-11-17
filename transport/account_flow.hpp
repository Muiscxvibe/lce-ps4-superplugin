#pragma once
#include "auth_client.h"
#include <stdint.h>

namespace lce_account_flow {
enum class Input { Pending, Accepted, Cancelled };
enum class State { Idle, Username, PasswordStart, Password, Waiting, Accepted, Failed, Cancelled };
struct Ops {
    void *context;
    bool (*keyboard_start)(void *,int,bool,const uint16_t *);
    Input (*keyboard_take)(void *,uint16_t *,size_t);
    bool (*schedule_worker)(void *,void (*)(void *,uint64_t),void *,uint64_t);
    bool (*authenticate)(void *,const char *,const char *,bool,bool,lce_auth_result *);
};
// UI-thread owner; the worker owns credential buffers while state is Waiting.
// Never free this object while a request is outstanding.
class Flow {
public:
    bool configure(const Ops &);
    bool start(int pad);
    bool start_with_credentials(int pad,const char *username,const char *password,bool remember=false);
    bool start_with_token(int pad,const char *token);
    void tick();
    State state() const { return state_; }
    bool take_result(lce_auth_result *);
    void reset();
private:
    static void worker(void *,uint64_t);
    static bool utf8(const uint16_t *,char *,size_t,size_t *);
    Ops ops_{};
    State state_=State::Idle;
    int pad_=-1;
    uint64_t generation_=0;
    char name_[17]{}, password_[257]{};
    lce_auth_result result_{};
    unsigned worker_done_=0;
    bool worker_ok_=false,configured_=false,use_token_=false,remember_=false;
};
}
