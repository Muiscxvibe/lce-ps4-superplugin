// Plugin auto-update (console side).
//
// Once per launch, after the account HTTPS context is ready, a worker task
// fetches the signed "plugin" catalog from the LCE server's content mirror.
// When it offers a newer official build, the PRX is downloaded and verified by
// the content engine (Ed25519 catalog signature + size + SHA-256), moved to
// /data/GoldHEN/plugins/mc195_lce_session_<sha16>.prx, and GoldHEN's
// plugins.ini is switched to it (written to a temp file, then renamed). The
// running plugin is untouched; the new one loads at the next game launch.
// Any failure leaves the current plugin and plugins.ini as they were.
#include <fcntl.h>
#include <orbis/libkernel.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "client_report.h"
#include "content_catalog.h"
#include "content_download.h"
#include "mods_menu.hpp"
#include "plugin_update.h"

extern "C" const char *lce_mods_net(const lce_https_api **api, const lce_https_trust **trust);
extern "C" bool lce_mods_schedule(void (*task)(void *, uint64_t), void *ctx);
extern "C" void lce_mods_notify(const char *message);
extern "C" void mc195_remote_log(const char *);

namespace {
constexpr const char *kStageRoot = "/data/GoldHEN/plugins/lce-update";
constexpr const char *kIni = "/data/GoldHEN/plugins.ini";
constexpr const char *kIniTmp = "/data/GoldHEN/plugins.ini.lce-update";

unsigned g_state;            // 0 idle, 1 scheduled/running, 2 done
lce_catalog g_cat;
alignas(8) uint8_t g_index[64 * 1024];
char g_ini[64 * 1024], g_ini_out[66 * 1024];

void log(const char *m) { mc195_remote_log(m); }
bool exists(const char *p) { OrbisKernelStat st; return sceKernelStat(p, &st) >= 0; }
int fs_mkdir_p(void *, const char *dir) {
    char tmp[400]; size_t n = strlen(dir);
    if (!n || n >= sizeof tmp) return -1;
    memcpy(tmp, dir, n + 1);
    for (size_t i = 1; i < n; ++i)
        if (tmp[i] == '/') { tmp[i] = 0; if (!exists(tmp)) (void)sceKernelMkdir(tmp, 0777); tmp[i] = '/'; }
    if (!exists(tmp)) (void)sceKernelMkdir(tmp, 0777);
    return exists(tmp) ? 0 : -1;
}
int fs_open_write(void *, const char *p) { return sceKernelOpen(p, O_WRONLY | O_CREAT | O_TRUNC, 0666); }
int fs_write(void *, int fd, const void *p, size_t n) {
    const uint8_t *b = static_cast<const uint8_t *>(p);
    while (n) { size_t w = sceKernelWrite(fd, b, n); if (!w || w > n) return -1; b += w; n -= w; }
    return 0;
}
int fs_close(void *, int fd) { return sceKernelClose(fd) >= 0 ? 0 : -1; }
int fs_rename(void *, const char *a, const char *b) { return sceKernelRename(a, b) >= 0 ? 0 : -1; }
int fs_unlink(void *, const char *p) { return sceKernelUnlink(p) >= 0 ? 0 : -1; }
const lce_content_fs kFs = {fs_mkdir_p, fs_open_write, fs_write, fs_close, fs_rename, fs_unlink, nullptr};

bool read_file(const char *path, char *buf, size_t cap, size_t *len) {
    int fd = sceKernelOpen(path, O_RDONLY, 0);
    if (fd < 0) return false;
    size_t used = 0; bool ok = true;
    while (used < cap) {
        size_t got = sceKernelRead(fd, buf + used, cap - used);
        if (!got) break;
        if (got > cap - used) { ok = false; break; }
        used += got;
    }
    if (sceKernelClose(fd) < 0) ok = false;
    if (!ok || used == cap) return false;
    *len = used; return true;
}

bool write_replace(const char *tmp, const char *dest, const char *data, size_t len) {
    int fd = fs_open_write(nullptr, tmp);
    if (fd < 0) return false;
    bool ok = fs_write(nullptr, fd, data, len) == 0 && sceKernelFsync(fd) >= 0;
    if (fs_close(nullptr, fd) != 0) ok = false;
    if (ok) ok = sceKernelRename(tmp, dest) >= 0;
    if (!ok) (void)sceKernelUnlink(tmp);
    return ok;
}

void task(void *, uint64_t) {
    const lce_https_api *api = nullptr; const lce_https_trust *trust = nullptr;
    const char *url = lce_mods_net(&api, &trust);
    char base[256];
    if (!url || !lce_mods::mirror_base_from_url(url, base, sizeof base)) { log("Plugin update: server not reachable.\n"); g_state = 2; return; }
    if (!lce_content_fetch_catalog(api, trust, base, "plugin", g_index, sizeof g_index, &g_cat)) {
        log("Plugin update: no signed update catalog.\n"); g_state = 2; return;
    }
    const uint64_t mine = lce_plugin_build_version(lce_client_report_plugin_build());
    const lce_catalog_item *item = nullptr;
    for (size_t i = 0; i < g_cat.count; ++i) if (!strcmp(g_cat.items[i].id, "session-plugin")) item = &g_cat.items[i];
    const uint64_t offered = item ? lce_plugin_catalog_version(item->version) : 0;
    char line[160];
    snprintf(line, sizeof line, "Plugin update: running %s, offered %s.\n", lce_client_report_plugin_build(), item ? item->version : "none");
    log(line);
    if (!item || item->file_count != 1 || !mine || offered <= mine) { g_state = 2; return; }
    char final_path[128];
    if (!lce_plugin_final_path(item->files[0].sha256, final_path, sizeof final_path)) { g_state = 2; return; }
    if (!exists(final_path)) {
        if (!lce_content_install(api, trust, &kFs, base, "plugin", kStageRoot, item)) {
            log("Plugin update: download or verification failed; keeping the current plugin.\n"); g_state = 2; return;
        }
        char staged[256];
        if (!lce_catalog_install_path(staged, sizeof staged, kStageRoot, "plugin", item, &item->files[0]) ||
            sceKernelRename(staged, final_path) < 0) {
            log("Plugin update: could not place the new plugin.\n"); g_state = 2; return;
        }
    }
    size_t ini_len = 0, out_len = 0;
    if (!read_file(kIni, g_ini, sizeof g_ini, &ini_len) ||
        !lce_plugin_ini_switch(g_ini, ini_len, final_path, g_ini_out, sizeof g_ini_out, &out_len) ||
        !write_replace(kIniTmp, kIni, g_ini_out, out_len)) {
        log("Plugin update: plugins.ini not switched; keeping the current plugin.\n"); g_state = 2; return;
    }
    snprintf(line, sizeof line, "Plugin update: installed %s for the next launch.\n", final_path);
    log(line);
    lce_mods_notify("LCE Server plugin updated. Restart Minecraft to use the new version.");
    g_state = 2;
}
}  // namespace

// Called from the plugin tick; retries until the shared worker accepts the task.
extern "C" __attribute__((visibility("hidden"))) void lce_plugin_update_tick() {
    if (g_state) return;
    const lce_https_api *api = nullptr; const lce_https_trust *trust = nullptr;
    if (!lce_mods_net(&api, &trust)) return;          // account HTTPS context not ready yet
    g_state = 1;
    if (!lce_mods_schedule(task, nullptr)) g_state = 0;
}
