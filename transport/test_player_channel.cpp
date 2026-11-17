#include "player_channel.hpp"
#include <cassert>
#include <cstdlib>
#include <cstdint>
#include <cstring>
static constexpr uintptr_t slots[30]={0xdfd0a0,0xdfd0b0,0xdfce60,0xdfce70,0xdfce80,0xdfce90,0xdfd0c0,0xdfd0d0,0xdfcea0,0xdfceb0,0xdfcec0,0xdfced0,0xdfcee0,0xdfcef0,0xdfcf00,0xdfcf10,0xdfcf20,0xdfcf30,0xdfcf40,0xdfcf50,0xdfcf60,0xdfcf70,0xdfcf80,0xdfcf90,0xdfcff0,0xdfd010,0xdfd040,0xdfd060,0xdfd090,0xdfd0e0};
struct Wrapper { const uintptr_t *vptr; };
static Wrapper wrappers[2]; static int wrapped, closed, received, sent;
static void *make_player(void *,bool local,int,uint8_t,const void*){return &wrappers[local?0:1];}
static void destroy(void*){}
static void *wrap(void *p){++wrapped;return p;}
static void *socket_(void *p){return p;}
static bool retire(void*,void*){return true;}
static void lock(void*){} static void unlock(void*){}
static void push(void *s,const uint8_t *p,uint32_t n,int from_host){
 assert(s==&wrappers[0] && n==2 && p[0]==9 && p[1]==10 && from_host==1);++received;
}
static void failure(void*){assert(false);}
static void close_(void*,int fd){assert(fd==5);++closed;}
static int recv_(void*,int fd,uint8_t *p,size_t n){
 assert(fd==5);if(received)return 0;const uint8_t frame[]={0,0,0,2,9,10};
 assert(n>=sizeof frame);std::memcpy(p,frame,sizeof frame);return sizeof frame;
}
static int send_(void*,int fd,const uint8_t *p,size_t n){
 const uint8_t frame[]={0,0,0,2,7,8};assert(fd==5&&n==sizeof frame&&!std::memcmp(p,frame,n));
 ++sent;return static_cast<int>(n);
}
int main(){
 void *memory=std::calloc(1,0x134b000);assert(memory); uintptr_t base=reinterpret_cast<uintptr_t>(memory);
 auto *header=reinterpret_cast<uintptr_t*>(base+0x134aa98);header[1]=base+0x134aba0;
 for(unsigned i=0;i<30;++i)header[2+i]=base+slots[i];
 wrappers[0].vptr=header+2; wrappers[1].vptr=header+2;
 lce_external_players::Ops ops{make_player,destroy,wrap,destroy,socket_,retire};
 lce_external_players::Players players;int manager,uid;
 assert(players.prepare(&manager,1,4,&uid,&uid,ops)&&wrapped==2);
 uint8_t incoming[64],outgoing[64];lce_session_transport session{};
 session.fd=5;session.state=LCE_SESSION_ACTIVE;session.io.close=close_;session.io.recv=recv_;session.io.send=send_;
 lce_connection_init(&session.connection,incoming,sizeof incoming,outgoing,sizeof outgoing);
 lce_native_bridge_ops bridge_ops{nullptr,lock,unlock,push};lce_player_channel::Channel channel;
 assert(!channel.start(base,players,session,bridge_ops,failure,nullptr));
 session.connection.reader.has_id=1;session.connection.reader.assigned_id=4;
 assert(channel.start(base,players,session,bridge_ops,failure,nullptr));
 assert(wrappers[0].vptr!=header+2 && wrappers[1].vptr==header+2);
 auto send=reinterpret_cast<lce_player_send_fn>(wrappers[0].vptr[3]); const uint8_t payload[]={7,8};
 send(players.local(),players.host(),payload,2,false,false);
 assert(session.connection.outgoing.end-session.connection.outgoing.begin==6);
 assert(lce_native_bridge_tick(channel.bridge(),1)==LCE_SESSION_ACTIVE && received==1 && sent==1);
 assert(channel.stop() && wrappers[0].vptr==header+2 && closed==1);
 assert(players.retire());std::free(memory);
}
