/* One-shot startup call-site hooks that leave .text pristine afterwards.
 *
 * The shipping plugin installs startup-phase hooks with mc195_publish_call()
 * (r72 native_threeway_repeat_candidate/source/legacy195/call_publish.c): an
 * 8-byte aligned CAS of the call word (expected_word -> replacement_word,
 * whose rel32 points at a relay in a private data mapping), wrapped in
 * sceKernelMprotect RW/restore. The relay page is NOT part of eboot.bin
 * segment 0, so once the call word is restored the segment hashes exactly as
 * before the hook (client_report.c hashes segment 0 at report time).
 *
 * lce195_oneshot_restore() performs the inverse CAS (replacement -> expected).
 * Call it from the hook observer AFTER the original target returned and the
 * phase work finished, on the same thread. It refuses if the word holds
 * anything other than our replacement (someone else patched it) and is
 * idempotent. Instruction-stream visibility: other cores may still execute
 * the old (patched) bytes briefly; that is harmless because the relay stays
 * mapped and forwards to the original target forever (relay pages are never
 * unmapped), and every observer is itself one-shot (guarded by a state CAS).
 *
 * Sites the SDK owner uses (1.95 RVAs, all single-execution at startup):
 *   items init call word   0x477cc8 (call -> 0x386070)   one-shot: restore
 *   recipes wrapper word   0x477ce8 (call -> 0x5a0710)   one-shot: restore
 *   crafting pre-cache     0x5a9ef0 (call -> 0x5aa200)   one-shot: restore
 *   block init window      0xb74d8  (post-stock blocks)  one-shot: restore
 *   item atlas seed        0xd3d6d8 (call -> 0xd3e070)   RE-FIRES on texture reloads: cannot be restored;
 *                                                        see INTEGRATION.md section 5 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uintptr_t word_address;          /* 8-byte aligned */
    uint64_t expected_word;          /* pristine bytes */
    uint64_t replacement_word;       /* bytes while hooked */
} lce195_patch_word;

typedef int (*lce195_protect_fn)(void *ctx, void *address, size_t length, int protection);

enum { LCE195_RESTORE_DONE = 1, LCE195_RESTORE_ALREADY = 2, LCE195_RESTORE_REFUSED = 0, LCE195_RESTORE_PROTECT_FAILED = -1 };

/* code_protection: the page's normal protection (5 = RX on the console). */
int lce195_oneshot_restore(const lce195_patch_word *w, int code_protection, lce195_protect_fn protect, void *ctx);

#ifdef __cplusplus
}
#endif
