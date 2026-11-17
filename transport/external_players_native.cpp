#include "external_players_native.hpp"
#include "sqr_native_factory.hpp"
#include "player_native_factory.hpp"
#include "socket_native_factory.hpp"
#include "native_identity.hpp"
namespace lce_external_players {
namespace { bool ready; }
bool bind_native(uintptr_t base) {
 if(ready)return false;
 // The factories are process-lifetime bindings; a partial failure is not
 // retried because their validated image bindings cannot be replaced.
 if(!lce_sqr_native::bind(base)||!lce_player_native::bind(base)||!lce_socket_native::bind(base)||!lce_native_identity::bind(base))return false;
 ready=true;return true;
}
Ops native_ops(bool (*retire_socket)(void *,void *)) {
 if(!ready||!retire_socket)return {};
 return {lce_sqr_native::create,lce_sqr_native::destroy,lce_player_native::wrap,
         lce_player_native::destroy,lce_socket_native::create_client,retire_socket};
}
bool prepare_native(Players &players,void *manager,int user,uint8_t id,
                    bool (*retire_socket)(void *,void *)) {
 if(!ready||!manager||!retire_socket||id==0||id==0xff)return false;
 alignas(8) unsigned char local_uid[28],host_uid[28];
 if(!lce_native_identity::get_local(user,local_uid)||
    !lce_native_identity::init_empty(host_uid))return false;
 return players.prepare(manager,user,id,local_uid,host_uid,native_ops(retire_socket));
}
}
