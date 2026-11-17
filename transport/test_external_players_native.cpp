#include "external_players_native.hpp"
#include <cassert>
#include <cstring>
#include <cstdint>
static int made,freed; static bool identity_ok=true;
namespace lce_sqr_native { bool bind(uintptr_t){return true;} void *create(void *,bool local,int user,uint8_t id,const void *uid){assert(local?(user==1&&id==5&&*(const char*)uid==42):(user==-1&&id==0&&*(const char*)uid==0)); return reinterpret_cast<void*>(uintptr_t(++made*16));} void destroy(void*){++freed;} }
namespace lce_player_native { bool bind(uintptr_t){return true;} void *wrap(void*){return reinterpret_cast<void*>(uintptr_t(++made*16));} void destroy(void*){++freed;} }
namespace lce_socket_native { bool bind(uintptr_t){return true;} void *create_client(void*){return reinterpret_cast<void*>(uintptr_t(++made*16));} }
namespace lce_native_identity { bool bind(uintptr_t){return true;} bool get_local(int u,void *p){if(!identity_ok||u!=1)return false;memset(p,42,28);return true;} bool init_empty(void *p){memset(p,0,28);return true;} }
static bool retire(void*,void*){return true;}
int main(){using namespace lce_external_players;Players p;int manager;assert(!prepare_native(p,&manager,1,5,retire));assert(bind_native(1));assert(!prepare_native(p,&manager,1,0,retire));identity_ok=false;assert(!prepare_native(p,&manager,1,5,retire)&&made==0);identity_ok=true;assert(prepare_native(p,&manager,1,5,retire)&&made==5);assert(p.retire()&&freed==4);}
