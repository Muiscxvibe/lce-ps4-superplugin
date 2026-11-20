#include "../nbt_mode_runtime_195/runtime.hpp"
#include "../debug_give_195/runtime.hpp"
#include "../lucky_block_full_events_195/provider_repair_r26/giant/texture.h"
#include "../lucky_block_full_events_195/provider_repair_r26/rainbow/rainbow.h"
#include "../lucky_block_full_events_195/chat_colour/runtime.h"
#include "lucky_trace_filter.h"
#include "../lucky_block_world_events_195/placement_diag.h"
#include "../lucky_block_world_events_195/contents_diag.h"
#include "../lucky_block_full_events_195/custom_block/startup_component.h"
#include "../lucky_block_full_events_195/custom_block/redstone.h"
#include "../lucky_block_full_events_195/well_actions/coin.h"
#include "../lucky_block_full_events_195/well_probe/probe.h"
extern "C" int mc195_native_well_probe_last(mc195_well_probe_result *);
extern "C" int mc195_native_giant_probe_last(mc195_giant_probe_result *);
extern "C" void mc195_native_redstone_stats(uint64_t [4]);
#include "../lucky_block_full_events_195/item_diagnostics/diag.h"
#include "../lucky_block_native_provider_195/provider.h"
#include "mc195_craft_diagnostics.h"
#include "../lucky_block_native_provider_195/drops.h"
#include "nbt_integration.hpp"
#include "../f3_server_195/f3_server.h"
namespace mc195_f3_hud { unsigned diagnostic_flags(unsigned); }
#include "../shared_server_scope_195/mux.hpp"
#include "../debug_world_difficulty_195/runtime.hpp"
static void mc195_f3_shared_tick(void *,const mc195_lucky_server_view *v){mc195_f3_tick(v);}
/* Local-world NBT editor (crashlog-build/local_nbt.cpp): player document on the native server thread. */
extern "C" void lce_nbt_local_scope_tick(void *,const mc195_lucky_server_view *);
extern "C" void lce_nbt_local_scope_failed(void *,unsigned);
#include "../lucky_block_world_events_195/native_owner.h"
static void mc195_f3_shared_failed(void *,unsigned){mc195_f3_scope_failed();}
static void mc195_lucky_shared_failed(void *,unsigned){mc195_lucky_native_scope_fault();}
static void mc195_mode_shared_failed(void *,unsigned){mc195_nbt_mode_runtime_scope_failed();}
static void mc195_give_shared_failed(void *,unsigned){mc195_debug_give_runtime_scope_failed();}
static void mc195_difficulty_shared_failed(void *,unsigned){mc195_debug_difficulty_runtime_scope_failed();}
#include "../lucky_block_full_events_195/lucky_events_bridge.h"
#include "../lucky_block_full_events_195/villager/armorer.h"
#include "../lucky_block_full_events_195/catalogue_info.h"
#include "../nbt_status_process_195/diagnostics.h"
#include "../debug_action_log_195/worker.h"
#include <orbis/libkernel.h>
#include <stddef.h>
#include <stdint.h>
#include "storage.hpp"
#include "tools_runtime.h"
#include "lucky_integration.h"
#include "steel_ore_integration.h"
#include "../shared_terrain_195/runtime.h"
extern "C" void mc195_growth_relay_mark_ready(void);
extern "C" void mc195_frontend_diag_base(uintptr_t);
#include "../steel_ore_probe_195/probe.h"
#include "../additive_skin_owner_selection_195/gre_component.h"
#include "../additive_skin_fanout_195/status.h"
#include "../additive_skin_owner_195/storage_telemetry.h"
#include "../barebones_runtime_lookup_195/gre_adapter.h"
#include "../barebones_root_guard_195/guard.h"
#include "../barebones_root_guard_195/manager_guard.h"
#include "../../native-debug-plugin/cooperative-upgrade/menu-hierarchy-world-actions/runtime.h"
#include "../super_195_candidate/shared_tick_195.h"
#include "../super_195_candidate/dialog_gate_195.h"
#include "../super_195_candidate/store_bridge.hpp"
#include "../store_button_195/operational/menu_action.h"
#include "../store_button_195/operational/menu_label.h"
#include "../store_button_195/operational/probe.h"
#include "../offline_ui_owner_195/observe.h"
#include "../offline_ui_owner_195/once_component.h"
#include "../steel_startup_trace_195/trace_component.h"
#include "../steel_startup_trace_195/registry_component.h"
#include "../steel_process_owner_195/owner.h"
#include "../steel_process_owner_195/resource.h"
#include "../steel_retirement_hook_195/hook.h"
#include "../steel_process_retire_diag_195/retirement_stats.h"
#include "../steel_creative_195/creative.h"
#include "../mods_creative_page_telemetry_195/navigation.h"
#include "../mods_creative_page_telemetry_195/category.h"
#include "../mods_creative_page_telemetry_195/observations.h"
#include "../mods_creative_movie_195/runtime.h"
#include "../steel_process_content_195/process.h"
#include "../selection_complete_packs_195/worker.hpp"
#include "../expanded_pack_preflight_195/staged_verify.hpp"
#include "../workshop_skin_preflight_195/staged_verify.hpp"
#include "../additive_workshop_skin_runtime_195/component.h"
#include "../additive_skin_ports_195/component.h"
#include "../additive_modern_skin_ports_195/component.h"
#include "../additive_modern_skin_ports_195/staged_verify.hpp"
#include "../additive_skin_ports_195/staged_verify.hpp"
#include "../barebones_compat_runtime_195/gre_adapter.h"
#include "../pixel_companion_r3_runtime_195/gre_adapter.h"
#include "../java_texture_invictus_runtime_195/kernel_file_ops.hpp"
#include "../java_texture_invictus_runtime_195/staged_verify.hpp"
#include "../java_texture_invictus_runtime_195/bind.hpp"
#include "../java_texture_invictus_runtime_195/adapter.hpp"
#include "../content_texture_lookup_195/telemetry.h"
#include "lucky_items_providers.h"
#include "lucky_items_bridge.h"
#include "../content_startup_notice_195/runtime.h"
#include "../map_import_195/provider.h"
#include "../map_import_195/worker.h"
#include "../content_notifications_195/reporter.h"
#include "../../native-debug-plugin/cooperative-upgrade/debug-runtime/native-input/native_pad.hpp"
extern "C" bool mc195_ps4_attest(uintptr_t *);
extern "C" int mc195_debug_load(int,const char **);
extern "C" bool mc195_debug_ui_ready();
extern "C" unsigned mc195_debug_owner_thread();
extern "C" int mc195_bb2016_lookup_stats(struct mc195_texture_lookup_stats *);
extern "C" int mc195_pixel2017_lookup_stats(struct mc195_texture_lookup_stats *);
extern "C" int steel195_resource_hooks_start(uintptr_t);
extern "C" int steel195_creative_hook_start(uintptr_t);
extern "C" int steel195_craft_hook_start(uintptr_t);
extern "C" int mc195_craft_candidate_start(uintptr_t);
#if defined(MC195_SUPERVISOR_TEST)
extern "C" bool mc195_test_message_box(void *,int32_t,uint32_t);
#endif
/* agent-perfmods: boot timing + boot optimisations (crashlog-build/perf_boot.hpp). */
extern "C" void lce_perf_boot_init(void);
extern "C" void lce_perf_boot_hooks(uintptr_t);
extern "C" void lce_perf_mark(const char *);
extern "C" uint64_t lce_perf_now_us(void);
extern "C" void lce_perf_account(unsigned,uint64_t);
extern "C" void lce_perf_flush(void);
extern "C" void lce_perf_title_tick(void);
extern "C" void lce_perf_rx_release(void);
extern "C" unsigned lce_perf_poll_step_us(void);
#define EXPORT extern "C" __attribute__((visibility("default")))
EXPORT const char *g_pluginName="Minecraft 1.95 super plugin";
EXPORT const char *g_pluginDesc="Mods, additive packs and startup notifications";
EXPORT const char *g_pluginAuth="Codex";
EXPORT uint32_t g_pluginVersion=1;
namespace {
unsigned state,ui_busy;
Mc195SharedScope shared_server_scope{};
unsigned shared_lucky_bit;
constexpr bool debug_difficulty_enabled=true;
unsigned selection_bound;
unsigned content_boundary_ready;
unsigned choices_ready;
unsigned creative_ui_ready;
int steel_enabled,gre_enabled,texture_enabled,lucky_enabled;
unsigned java_eligible,java_selected,java_admitted;
const char *const java_ids[]={"faithful-lce","pixel-perfection-lce","before-lce","bare-bones-compat","pixel-extended","touhou-skins","source-skins","trailer-skins","420-skins","love-and-tolerance-lce","dandelion-lce","gaming-legends-classic-skins","rick-morty-classic-skins","classic-youtubers-skins","miku-friends-classic-skins","invictus-lce","gaming-legends-modern-2029","story-mode-modern-2030","fabulous-beasts-modern-2031","classic-youtubers-modern-2032"};
unsigned java_probe_done,java_probe_mask;
OrbisPthread java_probe_worker;
void *java_preflight(void *){
 lce195_file_ops ops{};bool ready=mc195_java_kernel_file_ops(&ops);
 unsigned java=ready?mc195_java_staged_available(&ops):0;
 __atomic_fetch_or(&java_probe_mask,java&0x43038u,__ATOMIC_RELAXED);
 __atomic_fetch_or(&java_probe_done,1u,__ATOMIC_RELEASE);
 unsigned pixel=ready?mc195_extra_staged_available_index(&ops,1):0;
 __atomic_fetch_or(&java_probe_mask,pixel&0x80u,__ATOMIC_RELAXED);
 __atomic_fetch_or(&java_probe_done,2u,__ATOMIC_RELEASE);
 for(unsigned i=0;i<4;++i){unsigned skin=ready?mc195_skin_staged_available_index(&ops,i):0;
  __atomic_fetch_or(&java_probe_mask,skin,__ATOMIC_RELAXED);
  __atomic_fetch_or(&java_probe_done,8u<<i,__ATOMIC_RELEASE);
 }
 for(unsigned i=0;i<4;++i){unsigned skin=ready?mc195_skin_ports_staged_available_index(&ops,i):0;
  __atomic_fetch_or(&java_probe_mask,skin,__ATOMIC_RELAXED);
  __atomic_fetch_or(&java_probe_done,128u<<i,__ATOMIC_RELEASE);
 }
 for(unsigned i=0;i<4;++i){unsigned skin=ready?mc195_modern_skin_ports_staged_available_index(&ops,i):0;
  __atomic_fetch_or(&java_probe_mask,skin,__ATOMIC_RELAXED);
  __atomic_fetch_or(&java_probe_done,2048u<<i,__ATOMIC_RELEASE);
 }
 unsigned bones=ready?mc195_extra_staged_available_index(&ops,0):0;
 __atomic_fetch_or(&java_probe_mask,bones&0x40u,__ATOMIC_RELAXED);
 __atomic_fetch_or(&java_probe_done,4u,__ATOMIC_RELEASE);
 mc195_maps_scan(ready?&ops:nullptr);return nullptr;
}
unsigned gre_ui_state;
unsigned store_thread;
unsigned storage_started,storage_ready;
mc195_storage::Sample storage_snapshot;
uintptr_t attested_base;
int32_t store_pad=-1;
void logline(const char *s){size_t n=0;while(s[n])++n;int fd=sceKernelOpen("/data/mc-195-combined-test.log",0x209,0600);if(fd>=0){(void)sceKernelWrite(fd,s,n);(void)sceKernelClose(fd);}}
unsigned lucky_native_reports;int lucky_native_last_result;
void lucky_native_report(void *,const char *,int result){__atomic_store_n(&lucky_native_last_result,result,__ATOMIC_RELAXED);__atomic_fetch_add(&lucky_native_reports,1u,__ATOMIC_RELAXED);}
void content_state(const char *id,unsigned value){
 (void)mc195_selection_worker_set_runtime_state(id,value);
 if(value==MC195_CONTENT_FAILED){
  unsigned mask=!__builtin_strcmp(id,"native-lucky-blocks")?(1u<<23):!__builtin_strcmp(id,"steel")?MC195_NOTICE_STEEL:
   !__builtin_strcmp(id,"gre-skins")?MC195_NOTICE_GRE:
   !__builtin_strcmp(id,"bare-bones")?MC195_NOTICE_TEXTURE:0;
  mc195_content_notices_fail(mask);
  unsigned port=!__builtin_strcmp(id,"faithful-lce")?8u:!__builtin_strcmp(id,"pixel-perfection-lce")?16u:!__builtin_strcmp(id,"before-lce")?32u:!__builtin_strcmp(id,"bare-bones-compat")?64u:!__builtin_strcmp(id,"pixel-extended")?128u:!__builtin_strcmp(id,"touhou-skins")?256u:!__builtin_strcmp(id,"source-skins")?512u:!__builtin_strcmp(id,"trailer-skins")?1024u:!__builtin_strcmp(id,"420-skins")?2048u:!__builtin_strcmp(id,"love-and-tolerance-lce")?4096u:!__builtin_strcmp(id,"dandelion-lce")?8192u:!__builtin_strcmp(id,"gaming-legends-classic-skins")?16384u:!__builtin_strcmp(id,"rick-morty-classic-skins")?32768u:!__builtin_strcmp(id,"classic-youtubers-skins")?65536u:!__builtin_strcmp(id,"miku-friends-classic-skins")?131072u:!__builtin_strcmp(id,"invictus-lce")?262144u:!__builtin_strcmp(id,"gaming-legends-modern-2029")?524288u:!__builtin_strcmp(id,"story-mode-modern-2030")?1048576u:!__builtin_strcmp(id,"fabulous-beasts-modern-2031")?2097152u:!__builtin_strcmp(id,"classic-youtubers-modern-2032")?4194304u:0u;
  if(port)mc195_port_notices_fail(port);
 }
}
void fail_selected_ports(){for(unsigned i=0;i<20;++i)if(java_selected&(8u<<i))content_state(java_ids[i],MC195_CONTENT_FAILED);}
struct Guard {bool held;Guard():held(!__atomic_exchange_n(&ui_busy,1u,__ATOMIC_ACQUIRE)){}~Guard(){if(held)__atomic_store_n(&ui_busy,0u,__ATOMIC_RELEASE);}};
bool valid_query(){
#if defined(MC195_SUPERVISOR_TEST)
 return true;
#else
 uintptr_t slot=attested_base+0x131a3c8+0x98;void *lo=nullptr,*hi=nullptr;int32_t prot=0;
 return !sceKernelQueryMemoryProtection((void*)slot,&lo,&hi,&prot) && (prot&1) &&
        (uintptr_t)lo<=slot && (uintptr_t)hi>=slot+sizeof(uintptr_t) &&
        *(const uintptr_t*)slot==attested_base+0x9dcc00;
#endif
}
bool mapping(uintptr_t p,size_t n,unsigned required){
 void *lo=nullptr,*hi=nullptr;int32_t prot=0;
 return p&&n&&p<=UINTPTR_MAX-n&&!sceKernelQueryMemoryProtection((void*)p,&lo,&hi,&prot)&&
        ((unsigned)prot&required)==required&&(uintptr_t)lo<=p&&(uintptr_t)hi>=p+n;
}
/* Called only from the validated native UI tick. The Store coordinator's
   quiescence includes consumed callbacks and independent release checks. */
void ui_tick(void *controller){
 Guard guard;if(!guard.held)return;
 lce_perf_title_tick();
 struct PerfTick{uint64_t t=lce_perf_now_us();~PerfTick(){lce_perf_account(2u,t);}} perf_tick; /* LCE_PERF_SPAN_TITLE_TICK */
 unsigned u=1u;
 if(controller==(void *)(attested_base+0x143b290u))u|=2u;
 int32_t tid=scePthreadGetthreadid();
 if(tid>0&&(unsigned)tid==mc195_debug_owner_thread())u|=4u;
 if(mc195_dialog_owned(MC195_DIALOG_STORE))u|=8u;
 if(mc195_dialog_owned(MC195_DIALOG_DEBUG))u|=16u;
 __atomic_store_n(&gre_ui_state,u,__ATOMIC_RELEASE);
 if(!selection_bound && controller==(void *)(attested_base+0x143b290u) &&
    scePthreadGetthreadid()>0 && (unsigned)scePthreadGetthreadid()==mc195_debug_owner_thread() &&
    mc195_selection_worker_status()==MC195_SELECTION_WORKER_READY && mc195_selection_worker_bind())selection_bound=1;
 if(valid_query())mc195_menu_candidate_tick(controller);
 mc195_menu_label_tick(controller);
 if(content_boundary_ready&&(gre_enabled||texture_enabled||java_admitted)&&!mc195_dialog_owned(MC195_DIALOG_STORE)&&!mc195_dialog_owned(MC195_DIALOG_DEBUG)){uint64_t t=lce_perf_now_us();mc195_gre_component_tick(controller);lce_perf_account(0u,t);} /* LCE_PERF_SPAN_GRE_TICK */
 if(mc195_dialog_owned(MC195_DIALOG_STORE)&&store_thread &&
    store_thread==(unsigned)scePthreadGetthreadid()&&store_pad>=0&&store_pad<=3&&
    (uintptr_t)controller==attested_base+0x143b290 && valid_query() && mc195_super_store_quiescent()){
  using Query=bool (*)(void*,int32_t,uint32_t);
#if defined(MC195_SUPERVISOR_TEST)
  auto query=(Query)mc195_test_message_box;
#else
  auto query=(Query)(attested_base+0x9dcc00);
#endif
  bool pad_empty=!query(controller,store_pad,0x4c),fullscreen_empty=!query(controller,0xff,0x4c);
  (void)mc195_dialog_release(MC195_DIALOG_STORE,true,pad_empty,fullscreen_empty);
 }
}
int store_event(uintptr_t scene,int32_t pad,uintptr_t caller){
 Guard guard;if(!guard.held)return 1;
 if(!valid_query())return 0;
 unsigned tid=(unsigned)scePthreadGetthreadid();if(!tid||pad<0||pad>3)return 1;
 bool newly_claimed=false;
 if(!mc195_dialog_owned(MC195_DIALOG_STORE)){
  if(!mc195_dialog_claim(MC195_DIALOG_STORE))return 1;
  newly_claimed=true;store_pad=pad;store_thread=tid;
 }else if(store_pad!=pad||store_thread!=tid)return 1;
 int result=mc195_menu_candidate_event(scene,pad,caller);
 if(result && mc195_menu_candidate_owner_thread(scene)==tid){
  unsigned expected=0;
  if(__atomic_compare_exchange_n(&storage_started,&expected,1u,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)){
   storage_snapshot=mc195_storage::capture(attested_base,tid,mapping);
   __atomic_store_n(&storage_ready,1u,__ATOMIC_RELEASE);
  }
 }
 /* A newly claimed event that falls through to stock issued no Mods request. */
 if(!result && newly_claimed && mc195_super_store_quiescent())
  (void)mc195_dialog_release(MC195_DIALOG_STORE,true,true,true);
 return result;
}
extern "C" unsigned mc195_tabs_slot_stat(unsigned,unsigned);
extern "C" unsigned mc195_debug_view_declines(unsigned);
uint32_t events(){
 mc195_debug_action_log_worker(logline);
 {static unsigned prior[4]{};for(unsigned i=0;i<4;++i){unsigned flags=mc195_f3_hud::diagnostic_flags(i);if(flags!=prior[i]){char line[192];snprintf(line,sizeof line,"F3_PIPELINE pad=%u flags=%x toggle=%u admitted=%u identity=%u request=%u sample=%u viewport=%u paint=%u inserted=%u\n",i,flags,!!(flags&1),!!(flags&2),!!(flags&4),!!(flags&8),!!(flags&16),!!(flags&32),!!(flags&64),!!(flags&128));logline(line);prior[i]=flags;}}}
 {static unsigned last[32],lines;unsigned values[32];bool changed=false;for(unsigned i=0;i<32;++i){values[i]=mc195_difficulty_diag_count(i);if(values[i]!=last[i])changed=true;}if(changed&&lines<64){char msg[768];int used=snprintf(msg,sizeof msg,"DIFFICULTY_DIAG");for(unsigned i=0;i<32&&used>0&&(size_t)used<sizeof(msg);++i){int n=snprintf(msg+used,sizeof(msg)-(size_t)used," %u=%u",i,values[i]);if(n<0)break;used+=n;}if(used>0&&(size_t)used<sizeof(msg)-1){msg[used++]='\n';msg[used]=0;logline(msg);}for(unsigned i=0;i<32;++i)last[i]=values[i];++lines;}}

 {static unsigned prior;unsigned failed=mc195_shared_scope_failures(&shared_server_scope),bit=__atomic_load_n(&shared_lucky_bit,__ATOMIC_ACQUIRE);if(failed!=prior){char line[96];snprintf(line,sizeof line,"SHARED_SCOPE subscriber_failures=%x\n",failed);logline(line);if((failed&bit)&&!(prior&bit))content_state("native-lucky-blocks",MC195_CONTENT_FAILED);prior=failed;}}
 {static uint64_t cursor;mc195_contents_diagnostic d{};for(unsigned i=0;i<16&&mc195_contents_diag_next(&cursor,&d);i++){char line[512];snprintf(line,sizeof line,"LUCKY_CONTENTS seq=%zu route=%u block=%u stage=%u rc=%d pos=%d,%d,%d object=%zx vt=%zx level=%zx be_pos=%d,%d,%d strong=%d dropped=%zu\n",(size_t)d.sequence,d.route,d.block,d.stage,d.result,d.pos.x,d.pos.y,d.pos.z,(size_t)d.object,(size_t)d.vtable,(size_t)d.observed_level,d.observed_pos.x,d.observed_pos.y,d.observed_pos.z,d.strong,(size_t)d.dropped);logline(line);}}

 mc195_nbt_runtime_worker();
 mc195_nbt_diag_worker(logline);
 {static unsigned previous[9]{};unsigned current[9]{};mc195_diagnostic_items_results(current);auto bucket=[](unsigned n){unsigned b=0;while(n){++b;n>>=1;}return b;};bool emit=false;for(unsigned i=0;i<9;i++)if(bucket(current[i])!=bucket(previous[i]))emit=true;if(emit){char line[224];snprintf(line,sizeof line,"LUCKY_ITEM_RESULTS bow=%u/%u/%u sword=%u/%u/%u potion=%u/%u/%u order=accepted/refused/uncertain bow_potion_effects=inactive\n",current[0],current[1],current[2],current[3],current[4],current[5],current[6],current[7],current[8]);logline(line);memcpy(previous,current,sizeof current);}}
 {static unsigned previous=0;unsigned current=mc195_lucky_chat_colour_observations();if(current!=previous){char line[128];snprintf(line,sizeof line,"LUCKY_CHAT flags=%u setter=%u matched=%u submitted=%u\n",current,!!(current&1),!!(current&2),!!(current&8));logline(line);previous=current;}}
 {static mc195_craft_diag_snapshot prior{};mc195_craft_diag_snapshot now{};mc195_craft_diag_read(&now);
  if(memcmp(&prior,&now,sizeof now)){char line[512];auto *v=now.value;
   snprintf(line,sizeof line,"CRAFT_MODS movie_seen=%u match=%u made=%u track_fail=%u scenes=%u type=%u type_reject=%u forward=%u enter=%u exit=%u present_fail=%u filter_yes=%u filter_no=%u start=%u scope=%u witness=%u install=%u capture=%u capture_fail=%u movie_size=%u movie_fnv_hi=%x movie_fnv_lo=%x\n",v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7],v[8],v[9],v[10],v[11],v[12],v[13],v[14],v[15],v[16],v[17],v[18],now.movie_size,(unsigned)(now.movie_fnv64>>32),(unsigned)now.movie_fnv64);logline(line);prior=now;}}

 {static uint64_t prior[7]{};uint64_t counts[7]{};mc195_nbt_save_counts(counts);if(prior[0]!=counts[0]||prior[1]!=counts[1]||prior[3]!=counts[3]||prior[4]!=counts[4]||prior[6]!=counts[6]){char line[320];snprintf(line,sizeof line,"NBT_SAVE arm=%zu admitted=%zu getter=%zu busy=%zu trigger=%zu clear_native=%zu clear_skip=%zu\n",(size_t)counts[0],(size_t)counts[1],(size_t)counts[2],(size_t)counts[3],(size_t)counts[4],(size_t)counts[5],(size_t)counts[6]);logline(line);memcpy(prior,counts,sizeof counts);}}
 {static unsigned prior=0;unsigned current=__atomic_load_n(&lucky_native_reports,__ATOMIC_RELAXED);if(current!=prior){char line[96];snprintf(line,sizeof line,"LUCKY_NATIVE reports=%u last=%d\n",current,__atomic_load_n(&lucky_native_last_result,__ATOMIC_RELAXED));logline(line);prior=current;}}
 {static uint64_t cursor=0;mc195_item_rejection d{};for(unsigned i=0;i<64&&mc195_item_rejection_next(&cursor,&d);i++){char line[256];snprintf(line,sizeof line,"LUCKY_ITEM_REJECTION seq=%zu record=%u stage=%s id=%s result=%d dropped=%zu\n",(size_t)d.sequence,d.record,d.stage,d.name,d.result,(size_t)d.dropped);logline(line);}}
 {static uint64_t prior_redstone[8]{};uint64_t now[8]{};mc195_custom_redstone_stats(now);mc195_native_redstone_stats(now+4);if(memcmp(prior_redstone,now,sizeof now)){char line[256];snprintf(line,sizeof line,"LUCKY_REDSTONE neighbor=%zu queued=%zu taken=%zu dropped=%zu checked=%zu powered=%zu captured=%zu event_ok=%zu\n",(size_t)now[0],(size_t)now[1],(size_t)now[2],(size_t)now[3],(size_t)now[4],(size_t)now[5],(size_t)now[6],(size_t)now[7]);logline(line);memcpy(prior_redstone,now,sizeof now);}}
 {static uint32_t previous_giant=0;mc195_giant_probe_result probe{};if(mc195_native_giant_probe_last(&probe)&&probe.sequence!=previous_giant){char line[256];snprintf(line,sizeof line,"LUCKY_GIANT_PROBE seq=%u ids=(%u,%u,%u) meta=(%u,%u,%u) world=%zu session=%zu player=%zu pos=(%d,%d,%d) rotation=%u due=%zu\n",probe.sequence,probe.ids[0],probe.ids[1],probe.ids[2],probe.metadata[0],probe.metadata[1],probe.metadata[2],(size_t)probe.job.world,(size_t)probe.job.session,(size_t)probe.job.player,probe.job.x,probe.job.y,probe.job.z,probe.job.rotation,(size_t)probe.job.due);logline(line);previous_giant=probe.sequence;}}
 {static uint32_t previous_well=0;mc195_well_probe_result probe{};if(mc195_native_well_probe_last(&probe)&&probe.sequence!=previous_well){char line[224];snprintf(line,sizeof line,"LUCKY_WELL_PROBE seq=%u mask=%u world=%zu session=%zu player=%zu pos=(%d,%d,%d) due=%zu\n",probe.sequence,probe.mask,(size_t)probe.job.world,(size_t)probe.job.session,(size_t)probe.job.player,probe.job.x,probe.job.y,probe.job.z,(size_t)probe.job.due);logline(line);previous_well=probe.sequence;}}
 {static uint32_t previous_coin=0;mc195_well_coin_diag coin{};if(mc195_well_coin_diagnostic(&coin)&&coin.sequence!=previous_coin){char line[192];snprintf(line,sizeof line,"LUCKY_WELL_COIN seq=%u stage=%u before=%d after=%d remaining=%d added=%d\n",coin.sequence,coin.stage,coin.before,coin.after,coin.remaining,coin.added);logline(line);previous_coin=coin.sequence;}}
 {static uint64_t previous=0;uint64_t current=mc195_lucky_armorer_diagnostic();if(current!=previous){char line[96];snprintf(line,sizeof line,"LUCKY_ARMORER_DIAGNOSTIC seq=%zu stage=%u\n",(size_t)(current>>8),(unsigned)(current&255));logline(line);previous=current;}}
 {static mc195_lucky_event_stats previous{};mc195_lucky_event_stats current{};mc195_lucky_events_stats(&current);if(current.captures!=previous.captures||current.chosen!=previous.chosen||current.returned_success!=previous.returned_success||current.refused!=previous.refused||current.unsupported!=previous.unsupported||current.uncertain!=previous.uncertain||current.nested_queued!=previous.nested_queued||current.delayed_scheduled!=previous.delayed_scheduled||current.tick_unsupported!=previous.tick_unsupported||current.tick_uncertain!=previous.tick_uncertain||(current.tick_refused!=previous.tick_refused&&current.tick_refused&&!(current.tick_refused&(current.tick_refused-1)))){char line[512];snprintf(line,sizeof line,"LUCKY_EVENTS captures=%zu chosen=%zu record=%u custom=%u returned=%zu refused=%zu unsupported=%zu uncertain=%zu dispatch_ok=%zu nested=%zu scheduled=%zu tick_attempts=%zu tick_refused=%zu tick_unsupported=%zu tick_uncertain=%zu last=%d\n",(size_t)current.captures,(size_t)current.chosen,current.last_record,current.last_custom,(size_t)current.returned_success,(size_t)current.refused,(size_t)current.unsupported,(size_t)current.uncertain,(size_t)current.dispatch_returned_success,(size_t)current.nested_queued,(size_t)current.delayed_scheduled,(size_t)current.tick_jobs_attempted,(size_t)current.tick_refused,(size_t)current.tick_unsupported,(size_t)current.tick_uncertain,current.last_result);logline(line);if(current.chosen!=previous.chosen&&!current.last_custom){mc195_lucky_record_info entry{};if(mc195_lucky_record_describe(current.last_record,&entry)==1){char chosen[400];snprintf(chosen,sizeof chosen,"LUCKY_SELECTION record=%u origin=%s source=%s line=%u category=%s name=%s\n",entry.record,entry.expansion?"expansion":"upstream",entry.source,entry.line,entry.category,entry.name);logline(chosen);}}previous=current;}}
 {static mc195_lucky_delayed_stats previous{};mc195_lucky_delayed_stats current{};
  if(mc195_lucky_events_delayed_stats(&current)==1&&(current.scheduled!=previous.scheduled||current.failed!=previous.failed||current.cancelled!=previous.cancelled||current.completed/32!=previous.completed/32||(previous.queued&&current.queued==0))){
   char line[192];snprintf(line,sizeof line,"LUCKY_DELAYED scheduled=%zu completed=%zu failed=%zu cancelled=%zu queued=%u\n",(size_t)current.scheduled,(size_t)current.completed,(size_t)current.failed,(size_t)current.cancelled,current.queued);logline(line);previous=current;
  }}
 {static uint32_t last;mc195_placement_diag d{};if(mc195_world_placement_diag(&d)&&d.sequence!=last){last=d.sequence;char line[256];snprintf(line,sizeof line,"LUCKY_PLACE seq=%u block=%u meta=%u requested=%u observed=%u native=%d result=%d same=%u\n",d.sequence,d.block_id,d.metadata,d.requested,d.observed,d.native_result,d.result,d.same_pointer);logline(line);}}
 {static uint64_t cursor=0,failure_cursor=0;static mc195_trace_filter filter[2]{};
  for(unsigned stream=0;stream<2;stream++)for(unsigned i=0;i<64;i++){
   mc195_lucky_action_trace action{};
   int found=stream?mc195_lucky_events_next_action(&cursor,&action):mc195_lucky_events_next_failure(&failure_cursor,&action);
   if(found!=1)break;
   if(!filter[stream].emit(action))continue;
   char line[640];
   snprintf(line,sizeof line,"%s seq=%zu record=%u type=%s id=%s result=%d dropped=%zu world=%zu session=%zu player=%zu tick=%zu origin=(%d,%d,%d)\n",stream?"LUCKY_ACTION":"LUCKY_FAILURE",(size_t)action.sequence,action.record,action.type,action.id,action.result,(size_t)action.dropped,(size_t)action.world,(size_t)action.session,(size_t)action.player,(size_t)action.tick,(int)action.origin[0],(int)action.origin[1],(int)action.origin[2]);
   logline(line);
  }
 }
 {static uint32_t prior_life[11]{};mc195_craft_diag_snapshot now{};mc195_craft_diag_read(&now);uint32_t observed[11];memcpy(observed,now.value+32,10*sizeof(uint32_t));observed[10]=now.value[27];if(memcmp(prior_life,observed,sizeof prior_life)){char line[400];auto *v=observed;snprintf(line,sizeof line,"CRAFT_LIFECYCLE guard=%u read=%u dtor_write=%u ctor_write=%u dtor_cas=%u ctor_cas=%u ctor_restore=%u dtor_restore=%u cas_lease=%u cas_success=%u query=%d prot=%d mprotect=%d expected_hi=%x expected_lo=%x seen_hi=%x seen_lo=%x\n",v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7],v[8],v[9],now.protect_query,now.protect_original,now.protect_change,(unsigned)(now.cas_expected>>32),(unsigned)now.cas_expected,(unsigned)(now.cas_observed>>32),(unsigned)now.cas_observed);logline(line);memcpy(prior_life,observed,sizeof prior_life);}}
 {static uint32_t prior_tx[9]{};mc195_craft_diag_snapshot now{};mc195_craft_diag_read(&now);auto *v=now.value+42;if(memcmp(prior_tx,v,sizeof prior_tx)){char line[240];snprintf(line,sizeof line,"CRAFT_TX guard=%u verify=%u open=%u quiet=%u same=%u write=%u readback=%u restore=%u rollback=%u\n",v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7],v[8]);logline(line);memcpy(prior_tx,v,sizeof prior_tx);}}
 {static uint32_t prior_present[9]{};mc195_craft_diag_snapshot now{};mc195_craft_diag_read(&now);auto *v=now.value+51;if(memcmp(prior_present,v,sizeof prior_present)){char line[280];snprintf(line,sizeof line,"CRAFT_PRESENT entry=%u native=%u refresh=%u exit_type=%u label=%u highlight=%u ready=%u component=%u page=%u\n",v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7],v[8]);logline(line);memcpy(prior_present,v,sizeof prior_present);}}
 {static uint32_t prior_icons[3]{};mc195_craft_diag_snapshot now{};mc195_craft_diag_read(&now);auto *v=now.value+60;if(memcmp(prior_icons,v,sizeof prior_icons)){char line[120];snprintf(line,sizeof line,"CRAFT_ICONS swap=%u restore=%u reject=%u\n",v[0],v[1],v[2]);logline(line);memcpy(prior_icons,v,sizeof prior_icons);}}
 {static uint32_t prior_install[13]{};mc195_craft_diag_snapshot now{};mc195_craft_diag_read(&now);if(memcmp(prior_install,now.value+19,sizeof prior_install)){char line[320];auto *v=now.value+19;snprintf(line,sizeof line,"CRAFT_INSTALL map=%u read=%u lifeplan=%u travplan=%u navplan=%u filterplan=%u typeplan=%u rx=%u runtime=%u life=%u barrier=%u tx=%u arm=%u\n",v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7],v[8],v[9],v[10],v[11],v[12]);logline(line);memcpy(prior_install,v,sizeof prior_install);}}
 (void)mc195_ore_probe_log_once(logline);
 static unsigned prior_declines[8]{};unsigned declines[8]{};bool decline_changed=false;
 for(unsigned i=0;i<8;++i){declines[i]=mc195_debug_view_declines(i);if(declines[i]!=prior_declines[i])decline_changed=true;}
 if(decline_changed){char line[200];snprintf(line,sizeof line,"DEBUG_VIEW_DECLINE owner=%u client=%u lease=%u read=%u missing=%u ambiguous=%u revalidate=%u unlock=%u\n",declines[0],declines[1],declines[2],declines[3],declines[4],declines[5],declines[6],declines[7]);logline(line);__builtin_memcpy(prior_declines,declines,sizeof declines);}
 static unsigned old_tabs[4][4]{};
 for(unsigned slot=0;slot<4;++slot){unsigned current[4]{};bool changed=false;
  for(unsigned k=0;k<4;++k){current[k]=mc195_tabs_slot_stat(slot,k);if(current[k]!=old_tabs[slot][k])changed=true;}
  if(changed){char line[144];snprintf(line,sizeof line,"DEBUG_TABS slot=%u requested=%u opened=%u refused=%u retired=%u\n",slot,current[0],current[1],current[2],current[3]);logline(line);__builtin_memcpy(old_tabs[slot],current,sizeof current);}
 }
 static unsigned last_gre=0;
 static unsigned last_gre_ui=~0u;
 unsigned gui=__atomic_load_n(&gre_ui_state,__ATOMIC_ACQUIRE);
 if(gui!=last_gre_ui){char line[64];snprintf(line,sizeof line,"GRE_UI flags=%x\n",gui);logline(line);last_gre_ui=gui;}
 static unsigned last_barebones=0;
 unsigned barebones=mc195_barebones_gre_status();
 if(barebones!=last_barebones){char line[64];snprintf(line,sizeof line,"BARE_BONES status=%u\n",barebones);logline(line);last_barebones=barebones;}
 unsigned stage=0,load=0,publish=0,registration=0;
 unsigned gre=mc195_gre_component_status(&stage,&load,&publish,&registration);
 if(gre_enabled && gre>=2)content_state("gre-skins",gre==2?MC195_CONTENT_ACTIVE:MC195_CONTENT_FAILED);
 if(texture_enabled && barebones>=2)content_state("bare-bones",barebones==2?MC195_CONTENT_ACTIVE:barebones==5?MC195_CONTENT_INACTIVE:MC195_CONTENT_FAILED);
 for(unsigned i=3;i<5;++i){unsigned status=i==3?mc195_bb2016_gre_status():mc195_pixel2017_gre_status();
  if((java_selected&(8u<<i))&&(status>=2||(!status&&mc195_gre_title_operations_failed())))content_state(java_ids[i],status==2?MC195_CONTENT_ACTIVE:status==5?MC195_CONTENT_INACTIVE:MC195_CONTENT_FAILED);}
 for(unsigned i=0;i<4;++i){unsigned status=mc195_workshop_skin_status(i);unsigned j=i+5;
  if((java_selected&(8u<<j))&&(status>=2||(!status&&mc195_gre_title_operations_failed())))content_state(java_ids[j],status==2?MC195_CONTENT_ACTIVE:MC195_CONTENT_FAILED);}
 for(unsigned i=0;i<4;++i){unsigned status=mc195_skin_ports_status(i);unsigned j=i+11;
  if((java_selected&(8u<<j))&&(status>=2||(!status&&mc195_gre_title_operations_failed())))content_state(java_ids[j],status==2?MC195_CONTENT_ACTIVE:MC195_CONTENT_FAILED);}
 for(unsigned i=0;i<4;++i){unsigned status=mc195_modern_skin_ports_status(i);unsigned j=i+16;
  if((java_selected&(8u<<j))&&(status>=2||(!status&&mc195_gre_title_operations_failed())))content_state(java_ids[j],status==2?MC195_CONTENT_ACTIVE:MC195_CONTENT_FAILED);}
 /* Admission queues providers; registration status does not prove rendering. */
 {static unsigned previous[20],reported;
  for(unsigned i=0;i<20;++i)if(java_selected&(8u<<i)){
   unsigned value=i<3?mc195_java_attempt_status(i):i==3?mc195_bb2016_gre_status():i==4?mc195_pixel2017_gre_status():
    i<9?mc195_workshop_skin_status(i-5):i<11?mc195_java_attempt_status(i-6):
    i<15?mc195_skin_ports_status(i-11):i==15?mc195_java_attempt_status(5):mc195_modern_skin_ports_status(i-16);
   if(!(reported&(1u<<i))||previous[i]!=value){char line[160];snprintf(line,sizeof line,"PACK_STATUS id=%s status=%u\n",java_ids[i],value);logline(line);previous[i]=value;reported|=1u<<i;}
  }
  static unsigned workshop_previous[4]={~0u,~0u,~0u,~0u};
  for(unsigned i=0;i<4;++i)if(java_selected&(1u<<(8+i))){
   unsigned stage=mc195_workshop_skin_stage(i),busy=mc195_workshop_skin_busy_attempts(i),bucket=0;
   while(busy>1){busy>>=1;++bucket;}unsigned key=(stage<<8)|bucket;
   if(workshop_previous[i]!=key){char line[160];snprintf(line,sizeof line,"PACK_WORKSHOP id=%s stage=%u cache_busy=%u\n",java_ids[i+5],stage,mc195_workshop_skin_busy_attempts(i));logline(line);workshop_previous[i]=key;}
  }
 }
 if(gre!=last_gre){char line[144];snprintf(line,sizeof line,"GRE status=%u stage=%u load=%u publish=%u registration=%u\n",gre,stage,load,publish,registration);logline(line);last_gre=gre;}
 if(gre==0){
  mc195_storage_telemetry telemetry;__builtin_memset(&telemetry,0,sizeof telemetry);mc195_storage_telemetry_snapshot(&telemetry);
  static mc195_storage_telemetry old_telemetry{};static bool telemetry_reported=false;
  if(!telemetry_reported||__builtin_memcmp(&telemetry,&old_telemetry,sizeof telemetry)){
   char trace[256];snprintf(trace,sizeof trace,"GRE_ACTIVITY reason=%u request=%u return=%u join=%u depth=%u reqrc=%d joinrc=%d receiver=%p handle=%p previous=%p joined=%p\n",telemetry.reason,telemetry.requests,telemetry.returns,telemetry.joins,telemetry.pump_depth,telemetry.request_result,telemetry.join_result,(void*)telemetry.receiver,(void*)telemetry.request_handle,(void*)telemetry.previous_handle,(void*)telemetry.join_thread);logline(trace);old_telemetry=telemetry;telemetry_reported=true;
   if(telemetry.failure_captured){snprintf(trace,sizeof trace,"GRE_JOIN_FAILURE rc=%d depth=%u joined=%p handle=%p previous=%p\n",telemetry.failed_join_result,telemetry.failed_pump_depth,(void*)telemetry.failed_join_thread,(void*)telemetry.failed_request_handle,(void*)telemetry.failed_previous_handle);logline(trace);}
  }
  mc195_gre_wait_data sample,key;__builtin_memset(&sample,0,sizeof sample);
  mc195_gre_wait_snapshot(&sample);__builtin_memcpy(&key,&sample,sizeof key);
  /* These fields change during ordinary pumping and do not trigger logging. */
  key.pump_observations=0;key.inflight=0;key.pumps=0;
  static mc195_gre_wait_data previous{};static bool reported=false;
  if(!reported||__builtin_memcmp(&key,&previous,sizeof key)){
   char line[256];
   snprintf(line,sizeof line,"GRE_WAIT reason=%u tid=%x pump=%x store=%x other=%x,%x flags=%x observed=%u\n",sample.reason,sample.thread,sample.pump_tid,sample.store_tid,sample.pump_other_tid,sample.store_other_tid,sample.pump_flags,sample.pump_observations);logline(line);
   snprintf(line,sizeof line,"GRE_STORAGE actual=%p pump=%p other=%p request=%p armed=%u poison=%u outstanding=%u inflight=%u pumps=%u\n",(void*)sample.storage,(void*)sample.pump_storage,(void*)sample.other_storage,(void*)sample.request_receiver,sample.armed,sample.poisoned,sample.outstanding,sample.inflight,sample.pumps);logline(line);
   snprintf(line,sizeof line,"GRE_DLC valid=%u complete=%u pending=%u total=%u processed=%u\n",sample.dlc_valid,sample.dlc_complete,sample.dlc_pending,sample.dlc_total,sample.dlc_processed);logline(line);previous=key;reported=true;
  }
 }
 struct mods195_movie_stats movie{};mods195_private_movie_stats(&movie);
 static struct mods195_movie_stats old_movie{};
 if(__builtin_memcmp(&movie,&old_movie,sizeof movie)){
  char line[200];snprintf(line,sizeof line,"CREATIVE_MOVIE call=%u match=%u made=%u failed=%u yes=%u no=%u full=%u\n",movie.callsite,movie.matched,movie.created,movie.failed,movie.owned_yes,movie.owned_no,movie.exhausted);logline(line);old_movie=movie;
 }
 struct mods195_nav_stats nav{};mods195_nav_stats(&nav);
 static struct mods195_nav_stats old_nav{};
 if(__builtin_memcmp(&nav,&old_nav,sizeof nav)){
  char line[224];snprintf(line,sizeof line,"CREATIVE_NAV match=%u mismatch=%u entries=%u reentries=%u highlight_ok=%u highlight_failed=%u page_ok=%u page_failed=%u\n",nav.owner_matches,nav.owner_mismatches,nav.entries,nav.reentries,nav.highlight_ok,nav.highlight_failed,nav.page_ok,nav.page_failed);logline(line);old_nav=nav;
 }
 struct mods195_nav_observation observation{};
 for(unsigned i=0;i<8&&mods195_nav_observation_take(&observation);++i){
  char line[256];snprintf(line,sizeof line,"CREATIVE_EVENT kind=%u scene=%p caller=%p vt=%p menu=%p tid=%x pad=%d current=%d next=%d valid=%x\n",observation.kind,(void*)observation.scene,(void*)observation.caller,(void*)observation.vtable,(void*)observation.menu,(unsigned)observation.tid,observation.pad,observation.current,observation.next,observation.read_mask);logline(line);
 }
 static unsigned last_dropped=0;unsigned dropped=mods195_nav_observation_dropped();
 if(dropped!=last_dropped){char line[96];snprintf(line,sizeof line,"CREATIVE_EVENT dropped=%u\n",dropped);logline(line);last_dropped=dropped;}
 for(unsigned i=0;i<9;++i){
  struct mc195_texture_lookup_stats sample{};
  int ok=i==0?mc195_barebones_lookup_stats(&sample):i<4?mc195_java_lookup_stats(i-1,&sample):i==4?mc195_bb2016_lookup_stats(&sample):i==5?mc195_pixel2017_lookup_stats(&sample):mc195_java_lookup_stats(i-3,&sample);
  static struct mc195_texture_lookup_stats previous[9]{};
  if(ok && __builtin_memcmp(&sample,&previous[i],sizeof sample)){
   char line[256];snprintf(line,sizeof line,"TEXTURE_LOOKUP pack=%u load=%u stage=%u lookups=%u hits=%u native=%u misses=%u invalid=%u image=%u pack_image=%u fallback=%u unavailable=%u null=%u\n",i,sample.load_calls,sample.load_stage,sample.lookup_calls,sample.provider_hits,sample.native_hits,sample.misses,sample.invalid,sample.image_calls,sample.image_pack,sample.image_fallback,sample.image_unavailable,sample.image_null);logline(line);previous[i]=sample;
  }
 }
 lucky195_drop_stats lucky_sample{};static lucky195_drop_stats lucky_previous{};
 if(lucky195_block_drop_stats(&lucky_sample)&&memcmp(&lucky_sample,&lucky_previous,sizeof lucky_sample)){
  char line[280];snprintf(line,sizeof line,"LUCKY_BREAK state=%u pre=%u removed=%u loot=%u attempts=%u silk=%u duplicate_suppressed=%u rejected=%u reason=%u level_vt=%p\n",lucky_sample.state,lucky_sample.prebreaks,lucky_sample.postbreaks,lucky_sample.callbacks,lucky_sample.attempts,lucky_sample.silk_returns,lucky_sample.legacy_suppressed,lucky_sample.rejected,lucky_sample.last_reject,(void*)lucky_sample.last_level_vtable);logline(line);lucky_previous=lucky_sample;
 }
 struct steel195_retirement_stats retirement_sample{};steel195_retirement_stats(&retirement_sample);
 static struct steel195_retirement_stats retirement_previous{};
 if(__builtin_memcmp(&retirement_sample,&retirement_previous,sizeof retirement_sample)){
  char line[256];snprintf(line,sizeof line,"STEEL_RETIRE attempts=%u phase=%u atlas=%u unreadable=%u texture=%u busy=%u begin=%u\n",retirement_sample.attempts,retirement_sample.wrong_phase,retirement_sample.wrong_atlas,retirement_sample.unreadable,retirement_sample.wrong_texture,retirement_sample.busy,retirement_sample.begun);logline(line);
  snprintf(line,sizeof line,"STEEL_RETIRE_END thread=%u phase=%u atlas=%u unreadable=%u nonnull=%u confirmed=%u grow_missing=%u grow_thread=%u\n",retirement_sample.end_wrong_thread,retirement_sample.end_wrong_phase,retirement_sample.end_wrong_atlas,retirement_sample.end_unreadable,retirement_sample.end_not_null,retirement_sample.confirmed,retirement_sample.grow_no_witness,retirement_sample.grow_wrong_thread);logline(line);
  retirement_previous=retirement_sample;
 }
 for(unsigned i=0;i<6;++i){unsigned j=i<3?i:i<5?i+6:15;unsigned bit=8u<<j;if(!(java_admitted&bit))continue;
  unsigned status=mc195_java_attempt_status(i);
  if(status==2)content_state(java_ids[j],MC195_CONTENT_ACTIVE);
  else if(status>=3 || (status==0&&mc195_gre_title_operations_failed()))content_state(java_ids[j],MC195_CONTENT_FAILED);
 }
 mc195_terrain_status terrain_now{};mc195_terrain_snapshot(&terrain_now);
 static mc195_terrain_status terrain_previous{};
 if(memcmp(&terrain_now,&terrain_previous,sizeof terrain_now)){
  char line[320];
  snprintf(line,sizeof line,"SHARED_TERRAIN installed=%u poisoned=%u active=%u retired=%u first_error=%u lease=%u lease_error=%u wait=%u reload=%u reload_error=%u generation=%u image=%u width=%u height=%u\n",terrain_now.installed,terrain_now.poisoned,terrain_now.active,terrain_now.retired,terrain_now.first_error,terrain_now.lease_state,terrain_now.lease_error,terrain_now.wait_result,terrain_now.reload_stage,terrain_now.reload_error,terrain_now.generation,terrain_now.image_state,terrain_now.source_width,terrain_now.source_height);
  logline(line);terrain_previous=terrain_now;
 }
 {unsigned mask=0,running=0,probe=0;int poll=0;uint64_t pattern=0;
  static unsigned prev_mask=0,prev_running=0,prev_probe=0;static int prev_poll=0;static uint64_t prev_pattern=0;
  mc195_terrain_lease_detail(&mask,&running,&probe,&poll,&pattern);
  if(mask!=prev_mask||running!=prev_running||probe!=prev_probe||poll!=prev_poll||pattern!=prev_pattern){
   char line[160];snprintf(line,sizeof line,"TERRAIN_LEASE_DETAIL mask=%x running=%u probe=%u poll=%x pattern_lo=%x pattern_hi=%x\n",mask,running,probe,(unsigned)poll,(unsigned)pattern,(unsigned)(pattern>>32));logline(line);
   prev_mask=mask;prev_running=running;prev_probe=probe;prev_poll=poll;prev_pattern=pattern;
  }
 }
 {unsigned pending=0,done=0,refused=0,deferred=0;uintptr_t worker=0,owner=0;
  static unsigned previous[4]={~0u,~0u,~0u,~0u};static uintptr_t prior_worker,prior_owner;
  mc195_terrain_handoff_status(&pending,&done,&refused,&deferred,&worker,&owner);
  if(previous[0]!=pending||previous[1]!=done||previous[2]!=refused||previous[3]!=deferred||prior_worker!=worker||prior_owner!=owner){
   char line[180];snprintf(line,sizeof line,"TERRAIN_HANDOFF pending=%u done=%u refused=%u deferred=%u worker=%llu owner=%llu\n",pending,done,refused,deferred,(unsigned long long)worker,(unsigned long long)owner);logline(line);
   previous[0]=pending;previous[1]=done;previous[2]=refused;previous[3]=deferred;prior_worker=worker;prior_owner=owner;
  }
 }
 static unsigned last_owner=~0u,last_resource=~0u,last_creative=~0u,last_recipe=~0u,last_names=~0u;
 static unsigned resource_reported=~0u;
 steel195_tools_runtime_status tools_now{};steel195_tools_runtime_snapshot(&tools_now);
 static steel195_tools_runtime_status tools_previous{};
 if(memcmp(&tools_now,&tools_previous,sizeof tools_now)){char line[224];snprintf(line,sizeof line,"STEEL_TOOLS prepare=%u registry=%u names=%u recipes=%u creative=%u count=%u icons=%u generations=%u\n",tools_now.preparation,tools_now.registration,tools_now.names,tools_now.recipes,tools_now.creative,tools_now.count,tools_now.icons,tools_now.generations);logline(line);tools_previous=tools_now;}
 unsigned owner=steel195_owner_status(),resource=steel195_resource_status(),creative=steel195_creative_status();
 if(owner!=last_owner || resource!=last_resource || creative!=last_creative){
  char line[144];snprintf(line,sizeof line,"STEEL owner=%u resource=%u creative=%u\n",owner,resource,creative);
  logline(line);last_owner=owner;last_resource=resource;last_creative=creative;
 }
 unsigned recipe=steel195_content_recipe_status(),names=steel195_content_names_status();
 unsigned lucky_items=mc195_lucky_items_status();
 static unsigned last_lucky_items=~0u;
 if(lucky_items!=last_lucky_items){char line[112];snprintf(line,sizeof line,"LUCKY_ITEMS status=%u armed=%u resource=%u\n",lucky_items,(unsigned)mc195_lucky_only_items_armed(),resource);logline(line);last_lucky_items=lucky_items;}
 {static unsigned last_failure=0;unsigned provider=0;unsigned failure=mc195_custom_items_failure(&provider);
  if(failure&&failure!=last_failure){char line[112];snprintf(line,sizeof line,"ITEM_PROVIDER_FAILURE code=%u provider_index=%u\n",failure,provider);logline(line);last_failure=failure;}}
 if(lucky_enabled&&mc195_lucky_is_active()){
  if((mc195_shared_scope_failures(&shared_server_scope)&__atomic_load_n(&shared_lucky_bit,__ATOMIC_ACQUIRE))||lucky_items>=3 || (mc195_lucky_only_items_armed()&&resource==7))content_state("native-lucky-blocks",MC195_CONTENT_FAILED);
  else if(mc195_lucky_ready()&&mods195_category_status()==3&&__atomic_load_n(&creative_ui_ready,__ATOMIC_ACQUIRE)&&
    (!mc195_lucky_only_items_armed()||(mc195_lucky_only_owner()&&resource==6)))content_state("native-lucky-blocks",MC195_CONTENT_ACTIVE);
 }
 if(steel_enabled){
  if(__atomic_load_n(&creative_ui_ready,__ATOMIC_ACQUIRE)&&owner==3&&resource==6&&creative==3&&recipe==2&&names==2&&steel195_tools_runtime_ready())content_state("steel",MC195_CONTENT_ACTIVE);
  else if(owner>=4||resource==7||creative>=4||recipe>=3||names>=3||tools_now.registration>=3||tools_now.recipes>=3)content_state("steel",MC195_CONTENT_FAILED);
 }
 if(recipe!=last_recipe||names!=last_names){char line[96];snprintf(line,sizeof line,"STEEL_CONTENT recipe=%u names=%u\n",recipe,names);logline(line);last_recipe=recipe;last_names=names;}
 if(resource_reported!=resource && (resource==6||resource==7)){
  struct steel195_resource_diagnostic d{};
  if(steel195_resource_diagnostic(&d)==1){
   char line[256];
   snprintf(line,sizeof line,"STEEL_RESOURCE reason=%u count=%u current=%u sample=%u index=%u grown=%u atlas=%p source=%p prior=%p icon=%p vt=%p\n",d.reason,d.count,d.current_count,d.sample_state,d.index,d.grown_state,(void*)d.atlas,(void*)d.source,(void*)d.prior_texture,(void*)d.observed_icon,(void*)d.observed_vtable);logline(line);
   snprintf(line,sizeof line,"STEEL_IMAGE width=%u height=%u flag=%u readable=%u mip0=%p mip1=%p mip2=%p mip3=%p mip4=%p\n",d.width,d.height,d.flag,d.source_readable,(void*)d.mip[0],(void*)d.mip[1],(void*)d.mip[2],(void*)d.mip[3],(void*)d.mip[4]);logline(line);
   snprintf(line,sizeof line,"STEEL_IMAGE mip5=%p mip6=%p mip7=%p mip8=%p mip9=%p uv=%x,%x,%x,%x,%x,%x\n",(void*)d.mip[5],(void*)d.mip[6],(void*)d.mip[7],(void*)d.mip[8],(void*)d.mip[9],d.uv_bits[0],d.uv_bits[1],d.uv_bits[2],d.uv_bits[3],d.uv_bits[4],d.uv_bits[5]);logline(line);
   snprintf(line,sizeof line,"STEEL_BIND fallback=%p ingot=%p pickaxe=%p\n",(void*)d.fallback,(void*)d.ingot_icon,(void*)d.pickaxe_icon);logline(line);
   snprintf(line,sizeof line,"STEEL_VTABLES count=%u overflow=%u\n",d.vtable_count,d.vtable_overflow);logline(line);
   for(unsigned i=0;i<d.vtable_count && i<20;i++){
    snprintf(line,sizeof line,"STEEL_VTABLE row=%u vt=%p count=%u first=%u\n",i,(void*)d.vtables[i],d.vtable_counts[i],d.vtable_first[i]);logline(line);
   }
   resource_reported=resource;
  }
 }
 if(__atomic_exchange_n(&storage_ready,0u,__ATOMIC_ACQ_REL)){
  auto s=storage_snapshot;char line[192];
  snprintf(line,sizeof line,"storage flags=%x tid=%x object=%p vtable=%p entry=%p\n",s.flags,s.thread,(void*)s.storage,(void*)s.vtable,(void*)s.entry);logline(line);
 }
 mc195_offline_observation s{};
 if(mc195_offline_observer_take_snapshot(&s)){
  char line[320];snprintf(line,sizeof line,
   "offline flags=%x tid=%x owner=%x later_owner=%x scene=%p ui=%p callback=%p caller=%p pad=%u options=%x,%x action=%x\n",
   s.flags,s.thread,s.owner_thread,mc195_debug_owner_thread(),(void*)s.scene,(void*)s.controller,(void*)s.callback,(void*)s.return_address,
   s.pad,s.options[0],s.options[1],s.action);logline(line);
 }
 return mc195_menu_candidate_take_events()|(mc195_menu_label_take_events()<<16)|(mc195_offline_observer_take_events()<<24);
}
}
extern "C" __attribute__((visibility("hidden"))) uintptr_t mc195_super_expected_base(){return __atomic_load_n(&attested_base,__ATOMIC_ACQUIRE);}
static uintptr_t lucky_custom_base;
static uintptr_t lucky_custom_block(void*){lucky195_block_native_status status{};return lucky195_block_native_snapshot(&status)&&status.state==5?status.block:0;}
static void lucky_custom_completed(void*,int result,uintptr_t block,uintptr_t previous,uintptr_t next){
 if(result==1){auto custom=mc195_custom_startup_integration();mc195_custom_redstone_config rc{lucky_custom_base,block,next,mc195_custom_integration_original_neighbor(custom),nullptr,mc195_lucky_candidate_attested,mc195_lucky_candidate_read,mc195_lucky_candidate_thread,mc195_lucky_candidate_session};
  if(mc195_custom_redstone_start(&rc)!=1||lucky195_block_drops_rebind(block,previous,next,(uintptr_t)mc195_custom_redstone_neighbor)!=1)result=-1;
 }
 char line[128];snprintf(line,sizeof line,"LUCKY_CUSTOM_BE result=%d redstone=neighbor payload=native_saved\n",result);logline(line);
 if(result!=1)content_state("native-lucky-blocks",MC195_CONTENT_FAILED);
}
EXPORT int plugin_load(int argc,const char **argv){
 unsigned expected=0;if(!__atomic_compare_exchange_n(&state,&expected,1u,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return 0;
 lce_perf_boot_init(); /* perf.ini (must precede the first pristine-image check), first BOOT_MS mark */
 struct PerfEnd{~PerfEnd(){lce_perf_mark("plugin_load_end");lce_perf_flush();}} perf_end;
 logline("BEGIN combined 1.95 candidate r26 event repairs, complete structure placement, rainbow sheep and named chat outcomes\n");uintptr_t base=0;
 if(!mc195_ps4_attest(&base)||!base){logline("ABORT image attestation\n");__atomic_store_n(&state,2u,__ATOMIC_RELEASE);return 0;}
 __atomic_store_n(&attested_base,base,__ATOMIC_RELEASE);
 lce_perf_mark("image_attested");
 mc195_frontend_diag_base(base);
 if(!mc195_menu_candidate_prepare(base)||!mc195_menu_label_prepare(base)||!mc195_debug_bind_ui_tick(ui_tick)){
  logline("ABORT shared UI preparation\n");__atomic_store_n(&state,2u,__ATOMIC_RELEASE);return 0;}
 /* Prepare the repeat relay while the full original image is still pristine.
    No dialog is changed unless the later publication succeeds. */
 int offline_ready=mc195_offline_once_prepare(base);
 if(!offline_ready)logline("Offline repeat preparation unavailable; stock prompt retained\n");
 /* Native pad's pristine digest must precede every RX patch. Even a failure
    can leave protection or module state uncertain, so remain resident. */
 __atomic_store_n(&state,3u,__ATOMIC_RELEASE);
 if(!mc195_map_worker_start(base,logline))logline("World import worker unavailable\n");
 if(install_native_pad()!=1){logline("ABORT input setup; resident\n");return 0;}
 /* The native pad's check was the last pristine-image verification: drop the cached copy,
  * then time the retail add-on mounts / capture the video handle (import slots, data only). */
 lce_perf_rx_release();lce_perf_mark("native_pad_installed");lce_perf_boot_hooks(base);
 int nbt_start=mc195_nbt_runtime_start(base);
 if(nbt_start<0){logline("ABORT NBT lifecycle uncertain; resident\n");return 0;}
 logline(nbt_start==1?"NBT read-only lifecycle installed\n":"NBT read-only lifecycle unavailable\n");
 logline(mc195_nbt_save_status()==2u?"NBT stock save request installed\n":"NBT stock save request unavailable\n");
 logline("GRE storage monitor: early isolated launch assumed, not runtime-proven\n");
 int content_monitor=mc195_gre_component_start(base,1);
 if(content_monitor<0){logline("ABORT Gre storage observation uncertain; resident\n");return 0;}
 content_boundary_ready=content_monitor==1;
 if(!content_boundary_ready)logline("Content storage unavailable; Steel/UI continue\n");
 lce_perf_mark("content_monitor");
 const unsigned poll_step=lce_perf_poll_step_us(); /* 2 ms (fast_poll) or the old 50 ms; same 5 s / 2 s limits */
 if(!scePthreadCreate(&java_probe_worker,nullptr,java_preflight,nullptr,"mc195_java_check")){
  for(unsigned waited=0;waited<5000000u&&__atomic_load_n(&java_probe_done,__ATOMIC_ACQUIRE)!=32767u;waited+=poll_step)sceKernelUsleep(poll_step);
  java_eligible=__atomic_load_n(&java_probe_mask,__ATOMIC_RELAXED);
 }else mc195_maps_scan(nullptr);
 {char line[112];snprintf(line,sizeof line,"JAVA_PREFLIGHT eligible=%x completed=%u\n",java_eligible,__atomic_load_n(&java_probe_done,__ATOMIC_ACQUIRE));logline(line);}
 lce_perf_mark("java_preflight");
 const char *ids[24]={"steel","gre-skins","bare-bones"};unsigned ids_count=3;
 ids[ids_count++]="native-lucky-blocks";
 for(unsigned i=0;i<20;++i)if(java_eligible&(8u<<i))ids[ids_count++]=java_ids[i];
 if(mc195_selection_worker_start_configured(ids,ids_count)){
  for(unsigned waited=0;waited<2000000u && mc195_selection_worker_status()==MC195_SELECTION_WORKER_STARTING;waited+=poll_step)sceKernelUsleep(poll_step);
  lce_perf_mark("selection_worker");
  if(mc195_selection_worker_status()==MC195_SELECTION_WORKER_READY){
   choices_ready=mc195_selection_worker_startup_choice("steel",&steel_enabled)&&
    mc195_selection_worker_startup_choice("gre-skins",&gre_enabled)&&
    mc195_selection_worker_startup_choice("bare-bones",&texture_enabled)&&
    mc195_selection_worker_startup_choice("native-lucky-blocks",&lucky_enabled);
   for(unsigned i=0;i<20&&choices_ready;++i)if(java_eligible&(8u<<i)){
    int choice=0;choices_ready=mc195_selection_worker_startup_choice(java_ids[i],&choice);
    if(choice)java_selected|=8u<<i;
   }
   if(!choices_ready){steel_enabled=gre_enabled=texture_enabled=lucky_enabled=0;java_selected=0;}
  }
 }
 unsigned content_mask=(steel_enabled?MC195_NOTICE_STEEL:0u)|
  (gre_enabled?MC195_NOTICE_GRE:0u)|(texture_enabled?MC195_NOTICE_TEXTURE:0u);
 if(!mc195_ps4_mods_notice_start(content_mask | java_selected | (lucky_enabled?(1u<<23):0u)))logline("PS4 Mods notification worker unavailable\n");
 content_state("native-lucky-blocks",lucky_enabled?MC195_CONTENT_STARTING:MC195_CONTENT_INACTIVE);
 content_state("steel",steel_enabled?MC195_CONTENT_STARTING:MC195_CONTENT_INACTIVE);
 content_state("gre-skins",gre_enabled?MC195_CONTENT_STARTING:MC195_CONTENT_INACTIVE);
 content_state("bare-bones",texture_enabled?MC195_CONTENT_STARTING:MC195_CONTENT_INACTIVE);
 {char line[128];snprintf(line,sizeof line,"CONTENT_CHOICES steel=%u gre=%u textures=%u\n",(unsigned)steel_enabled,(unsigned)gre_enabled,(unsigned)texture_enabled);logline(line);}
 if(!mc195_gre_set_skin_enabled(gre_enabled)){mc195_content_notices_fail(content_mask);logline("ABORT content choice configuration\n");fail_selected_ports();return 0;}
 for(unsigned i=0;i<20;++i)content_state(java_ids[i],(java_selected&(8u<<i))?MC195_CONTENT_STARTING:MC195_CONTENT_INACTIVE);
 if(!content_boundary_ready){
  for(unsigned i=0;i<20;++i)if(java_selected&(8u<<i))content_state(java_ids[i],MC195_CONTENT_FAILED);
  if(gre_enabled)content_state("gre-skins",MC195_CONTENT_FAILED);
  if(texture_enabled)content_state("bare-bones",MC195_CONTENT_FAILED);
 }
 if((gre_enabled||texture_enabled||java_selected)&&content_boundary_ready){
  int root_guard=mc195_barebones_root_guard_start(base,1);
  if(root_guard<0){mc195_content_notices_fail(content_mask);logline("ABORT content root guard uncertain; resident\n");fail_selected_ports();return 0;}
  if(!root_guard){logline("Content root guard unavailable; packs stay disabled\n");content_boundary_ready=0;}
  if(content_boundary_ready){
  int manager_guard=mc195_barebones_manager_guard_start(base,1);
  if(manager_guard<0){mc195_content_notices_fail(content_mask);logline("ABORT content manager guard uncertain; resident\n");fail_selected_ports();return 0;}
  if(!manager_guard){logline("Content manager guard unavailable; packs stay disabled\n");content_boundary_ready=0;}
  }
  if(!content_boundary_ready){
   for(unsigned i=0;i<20;++i)if(java_selected&(8u<<i))content_state(java_ids[i],MC195_CONTENT_FAILED);
   if(gre_enabled)content_state("gre-skins",MC195_CONTENT_FAILED);
   if(texture_enabled)content_state("bare-bones",MC195_CONTENT_FAILED);
  }
 }
 unsigned texture_bound=0;
 if(texture_enabled&&content_boundary_ready){
  if(!mc195_barebones_gre_bind(base)){
   content_state("bare-bones",MC195_CONTENT_FAILED);
   logline("Bare Bones owner binding unavailable; Gre/UI continue\n");
  }else texture_bound=1;
 }
 if(java_selected&&content_boundary_ready){
  java_admitted=mc195_java_bind(base,java_selected&0x43038u,java_eligible&0x43038u);
  if((java_selected&64u)&&mc195_bb2016_gre_bind(base))java_admitted|=64u;
  if((java_selected&128u)&&mc195_pixel2017_gre_bind(base))java_admitted|=128u;
  if((java_selected&0xf00u)&&mc195_workshop_skins_bind(base,(java_selected>>8)&15u))java_admitted|=java_selected&0xf00u;
  if((java_selected&0x3c000u)&&mc195_skin_ports_bind(base,(java_selected>>14)&15u))java_admitted|=java_selected&0x3c000u;
  if((java_selected&0x780000u)&&mc195_modern_skin_ports_bind(base,(java_selected>>19)&15u))java_admitted|=java_selected&0x780000u;
  for(unsigned i=0;i<20;++i)if((java_selected&(8u<<i))&&!(java_admitted&(8u<<i)))content_state(java_ids[i],MC195_CONTENT_FAILED);
 }
 {char line[96];snprintf(line,sizeof line,"JAVA_CHOICE eligible=%x selected=%x admitted=%x\n",java_eligible,java_selected,java_admitted);logline(line);}
 lce_perf_mark("content_binds");
 if(java_selected)mc195_port_notices_arm();
 lce_perf_mark("sdk_phase");
 int enabled=steel_enabled,lucky_items_ready=0;
 if(!enabled)logline("Steel disabled or selection unavailable; Gre/UI continue\n");
 int lucky_items_configured=0;
 if(lucky_enabled){
  lucky_items_configured=mc195_diagnostic_items_configure(base)==1;
  logline(lucky_items_configured?"Lucky items selected; sword events limited; bow and potion effects inactive\n":"Lucky item registration configuration failed\n");
 }
 if(enabled){
  int tools_ready=steel195_tools_runtime_prepare(base,1);
  if(mc195_custom_items_status()==4)logline("Lucky diagnostic reservation failed; Steel continues without diagnostic items\n");
  if(tools_ready!=1){logline("Steel expansion preparation failed; Steel hooks disabled\n");enabled=0;}
  int owner_ready=enabled?steel195_owner_prepare(base,1):0;
  if(owner_ready!=1){logline("Steel owner preparation failed; Gre/UI continue\n");enabled=0;}
  if(enabled && steel195_creative_prepare(base)!=1){logline("Steel Creative preparation failed; Gre/UI continue\n");enabled=0;}
  if(enabled && steel195_content_prepare(base)!=1){logline("Steel content preparation failed; Gre/UI continue\n");enabled=0;}
 }else if(lucky_items_configured){
  int ready=steel195_tools_runtime_prepare(base,0);
  lucky_items_ready=ready==1&&mc195_custom_items_status()==1&&mc195_custom_items_count()>0;
  if(ready!=1){logline("ABORT Lucky item preparation incomplete or uncertain; resident\n");return 0;}
  if(!lucky_items_ready)logline("Lucky custom item preparation unavailable; mapped blocks continue\n");
 }
 if(steel_enabled&&!enabled&&mc195_custom_items_status()==1){
  logline("ABORT prepared custom items cannot use failed Steel owner; resident\n");return 0;
 }
 if(enabled||lucky_items_ready){
  int retirement=steel195_retirement_hook_start(base);
  if(retirement!=1){
   if(enabled)content_state("steel",MC195_CONTENT_FAILED);
   if(retirement<0){mc195_content_notices_fail(content_mask);logline("ABORT item atlas retirement publication uncertain; resident\n");fail_selected_ports();return 0;}
   if(lucky_items_ready){logline("ABORT Lucky item atlas retirement unavailable; resident\n");return 0;}
   logline("Steel retirement hook unavailable; Steel hooks disabled\n");enabled=0;
  }else if(lucky_items_ready&&!mc195_lucky_only_arm_items(base)){
   logline("ABORT Lucky item publication gate unavailable; resident\n");return 0;
  }
 }
 if(lucky_enabled){mc195_lucky_catalogue_info info{};if(mc195_lucky_catalogue_describe(&info)!=1){content_state("native-lucky-blocks",MC195_CONTENT_FAILED);lucky_enabled=0;logline("Lucky catalogue metadata mismatch; event provider disabled\n");}else{char line[240];snprintf(line,sizeof line,"LUCKY_CATALOGUE block_outcomes=%u upstream=%u expansion=%u other_item_outcomes=%u sha256=%s runtime_acceptance=pending\n",info.block_outcomes,info.upstream_blocks,info.expansion_blocks,info.other_item_outcomes,info.expansion_sha256);logline(line);}}
 // One retained-player hook owner serves the reviewed debug and mod callbacks.
 // Give has its own value identity and opts into its reviewed paused branch.
 lce_perf_mark("items_prepared");
 const bool debug_authority_ready=mc195_nbt_runtime_status()==2u;
 if(debug_authority_ready||lucky_enabled){
  bool prepared=false;const bool lucky_requested=lucky_enabled!=0;
  if(lucky_enabled){
   mc195_lucky_native_config lucky{base,nullptr,mc195_lucky_candidate_attested,mc195_lucky_candidate_read,mc195_lucky_candidate_thread,mc195_lucky_candidate_session,lucky_native_report};
   if(mc195_lucky_native_prepare(&lucky,1)!=1){content_state("native-lucky-blocks",MC195_CONTENT_FAILED);logline("Lucky event owner preparation failed; shared scope not started\n");lucky_enabled=0;}
   else prepared=true;
  }
  // Do not retry with a different hook configuration after an attempted Lucky
  // owner failed: its event-provider startup may have been partly published.
  if(lucky_requested&&!prepared){
   logline("F3 scope unavailable after Lucky owner preparation failure\n");
  }else{
   mc195_lucky_server_config config{base,nullptr,mc195_lucky_candidate_attested,mc195_lucky_candidate_read,mc195_lucky_candidate_thread,mc195_lucky_candidate_session,nullptr,nullptr};
   Mc195ScopeSubscriber subscribers[MC195_SHARED_SCOPE_SUBSCRIBERS]{};unsigned count=0;
   if(debug_authority_ready)subscribers[count++]={nullptr,mc195_f3_shared_tick,nullptr,mc195_f3_shared_failed};
   if(prepared){__atomic_store_n(&shared_lucky_bit,1u<<count,__ATOMIC_RELEASE);subscribers[count++]={nullptr,mc195_lucky_native_scope_tick,mc195_lucky_native_scope_finish,mc195_lucky_shared_failed};}
   if(debug_authority_ready&&debug_difficulty_enabled)subscribers[count++]={nullptr,mc195_debug_difficulty_runtime_server_tick,nullptr,mc195_difficulty_shared_failed};
   if(debug_authority_ready)subscribers[count++]={nullptr,mc195_nbt_mode_runtime_server_tick,nullptr,mc195_mode_shared_failed,true};
   if(debug_authority_ready)subscribers[count++]={nullptr,mc195_debug_give_runtime_server_tick,nullptr,mc195_give_shared_failed,true};
   /* NBT editor in local worlds: reviewed for the paused loop (the debug menu pauses the game), like give/mode. */
   if(debug_authority_ready&&count<MC195_SHARED_SCOPE_SUBSCRIBERS)subscribers[count++]={nullptr,lce_nbt_local_scope_tick,nullptr,lce_nbt_local_scope_failed,true};
   int rc=mc195_shared_scope_start(&shared_server_scope,&config,subscribers,count,1);
   if(rc==1){if(debug_authority_ready){mc195_f3_scope_installed();mc195_nbt_mode_runtime_scope_installed();mc195_debug_give_runtime_scope_installed();}if(prepared)mc195_lucky_native_scope_activate();if(debug_authority_ready&&debug_difficulty_enabled)mc195_debug_difficulty_runtime_scope_installed();logline("Shared retained-player scope installed for F3 and selected mods\n");}
   else if(rc<0){if(prepared)content_state("native-lucky-blocks",MC195_CONTENT_FAILED);logline("ABORT shared scope publication uncertain; resident\n");return 0;}
   else {if(prepared){content_state("native-lucky-blocks",MC195_CONTENT_FAILED);lucky_enabled=0;}logline("Shared retained-player scope unavailable\n");}
  }
 }
 if(lucky_enabled){
  int giant=mc195_giant_texture_install(base,1);
  logline(giant==1?"Lucky giant zombie texture installed\n":"Lucky giant zombie texture installation failed\n");
  if(giant<0){content_state("native-lucky-blocks",MC195_CONTENT_FAILED);return 0;}
  int rainbow=mc195_rainbow_install(base,1);
  logline(rainbow==1?"Lucky rainbow renderer installed\n":"Lucky rainbow renderer installation failed\n");
  if(rainbow<0){content_state("native-lucky-blocks",MC195_CONTENT_FAILED);return 0;}
  int chat_colour=mc195_lucky_chat_colour_install(base,1);
  logline(chat_colour==1?"Lucky chat colour renderer installed\n":"Lucky chat colour renderer unavailable\n");
  if(chat_colour<0){content_state("native-lucky-blocks",MC195_CONTENT_FAILED);return 0;}
 }
 if(lucky_enabled){lucky_custom_base=base;mc195_custom_startup_config custom{base,(uintptr_t)mc195_custom_redstone_neighbor,nullptr,mc195_lucky_candidate_attested,mc195_lucky_candidate_read,mc195_lucky_candidate_thread,lucky_custom_block,lucky_custom_completed};int rc=mc195_custom_startup_prepare(&custom,1);if(rc!=1){content_state("native-lucky-blocks",MC195_CONTENT_FAILED);logline("ABORT Lucky custom BE startup relay unavailable; resident\n");return 0;}}
 int resource_ready=0;
 if(lucky_enabled||enabled){
  int terrain=mc195_terrain_install(base,1,0);
  if(terrain<0){content_state("native-lucky-blocks",MC195_CONTENT_FAILED);logline("ABORT terrain publication uncertain; resident\n");return 0;}
  if(terrain==1){int r=steel195_resource_hooks_start(base);if(r<0){logline("ABORT resource relay publication uncertain; resident\n");return 0;}if(r==1){resource_ready=1;mc195_growth_relay_mark_ready();}}
  int prepared=resource_ready?mc195_lucky_prepare(base,enabled,lucky_enabled):0;
  int ore_prepared=prepared==1&&enabled?mc195_steel_ore_prepare(base):1;
  int hook=prepared==1&&ore_prepared==1?mc195_lucky_hook_start(base):0;
  if(hook<0){content_state("native-lucky-blocks",MC195_CONTENT_FAILED);logline("ABORT Block relay publication uncertain; resident\n");return 0;}
  if(hook!=1){if(lucky_enabled)content_state("native-lucky-blocks",MC195_CONTENT_FAILED);logline("Mapped block initialization unavailable\n");}
  else if(!enabled&&steel195_creative_prepare(base)!=1){content_state("native-lucky-blocks",MC195_CONTENT_FAILED);logline("ABORT Lucky Creative preparation unavailable; resident\n");return 0;}
 }
 int gameplay_enabled=enabled||mc195_lucky_is_active();
 if(steel_enabled&&!enabled)content_state("steel",MC195_CONTENT_FAILED);
 if(enabled&&!resource_ready){
 int resource=steel195_resource_hooks_start(base);
 if(resource==1){resource_ready=1;mc195_growth_relay_mark_ready();}
 if(resource!=1){mc195_content_notices_fail(content_mask);char line[96];snprintf(line,sizeof line,"ABORT Steel resource hooks=%d; resident\n",resource);logline(line);fail_selected_ports();return 0;}
 }
 if(gameplay_enabled){
 if(!steel195_content_register_crafting(enabled,mc195_lucky_is_active()?1:0)){logline("ABORT recipe provider configuration failed; resident\n");return 0;}
 /* Publish the craft activation base before publishing the recipe-owner
  * hook. Its one-shot callback can run immediately on another native thread.
  * Runtime paths remain inert until the movie hook/ownership is available. */
 int craft_tabs=mc195_craft_candidate_start(base);
 if(craft_tabs!=1)logline("Crafting tab base unavailable; native fallback\n");
 int creative=steel195_creative_hook_start(base);
 if(creative!=1){mc195_content_notices_fail(content_mask);char line[96];snprintf(line,sizeof line,"ABORT Steel creative hook=%d; resident\n",creative);logline(line);fail_selected_ports();return 0;}
 int craft=steel195_craft_hook_start(base);
 if(craft!=1){mc195_content_notices_fail(content_mask);char line[96];snprintf(line,sizeof line,"ABORT Steel crafting hook=%d; resident\n",craft);logline(line);fail_selected_ports();return 0;}
 int registry=steel195_registry_observer_start(base,logline);
 if(registry<0){mc195_content_notices_fail(content_mask);logline("ABORT registry observer uncertain; resident\n");fail_selected_ports();return 0;}
 if(!registry)logline("Registry observer unavailable; continuing UI\n");
 int ore_probe=0;
 if(mc195_ore_probe_prepare(base))ore_probe=mc195_ore_probe_hook_start(base);
 {char line[112];snprintf(line,sizeof line,"ORE_PROBE_ARM hook=%d phase=%u observation_only=1\n",ore_probe,mc195_ore_probe_phase());logline(line);}
 if(ore_probe<0){mc195_content_notices_fail(content_mask);logline("ABORT diagnostic call publication uncertain; resident\n");fail_selected_ports();return 0;}
 int trace=steel195_trace_start(base,logline);
 if(trace<0){mc195_content_notices_fail(content_mask);logline("ABORT startup trace partial/uncertain; resident\n");fail_selected_ports();return 0;}
 if(trace!=1){mc195_content_notices_fail(content_mask);logline("ABORT Steel startup trace unavailable; resident\n");fail_selected_ports();return 0;}
 }
 if(content_mask||!java_selected)mc195_content_notices_arm();
 lce_perf_mark("mod_hooks_installed");
 (void)mc195_debug_load(argc,argv);
 lce_perf_mark("debug_plugin_loaded");
 if(!mc195_debug_ui_ready()){mc195_content_notices_fail(content_mask&6u);logline("ABORT debug UI not ready; Store not attempted\n");fail_selected_ports();return 0;}
 if(gameplay_enabled){
  int movie=mods195_private_movie_start(base,1);
  if(movie<0){mc195_content_notices_fail(content_mask);logline("ABORT Creative movie publication uncertain; resident\n");fail_selected_ports();return 0;}
  if(!movie)logline("Creative private movie unavailable; stock-FUI fallback\n");
  int nav=mods195_nav_start(base,1);
  char line[128];snprintf(line,sizeof line,"CREATIVE_UI movie=%d navigation=%d; isolated startup assumed\n",movie,nav);logline(line);
  if(nav==1)__atomic_store_n(&creative_ui_ready,1u,__ATOMIC_RELEASE);
  else {content_state("steel",MC195_CONTENT_FAILED);if(nav<0){logline("ABORT Creative hook publication uncertain; resident\n");fail_selected_ports();return 0;}}
 }
 unsigned applied=(enabled&&__atomic_load_n(&creative_ui_ready,__ATOMIC_ACQUIRE)?1u:0u)|
  (gre_enabled&&content_boundary_ready?2u:0u)|(texture_bound?4u:0u)|java_admitted|
  (lucky_enabled&&mc195_lucky_is_active()&&__atomic_load_n(&creative_ui_ready,__ATOMIC_ACQUIRE)?0x800000u:0u);
 if(!mc195_selection_worker_record_startup_applied_mask(applied)){
  logline("ABORT applied content snapshot; resident\n");fail_selected_ports();return 0;
 }
 if(!mc195_store_probe_set_event_drain(events)){logline("ABORT event telemetry\n");fail_selected_ports();return 0;}
 int result=mc195_store_probe_install_action(base,store_event,logline);
 if(result==0)logline("ABORT Store action not installed\n");
 else if(result==2||result==3)logline("Store publication protection restoration failed; resident\n");
 else {
  logline("Combined UI hooks installed; visible behavior unverified\n");
  if(offline_ready){
   int offline=mc195_offline_once_publish();
   if(offline==1)logline("Offline repeat relay published; callback and scene checks experimental\n");
   else if(offline==2)logline("Offline repeat publication protection uncertain; resident\n");
   else logline("Offline repeat publication unavailable; stock prompt retained\n");
  }
 }
 return 0;
}
EXPORT int plugin_unload(int,const char **){unsigned s=__atomic_load_n(&state,__ATOMIC_ACQUIRE);return s==1||s==3?-1:0;}
extern "C" __attribute__((visibility("hidden"))) int module_start(size_t,const void *){return 0;}
extern "C" __attribute__((visibility("hidden"))) int module_stop(size_t,const void *){return plugin_unload(0,nullptr);}

// Called exclusively by the NBT Controller on its serialized UI owner thread.
// The worker never reads UI Session state; only bounded value diagnostics leave it.
extern "C" void mc195_nbt_log_edit_gate(unsigned slot,const mc195_debug_pad_ticket *pad,uint64_t world,unsigned field,unsigned reason){
 if(slot>=4||!pad)return;
 unsigned index=field==MC195_NBT_SCALAR_XP?0:field==MC195_NBT_SCALAR_FOOD?1:field==MC195_NBT_SCALAR_AIR?2:field==MC195_NBT_SCALAR_HEALTH?3:4;if(index>=4)return;
 struct Last {bool seen=false;unsigned reason=0,fields=0,session=0,lines=0;uint64_t time=0,generation=0;};
 static Last prior[4][4];Last &last=prior[slot][index];
 Mc195NbtScalarRuntimeDiagnostic d{};if(!mc195_nbt_player_runtime_diagnostic(slot,pad,world,&d))return;
 uint64_t time=sceKernelGetProcessTime();
 if(last.lines>=64||(last.seen&&last.generation==pad->generation&&last.reason==reason&&last.fields==d.fields&&last.session==d.session_state)||
    (last.time&&time>=last.time&&time-last.time<1000000))return;
 char line[320];snprintf(line,sizeof line,"NBT_EDIT pad=%u field=%u reason=%u scope=%u session=%u identity=%u requested=%u capabilities=%u xp_reason=%u age_us=%zu ticket=%u completion=%u host_privileges=not_required\n",
  slot,field,reason,d.scope_state,d.session_state,d.identity_ready,d.requested,d.fields,d.xp_reason,(size_t)d.age_us,d.has_ticket,d.completion);
 logline(line);last.seen=true;last.reason=reason;last.fields=d.fields;last.session=d.session_state;last.time=time;last.generation=pad->generation;++last.lines;
}
