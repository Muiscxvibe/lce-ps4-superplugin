#include "native_identity.hpp"
#include <string.h>
namespace lce_native_identity {
namespace { uintptr_t image; }
bool bind(uintptr_t base) {
 if(image||!base||base>UINTPTR_MAX-0x147c988)return false;
 const unsigned char global_load[]={0x48,0x8d,0x05,0x29,0xc3,0x68,0};
 const unsigned char virtual_call[]={0xff,0x90,0xa0,0,0,0};
 const unsigned char uid_init[]={0xe8,0x51,0x80,0,0};
 if(memcmp(reinterpret_cast<const void *>(base+0xdf0658),global_load,sizeof global_load)||
    memcmp(reinterpret_cast<const void *>(base+0xdf06ea),uid_init,sizeof uid_init)||
    memcmp(reinterpret_cast<const void *>(base+0xdf0700),virtual_call,sizeof virtual_call))return false;
 image=base;return true;
}
bool init_empty(void *uid) {
 if(!image||!uid)return false;
 reinterpret_cast<void (*)(void *)>(image+0xdf8740)(uid);
 return true;
}
bool get_local(int user_index,void *uid) {
 if(!image||!uid||user_index<0||user_index>=4)return false;
 void *profile=*reinterpret_cast<void **>(image+0x147c988);
 if(!profile)return false;
 auto table=*reinterpret_cast<uintptr_t **>(profile);
 if(!table||!table[20])return false;
 if(!init_empty(uid))return false;
 reinterpret_cast<void (*)(void *,int,void *,bool)>(table[20])(profile,user_index,uid,false);
 return true;
}
}
