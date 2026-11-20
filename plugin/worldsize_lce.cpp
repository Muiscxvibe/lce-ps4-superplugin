// Console glue for worldsize195 (Huge local worlds). Pumped from the plugin
// monitor thread (debug_main.item_diag.cpp, every 100 ms) with the session
// state: the game-code words are installed only while this console is not
// joining/joined to an LCE server, and reverted before any join proceeds.
//
// Patch mechanics follow the console-proven pattern in debug_main
// (install_dialog_trace): relay page mmapped within rel32 range, page made
// writable only for one 8-byte CAS, protection restored. Relays are never
// unmapped (a thread may still be inside one after a revert).
#include "worldsize195.hpp"

#include <orbis/libkernel.h>
#include <stdio.h>
#include <string.h>

extern "C" void mc195_remote_log(const char *m);  // debug_main logline
#include "mod_gate.hpp"  // lce_mod_gate_record: anticheat canonical hash reverts these words

namespace {
ws195::Policy g_policy;
ws195::Relays g_relays{0, 0};
// Relay mapping: 0x8000 bytes; code page (RX) at +0: create @0, render @0x100, diag relays @0x200;
// data page (RW) at +0x4000: diag counters @+0 (u64 each), Huge-created flag (u32) @+0xff8.
uintptr_t g_page, g_data;
constexpr uintptr_t kDiagRelays = 0x200, kCreatedFlag = 0xff8;
int g_armed;
bool g_label;

void log_line(const char *m) { mc195_remote_log(m); }

int read8(void *, uintptr_t addr, uint64_t *out) {
    *out = __atomic_load_n((const uint64_t *)addr, __ATOMIC_ACQUIRE);
    return 1;
}
int cas8(void *, uintptr_t addr, uint64_t expect, uint64_t want) {
    uintptr_t page = addr & ~(uintptr_t)0x3fff;
    void *lo = nullptr, *hi = nullptr;
    int32_t prot = 0;
    if (sceKernelQueryMemoryProtection((void *)addr, &lo, &hi, &prot) || (uintptr_t)lo > addr || (uintptr_t)hi < addr + 8) return 0;
    const bool writable = (prot & 2) != 0;
    if (!writable && sceKernelMprotect((void *)page, 0x4000, prot | 2)) return 0;
    uint64_t e = expect;
    bool ok = __atomic_compare_exchange_n((uint64_t *)addr, &e, want, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    int restore = writable ? 0 : sceKernelMprotect((void *)page, 0x4000, prot);
    if (restore) log_line("WORLDSIZE page protection restore failed\n");
    return ok && !restore;
}
void record(void *, uintptr_t addr, uint64_t pristine, uint64_t patched) {
    uint8_t p[8], q[8];
    for (int i = 0; i < 8; ++i) { p[i] = (uint8_t)(pristine >> (8 * i)); q[i] = (uint8_t)(patched >> (8 * i)); }
    (void)lce_mod_gate_record(addr, p, q);
}

uintptr_t map_relay(uintptr_t base) {
    const uintptr_t page = base & ~(uintptr_t)0x3fff;
    for (uintptr_t shift = 0x10000000; shift <= 0x40000000; shift *= 2) {
        if (page > UINTPTR_MAX - shift) break;
        void *p = nullptr;
        if (sceKernelMmap((void *)(page + shift), 0x8000, 3, 0x1002, -1, 0, &p) || !p) continue;
        // Every patch site must reach the page with rel32 (the lowest and highest site bound the range).
        if (ws195::jmp_word(base + 0x8e300, (uintptr_t)p + 0x400) && ws195::jmp_word(base + 0xc58380, (uintptr_t)p + 0x100) &&
            ws195::jmp_word(base + 0xe978d0, (uintptr_t)p))
            return (uintptr_t)p;
        sceKernelMunmap(p, 0x8000);
    }
    return 0;
}

bool ensure_relays(uintptr_t base) {
    if (g_relays.render) return true;
    uintptr_t page = map_relay(base);
    if (!page) return false;
    auto *code = (uint8_t *)page;
    memset(code, 0xcc, 0x4000);
    memset(code + 0x4000, 0, 0x4000);
    bool ok = ws195::build_create_relay(code, 0x100, base, page, page + 0x4000 + kCreatedFlag) &&
              ws195::build_render_relay(code + 0x100, 0x100, base);
    for (size_t i = 0; ok && i < ws195::kDiagCount; ++i) {
        uintptr_t self = page + kDiagRelays + i * ws195::kDiagRelayStride;
        ok = ws195::build_diag_relay(code + kDiagRelays + i * ws195::kDiagRelayStride, ws195::kDiagRelayStride, self,
                                     page + 0x4000 + i * 8, base + ws195::kDiagSites[i].rva) != 0;
    }
    if (!ok || sceKernelMprotect((void *)page, 0x4000, 5)) {
        sceKernelMunmap((void *)page, 0x8000);
        return false;
    }
    g_page = page;
    g_data = page + 0x4000;
    g_relays.create = page;
    g_relays.render = page + 0x100;
    return true;
}

// D1: the label for slider index 4 (RW .data, not part of the hashed RX).
void set_label(uintptr_t base, bool on) {
    auto *w = (uint64_t *)(base + ws195::kLabelWordRva);
    uint64_t from = on ? ws195::kLabelWordPristine : ws195::kLabelWordPatched;
    uint64_t to = on ? ws195::kLabelWordPatched : ws195::kLabelWordPristine;
    if (__atomic_compare_exchange_n(w, &from, to, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST) || from == to) g_label = on;
}

bool marker(const char *path) {
    OrbisKernelStat st;
    return sceKernelStat(path, &st) == 0;
}

// ---- BiomeCacheLayer window (RELRO vtable slot, not part of the hashed RX) ----
uintptr_t g_cache_base;
volatile int g_cache_lock;
void cache_lock(void) { while (__atomic_exchange_n(&g_cache_lock, 1, __ATOMIC_ACQUIRE)) __builtin_ia32_pause(); }
void cache_unlock(void) { __atomic_store_n(&g_cache_lock, 0, __ATOMIC_RELEASE); }
extern "C" ws195::IntArr lce_ws_biome_cache_get_area(void *self, int x, int z, int w, int h) {
    ws195::CacheEnv env{(ws195::GetAreaFn)(g_cache_base + ws195::kBiomeCacheGetArea), cache_lock, cache_unlock};
    return ws195::biome_cache_get_area(self, x, z, w, h, env);
}
int cache_slot(uintptr_t base, bool on) {
    uintptr_t slot = base + ws195::kBiomeCacheSlotRva, orig = base + ws195::kBiomeCacheGetArea, mine = (uintptr_t)&lce_ws_biome_cache_get_area;
    g_cache_base = base;
    uint64_t cur = __atomic_load_n((uint64_t *)slot, __ATOMIC_ACQUIRE);
    uint64_t from = on ? orig : mine, to = on ? mine : orig;
    if (cur == to) return 1;
    if (cur != from) return 0;   // someone else owns the slot: leave it
    return cas8(nullptr, slot, from, to);
}

// ---- creation-stall diagnostics -------------------------------------------------
// When a Huge world is created (the create relay bumps the flag) the 17 entry counters
// are installed for 15 minutes and logged every 2 s while they change, so a stalled
// creation shows which stage is still running (or that nothing listed runs any more).
struct Diag { bool on; uint64_t until_us, last_log_us, last_change_us; uint32_t flag_seen; uint64_t last[ws195::kDiagCount]; bool idle_logged; };
Diag g_diag;
void diag_pump(uintptr_t base, uint64_t now, bool want_armed, ws195::Mem &mem) {
    if (!g_data) return;
    char line[512];
    uint32_t flag = __atomic_load_n((uint32_t *)(g_data + kCreatedFlag), __ATOMIC_ACQUIRE);
    if (flag != g_diag.flag_seen && want_armed && !g_diag.on) {
        unsigned changed = 0;
        int rc = ws195::diag_apply(base, g_page + kDiagRelays, mem, &changed);
        g_diag.on = rc != ws195::kRefused;
        g_diag.until_us = now + 15ull * 60 * 1000000;
        g_diag.last_change_us = now;
        g_diag.idle_logged = false;
        snprintf(line, sizeof line, "WORLDSIZE_DIAG huge_created=%u install rc=%d changed=%u\n", flag, rc, changed);
        log_line(line);
    }
    g_diag.flag_seen = flag;
    if (!g_diag.on) return;
    bool changed_any = false;
    uint64_t cur[ws195::kDiagCount];
    for (size_t i = 0; i < ws195::kDiagCount; ++i) {
        cur[i] = __atomic_load_n((uint64_t *)(g_data + i * 8), __ATOMIC_ACQUIRE);
        if (cur[i] != g_diag.last[i]) changed_any = true;
    }
    if (changed_any) { g_diag.last_change_us = now; g_diag.idle_logged = false; }
    bool idle = !changed_any && now - g_diag.last_change_us > 10000000ull && !g_diag.idle_logged;
    if ((changed_any && now - g_diag.last_log_us >= 2000000ull) || idle) {
        size_t n = (size_t)snprintf(line, sizeof line, "WORLDSIZE_DIAG%s", idle ? " IDLE10s" : "");
        for (size_t i = 0; i < ws195::kDiagCount && n < sizeof line - 40; ++i)
            n += (size_t)snprintf(line + n, sizeof line - n, " %s=%llu", ws195::kDiagSites[i].name, (unsigned long long)cur[i]);
        snprintf(line + n, sizeof line - n, "\n");
        log_line(line);
        memcpy(g_diag.last, cur, sizeof cur);
        g_diag.last_log_us = now;
        if (idle) g_diag.idle_logged = true;
    }
    if (now > g_diag.until_us || !want_armed) {
        unsigned changed = 0;
        int rc = ws195::diag_revert(base, g_page + kDiagRelays, mem, &changed);
        if (rc == ws195::kOk) g_diag.on = false;
        snprintf(line, sizeof line, "WORLDSIZE_DIAG remove rc=%d changed=%u\n", rc, changed);
        log_line(line);
    }
}
}  // namespace

extern "C" __attribute__((visibility("hidden"))) int lce_worldsize_armed(void) { return __atomic_load_n(&g_armed, __ATOMIC_ACQUIRE); }

extern "C" __attribute__((visibility("hidden"))) void lce_worldsize_pump(uintptr_t base, int remote) {
    if (!base) return;
    const uint64_t now = sceKernelGetProcessTime();
    if (!g_policy.first_us) g_policy.first_us = now;
    static unsigned marker_tick;
    static bool disabled, server_off;
    if (marker_tick++ % 50 == 0) {  // every ~5 s
        disabled = marker("/data/lce-mods/worldsize.off");
        server_off = marker("/data/lce-mods/worldsize-server.off");
    }
    const bool want = ws195::want_armed(g_policy, now, remote != 0, disabled, server_off);
    ws195::Mem mem{nullptr, read8, cas8, record};
    char line[160];
    if (want && !g_policy.armed) {
        if (!ensure_relays(base)) {
            ++g_policy.failures;
            log_line("WORLDSIZE no relay page in range; Huge worlds unavailable\n");
            return;
        }
        unsigned changed = 0;
        int rc = ws195::apply(base, g_relays, mem, &changed);
        int slot = rc == ws195::kOk ? cache_slot(base, true) : 0;
        if (rc == ws195::kOk && !slot) {   // without the window a Huge world would freeze at creation: back out
            unsigned undo = 0;
            (void)ws195::revert(base, g_relays, mem, &undo);
            rc = ws195::kRefused;
            log_line("WORLDSIZE biome cache slot not hookable; Huge worlds off\n");
        }
        if (rc == ws195::kOk) {
            g_policy.armed = true;
            set_label(base, true);
            __atomic_store_n(&g_armed, 1, __ATOMIC_RELEASE);
        } else {
            ++g_policy.failures;
        }
        snprintf(line, sizeof line, "WORLDSIZE arm rc=%d changed=%u label=%d max_chunks=%u\n", rc, changed, g_label ? 1 : 0,
                 rc == ws195::kOk ? ws195::kHugeChunks : 320u);
        log_line(line);
    } else if (!want && g_policy.armed) {
        set_label(base, false);
        unsigned changed = 0;
        int rc = ws195::revert(base, g_relays, mem, &changed);
        if (!cache_slot(base, false)) rc = ws195::kPartial;
        static int last_rc = 2;
        if (rc == ws195::kOk) {
            g_policy.armed = false;
            __atomic_store_n(&g_armed, 0, __ATOMIC_RELEASE);
        }
        if (rc != last_rc || changed) {
            snprintf(line, sizeof line, "WORLDSIZE disarm rc=%d changed=%u remote=%d disabled=%d\n", rc, changed, remote, disabled ? 1 : 0);
            log_line(line);
        }
        last_rc = rc == ws195::kOk ? 2 : rc;
    }
    diag_pump(base, now, want, mem);
}
