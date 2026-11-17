#define _BSD_SOURCE 1
#define _DEFAULT_SOURCE 1
/* Console identity via GoldHEN (PS4 only).
 *
 * sceKernelGetIdPs / sceKernelGetOpenPsIdForSystem are privileged: from the
 * game process they raise SIGSYS. GoldHEN's SDK syscall (500, MIT-licensed
 * GoldHEN Plugins SDK) can temporarily "jailbreak" this process (root creds,
 * all capabilities, no sandbox prison). We jailbreak, read both IDs under a
 * temporary SIGSYS/SIGSEGV guard (a refusal skips the ID instead of crashing
 * the game), then restore the original credentials immediately.
 *
 * Overrides the weak lce_console_ids_privileged() in client_report.c. */
#include <orbis/libkernel.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>

struct jailbreak_backup {            /* GoldHEN_Plugins_SDK include/GoldHEN.h */
    uint32_t cr_uid, cr_ruid, cr_rgid, cr_groups;
    uint64_t cr_paid;
    uint64_t cr_caps[2];
    void *cr_prison;
    void *fd_cdir, *fd_jdir, *fd_rdir;
};
#define GOLDHEN_SDK_CMD_VERSION     0
#define GOLDHEN_SDK_CMD_JAILBREAK   2
#define GOLDHEN_SDK_CMD_UNJAILBREAK 3

/* Same calling convention as the SDK's orbis_syscall (indirect syscall 0). */
__asm__(
    ".text\n"
    ".globl lce_goldhen_syscall\n"
    ".hidden lce_goldhen_syscall\n"
    "lce_goldhen_syscall:\n"
    "  movq $0, %rax\n"
    "  movq %rcx, %r10\n"
    "  syscall\n"
    "  jb 1f\n"
    "  retq\n"
    "1:\n"
    "  negq %rax\n"          /* carry set: rax holds errno -> return -errno */
    "  retq\n");
long lce_goldhen_syscall(long number, uint64_t cmd, void *data);

void mc195_remote_log(const char *);
int snprintf(char *, size_t, const char *, ...);
static void logf3(const char *what, long a, long b) {
    char line[160];
    snprintf(line, sizeof line, "Console IDs: %s %ld %ld\n", what, a, b);
    mc195_remote_log(line);
}

static int sdk_cmd(uint64_t cmd, void *data) { return (int)lce_goldhen_syscall(500, cmd, data); }

static sigjmp_buf guard_jmp;
static volatile sig_atomic_t guard_armed;
static int32_t guard_rc;
static void guard_handler(int sig) { (void)sig; if (guard_armed) siglongjmp(guard_jmp, 1); }

/* Run fn(out) with SIGSYS/SIGSEGV/SIGBUS turned into a clean failure. */
static int guarded(int32_t (*fn)(void *), void *out) {
    struct sigaction sa, old_sys, old_segv, old_bus;
    int ok = 0;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = guard_handler;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGSYS, &sa, &old_sys) != 0) return 0;
    sigaction(SIGSEGV, &sa, &old_segv);
    sigaction(SIGBUS, &sa, &old_bus);
    if (sigsetjmp(guard_jmp, 1) == 0) {
        guard_armed = 1;
        guard_rc = fn(out);
        ok = guard_rc == 0;
    }
    guard_armed = 0;
    sigaction(SIGBUS, &old_bus, NULL);
    sigaction(SIGSEGV, &old_segv, NULL);
    sigaction(SIGSYS, &old_sys, NULL);
    return ok;
}

static int32_t get_idps(void *out) { return ((int32_t (*)(void *))sceKernelGetIdPs)(out); }
static int32_t get_openpsid(void *out) { return sceKernelGetOpenPsIdForSystem(out); }

static int nonzero(const uint8_t *p, size_t n) { for (size_t i = 0; i < n; ++i) if (p[i]) return 1; return 0; }

/* Results are cached: the IDs cannot change during a session. */
static int cached;
static uint8_t cache_idps[16], cache_psid[16];
static int have_idps, have_psid;

int lce_console_ids_privileged(uint8_t *idps, uint8_t *psid) {
    if (!cached) {
        cached = 1;
        struct jailbreak_backup backup;
        uint8_t buf[64];
        memset(&backup, 0, sizeof backup);
        int version = sdk_cmd(GOLDHEN_SDK_CMD_VERSION, NULL);
        logf3("goldhen sdk version / (negative = -errno)", version, 0);
        /* GoldHEN returns the SDK version through the syscall error channel
           (carry set, value 0x100 for 1.00), so -version arrives here. */
        if (version < 0) version = -version;
        if (version < 0x100 || version > 0xffff) return 0;               /* GoldHEN SDK not available */
        int jb = sdk_cmd(GOLDHEN_SDK_CMD_JAILBREAK, &backup);
        logf3("jailbreak rc / uid before", jb, (long)backup.cr_uid);
        if (jb != 0) return 0;
        memset(buf, 0, sizeof buf);
        int a = guarded(get_idps, buf);
        logf3("idps ok / rc", a, (long)guard_rc); logf3("idps nonzero", nonzero(buf, 16), 0);
        if (a && nonzero(buf, 16)) { memcpy(cache_idps, buf, 16); have_idps = 1; }
        memset(buf, 0, sizeof buf);
        int b = guarded(get_openpsid, buf);
        logf3("openpsid ok / rc", b, (long)guard_rc); logf3("openpsid nonzero", nonzero(buf, 16), 0);
        if (b && nonzero(buf, 16)) { memcpy(cache_psid, buf, 16); have_psid = 1; }
        int un = sdk_cmd(GOLDHEN_SDK_CMD_UNJAILBREAK, &backup);          /* always restore credentials */
        logf3("unjailbreak rc", un, 0);
    }
    if (!have_idps && !have_psid) return 0;
    memcpy(idps, cache_idps, 16);
    memcpy(psid, cache_psid, 16);
    return 1;
}
