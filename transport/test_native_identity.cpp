#include "native_identity.hpp"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <sys/mman.h>

static bool called;
static void get_uid(void *,int index,void *uid,bool online) {
    assert(index==2 && !online);
    called=true;
    std::memset(uid,0x5a,28);
}
int main() {
    constexpr size_t size=0x1480000;
    auto *image=static_cast<unsigned char *>(mmap(nullptr,size,PROT_READ|PROT_WRITE|PROT_EXEC,
                                                  MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
    assert(image!=MAP_FAILED);
    const unsigned char load[]={0x48,0x8d,0x05,0x29,0xc3,0x68,0};
    const unsigned char call[]={0xff,0x90,0xa0,0,0,0};
    const unsigned char init[]={0xe8,0x51,0x80,0,0};
    std::memcpy(image+0xdf0658,load,sizeof load);
    std::memcpy(image+0xdf06ea,init,sizeof init);
    std::memcpy(image+0xdf0700,call,sizeof call);
    image[0xdf8740]=0xc3; // Mock native empty-UID constructor.
    uintptr_t vtable[21]={};
    vtable[20]=reinterpret_cast<uintptr_t>(&get_uid);
    uintptr_t *profile_vtable=vtable;
    void *profile=&profile_vtable;
    std::memcpy(image+0x147c988,&profile,sizeof profile);
    assert(lce_native_identity::bind(reinterpret_cast<uintptr_t>(image)));
    unsigned char uid[28]={};
    assert(!lce_native_identity::get_local(4,uid));
    assert(!called);
    assert(lce_native_identity::get_local(2,uid));
    assert(called && uid[0]==0x5a && uid[27]==0x5a);
    assert(munmap(image,size)==0);
}
