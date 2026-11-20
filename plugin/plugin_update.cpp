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
//
// Anti-brick: a boot marker (kMarker) records the previous and the new plugin.
// Every launch of the new plugin counts itself in plugin_load
// (lce_plugin_update_boot); reaching a healthy state (owner tick running for
// a while, lce_plugin_update_healthy) deletes the marker. After
// LCE_PLUGIN_MAX_FAILED_LAUNCHES launches that never got healthy, plugins.ini
// is switched back to the previous plugin, the new one is remembered in
// kBlocked (never re-installed), and this launch installs no hooks.
//
// Stable / any-name installs (plugin_update.h): when the active plugins.ini
// entry is not a per-hash name (normally /data/GoldHEN/plugins/lce_superplugin.prx,
// one permanent ini line), the update keeps plugins.ini untouched: the current
// file is copied to <stem>.prev.prx, the verified download is renamed over the
// active file (atomic), and the same boot marker (previous = .prev.prx) restores
// .prev.prx over it if the new build never gets healthy. The plugin finds its
// own ini line by its module name, so any file name works. If plugins.ini also
// still enables an old per-hash session plugin next to the stable one, the old
// line is set to =false and that copy stays inert, so only one copy runs.
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
constexpr const char *kMarker = "/data/GoldHEN/plugins/lce-update/pending";
constexpr const char *kMarkerTmp = "/data/GoldHEN/plugins/lce-update/pending.tmp";
constexpr const char *kBlocked = "/data/GoldHEN/plugins/lce-update/blocked";
constexpr const char *kBlockedTmp = "/data/GoldHEN/plugins/lce-update/blocked.tmp";
// in-place updates: per-hash id of the staged build (what kBlocked records on rollback)
constexpr const char *kPendingId = "/data/GoldHEN/plugins/lce-update/pending-id";
constexpr const char *kPendingIdTmp = "/data/GoldHEN/plugins/lce-update/pending-id.tmp";
constexpr const char *kPrevTmp = "/data/GoldHEN/plugins/lce-update/prev.tmp";

unsigned g_state;            // 0 idle, 1 scheduled/running, 2 done
unsigned g_staged;           // this launch wrote a boot marker for the NEXT launch: healthy() must keep it
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
int fs_open_write(void *, const char *p) { return sceKernelOpen(p, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0644); }
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

// Full SHA-256 + size check of a file already at its final path (the name
// carries only 16 hex digits of the hash, and the file could be truncated).
bool file_matches(const char *path, const lce_catalog_file &f) {
    int fd = sceKernelOpen(path, O_RDONLY | O_NOFOLLOW, 0);
    if (fd < 0) return false;
    static uint8_t chunk[64 * 1024];
    lce_sha256_ctx ctx; lce_sha256_init(&ctx);
    uint64_t total = 0; bool ok = true;
    for (;;) {
        int64_t got = sceKernelRead(fd, chunk, sizeof chunk);
        if (got < 0) { ok = false; break; }
        if (!got) break;
        total += (uint64_t)got;
        if (total > f.size) { ok = false; break; }
        lce_sha256_update(&ctx, chunk, (size_t)got);
    }
    sceKernelClose(fd);
    uint8_t digest[32]; lce_sha256_final(&ctx, digest);
    return ok && total == f.size && !memcmp(digest, f.sha256, 32);
}

// Basename of the module that contains this code (GoldHEN loads us by file
// name), "" when the kernel does not report it.
const char *own_name() {
    static char name[64]; static bool done;
    if (done) return name[0] ? name : nullptr;
    done = true;
    OrbisKernelModule handles[256]; size_t n = 0;
    if (sceKernelGetModuleList(handles, sizeof handles / sizeof handles[0], &n) < 0) return nullptr;
    if (n > 256) n = 256;
    const uintptr_t self = reinterpret_cast<uintptr_t>(&own_name);
    for (size_t i = 0; i < n; ++i) {
        OrbisKernelModuleInfo info{}; info.size = sizeof info;
        if (sceKernelGetModuleInfo(handles[i], &info) < 0) continue;
        info.name[255] = 0;
        for (uint32_t k = 0; k < info.segmentCount && k < 4; ++k) {
            const uintptr_t a = reinterpret_cast<uintptr_t>(info.segmentInfo[k].address);
            if (self < a || self - a >= info.segmentInfo[k].size) continue;
            const char *b = info.name;
            for (const char *p = info.name; *p; ++p) if (*p == '/') b = p + 1;
            size_t l = strlen(b);
            if (l < 5 || l >= sizeof name || strcmp(b + l - 4, ".prx")) return nullptr;
            memcpy(name, b, l + 1);
            return name;
        }
    }
    return nullptr;
}

bool ini_active(const char *ini, size_t len, char *out, size_t cap) {
    return lce_plugin_ini_active_ex(ini, len, own_name(), out, cap) != 0;
}

bool copy_file(const char *src, const char *tmp, const char *dest) {
    int in = sceKernelOpen(src, O_RDONLY | O_NOFOLLOW, 0);
    if (in < 0) return false;
    int out = fs_open_write(nullptr, tmp);
    if (out < 0) { sceKernelClose(in); return false; }
    static uint8_t chunk[64 * 1024];
    bool ok = true;
    for (;;) {
        int64_t got = sceKernelRead(in, chunk, sizeof chunk);
        if (got < 0) { ok = false; break; }
        if (!got) break;
        if (fs_write(nullptr, out, chunk, (size_t)got) != 0) { ok = false; break; }
    }
    if (ok && sceKernelFsync(out) < 0) ok = false;
    sceKernelClose(in);
    if (sceKernelClose(out) < 0) ok = false;
    if (ok) ok = sceKernelRename(tmp, dest) >= 0;
    if (!ok) (void)sceKernelUnlink(tmp);
    return ok;
}

// In-place update of `active` (any non per-hash name). Returns after logging.
void task_in_place(const lce_https_api *api, const lce_https_trust *trust, const char *base,
                   const lce_catalog_item *item, const char *active, const char *final_path) {
    char prev[256], line[200];
    if (!lce_plugin_prev_path(active, prev, sizeof prev)) { log("Plugin update: cannot derive a backup name; not updating.\n"); return; }
    if (file_matches(active, item->files[0])) return;               // already replaced; restart pending
    if (!lce_content_install(api, trust, &kFs, base, "plugin", kStageRoot, item)) {
        log("Plugin update: download or verification failed; keeping the current plugin.\n"); return;
    }
    char staged[256];
    if (!lce_catalog_install_path(staged, sizeof staged, kStageRoot, "plugin", item, &item->files[0])) {
        log("Plugin update: could not locate the staged plugin.\n"); return;
    }
    // 1. complete backup of the running build, 2. boot marker, 3. atomic replace.
    if (!copy_file(active, kPrevTmp, prev)) { log("Plugin update: could not back up the current plugin; not updating.\n"); return; }
    (void)fs_mkdir_p(nullptr, kStageRoot);
    lce_plugin_marker marker{};
    char mbuf[512]; size_t mlen = 0;
    snprintf(marker.previous, sizeof marker.previous, "%s", prev);
    snprintf(marker.next, sizeof marker.next, "%s", active);
    size_t il = strlen(final_path); char id[204]; memcpy(id, final_path, il); id[il++] = '\n';
    if (!write_replace(kPendingIdTmp, kPendingId, id, il) ||
        !lce_plugin_marker_format(&marker, mbuf, sizeof mbuf, &mlen) || !write_replace(kMarkerTmp, kMarker, mbuf, mlen)) {
        log("Plugin update: could not write the boot marker; keeping the current plugin.\n"); return;
    }
    __atomic_store_n(&g_staged, 1u, __ATOMIC_RELEASE);
    if (sceKernelRename(staged, active) < 0) {
        (void)sceKernelUnlink(kMarker);
        log("Plugin update: could not replace the plugin file; keeping the current plugin.\n"); return;
    }
    snprintf(line, sizeof line, "Plugin update: replaced %s in place (previous kept as %s) for the next launch.\n", active, prev);
    log(line);
    lce_mods_notify("LCE Server plugin updated. Restart Minecraft to use the new version.");
}

bool blocked(const char *final_path) {
    char buf[256]; size_t n = 0;
    if (!read_file(kBlocked, buf, sizeof buf - 1, &n)) return false;
    buf[n] = 0; if (n && buf[n - 1] == '\n') buf[--n] = 0;
    return !strcmp(buf, final_path);
}

void task(void *, uint64_t) {
    const lce_https_api *api = nullptr; const lce_https_trust *trust = nullptr;
    const char *url = lce_mods_net(&api, &trust);
    char base[256];
    if (!url || !lce_mods::mirror_base_from_url(url, base, sizeof base)) { log("Plugin update: server not reachable.\n"); g_state = 2; return; }
    if (!lce_content_fetch_catalog(api, trust, base, "plugin", g_index, sizeof g_index, &g_cat) || strcmp(g_cat.type, "plugin")) {
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
    if (blocked(final_path)) { log("Plugin update: offered build failed to start before; not installing it again.\n"); g_state = 2; return; }
    size_t ini_len = 0, out_len = 0;
    char active[256];
    if (!read_file(kIni, g_ini, sizeof g_ini, &ini_len) || !ini_active(g_ini, ini_len, active, sizeof active)) {
        log("Plugin update: plugins.ini has no single active session plugin; not updating.\n"); g_state = 2; return;
    }
    if (!lce_plugin_is_hash_path(active)) { task_in_place(api, trust, base, item, active, final_path); g_state = 2; return; }
    if (!strcmp(active, final_path)) { g_state = 2; return; }      // already staged; restart pending
    if (!exists(final_path) || !file_matches(final_path, item->files[0])) {
        if (!lce_content_install(api, trust, &kFs, base, "plugin", kStageRoot, item)) {
            log("Plugin update: download or verification failed; keeping the current plugin.\n"); g_state = 2; return;
        }
        char staged[256];
        if (!lce_catalog_install_path(staged, sizeof staged, kStageRoot, "plugin", item, &item->files[0]) ||
            sceKernelRename(staged, final_path) < 0) {
            log("Plugin update: could not place the new plugin.\n"); g_state = 2; return;
        }
    }
    // Boot marker first: without it a broken update could not roll back, so no marker, no switch.
    (void)fs_mkdir_p(nullptr, kStageRoot);
    lce_plugin_marker marker{};
    char mbuf[512]; size_t mlen = 0;
    snprintf(marker.previous, sizeof marker.previous, "%s", active);
    snprintf(marker.next, sizeof marker.next, "%s", final_path);
    if (!lce_plugin_marker_format(&marker, mbuf, sizeof mbuf, &mlen) || !write_replace(kMarkerTmp, kMarker, mbuf, mlen)) {
        log("Plugin update: could not write the boot marker; keeping the current plugin.\n"); g_state = 2; return;
    }
    __atomic_store_n(&g_staged, 1u, __ATOMIC_RELEASE);
    if (!lce_plugin_ini_switch(g_ini, ini_len, final_path, g_ini_out, sizeof g_ini_out, &out_len) ||
        !write_replace(kIniTmp, kIni, g_ini_out, out_len)) {
        (void)sceKernelUnlink(kMarker);
        log("Plugin update: plugins.ini not switched; keeping the current plugin.\n"); g_state = 2; return;
    }
    snprintf(line, sizeof line, "Plugin update: installed %s for the next launch.\n", final_path);
    log(line);
    lce_mods_notify("LCE Server plugin updated. Restart Minecraft to use the new version.");
    g_state = 2;
}
}  // namespace

// Called first thing in plugin_load. Returns true when this launch rolled the
// update back (the caller then installs nothing; the previous plugin loads at
// the next launch).
// Stable plugin listed next to an old per-hash one: switch the per-hash lines
// off. Returns true when this copy is such a per-hash duplicate (stays inert).
static bool resolve_duplicates() {
    size_t ini_len = 0;
    if (!read_file(kIni, g_ini, sizeof g_ini, &ini_len) || !lce_plugin_ini_enabled(g_ini, ini_len, LCE_PLUGIN_STABLE_PATH) ||
        !exists(LCE_PLUGIN_STABLE_PATH))
        return false;
    const char *me = own_name();
    const bool i_am_hash = me && lce_plugin_is_hash_path(me);
    size_t out_len = 0; unsigned disabled = 0;
    if (lce_plugin_ini_disable_hash(g_ini, ini_len, LCE_PLUGIN_STABLE_PATH, g_ini_out, sizeof g_ini_out, &out_len, &disabled) && disabled) {
        char line[160];
        snprintf(line, sizeof line, write_replace(kIniTmp, kIni, g_ini_out, out_len)
                     ? "Plugin update: %u old per-hash session plugin line(s) disabled; %s stays active.\n"
                     : "Plugin update: could not disable %u old per-hash plugin line(s) next to %s.\n",
                 disabled, LCE_PLUGIN_STABLE_PATH);
        log(line);
    }
    if (i_am_hash) log("Plugin update: lce_superplugin.prx is installed too; this per-hash copy stays inactive.\n");
    return i_am_hash;
}

extern "C" __attribute__((visibility("hidden"))) bool lce_plugin_update_boot() {
    if (resolve_duplicates()) return true;
    char buf[512]; size_t n = 0;
    lce_plugin_marker m{};
    if (!read_file(kMarker, buf, sizeof buf, &n)) return false;
    size_t ini_len = 0; char active[256];
    if (!lce_plugin_marker_parse(buf, n, &m) || !read_file(kIni, g_ini, sizeof g_ini, &ini_len) ||
        !ini_active(g_ini, ini_len, active, sizeof active)) {
        (void)sceKernelUnlink(kMarker); return false;
    }
    const int action = lce_plugin_marker_decide(&m, active);
    if (action == 0) { (void)sceKernelUnlink(kMarker); return false; }
    if (action == 1) {
        ++m.attempts;
        if (lce_plugin_marker_format(&m, buf, sizeof buf, &n)) (void)write_replace(kMarkerTmp, kMarker, buf, n);
        return false;
    }
    char line[204]; size_t bl = 0;
    if (!lce_plugin_is_hash_path(m.next)) {
        // in-place: put the kept previous build back over the failing one
        if (!exists(m.previous) || sceKernelRename(m.previous, m.next) < 0) {
            log("Plugin update: restoring the previous plugin file failed.\n");
            return false;                   // keep the marker; try again next launch
        }
        if (read_file(kPendingId, line, sizeof line - 1, &bl) && bl) (void)write_replace(kBlockedTmp, kBlocked, line, bl);
        (void)sceKernelUnlink(kPendingId);
    } else {
        size_t out_len = 0;
        if (!lce_plugin_ini_revert(g_ini, ini_len, m.next, m.previous, g_ini_out, sizeof g_ini_out, &out_len) ||
            !write_replace(kIniTmp, kIni, g_ini_out, out_len)) {
            log("Plugin update: rollback of plugins.ini failed.\n");
            return false;                   // keep the marker; try again next launch
        }
        bl = strlen(m.next);
        memcpy(line, m.next, bl); line[bl++] = '\n';
        (void)write_replace(kBlockedTmp, kBlocked, line, bl);
    }
    (void)sceKernelUnlink(kMarker);
    log("Plugin update: new plugin did not start correctly; previous plugin restored for the next launch.\n");
    lce_mods_notify("LCE Server plugin update failed to start. The previous version was restored; restart Minecraft.");
    return true;
}

// Called from the owner tick once the game has run normally for a while.
extern "C" __attribute__((visibility("hidden"))) void lce_plugin_update_healthy() {
    static bool done;
    if (done) return;
    done = true;
    if (__atomic_load_n(&g_staged, __ATOMIC_ACQUIRE)) return;   // marker belongs to the build staged this launch
    if (exists(kMarker)) { (void)sceKernelUnlink(kMarker); (void)sceKernelUnlink(kPendingId); log("Plugin update: new plugin started correctly.\n"); }
}

// Called from the plugin tick; retries until the shared worker accepts the task.
extern "C" __attribute__((visibility("hidden"))) void lce_plugin_update_tick() {
#if defined(LCE_OFFLINE_PREVIEW)
    return;
#endif
    if (g_state) return;
    const lce_https_api *api = nullptr; const lce_https_trust *trust = nullptr;
    if (!lce_mods_net(&api, &trust)) return;          // account HTTPS context not ready yet
    g_state = 1;
    if (!lce_mods_schedule(task, nullptr)) g_state = 0;
}
