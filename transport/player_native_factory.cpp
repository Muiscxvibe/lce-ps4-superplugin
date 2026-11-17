#include "player_native_factory.hpp"
#include <string.h>
namespace lce_player_native {
namespace { uintptr_t image; }
bool bind(uintptr_t base) {
    if(image||!base||base>UINTPTR_MAX-0x134aaa8)return false;
    // NetworkPlayerSony factory: allocates 0x20 bytes, then sets the
    // verified vtable, underlying SQR pointer, socket and timestamp.
    const unsigned char entry[]={0x55,0x48,0x89,0xe5,0x53,0x50,0x48,0x89,0xfb,0xbf,0x20,0,0,0};
    const unsigned char deleting_dtor[]={0xe9,0xfb,0xfe,0x06,0x00};
    const unsigned char body[]={0x48,0x8d,0x0d,0x5e,0xdc,0x54,0x00};
    if(memcmp(reinterpret_cast<const void *>(base+0xdfce20),entry,sizeof entry)||
       memcmp(reinterpret_cast<const void *>(base+0xdfce33),body,sizeof body)||
       memcmp(reinterpret_cast<const void *>(base+0xdfd0b0),deleting_dtor,sizeof deleting_dtor))return false;
    image=base;return true;
}
void *wrap(void *sqr_player) {
    if(!image||!sqr_player)return nullptr;
    return reinterpret_cast<void *(*)(void *)>(image+0xdfce20)(sqr_player);
}
void destroy(void *wrapper) {
    if(image&&wrapper)reinterpret_cast<void (*)(void *)>(image+0xdfd0b0)(wrapper);
}
}
