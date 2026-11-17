#include "account_flow.hpp"
#include <cassert>
#include <cstring>
using namespace lce_account_flow;
struct Mock {
    int starts=0;bool cancelled=false,expected_token=false,expected_remember=false;
    void (*work)(void *,uint64_t)=nullptr;void *work_context=nullptr;uint64_t generation=0;
};
static bool start(void *p,int pad,bool password,const uint16_t *) {
    auto *m=static_cast<Mock *>(p);assert(pad==1 && password==(m->starts==1));++m->starts;return true;
}
static Input take(void *p,uint16_t *out,size_t size) {
    auto *m=static_cast<Mock *>(p);assert(size>=10);
    if (m->cancelled) return Input::Cancelled;
    const char16_t *s=m->starts==1?u"Codey":u"password123";
    while ((*out++=*s++)) {}
    return Input::Accepted;
}
static bool schedule(void *p,void (*fn)(void *,uint64_t),void *context,uint64_t generation) {
    auto *m=static_cast<Mock *>(p);m->work=fn;m->work_context=context;m->generation=generation;return true;
}
static bool auth(void *context,const char *name,const char *password,bool token,bool remember,lce_auth_result *result) {
    auto *mock=static_cast<Mock *>(context);assert(token==mock->expected_token && remember==mock->expected_remember);
    if(token)assert(!name[0] && strlen(password)==43);else assert(!strcmp(name,"Codey") && !strcmp(password,"password123"));
    result->accepted=1;result->game_uid=42;return true;
}
int main() {
    Mock mock;Flow flow;Ops ops{&mock,start,take,schedule,auth};
    assert(flow.configure(ops));assert(flow.start(1));flow.tick();
    assert(flow.state()==State::PasswordStart);flow.tick();assert(mock.starts==2);
    flow.tick();assert(flow.state()==State::Waiting && mock.work);
    flow.reset();assert(flow.state()==State::Waiting);
    mock.work(mock.work_context,mock.generation);flow.tick();assert(flow.state()==State::Accepted);
    lce_auth_result result{};assert(flow.take_result(&result) && result.game_uid==42);
    assert(flow.state()==State::Idle);
    assert(!flow.start_with_credentials(1,"bad name","password123"));
    assert(!flow.start_with_credentials(1,"Codey","short"));
    mock.expected_remember=true;
    assert(flow.start_with_credentials(1,"Codey","password123",true));
    assert(flow.state()==State::Waiting);mock.work(mock.work_context,mock.generation);
    flow.tick();assert(flow.take_result(&result) && result.game_uid==42);
    assert(!flow.start_with_token(1,"short"));
    char token[44];memset(token,'a',43);token[43]=0;mock.expected_token=true;mock.expected_remember=false;
    assert(flow.start_with_token(1,token));mock.work(mock.work_context,mock.generation);
    flow.tick();assert(flow.take_result(&result) && result.game_uid==42);
}
