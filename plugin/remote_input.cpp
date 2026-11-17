#include <stdint.h>
extern "C" bool mc195_remote_actions_active();
extern "C" void mc195_remote_log(const char *);
#include <stdio.h>
// Navigation::input handles menu key presses. On a server the dispatcher runs
// in visual-only mode, so actions were refused before reaching the backend.
// While the server is active and grants debug access, clear the dispatcher's
// visual-only flag for the duration of the key press so supported actions
// reach mc195_debug_action_available / wrapped_submit (routed to the server).
// Offsets from configure_visual_world_actions: dispatcher at +0x3210,
// visual_only_ at dispatcher+0x62.
#include "/home/codey/minecraft-lce-work/r72-session-candidate/tab-menu/modal-binding/navigation.hpp"
using NavResponse = tab_modal::Response;
NavResponse lce_real_input(tab_modal::Navigation *self, int32_t pad, tab_modal::Key key, bool pressed, bool repeat)
 __asm__("__real__ZN9tab_modal10Navigation5inputEiNS_3KeyEbb");
NavResponse lce_wrap_input(tab_modal::Navigation *self, int32_t pad, tab_modal::Key key, bool pressed, bool repeat)
 __asm__("__wrap__ZN9tab_modal10Navigation5inputEiNS_3KeyEbb") __attribute__((visibility("hidden")));
NavResponse lce_wrap_input(tab_modal::Navigation *self, int32_t pad, tab_modal::Key key, bool pressed, bool repeat) {
 unsigned char *visual_only = reinterpret_cast<unsigned char *>(self) + 0x3210 + 0x62;
 const bool lift = self && mc195_remote_actions_active() && *visual_only;
 if (lift) *visual_only = 0;
 NavResponse r = lce_real_input(self, pad, key, pressed, repeat);
 if (lift) *visual_only = 1;
 return r;
}
