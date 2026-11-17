extern "C" void mc195_crashlog_rearm(void);
extern "C" void mc195_crashlog_exceptions(void);
extern "C" void mc195_crashlog_thread(void);
#include "/home/codey/minecraft-lce-work/r72-session-candidate/native-debug-plugin/menu_diagnostics.hpp"
#include "runtime.h"
#if defined(MC_LEGACY195)
#include "/home/codey/minecraft-lce-work/r72-session-candidate/loader/native_threeway_repeat_candidate/source/legacy195/dialog_localize.h"
#endif
#if defined(MC_SUPER195)
#include "/home/codey/minecraft-lce-work/r72-session-candidate/loader/super_195_candidate/shared_tick_195.h"
#include "/home/codey/minecraft-lce-work/r72-session-candidate/loader/super_195_candidate/dialog_gate_195.h"
extern "C" uintptr_t mc195_super_expected_base();
#endif
#include "/home/codey/minecraft-lce-work/r72-session-candidate/native-debug-plugin/cooperative-upgrade/loader/legacy_signature.hpp"
#include "native_menu.hpp"
#include "tab_site_guard.hpp"
#include "monitor_mailbox.hpp"
#if defined(MC_LEGACY195) && defined(MC_SUPER195)
#include "/home/codey/minecraft-lce-work/ps4-transport/session_runtime.hpp"
#include "/home/codey/minecraft-lce-work/ps4-transport/native_keyboard.hpp"
#include "/home/codey/minecraft-lce-work/ps4-transport/account_flow.hpp"
#include "/home/codey/minecraft-lce-work/ps4-transport/client_report.h"
#include "/home/codey/minecraft-lce-work/ps4-transport/account_join_route.hpp"
#include "/home/codey/minecraft-lce-work/ps4-transport/orbis_auth.h"
#include "/home/codey/minecraft-lce-work/ps4-transport/account_config.hpp"
#include "/home/codey/minecraft-lce-work/ps4-transport/selection_lookup.hpp"
#include "/home/codey/minecraft-lce-work/ps4-transport/login_gui.hpp"
#if defined(MC_METADATA_SNAPSHOT)
#include "/home/codey/minecraft-lce-work/ps4-transport/native_metadata_snapshot.h"
#include "/home/codey/minecraft-lce-work/ps4-metadata-snapshot/item-audit-build/native_metadata_snapshot_extended.h"
#endif
#include <fcntl.h>
#endif
#if defined(MC_COOPERATIVE_TABS)
#include "tab_cooperation.hpp"
#if defined(MC_LEGACY195) && defined(MC_SUPER195)
extern "C" void mc195_tabs_sessions_tick(void *,uintptr_t,uintptr_t);
#include "/home/codey/minecraft-lce-work/r72-session-candidate/loader/super_content_steel_nbt_save_195/nbt_integration.hpp"
extern "C" void mc195_debug_pad_master_enable(int);
namespace mc195_tabs { void invalidate_all(); }
#endif
#endif
#include "/home/codey/minecraft-lce-work/r72-session-candidate/native-debug-plugin/cooperative-upgrade/debug-runtime/native-input/native_pad.hpp"
#include "/home/codey/minecraft-lce-work/r72-session-candidate/native-debug-plugin/cooperative-upgrade/weather-bridge/runtime/weather_runtime.h"
#define EXPORT extern "C" __attribute__((visibility("default")))
#define LOG "/data/minecraft-lce-custom-dialog.log"
#if defined(MC_LEGACY195)
EXPORT const char *g_pluginName="Minecraft 1.95 experimental debug UI";
#else
EXPORT const char *g_pluginName="Minecraft LCE UI seam observer";
#endif
EXPORT const char *g_pluginDesc="Observe a candidate UI update callsite";
EXPORT const char *g_pluginAuth="Codex";
EXPORT uint32_t g_pluginVersion=1;
static unsigned count, first_tid, other_tid, resident; static OrbisPthread worker;
static uintptr_t game_base;
static void logline(const char *s);
static void notify_user(const char *message);
#if defined(MC_LEGACY195) && defined(MC_SUPER195)
inline void *operator new(size_t,void *storage) noexcept { return storage; }
static lce_orbis_scheduler::Scheduler session_scheduler;
alignas(lce_session_runtime::Runtime) static unsigned char session_runtime_storage[sizeof(lce_session_runtime::Runtime)];
static lce_session_runtime::Runtime *session_runtime=nullptr;
alignas(lce_native_keyboard::Keyboard) static unsigned char account_keyboard_storage[sizeof(lce_native_keyboard::Keyboard)];
static lce_native_keyboard::Keyboard *account_keyboard=nullptr;
static bool account_keyboard_attempted=false;
alignas(lce_account_flow::Flow) static unsigned char account_flow_storage[sizeof(lce_account_flow::Flow)];
static lce_account_flow::Flow *account_flow=nullptr;
alignas(lce_account_join::Route) static unsigned char account_route_storage[sizeof(lce_account_join::Route)];
static lce_account_join::Route *account_route=nullptr;
static lce_login_gui::Gui account_gui;
static native_menu::Slot *account_gui_slot=nullptr;
static unsigned account_gui_request=0,account_gui_index=0;
static int account_gui_pad=-1;
static char account_pending_save_name[17]{};
static unsigned account_pending_save_index=0;
static bool account_saved_exists=false;
static bool valid_message_query();
static lce_orbis_auth account_network{};
static char account_url[512]{};
static char account_device[65]{};
static char account_pem[16385]{};
// 0=not scheduled, 1=worker pending, 2=ready, 3=disabled or failed.
static unsigned account_bootstrap_state=0;
static bool account_hook_attempted=false;
static bool session_start_hook_ready=false;
static lce_servers::List session_servers{};
static char session_config_buffer[lce_servers::kMaxFileBytes+1];
static uint8_t session_input_buffer[3*1024*1024];
static uint8_t session_output_buffer[65536];
static bool session_servers_loaded=false;
static unsigned session_config_retry=0;
static bool session_config_terminal=false;
static bool session_owner_logged=false;
static bool session_config_attempt_logged=false;
static unsigned session_report_pending=0;
#if defined(MC_METADATA_SNAPSHOT)
static lce_metadata_snapshot::Values metadata_snapshot{};
// Published once by the owner thread, consumed by the monitor thread.
static unsigned metadata_snapshot_state=0;
static unsigned metadata_snapshot_ticks=0,metadata_snapshot_attempts=0;
static lce_metadata_snapshot_extended::Values metadata_extended{};
static unsigned metadata_extended_state=0,metadata_extended_ticks=0,metadata_extended_attempts=0;
static lce_metadata_snapshot_extended::Details metadata_detail{};
static unsigned metadata_detail_state=0;
static bool metadata_read(void *,uintptr_t address,void *out,size_t size) {
 void *lo=nullptr,*hi=nullptr;int32_t prot=0;
 if(!address || !out || !size || sceKernelQueryMemoryProtection((void *)address,&lo,&hi,&prot) ||
    !(prot&1) || (uintptr_t)lo>address || (uintptr_t)hi<=address ||
    (uintptr_t)hi-address<size) return false;
 memcpy(out,(const void *)address,size);return true;
}
static void metadata_snapshot_tick() {
 if(__atomic_load_n(&metadata_snapshot_state,__ATOMIC_ACQUIRE) || ++metadata_snapshot_ticks%60) return;
 static const unsigned char signature[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56};
 unsigned char actual[sizeof signature]{};
 lce_metadata_snapshot::Values candidate{};
 if(game_base && metadata_read(nullptr,game_base+0x2741a0,actual,sizeof actual) &&
    !memcmp(actual,signature,sizeof actual) &&
    lce_metadata_snapshot::stable(metadata_read,nullptr,game_base,candidate)) {
  metadata_snapshot=candidate;
  __atomic_store_n(&metadata_snapshot_state,1u,__ATOMIC_RELEASE);
 } else if(++metadata_snapshot_attempts>=30) __atomic_store_n(&metadata_snapshot_state,2u,__ATOMIC_RELEASE);
}
static void metadata_extended_tick() {
 if(__atomic_load_n(&metadata_extended_state,__ATOMIC_ACQUIRE) || ++metadata_extended_ticks%60) return;
 static const unsigned char signature[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56};
 unsigned char actual[sizeof signature]{};
 lce_metadata_snapshot_extended::Values candidate{};
 const bool pinned=game_base && metadata_read(nullptr,game_base+0x2741a0,actual,sizeof actual) &&
    !memcmp(actual,signature,sizeof actual);
 if(pinned &&
    lce_metadata_snapshot_extended::stable(metadata_read,nullptr,game_base,candidate)) {
  metadata_extended=candidate;
  __atomic_store_n(&metadata_extended_state,1u,__ATOMIC_RELEASE);
 } else if(++metadata_extended_attempts>=30) {
  if(pinned) {
   lce_metadata_snapshot_extended::inspect(metadata_read,nullptr,game_base,metadata_detail);
   __atomic_store_n(&metadata_detail_state,1u,__ATOMIC_RELEASE);
  }
  __atomic_store_n(&metadata_extended_state,2u,__ATOMIC_RELEASE);
 }
}
#endif
extern "C" __attribute__((visibility("hidden"))) lce_session_runtime::Runtime *
mc195_session_runtime() { return session_runtime; }
static bool session_network_ready(void *) { return lce_orbis_session_socket_api_ready()!=0; }
static bool account_input_start(void *,int pad,bool password,const uint16_t *title) {
 return account_keyboard && account_keyboard->start(pad,password,title);
}
static lce_account_flow::Input account_input_take(void *,uint16_t *text,size_t capacity) {
 if(!account_keyboard)return lce_account_flow::Input::Cancelled;
 auto result=account_keyboard->take(text,capacity);
 if(result==lce_native_keyboard::Keyboard::Result::Accepted)return lce_account_flow::Input::Accepted;
 if(result==lce_native_keyboard::Keyboard::Result::Cancelled)return lce_account_flow::Input::Cancelled;
 return lce_account_flow::Input::Pending;
}
static bool account_schedule(void *,void (*task)(void *,uint64_t),void *ctx,uint64_t generation) {
 return lce_orbis_scheduler::Scheduler::schedule_worker(&session_scheduler,task,ctx,generation);
}
// Server-granted debug menu permission for the current server session.
static unsigned server_debug_allowed=0;
static char account_client_report[1537];
static bool account_authenticate(void *,const char *username,const char *password,bool use_token,bool remember,lce_auth_result *result) {
 // Anticheat: attach the signed client integrity report (runs on the worker thread).
 if(lce_client_report_build(account_client_report,sizeof account_client_report,account_device,game_base,0))
  (void)lce_auth_set_client_report(account_client_report);
 else (void)lce_auth_set_client_report(nullptr);
 return lce_orbis_auth_request(&account_network,account_url,use_token?LCE_AUTH_RESUME:LCE_AUTH_LOGIN_OR_REGISTER,
                               username,password,account_device,remember?1:0,result)!=0;
}
static bool account_read_file(const char *path,char *buffer,size_t capacity,size_t *length) {
 int fd=sceKernelOpen(path,O_RDONLY|O_NONBLOCK|O_NOFOLLOW,0);
 if(fd<0)return false;
 size_t used=0;bool ok=true;
 while(used<capacity) {
  size_t remaining=capacity-used;
  size_t got=sceKernelRead(fd,buffer+used,remaining);
  if(!got)break;
  if(got>remaining){ok=false;break;}
  used+=got;
 }
 if(sceKernelClose(fd)<0)ok=false;
 if(!ok || !used || used==capacity)return false;
 buffer[used]=0;*length=used;return true;
}
static void account_bootstrap(void *,uint64_t) {
 char config_file[1025]{};size_t config_size=0,pem_size=0;
 lce_account_config::Config config{};
 unsigned step=1;
 bool ok=account_read_file("/data/lce-account.conf",config_file,sizeof config_file-1,&config_size);
 if(ok) {step=2;ok=lce_account_config::parse(config_file,config_size,session_servers,&config);}
 if(ok) {step=3;ok=account_read_file("/data/lce-account-ca.pem",account_pem,sizeof account_pem-1,&pem_size);}
 if(ok) {step=4;ok=lce_orbis_auth_init(&account_network,game_base,account_pem,pem_size);}
 if(ok) {memcpy(account_url,config.url,sizeof account_url);memcpy(account_device,config.device,sizeof account_device);}
 lce_auth_wipe(&config,sizeof config);lce_auth_wipe(config_file,sizeof config_file);
 __atomic_store_n(&account_bootstrap_state,ok?2u:3u,__ATOMIC_RELEASE);
 if(ok)logline("Account HTTPS context initialized.\n");
 else {char line[160];snprintf(line,sizeof line,"Account bootstrap failed step=%u native_stage=%d error=%x ssl_import=%d http_import=%d.\n",step,account_network.stage,(unsigned)account_network.native_error,account_network.ssl_import_error,account_network.http_import_error);logline(line);}
}
static bool account_join_ready(void *) {
 return session_start_hook_ready && __atomic_load_n(&account_bootstrap_state,__ATOMIC_ACQUIRE)==2 && account_network.ready && session_runtime && session_runtime->configured() &&
        session_runtime->status().published && session_runtime->coordinator().ready();
}
// A saved token is a bearer credential. Bind it to the configured service and
// endpoint, and keep it readable only by the console user where supported.
struct AccountSaved { char magic[8],name[17],token[44],host[254],url[512],device[65]; uint16_t port; };
static void account_saved_path(char *out,size_t capacity,unsigned index,bool temp=false) {
 snprintf(out,capacity,"/data/lce-account-saved-%u.bin%s",index,temp?".tmp":"");
}
static size_t account_bounded_len(const char *s,size_t max) {size_t n=0;while(n<max && s[n])++n;return n;}
static bool account_saved_valid(const AccountSaved &v,unsigned index) {
 if(index>=session_servers.count || memcmp(v.magic,"LCESAV1",8) || account_bounded_len(v.name,sizeof v.name)==sizeof v.name ||
    account_bounded_len(v.token,sizeof v.token)==sizeof v.token || account_bounded_len(v.host,sizeof v.host)==sizeof v.host ||
    account_bounded_len(v.url,sizeof v.url)==sizeof v.url || account_bounded_len(v.device,sizeof v.device)==sizeof v.device)return false;
 const auto &server=session_servers.entries[index];
 if(strcmp(v.host,server.host) || v.port!=server.port || strcmp(v.url,account_url) || strcmp(v.device,account_device))return false;
 size_t n=account_bounded_len(v.name,sizeof v.name);if(n<3 || n>16 || account_bounded_len(v.token,sizeof v.token)!=43)return false;
 for(size_t i=0;i<n;++i){char c=v.name[i];if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'))return false;}
 for(size_t i=0;i<43;++i){char c=v.token[i];if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'))return false;}
 return true;
}
static bool account_gui_load(void *,char *name,size_t namesize,char *token,size_t tokensize) {
 account_saved_exists=false;if(namesize<17 || tokensize<44)return false;
 char path[96];account_saved_path(path,sizeof path,account_gui_index);
 int fd=sceKernelOpen(path,O_RDONLY|O_NOFOLLOW,0);if(fd<0)return false;
 AccountSaved value{};size_t got=sceKernelRead(fd,&value,sizeof value);char extra;
 size_t tail=sceKernelRead(fd,&extra,1);bool closed=sceKernelClose(fd)>=0;
 bool ok=got==sizeof value && tail==0 && closed && account_saved_valid(value,account_gui_index);
 if(ok){memcpy(name,value.name,17);memcpy(token,value.token,44);account_saved_exists=true;}
 lce_auth_wipe(&value,sizeof value);return ok;
}
static bool account_gui_forget(void *) {
 if(!account_saved_exists)return true;
 char path[96];account_saved_path(path,sizeof path,account_gui_index);
 if(sceKernelUnlink(path)<0){notify_user("Could not remove saved login details.");return false;}
 account_saved_exists=false;return true;
}
static bool account_save_token(unsigned index,const char *name,const char *token) {
 if(index>=session_servers.count)return false;
 AccountSaved value{};memcpy(value.magic,"LCESAV1",8);
 snprintf(value.name,sizeof value.name,"%s",name);snprintf(value.token,sizeof value.token,"%s",token);
 snprintf(value.host,sizeof value.host,"%s",session_servers.entries[index].host);
 snprintf(value.url,sizeof value.url,"%s",account_url);snprintf(value.device,sizeof value.device,"%s",account_device);
 value.port=session_servers.entries[index].port;
 if(!account_saved_valid(value,index)){lce_auth_wipe(&value,sizeof value);return false;}
 char path[96],temp[96];account_saved_path(path,sizeof path,index);account_saved_path(temp,sizeof temp,index,true);
 int fd=sceKernelOpen(temp,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);bool ok=fd>=0;
 if(ok) {size_t written=sceKernelWrite(fd,&value,sizeof value);ok=written==sizeof value && sceKernelFsync(fd)>=0;
  if(sceKernelClose(fd)<0)ok=false;
  if(ok)ok=sceKernelRename(temp,path)>=0;
  if(!ok)(void)sceKernelUnlink(temp);
 }
 lce_auth_wipe(&value,sizeof value);return ok;
}
static bool account_join_begin(void *,const lce_servers::Server &server,int pad,const lce_auth_result &result) {
 __atomic_store_n(&server_debug_allowed,result.debug_allowed?1u:0u,__ATOMIC_RELEASE);
 if(account_pending_save_name[0] && !account_save_token(account_pending_save_index,account_pending_save_name,result.token))
  notify_user("Could not save login details; please enter them next time.");
 lce_auth_wipe(account_pending_save_name,sizeof account_pending_save_name);
 return session_runtime && session_runtime->coordinator().join_authenticated_native(server,pad,result)==0;
}
static void account_join_report(void *,bool accepted) {
 lce_auth_wipe(account_pending_save_name,sizeof account_pending_save_name);
 const char *server_message=lce_auth_last_error();
 if(!accepted && server_message && server_message[0]) {notify_user(server_message);return;}
 notify_user(accepted?"Connecting to server...":"Server login failed or cancelled. Re-enter your details if the saved login expired.");
}
// On a server there is no local host to run Self/World debug actions, so
// pending requests are forwarded as "/lce-action <id> <arg>" chat commands;
// the server executes them with its own authority (operators only).
extern "C" bool mc195_take_remote_action(unsigned *action, uint32_t *item_id, uint64_t *ticket);
extern "C" void mc195_finish_remote_action(uint64_t ticket, unsigned result);
static void forward_remote_debug_actions() {
 if(!session_runtime || !session_runtime->configured())return;
 auto &coordinator=session_runtime->coordinator();
 if(coordinator.phase()!=lce_session_coordinator::Phase::Active)return;
 if(!__atomic_load_n(&server_debug_allowed,__ATOMIC_ACQUIRE))return;
 unsigned action=0;uint32_t item_id=0;uint64_t ticket=0;
 if(!mc195_take_remote_action(&action,&item_id,&ticket))return;
 char text[48];
 int n=snprintf(text,sizeof text,"/lce-action %u %u",action,(unsigned)item_id);
 uint8_t packet[3+2*48];size_t used=0;
 packet[used++]=0xde;packet[used++]=(uint8_t)(n>>8);packet[used++]=(uint8_t)n;
 for(int i=0;i<n;++i){packet[used++]=0;packet[used++]=(uint8_t)text[i];}
 const bool sent=n>0 && coordinator.send_game_packet(packet,used);
 mc195_finish_remote_action(ticket,sent?1u:2u);
}
extern "C" __attribute__((visibility("hidden"))) void mc195_remote_log(const char *m) { logline(m); }
extern "C" __attribute__((visibility("hidden"))) bool mc195_remote_actions_active() {
 static unsigned reported;
 if(reported<2) {++reported;char line[128];snprintf(line,sizeof line,"Remote actions: runtime=%d phase=%u debug_allowed=%u\n",
  session_runtime&&session_runtime->configured()?1:0,session_runtime&&session_runtime->configured()?(unsigned)session_runtime->coordinator().phase():99u,
  __atomic_load_n(&server_debug_allowed,__ATOMIC_ACQUIRE));logline(line);}
 if(!session_runtime || !session_runtime->configured())return false;
 return session_runtime->coordinator().phase()==lce_session_coordinator::Phase::Active &&
        __atomic_load_n(&server_debug_allowed,__ATOMIC_ACQUIRE)!=0;
}
extern "C" __attribute__((visibility("hidden"))) bool mc195_remote_send_action2(unsigned action,uint32_t a,uint32_t b) {
 if(!mc195_remote_actions_active())return false;
 char text[48];
 int n=snprintf(text,sizeof text,"/lce-action %u %u %u",action,(unsigned)a,(unsigned)b);
 if(n<=0||n>=(int)sizeof text)return false;
 uint8_t packet[3+2*48];size_t used=0;
 packet[used++]=0xde;packet[used++]=(uint8_t)(n>>8);packet[used++]=(uint8_t)n;
 for(int i=0;i<n;++i){packet[used++]=0;packet[used++]=(uint8_t)text[i];}
 return session_runtime->coordinator().send_game_packet(packet,used);
}
extern "C" __attribute__((visibility("hidden"))) bool mc195_remote_send_action(unsigned action,uint32_t item_id) {
 return mc195_remote_send_action2(action,item_id,0);
}
// The debug menu is unavailable while connected to a server that has not
// granted this account debug access.
static bool debug_menu_permitted() {
 if(!session_runtime || !session_runtime->configured())return true;
 const auto phase=session_runtime->coordinator().phase();
 const bool on_server=phase==lce_session_coordinator::Phase::Active || phase==lce_session_coordinator::Phase::Starting ||
     phase==lce_session_coordinator::Phase::AwaitingGameThread;
 return !on_server || __atomic_load_n(&server_debug_allowed,__ATOMIC_ACQUIRE)!=0;
}
static bool account_gui_show(void *,bool has_name,bool has_password,bool remember,bool saved_token) {
 if(!valid_message_query() || !mc195_dialog_claim(MC195_DIALOG_ACCOUNT))return false;
 const unsigned n=native_menu::used;
 if(n>=native_menu::capacity) {
  (void)mc195_dialog_release(MC195_DIALOG_ACCOUNT,true,true,true);return false;
 }
 auto &slot=native_menu::slots[n];account_gui_slot=&slot;
 slot.labels[0]=u"Server login";
 slot.labels[1]=u"Choose your username and password, then connect.";
 slot.labels[2]=has_name?u"Username: set":u"Username: enter";
 slot.labels[3]=saved_token?u"Password: saved":has_password?u"Password: set":u"Password: enter";
 slot.labels[4]=u"Connect";
 slot.labels[5]=remember?u"Save Details: On":u"Save Details: Off";
 for(unsigned i=0;i<4;++i)slot.ids[i]=native_menu::id_base+n*8+2+i;
 __atomic_store_n(&native_menu::used,n+1,__ATOMIC_RELEASE);
 auto request=(dialog::Request)(game_base+0x9de300);
 request((void *)(game_base+0x143b290),native_menu::id_base+n*8,native_menu::id_base+n*8+1,
         slot.ids,4,account_gui_pad,dialog::callback,&slot.pending,slot.labels[1],0,false,true);
 return true;
}
static int account_gui_take_menu(void *) {
 int pad=-1,result=-1;
 if(!account_gui_slot || !account_gui_slot->pending.take(pad,result))return -2;
 return pad==account_gui_pad?dialog::button_index(result,4):-1;
}
static bool account_gui_closed(void *) {
 if(!valid_message_query())return false;
 using Query=bool (*)(void *,int32_t,uint32_t);
 auto query=(Query)(game_base+0x9dcc00);void *controller=(void *)(game_base+0x143b290);
 bool a=!query(controller,account_gui_pad,0x4c),b=!query(controller,0xff,0x4c);
 return mc195_dialog_release(MC195_DIALOG_ACCOUNT,true,a,b);
}
static bool account_gui_start_input(void *,bool password) {
 static const uint16_t username[]={u'S',u'e',u'r',u'v',u'e',u'r',u' ',u'u',u's',u'e',u'r',u'n',u'a',u'm',u'e',0};
 static const uint16_t pass[]={u'S',u'e',u'r',u'v',u'e',u'r',u' ',u'p',u'a',u's',u's',u'w',u'o',u'r',u'd',0};
 return account_keyboard && account_keyboard->start(account_gui_pad,password,password?pass:username);
}
static lce_login_gui::Input account_gui_take_input(void *,uint16_t *out,size_t size) {
 if(!account_keyboard)return lce_login_gui::Input::Cancelled;
 auto r=account_keyboard->take(out,size);
 return r==lce_native_keyboard::Keyboard::Result::Accepted?lce_login_gui::Input::Accepted:
        r==lce_native_keyboard::Keyboard::Result::Cancelled?lce_login_gui::Input::Cancelled:lce_login_gui::Input::Pending;
}
static bool account_gui_connect(void *,const char *username,const char *secret,bool use_token,bool remember) {
 alignas(8) unsigned char record[0x208];
 if(!account_route || !lce_join_list::make_join_record(record,sizeof record,account_gui_index))return false;
 auto selected=use_token?account_route->select_token(record,account_gui_pad,secret):
  account_route->select_credentials(record,account_gui_pad,username,secret,remember);
 if(selected!=lce_account_join::Selection::Started)return false;
 lce_auth_wipe(account_pending_save_name,sizeof account_pending_save_name);
 if(remember){snprintf(account_pending_save_name,sizeof account_pending_save_name,"%s",username);account_pending_save_index=account_gui_index;}
 return true;
}
static bool account_gui_queue(unsigned index,int pad) {
 if(account_gui_request || (account_gui.state()!=lce_login_gui::State::Idle && account_gui.state()!=lce_login_gui::State::Done))return false;
 account_gui_index=index;account_gui_pad=pad;account_gui_request=1;return true;
}
extern "C" __attribute__((visibility("hidden"))) int mc195_account_select(void *record,int pad) {
 if(!account_route) {uint32_t index=0;return lce_join_list::endpoint_index(record,&index)?2:0;}
 const auto result=account_route->select(record,pad);
 return result==lce_account_join::Selection::Native?0:result==lce_account_join::Selection::Started?1:2;
}
static void account_join_intercept(void *scene) {
 if(!scene)return;
 auto *bytes=static_cast<unsigned char *>(scene);
 void *record=*reinterpret_cast<void **>(bytes+0xa20);
 const int pad=*reinterpret_cast<int *>(bytes+0x14c);
 uint32_t index=0;
 logline(lce_join_list::endpoint_index(record,&index)?"Account join handler: custom record.\n":"Account join handler: native record.\n");
 if(!lce_join_list::endpoint_index(record,&index)) {
  reinterpret_cast<void (*)(void *)>(game_base+0xa67b90)(scene);
  return;
 }
 if((unsigned)scePthreadGetthreadid()!=__atomic_load_n(&first_tid,__ATOMIC_ACQUIRE) ||
    __atomic_load_n(&other_tid,__ATOMIC_ACQUIRE)) {
  bytes[0xa28]=0;return;
 }
 const int result=mc195_account_select(record,pad);
 // Do not retain the scene while the keyboard and network run asynchronously.
 bytes[0xa28]=0;
 if(result!=1)notify_user("Server login is unavailable.");
}

// Record which retail call site requested the offline dialog. The trampoline
// preserves the original entry and forwards all ten integer/pointer args.
using DialogCall=uint64_t (*)(void *,uint32_t,uint32_t,void *,uint32_t,int,
                              uintptr_t,uintptr_t,uintptr_t,uintptr_t);
using DialogCall12=uint64_t (*)(void *,uint32_t,uint32_t,void *,uint32_t,int,
                                uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t);
static uintptr_t dialog_original[8];
static unsigned dialog_reports;
static unsigned error_reports;
static unsigned action_reports;
static unsigned packet_trace_count;
static unsigned packet_trace_ids[16];
// The retail packet reader calls InputStream::read through slot 0x10 here.
// Record only packet IDs, never packet payloads or account credentials.
static __attribute__((noinline)) int trace_packet_read(void *stream) {
 auto *table=*reinterpret_cast<uintptr_t **>(stream);
 const int id=((int (*)(void *))table[2])(stream);
 if(id>=0 && id<=255) {
  auto *runtime=session_runtime;
  auto *coordinator=runtime && runtime->configured()?&runtime->coordinator():nullptr;
  const auto phase=coordinator?coordinator->phase():lce_session_coordinator::Phase::Unconfigured;
  if(phase==lce_session_coordinator::Phase::Starting || phase==lce_session_coordinator::Phase::Active) {
   const unsigned n=__atomic_fetch_add(&packet_trace_count,1u,__ATOMIC_RELAXED);
   __atomic_store_n(&packet_trace_ids[n%16],(unsigned)id,__ATOMIC_RELEASE);
  }
 }
 return id;
}
static void trace_disconnect_action(unsigned hook,void *app,int pad,int action,uintptr_t caller) {
 if(app!=reinterpret_cast<void *>(game_base+0x143ef58) || action!=0x10)return;
 auto *runtime=session_runtime;
 auto *coordinator=runtime && runtime->configured()?&runtime->coordinator():nullptr;
 const auto phase=coordinator?coordinator->phase():lce_session_coordinator::Phase::Unconfigured;
 if((phase!=lce_session_coordinator::Phase::Starting && phase!=lce_session_coordinator::Phase::Active) ||
    __atomic_fetch_add(&action_reports,1u,__ATOMIC_RELAXED)>=16)return;
 char line[128];
 snprintf(line,sizeof line,"Disconnect action hook=%u pad=%d caller rva=%x.\n",hook,pad,
          (unsigned)(caller>=game_base?caller-game_base:0));
 logline(line);
 if(hook==7 && caller==game_base+0x515623) {
  const unsigned count=__atomic_load_n(&packet_trace_count,__ATOMIC_ACQUIRE);
  const unsigned first=count>16?count-16:0;
  for(unsigned i=first;i<count;++i) {
   snprintf(line,sizeof line,"Packet read index=%u id=%u.\n",i,
            __atomic_load_n(&packet_trace_ids[i%16],__ATOMIC_ACQUIRE));
   logline(line);
  }
 }
}
#define ACTION_TRACE_WRAPPER(N) \
static __attribute__((noinline)) void action_trace_##N(void *app,int pad,int action,void *param) { \
 trace_disconnect_action(N,app,pad,action,(uintptr_t)__builtin_return_address(0)); \
 ((void (*)(void *,int,int,void *))dialog_original[N])(app,pad,action,param); \
}
ACTION_TRACE_WRAPPER(6)
ACTION_TRACE_WRAPPER(7)
#undef ACTION_TRACE_WRAPPER
static __attribute__((noinline)) uint32_t trace_correct_error(void *self,uint32_t id) {
 const uint32_t result=((uint32_t (*)(void *,uint32_t))dialog_original[5])(self,id);
 if(result==0x18400244 && __atomic_fetch_add(&error_reports,1u,__ATOMIC_RELAXED)<8) {
  const uintptr_t caller=(uintptr_t)__builtin_return_address(0);
  char line[128];
  snprintf(line,sizeof line,"Disconnect error input=%x caller rva=%x.\n",id,
           (unsigned)(caller>=game_base?caller-game_base:0));
  logline(line);
 }
 return result;
}
// RunNetworkGame returns true only after its connection has started and the
// game-started state has been set. Keep the retail routine and its return
// value intact, and publish gameplay only for the same custom generation.
static __attribute__((noinline)) bool session_run_network_game(void *manager,void *params) {
 auto *runtime=session_runtime;
 auto *coordinator=runtime && runtime->configured()?&runtime->coordinator():nullptr;
 const auto phase=coordinator?coordinator->phase():lce_session_coordinator::Phase::Unconfigured;
 const bool custom=manager==reinterpret_cast<void *>(game_base+0x143b050) &&
     (phase==lce_session_coordinator::Phase::Starting || phase==lce_session_coordinator::Phase::Active);
 const uint64_t generation=custom?coordinator->generation():0;
 const bool result=((bool (*)(void *,void *))dialog_original[4])(manager,params);
 if(custom && result && coordinator->mark_gameplay_loaded(generation))
  logline("Custom session gameplay started.\n");
 return result;
}
static void trace_offline_dialog(uint32_t title,uint32_t body,uintptr_t caller) {
 const bool offline=title==0x8a21894b && body==0xb837024e;
 const bool connection=title==0x386c3a1a && (body==0x665e892b || body==0x18400244);
 if(!offline && !connection)return;
 if(__atomic_fetch_add(&dialog_reports,1u,__ATOMIC_RELAXED)>=8)return;
 char line[128];
 snprintf(line,sizeof line,offline?"Offline dialog caller rva=%x.\n":"Connection dialog caller rva=%x.\n",
          (unsigned)(caller>=game_base?caller-game_base:0));
 logline(line);
}
#define DIALOG_TRACE_WRAPPER(N) \
static __attribute__((noinline)) uint64_t dialog_trace_##N(void *self,uint32_t title,uint32_t body,void *options,uint32_t count,int pad,uintptr_t cb,uintptr_t ctx,uintptr_t fmt,uintptr_t flag) { \
 trace_offline_dialog(title,body,(uintptr_t)__builtin_return_address(0)); \
 return ((DialogCall)dialog_original[N])(self,title,body,options,count,pad,cb,ctx,fmt,flag); \
}
DIALOG_TRACE_WRAPPER(0)
DIALOG_TRACE_WRAPPER(1)
#undef DIALOG_TRACE_WRAPPER
static __attribute__((noinline)) uint64_t dialog_trace_2(void *self,uint32_t title,uint32_t body,void *options,uint32_t count,int pad,uintptr_t cb,uintptr_t ctx,uintptr_t fmt,uintptr_t kind,uintptr_t extra,uintptr_t flag) {
 trace_offline_dialog(title,body,(uintptr_t)__builtin_return_address(0));
 return ((DialogCall12)dialog_original[2])(self,title,body,options,count,pad,cb,ctx,fmt,kind,extra,flag);
}
// This retail selection method takes an adapter and list index. It normally
// rejects offline users before reaching JoinMenu. Route only our marked row;
// every other selection retains the original checks and behavior.
static __attribute__((noinline)) void offline_selection(void *adapter,int index) {
 if(adapter && index>=0) {
  auto *bytes=(unsigned char *)adapter;
  auto *list=*(lce_join_list::Vector **)(bytes+0x10);
  if(list) {
    void *record=lce_selection_lookup::record(list,index);uint32_t endpoint=0;
    if(lce_join_list::endpoint_index(record,&endpoint)) {
     logline("Offline selection: custom record.\n");
     if((unsigned)scePthreadGetthreadid()!=__atomic_load_n(&first_tid,__ATOMIC_ACQUIRE) ||
        __atomic_load_n(&other_tid,__ATOMIC_ACQUIRE)) {
      bytes[0x30]=0;notify_user("Server login is unavailable.");return;
     }
     auto *vtable=*(uintptr_t **)adapter;
     int pad=((int (*)(void *))vtable[0x18/sizeof(uintptr_t)])(adapter);
     bool started=account_gui_queue(endpoint,pad);
     bytes[0x30]=0;
     if(!started)notify_user("Server login is unavailable.");
     return;
    }
  }
 }
 ((void (*)(void *,int))dialog_original[3])(adapter,index);
}

static bool install_dialog_trace(unsigned which,uintptr_t offset,uintptr_t destination) {
 const uintptr_t entry=game_base+offset, page=entry&~(uintptr_t)0x3fff;
 const uint64_t original=which>=2?0x56415741e5894855ull:0x30ec8348e5894855ull;
 if((entry&7) || __atomic_load_n((uint64_t *)entry,__ATOMIC_ACQUIRE)!=original)return false;
 void *lo=nullptr,*hi=nullptr;int32_t prot=0;
 if(sceKernelQueryMemoryProtection((void *)entry,&lo,&hi,&prot) || (prot&5)!=5 ||
    (uintptr_t)lo>entry || (uintptr_t)hi<entry+8)return false;
 void *relay=nullptr;
 for(uintptr_t shift=0x10000000;shift<=0x40000000;shift*=2) {
  if(page>UINTPTR_MAX-shift)break;
  void *p=nullptr;
  if(sceKernelMmap((void *)(page+shift),0x4000,3,0x1002,-1,0,&p)||!p)continue;
  intptr_t delta=(intptr_t)(uintptr_t)p-(intptr_t)(entry+5);
  if(delta>=INT32_MIN && delta<=INT32_MAX){relay=p;break;}
  sceKernelMunmap(p,0x4000);
 }
 if(!relay)return false;
 auto *code=(unsigned char *)relay;
 // Relay jumps to the wrapper; trampoline executes the replaced prologue and
 // jumps back after it. Both jumps are RIP-relative and clobber no registers.
 code[0]=0xff;code[1]=0x25;memset(code+2,0,4);memcpy(code+6,&destination,8);
 memcpy(code+32,&original,8);code[40]=0xff;code[41]=0x25;memset(code+42,0,4);
 uintptr_t resume=entry+8;memcpy(code+46,&resume,8);
 if(sceKernelMprotect(relay,0x4000,5)){sceKernelMunmap(relay,0x4000);return false;}
 dialog_original[which]=(uintptr_t)(code+32);
 if(sceKernelMprotect((void *)page,0x4000,prot|2)){dialog_original[which]=0;sceKernelMunmap(relay,0x4000);return false;}
 const uint32_t rel=(uint32_t)((intptr_t)(uintptr_t)relay-(intptr_t)(entry+5));
 const uint64_t replacement=0x90909000000000e9ull|((uint64_t)rel<<8);
 uint64_t expected=original;
 bool ok=__atomic_compare_exchange_n((uint64_t *)entry,&expected,replacement,false,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST);
 int restore=sceKernelMprotect((void *)page,0x4000,prot);
 if(ok || restore)__atomic_store_n(&resident,1,__ATOMIC_RELEASE);
 if(!ok){dialog_original[which]=0;sceKernelMunmap(relay,0x4000);}
 if(restore)logline("Dialog trace page protection restore failed.\n");
 return ok && !restore;
}

// This failure-only probe logs the unknown packet ID before retail overwrites
// EAX. It never intercepts successful packet reads or modifies the outcome.
static void trace_invalid_packet(unsigned id) {
 auto *runtime=session_runtime;
 auto *coordinator=runtime && runtime->configured()?&runtime->coordinator():nullptr;
 const auto phase=coordinator?coordinator->phase():lce_session_coordinator::Phase::Unconfigured;
 if(phase!=lce_session_coordinator::Phase::Starting && phase!=lce_session_coordinator::Phase::Active)return;
 char line[96];snprintf(line,sizeof line,"Unregistered clientbound packet id=%u.\n",id);logline(line);
 const unsigned count=__atomic_load_n(&packet_trace_count,__ATOMIC_ACQUIRE);
 const unsigned first=count>16?count-16:0;
 for(unsigned i=first;i<count;++i) {
  snprintf(line,sizeof line,"Packet read index=%u id=%u.\n",i,
           __atomic_load_n(&packet_trace_ids[i%16],__ATOMIC_ACQUIRE));
  logline(line);
 }
}
static bool install_invalid_packet_trace() {
 const uintptr_t entry=game_base+0x5155f0,page=entry&~(uintptr_t)0x3fff;
 const uint64_t expected_low=0x8d4851eb4174d045ull,expected_high=0x04c64100f6738b05ull;
 if((entry&15) || *reinterpret_cast<volatile uint64_t *>(entry)!=expected_low ||
    *reinterpret_cast<volatile uint64_t *>(entry+8)!=expected_high)return false;
 void *lo=nullptr,*hi=nullptr;int32_t prot=0;
 if(sceKernelQueryMemoryProtection((void *)entry,&lo,&hi,&prot) || (prot&5)!=5 ||
    (uintptr_t)lo>entry || (uintptr_t)hi<entry+16)return false;
 void *relay=nullptr;
 for(uintptr_t shift=0x10000000;shift<=0x40000000;shift*=2) {
  if(page>UINTPTR_MAX-shift)break;
  void *p=nullptr;if(sceKernelMmap((void *)(page+shift),0x4000,3,0x1002,-1,0,&p)||!p)continue;
  intptr_t delta=(intptr_t)(uintptr_t)p-(intptr_t)(entry+11);
  if(delta>=INT32_MIN && delta<=INT32_MAX){relay=p;break;}sceKernelMunmap(p,0x4000);
 }
 if(!relay)return false;
 auto *code=(unsigned char *)relay;size_t n=0;
 // Original failing packet ID is in EAX. Volatile registers may be clobbered
 // by the immediately following original virtual call; nonvolatile registers
 // are preserved by the C++ ABI. The original LEA is restored after logging.
 code[n++]=0x89;code[n++]=0xc7; // mov eax, edi
 code[n++]=0x48;code[n++]=0xb8;uintptr_t logger=(uintptr_t)&trace_invalid_packet;memcpy(code+n,&logger,8);n+=8;
 code[n++]=0xff;code[n++]=0xd0; // call rax
 code[n++]=0x48;code[n++]=0xb8;uintptr_t object=game_base+0x147c988;memcpy(code+n,&object,8);n+=8;
 code[n++]=0xff;code[n++]=0x25;memset(code+n,0,4);n+=4;uintptr_t resume=entry+13;memcpy(code+n,&resume,8);
 if(sceKernelMprotect(relay,0x4000,5)){sceKernelMunmap(relay,0x4000);return false;}
 if(sceKernelMprotect((void *)page,0x4000,prot|2)){sceKernelMunmap(relay,0x4000);return false;}
 unsigned char bytes[16];memcpy(bytes,&expected_low,8);memcpy(bytes+8,&expected_high,8);
 bytes[6]=0xe9;int32_t delta=(int32_t)((intptr_t)(uintptr_t)relay-(intptr_t)(entry+11));memcpy(bytes+7,&delta,4);bytes[11]=bytes[12]=0x90;
 uint64_t new_low,new_high;memcpy(&new_low,bytes,8);memcpy(&new_high,bytes+8,8);
 uint64_t old_low=expected_low,old_high=expected_high;unsigned char ok;
 __asm__ volatile("lock cmpxchg16b %1; sete %0" : "=q"(ok), "+m"(*reinterpret_cast<volatile __int128 *>(entry)), "+a"(old_low), "+d"(old_high) : "b"(new_low), "c"(new_high) : "cc", "memory");
 int restore=sceKernelMprotect((void *)page,0x4000,prot);
 if(ok || restore)__atomic_store_n(&resident,1,__ATOMIC_RELEASE);
 if(!ok)sceKernelMunmap(relay,0x4000);
 if(restore)logline("Invalid packet trace page protection restore failed.\n");
 return ok && !restore;
}

// Replay the native virtual call with its original stack alignment, then record
// only its integer result with plain machine instructions. No C++ or logging
// runs on successful reads. The preceding instruction's last byte is retained.
static bool install_packet_id_trace() {
 const uintptr_t entry=game_base+0x515460, page=entry&~(uintptr_t)0x3fff;
 const uint64_t original=0x1050ff00458b49d0ull;
 if((entry&7) || __atomic_load_n((uint64_t *)entry,__ATOMIC_ACQUIRE)!=original)return false;
 void *lo=nullptr,*hi=nullptr;int32_t prot=0;
 if(sceKernelQueryMemoryProtection((void *)entry,&lo,&hi,&prot) || (prot&5)!=5 ||
    (uintptr_t)lo>entry || (uintptr_t)hi<entry+8)return false;
 void *relay=nullptr;
 for(uintptr_t shift=0x10000000;shift<=0x40000000;shift*=2) {
  if(page>UINTPTR_MAX-shift)break;
  void *p=nullptr;
  if(sceKernelMmap((void *)(page+shift),0x4000,3,0x1002,-1,0,&p)||!p)continue;
  intptr_t delta=(intptr_t)(uintptr_t)p-(intptr_t)(entry+6);
  if(delta>=INT32_MIN && delta<=INT32_MAX){relay=p;break;}
  sceKernelMunmap(p,0x4000);
 }
 if(!relay)return false;
 auto *code=(unsigned char *)relay;size_t n=0;
 const unsigned char original_read[]={0x49,0x8b,0x45,0x00,0xff,0x50,0x10};
 memcpy(code+n,original_read,sizeof original_read);n+=sizeof original_read;
 // EAX is the native return value. RCX/RDX are caller-saved at this point.
 code[n++]=0x48;code[n++]=0xba;uintptr_t counter=(uintptr_t)&packet_trace_count;memcpy(code+n,&counter,8);n+=8;
 code[n++]=0xb9;code[n++]=1;memset(code+n,0,3);n+=3;
 code[n++]=0xf0;code[n++]=0x0f;code[n++]=0xc1;code[n++]=0x0a;
 code[n++]=0x83;code[n++]=0xe1;code[n++]=0x0f;
 code[n++]=0x48;code[n++]=0xba;uintptr_t ids=(uintptr_t)&packet_trace_ids;memcpy(code+n,&ids,8);n+=8;
 code[n++]=0x89;code[n++]=0x04;code[n++]=0x8a;
 code[n++]=0xff;code[n++]=0x25;memset(code+n,0,4);n+=4;uintptr_t resume=entry+8;memcpy(code+n,&resume,8);
 if(sceKernelMprotect(relay,0x4000,5)){sceKernelMunmap(relay,0x4000);return false;}
 if(sceKernelMprotect((void *)page,0x4000,prot|2)){sceKernelMunmap(relay,0x4000);return false;}
 const uint32_t rel=(uint32_t)((intptr_t)(uintptr_t)relay-(intptr_t)(entry+6));
 const uint64_t replacement=0x909000000000e9d0ull|((uint64_t)rel<<16);
 uint64_t expected=original;
 bool ok=__atomic_compare_exchange_n((uint64_t *)entry,&expected,replacement,false,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST);
 int restore=sceKernelMprotect((void *)page,0x4000,prot);
 if(ok || restore)__atomic_store_n(&resident,1,__ATOMIC_RELEASE);
 if(!ok)sceKernelMunmap(relay,0x4000);
 if(restore)logline("Packet trace page protection restore failed.\n");
 return ok && !restore;
}

// Redirect both direct call paths to a process-lifetime relay. Installation is
// deferred until the verified UI owner tick and an initialized account service.
static bool install_account_join_hook(int prot) {
 const uintptr_t words[2]={game_base+0xa67b60,game_base+0xa68228};
 const uint64_t originals[2]={0x00000028e95d5e41ull,0x07ebfffff962e8dfull};
 for(unsigned i=0;i<2;++i)
  if((words[i]&7) || __atomic_load_n(reinterpret_cast<uint64_t *>(words[i]),__ATOMIC_ACQUIRE)!=originals[i])return false;
 void *relay=nullptr;
 for(uintptr_t shift=0x10000000;shift<=0x40000000;shift*=2) {
  if(words[0]>UINTPTR_MAX-shift)break;
  void *p=nullptr;
  if(sceKernelMmap(reinterpret_cast<void *>((words[0]&~(uintptr_t)0x3fff)+shift),0x4000,3,0x1002,-1,0,&p)||!p)continue;
  const intptr_t d0=(intptr_t)(uintptr_t)p-(intptr_t)(words[0]+8);
  const intptr_t d1=(intptr_t)(uintptr_t)p-(intptr_t)(words[1]+6);
  if(d0>=INT32_MIN&&d0<=INT32_MAX&&d1>=INT32_MIN&&d1<=INT32_MAX){relay=p;break;}
  sceKernelMunmap(p,0x4000);
 }
 if(!relay)return false;
 auto *code=static_cast<unsigned char *>(relay);
 code[0]=0xff;code[1]=0x25;code[2]=code[3]=code[4]=code[5]=0;
 uintptr_t target=reinterpret_cast<uintptr_t>(&account_join_intercept);memcpy(code+6,&target,sizeof target);
 if(sceKernelMprotect(relay,0x4000,5)){sceKernelMunmap(relay,0x4000);return false;}
 const uintptr_t pages[2]={words[0]&~(uintptr_t)0x3fff,words[1]&~(uintptr_t)0x3fff};
 if(sceKernelMprotect(reinterpret_cast<void *>(pages[0]),0x4000,prot|2)) {sceKernelMunmap(relay,0x4000);return false;}
 if(sceKernelMprotect(reinterpret_cast<void *>(pages[1]),0x4000,prot|2)) {
  sceKernelMprotect(reinterpret_cast<void *>(pages[0]),0x4000,prot);sceKernelMunmap(relay,0x4000);return false;
 }
 const uint32_t rel0=(uint32_t)((intptr_t)(uintptr_t)relay-(intptr_t)(words[0]+8));
 const uint32_t rel1=(uint32_t)((intptr_t)(uintptr_t)relay-(intptr_t)(words[1]+6));
 const uint64_t replacement0=(originals[0]&0x00ffffffull)|((uint64_t)0xe9<<24)|((uint64_t)rel0<<32);
 const uint64_t replacement1=(originals[1]&0xffff0000000000ffull)|((uint64_t)0xe8<<8)|((uint64_t)rel1<<16);
 uint64_t expected0=originals[0],expected1=originals[1];
 bool first=__atomic_compare_exchange_n(reinterpret_cast<uint64_t *>(words[0]),&expected0,replacement0,false,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST);
 bool second=first && __atomic_compare_exchange_n(reinterpret_cast<uint64_t *>(words[1]),&expected1,replacement1,false,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST);
 if(first&&!second) {
  uint64_t expected=replacement0;
  if(!__atomic_compare_exchange_n(reinterpret_cast<uint64_t *>(words[0]),&expected,originals[0],false,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST)) {
   __atomic_store_n(&resident,1,__ATOMIC_RELEASE); // retain relay if rollback is uncertain
   logline("Account join hook rollback failed.\n");
  } else first=false;
 }
 int r0=sceKernelMprotect(reinterpret_cast<void *>(pages[0]),0x4000,prot);
 int r1=sceKernelMprotect(reinterpret_cast<void *>(pages[1]),0x4000,prot);
 if(first || second || r0 || r1)__atomic_store_n(&resident,1,__ATOMIC_RELEASE);
 if(!first&&!second && !r0&&!r1)sceKernelMunmap(relay,0x4000);
 if(r0||r1)logline("Account join hook page protection restore failed.\n");
 return first&&second&&!r0&&!r1;
}
static void session_report_error(void *,lce_session_coordinator::Error error) {
 __atomic_store_n(&session_report_pending,static_cast<unsigned>(error)+1,__ATOMIC_RELEASE);
}
static void session_configure_on_owner_tick() {
 if(!session_runtime || session_runtime->configured() || !session_servers_loaded || session_config_retry || session_config_terminal)return;
 if(!session_config_attempt_logged) {session_config_attempt_logged=true;logline("Session configuration attempt.\n");}
 auto *manager=*reinterpret_cast<void **>(game_base+0x143b148);
 lce_session_runtime::Config config{};
 config.network_ready=session_network_ready;
 config.report_error=session_report_error;
 config.input_buffer=session_input_buffer;config.input_capacity=sizeof session_input_buffer;
 config.output_buffer=session_output_buffer;config.output_capacity=sizeof session_output_buffer;
 config.connect_timeout_ms=15000;
 if(session_runtime->configure(game_base,manager,session_servers,config)) {
  // Runtime, tables and callbacks have static lifetime and plugin_unload
  // refuses unloading once the main hooks make the plugin resident. The
  // atomic table switch allows a previous stock dispatch to complete on its
  // unchanged table; higher-level game behavior still requires PS4 testing.
  const lce_session_runtime::PublishEvidence evidence{true,true};
  if(session_runtime->publish(evidence))
   logline("Experimental server Join hooks published.\n");
  else {
   logline("Experimental server Join hook publication rejected.\n");
   session_config_terminal=true;
  }
 } else {
  const auto error=session_runtime->status().error;
  char line[96];snprintf(line,sizeof line,"Session configuration error=%u.\n",static_cast<unsigned>(error));logline(line);
  if(error==lce_session_runtime::Error::SchedulerNotRunning ||
     error==lce_session_runtime::Error::NetworkNotReady ||
     error==lce_session_runtime::Error::HookOwnerMismatch)
   session_config_retry=120;
  else session_config_terminal=true;
 }
}
#endif
#if defined(MC_COOPERATIVE_TABS)
extern "C" bool mc_legacy_code_resident() {return __atomic_load_n(&resident,__ATOMIC_ACQUIRE)!=0;}
#endif
static void (*original)(void *);
static unsigned ui_lock, reported, outcome;
static native_menu::Probe dialog_probe;
static unsigned cooldown, ui_dropped, menu_enabled; static uint64_t ui_action;
#if defined(MC_SUPER195)
extern "C" __attribute__((visibility("hidden"))) bool mc195_debug_ui_ready(){return __atomic_load_n(&menu_enabled,__ATOMIC_ACQUIRE)!=0;}
extern "C" __attribute__((visibility("hidden"))) unsigned mc195_debug_owner_thread(){
 if(__atomic_load_n(&other_tid,__ATOMIC_ACQUIRE))return 0;
 return __atomic_load_n(&first_tid,__ATOMIC_ACQUIRE);
}
#endif
extern "C" uint64_t visual_take_action() {return __atomic_exchange_n(&ui_action,0ull,__ATOMIC_ACQ_REL);}
// All special strings have static process lifetime. Unrecognized IDs use the
// exact original resolver's singleton/dispatch sequence.
extern "C" __attribute__((visibility("hidden"),noinline)) const char16_t *custom_localize(uint32_t id) {
 uintptr_t caller=(uintptr_t)__builtin_return_address(0);
#if defined(MC_LEGACY195)
 if(mc195_dialog_caller(game_base,caller)) {
  const char16_t *label=native_menu::resolve(id);if(label)return label;
 }
 void *object=*(void **)(game_base+0x1440530);
 if(!object)return (const char16_t *)(game_base+0xeeeda2);
 using Lookup=const char16_t *(*)(void *,uint32_t);
 return ((Lookup)(game_base+0xd39880))(object,id);
#else
 if(caller==game_base+0xa3df56 || caller==game_base+0xa3dfd8 ||
    caller==game_base+0xa3dee2 || caller==game_base+0xa3df21 ||
    caller==game_base+0xa3df97 || caller==game_base+0xa3e020) {
  const char16_t *label=native_menu::resolve(id); if(label)return label;
 }
 void *object=*(void **)(game_base+0x14401f0);
 if(!object)return (const char16_t *)(game_base+0xe9454e);
 using Lookup=const char16_t *(*)(void *,uint32_t);
 return ((Lookup)(game_base+0xcdee40))(object,id);
#endif
}
static bool install_localization(unsigned char *base, size_t size, int prot) {
#if defined(MC_LEGACY195)
 static const unsigned char sig[]={0x48,0x8d,0x0d,0x79,0x3b,0x99,0x00,0x89,0xf8,0x48,0x8b,0x39,0x48,0x85,0xff,0x74,0x07,0x89,0xc6,0xe9,0xb8,0xce,0x28,0x00,0x48,0x8d,0x05,0xd3,0x23,0x44,0x00,0xc3};
 static const unsigned char delegate[]={0x89,0xf1,0x48,0x8d,0x05,0x37,0x65,0x1d,0x00,0x83,0xf9,0xfd,0x0f,0x87,0xce,0x00,0x00,0x00,0x53,0x4c};
 struct Guard{size_t offset;unsigned char bytes[5];};
 static const Guard guards[]={
 {0xa87684,{0xe8,0x27,0x53,0x02,0x00}}, {0xa876d5,{0xe8,0xd6,0x52,0x02,0x00}},
 {0xa87714,{0xe8,0x97,0x52,0x02,0x00}}, {0xa87748,{0xe8,0x63,0x52,0x02,0x00}},
 {0xa87789,{0xe8,0x22,0x52,0x02,0x00}}, {0xa877ca,{0xe8,0xe1,0x51,0x02,0x00}},
 {0xa87812,{0xe8,0x99,0x51,0x02,0x00}}};
 if(size<0xd39880+sizeof delegate || memcmp(base+0xaac9b0,sig,sizeof sig) || memcmp(base+0xd39880,delegate,sizeof delegate))return false;
 for(const auto &g:guards)if(memcmp(base+g.offset,g.bytes,sizeof g.bytes))return false;
 uintptr_t entry=(uintptr_t)base+0xaac9b0;
#else
 static const unsigned char sig[]={0x89,0xfe,0x48,0x8b,0x3d,0xd7,0x3e,0x9e,0x00,0x48,0x85,0xff,0x74,0x05,0xe9,0x1d,0x2b,0x28,0x00,0x48,0x8d,0x05,0x24,0x82,0x43,0x00,0xc3};
 static const unsigned char delegate[]={0x53,0x48,0x8d,0x05,0xd8,0x8a,0x1d,0x00,0x83,0xfe,0xfd,0x0f,0x87,0x9c,0x00,0x00,0x00,0x89,0xf1,0x89};
 struct Guard{size_t offset;unsigned char bytes[5];};
 static const Guard guards[]={
 {0xa3df51,{0xe8,0xba,0xe3,0x01,0x00}}, {0xa3dfd3,{0xe8,0x38,0xe3,0x01,0x00}}, {0xa3dedd,{0xe8,0x2e,0xe4,0x01,0x00}},
 {0xa3df1c,{0xe8,0xef,0xe3,0x01,0x00}}, {0xa3df92,{0xe8,0x79,0xe3,0x01,0x00}},
 {0xa3e01b,{0xe8,0xf0,0xe2,0x01,0x00}}};
 if(size<0xcdee40+sizeof delegate || memcmp(base+0xa5c310,sig,sizeof sig) || memcmp(base+0xcdee40,delegate,sizeof delegate)) return false;
 for(const auto &g:guards)if(memcmp(base+g.offset,g.bytes,sizeof g.bytes))return false;
 uintptr_t entry=(uintptr_t)base+0xa5c310;
#endif
 uintptr_t page=entry&~(uintptr_t)0x3fff;
 if(entry&7)return false;
 void *relay=nullptr;uint32_t rel=0;
 for(uintptr_t shift=0x10000000;shift<=0x40000000;shift*=2) {
  if(page>UINTPTR_MAX-shift)break;void *p=nullptr;
  if(sceKernelMmap((void *)(page+shift),0x4000,3,0x1002,-1,0,&p)||!p)continue;
  intptr_t delta=(intptr_t)(uintptr_t)p-(intptr_t)(entry+5);
  if(delta>=-2147483648ll && delta<=2147483647ll){relay=p;rel=(uint32_t)delta;break;}
  sceKernelMunmap(p,0x4000);
 }
 if(!relay)return false;
 auto *r=(unsigned char *)relay;r[0]=0xff;r[1]=0x25;r[2]=r[3]=r[4]=r[5]=0;
 uintptr_t dest=(uintptr_t)&custom_localize;for(int i=0;i<8;i++)r[6+i]=(unsigned char)(dest>>(8*i));
 if(sceKernelMprotect(relay,0x4000,5)){sceKernelMunmap(relay,0x4000);return false;}
 if(sceKernelMprotect((void *)page,0x4000,prot|2)){sceKernelMunmap(relay,0x4000);return false;}
 uint64_t expected=0;for(int i=0;i<8;i++)expected|=(uint64_t)sig[i]<<(8*i);
 uint64_t patch=0x90909000000000e9ull|((uint64_t)rel<<8);
 bool ok=__atomic_compare_exchange_n((uint64_t *)entry,&expected,patch,false,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST);
 int restore=sceKernelMprotect((void *)page,0x4000,prot);
 if(ok||restore)__atomic_store_n(&resident,1,__ATOMIC_RELEASE);
 if(!ok)sceKernelMunmap(relay,0x4000);
 if(restore)logline("FATAL: localization page protection restore failed.\n");
 return ok && !restore;
}
static bool valid_controller(void *arg) {
#if defined(MC_LEGACY195)
 if ((uintptr_t)arg != game_base + 0x143b290) return false;
#else
 if ((uintptr_t)arg != game_base + 0x143af80) return false;
#endif
 void *lo=nullptr,*hi=nullptr; int32_t prot=0;
 if(sceKernelQueryMemoryProtection(arg,&lo,&hi,&prot) || !(prot&1) ||
    (uintptr_t)lo>(uintptr_t)arg || (uintptr_t)hi<=(uintptr_t)arg || (uintptr_t)hi-(uintptr_t)arg<sizeof(uintptr_t)) return false;
#if defined(MC_LEGACY195)
 return *(uintptr_t *)arg == game_base+0x131a3c8;
#else
 return *(uintptr_t *)arg == game_base+0x1315f28;
#endif
}
#if defined(MC_SUPER195)
static bool valid_message_query() {
 uintptr_t slot=game_base+0x131a3c8+0x98;void *lo=nullptr,*hi=nullptr;int32_t prot=0;
 return !sceKernelQueryMemoryProtection((void*)slot,&lo,&hi,&prot) && (prot&1) &&
        (uintptr_t)lo<=slot && (uintptr_t)hi>=slot+sizeof(uintptr_t) &&
        *(const uintptr_t*)slot==game_base+0x9dcc00;
}
#endif
static void logline(const char *s){size_t n=0;while(s[n])++n;int fd=sceKernelOpen(LOG,0x209,0666);if(fd>=0){sceKernelWrite(fd,s,n);sceKernelClose(fd);}}
extern "C" __attribute__((visibility("hidden"))) void observe_entry(void *);
// Preserve the original call's argument, return address and stack; r15 is the
// normal loop's client on the one verified caller. Other callers fail closed.
__asm__(".text\n.hidden observe_entry\n.type observe_entry,@function\n"
        "observe_entry:\nmov %r15,%rsi\njmp observe\n");
extern "C" __attribute__((visibility("hidden"),noinline)) void observe(void *arg,void *client){
 (void)client;
 // Claim before stock tick: a nested invocation must still execute stock code,
 // but must not poll or establish the owner while its outer tick is in flight.
 bool nested=__atomic_exchange_n(&ui_lock,1u,__ATOMIC_ACQUIRE)!=0;
 original(arg);
 unsigned tid=(unsigned)scePthreadGetthreadid(); unsigned expected=0;
#if defined(MC_SUPER195)
 bool valid_tick=tid && valid_controller(arg);
 if(valid_tick){
 if(!nested)
#endif
 __atomic_compare_exchange_n(&first_tid,&expected,tid,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE);
#if defined(MC_SUPER195)
 else expected=__atomic_load_n(&first_tid,__ATOMIC_ACQUIRE);
#endif
 if(expected && expected!=tid)__atomic_store_n(&other_tid,tid,__ATOMIC_RELEASE);
#if defined(MC_SUPER195)
 }
 const bool owned_tick=valid_tick && tid==__atomic_load_n(&first_tid,__ATOMIC_ACQUIRE) &&
                       !__atomic_load_n(&other_tid,__ATOMIC_ACQUIRE);
#if defined(MC_LEGACY195)
 /* Independent of menu_enabled: run one queued session task after stock
    update, only on the verified outer game/UI thread. */
 if(!nested && owned_tick) {
#if defined(MC_METADATA_SNAPSHOT)
  metadata_snapshot_tick();
  metadata_extended_tick();
#endif
  if(!session_owner_logged) {session_owner_logged=true;logline("Session owner tick entered.\n");}
  if(!account_keyboard_attempted) {
   account_keyboard_attempted=true;
   account_keyboard=new (account_keyboard_storage) lce_native_keyboard::Keyboard();
   if(account_keyboard->bind(game_base)) {
    account_flow=new (account_flow_storage) lce_account_flow::Flow();
    lce_account_flow::Ops ops{nullptr,account_input_start,account_input_take,account_schedule,account_authenticate};
    if(account_flow->configure(ops)) {
     logline("Account input flow initialized; awaiting secure server setup.\n");
    }
    else {account_flow=nullptr;logline("Account input flow rejected.\n");}
   }
   else {account_keyboard=nullptr;logline("Account keyboard ABI rejected.\n");}
  }
  if(!account_route && account_flow && session_servers_loaded) {
   lce_account_join::Ops route_ops{nullptr,account_join_ready,account_join_begin,account_join_report};
   account_route=new (account_route_storage) lce_account_join::Route(*account_flow,session_servers,route_ops);
   logline("Account join route initialized.\n");
  }
  if(account_gui_request) {
   account_gui_request=0;
   lce_login_gui::Ops gui_ops{nullptr,account_gui_show,account_gui_take_menu,account_gui_closed,
                             account_gui_start_input,account_gui_take_input,account_gui_load,account_gui_forget,account_gui_connect};
   if(!account_gui.start(gui_ops))notify_user("Server login is unavailable.");
  }
  account_gui.tick();
  if(account_route)account_route->tick();
  if(account_route && session_runtime && session_runtime->configured() &&
     session_runtime->status().published && !__atomic_load_n(&account_bootstrap_state,__ATOMIC_ACQUIRE)) {
   __atomic_store_n(&account_bootstrap_state,1u,__ATOMIC_RELEASE);
   if(!account_schedule(nullptr,account_bootstrap,nullptr,0))
    __atomic_store_n(&account_bootstrap_state,0u,__ATOMIC_RELEASE);
  }
  if(!account_hook_attempted && account_route &&
     __atomic_load_n(&account_bootstrap_state,__ATOMIC_ACQUIRE)==2 &&
     session_runtime && session_runtime->configured() && session_runtime->status().published) {
   account_hook_attempted=true;
   void *lo0=nullptr,*hi0=nullptr,*lo1=nullptr,*hi1=nullptr;int32_t p0=0,p1=0;
   uintptr_t word0=game_base+0xa67b60,word1=game_base+0xa68228;
   bool pages=!sceKernelQueryMemoryProtection((void *)word0,&lo0,&hi0,&p0) &&
              !sceKernelQueryMemoryProtection((void *)word1,&lo1,&hi1,&p1) &&
              (p0&5)==5 && p0==p1 && (uintptr_t)lo0<=word0 && (uintptr_t)hi0>=word0+8 &&
              (uintptr_t)lo1<=word1 && (uintptr_t)hi1>=word1+8;
   bool installed=pages && install_account_join_hook(p0);
   logline(installed?"Account join hook installed.\n":"Account join hook installation failed; inspect rollback log.\n");
   bool trace0=install_dialog_trace(0,0x9bd4d0,(uintptr_t)&dialog_trace_0);
   bool trace1=install_dialog_trace(1,0x9bd520,(uintptr_t)&dialog_trace_1);
   bool trace2=install_dialog_trace(2,0x9de300,(uintptr_t)&dialog_trace_2);
   logline(trace0 && trace1 && trace2?"Offline dialog diagnostics installed.\n":"Offline dialog diagnostics incomplete.\n");
   bool error_trace=install_dialog_trace(5,0xddf9a0,(uintptr_t)&trace_correct_error);
   logline(error_trace?"Disconnect reason diagnostics installed.\n":"Disconnect reason diagnostics failed.\n");
   bool action_trace=install_dialog_trace(6,0xae84a0,(uintptr_t)&action_trace_6);
   bool locked_action_trace=install_dialog_trace(7,0xab8280,(uintptr_t)&action_trace_7);
   logline(action_trace && locked_action_trace?"Disconnect action diagnostics installed.\n":"Disconnect action diagnostics incomplete.\n");
   // Both implementations of the packet-read hook crashed on console; do not
   // reinstall until their concrete failure is understood and isolated.
   bool invalid_trace=install_invalid_packet_trace();
   logline(invalid_trace?"Invalid packet failure-only trace installed.\n":"Invalid packet failure-only trace failed.\n");
   session_start_hook_ready=install_dialog_trace(4,0x943440,(uintptr_t)&session_run_network_game);
   logline(session_start_hook_ready?"Custom gameplay transition hook installed.\n":"Custom gameplay transition hook failed.\n");
   bool selection=install_dialog_trace(3,0x9c19e0,(uintptr_t)&offline_selection);
   logline(selection?"Offline custom selection hook installed.\n":"Offline custom selection hook failed.\n");
  }
  if(session_runtime) (void)session_runtime->run_ui_task(true);
  if(session_runtime) {
   static unsigned last_phase=~0u,last_error=~0u;
   mc195_crashlog_thread();
   forward_remote_debug_actions();
   const auto status=session_runtime->status();
   unsigned phase=(unsigned)status.phase,error=(unsigned)status.session_error;
   if(phase!=last_phase || error!=last_error) {
    char line[96];snprintf(line,sizeof line,"Server session phase=%u error=%u.\n",phase,error);logline(line);
    mc195_crashlog_rearm();
    last_phase=phase;last_error=error;
   }
  }
  unsigned session_error=__atomic_exchange_n(&session_report_pending,0u,__ATOMIC_ACQ_REL);
  if(session_error) {
   char line[96];snprintf(line,sizeof line,"Server session error=%u.\n",session_error-1);logline(line);
  }
  if(session_config_retry) --session_config_retry;
  else session_configure_on_owner_tick();
 }
#endif
#endif
 __atomic_fetch_add(&count,1,__ATOMIC_RELAXED);
 if(nested) {
#if defined(MC_COOPERATIVE_TABS)
  mc_tabs_render_invalidated(nullptr);
#if defined(MC_LEGACY195) && defined(MC_SUPER195)
  mc195_debug_pad_master_enable(0);mc195_tabs::invalidate_all();
#endif
#endif
  return;
 }
#if defined(MC_SUPER195)
 /* The debug observer owns this callsite. Fan out after stock execution,
    excluding nested ticks; the subscriber independently validates ownership. */
 if(__atomic_load_n(&menu_enabled,__ATOMIC_ACQUIRE) && owned_tick && debug_menu_permitted())mc195_debug_dispatch_ui_tick(arg);
#endif
#if defined(MC_COOPERATIVE_TABS)
 // Bind once to the game-verified main thread in the existing outer tick.
 // Failure leaves the original dialog/world-action path unchanged.
 uintptr_t caller=reinterpret_cast<uintptr_t>(__builtin_return_address(0));
 bool claimed=mc_tabs_claim_owner_thread(arg,caller);
#if defined(MC_LEGACY195) && defined(MC_SUPER195)
 if(claimed && owned_tick && __atomic_load_n(&menu_enabled,__ATOMIC_ACQUIRE) && debug_menu_permitted())mc195_tabs_sessions_tick(arg,caller,game_base);
 else {
  mc195_debug_pad_master_enable(0);mc195_tabs::invalidate_all();
 }
 if(owned_tick && mc_tabs_is_owner_thread(nullptr))mc195_nbt_runtime_owner_tick(claimed && __atomic_load_n(&menu_enabled,__ATOMIC_ACQUIRE));
#endif
#endif
#if defined(MC_SUPER195)
 if(owned_tick && dialog_probe.poll()) {
#else
 if(dialog_probe.poll()) {
#endif
  __atomic_store_n(&outcome,(unsigned)dialog_probe.result,__ATOMIC_RELEASE);
  __atomic_store_n(&reported,1u,__ATOMIC_RELEASE);
  auto selection=dialog_probe.select(mc_weather_ui_world_available());
  if(selection.outcome==hierarchy::Outcome::command) {
   uint64_t zero=0,value=((uint64_t)selection.epoch<<32)|selection.command;
   if(!__atomic_compare_exchange_n(&ui_action,&zero,value,false,__ATOMIC_RELEASE,__ATOMIC_RELAXED)) __atomic_add_fetch(&ui_dropped,1u,__ATOMIC_RELAXED);
  }
  cooldown=60;
 }
#if defined(MC_SUPER195)
 /* Callback consumption is required but not sufficient: the old native
    message box must also be absent from both UI groups. This query ABI and
    stack coverage are candidates and still require real-console validation. */
 if(mc195_dialog_owned(MC195_DIALOG_DEBUG) && dialog_probe.phase==2 && dialog_probe.pad==0 &&
    owned_tick && valid_message_query()) {
  using Query=bool (*)(void *,int32_t,uint32_t);
  auto query=(Query)(game_base+0x9dcc00);
  bool pad_empty=!query(arg,0,0x4c), fullscreen_empty=!query(arg,0xff,0x4c);
  (void)mc195_dialog_release(MC195_DIALOG_DEBUG,true,pad_empty,fullscreen_empty);
 }
#endif
#if defined(MC_SUPER195)
 if(owned_tick && cooldown) --cooldown;
 if(owned_tick && !cooldown && !mc195_dialog_owned(MC195_DIALOG_DEBUG))dialog_probe.reset();
#else
 if(cooldown) --cooldown;
 if(!cooldown)dialog_probe.reset();
#endif
 const bool ready=__atomic_load_n(&menu_enabled,__ATOMIC_ACQUIRE) && dialog_probe.phase==0 &&
                  !__atomic_load_n(&other_tid,__ATOMIC_ACQUIRE) && valid_controller(arg)
#if defined(MC_SUPER195)
                  && owned_tick
#endif
#if defined(MC_COOPERATIVE_TABS)
                  && !mc_tabs_raw_active()
#endif
                  ;
#if defined(MC_COOPERATIVE_TABS) && defined(MC_LEGACY195) && defined(MC_SUPER195)
 (void)ready; // Slot-specific opening and capture are owned by sessions_tick.
#else
 native_pad_set_enabled(ready);
#if defined(MC_COOPERATIVE_TABS)
 NativePadOpening opening_event{};
 const bool opening=ready && native_pad_take_chord_opening(&opening_event);
#else
 const bool opening=ready && native_pad_take_chord();
#endif
 if(opening && ready) {
#if defined(MC_SUPER195)
  if(mc195_dialog_claim(MC195_DIALOG_DEBUG)) {
#endif
  native_pad_set_enabled(false);
#if defined(MC_COOPERATIVE_TABS) && defined(MC_LEGACY195) && defined(MC_SUPER195)
  if (!mc_tabs_request_raw(opening_event.handle,opening_event.input_slot,opening_event.sequence)) {
  native_pad_menu_end();
  /* Do not silently open the old Lists dialog when the requested tabbed
     renderer cannot start. The claim is released only because no dialog or
     raw session was published on this refusal path. */
  (void)mc195_dialog_release(MC195_DIALOG_DEBUG,true,true,true);
  notify_user("Debug tabs unavailable: renderer or input is not ready.");
  }
#else
#if defined(MC_COOPERATIVE_TABS)
  if (!mc_tabs_request_raw(opening_event.handle,opening_event.input_slot,opening_event.sequence)) {
  native_pad_menu_end();
#endif
#if defined(MC_LEGACY195)
  auto request=(dialog::Request)(game_base+0x9de300);
#else
  auto request=(dialog::Request)(game_base+0x990410);
#endif
  if(dialog_probe.open(request,arg,0,mc_weather_ui_world_available())) __atomic_store_n(&reported,2u,__ATOMIC_RELEASE);
#if defined(MC_SUPER195)
  else (void)mc195_dialog_release(MC195_DIALOG_DEBUG,true,true,true); /* No request was made. */
#endif
#if defined(MC_COOPERATIVE_TABS)
  }
#endif
#endif
#if defined(MC_SUPER195)
  }
#endif
 }
#endif
#if defined(MC_COOPERATIVE_TABS)
#if !defined(MC_LEGACY195)
 if(claimed)mc_tabs_capture_client(game_base,client,caller);
#endif
#endif
 __atomic_store_n(&ui_lock,0u,__ATOMIC_RELEASE);
}
static void notify_user(const char *message) {
 OrbisNotificationRequest req={};req.type=NotificationRequest;req.targetId=-1;
 size_t i=0;while(message[i] && i+1<sizeof req.message){req.message[i]=message[i];++i;}
 (void)sceKernelSendNotificationRequest(0,&req,sizeof req,0);
}
namespace weather_monitor {
static Mailbox menu_mailbox_storage;
Mailbox &menu_mailbox() { return menu_mailbox_storage; }
}
static void *monitor(void *) {
 unsigned last=0;
#ifdef MC_NATIVE_DEBUG_DIAGNOSTICS
 uint64_t menu_seen=0,timing_previous=0,timing_sums[4]={};unsigned snapshot_due=0,timing_ticks=0;bool snapshot_done=false,timing_was_active=false;
 unsigned acquire_previous[menu_diag::acquire_issue_count]={};
 static const char *const menu_events[]={
  "owner tick", "GPU mode invalid", "private surface allocation failed", "private GPU arena allocation failed", "private publication initialization failed", "private allocations ready",
  "graphics configuration failed", "graphics configured", "Present hook unavailable", "Present hook rejected", "Present hook installed", "Present protection restore failed",
  "chord could not open tabs", "chord opened tabs", "Present callback entered", "Present callback invalidated",
  "frame target not ready", "frame waiting for EOP or quarantined", "frame lease begin rejected", "frame validation rejected",
  "frame resource acquisition failed", "frame target mismatch", "menu paint failed", "surface take failed",
  "GPU command recording or publication failed", "DCB space or bounds rejected", "DCB source overlap", "frame changed during recording",
  "overlay commands inserted before native Present", "EOP observed", "surface retirement failed", "menu input timeout after 180 ticks",
  "renderer frame inspection or lock failed", "renderer frame preparation failed", "renderer acquisition or lock failed",
  "Present module lookup failed", "Present identity rejected", "Present cooperative signature rejected", "Present call or entry bytes rejected",
  "Present slot mapping rejected", "Present slot value rejected", "Present publication already claimed", "Present slot could not be made writable", "Present slot compare exchange failed",
  "pad lock busy; frame skipped", "session invalidated", "frame aborted", "menu owner lost", "menu session closed"
 };
#endif
 // This thread is the sole consumer of mc_weather_ui_result. Keep ownership
 // until a terminal result arrives; an enqueue alone is not completion.
 enum class Owner { none,dialog,menu };
 Owner owner=Owner::none;
 uint64_t menu_ticket=0;
 for(;;) {
  sceKernelUsleep(100000);
#if defined(MC_METADATA_SNAPSHOT) && defined(MC_LEGACY195) && defined(MC_SUPER195)
  static bool metadata_reported=false;
  const unsigned metadata_state=__atomic_load_n(&metadata_snapshot_state,__ATOMIC_ACQUIRE);
  if(!metadata_reported && metadata_state) {
   if(metadata_state==1) {
    char line[128];snprintf(line,sizeof line,"Metadata snapshot: flags=%d air=%d living=%d health=%d.\n",
     metadata_snapshot.indices[0],metadata_snapshot.indices[1],metadata_snapshot.indices[2],metadata_snapshot.indices[3]);
    logline(line);
   } else logline("Metadata snapshot unavailable.\n");
   metadata_reported=true;
  }
  static bool extended_reported=false;
  const unsigned extended_state=__atomic_load_n(&metadata_extended_state,__ATOMIC_ACQUIRE);
  if(!extended_reported && extended_state) {
   if(extended_state==1) {
    static const char *names[]={"flags","air","name","name_visible","living_first","health",
     "effect_color","effect_ambient","arrows","baby","positive_age","in_love",
     "pig_saddle","pig_extra","sheep_wool","mob_use_state","creeper_swell","creeper_powered",
     "spider_climbing","bat_resting","zombie_baby","rabbit_variant","horse_variant","player_absorption","player_score","item_stack"};
    static_assert(sizeof names/sizeof names[0]==lce_metadata_snapshot_extended::slot_count,"snapshot labels");
    for(unsigned i=0;i<lce_metadata_snapshot_extended::slot_count;++i) {
     char line[96];snprintf(line,sizeof line,"Metadata extended: %s=%d.\n",names[i],metadata_extended.indices[i]);
     logline(line);
    }
   } else logline("Metadata extended unavailable.\n");
   extended_reported=true;
  }
  static bool detail_reported=false;
  if(!detail_reported && __atomic_load_n(&metadata_detail_state,__ATOMIC_ACQUIRE)) {
   static const char *names[]={"flags","air","name","name_visible","living_first","health",
    "effect_color","effect_ambient","arrows","baby","positive_age","in_love",
    "pig_saddle","pig_extra","sheep_wool","mob_use_state","creeper_swell","creeper_powered",
    "spider_climbing","bat_resting","zombie_baby","rabbit_variant","horse_variant","player_absorption","player_score","item_stack"};
   static_assert(sizeof names/sizeof names[0]==lce_metadata_snapshot_extended::slot_count,"detail labels");
   for(unsigned i=0;i<lce_metadata_snapshot_extended::slot_count;++i) {
    char line[96];snprintf(line,sizeof line,"Metadata detail: %s=%d status=%u.\n",names[i],
     metadata_detail.indices[i],(unsigned)metadata_detail.status[i]);
    logline(line);
   }
   detail_reported=true;
  }
#endif
#ifdef MC_NATIVE_DEBUG_DIAGNOSTICS
  uint64_t fresh=__atomic_load_n(&menu_diag::events,__ATOMIC_RELAXED)&~menu_seen;
  for(unsigned i=0;i<sizeof(menu_events)/sizeof(menu_events[0]);++i) if(fresh&(UINT64_C(1)<<i)) {
   char line[128];snprintf(line,sizeof line,"Native tabs: %s.\n",menu_events[i]);logline(line);
  }
  menu_seen|=fresh;
  if(!snapshot_done && !snapshot_due && (menu_seen&(UINT64_C(1)<<menu_diag::chord_opened)))snapshot_due=20;
  if(snapshot_due && !--snapshot_due) {
   char line[160];snprintf(line,sizeof line,"Native tabs: snapshot active=%u inserted=%u eop=%u center_alpha=%u.\n",
    __atomic_load_n(&menu_diag::session_active,__ATOMIC_RELAXED),__atomic_load_n(&menu_diag::inserted_frames,__ATOMIC_RELAXED),
    __atomic_load_n(&menu_diag::completed_frames,__ATOMIC_RELAXED),__atomic_load_n(&menu_diag::sample_alpha,__ATOMIC_RELAXED));
   logline(line);snapshot_done=true;
  }
  // The owner thread only accumulates monotonic microsecond counters. The
  // monitor reports five-second averages and high-water marks without file
  // I/O or formatting in the render callback.
  if(++timing_ticks>=50) {
   timing_ticks=0;
   unsigned issues[menu_diag::acquire_issue_count];bool acquire_changed=false;
   for(unsigned i=0;i<menu_diag::acquire_issue_count;++i) {
    issues[i]=__atomic_load_n(&menu_diag::acquire_issues[i],__ATOMIC_RELAXED);
    if(issues[i]!=acquire_previous[i])acquire_changed=true;
    acquire_previous[i]=issues[i];
   }
   if(acquire_changed) {
    char line[256];snprintf(line,sizeof line,
     "Native tabs: acquire rejects cumulative guard=%u range=%u global=%u lock-guard=%u identity=%u thread-or-trylock=%u changed=%u.\n",
     issues[0],issues[1],issues[2],issues[3],issues[4],issues[5],issues[6]);logline(line);
   }
   const unsigned active=__atomic_load_n(&menu_diag::session_active,__ATOMIC_RELAXED);
   uint64_t n=__atomic_load_n(&menu_diag::timing_count,__ATOMIC_ACQUIRE);
   if(active || timing_was_active) {
    uint64_t avg[4]={},max[4];
    for(unsigned i=0;i<4;++i) {
     uint64_t sum=__atomic_load_n(&menu_diag::timing_sum[i],__ATOMIC_RELAXED);
     if(n>timing_previous)avg[i]=(sum-timing_sums[i])/(n-timing_previous);
     timing_sums[i]=sum;
     max[i]=__atomic_load_n(&menu_diag::timing_max[i],__ATOMIC_RELAXED);
    }
    auto bounded=[](uint64_t v) {return static_cast<unsigned>(v>UINT32_MAX?UINT32_MAX:v);};
    char line[320];snprintf(line,sizeof line,
     "Native tabs: timing active=%u us avg/max paint=%u/%u record=%u/%u publish=%u/%u lease=%u/%u frames=%u.\n",active,
     bounded(avg[0]),bounded(max[0]),bounded(avg[1]),bounded(max[1]),
     bounded(avg[2]),bounded(max[2]),bounded(avg[3]),bounded(max[3]),bounded(n-timing_previous));logline(line);timing_previous=n;
   }
   timing_was_active=active!=0;
  }
#endif
  if(__atomic_exchange_n(&ui_dropped,0u,__ATOMIC_ACQ_REL)){logline("Debug selection dropped: request busy.\n");notify_user("Debug request busy");}
  uint64_t action=visual_take_action();
  if(action){if(owner==Owner::none && mc_weather_ui_submit((unsigned)action,(unsigned)(action>>32))){owner=Owner::dialog;logline("Debug request queued.\n");}else {logline("Debug request unavailable or busy.\n");notify_user("Debug action unavailable or busy");}}
  weather_monitor::Request request{};
  if(weather_monitor::menu_mailbox().take(&request)) {
   if(owner==Owner::none && (request.session ?
      mc_weather_ui_submit_menu(request.action,request.epoch,request.session) :
      mc_weather_ui_submit(request.action,request.epoch))) {
    owner=Owner::menu;menu_ticket=request.ticket;
   } else weather_monitor::menu_mailbox().finish(request.ticket,2);
  }
  unsigned wr=mc_weather_ui_result();
  if(wr) {
   char line[96];snprintf(line,sizeof line,"Debug action result=%u.\n",wr);logline(line);
   if(owner==Owner::menu)weather_monitor::menu_mailbox().finish(menu_ticket,wr);
   else if(owner==Owner::dialog)notify_user(wr==1?"Debug adapter returned":wr==2?"Debug action unavailable in this world":"Debug action expired");
   else logline("Debug result had no monitor owner.\n");
   owner=Owner::none;menu_ticket=0;
  }
  unsigned r=__atomic_load_n(&reported,__ATOMIC_ACQUIRE);
  if(r!=last) {last=r;if(r==2)logline("Custom dialog request invoked.\n");else if(r==1){char line[96];snprintf(line,sizeof line,"Dialog callback result=%u.\n",__atomic_load_n(&outcome,__ATOMIC_ACQUIRE));logline(line);}}
 }
 return nullptr;
}
EXPORT int32_t plugin_load(int32_t,const char **) {
 if(__atomic_load_n(&resident,__ATOMIC_ACQUIRE))return 0;
 OrbisKernelModuleInfo m={};m.size=sizeof m;if(sceKernelGetModuleInfo(0,&m))return 0;m.name[255]=0;
#if defined(MC_LEGACY195)
 if(!m.segmentCount || !m.segmentInfo[0].address)return 0;
 auto *base=(unsigned char *)m.segmentInfo[0].address;size_t size=m.segmentInfo[0].size;
#if defined(MC_SUPER195)
 if((uintptr_t)base!=mc195_super_expected_base()){logline("1.95 super owner base changed.\n");return 0;}
#endif
 static const unsigned char request_sig[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec,0x58,0x48,0x8b,0x1d,0xd0,0xd7,0x96,0x00};
 if(size<0x10c0354 || memcmp(base+0x9de300,request_sig,sizeof request_sig)){logline("1.95 dialog signature rejected.\n");return 0;}
 // The input installer authenticates the full pristine RX image before writing
 // its import slot. It must run before our localization/graphics patches.
 int pad_result=install_native_pad();
 if(pad_result!=1){if(pad_result<0)__atomic_store_n(&resident,1,__ATOMIC_RELEASE);logline("1.95 input installation rejected.\n");return 0;}
 __atomic_store_n(&resident,1,__ATOMIC_RELEASE);
#else
 if(strcmp(m.name,"Minecraft.Client.elf") || !m.segmentCount || !m.segmentInfo[0].address)return 0;
 auto *base=(unsigned char *)m.segmentInfo[0].address;size_t size=m.segmentInfo[0].size;
 static const unsigned char request_sig[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec,0x58,0x4c,0x8b,0x2d,0xe8,0x5f,0x9b,0x00};
 if(!legacy_code_matches(base,size) || size<0xaab2f0 || memcmp(base+0x990410,request_sig,sizeof request_sig)){logline("Signature rejected.\n");return 0;}
 // The visual-only direct path has its own Present and renderer checks.
 if(!tab_site_guard(base,size))logline("Additional scene sites incompatible; direct tabs use separate checks.\n");
 else logline("Scene sites match; direct tabs use separate Present checks.\n");
#endif
#if defined(MC_COOPERATIVE_TABS)
 (void)mc_tabs_configure_owner_thread();
#endif
 game_base=(uintptr_t)base;
#if defined(MC_LEGACY195) && defined(MC_SUPER195)
 if(!session_runtime) session_runtime=new (session_runtime_storage) lce_session_runtime::Runtime(session_scheduler);
 logline("Session runtime explicitly initialized.\n");
 session_servers_loaded=session_runtime->load_server_list(session_servers,session_config_buffer,sizeof session_config_buffer);
 if(!session_servers_loaded)logline("Server config missing/invalid; Join hooks remain disabled.\n");
#endif
 if(!install_localization(base,size,m.segmentInfo[0].prot)){logline("Localization hook failed; no dialog.\n");return 0;}
#if defined(MC_LEGACY195)
 uintptr_t word=(uintptr_t)base+0xb02b78,call=word+2,page=word&~(uintptr_t)0x3fff; const uint64_t wanted=0xbfffed7ec1e8ff89ull;
#else
 uintptr_t word=(uintptr_t)base+0xaab2e8,call=word+2,page=word&~(uintptr_t)0x3fff; const uint64_t wanted=0xbfffee1851e8df89ull;
#endif
 if((word&7) || __atomic_load_n((uint64_t *)word,__ATOMIC_ACQUIRE)!=wanted){logline("Callsite guard rejected.\n");return 0;}
 void *relay=nullptr;uint32_t rel=0;
 for(uintptr_t shift=0x10000000;shift<=0x40000000;shift*=2){
  if(page>UINTPTR_MAX-shift)break;void *p=nullptr;
  if(sceKernelMmap((void *)(page+shift),0x4000,3,0x1002,-1,0,&p)||!p)continue;
  intptr_t delta=(intptr_t)(uintptr_t)p-(intptr_t)(call+5);
  if(delta>=-2147483648ll && delta<=2147483647ll){relay=p;rel=(uint32_t)delta;break;}
  sceKernelMunmap(p,0x4000);
 }
 if(!relay){logline("No in-range relay.\n");return 0;}
 auto *r=(unsigned char *)relay;r[0]=0xff;r[1]=0x25;r[2]=r[3]=r[4]=r[5]=0;uintptr_t dest=(uintptr_t)&observe_entry;for(int i=0;i<8;i++)r[6+i]=(unsigned char)(dest>>(8*i));
 if(sceKernelMprotect(relay,0x4000,5)){sceKernelMunmap(relay,0x4000);return 0;}
#if defined(MC_LEGACY195)
 game_base=(uintptr_t)base; original=(void (*)(void *))(base+0x9daa40);
#else
 game_base=(uintptr_t)base; original=(void (*)(void *))(base+0x98cb40);
#endif
 if(sceKernelMprotect((void *)page,0x4000,m.segmentInfo[0].prot|2)){sceKernelMunmap(relay,0x4000);return 0;}
 uint64_t patch=(wanted&0xff0000000000ffffull)|((uint64_t)0xe8<<16)|((uint64_t)rel<<24);uint64_t expected=wanted;
 bool ok=__atomic_compare_exchange_n((uint64_t *)word,&expected,patch,false,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST);
 int restore=sceKernelMprotect((void *)page,0x4000,m.segmentInfo[0].prot);
 if(ok || restore)__atomic_store_n(&resident,1,__ATOMIC_RELEASE);
 if(!ok){sceKernelMunmap(relay,0x4000);logline(restore?"FATAL: protection restore failed.\n":"Callsite changed; no hook.\n");return 0;}
 logline(restore?"FATAL: observer installed; protection restore failed.\n":"Dialog probe installed; waiting for manual trigger.\n");
 if(!restore) {
#if !defined(MC_LEGACY195)
  int pad_result=install_native_pad();
  if(pad_result!=1){logline(pad_result<0?"FATAL: pad page restore failed; resident, chord disabled.\n":"Pad hook failed; chord disabled.\n");return 0;}
  logline("Pad chord installed.\n");
  if(mc_weather_runtime_start()){logline("Weather runtime failed; chord disabled.\n");return 0;}
#else
  if(!session_scheduler.start()) logline("Session scheduler failed to start.\n");
  else logline("Session scheduler started; no session backend is configured.\n");
  logline("1.95 debug UI: world actions are not yet available.\n");
#endif
  if(scePthreadCreate(&worker,nullptr,monitor,nullptr,"mc_ui_observe")){logline("Monitor failed; chord disabled.\n");return 0;}
  __atomic_store_n(&menu_enabled,1u,__ATOMIC_RELEASE);
 }
 return 0;
}
EXPORT int32_t plugin_unload(int32_t,const char **){if(__atomic_load_n(&resident,__ATOMIC_ACQUIRE))return -1; return mc_weather_runtime_stop();}
extern "C" __attribute__((visibility("hidden"))) int module_start(size_t,const void *){return 0;}
extern "C" __attribute__((visibility("hidden"))) int module_stop(size_t,const void *){return plugin_unload(0,nullptr);}
