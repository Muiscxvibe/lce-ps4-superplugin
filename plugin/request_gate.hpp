#pragma once
#include <stdint.h>
#include <string.h>
#include "/home/codey/minecraft-lce-work/r72-session-candidate/loader/nbt_player_authority_unique_195/authority.h"
// Value-only admission primitive. Caller must obtain pad from the exact menu
// owner, maintain exclusive ownership of the request and native scope, and
// repeat this check immediately before any world read or mutation.
namespace local_nbt_gate {
struct Request {mc195_debug_pad_ticket pad;uint64_t world,deadline_us;mc195_nbt_player player;};
struct Ops {
 void *ctx;
 int (*authority_copy)(void*,const mc195_debug_pad_ticket*,uint64_t,mc195_nbt_player*);
 int (*scope_current)(void*);
 uint64_t (*clock)(void*);
};
inline bool uid_equal(const uint8_t*a,const uint8_t*b){
 if((a[17]&1u)&&(b[17]&1u)){if(!a[0]||!b[0])return false;for(unsigned i=0;i<17;++i){if(a[i]!=b[i])return false;if(!a[i])return true;}return false;}
 if((a[17]&1u)||(b[17]&1u))return false;
 unsigned x=0,y=0;for(unsigned i=18;i<28;++i){x|=a[i];y|=b[i];}return x&&y&&!memcmp(a+18,b+18,10);
}
inline bool identity_equal(const mc195_nbt_player&a,const mc195_nbt_player&b){return a.entity_id==b.entity_id&&(uid_equal(a.offline,b.offline)||uid_equal(a.offline,b.online)||uid_equal(a.online,b.offline)||uid_equal(a.online,b.online));}
inline bool admit(const Ops&o,const mc195_debug_pad_ticket&pad,uint64_t world,uint64_t deadline,Request*out){
 if(!out||!o.authority_copy||!o.clock||pad.slot<0||pad.slot>=4||pad.handle<0||!pad.generation||!world)return false;
 uint64_t t=o.clock(o.ctx);if(!t||deadline<=t||deadline-t>15000000)return false;
 mc195_nbt_player p{},again{};if(o.authority_copy(o.ctx,&pad,world,&p)!=1||o.authority_copy(o.ctx,&pad,world,&again)!=1||memcmp(&p,&again,sizeof p))return false;
 t=o.clock(o.ctx);if(!t||t>=deadline)return false;*out={pad,world,deadline,p};return true;
}
inline bool authorized(const Ops&o,const Request&r,uint64_t server_world,const mc195_nbt_player&server_player){
 if(!o.authority_copy||!o.clock||!o.scope_current||!r.world||r.world!=server_world||o.scope_current(o.ctx)!=1)return false;
 uint64_t t=o.clock(o.ctx);if(!t||t>=r.deadline_us)return false;mc195_nbt_player fresh{};
 if(o.authority_copy(o.ctx,&r.pad,r.world,&fresh)!=1||memcmp(&fresh,&r.player,sizeof fresh)||!identity_equal(fresh,server_player))return false;
 t=o.clock(o.ctx);return t&&t<r.deadline_us&&o.scope_current(o.ctx)==1;
}
}
