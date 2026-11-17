#include "sqr_native_factory.hpp"
#include <stddef.h>
#include <string.h>
namespace lce_sqr_native {
namespace { uintptr_t image; }
bool bind(uintptr_t base) {
 if(image||!base||base>UINTPTR_MAX-0xe6cfd0)return false;
 const unsigned char ctor[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53};
 const unsigned char dtor[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x53,0x50};
 const unsigned char getuser[]={0x8b,0x47,0x10,0xc3};
 const unsigned char allocid[]={0x40,0x88,0x77,0x18};
 if(memcmp(reinterpret_cast<const void *>(base+0xde3930),ctor,sizeof ctor)||
    memcmp(reinterpret_cast<const void *>(base+0xde3a80),dtor,sizeof dtor)||
    memcmp(reinterpret_cast<const void *>(base+0xde38f0),getuser,sizeof getuser)||
    memcmp(reinterpret_cast<const void *>(base+0xde3bf0),allocid,sizeof allocid))return false;
 image=base;return true;
}
void *create(void *manager,bool local,int user_index,uint8_t small_id,const void *uid) {
 if(!image||!manager||!uid||small_id==0xff||(local&&(user_index<0||user_index>=4)))return nullptr;
 using Alloc=void *(*)(size_t);
 using Ctor=void (*)(void *,void *,int,bool,uint16_t,int,int,const void *);
 void *p=reinterpret_cast<Alloc>(image+0xe6cfd0)(0xc8);
 if(!p)return nullptr;
 // type=LOCAL(1) or HOST(0), not on the hosting machine. No RUDP
 // context: external transport supplies the connection. The RUDP context is
 // the stack argument; r9 carries user index as verified by GetUserIndex.
 reinterpret_cast<Ctor>(image+0xde3930)(p,manager,local?1:0,false,0,
                                      local?user_index:-1,-1,uid);
 reinterpret_cast<void (*)(void *,uint8_t)>(image+0xde3bf0)(p,small_id);
 return p;
}
void destroy(void *player) {
 if(!image||!player)return;
 reinterpret_cast<void (*)(void *)>(image+0xde3a80)(player);
 reinterpret_cast<void (*)(void *)>(image+0xe6cfb0)(player);
}
}
