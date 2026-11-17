#include "external_players.hpp"
namespace lce_external_players {
bool Players::prepare(void *manager,int user,uint8_t id,const void *local_uid,const void *host_uid,const Ops &ops) {
 if(local_sqr_||host_sqr_||local_||host_||socket_||!manager||user<0||user>=4||id==0||id==0xff||!local_uid||!host_uid||
    !ops.create_player||!ops.destroy_player||!ops.wrap||!ops.destroy_wrapper||!ops.create_socket||!ops.retire_socket)return false;
 ops_=ops;
 local_sqr_=ops_.create_player(manager,true,user,id,local_uid);
 if(!local_sqr_)return false;
 host_sqr_=ops_.create_player(manager,false,-1,0,host_uid);
 if(!host_sqr_)return false;
 local_=ops_.wrap(local_sqr_);
 if(!local_)return false;
 host_=ops_.wrap(host_sqr_);
 if(!host_)return false;
 // Last operation: successful creation transfers socket teardown to retire().
 socket_=ops_.create_socket(local_);
 if(!socket_)return false;
 return true;
}
void Players::cleanup() {
 if(host_)ops_.destroy_wrapper(host_);
 if(local_)ops_.destroy_wrapper(local_);
 if(host_sqr_)ops_.destroy_player(host_sqr_);
 if(local_sqr_)ops_.destroy_player(local_sqr_);
 host_=local_=host_sqr_=local_sqr_=nullptr;
}
bool Players::retire() {
 if(!has_native_objects())return true;
 // Partial native construction may have side effects (the SQR local-player
 // ctor initializes voice state). There is no verified inverse for that path.
 // Retain partial objects rather than destroying/retrying them.
 if(!local_sqr_||!host_sqr_||!local_||!host_||!socket_)return false;
 if(socket_&&!ops_.retire_socket(local_,socket_))return false;
 socket_=nullptr;cleanup();return true;
}
}
