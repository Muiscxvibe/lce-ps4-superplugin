#include "native_keyboard.hpp"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <sys/mman.h>

static int (*callback)(void *,bool);
static void *callback_context;
static int requested_mode;
static int request(void *input,const uint16_t *,const uint16_t *,int pad,unsigned max,
                   int (*cb)(void *,bool),void *context,int mode) {
    assert(pad==1 && max==256);
    callback=cb;callback_context=context;requested_mode=mode;
    *reinterpret_cast<int *>(static_cast<uint8_t *>(input)+0x10e8)=1;
    return 0;
}
static void get_text(void *input,uint16_t *out,int max) {
    assert(max==256);
    auto *text=reinterpret_cast<uint16_t *>(static_cast<uint8_t *>(input)+0x1120);
    for (int i=0;i<=max;++i) {out[i]=text[i];if (!text[i]) break;}
}
static void jump_to(unsigned char *p,uintptr_t target) {
    p[0]=0x48;p[1]=0xb8;std::memcpy(p+2,&target,8);p[10]=0xff;p[11]=0xe0;
}
int main() {
    constexpr size_t size=0x1480000;
    auto *image=static_cast<unsigned char *>(mmap(nullptr,size,PROT_READ|PROT_WRITE|PROT_EXEC,
                                                  MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
    assert(image!=MAP_FAILED);
    const unsigned char req[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x50,0x48,0x89,0xfb};
    const unsigned char get[]={0x8d,0x42,0x01,0x4c,0x8d,0x87,0x20,0x11,0,0};
    std::memcpy(image+0xdba4b0,req,sizeof req);std::memcpy(image+0xdba730,get,sizeof get);
    auto *table=reinterpret_cast<uintptr_t *>(image+0x1349080);
    table[0x140/8]=reinterpret_cast<uintptr_t>(image+0xdba4b0);
    table[0x150/8]=reinterpret_cast<uintptr_t>(image+0xdba730);
    alignas(8) unsigned char input[0x1400]={};
    *reinterpret_cast<uintptr_t **>(input)=table;
    *reinterpret_cast<void **>(image+0x147c978)=input;
    lce_native_keyboard::Keyboard keyboard;
    assert(keyboard.bind(reinterpret_cast<uintptr_t>(image)));
    jump_to(image+0xdba4b0,reinterpret_cast<uintptr_t>(&request));
    jump_to(image+0xdba730,reinterpret_cast<uintptr_t>(&get_text));
    const uint16_t title[]={u'P',0};uint16_t output[257]={};
    assert(keyboard.start(1,true,title) && requested_mode==2);
    assert(!keyboard.start(1,true,title));
    auto *text=reinterpret_cast<uint16_t *>(input+0x1120);
    text[0]=u's';text[1]=u'e';text[2]=0;
    assert(callback(callback_context,true)==0);
    assert(text[0]==0 && text[1]==0);
    assert(keyboard.take(output,2)==lce_native_keyboard::Keyboard::Result::Pending);
    assert(keyboard.take(output,257)==lce_native_keyboard::Keyboard::Result::Accepted);
    assert(output[0]==u's' && output[1]==u'e' && output[2]==0);
    assert(!keyboard.start(1,false,title)); // native completion has not yet drained
    *reinterpret_cast<int *>(input+0x10e8)=0;
    assert(keyboard.start(1,false,title) && requested_mode==3);
    assert(callback(callback_context,false)==0);
    assert(keyboard.take(output,257)==lce_native_keyboard::Keyboard::Result::Cancelled);
    *reinterpret_cast<int *>(input+0x10e8)=0;
    assert(keyboard.start(1,true,title));
    text[0]=u'x';
    assert(keyboard.take(output,257)==lce_native_keyboard::Keyboard::Result::Pending);
    // Native IME initialization failure resets state without a callback.
    *reinterpret_cast<int *>(input+0x10e8)=0;
    assert(keyboard.take(output,257)==lce_native_keyboard::Keyboard::Result::Cancelled);
    assert(text[0]==0);
    assert(keyboard.start(1,false,title));
    assert(munmap(image,size)==0);
}
