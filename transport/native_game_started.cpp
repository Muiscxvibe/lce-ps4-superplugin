#include "native_game_started.hpp"
#include <string.h>
namespace lce_native_game_started {
namespace { uintptr_t image; }
bool bind(uintptr_t base) {
    if (image || !base || base>UINTPTR_MAX-0x143ef58) return false;
    const unsigned char get[]={0x8a,0x87,0x74,0x02,0,0,0xc3};
    const unsigned char set[]={0x40,0x88,0xb7,0x74,0x02,0,0,0x40,0x80,0xf6,1};
    if (memcmp(reinterpret_cast<const void *>(base+0xabc9d0),get,sizeof get) ||
        memcmp(reinterpret_cast<const void *>(base+0xab87e0),set,sizeof set)) return false;
    image=base; return true;
}
bool read(bool *started) {
    if (!image || !started) return false;
    *started=reinterpret_cast<bool (*)(void *)>(image+0xabc9d0)(reinterpret_cast<void *>(image+0x143ef58));
    return true;
}
}
