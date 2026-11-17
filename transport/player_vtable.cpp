#include "player_vtable.hpp"
#include <stddef.h>
namespace lce_player_hook {
namespace {
constexpr uintptr_t expected[30]={
0xdfd0a0,0xdfd0b0,0xdfce60,0xdfce70,0xdfce80,0xdfce90,0xdfd0c0,0xdfd0d0,
0xdfcea0,0xdfceb0,0xdfcec0,0xdfced0,0xdfcee0,0xdfcef0,0xdfcf00,0xdfcf10,
0xdfcf20,0xdfcf30,0xdfcf40,0xdfcf50,0xdfcf60,0xdfcf70,0xdfcf80,0xdfcf90,
0xdfcff0,0xdfd010,0xdfd040,0xdfd060,0xdfd090,0xdfd0e0};
}
bool Vtable::prepare(uintptr_t base,const uintptr_t *header,uintptr_t send_hook) {
 if(ready_||!base||base>UINTPTR_MAX-0x134aba0||!header||!send_hook)return false;
 uintptr_t snapshot[32];for(size_t i=0;i<32;++i)snapshot[i]=header[i];
 if(snapshot[0]||snapshot[1]!=base+0x134aba0)return false;
 for(size_t i=0;i<30;++i)if(snapshot[i+2]!=base+expected[i])return false;
 for(size_t i=0;i<32;++i)words_[i]=snapshot[i];
 words_[5]=send_hook;ready_=true;return true;
}
}
