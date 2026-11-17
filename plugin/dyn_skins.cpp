// Startup registration of downloaded skin packs.
//
// The content downloader installs signed skin packs into
// /data/lce-mods/packs/<slug>/SkinPack.pck and writes <slug>/lce.desc
// (DLC id, skin count, size, SHA-256). At startup this component scans that
// folder and registers every valid pack through the same guarded native path
// as the built-in packs (generic catalog -> owner load (re-checks size+hash)
// -> scoped publish under the texture-cache lease -> guarded append).
#include <orbis/libkernel.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "/home/codey/minecraft-lce-work/r72-session-candidate/loader/additive_skin_owner_195/gre_component.h"
extern "C" {
#include "/home/codey/minecraft-lce-work/r72-session-candidate/loader/additive_pack_import_195/catalog.h"
#include "/home/codey/minecraft-lce-work/r72-session-candidate/loader/additive_pack_import_195/scoped_owner.h"
#include "/home/codey/minecraft-lce-work/r72-session-candidate/loader/additive_pack_import_195/resource_checks.h"
#include "/home/codey/minecraft-lce-work/r72-session-candidate/loader/additive_skin_owner_195/storage_pump_hook.h"
#include "/home/codey/minecraft-lce-work/r72-session-candidate/loader/additive_multi_root_guard_195/roots.h"
#include "/home/codey/minecraft-lce-work/r72-session-candidate/loader/additive_skin_snapshot_195/observer.h"
#include "/home/codey/minecraft-lce-work/r72-session-candidate/loader/gre_root_guard_195/guard.h"
#include "/home/codey/minecraft-lce-work/r72-session-candidate/loader/gre_root_guard_195/manager_guard.h"
#include "skin_registry.h"
uint32_t mc195_debug_owner_thread(void);
void mc195_remote_log(const char *);
}

namespace {
const char kRoot[] = "/data/lce-mods/packs";
// Built-in packs that may still be registered by their own components.
const uint32_t kReserved[] = {2002};

lce_skin_entry entries[LCE_SKIN_REGISTRY_MAX];
mc195_generic_skin_descriptor descs[LCE_SKIN_REGISTRY_MAX];
const mc195_generic_skin_descriptor *desc_ptrs[LCE_SKIN_REGISTRY_MAX];
unsigned char *inputs[LCE_SKIN_REGISTRY_MAX];
unsigned count, status[LCE_SKIN_REGISTRY_MAX], cache_attempts[LCE_SKIN_REGISTRY_MAX];
int scanned, bound;
uintptr_t base;
constexpr unsigned max_cache_attempts = 600;
mc195_generic_catalog catalog{};
lce195_file_ops files{};
lce195_skin_ops skin{};

// --- directory scan via sceKernelGetdents (BSD dirent: u32 ino, u16 reclen, u8 type, u8 namlen, name) ---
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
    long n = (long)sceKernelRead(fd, out, cap);
    sceKernelClose(fd);
    return n;
}

void scan_once() {
    if (scanned) return;
    scanned = 1;
    lce_skin_fs fs = {list_dirs, read_file, nullptr};
    count = (unsigned)lce_skin_registry_scan(&fs, kRoot, kReserved, sizeof kReserved / sizeof kReserved[0], entries,
                                             LCE_SKIN_REGISTRY_MAX);
}

// --- registration (same guarded path as the built-in skin-port components) ---
uint64_t tid() { int32_t n = scePthreadGetthreadid(); return n > 0 ? (uint64_t)n : 0; }
bool span(uintptr_t p, size_t n, int bits) {
    void *lo = nullptr, *hi = nullptr; int32_t prot = 0;
    return p && n && p <= UINTPTR_MAX - n && !sceKernelQueryMemoryProtection((void *)p, &lo, &hi, &prot) &&
           (prot & bits) == bits && (uintptr_t)lo <= p && (uintptr_t)hi >= p + n;
}
int read_checked(void *, uintptr_t p, void *out, size_t n) { if (!out || !span(p, n, 1)) return 0; __builtin_memcpy(out, (void *)p, n); return 1; }
int write_checked(void *, uintptr_t p, const void *data, size_t n) { if (!data || !span(p, n, 3)) return 0; __builtin_memcpy((void *)p, data, n); return 1; }
int scope(void *) { return mc195_gre_scope_held() && tid() == mc195_debug_owner_thread(); }
struct Append { const lce195_skin_ops *ops; void *manager, *pack; uint32_t id; };
int do_append(void *ctx) {
    auto *a = (Append *)ctx;
    if (!a || !scope(nullptr)) return 0;
    a->ops->add_pack(a->manager, a->pack);
    return mc195_skin_root_membership((uintptr_t)a->manager, (uintptr_t)a->pack, a->id, read_checked, nullptr);
}
int guarded_append(void *ctx, const lce195_skin_ops *ops, void *manager, void *pack) {
    auto *desc = (const mc195_generic_skin_descriptor *)ctx;
    if (!desc || !ops || !ops->add_pack || !manager || !pack || !scope(nullptr)) return 0;
    Append arg{ops, manager, pack, desc->id};
    return mc195_pack_roots_append((uintptr_t)pack, do_append, &arg) == 1;
}
int run(unsigned i) {
    if (i >= count || !scope(nullptr)) return 0;
    __atomic_store_n(&status[i], 1u, __ATOMIC_RELEASE);
    if (!mc195_barebones_root_guard_installed() || !mc195_barebones_manager_guard_installed()) { __atomic_store_n(&status[i], 3u, __ATOMIC_RELEASE); return 1; }
    auto *gate = (lce195_registry_gate *)mc195_gre_scope_registry();
    uintptr_t storage = 0, app = 0, textures = 0;
    if (!gate || !read_checked(nullptr, base + 0x147cb78, &storage, 8) || !storage || !read_checked(nullptr, base + 0x1464780, &app, 8) || !app ||
        !read_checked(nullptr, app + 0x1f0, &textures, 8) || !textures || !span(textures, 0x19c, 3)) {
        __atomic_store_n(&status[i], 3u, __ATOMIC_RELEASE); return 1;
    }
    unsigned phase = __atomic_load_n(&catalog.entries[i].owner.phase, __ATOMIC_ACQUIRE);
    if (phase == MC195_GRE_EMPTY) {
        if (!inputs[i]) inputs[i] = (unsigned char *)malloc((size_t)entries[i].size);
        if (!inputs[i] || mc195_generic_catalog_load(&catalog, i, &files, inputs[i], (size_t)entries[i].size) != MC195_GRE_OK) {
            __atomic_store_n(&status[i], 3u, __ATOMIC_RELEASE); return 1;
        }
    } else if (phase != MC195_GRE_READY) { __atomic_store_n(&status[i], 4u, __ATOMIC_RELEASE); return -1; }
    mc195_texture_cache_lease cache{};
    int acquired = mc195_cache_lease_begin(&cache, textures, tid(), 1, read_checked, write_checked, nullptr, gate->native_trylock, gate->native_unlock);
    if (acquired < 0) { __atomic_store_n(&status[i], 4u, __ATOMIC_RELEASE); return -1; }
    if (!acquired) {
        if (++cache_attempts[i] >= max_cache_attempts) { __atomic_store_n(&status[i], 3u, __ATOMIC_RELEASE); return 1; }
        __atomic_store_n(&status[i], 0u, __ATOMIC_RELEASE); return 0;
    }
    mc195_generic_preflight_context checks{}; mc195_generic_resource_checks resources{};
    resources.base = base; resources.read = read_checked; resources.outer_scope_held = scope; resources.thread_id = tid; resources.cache_lease = &cache;
    lce195_mutex_fn lock = nullptr, unlock = nullptr;
    bool initialized = lce195_bind_skin_mutex(base, 1, &lock, &unlock) && mc195_generic_resource_checks_bind_observed(&resources, &checks, lock, unlock);
    mc195_generic_borrow borrow{}; borrow.checks = &checks; borrow.gate = gate; borrow.required_thread = tid(); borrow.current_thread = tid;
    mc195_generic_scoped_owner owner{}; owner.storage = storage; owner.manager = base + 0x143ef80; owner.required_thread = tid(); owner.thread_id = tid;
    owner.scope_held = scope; owner.read = read_checked; owner.borrow = &borrow;
    mc195_gre_owner_ops ops{}; initialized = initialized && mc195_generic_scoped_owner_bind(&owner, &ops);
    struct mc195_pump_snapshot sample{}; mc195_storage_pump_observe_snapshot(&sample);
    mc195_gre_owner_result result = MC195_GRE_INVALID;
    if (initialized) {
        auto &e = catalog.entries[i];
        result = mc195_generic_owner_publish_guarded(&e.owner, &ops, &sample, storage, &skin, (void *)owner.manager, read_checked, nullptr,
                                                     mc195_generic_borrow_pre, mc195_generic_borrow_post, &borrow, guarded_append, (void *)desc_ptrs[i]);
    }
    int released = mc195_cache_lease_end(&cache, tid());
    if (released != 1 || catalog.entries[i].owner.phase == MC195_GRE_POISONED) { __atomic_store_n(&status[i], 4u, __ATOMIC_RELEASE); return -1; }
    __atomic_store_n(&status[i], result == MC195_GRE_OK ? 2u : 3u, __ATOMIC_RELEASE);
    return 1;
}
int operation(void *) {
    if (!scope(nullptr)) return 0;
    bool pending = false;
    for (unsigned i = 0; i < count; ++i)
        if (__atomic_load_n(&status[i], __ATOMIC_ACQUIRE) == 0) { int r = run(i); if (r < 0) return -1; if (!r) pending = true; }
    return pending ? 0 : 1;
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
}

// 1 if at least one downloaded skin pack is installed (scans once).
extern "C" __attribute__((visibility("hidden"))) int mc195_dyn_skins_present() { scan_once(); return count != 0; }

extern "C" __attribute__((visibility("hidden"))) int mc195_dyn_skins_bind() {
    if (bound) return 1;
    bound = 1;
    scan_once();
    if (!count) return 1;
    base = game_base();
    if (!base || base > UINTPTR_MAX - 0x147cb78) return 0;
    for (unsigned i = 0; i < count; ++i) {
        descs[i].id = entries[i].dlc_id;
        descs[i].skin_count = entries[i].skin_count;
        descs[i].byte_count = (size_t)entries[i].size;
        memcpy(descs[i].sha256, entries[i].sha256, 32);
        memcpy(descs[i].slug, entries[i].slug, sizeof descs[i].slug);
        desc_ptrs[i] = &descs[i];
    }
    if (!mc195_generic_catalog_init(&catalog, desc_ptrs, count) || !lce195_game_file_ops(base, 1, &files) || !lce195_skin_bind(base, 1, &skin)) {
        mc195_remote_log("Downloaded skins: catalog/bind unavailable\n");
        return 0;
    }
    if (!mc195_gre_set_title_operation(operation, &catalog)) { mc195_remote_log("Downloaded skins: title operation unavailable\n"); return 0; }
    mc195_remote_log("Downloaded skins: registration scheduled\n");
    return 1;
}

extern "C" __attribute__((visibility("hidden"))) unsigned mc195_dyn_skins_status(unsigned i) {
    return i < count ? __atomic_load_n(&status[i], __ATOMIC_ACQUIRE) : 3u;
}
