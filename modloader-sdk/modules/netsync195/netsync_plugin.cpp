/* netsync195 plugin entry points (C ABI). The ps4-transport hooks reference
 * the two lce_netsync_* callbacks weakly (client-integration.patch), so a
 * plugin without this module behaves exactly as before.
 *
 *   lce_netsync_configure_from_loader  after lce_sdk_boot (game thread)
 *   lce_netsync_inbound_frame          native_bridge.c receive_frame (worker, bridge lock held)
 *   lce_netsync_pump                   session_runtime.cpp Runtime::run_ui_task (UI thread)
 */
#include "netsync_client.h"
#include "netsync_sdk.h"
#include "../../loader/loader.h"

namespace {
lce_netsync::Client g_client;            // process lifetime (~160 KB .bss)
lce_modids::Entry g_scratch[lce_modids::kMaxEntries];
void (*g_log)(const char *);
void log_line(void *, const char *line) { if (g_log) g_log(line); }
}  // namespace

extern "C" int lce_netsync_configure_from_loader(const lce_loader *loader, int published, void (*log)(const char *)) {
    g_log = log;
    if (!loader) return g_client.configure(nullptr, 0, LCE_MOD_API_VERSION, false) ? 1 : 0;
    int n = lce_netsync::entries_from_sdk(lce_loader_ledger(loader), lce_loader_plan(loader), published != 0,
                                          g_scratch, lce_modids::kMaxEntries);
    bool ok = n >= 0 && g_client.configure(g_scratch, (unsigned)n, LCE_MOD_API_VERSION, true);
    if (!ok) (void)g_client.configure(nullptr, 0, LCE_MOD_API_VERSION, false);
    return ok ? 1 : 0;
}

extern "C" int lce_netsync_inbound_frame(const uint8_t *data, size_t n) {
    return g_client.on_frame(data, n) ? 1 : 0;
}

extern "C" void lce_netsync_pump(int active, int (*send)(void *, const uint8_t *, size_t), void *ctx) {
    (void)g_client.pump(active != 0, send, ctx, log_line, nullptr);
}

/* State for UI/debug: lce_netsync::State in the low byte, RESULT code << 8. */
extern "C" unsigned lce_netsync_state(void) {
    const lce_netsync::Report &r = g_client.report();
    return (unsigned)r.state | (unsigned)r.code << 8;
}
extern "C" const char *lce_netsync_message(void) { return g_client.report().message; }

/* SDK mods the current server declared (accepted LCE|ModIds table), for the
 * plugin's server content gate (crashlog-build mod_gate.cpp). UI thread, like
 * lce_netsync_pump. Returns the number of namespaces copied (0: none). */
extern "C" unsigned lce_netsync_server_mods(char (*out)[lce_netsync::kModIdCap], unsigned cap) {
    return g_client.server_mods(out, cap);
}
