// Console operations for fuel195 (OpenOrbis libkernel). Built only with
// -DMC_LEGACY195. DATA-ONLY: alloc returns an RW game-heap page for the private
// vtable (a vtable is read-only DATA, never executed as our code), cas8 swaps a
// furnace object's vptr word (heap data). No .text is ever touched; getBurnDuration
// is never hooked. Mirrors the calls the shipping hooks already import.
#if defined(MC_LEGACY195)
#include "fuel195.h"
#include <orbis/libkernel.h>
#include <string.h>

namespace {
int q_prot(uintptr_t a, size_t n, int *prot) {
    void *lo = nullptr, *hi = nullptr; int32_t p = 0;
    if (!a || a > UINTPTR_MAX - n || sceKernelQueryMemoryProtection((void *)a, &lo, &hi, &p)) return 0;
    if ((uintptr_t)lo > a || (uintptr_t)hi < a + n) return 0;
    if (prot) *prot = p;
    return 1;
}
int rd(void *, uintptr_t a, void *out, size_t n) {
    int p = 0; if (!q_prot(a, n, &p) || !(p & 1)) return 0; memcpy(out, (const void *)a, n); return 1;
}
int wr(void *, uintptr_t a, const void *in, size_t n) {
    int p = 0; if (!q_prot(a, n, &p) || (p & 4)) return 0;
    uintptr_t page = a & ~(uintptr_t)(FUEL195_PAGE - 1);
    size_t span = (a + n) - page;
    bool temp = !(p & 2);
    if (temp && sceKernelMprotect((void *)page, span, p | 2)) return 0;
    memcpy((void *)a, in, n);
    if (temp && sceKernelMprotect((void *)page, span, p)) return -1;
    return 1;
}
int query(void *, uintptr_t a, int *prot) { return q_prot(a, 8, prot); }
// Swap a furnace object's vptr (an 8-byte heap DATA word). The object is on the
// game heap (RW); no RX page is involved.
int cas8(void *, uintptr_t a, uint64_t expected, uint64_t repl) {
    int p = 0; if ((a & 7) || !q_prot(a, 8, &p) || (p & 4)) return 0;
    uintptr_t page = a & ~(uintptr_t)(FUEL195_PAGE - 1);
    bool temp = !(p & 2);
    if (temp && sceKernelMprotect((void *)page, FUEL195_PAGE, p | 2)) return 0;
    uint64_t e = expected;
    bool ok = __atomic_compare_exchange_n((uint64_t *)a, &e, repl, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    if (temp && sceKernelMprotect((void *)page, FUEL195_PAGE, p)) return -1;  /* protection not restored */
    return ok ? 1 : 0;
}
// RW private page in the game heap for the vtable copy (data, not code).
uintptr_t alloc(void *, size_t n) {
    void *p = nullptr;
    if (sceKernelMmap(nullptr, n, 3, 0x1002, -1, 0, &p) || !p) return 0;      /* private anon RW */
    return (uintptr_t)p;
}
const fuel195_ops kOps = {nullptr, rd, wr, cas8, query, alloc};
}  // namespace

extern "C" const fuel195_ops *fuel195_ps4_ops(void) { return &kOps; }
#endif
