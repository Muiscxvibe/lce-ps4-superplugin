// Local-world NBT backend (console glue for local_nbt_core.hpp).
//
// When this console hosts the world (single player / local split screen, no LCE server
// session), the NBT editor talks to this backend instead of the server: the same
// "/lce-nbt" lines, the same replies. The work runs on the native server thread inside the
// shared retained-player scope (super.cpp registers lce_nbt_local_scope_tick with
// allow_paused, like the debug give/mode subscribers, because the menu pauses the game).
//
// Every native entry point is attested before first use: a 64-byte FNV-1a hash of its code
// at image + RVA (exact 1.95 image c675e6ce...029f), the ServerPlayer vtable of the view's
// player and its load/teleport slots. Any mismatch leaves the backend unavailable (the NBT
// tab then shows the stock viewer, as before). Large buffers are allocated on first use.
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <orbis/libkernel.h>
#include "../../r72-session-candidate/loader/lucky_block_world_events_195/server_scope.h"
#include "local_nbt_core.hpp"
#include "request_gate.hpp"
#include "owned_save_pin.hpp"
extern "C" uintptr_t mc195_stock_player_original;
extern "C" void mc195_stock_player_proxy();
extern "C" bool mc195_nbt_offline_ready();
extern "C" int mc195_nbt_navigation_owner(const void *,unsigned *,mc195_debug_pad_ticket *,void **);
extern "C" int mc195_nbt_local_authority_copy(const mc195_debug_pad_ticket *,uint64_t,mc195_nbt_player *);
extern "C" uint64_t mc195_lucky_candidate_session(void *);
extern "C" int mc195_lucky_candidate_read(void *,uintptr_t,void *,size_t);

extern "C" void mc195_remote_log(const char *);
#ifndef LCE_HOST_TEST
inline void *operator new(size_t, void *p) noexcept { return p; }
#endif

namespace {
using namespace lce_local_nbt;
struct Pin { uint32_t rva, len, fnv; };
// alloc, free, alloc_array, ws_assign, compound_ctor, put_tag, list_ctor, list_grow, ctor_byte, ctor_short, ctor_int,
// ctor_long, ctor_float, ctor_double, ctor_string, ctor_byte_array, ctor_int_array, entity_save, entity_load, teleport, set_game_mode
const Pin kPins[] = {
    {0xe6cfd0u, 16, 0x76e60709u}, {0xe6cfb0u, 16, 0xcfed5119u}, {0xe6cfc0u, 16, 0xdddf5ce1u}, {0x67840u, 64, 0x24cc3f24u},
    {0x1b6df0u, 64, 0x830effecu}, {0x1af4a0u, 64, 0x5604cc97u}, {0x41cbf0u, 64, 0xbb615a90u}, {0x1e5830u, 64, 0xd322c784u},
    {0x158f80u, 64, 0xd9c00cd3u}, {0x60abd0u, 64, 0x732d91dcu}, {0x37a3b0u, 64, 0xc984153au}, {0x439d90u, 64, 0x46efb56eu},
    {0x2e9350u, 64, 0xecff69d5u}, {0x215dc0u, 64, 0xfd6cde73u}, {0x68b620u, 64, 0xeea651b2u}, {0x13b2f0u, 64, 0x0d3c74d2u},
    {0x378f40u, 64, 0xb1665bcau}, {0x27bc70u, 64, 0x838e1ce9u}, {0x27cc90u, 64, 0x8db12f48u}, {0xd0ca30u, 64, 0xcab63626u},
    {0xd0d0e0u, 64, 0x3cf5b299u},
};
constexpr uintptr_t kServerPlayerVt = 0x1341168u, kGameTypeSurvival = 0x1416a90u, kGameTypeCreative = 0x1416a98u;
constexpr unsigned kSlotLoad = 0x290, kSlotTeleport = 0x670;
constexpr uint64_t kFreshUs = 1500000;   // a scope tick this recent means "a local world is live"

Natives nat;
Backend *backend;
unsigned state;            // 0 not tried, 1 ready, 2 refused (attestation), 3 out of memory, 4 faulted
uintptr_t bound_image;
uint64_t last_tick_us;
unsigned lease_lock;
const void *lease_nav; void *lease_owner; unsigned lease_slot;
local_nbt_gate::Request lease_request, posted_request;
bool lock_lease(){unsigned z=0;return __atomic_compare_exchange_n(&lease_lock,&z,1u,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED);}
void unlock_lease(){__atomic_store_n(&lease_lock,0u,__ATOMIC_RELEASE);}
int copy_authority(void *,const mc195_debug_pad_ticket*t,uint64_t w,mc195_nbt_player*out){return mc195_nbt_local_authority_copy(t,w,out);}
uint64_t gate_clock(void *){return sceKernelGetProcessTime();}
int scope_current(void *p){return mc195_lucky_server_current((const mc195_lucky_server_view *)p);}
local_nbt_gate::Ops gate_ops(void *p){return {p,copy_authority,scope_current,gate_clock};}
bool same_ticket(const mc195_debug_pad_ticket&a,const mc195_debug_pad_ticket&b){return a.slot==b.slot&&a.handle==b.handle&&a.generation==b.generation;}
bool lease_current(uint64_t now,local_nbt_gate::Request *fresh){
 unsigned slot=0;void *owner=nullptr;mc195_debug_pad_ticket pad{};
 if(!mc195_nbt_offline_ready()||!lease_nav||!fresh||!now||now>UINT64_MAX-15000000||mc195_lucky_candidate_session(nullptr)!=lease_request.world||
    mc195_nbt_navigation_owner(lease_nav,&slot,&pad,&owner)!=1||slot!=lease_slot||owner!=lease_owner||!same_ticket(pad,lease_request.pad))return false;
 return local_nbt_gate::admit(gate_ops(nullptr),pad,lease_request.world,now+15000000,fresh)&&!memcmp(&fresh->player,&lease_request.player,sizeof fresh->player);
}
struct Authorization {const mc195_lucky_server_view *view; mc195_nbt_player actor;};
int authorized(void *p,uintptr_t player){auto*a=(Authorization*)p;return mc195_nbt_offline_ready()&&a&&a->view&&player==a->view->player.object&&local_nbt_gate::authorized(gate_ops((void*)a->view),posted_request,a->view->session,a->actor)?1:0;}
bool actor(const mc195_lucky_server_view*v,mc195_nbt_player *p){
 if(!v||!p||mc195_lucky_server_current(v)!=1||!v->player.object||v->player.object>UINTPTR_MAX-0xa40-28)return false;
 auto a=v->player.object; return mc195_lucky_candidate_read(nullptr,a+0x18,&p->entity_id,4)==1&&mc195_lucky_candidate_read(nullptr,a+0xa24,p->offline,28)==1&&mc195_lucky_candidate_read(nullptr,a+0xa40,p->online,28)==1&&mc195_lucky_server_current(v)==1;
}


int read_mem(void *, uintptr_t a, void *o, size_t n) { memcpy(o, (const void *)a, n); return 1; }
uint32_t fnv(const uint8_t *p, size_t n) { uint32_t h = 2166136261u; for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 16777619u; } return h; }
void log_line(const char *m) { char l[200]; snprintf(l, sizeof l, "NBT local: %s\n", m); mc195_remote_log(l); }

template <class F> void bind(F &f, uintptr_t image, uint32_t rva) { f = (F)(image + rva); }
bool checked_rx(uintptr_t a,size_t n) {
    void *lo=nullptr,*hi=nullptr;int32_t prot=0;
    return a && n && a<=UINTPTR_MAX-n && !sceKernelQueryMemoryProtection((void*)a,&lo,&hi,&prot) && (prot&7)==5 && (uintptr_t)lo<=a && (uintptr_t)hi>=a+n;
}
bool owned_save_attested(uintptr_t image) {
    uintptr_t site=image+0x27bc70u, original=__atomic_load_n(&mc195_stock_player_original,__ATOMIC_ACQUIRE);
    if(original<0x40) return false;
    uintptr_t page=original-0x40;
    if(!checked_rx(site,64)||!checked_rx(page,14)||!checked_rx(original,22))return false;
    uint8_t live[64],relay[14],tramp[22];memcpy(live,(void*)site,64);memcpy(relay,(void*)page,14);memcpy(tramp,(void*)original,22);
    return owned_save_pin(live,relay,tramp,site,page,original,(uintptr_t)&mc195_stock_player_proxy);
}
bool attest(uintptr_t image)
{
    for (const Pin &p : kPins) {
        if (fnv((const uint8_t *)(image + p.rva), p.len) != p.fnv && !(p.rva==0x27bc70u && p.len==64 && owned_save_attested(image))) { char m[80]; snprintf(m, sizeof m, "pin 0x%x does not match; local editing off", (unsigned)p.rva); log_line(m); return false; }
    }
    uintptr_t gs, gc; memcpy(&gs, (const void *)(image + kGameTypeSurvival), 8); memcpy(&gc, (const void *)(image + kGameTypeCreative), 8);
    if (!gs || !gc || gs == gc) { log_line("game type globals not ready"); return false; }
    nat = Natives{};
    nat.image = image; nat.read = read_mem; nat.ctx = nullptr;
    bind(nat.alloc, image, 0xe6cfd0u); bind(nat.free_, image, 0xe6cfb0u); bind(nat.alloc_array, image, 0xe6cfc0u); bind(nat.ws_assign, image, 0x67840u);
    bind(nat.compound_ctor, image, 0x1b6df0u); bind(nat.put_tag, image, 0x1af4a0u); bind(nat.list_ctor, image, 0x41cbf0u); bind(nat.list_grow, image, 0x1e5830u);
    bind(nat.ctor_byte, image, 0x158f80u); bind(nat.ctor_short, image, 0x60abd0u); bind(nat.ctor_int, image, 0x37a3b0u); bind(nat.ctor_long, image, 0x439d90u);
    bind(nat.ctor_float, image, 0x2e9350u); bind(nat.ctor_double, image, 0x215dc0u); bind(nat.ctor_string, image, 0x68b620u);
    bind(nat.ctor_byte_array, image, 0x13b2f0u); bind(nat.ctor_int_array, image, 0x378f40u);
    bind(nat.entity_save, image, 0x27bc70u); bind(nat.entity_load, image, 0x27cc90u); bind(nat.teleport, image, 0xd0ca30u); bind(nat.set_game_mode, image, 0xd0d0e0u);
    nat.game_type[0] = gs; nat.game_type[1] = gc;
    return true;
}
// The view's player must be the exact ServerPlayer whose load/teleport slots are the pinned ones.
bool player_ok(uintptr_t image, uintptr_t player)
{
    if (!player) return false;
    uintptr_t vt; memcpy(&vt, (const void *)player, 8);
    if (vt != image + kServerPlayerVt) return false;
    uintptr_t load, tp; memcpy(&load, (const void *)(vt + kSlotLoad), 8); memcpy(&tp, (const void *)(vt + kSlotTeleport), 8);
    return load == image + 0x27cc90u && tp == image + 0xd0ca30u;
}
bool ensure(uintptr_t image)
{
    if (state == 1) return image == bound_image;
    if (state) return false;
    if (!attest(image)) { state = 2; return false; }
    void *mem = malloc(sizeof(Backend) + 2 * sizeof(Doc) + 64);
    if (!mem) { state = 3; log_line("out of memory; local editing off"); return false; }
    uint8_t *p = (uint8_t *)(((uintptr_t)mem + 63) & ~(uintptr_t)63);
    Doc *live = new (p) Doc(); Doc *work = new (p + sizeof(Doc)) Doc(); live->clear(); work->clear();
    backend = new (p + 2 * sizeof(Doc)) Backend(live, work);
    bound_image = image;
    __atomic_store_n(&state, 1u, __ATOMIC_RELEASE);
    log_line("ready (player document; natives attested)");
    return true;
}
}  // namespace

// ---------------------------------------------------------------- server thread (shared scope)
extern "C" __attribute__((visibility("hidden"))) void lce_nbt_local_scope_tick(void *, const mc195_lucky_server_view *v)
{
    if (!v || !v->image || !v->player.object) return;
    if (!ensure(v->image)) return;
    if (!player_ok(v->image, v->player.object)) return;
    __atomic_store_n(&last_tick_us, (uint64_t)sceKernelGetProcessTime(), __ATOMIC_RELEASE);
    if (!lock_lease()) return;
    Authorization a{v,{}};
    if (lease_nav && actor(v,&a.actor) && authorized(&a,v->player.object)==1) {
        Natives current=nat; current.authorize=authorized; current.authorize_context=&a;
        backend->process(current,v->player.object);
    }
    unlock_lease();
}
extern "C" __attribute__((visibility("hidden"))) void lce_nbt_local_scope_failed(void *, unsigned)
{
    __atomic_store_n(&state, 4u, __ATOMIC_RELEASE);   // never touched again this boot
}

// ---------------------------------------------------------------- UI thread (nbt_editor.cpp)
extern "C" __attribute__((visibility("hidden"))) bool lce_nbt_local_available(void)
{
    if (__atomic_load_n(&state, __ATOMIC_ACQUIRE) != 1u) return false;
    const uint64_t t = __atomic_load_n(&last_tick_us, __ATOMIC_ACQUIRE);
    return t && (uint64_t)sceKernelGetProcessTime() - t < kFreshUs;
}
// Owner thread only. Return 2 after a fresh lease, 1 on the same lease, 0 if unavailable.
extern "C" __attribute__((visibility("hidden"))) int lce_nbt_local_bind(const void *nav) {
 if (!lock_lease()) return 0;
 unsigned slot=0;void *owner=nullptr;mc195_debug_pad_ticket pad{};
 uint64_t world=mc195_lucky_candidate_session(nullptr), now=sceKernelGetProcessTime();
 local_nbt_gate::Request fresh{};
 bool valid=mc195_nbt_offline_ready()&&nav&&backend&&world&&now&&now<=UINT64_MAX-15000000&&mc195_nbt_navigation_owner(nav,&slot,&pad,&owner)==1&&local_nbt_gate::admit(gate_ops(nullptr),pad,world,now+15000000,&fresh);
 // Refusal includes transient locks. Do not clear an existing document,
 // undo history or request merely because a fresh authority copy was busy.
 if(!valid){unlock_lease();return 0;}
 bool same=lease_nav==nav&&lease_owner==owner&&lease_slot==slot&&same_ticket(lease_request.pad,pad)&&lease_request.world==world&&memcmp(&lease_request.player,&fresh.player,sizeof fresh.player)==0;
 // A busy pad is not proof that an old lease is dead. Replacement requires
 // an independently dead old ticket or a positively different world token.
 if(lease_nav && !same && lease_request.world==world && mc195_debug_pad_state(&lease_request.pad)!=0){unlock_lease();return 0;}
 int result=0;
 if(same && backend->busy() && !backend->reply_ready() && posted_request.deadline_us && now>=posted_request.deadline_us) {
  if(backend->discard_exchange(4u)){posted_request={};result=1;}
 } else if(same)result=1;
 else if(!backend || backend->retire()) {lease_nav=nullptr;lease_owner=nullptr;lease_request={};posted_request={};if(valid){lease_nav=nav;lease_owner=owner;lease_slot=slot;lease_request=fresh;result=2;}}
 unlock_lease();return result;
}
extern "C" __attribute__((visibility("hidden"))) bool lce_nbt_local_current(const void *navigation) {
 if(!lock_lease())return false;
 local_nbt_gate::Request fresh{};
 bool result=navigation&&navigation==lease_nav&&lease_current(sceKernelGetProcessTime(),&fresh);
 unlock_lease();return result;
}
extern "C" __attribute__((visibility("hidden"))) bool lce_nbt_local_retire(void) {
 if(!lock_lease())return false;
 bool done=!backend||backend->retire();
 if(done){lease_nav=nullptr;lease_owner=nullptr;lease_request={};posted_request={};}
 unlock_lease();return done;
}
extern "C" __attribute__((visibility("hidden"))) bool lce_nbt_local_busy(void) { if(!lock_lease())return true;bool b=!backend||backend->busy();unlock_lease();return b; }
extern "C" __attribute__((visibility("hidden"))) bool lce_nbt_local_post(const char *lines, size_t n)
{
    if (__atomic_load_n(&state, __ATOMIC_ACQUIRE) != 1u || !backend || !lock_lease()) return false;
    uint64_t now=sceKernelGetProcessTime();local_nbt_gate::Request fresh{};
    bool valid=lease_current(now,&fresh);
    bool result=false;
    if(valid&&!backend->busy()){result=backend->post(lines,n);if(result)posted_request=fresh;}
    unlock_lease();return result;
}
extern "C" __attribute__((visibility("hidden"))) bool lce_nbt_local_take(char *op, size_t opc, char *target, size_t tc, bool *ok, char *msg, size_t mc, const uint8_t **nbt, size_t *n)
{
    if (!backend || !lock_lease()) return false;
    // Polling with no reply is a no-op; it must not retire the backend.
    if (!backend->reply_ready()) {unlock_lease();return false;}
    Backend::Reply r; local_nbt_gate::Request fresh{};
    // Authority may be transiently busy: withhold the reply and retry. The
    // original deadline below remains mandatory before any eventual take.
    if (!lease_current(sceKernelGetProcessTime(),&fresh)) {unlock_lease();return false;}
    uint64_t now=sceKernelGetProcessTime();
    if(!posted_request.deadline_us || now>=posted_request.deadline_us || posted_request.world!=fresh.world || !same_ticket(posted_request.pad,fresh.pad) || memcmp(&posted_request.player,&fresh.player,sizeof fresh.player)) {
        (void)backend->discard_exchange(3u); posted_request={}; unlock_lease(); return false;
    }
    if (!backend->take(&r)) {unlock_lease(); return false;}
    snprintf(op, opc, "%s", r.op); snprintf(target, tc, "%s", r.target); snprintf(msg, mc, "%s", r.msg);
    *ok = r.ok; *nbt = r.nbt; *n = r.n; posted_request={};
    unlock_lease(); return true;
}
