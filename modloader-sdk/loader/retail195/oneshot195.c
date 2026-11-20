#include "oneshot195.h"

int lce195_oneshot_restore(const lce195_patch_word *w, int code_protection, lce195_protect_fn protect, void *ctx) {
    const uintptr_t page_size = 0x4000;
    if (!w || !protect || !w->word_address || (w->word_address & 7) || w->expected_word == w->replacement_word) return LCE195_RESTORE_REFUSED;
    uint64_t *word = (uint64_t *)w->word_address;
    uint64_t now = __atomic_load_n(word, __ATOMIC_ACQUIRE);
    if (now == w->expected_word) return LCE195_RESTORE_ALREADY;
    if (now != w->replacement_word) return LCE195_RESTORE_REFUSED;  /* foreign bytes: never overwrite */
    void *page = (void *)(w->word_address & ~(page_size - 1));
    if (protect(ctx, page, page_size, code_protection | 2)) return LCE195_RESTORE_PROTECT_FAILED;
    uint64_t expected = w->replacement_word;
    int swapped = __atomic_compare_exchange_n(word, &expected, w->expected_word, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    if (protect(ctx, page, page_size, code_protection)) return LCE195_RESTORE_PROTECT_FAILED;
    if (swapped) return LCE195_RESTORE_DONE;
    return expected == w->expected_word ? LCE195_RESTORE_ALREADY : LCE195_RESTORE_REFUSED;
}
