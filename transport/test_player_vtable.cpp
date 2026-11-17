#include "player_vtable.hpp"
#include <cassert>
#include <cstdio>
static constexpr uintptr_t slots[30]={0xdfd0a0,0xdfd0b0,0xdfce60,0xdfce70,0xdfce80,0xdfce90,0xdfd0c0,0xdfd0d0,0xdfcea0,0xdfceb0,0xdfcec0,0xdfced0,0xdfcee0,0xdfcef0,0xdfcf00,0xdfcf10,0xdfcf20,0xdfcf30,0xdfcf40,0xdfcf50,0xdfcf60,0xdfcf70,0xdfcf80,0xdfcf90,0xdfcff0,0xdfd010,0xdfd040,0xdfd060,0xdfd090,0xdfd0e0};
int main(){uintptr_t base=0x10000000,header[32]={0,base+0x134aba0};for(int i=0;i<30;++i)header[i+2]=base+slots[i];lce_player_hook::Vtable bad;header[9]++;assert(!bad.prepare(base,header,42));header[9]--;lce_player_hook::Vtable v;assert(v.prepare(base,header,42));for(int i=0;i<30;++i)assert(v.address_point()[i]==(i==3?42:base+slots[i]));puts("player vtable tests passed");}
