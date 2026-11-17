#ifndef MC195_CRASH_NATIVE_ABI_H
#define MC195_CRASH_NATIVE_ABI_H
/* PS4 payload SDK's FreeBSD 9.3 amd64 signal ABI, not OpenOrbis' Linux
 * signal.h. Header-derived layout; fatal delivery on this title is unverified.
 * Do not mix these types with a system sigaction declaration. */
#include <stddef.h>
#include <stdint.h>

#define MC195_SIGILL 4
#define MC195_SIGBUS 10
#define MC195_SIGSEGV 11
#define MC195_SA_SIGINFO 0x40

typedef struct { uint32_t bits[4]; } Mc195Sigset;
typedef struct Mc195Siginfo Mc195Siginfo;
typedef void (*Mc195SimpleHandler)(int);
typedef void (*Mc195InfoHandler)(int, Mc195Siginfo *, void *);
typedef struct {
    union { Mc195SimpleHandler simple; Mc195InfoHandler info; } handler;
    int32_t flags;
    Mc195Sigset mask;
} Mc195Sigaction;

struct Mc195Siginfo {
    int32_t signo, error, code, pid;
    uint32_t uid;
    int32_t status;
    void *addr;
    /* Remaining siginfo fields intentionally opaque: never allocate this. */
};
/* Prefix through SS only; pointer is borrowed from the kernel. The true
 * mcontext extends beyond this prefix with 16-byte-aligned FPU state. */
typedef struct {
    uint64_t onstack, rdi, rsi, rdx, rcx, r8, r9, rax;
    uint64_t rbx, rbp, r10, r11, r12, r13, r14, r15;
    uint32_t trapno; uint16_t fs, gs;
    uint64_t addr;
    uint32_t flags; uint16_t es, ds;
    uint64_t err, rip, cs, rflags, rsp, ss;
} Mc195McontextPrefix;
typedef struct {
    Mc195Sigset sigmask;
    Mc195McontextPrefix mcontext;
} Mc195UcontextPrefix;

#if defined(__cplusplus)
#define MC195_ABI_ASSERT(c, m) static_assert(c, m)
#else
#define MC195_ABI_ASSERT(c, m) _Static_assert(c, m)
#endif
MC195_ABI_ASSERT(sizeof(void *) == 8, "requires amd64 ABI");
MC195_ABI_ASSERT(sizeof(Mc195Sigaction) == 32, "sigaction size");
MC195_ABI_ASSERT(offsetof(Mc195Sigaction, flags) == 8, "sigaction flags");
MC195_ABI_ASSERT(offsetof(Mc195Sigaction, mask) == 12, "sigaction mask");
MC195_ABI_ASSERT(offsetof(Mc195Siginfo, addr) == 24, "siginfo address");
MC195_ABI_ASSERT(offsetof(Mc195McontextPrefix, rip) == 0xa0, "mcontext RIP");
MC195_ABI_ASSERT(offsetof(Mc195McontextPrefix, rsp) == 0xb8, "mcontext RSP");
MC195_ABI_ASSERT(offsetof(Mc195UcontextPrefix, mcontext) == 0x10, "ucontext mcontext");
MC195_ABI_ASSERT(offsetof(Mc195UcontextPrefix, mcontext.rip) == 0xb0, "ucontext RIP");
MC195_ABI_ASSERT(offsetof(Mc195UcontextPrefix, mcontext.rbp) == 0x58, "ucontext RBP");
MC195_ABI_ASSERT(offsetof(Mc195UcontextPrefix, mcontext.rsp) == 0xc8, "ucontext RSP");
#undef MC195_ABI_ASSERT
#endif
