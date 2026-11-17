#include "account_join_route.hpp"
#include <cassert>
#include <cstring>
using namespace lce_account_flow;
struct Test {int starts=0,joined=0,reports=0;bool token=false;void (*worker)(void *,uint64_t)=nullptr;void *arg=nullptr;uint64_t gen=0;};
static bool keyboard(void *p,int pad,bool password,const uint16_t *) {auto *t=static_cast<Test *>(p);assert(pad==0 && password==(t->starts==1));++t->starts;return true;}
static Input take(void *p,uint16_t *out,size_t n) {auto *t=static_cast<Test *>(p);assert(n>=12);const char16_t *s=t->starts==1?u"Codey":u"password123";while ((*out++=*s++)){}return Input::Accepted;}
static bool schedule(void *p,void (*fn)(void *,uint64_t),void *arg,uint64_t gen) {auto *t=static_cast<Test *>(p);t->worker=fn;t->arg=arg;t->gen=gen;return true;}
static bool auth(void *p,const char *,const char *secret,bool token,bool remember,lce_auth_result *r) {auto *t=static_cast<Test *>(p);assert(token==t->token && !remember);if(token)assert(strlen(secret)==43);r->accepted=1;r->game_uid=1;memset(r->ticket_id,'a',32);memset(r->ticket_key,'b',64);return true;}
static bool ready(void *) {return true;}
static bool join(void *p,const lce_servers::Server &s,int pad,const lce_auth_result &r) {auto *t=static_cast<Test *>(p);assert(!strcmp(s.host,"192.168.1.165")&&pad==0&&r.accepted);++t->joined;return true;}
static void report(void *p,bool ok) {auto *t=static_cast<Test *>(p);assert(ok);++t->reports;}
int main() {
    Test test;Flow flow;Ops ops{&test,keyboard,take,schedule,auth};assert(flow.configure(ops));
    lce_servers::List servers{};servers.count=1;strcpy(servers.entries[0].host,"192.168.1.165");servers.entries[0].port=25565;
    lce_account_join::Ops route_ops{&test,ready,join,report};lce_account_join::Route route(flow,servers,route_ops);
    unsigned char record[32]={};assert(route.select(record,1)==lce_account_join::Selection::Native);
    memcpy(record,"LCEPCSERVER\1",12);record[12]=1;
    assert(route.select(record,1)==lce_account_join::Selection::Rejected);
    record[12]=0;assert(route.select(record,1)==lce_account_join::Selection::Rejected);
    assert(route.select(record,0)==lce_account_join::Selection::Started);
    assert(route.select(record,0)==lce_account_join::Selection::Rejected);
    route.tick();route.tick();route.tick();assert(test.worker && !test.joined);
    test.worker(test.arg,test.gen);route.tick();assert(test.joined==1 && test.reports==1);
    char token[44];memset(token,'a',43);token[43]=0;test.token=true;
    assert(route.select_token(record,0,token)==lce_account_join::Selection::Started);
    test.worker(test.arg,test.gen);route.tick();assert(test.joined==2 && test.reports==2);
}
