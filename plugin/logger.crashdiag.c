#include "logger.h"
#include "native_abi.h"
#include <orbis/libkernel.h>
#include <stddef.h>

/* Deliberately avoid OpenOrbis signal.h: its Linux ABI does not describe
 * the PS4 libkernel interface. The explicit prefix lives in native_abi.h. */
extern int mc195_native_sigaction(int, const Mc195Sigaction *, Mc195Sigaction *) __asm__("sigaction");
extern int mc195_native_getcontext(void *) __asm__("getcontext");
extern int mc195_native_raise(int) __asm__("raise");
extern void mc195_native_exit(int) __asm__("_exit") __attribute__((noreturn));

static int log_fd = -1;
static uintptr_t game_base;
static unsigned started, in_fault, last_stage = 0xffffffffu, sequence, io_errors;
static uint64_t seen_stages;
static Mc195Sigaction prior[8];
static const int fault_signals[8] = {4, 5, 6, 7, 8, 10, 11, 12};
static unsigned installed[8];

struct Line { char bytes[2048]; size_t used; };
static void literal(struct Line *l, const char *s) {
    while (*s && l->used + 1 < sizeof l->bytes) l->bytes[l->used++] = *s++;
}
static void number(struct Line *l, uint64_t v) {
    static const char hex[] = "0123456789abcdef";
    literal(l, "0x");
    for (int shift = 60; shift >= 0; shift -= 4)
        if (l->used + 1 < sizeof l->bytes) l->bytes[l->used++] = hex[(v >> shift) & 15];
}
static void field(struct Line *l, const char *name, uint64_t value) {
    literal(l, " "); literal(l, name); literal(l, "="); number(l, value);
}
static void begin(struct Line *l, const char *kind) {
    l->used = 0; literal(l, "MC195_CRASH "); literal(l, kind);
    field(l, "seq", __atomic_add_fetch(&sequence, 1, __ATOMIC_RELAXED));
}
/* Finite cold-launch checkpoints only. No render-frame logging and no
 * busy admission flag: an accepted checkpoint is never silently dropped.
 * fd is retained until process exit, including during a synchronous fault. */
static void commit(struct Line *l) {
    if (l->used + 1 < sizeof l->bytes) l->bytes[l->used++] = '\n';
    int fd = __atomic_load_n(&log_fd, __ATOMIC_ACQUIRE);
    if (fd < 0) { __atomic_add_fetch(&io_errors, 1, __ATOMIC_RELAXED); return; }
    size_t done = 0;
    while (done < l->used) {
        int64_t n = (int64_t)sceKernelWrite(fd, l->bytes + done, l->used - done);
        if (n <= 0 || (uint64_t)n > l->used - done) {
            __atomic_add_fetch(&io_errors, 1, __ATOMIC_RELAXED); break;
        }
        done += (size_t)n;
    }
    if (sceKernelFsync(fd) < 0) __atomic_add_fetch(&io_errors, 1, __ATOMIC_RELAXED);
}

static void fault_handler(int signal_number, Mc195Siginfo *info, void *context) {
    /* BSD SI_USER/SI_LWP/etc occupy 0x10000+, unlike Linux's negative
     * sent-signal codes. Hardware fault reason codes are small positives. */
    int asynchronous = signal_number == 6 || !info || info->code <= 0 || info->code >= 0x10000;
    /* Synchronous faults re-execute under the old disposition. A raised
     * signal must instead be queued again while blocked by this handler. */
    if (!__atomic_exchange_n(&in_fault, 1, __ATOMIC_ACQ_REL)) {
        struct Line l; begin(&l, "FAULT");
        field(&l, "signal", (uint32_t)signal_number);
        field(&l, "tid", (uint32_t)scePthreadGetthreadid());
        field(&l, "last_stage", __atomic_load_n(&last_stage, __ATOMIC_ACQUIRE));
        field(&l, "seen", __atomic_load_n(&seen_stages, __ATOMIC_ACQUIRE));
        field(&l, "io_errors", __atomic_load_n(&io_errors, __ATOMIC_RELAXED));
        field(&l, "context", (uintptr_t)context);
        if (info) {
            field(&l, "signo", (uint32_t)info->signo);
            field(&l, "code", (uint32_t)info->code);
            field(&l, "address", (uintptr_t)info->addr);
        }
        if (context) {
            const Mc195McontextPrefix *m = &((const Mc195UcontextPrefix *)((const unsigned char *)context + 0x30))->mcontext;
            field(&l, "cs", m->cs); field(&l, "ss", m->ss);
            /* Only decode the proven BSD prefix, never walk the stack or
             * dereference registers/code pointers from a damaged process. */
            if (m->cs == 0x43 && m->ss == 0x3b) {
                field(&l, "rip", m->rip); field(&l, "rsp", m->rsp); field(&l, "rbp", m->rbp);
                field(&l, "game_offset", m->rip >= game_base ? m->rip - game_base : UINT64_MAX);
                field(&l, "rdi", m->rdi); field(&l, "rsi", m->rsi); field(&l, "rdx", m->rdx);
                field(&l, "rcx", m->rcx); field(&l, "rax", m->rax); field(&l, "rbx", m->rbx);
                field(&l, "r8", m->r8); field(&l, "r9", m->r9); field(&l, "r10", m->r10);
                field(&l, "r11", m->r11); field(&l, "r12", m->r12); field(&l, "r13", m->r13);
                field(&l, "r14", m->r14); field(&l, "r15", m->r15);
                field(&l, "trap", m->trapno); field(&l, "error", m->err);
            } else {
                literal(&l, " context_layout=unrecognized");
                /* Bounded raw view of the borrowed context for layout recovery. */
                const uint64_t *w = (const uint64_t *)context;
                for (unsigned k = 0; k < 32; ++k) field(&l, "w", w[k]);
            }
        }
        commit(&l);
    }
    for (unsigned i = 0; i < 8; ++i) {
        if (fault_signals[i] == signal_number && __atomic_load_n(&installed[i], __ATOMIC_ACQUIRE)) {
            int rc = mc195_native_sigaction(signal_number, &prior[i], 0);
            struct Line l; begin(&l, "RESTORE"); field(&l, "signal", signal_number);
            field(&l, "rc", (uint32_t)rc); commit(&l);
            if (rc != 0) {
                Mc195Sigaction fallback = {0};
                rc = mc195_native_sigaction(signal_number, &fallback, 0);
                if (rc != 0) mc195_native_exit(128 + signal_number);
            }
            if (asynchronous) (void)mc195_native_raise(signal_number);
            break;
        }
    }
}

static void module_map(void) {
    OrbisKernelModule handles[256];
    for (unsigned i = 0; i < 256; ++i) handles[i] = (OrbisKernelModule)-1;
    size_t count = 0;
    int rc = sceKernelGetModuleList(handles, 256, &count);
    struct Line l; begin(&l, "MODULE_LIST"); field(&l, "rc", (uint32_t)rc);
    field(&l, "count", count); field(&l, "capacity_argument", 256); commit(&l);
    if (rc < 0) return;
    if (count > 256) count = 256;
    for (size_t i = 0; i < count; ++i) {
        if (handles[i] == (OrbisKernelModule)-1) continue;
        OrbisKernelModuleInfo m = {0}; m.size = sizeof m;
        rc = sceKernelGetModuleInfo(handles[i], &m);
        begin(&l, "MODULE"); field(&l, "handle", handles[i]); field(&l, "rc", (uint32_t)rc);
        if (rc == 0) {
            literal(&l, " name=");
            for (unsigned j = 0; j < sizeof m.name && m.name[j]; ++j) {
                char c = m.name[j]; if (c < 32 || c > 126) c = '?';
                if (l.used + 1 < sizeof l.bytes) l.bytes[l.used++] = c;
            }
            field(&l, "segments", m.segmentCount);
            unsigned segments = m.segmentCount < 4 ? m.segmentCount : 4;
            for (unsigned j = 0; j < segments; ++j) {
                field(&l, "index", j); field(&l, "address", (uintptr_t)m.segmentInfo[j].address);
                field(&l, "size", m.segmentInfo[j].size); field(&l, "prot", (uint32_t)m.segmentInfo[j].prot);
            }
        }
        commit(&l);
    }
}

void mc195_crashlog_start(uintptr_t base) {
    if (__atomic_exchange_n(&started, 1, __ATOMIC_ACQ_REL)) return;
    game_base = base;
    int fd = sceKernelOpen("/data/mc-195-mods-crash.log", 0x209, 0600);
    __atomic_store_n(&log_fd, fd, __ATOMIC_RELEASE);
    if (fd < 0) return;
    struct Line l; begin(&l, "START"); literal(&l, " build=mods-crash-diag-r7-wide");
    field(&l, "base", base); field(&l, "logger", (uintptr_t)&mc195_crashlog_start);
    field(&l, "tid", (uint32_t)scePthreadGetthreadid()); commit(&l);
    module_map();
    /* Check the actual runtime context prefix before installing fatal
     * handlers. Storage exceeds the native context incl. extended state. */
    union { uint64_t align[512]; unsigned char bytes[4096]; } storage __attribute__((aligned(16))) = {{0}};
    int rc = mc195_native_getcontext(storage.bytes);
    const Mc195McontextPrefix *m = &((const Mc195UcontextPrefix *)storage.bytes)->mcontext;
    int context_ok = rc == 0 && m->cs == 0x43 && m->ss == 0x3b && m->rip && m->rsp;
    begin(&l, "CONTEXT_ABI"); field(&l, "rc", (uint32_t)rc);
    field(&l, "valid", context_ok); field(&l, "cs", m->cs); field(&l, "ss", m->ss);
    field(&l, "rip", m->rip); field(&l, "rsp", m->rsp); commit(&l);
    /* On this title the expected prefix failed its startup gate. Preserve a
     * bounded raw sample to investigate the actual getcontext layout on a
     * future launch. These are opaque words, not decoded fault registers;
     * successful getcontext alone does not prove signal-delivery ABI. */
    if (rc == 0 && !context_ok) {
        for (unsigned i = 0; i < 40; i += 8) {
            begin(&l, "CONTEXT_WORDS"); field(&l, "offset", i * 8);
            for (unsigned j = 0; j < 8; ++j) field(&l, "word", storage.align[i + j]);
            commit(&l);
        }
    }
    /* getcontext() does not populate the trap-frame selectors this gate
     * tested, so it never passed on console. The fault handler validates the
     * kernel-delivered context itself; install regardless. */
    for (unsigned i = 0; i < 8; ++i) {
        Mc195Sigaction next = {0}; next.handler.info = fault_handler; next.flags = MC195_SA_SIGINFO | 1;
        rc = mc195_native_sigaction(fault_signals[i], 0, &prior[i]);
        if (rc == 0) {
            __atomic_store_n(&installed[i], 1, __ATOMIC_RELEASE);
            rc = mc195_native_sigaction(fault_signals[i], &next, 0);
            if (rc != 0) __atomic_store_n(&installed[i], 0, __ATOMIC_RELEASE);
        }
        begin(&l, "HANDLER"); field(&l, "signal", fault_signals[i]); field(&l, "rc", (uint32_t)rc);
        field(&l, "previous", (uintptr_t)prior[i].handler.simple); field(&l, "flags", (uint32_t)prior[i].flags);
        commit(&l);
    }
}

void mc195_crashlog_stage(unsigned stage, uintptr_t scene, unsigned detail) {
    __atomic_store_n(&last_stage, stage, __ATOMIC_RELEASE);
    if (stage < 64) __atomic_fetch_or(&seen_stages, UINT64_C(1) << stage, __ATOMIC_RELEASE);
    struct Line l; begin(&l, "STAGE"); field(&l, "stage", stage); field(&l, "scene", scene);
    field(&l, "detail", detail); field(&l, "tid", (uint32_t)scePthreadGetthreadid());
    field(&l, "io_errors", __atomic_load_n(&io_errors, __ATOMIC_RELAXED)); commit(&l);
}

/* The title replaces fatal-signal dispositions after plugin start. Re-install
 * ours over whatever is current, keeping that as the chained prior handler. */
void mc195_crashlog_rearm(void) {
    if (!__atomic_load_n(&started, __ATOMIC_ACQUIRE)) return;
    for (unsigned i = 0; i < 8; ++i) {
        Mc195Sigaction current = {0};
        int rc = mc195_native_sigaction(fault_signals[i], 0, &current);
        if (rc != 0 || current.handler.info == fault_handler) continue;
        Mc195Sigaction next = {0}; next.handler.info = fault_handler; next.flags = MC195_SA_SIGINFO | 1 /* SA_ONSTACK */;
        prior[i] = current;
        rc = mc195_native_sigaction(fault_signals[i], &next, 0);
        __atomic_store_n(&installed[i], rc == 0, __ATOMIC_RELEASE);
        struct Line l; begin(&l, "REARM"); field(&l, "signal", fault_signals[i]); field(&l, "rc", (uint32_t)rc);
        field(&l, "previous", (uintptr_t)current.handler.simple); field(&l, "flags", (uint32_t)current.flags);
        commit(&l);
    }
}

/* PS4-native exception path: ordinary sigaction handlers never observed the
 * title's fatal faults. Dump a bounded raw view of the delivered context
 * (layout recovered offline), then remove the handler so the fault recurs
 * under the system default. */
extern int sceKernelInstallExceptionHandler(int, void (*)(int, void *));
extern int sceKernelRemoveExceptionHandler(int);
static unsigned exc_installed, in_exc;
static void exc_handler(int signum, void *context) {
    if (!__atomic_exchange_n(&in_exc, 1, __ATOMIC_ACQ_REL)) {
        struct Line l; begin(&l, "EXC"); field(&l, "signal", (uint32_t)signum);
        field(&l, "tid", (uint32_t)scePthreadGetthreadid()); field(&l, "base", game_base);
        field(&l, "last_stage", __atomic_load_n(&last_stage, __ATOMIC_ACQUIRE));
        field(&l, "context", (uintptr_t)context); commit(&l);
        if (context) {
            const uint64_t *w = (const uint64_t *)context;
            for (unsigned i = 0; i < 64; i += 8) {
                begin(&l, "EXC_WORDS"); field(&l, "offset", i * 8);
                for (unsigned j = 0; j < 8; ++j) field(&l, "w", w[i + j]);
                commit(&l);
            }
        }
    }
    for (unsigned i = 0; i < 8; ++i) if (fault_signals[i] != 6) (void)sceKernelRemoveExceptionHandler(fault_signals[i]);
}
void mc195_crashlog_exceptions(void) {
    if (!__atomic_load_n(&started, __ATOMIC_ACQUIRE) || __atomic_exchange_n(&exc_installed, 1, __ATOMIC_ACQ_REL)) return;
    for (unsigned i = 0; i < 8; ++i) {
        if (fault_signals[i] == 6) continue;
        int rc = sceKernelInstallExceptionHandler(fault_signals[i], exc_handler);
        struct Line l; begin(&l, "EXC_INSTALL"); field(&l, "signal", fault_signals[i]); field(&l, "rc", (uint32_t)rc); commit(&l);
    }
}

/* A stack overflow cannot run a handler on the exhausted stack. Give each
 * thread that passes through our hooks a private alternate signal stack. */
typedef struct { void *sp; size_t size; int flags; } Mc195Stack;
extern int mc195_native_sigaltstack(const Mc195Stack *, Mc195Stack *) __asm__("sigaltstack");
static unsigned alt_tids[8], alt_used;
static unsigned char alt_stacks[8][65536] __attribute__((aligned(16)));
void mc195_crashlog_thread(void) {
    if (!__atomic_load_n(&started, __ATOMIC_ACQUIRE)) return;
    unsigned tid = (unsigned)scePthreadGetthreadid();
    unsigned n = __atomic_load_n(&alt_used, __ATOMIC_ACQUIRE);
    for (unsigned i = 0; i < n && i < 8; ++i) if (__atomic_load_n(&alt_tids[i], __ATOMIC_ACQUIRE) == tid) return;
    unsigned slot = __atomic_fetch_add(&alt_used, 1, __ATOMIC_ACQ_REL);
    if (slot >= 8) { __atomic_store_n(&alt_used, 8, __ATOMIC_RELEASE); return; }
    __atomic_store_n(&alt_tids[slot], tid, __ATOMIC_RELEASE);
    Mc195Stack st = { alt_stacks[slot], sizeof alt_stacks[slot], 0 };
    int rc = mc195_native_sigaltstack(&st, 0);
    struct Line l; begin(&l, "ALTSTACK"); field(&l, "tid", tid); field(&l, "slot", slot); field(&l, "rc", (uint32_t)rc); commit(&l);
}
