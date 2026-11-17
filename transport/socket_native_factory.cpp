#include "socket_native_factory.hpp"
#include <string.h>
namespace lce_socket_native {
namespace {
uintptr_t image;
QuiescenceGuard quiescence_guard;
}
bool bind(uintptr_t base) {
 if(image||!base||base>UINTPTR_MAX-0x134a798)return false;
 const unsigned char ctor[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x50};
 const unsigned char allocation[]={0xbf,0xb0,0,0,0};
 const unsigned char ctor_call[]={0xe8,0x2a,0x39,0xcf,0xff};
 const unsigned char setter[]={0x48,0x89,0x77,0x10,0xc3};
 const unsigned char push_sig[]={0x55,0x48,0x89,0xe5};
 const unsigned char close_sig[]={0x55,0x48,0x89,0xe5,0x53,0x50,0x48,0x89,0xfb,0x80,0x7b,0x01,0x00,0x74,0x19};
 const unsigned char socket_dtor[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x53,0x50};
 const unsigned char event_wait[]={0x55,0x48,0x89,0xe5,0x53,0x48,0x83,0xec,0x18};
 const unsigned char event_delete[]={0x55,0x48,0x89,0xe5,0x53,0x50,0x48,0x8d,0x05,0xeb,0x38,0x55,0x00};
 if(memcmp(reinterpret_cast<const void *>(base+0x63b170),ctor,sizeof ctor)||
    memcmp(reinterpret_cast<const void *>(base+0x9477fb),allocation,sizeof allocation)||
    memcmp(reinterpret_cast<const void *>(base+0x947841),ctor_call,sizeof ctor_call)||
    memcmp(reinterpret_cast<const void *>(base+0xdfcf60),setter,sizeof setter)||
    memcmp(reinterpret_cast<const void *>(base+0x63b3b0),push_sig,sizeof push_sig)||
    memcmp(reinterpret_cast<const void *>(base+0x63b5c0),close_sig,sizeof close_sig)||
    memcmp(reinterpret_cast<const void *>(base+0xde1a30),socket_dtor,sizeof socket_dtor)||
    memcmp(reinterpret_cast<const void *>(base+0xdf6ec0),event_wait,sizeof event_wait)||
    memcmp(reinterpret_cast<const void *>(base+0xdf6e70),event_delete,sizeof event_delete)||
    *reinterpret_cast<const uintptr_t *>(base+0x134a778)!=base+0xdf6e50||
    *reinterpret_cast<const uintptr_t *>(base+0x134a798)!=base+0xdf6ec0)return false;
 image=base;return true;
}
void *create_client(void *player) {
 if(!image||!player)return nullptr;
 void *socket=reinterpret_cast<void *(*)(size_t)>(image+0xe6cfd0)(0xb0);
 if(!socket)return nullptr;
 // Native Socket(INetworkPlayer*, response=false, hostLocal=false).
 reinterpret_cast<void (*)(void *,void *,bool,bool)>(image+0x63b170)(socket,player,false,false);
 reinterpret_cast<void (*)(void *,void *)>(image+0xdfcf60)(player,socket);
 return socket;
}
void push(void *socket,const uint8_t *data,uint32_t size,int from_host) {
 if(!image||!socket||!data||!size)return;
 reinterpret_cast<void (*)(void *,const uint8_t *,uint32_t,bool)>(image+0x63b3b0)(socket,data,size,from_host!=0);
}
bool set_quiescence_guard(QuiescenceGuard guard) {
 if(!image||!guard||quiescence_guard)return false;
 quiescence_guard=guard;
 return true;
}
bool retire_client(void *wrapper,void *socket) {
 if(!image||!quiescence_guard||!wrapper||!socket||
    *reinterpret_cast<void **>(static_cast<unsigned char *>(wrapper)+0x10)!=socket||
    !quiescence_guard(wrapper,socket))return false;

 // A non-host-local client socket closes completely on the client end.
 using Close=bool (*)(void *,bool);
 if(!reinterpret_cast<Close>(image+0x63b5c0)(socket,false))return false;
 void *event=*reinterpret_cast<void **>(static_cast<unsigned char *>(socket)+0xa0);
 if(!event)return false;
 using Wait=uint32_t (*)(void *,int);
 if(reinterpret_cast<Wait>(image+0xdf6ec0)(event,0)!=0)return false;
 if(!quiescence_guard(wrapper,socket))return false;

 // Match the stock Sony path: clear the player's socket before destruction.
 reinterpret_cast<void (*)(void *,void *)>(image+0xdfcf60)(wrapper,nullptr);
 reinterpret_cast<void (*)(void *)>(image+0xde1a30)(socket);
 reinterpret_cast<void (*)(void *)>(image+0xe6cfb0)(socket);
 return true;
}
bool close_client_end(void *socket) {
 if(!image||!socket)return false;
 using Close=bool (*)(void *,bool);
 return reinterpret_cast<Close>(image+0x63b5c0)(socket,false);
}
}
