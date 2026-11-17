#include "join_native_ops.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
int main(){const size_t n=0xe6d000;auto *p=static_cast<unsigned char *>(mmap(nullptr,n,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));assert(p!=MAP_FAILED);
 const unsigned char a[]={0xe8,0xa3,0xc2,0x08,0},b[]={0xe8,0x66,0xc2,0x08,0},c[]={0xe8,0xb4,0x0b,0,0},d[]={0xe8,0xb0,0x0e,0,0},e[]={0xe8,0x0a,0xc1,0x08,0},f[]={0x55,0x48,0x89,0xe5,0x53,0x50,0x48,0x89,0xfb};
 memcpy(p+0xde0d28,a,sizeof a);memcpy(p+0xde0d55,b,sizeof b);memcpy(p+0xde0d37,c,sizeof c);memcpy(p+0xde0e6b,d,sizeof d);memcpy(p+0xde0ea1,e,sizeof e);memcpy(p+0xde18f0,f,sizeof f);
 auto base=reinterpret_cast<uintptr_t>(p);p[0xde0ea1]^=1;assert(!lce_join_native::bind(base));p[0xde0ea1]^=1;assert(lce_join_native::bind(base));auto ops=lce_join_native::ops();assert(ops.alloc_object&&ops.alloc_array&&ops.dealloc&&ops.init_session_data&&ops.reserve_extra);assert(!lce_join_native::bind(base));
 munmap(p,n);puts("native allocator binding checks passed");}
