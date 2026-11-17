#include "join_native_ops.hpp"
#include <string.h>
namespace lce_join_native {
namespace {
using Alloc=void *(*)(size_t);
using Free=void (*)(void *);
using Init=void (*)(void *);
using Reserve=void (*)(lce_join_list::Vector *,size_t);
uintptr_t image;
void *object(size_t n) { return reinterpret_cast<Alloc>(image+0xe6cfd0)(n); }
void *array(size_t n) { return reinterpret_cast<Alloc>(image+0xe6cfc0)(n); }
void release(void *p) { reinterpret_cast<Free>(image+0xe6cfb0)(p); }
void init(void *p) { reinterpret_cast<Init>(image+0xde18f0)(p); }
bool reserve(lce_join_list::Vector *v,size_t n) {
    if (!v || n!=1) return false;
    reinterpret_cast<Reserve>(image+0xde1d20)(v,n);
    return v->end && v->capacity && reinterpret_cast<uintptr_t>(v->end)<reinterpret_cast<uintptr_t>(v->capacity);
}
}
bool bind(uintptr_t base) {
    if (image || !base || base>UINTPTR_MAX-0xe6cfd0) return false;
    // Verify the actual call sites that establish allocator, array allocator,
    // constructor and vector-growth identities; the caller pins the image.
    const unsigned char alloc[]={0xe8,0xa3,0xc2,0x08,0x00};
    const unsigned char arr[]={0xe8,0x66,0xc2,0x08,0x00};
    const unsigned char ctor[]={0xe8,0xb4,0x0b,0x00,0x00};
    const unsigned char grow[]={0xe8,0xb0,0x0e,0x00,0x00};
    const unsigned char dealloc[]={0xe8,0x0a,0xc1,0x08,0x00};
    const unsigned char init_sig[]={0x55,0x48,0x89,0xe5,0x53,0x50,0x48,0x89,0xfb};
    if (memcmp(reinterpret_cast<const void *>(base+0xde0d28),alloc,sizeof alloc) ||
        memcmp(reinterpret_cast<const void *>(base+0xde0d55),arr,sizeof arr) ||
        memcmp(reinterpret_cast<const void *>(base+0xde0d37),ctor,sizeof ctor) ||
        memcmp(reinterpret_cast<const void *>(base+0xde0e6b),grow,sizeof grow) ||
        memcmp(reinterpret_cast<const void *>(base+0xde0ea1),dealloc,sizeof dealloc) ||
        memcmp(reinterpret_cast<const void *>(base+0xde18f0),init_sig,sizeof init_sig)) return false;
    image=base;return true;
}
lce_join_list::Ops ops() {
    if(!image)return {};
    return {object,array,release,init,reserve};
}
}
