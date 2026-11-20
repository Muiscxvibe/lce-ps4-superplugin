// Host bridge: exposes strings195 (and, when linked, enchants195) to the
// Unicorn harness (tests/uc195.py). Memory and native calls are callbacks
// into the emulator; base is 0 because the analysis image is mapped at 0.
#include "../strings195.h"
#include <stdint.h>
#include <string.h>

typedef int (*rd_t)(uint64_t, void *, uint64_t);
typedef int (*wr_t)(uint64_t, const void *, uint64_t);
typedef uint64_t (*call_t)(uint64_t, const uint64_t *, unsigned, const uint64_t *, unsigned);
static rd_t g_rd; static wr_t g_wr; static call_t g_call;
static int held_flag = 1;
static int b_read(void *, uintptr_t a, void *d, size_t n) { return g_rd(a, d, n); }
static int b_write(void *, uintptr_t a, const void *s, size_t n) { return g_wr(a, s, n); }
static int b_held(void *) { return held_flag; }
static uintptr_t b_call(void *, uintptr_t fn, const uint64_t *r, unsigned n, const uint64_t *s, unsigned ns) { return g_call(fn, r, n, s, ns); }

extern "C" {
void bridge_set(rd_t r, wr_t w, call_t c) { g_rd = r; g_wr = w; g_call = c; }
void bridge_held(int h) { held_flag = h; }
s195_env *bridge_s195_env(void) { static s195_env e; e = {0, nullptr, b_read, b_write, b_held, b_call}; return &e; }

/* entries: n sids, each with an English text and an optional German text (UTF-8). */
int bridge_s195_ensure(unsigned n, const uint32_t *sids, const char *const *en, const char *const *de, s195_report *rep) {
    static uint16_t buf[64][2][S195_MAX_TEXT];
    static s195_text texts[64][2];
    static s195_entry ents[64];
    if (n > 64) return -9;
    for (unsigned i = 0; i < n; i++) {
        uint32_t a = s195_utf8_to_utf16(en[i], buf[i][0], S195_MAX_TEXT);
        texts[i][0] = {"en-EN", buf[i][0], a};
        unsigned k = 1;
        if (de && de[i]) { uint32_t b = s195_utf8_to_utf16(de[i], buf[i][1], S195_MAX_TEXT); texts[i][1] = {"de_DE", buf[i][1], b}; k = 2; }
        ents[i] = {sids[i], texts[i], k};
    }
    return s195_ensure(bridge_s195_env(), ents, n, rep);
}
uint64_t bridge_s195_find(uint64_t map, uint32_t sid) { return s195_find(bridge_s195_env(), map, sid); }
}
