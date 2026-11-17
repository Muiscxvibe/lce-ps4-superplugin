#include "external_players.hpp"
#include <cassert>
#include <cstdio>
static int seq,fail_at, destroyed,retired;static bool drained;
static void *make(){++seq;return seq==fail_at?nullptr:reinterpret_cast<void *>(static_cast<uintptr_t>(seq*16));}
static void *player(void *,bool local,int user,uint8_t id,const void *){assert(local?(user==1&&id==5):(user==-1&&id==0));return make();}
static void destroy(void *){++destroyed;}
static void *wrap(void *){return make();}
static void *socket_(void *){return make();}
static bool retire_(void *,void *){if(!drained)return false;++retired;return true;}
int main(){using namespace lce_external_players;Ops ops{player,destroy,wrap,destroy,socket_,retire_};int uid=0,manager=0;
 for(int failure=1;failure<=5;++failure){seq=destroyed=0;fail_at=failure;Players p;assert(!p.prepare(&manager,1,5,&uid,&uid,ops));
  assert(destroyed==0);
  if(failure==1) assert(!p.has_native_objects());
  else {assert(p.has_native_objects());assert(!p.retire());assert(destroyed==0);}
 }
 seq=destroyed=fail_at=retired=0;Players p;assert(p.prepare(&manager,1,5,&uid,&uid,ops));assert(p.local()&&p.host()&&p.socket());assert(!p.retire()&&destroyed==0);drained=true;assert(p.retire()&&retired==1&&destroyed==4);assert(p.retire()&&retired==1);puts("external player lifecycle tests passed");}
