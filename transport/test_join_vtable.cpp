#include "join_vtable.hpp"
#include <cassert>
#include <cstdint>
#include <cstdio>
// The fixture comes from the executable audit output, not production internals.
static constexpr uintptr_t entries[] = {
0xddf8e0,0xddf980,0xddf990,0xddf9a0,0xddfa60,0xddfca0,0xddfcc0,0xddfcd0,0xddfce0,0xddfcf0,0xde1280,0xde0fd0,0xde1000,0xde1030,0xde1080,0xddfcb0,0xde10b0,0xde10e0,0xde1110,0xddfe80,0xde1190,0xde11a0,0xde11c0,0xddfe20,0xddfe40,0xddfe50,0xddfe30,0xddfe60,0xddfe70,0xddfff0,0xde0450,0xde04e0,0xde05c0,0xde05d0,0xdde8b0,0xde0600,0xdde8c0,0xdde8d0,0xdde8e0,0xde0610,0xde0630,0xde0660,0xde0670,0xddffe0,0xde0100,0xde0430,0xde0710,0xde0830,0xde0890,0xde0a30,0xde0af0,0xde0b70,0xde0bf0,0xde0c30,0xde1140,0xde1150,0xde1160,0xde1180,0xde0c70,0xde0f00,0xde0f10,0xde0f20,0xde0f50,0xde0f90,0xde12a0,0xde1270,0xde11d0,0xde11e0,0xde1210,0xde1200,0xde1230,0xde1220,0xde11f0,0xde1240,0xde1250,0xde1260,0xde1580,0xde1590,0xde1290,0xde1360,0xde1370,0xde1380,0xde1390,0xddea50,0xddeaf0,0xddeec0,0xdde8f0,0xddf2d0,0xddf880,0xddf8a0,0xddf8c0,0xde15a0,0xde1670};
int main() {
 using namespace lce_join_hook; constexpr uintptr_t base = 0x10000000;
 static_assert(sizeof(entries)/sizeof(*entries)==Vtable::slots);
 uintptr_t original[Vtable::slots+2]={}; original[1]=base+0x134a120;
 for(size_t i=0;i<Vtable::slots;++i)original[i+2]=base+entries[i];
 Vtable rejected; original[22]++; assert(!rejected.prepare(base,original,{1,2,3})); original[22]--;
 Vtable table; assert(table.prepare(base,original,{1,2,3})); auto p=table.address_point();
 for(size_t i=0;i<Vtable::slots;++i) assert(p[i]==(i==30?1:i==58?2:i==61?3:base+entries[i]));
 assert(p[-2]==0 && p[-1]==base+0x134a120 && !table.prepare(base,original,{1,2,3}));
 // Retail ConnectingProgress calls CGameNetworkManager::IsInSession, whose
 // thunk at 0x944d20 dispatches through vtable byte offset 0xa0 (slot 20).
 Replacements queries{};queries.join=1;queries.list=2;queries.full_info=3;
 queries.player_count=104;queries.local_player=105;queries.player_by_small_id=106;
 queries.host_player=107;queries.is_host=108;
 queries.in_session=101;queries.in_gameplay=102;queries.ready_to_play_or_idle=103;
 Vtable query_table;assert(query_table.prepare(base,original,queries));auto q=query_table.address_point();
 assert(q[19]==base+0xddfe80 && q[20]==101 && q[21]==102 && q[22]==103);
 assert(q[5]==104 && q[10]==base+0xde1280 && q[11]==105);
 assert(q[13]==base+0xde1030 && q[14]==106 && q[15]==base+0xddfcb0);
 assert(q[16]==107 && q[17]==108 && q[18]==base+0xde1110);
 puts("join vtable tests passed");
}
