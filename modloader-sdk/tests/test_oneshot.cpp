// One-shot restore: after a hook fired, the code page must hash exactly as the
// pristine page (that is what client_report.c measures), and foreign patches
// must never be overwritten.
#include <string.h>
#include <sys/mman.h>
#include "../loader/retail195/oneshot195.h"
#include "../loader/sha256.h"
#include "check.h"

static int protect_calls, fail_protect, last_prot;
static int protect(void *, void *p, size_t n, int prot) {
    ++protect_calls; last_prot = prot;
    if (fail_protect) return -1;
    return mprotect(p, n, (prot & 1 ? PROT_READ : 0) | (prot & 2 ? PROT_WRITE : 0)) ? -1 : 0;  // exec bit irrelevant on host
}

int main() {
    // 16 KiB-aligned like the console's code pages
    uint8_t *raw = (uint8_t *)mmap(nullptr, 0x8000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(raw != MAP_FAILED);
    uint8_t *page = (uint8_t *)(((uintptr_t)raw + 0x3fff) & ~(uintptr_t)0x3fff);
    for (int i = 0; i < 0x4000; ++i) page[i] = (uint8_t)(i * 7 + 3);
    uint8_t pristine[32];
    lce_sha256(page, 0x4000, pristine);
    lce195_patch_word w;
    w.word_address = (uintptr_t)page + 0xc8;   // like 0x477cc8: aligned word holding a call
    memcpy(&w.expected_word, page + 0xc8, 8);
    w.replacement_word = w.expected_word ^ UINT64_C(0x00ffffffff000000);  // new rel32
    // simulate mc195_publish_call having installed the hook, then page back to RX (read-only here)
    memcpy(page + 0xc8, &w.replacement_word, 8);
    mprotect(page, 0x4000, PROT_READ);
    uint8_t hooked[32];
    lce_sha256(page, 0x4000, hooked);
    CHECK(memcmp(hooked, pristine, 32) != 0);

    CHECK_EQ(lce195_oneshot_restore(&w, 5, protect, nullptr), LCE195_RESTORE_DONE);
    CHECK_EQ(last_prot, 5);  // protection restored afterwards
    uint8_t after[32];
    lce_sha256(page, 0x4000, after);
    CHECK(memcmp(after, pristine, 32) == 0);  // segment hash identical to pristine
    // idempotent
    protect_calls = 0;
    CHECK_EQ(lce195_oneshot_restore(&w, 5, protect, nullptr), LCE195_RESTORE_ALREADY);
    CHECK_EQ(protect_calls, 0);
    // foreign patch: refuse, do not touch
    mprotect(page, 0x4000, PROT_READ | PROT_WRITE);
    uint64_t foreign = w.expected_word ^ 0x1111;
    memcpy(page + 0xc8, &foreign, 8);
    mprotect(page, 0x4000, PROT_READ);
    CHECK_EQ(lce195_oneshot_restore(&w, 5, protect, nullptr), LCE195_RESTORE_REFUSED);
    uint64_t now; memcpy(&now, page + 0xc8, 8);
    CHECK_EQ(now, foreign);
    // protect failure is reported, word untouched
    mprotect(page, 0x4000, PROT_READ | PROT_WRITE);
    memcpy(page + 0xc8, &w.replacement_word, 8);
    mprotect(page, 0x4000, PROT_READ);
    fail_protect = 1;
    CHECK_EQ(lce195_oneshot_restore(&w, 5, protect, nullptr), LCE195_RESTORE_PROTECT_FAILED);
    memcpy(&now, page + 0xc8, 8);
    CHECK_EQ(now, w.replacement_word);
    fail_protect = 0;
    // misaligned / degenerate plans are refused
    lce195_patch_word bad = w; bad.word_address += 1;
    CHECK_EQ(lce195_oneshot_restore(&bad, 5, protect, nullptr), LCE195_RESTORE_REFUSED);
    bad = w; bad.replacement_word = bad.expected_word;
    CHECK_EQ(lce195_oneshot_restore(&bad, 5, protect, nullptr), LCE195_RESTORE_REFUSED);
    munmap(raw, 0x8000);
    TEST_MAIN_END("test_oneshot");
}
