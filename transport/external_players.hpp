#pragma once
#include <stdint.h>
// One local player and one server-side host. All methods are owner-thread only.
// The game connection must stop using the socket before retire() can succeed.
namespace lce_external_players {
struct Ops {
 void *(*create_player)(void *manager,bool local,int user,uint8_t id,const void *uid);
 void (*destroy_player)(void *);
 void *(*wrap)(void *);
 void (*destroy_wrapper)(void *);
 void *(*create_socket)(void *local_wrapper);
 // Must close and free the socket only after game/bridge threads have drained.
 // Returns false without freeing if it is still in use.
 bool (*retire_socket)(void *local_wrapper,void *socket);
};
class Players {
public:
 Players()=default; Players(const Players&)=delete; Players& operator=(const Players&)=delete;
 bool prepare(void *manager,int user,uint8_t local_id,const void *local_uid,
              const void *host_uid,const Ops &);
 bool retire();
 // Forget native objects whose teardown cannot be verified (deliberate leak;
 // used only after the game has left the session).
 void abandon(){local_sqr_=host_sqr_=local_=host_=socket_=nullptr;}
 bool has_native_objects() const {
  return local_sqr_||host_sqr_||local_||host_||socket_;
 }
 void *local() const {return local_;}
 void *host() const {return host_;}
 void *socket() const {return socket_;}
private:
 void cleanup();
 Ops ops_{};
 void *local_sqr_=nullptr,*host_sqr_=nullptr,*local_=nullptr,*host_=nullptr,*socket_=nullptr;
};
}
