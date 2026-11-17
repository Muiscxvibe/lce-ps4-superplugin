#include "platform_hook_install.hpp"
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
static constexpr uintptr_t entries[] = {
0xddf8e0,0xddf980,0xddf990,0xddf9a0,0xddfa60,0xddfca0,0xddfcc0,0xddfcd0,0xddfce0,0xddfcf0,0xde1280,0xde0fd0,0xde1000,0xde1030,0xde1080,0xddfcb0,0xde10b0,0xde10e0,0xde1110,0xddfe80,0xde1190,0xde11a0,0xde11c0,0xddfe20,0xddfe40,0xddfe50,0xddfe30,0xddfe60,0xddfe70,0xddfff0,0xde0450,0xde04e0,0xde05c0,0xde05d0,0xdde8b0,0xde0600,0xdde8c0,0xdde8d0,0xdde8e0,0xde0610,0xde0630,0xde0660,0xde0670,0xddffe0,0xde0100,0xde0430,0xde0710,0xde0830,0xde0890,0xde0a30,0xde0af0,0xde0b70,0xde0bf0,0xde0c30,0xde1140,0xde1150,0xde1160,0xde1180,0xde0c70,0xde0f00,0xde0f10,0xde0f20,0xde0f50,0xde0f90,0xde12a0,0xde1270,0xde11d0,0xde11e0,0xde1210,0xde1200,0xde1230,0xde1220,0xde11f0,0xde1240,0xde1250,0xde1260,0xde1580,0xde1590,0xde1290,0xde1360,0xde1370,0xde1380,0xde1390,0xddea50,0xddeaf0,0xddeec0,0xdde8f0,0xddf2d0,0xddf880,0xddf8a0,0xddf8c0,0xde15a0,0xde1670};

static bool unavailable(){return false;}
static int join(const lce_servers::Server &,int,int){return 1;}
static void info(const lce_servers::Server &,void *,lce_join_adapter::Complete,void *){}
int main() {
 const size_t size=0x143c000; void *memory=std::calloc(1,size); assert(memory);
 auto base=reinterpret_cast<uintptr_t>(memory);
 auto *header=reinterpret_cast<uintptr_t *>(base+0x1349da8);
 header[1]=base+0x134a120;
 for(size_t i=0;i<lce_join_hook::Vtable::slots;++i) header[i+2]=base+entries[i];
 const uintptr_t *manager_vptr=header+2; void *manager=&manager_vptr;
 *reinterpret_cast<void **>(base+0x143b148)=manager;
 lce_servers::List servers{}; lce_join_list::Ops rows{}; lce_session_queries::Custom custom{};
 lce_platform_hook::Install install(base,manager,servers,rows,unavailable,join,info,nullptr,nullptr,custom);
 *reinterpret_cast<void **>(base+0x143b148)=nullptr;
 assert(!install.publish() && manager_vptr==header+2);
 *reinterpret_cast<void **>(base+0x143b148)=manager;
 ++header[7];
 assert(!install.publish() && manager_vptr==header+2);
 --header[7];
 assert(install.publish()); assert(manager_vptr != header+2);
 assert(manager_vptr[30]!=header[32] && manager_vptr[58]!=header[60]);
 assert(manager_vptr[5]!=header[7] && manager_vptr[0]==header[2]);
 assert(!install.publish());
 // Process-lifetime install: no teardown or native calls in this fixture.
 return 0;
}
