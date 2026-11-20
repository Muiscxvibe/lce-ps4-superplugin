// PS4 host services for the loader (OpenOrbis libkernel). Console build only.
// No privileged syscalls: only file I/O, getdents, memory-protection queries
// and thread ids, all already used by the shipping plugin (dyn_skins.cpp,
// tools_runtime.cpp).
#include "host_ps4.h"
#include <orbis/libkernel.h>
#include <stdio.h>
#include <string.h>

namespace {
constexpr int kRdOnly = 0x0000, kWrOnly = 0x0001, kCreat = 0x0200, kTrunc = 0x0400, kDirectory = 0x20000;

long read_file(void *, const char *path, uint8_t *buf, size_t cap) {
    int fd = sceKernelOpen(path, kRdOnly, 0);
    if (fd < 0) return -1;
    size_t total = 0;
    for (;;) {
        if (total == cap) {  // probe for more data: larger than the caller allows
            uint8_t extra;
            long more = (long)sceKernelRead(fd, &extra, 1);
            sceKernelClose(fd);
            return more > 0 ? -2 : (long)total;
        }
        long n = (long)sceKernelRead(fd, buf + total, cap - total);
        if (n < 0) { sceKernelClose(fd); return -1; }
        if (n == 0) break;
        total += (size_t)n;
    }
    sceKernelClose(fd);
    return (long)total;
}

void ensure_parent(const char *path) {
    char dir[256];
    size_t n = strlen(path);
    if (n >= sizeof dir) return;
    memcpy(dir, path, n + 1);
    char *slash = strrchr(dir, '/');
    if (!slash || slash == dir) return;
    *slash = 0;
    OrbisKernelStat st;
    if (sceKernelStat(dir, &st) != 0) sceKernelMkdir(dir, 0700);
}

int write_file_atomic(void *, const char *path, const uint8_t *data, size_t len) {
    char tmp[272];
    if (snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int)sizeof tmp) return 0;
    ensure_parent(path);
    int fd = sceKernelOpen(tmp, kWrOnly | kCreat | kTrunc, 0600);
    if (fd < 0) return 0;
    size_t done = 0;
    while (done < len) {
        long n = (long)sceKernelWrite(fd, data + done, len - done);
        if (n <= 0) { sceKernelClose(fd); return 0; }
        done += (size_t)n;
    }
    int ok = sceKernelFsync(fd) == 0;
    sceKernelClose(fd);
    if (!ok) return 0;
    // keep the previous ledger as .bak for manual recovery (never read automatically)
    char bak[272];
    if (snprintf(bak, sizeof bak, "%s.bak", path) < (int)sizeof bak) {
        OrbisKernelStat st;
        if (sceKernelStat(path, &st) == 0) { sceKernelUnlink(bak); sceKernelRename(path, bak); }
    }
    return sceKernelRename(tmp, path) == 0;
}

int list_dirs(void *, const char *dir, void (*emit)(void *, const char *), void *ctx) {
    int fd = sceKernelOpen(dir, kDirectory, 0);
    if (fd < 0) return 0;
    static char buf[4096];
    for (;;) {
        int n = sceKernelGetdents(fd, buf, sizeof buf);
        if (n <= 0) break;
        for (int off = 0; off + 8 <= n;) {  // BSD dirent: u32 ino, u16 reclen, u8 type, u8 namlen, name
            uint16_t reclen = (uint16_t)((buf[off + 4] & 0xff) | ((buf[off + 5] & 0xff) << 8));
            uint8_t type = (uint8_t)buf[off + 6], namlen = (uint8_t)buf[off + 7];
            if (reclen < 8 || off + reclen > n) break;
            if (type == 4 && namlen && namlen < 32 && 8 + namlen <= reclen) {
                char name[33];
                memcpy(name, buf + off + 8, namlen);
                name[namlen] = 0;
                if (name[0] != '.') emit(ctx, name);
            }
            off += reclen;
        }
    }
    sceKernelClose(fd);
    return 1;
}

int exists(void *, const char *path) {
    OrbisKernelStat st;
    return sceKernelStat(path, &st) == 0;
}

bool mapped(uintptr_t a, size_t n, int bits) {
    void *lo = nullptr, *hi = nullptr;
    int32_t prot = 0;
    return a && n && a <= UINTPTR_MAX - n && !sceKernelQueryMemoryProtection((void *)a, &lo, &hi, &prot) &&
           (prot & bits) == bits && (uintptr_t)lo <= a && (uintptr_t)hi >= a + n;
}
}  // namespace

extern "C" void lce_ps4_host_ops(lce_host_ops *ops, void (*log)(void *, const char *), void *log_user) {
    memset(ops, 0, sizeof *ops);
    ops->user = log_user;
    ops->read_file = read_file;
    ops->write_file_atomic = write_file_atomic;
    ops->list_dirs = list_dirs;
    ops->exists = exists;
    ops->load_native = nullptr;  // native mods stay disabled (anticheat module allowlist; see INTEGRATION.md)
    ops->log = log;
}

extern "C" int lce_ps4_mem_read(void *, uintptr_t a, void *out, size_t n) {
    if (!out || !mapped(a, n, 1)) return 0;
    memcpy(out, (const void *)a, n);
    return 1;
}
extern "C" int lce_ps4_mem_write(void *, uintptr_t a, const void *in, size_t n) {
    if (!in || !mapped(a, n, 3)) return 0;
    if (n == 8 && !(a & 7u)) {
        uintptr_t v;
        memcpy(&v, in, 8);
        __atomic_store_n((uintptr_t *)a, v, __ATOMIC_RELEASE);
    } else {
        memcpy((void *)a, in, n);
    }
    return 1;
}
extern "C" int lce_ps4_mem_cas8(void *, uintptr_t a, uint64_t expected, uint64_t desired) {
    if ((a & 7u) || !mapped(a, 8, 3)) return 0;
    return __atomic_compare_exchange_n((uint64_t *)a, &expected, desired, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE) ? 1 : 0;
}
// One aligned 8-byte CAS on a READ-ONLY data page (RELRO vtables, biomes195): never segment 0 (.text,
// hashed by client_report.c), never an executable page. mprotect RW -> CAS -> restore the exact old
// protection -> re-query. 1 swapped, 0 nothing written (refused / value mismatch), -1 protection not restored.
extern "C" int lce_ps4_mem_patch_ro8(uintptr_t base, uintptr_t a, uint64_t expected, uint64_t desired) {
    if (!base || (a & 7u) || (a >= base && a < base + 0x10c0354u)) return 0;
    void *lo = nullptr, *hi = nullptr;
    int32_t prot = 0;
    if (sceKernelQueryMemoryProtection((void *)a, &lo, &hi, &prot) || (prot & 4) || !(prot & 1) || (uintptr_t)hi < a + 8) return 0;
    const uintptr_t page = a & ~(uintptr_t)0x3fff;
    if (prot & 2) return __atomic_compare_exchange_n((uint64_t *)a, &expected, desired, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE) ? 1 : 0;
    if (sceKernelMprotect((void *)page, 0x4000, prot | 2) != 0) return 0;
    int swapped = __atomic_compare_exchange_n((uint64_t *)a, &expected, desired, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE) ? 1 : 0;
    if (sceKernelMprotect((void *)page, 0x4000, prot) != 0) return -1;
    int32_t now = 0;
    if (sceKernelQueryMemoryProtection((void *)a, &lo, &hi, &now) || now != prot) return -1;
    return swapped;
}
extern "C" int lce_ps4_is_code(uintptr_t base, uintptr_t a) {
    // RX segment 0 of eboot.bin: [base, base+0x10c0354) and executable
    return base && a >= base && a < base + 0x10c0354u && mapped(a, 1, 5) ? 1 : 0;
}
extern "C" uint64_t lce_ps4_thread_id(void) {
    int32_t t = scePthreadGetthreadid();
    return t > 0 ? (uint64_t)t : 0;
}
extern "C" uint64_t lce_ps4_now_ms(void) { return sceKernelGetProcessTime() / 1000u; }
