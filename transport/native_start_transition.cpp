#include "native_start_transition.hpp"
#include <string.h>

namespace lce_native_start {
namespace { uintptr_t image; }
bool bind(uintptr_t base) {
    if (image || !base || base > UINTPTR_MAX - 0x143b148) return false;
    // The native STARTING branch jumps here with the global game manager.
    const unsigned char branch[] = {0x83,0xfa,0x05,0x75,0x68};
    const unsigned char jump[] = {0xe9,0x05,0x8a,0xb6,0xff};
    const unsigned char routine[] = {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x53,0x50};
    if (memcmp(reinterpret_cast<const void *>(base+0xdde9c2),branch,sizeof branch) ||
        memcmp(reinterpret_cast<const void *>(base+0xdde9e6),jump,sizeof jump) ||
        memcmp(reinterpret_cast<const void *>(base+0x9473f0),routine,sizeof routine)) return false;
    image=base;
    return true;
}
bool start_client(void *expected_manager) {
    if (!image || !expected_manager ||
        *reinterpret_cast<void **>(image+0x143b148)!=expected_manager) return false;
    reinterpret_cast<void (*)(void *)>(image+0x9473f0)(reinterpret_cast<void *>(image+0x143b050));
    return true;
}
}
