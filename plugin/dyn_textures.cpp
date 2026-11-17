// Startup registration of downloaded texture packs.
//
// The Mods menu installs signed texture packs into
// /data/lce-mods/downloads/texture-packs/<slug>/ (TexturePack.pck, Data/x16Data.pck,
// Data/media.arc, languages.loc) and writes <slug>/lce.tex last (name, size and
// SHA-256 of those files, from the verified catalog; see tex_registry.h). At
// startup this component scans that folder and registers every valid pack through
// the same guarded Java-port path as the six built-in texture packs
// (java_texture_invictus_runtime_195: detached native pack build -> exclusive
// publication under the content guards -> texture-repository insert), one pack per
// title tick, each in its own slot. Nothing patches game code; a pack that fails
// any check is skipped (fail closed) and the rest are unaffected.
#include <orbis/libkernel.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "loader/additive_skin_owner_195/gre_component.h"
#include "loader/java_texture_invictus_runtime_195/dynamic.hpp"
#include "tex_registry.h"
extern "C" {
uint32_t mc195_debug_owner_thread(void);
void mc195_remote_log(const char *);
}

namespace {
const char kRoot[] = "/data/lce-mods/downloads/texture-packs";
// Ids that other GRE components of the plugin own (Bare Bones 2001, the flat skin
// 2002, Bare Bones Compat 2016, Pixel Perfection Extended 2017): never taken over.
// The six built-in Java ports (2011-2015, 2018) are NOT reserved: they are off in
// this build, and if one is ever enabled the one-attempt-per-slot rule decides.
const uint32_t kReserved[] = {2001, 2002, 2016, 2017};

constexpr unsigned kNoSlot = 0xffffffffu;
lce_tex_entry entries[LCE_TEX_REGISTRY_MAX];
mc195_java_dyn_pack packs[LCE_TEX_REGISTRY_MAX];
unsigned slots[LCE_TEX_REGISTRY_MAX];
unsigned result[LCE_TEX_REGISTRY_MAX];   // 0 pending, 1 running, 2 published, 3 rejected, 4 uncertain
unsigned count;
int scanned, bound;
uintptr_t base;

int list_dirs(void *, const char *dir, void (*emit)(void *, const char *), void *ec) {
    int fd = sceKernelOpen(dir, 0x20000 /* O_DIRECTORY */, 0);
    if (fd < 0) return 0;
    static char buf[4096];
    for (;;) {
        int n = sceKernelGetdents(fd, buf, sizeof buf);
        if (n <= 0) break;
        for (int off = 0; off + 8 <= n;) {
            uint16_t reclen = (uint16_t)(buf[off + 4] & 0xff) | (uint16_t)((buf[off + 5] & 0xff) << 8);
            uint8_t type = (uint8_t)buf[off + 6], namlen = (uint8_t)buf[off + 7];
            if (reclen < 8 || off + reclen > n) break;
            if (type == 4 /* DT_DIR */ && namlen && namlen < 49 && 8 + namlen <= reclen) {
                char name[49];
                memcpy(name, buf + off + 8, namlen); name[namlen] = 0;
                if (name[0] != '.') emit(ec, name);
            }
            off += reclen;
        }
    }
    sceKernelClose(fd);
    return 1;
}
long read_file(void *, const char *path, char *out, size_t cap) {
    int fd = sceKernelOpen(path, 0, 0);
    if (fd < 0) return -1;
    size_t n = 0;
    while (n < cap) {                       // a short read is not the end of the file
        long r = (long)sceKernelRead(fd, out + n, cap - n);
        if (r <= 0) break;
        n += (size_t)r;
    }
    sceKernelClose(fd);
    return (long)n;
}

void scan_once() {
    if (scanned) return;
    scanned = 1;
    lce_tex_fs fs = {list_dirs, read_file, nullptr};
    lce_tex_scan_stats st;
    count = (unsigned)lce_tex_registry_scan(&fs, kRoot, kReserved, sizeof kReserved / sizeof kReserved[0], entries, LCE_TEX_REGISTRY_MAX, &st);
    char line[128];
    snprintf(line, sizeof line, "Downloaded textures: %u valid, %u invalid, %u reserved, %u duplicate, %u over limit\n", count,
             (unsigned)st.invalid, (unsigned)st.reserved, (unsigned)st.duplicate, (unsigned)st.full);
    if (count || st.invalid || st.reserved || st.duplicate || st.full) mc195_remote_log(line);
}

uint64_t tid() { int32_t n = scePthreadGetthreadid(); return n > 0 ? (uint64_t)n : 0; }

// One pack per call, on the validated title tick with the content scope held.
// 0 = more to do next tick, 1 = all done, -1 = uncertain native state (poisons the fanout).
int operation(void *) {
    if (!mc195_gre_scope_held() || tid() != mc195_debug_owner_thread()) return 0;
    for (unsigned i = 0; i < count; ++i) {
        if (result[i]) continue;
        result[i] = 1;
        int r = slots[i] == kNoSlot ? 0 : mc195_java_attempt_dyn(slots[i], base, &packs[i]);
        result[i] = r > 0 ? 2u : r < 0 ? 4u : 3u;
        char line[140];
        snprintf(line, sizeof line, "Downloaded textures: %s (id %u) %s\n", entries[i].slug, (unsigned)entries[i].dlc_id,
                 r > 0 ? "registered" : r < 0 ? "UNCERTAIN" : "refused");
        mc195_remote_log(line);
        if (r < 0) return -1;
        break;                               // spread the hashing and building over title ticks
    }
    for (unsigned i = 0; i < count; ++i) if (!result[i]) return 0;
    return 1;
}

uintptr_t game_base() {
    OrbisKernelModule handles[256]; size_t total = 0;
    if (sceKernelGetModuleList(handles, 256, &total) != 0) return 0;
    for (size_t i = 0; i < total && i < 256; ++i) {
        OrbisKernelModuleInfo m; memset(&m, 0, sizeof m); m.size = sizeof m;
        if (sceKernelGetModuleInfo(handles[i], &m) == 0 && !strcmp(m.name, "eboot.bin") && m.segmentCount)
            return (uintptr_t)m.segmentInfo[0].address;
    }
    return 0;
}

bool fill(unsigned i) {
    const lce_tex_entry &e = entries[i];
    mc195_java_dyn_pack &p = packs[i];
    memset(&p, 0, sizeof p);
    p.id = e.dlc_id;
    memcpy(p.name, e.name, sizeof p.name < sizeof e.name ? sizeof p.name : sizeof e.name);
    for (unsigned f = 0; f < LCE_TEX_FILES; ++f) {
        if (e.size[f] > 0xffffffffu || !lce_tex_entry_path(p.path[f], sizeof p.path[f], kRoot, &e, f)) return false;
        p.size[f] = (uint32_t)e.size[f];
        memcpy(p.sha256[f], e.sha256[f], 32);
    }
    return true;
}
}

// 1 if at least one valid downloaded texture pack is installed (scans once).
extern "C" __attribute__((visibility("hidden"))) int mc195_dyn_textures_present() { scan_once(); return count != 0; }

// Called once at startup (before the content guards start): schedule the packs on
// the title tick, like the downloaded skin packs.
extern "C" __attribute__((visibility("hidden"))) int mc195_dyn_textures_bind() {
    if (bound) return 1;
    bound = 1;
    scan_once();
    if (!count) return 1;
    base = game_base();
    if (!base) return 0;
    for (unsigned i = 0; i < count; ++i) {
        slots[i] = kNoSlot;
        if (!fill(i)) { result[i] = 3; continue; }
        slots[i] = mc195_java_dyn_slot(entries[i].dlc_id);
        if (slots[i] == kNoSlot) { result[i] = 3; mc195_remote_log("Downloaded textures: no free slot, pack skipped\n"); }
    }
    if (!mc195_gre_set_title_operation(operation, &count)) { mc195_remote_log("Downloaded textures: title operation unavailable\n"); return 0; }
    mc195_remote_log("Downloaded textures: registration scheduled\n");
    return 1;
}

// 1 while this folder's pack is (being) registered in the running game: its files
// must stay as they are until the game is restarted.
extern "C" __attribute__((visibility("hidden"))) int mc195_dyn_textures_in_use(const char *slug) {
    if (!slug) return 0;
    for (unsigned i = 0; i < count; ++i)
        if (!strcmp(entries[i].slug, slug)) {
            unsigned r = __atomic_load_n(&result[i], __ATOMIC_ACQUIRE);
            return (bound && r != 3) ? 1 : 0;   // pending, running, published or uncertain
        }
    return 0;
}
