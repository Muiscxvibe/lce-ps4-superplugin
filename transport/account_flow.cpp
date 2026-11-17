#include "account_flow.hpp"
#include <string.h>

namespace lce_account_flow {
namespace {
constexpr uint16_t kName[]={u'S',u'e',u'r',u'v',u'e',u'r',u' ',u'u',u's',u'e',u'r',u'n',u'a',u'm',u'e',0};
constexpr uint16_t kPassword[]={u'S',u'e',u'r',u'v',u'e',u'r',u' ',u'p',u'a',u's',u's',u'w',u'o',u'r',u'd',0};
}
bool Flow::configure(const Ops &ops) {
    if (configured_ || !ops.keyboard_start || !ops.keyboard_take || !ops.schedule_worker || !ops.authenticate) return false;
    ops_=ops;configured_=true;return true;
}
bool Flow::start(int pad) {
    if (!configured_ || state_!=State::Idle || pad<0 || pad>3) return false;
    if (!ops_.keyboard_start(ops_.context,pad,false,kName)) return false;
    pad_=pad;use_token_=remember_=false;state_=State::Username;++generation_;return true;
}
bool Flow::start_with_credentials(int pad,const char *username,const char *password,bool remember) {
    if (!configured_ || state_!=State::Idle || pad<0 || pad>3 || !username || !password) return false;
    size_t n=0,p=0;
    while (n<sizeof name_ && username[n]) {
        const char c=username[n];
        if (!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_')) return false;
        ++n;
    }
    while (p<sizeof password_ && password[p]) ++p;
    if (n<3 || n>=sizeof name_ || p<8 || p>=sizeof password_) return false;
    memcpy(name_,username,n+1);memcpy(password_,password,p+1);
    pad_=pad;use_token_=false;remember_=remember;++generation_;__atomic_store_n(&worker_done_,0u,__ATOMIC_RELAXED);state_=State::Waiting;
    if (!ops_.schedule_worker(ops_.context,worker,this,generation_)) {
        lce_auth_wipe(name_,sizeof name_);lce_auth_wipe(password_,sizeof password_);
        state_=State::Idle;pad_=-1;return false;
    }
    return true;
}
bool Flow::start_with_token(int pad,const char *token) {
    if(!configured_ || state_!=State::Idle || pad<0 || pad>3 || !token)return false;
    for(size_t i=0;i<43;++i) {
        const char c=token[i];
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'))return false;
    }
    if(token[43])return false;
    memcpy(password_,token,44);name_[0]=0;pad_=pad;use_token_=true;remember_=false;
    ++generation_;__atomic_store_n(&worker_done_,0u,__ATOMIC_RELAXED);state_=State::Waiting;
    if(!ops_.schedule_worker(ops_.context,worker,this,generation_)) {
        lce_auth_wipe(password_,sizeof password_);state_=State::Idle;pad_=-1;return false;
    }
    return true;
}
bool Flow::utf8(const uint16_t *in,char *out,size_t cap,size_t *length) {
    if (!in || !out || !cap || !length) return false;
    size_t n=0;
    for (size_t i=0;i<256 && in[i];++i) {
        uint32_t cp=in[i];
        if (cp>=0xd800 && cp<=0xdbff) {
            uint32_t low=in[++i];
            if (i>=256 || low<0xdc00 || low>0xdfff) return false;
            cp=0x10000+((cp-0xd800)<<10)+(low-0xdc00);
        } else if (cp>=0xdc00 && cp<=0xdfff) return false;
        if (cp<0x20) return false;
        unsigned bytes=cp<0x80?1:cp<0x800?2:cp<0x10000?3:4;
        if (n+bytes>=cap) return false;
        if (bytes==1) out[n++]=(char)cp;
        else {
            out[n++]=(char)((bytes==2?0xc0:bytes==3?0xe0:0xf0)|(cp>>(6*(bytes-1))));
            for (unsigned j=bytes-1;j;--j) out[n++]=(char)(0x80|((cp>>(6*(j-1)))&0x3f));
        }
    }
    if (in[256]) return false;
    out[n]=0;*length=n;return true;
}
void Flow::worker(void *ctx,uint64_t generation) {
    auto *self=static_cast<Flow *>(ctx);
    if (!self || generation!=self->generation_) return;
    self->worker_ok_=self->ops_.authenticate(self->ops_.context,self->name_,self->password_,self->use_token_,self->remember_,&self->result_);
    lce_auth_wipe(self->password_,sizeof self->password_);
    __atomic_store_n(&self->worker_done_,1u,__ATOMIC_RELEASE);
}
void Flow::tick() {
    if (state_==State::PasswordStart) {
        if (ops_.keyboard_start(ops_.context,pad_,true,kPassword)) state_=State::Password;
        return;
    }
    if (state_==State::Waiting) {
        if (__atomic_load_n(&worker_done_,__ATOMIC_ACQUIRE)) state_=worker_ok_&&result_.accepted?State::Accepted:State::Failed;
        return;
    }
    if (state_!=State::Username && state_!=State::Password) return;
    uint16_t text[257]={};
    const Input input=ops_.keyboard_take(ops_.context,text,257);
    if (input==Input::Pending) return;
    if (input==Input::Cancelled) {state_=State::Cancelled;return;}
    size_t length=0;
    if (state_==State::Username) {
        bool ok=utf8(text,name_,sizeof name_,&length) && length>=3 && length<=16;
        for (size_t i=0;ok && i<length;++i) {
            char c=name_[i];ok=(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_';
        }
        state_=ok?State::PasswordStart:State::Failed;
    } else {
        bool ok=utf8(text,password_,sizeof password_,&length) && length>=8;
        if (ok) {
            __atomic_store_n(&worker_done_,0u,__ATOMIC_RELAXED);
            state_=State::Waiting;
            if (!ops_.schedule_worker(ops_.context,worker,this,generation_)) {lce_auth_wipe(password_,sizeof password_);state_=State::Failed;}
        } else {lce_auth_wipe(password_,sizeof password_);state_=State::Failed;}
    }
    lce_auth_wipe(text,sizeof text);
}
bool Flow::take_result(lce_auth_result *out) {
    if (state_!=State::Accepted || !out) return false;
    *out=result_;lce_auth_wipe(&result_,sizeof result_);state_=State::Idle;return true;
}
void Flow::reset() {
    if (state_==State::Waiting || state_==State::Username || state_==State::Password) return;
    lce_auth_wipe(name_,sizeof name_);lce_auth_wipe(password_,sizeof password_);
    lce_auth_wipe(&result_,sizeof result_);state_=State::Idle;
}
}
