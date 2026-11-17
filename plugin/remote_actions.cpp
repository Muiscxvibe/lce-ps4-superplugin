// Thin C bridge to the plugin's Self action queue, compiled on its own because
// the queue headers conflict with declarations already visible in debug_main.
#include "/home/codey/minecraft-lce-work/r72-session-candidate/debug-player-cheats/self_action_queue.hpp"
extern "C" __attribute__((visibility("hidden"))) bool mc195_take_remote_action(unsigned *action, uint32_t *item_id, uint64_t *ticket) {
 self_player::Request request{};
 if(!action || !item_id || !ticket || !self_player::queue().take(&request))return false;
 *action=request.action;*item_id=request.item_id;*ticket=request.ticket;return true;
}
extern "C" __attribute__((visibility("hidden"))) void mc195_finish_remote_action(uint64_t ticket, unsigned result) {
 self_player::queue().finish(ticket,result);
}

// ---- Debug menu on a server: route supported actions to the server ----
// Linked with --wrap so the prebuilt menu objects call these first. Off a
// server (or for unsupported actions) everything falls through unchanged.
#include "/home/codey/minecraft-lce-work/r72-session-candidate/tab-menu/modal-binding/action_dispatch.hpp"
extern "C" bool mc195_remote_actions_active();
extern "C" void mc195_remote_log(const char *);
extern "C" bool mc195_remote_send_action(unsigned action, uint32_t item_id);
extern "C" bool mc195_remote_send_action2(unsigned action, uint32_t a, uint32_t b);
namespace {
bool server_supported(unsigned a) {
 switch (a) {
 case 17: case 18: case 19: case 20: case 21: case 22: case 23: case 24: case 26: case 27:
 case 54: case 55: case 58: case 59: case 65: case 67: case 68: case 70: case 72: case 73:
 case 74: case 75: case 80: case 81: case 82: case 83: case 89: case 90: case 91: case 92:
 case 108: case 109: case 142: case 149: case 150: case 171: case 172: case 173: case 174:
 case 175: case 176: case 177: case 178: case 188: return true;
 default: return false;
 }
}
tab_actions::WeatherBackend original{};
unsigned remote_result=0;
bool remote_last=false;
bool wrapped_submit(void *context, unsigned action, uint32_t epoch) {
 if (mc195_remote_actions_active() && server_supported(action)) {
  if (!mc195_remote_send_action(action, 0)) return false;
  remote_result=1; remote_last=true; return true;
 }
 remote_last=false;
 return original.submit && original.submit(context, action, epoch);
}
unsigned wrapped_poll(void *context) {
 if (remote_result) { unsigned r=remote_result; remote_result=0; return r; }
 return original.poll ? original.poll(context) : 0;
}
tab_actions::Readback wrapped_readback(void *context, unsigned action) {
 if (remote_last) return tab_actions::Readback{};
 return original.readback ? original.readback(context, action) : tab_actions::Readback{};
}
}
extern "C" bool __real_mc195_debug_action_available(void *, unsigned, uint32_t);
extern "C" __attribute__((visibility("hidden"))) bool __wrap_mc195_debug_action_available(void *context, unsigned action, uint32_t epoch) {
 if (mc195_remote_actions_active() && server_supported(action)) return true;
 return __real_mc195_debug_action_available(context, action, epoch);
}
bool real_configure_visual_world_actions(void *self, const tab_actions::WeatherBackend &backend)
 __asm__("__real__ZN9tab_modal10Navigation30configure_visual_world_actionsERKN11tab_actions14WeatherBackendE");
extern "C" __attribute__((visibility("hidden"))) bool wrap_configure_visual_world_actions(void *self, const tab_actions::WeatherBackend &backend)
 __asm__("__wrap__ZN9tab_modal10Navigation30configure_visual_world_actionsERKN11tab_actions14WeatherBackendE");
bool wrap_configure_visual_world_actions(void *self, const tab_actions::WeatherBackend &backend) {
 original=backend;
 tab_actions::WeatherBackend patched=backend;
 patched.submit=wrapped_submit; patched.poll=wrapped_poll; patched.readback=wrapped_readback;
 return real_configure_visual_world_actions(self, patched);
}


// The live menu configures its backend through Navigation::configure_weather,
// an inline (weak, hidden) function in the prebuilt tabs_115.o. This strong
// definition overrides it at link time; the original body (a self-contained
// leaf: field checks + 0x60-byte backend copy, no calls or relocations) is
// kept verbatim in .text below and invoked with the patched backend.
__asm__(".pushsection .text.lce_orig_configure_weather,\"ax\",@progbits\n"
        ".p2align 4\n.hidden lce_orig_configure_weather\n.type lce_orig_configure_weather,@function\n"
        "lce_orig_configure_weather:\n.byte 0x55,0x48,0x89,0xe5,0x48,0x83,0xe4,0xf8,0x31,0xc0,0x80,0xbf,0xc0,0x34,0x00,0x00,0x00,0x0f,0x85,0x8b,0x00,0x00,0x00,0x48,0x83,0x3f,0x00,0x0f,0x85,0x81,0x00,0x00,0x00,0x48,0x83,0x7f,0x10,0x00,0x75,0x7a,0x80,0xbf,0x70,0x32,0x00,0x00,0x00,0x0f,0x95,0xc0,0x48,0x83,0xbf,0x18,0x32,0x00,0x00,0x00,0x0f,0x95,0xc1,0x08,0xc1,0x48,0x83,0x7e,0x08,0x00,0x0f,0x94,0xc0,0x48,0x83,0x7e,0x10,0x00,0x0f,0x94,0xc2,0x08,0xc2,0x08,0xca,0x48,0x83,0x7e,0x18,0x00,0x0f,0x94,0xc0,0x08,0xd0,0x75,0x41,0x0f,0x10,0x46,0x50,0x0f,0x11,0x87,0x60,0x32,0x00,0x00,0x0f,0x10,0x46,0x40,0x0f,0x11,0x87,0x50,0x32,0x00,0x00,0x0f,0x10,0x06,0x0f,0x10,0x4e,0x10,0x0f,0x10,0x56,0x20,0x0f,0x10,0x5e,0x30,0x0f,0x11,0x9f,0x40,0x32,0x00,0x00,0x0f,0x11,0x97,0x30,0x32,0x00,0x00,0x0f,0x11,0x8f,0x20,0x32,0x00,0x00,0x0f,0x11,0x87,0x10,0x32,0x00,0x00,0x34,0x01,0x48,0x89,0xec,0x5d,0xc3\n"
        ".size lce_orig_configure_weather,.-lce_orig_configure_weather\n.popsection\n");
extern "C" bool lce_orig_configure_weather(void *self, const tab_actions::WeatherBackend &backend);
extern "C" __attribute__((visibility("hidden"))) bool lce_wrap_configure_weather(void *self, const tab_actions::WeatherBackend &backend)
 __asm__("_ZN9tab_modal10Navigation17configure_weatherERKN11tab_actions14WeatherBackendE");
bool lce_wrap_configure_weather(void *self, const tab_actions::WeatherBackend &backend) {
 static_assert(sizeof(tab_actions::WeatherBackend)==0x60, "WeatherBackend layout changed");
 mc195_remote_log("Remote actions: live menu backend wrapped.\n");
 original=backend;
 tab_actions::WeatherBackend patched=backend;
 patched.submit=wrapped_submit; patched.poll=wrapped_poll; patched.readback=wrapped_readback;
 return lce_orig_configure_weather(self, patched);
}


// Hooks for tab_actions::Dispatcher::available/submit. tabs_29.o is
// post-processed (see build script): the inline definitions are renamed to
// weak lce_hook_*, their bodies stay callable as lce_body_*, and these strong
// definitions win. On a server the menu has no verified local world (epoch 0),
// so the original refuses every action; server-supported actions are instead
// routed to wrapped_submit/wrapped_poll by swapping the dispatcher's backend.
// Dispatcher layout (from the disassembly): WeatherBackend at +0, active_ +0x60.
extern "C" bool lce_body_available(const void *self, unsigned action, uint32_t epoch);
extern "C" unsigned char lce_body_submit(void *self, unsigned action, uint32_t epoch);
static void route_backend(void *self) {
 auto *b = static_cast<tab_actions::WeatherBackend *>(self);
 if (b->submit != wrapped_submit) {
  original = *b;
  b->submit = wrapped_submit; b->poll = wrapped_poll; b->readback = wrapped_readback;
 }
}
extern "C" __attribute__((visibility("hidden"))) bool lce_hook_available(const void *self, unsigned action, uint32_t epoch) {
 if (mc195_remote_actions_active() && server_supported(action))
  return *(static_cast<const unsigned char *>(self) + 0x60) == 0; // not busy
 return lce_body_available(self, action, epoch);
}
extern "C" __attribute__((visibility("hidden"))) unsigned char lce_hook_submit(void *self, unsigned action, uint32_t epoch) {
 if (mc195_remote_actions_active() && server_supported(action)) {
  route_backend(self);
  // The body re-checks availability through lce_hook_available, then calls
  // the backend submit (now wrapped_submit) and records active_.
  return lce_body_submit(self, action, epoch ? epoch : 1u);
 }
 return lce_body_submit(self, action, epoch);
}

// Item give (Items tab) and mob spawn (Mobs tab) on a server: sent as
// "/lce-action 247 <item> <quantity>" / "/lce-action 66 <mob>" and completed
// immediately through the wrapped backend poll.
extern "C" unsigned char lce_body_submit_item_quantity(void *self, uint32_t item, uint32_t quantity, uint32_t epoch);
extern "C" unsigned char lce_body_submit_mob(void *self, uint32_t mob, uint32_t epoch);
static unsigned char remote_complete(void *self, bool sent) {
 if (!sent) return 2; // busy
 route_backend(self);
 *(static_cast<unsigned char *>(self) + 0x60) = 4; // active_ = self action
 remote_result = 1; remote_last = true;
 return 0; // accepted
}
extern "C" __attribute__((visibility("hidden"))) unsigned char lce_hook_submit_item_quantity(void *self, uint32_t item, uint32_t quantity, uint32_t epoch) {
 if (mc195_remote_actions_active()) {
  if (*(static_cast<unsigned char *>(self) + 0x60)) return 2;
  return remote_complete(self, item && item < 32768 && quantity && quantity <= 127 && mc195_remote_send_action2(247, item, quantity));
 }
 return lce_body_submit_item_quantity(self, item, quantity, epoch);
}
extern "C" __attribute__((visibility("hidden"))) unsigned char lce_hook_submit_mob(void *self, uint32_t mob, uint32_t epoch) {
 if (mc195_remote_actions_active()) {
  if (*(static_cast<unsigned char *>(self) + 0x60)) return 2;
  return remote_complete(self, mob && mob < 256 && mc195_remote_send_action2(66, mob, 0));
 }
 return lce_body_submit_mob(self, mob, epoch);
}

// This server build ships no content mods: every startup mod choice (Steel,
// skins, texture packs, Lucky Blocks, Java packs) reports disabled, so no mod
// crafting tabs or Creative pages are installed.
extern "C" __attribute__((visibility("hidden"))) int __wrap_mc195_selection_worker_startup_choice(const char *, int *enabled) {
 if (!enabled) return 0;
 *enabled = 0;
 return 1;
}
