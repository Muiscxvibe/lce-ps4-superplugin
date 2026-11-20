/* strings195 - runtime string-id injection into the retail 1.95 Localizer.
 *
 * Image: reconstructed ELF c675e6ce601b6457a1c1466b0da3d9ccd7415f6b0db9f8e9da7b6e511816029f
 * (RX 231a6bfb...c682). All RVAs are cited in STATUS.md.
 *
 * Every consumer of a u32 string id (Enchantment::getFullname 0x24aec0,
 * Potion/MobEffect names, Biome names, UI labels) resolves it with
 * 0xaac9b0 -> Localizer::get 0xd39880, which hashes the id (FNV-1a over its
 * 4 bytes) into the current language's Dinkumware unordered_map<u32, wstring>
 * at [Localizer+0xa0]. strings195 inserts mod entries into that map (and into
 * every loaded language node) with the game's OWN functions:
 *   wstring assign 0x67840, list emplace_front 0xd5a9e0 (node 0x40 bytes,
 *   allocated with operator new 0xe6cfd0), hash _Insert 0x7ed670 (links the
 *   node into its bucket, rehashes through the game's code when the load
 *   factor is exceeded, frees the node itself if the id already exists).
 * The exact sequence the stock loader runs at 0xd38f3f / 0xd38f77.
 * No .text write, no hook. The game owns every node it may later free.
 *
 * The Localizer is deleted and rebuilt by 0xafc510 (startup 0xaff71e and the
 * language-change path 0x9d985f), which drops mod entries: call
 * s195_ensure() from a periodic tick (the SDK title/UI tick); it is
 * idempotent and cheap when nothing changed.
 *
 * Return convention: 1 done and post-verified, 0 refused/not ready before any
 * write, -1 partial/uncertain (keep resident, report, do not retry blindly).
 */
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- pinned RVAs ---------------------------------------------------------- */
#define S195_RVA_LOCALIZER     0x1440530u /* Localizer* global (object 0xd8, ctor 0xd37a40) */
#define S195_RVA_GET           0xaac9b0u  /* const wchar_t* (u32 id): null Localizer -> 0xeeeda2 */
#define S195_RVA_LOOKUP        0xd39880u  /* Localizer::get(this, id) -> wchar_t* or MISSING */
#define S195_RVA_MISSING       0xf0fdc0u  /* returned when the id is absent (or > 0xfffffffd) */
#define S195_RVA_WSTRING       0x67840u   /* wstring::assign(this, const wchar_t*, n) */
#define S195_RVA_LIST_EMPLACE  0xd5a9e0u  /* list<pair<u32,wstring>>::emplace_front(map, pair&&) */
#define S195_RVA_HASH_INSERT   0x7ed670u  /* _Hash::_Insert(ret, map, const u32* key, iterator{q0,q1,node} by value) */
#define S195_RVA_ALLOC         0xe6cfd0u  /* operator new */
#define S195_RVA_FREE          0xe6cfb0u  /* operator delete */
#define S195_RVA_RELOAD        0xafc510u  /* deletes + rebuilds the Localizer (for reference) */

/* Layouts (verified by the Unicorn test running the real code):
 *   Localizer +0x08 outer map<wstring lang, node>, list head ptr at +0x10;
 *             +0xa0 current string map (= lang node + 0x38); +0x98 = node + 0x88
 *   lang node: +0 next, +8 prev, +0x10 wstring name, +0x38 string map (0x50)
 *   string map: +0x08 list head ptr, +0x10 size, +0x20 bucket vector begin
 *               (bucket = 2 iterators x 0x18, node at iterator +0x10),
 *               +0x38 mask, +0x40 maxidx, +0x48 max_load_factor (float)
 *   node 0x40: +0 next, +8 prev, +0x10 u32 key, +0x18 wstring
 *   wstring 0x28: +0 proxy, +8 buf[8 u16] or heap ptr, +0x18 size, +0x20 cap (7 = SSO)
 */

/* Reserved mod string-id range: 0x4C430000 ('LC') .. 0x4C43FFFF. None of the
 * 4888 ids of the stock languages.loc (MediaOrbis.arc c49fc194...) is in it
 * (tests/test_image_pins.py), and it is disjoint from the plugin's own dialog
 * ids 0xfde30000..0xfde30004. */
#define S195_SID_BASE   0x4C430000u
#define S195_SID_COUNT  0x10000u
uint32_t s195_sid(uint32_t index);          /* S195_SID_BASE + index, 0 if index out of range */
int s195_sid_reserved(uint32_t sid);         /* 1 if sid is in the mod range */

#define S195_MAX_TEXT   512u                 /* UTF-16 units per text */
#define S195_MAX_LANGS  32u                  /* language nodes visited per Localizer */

/* Environment. call() runs a native: on the console s195_native_call (direct
 * call through the address, SysV ABI), in host tests a fake, in the Unicorn
 * bridge the emulator. regs = integer register args (<= 6), stack = qwords
 * of by-value MEMORY-class arguments (<= 3). Returns rax. */
typedef uintptr_t (*s195_call_fn)(void *ctx, uintptr_t fn, const uint64_t *regs, unsigned nregs, const uint64_t *stack, unsigned nstack);
typedef struct s195_env {
    uintptr_t base;
    void *ctx;
    int (*read)(void *ctx, uintptr_t addr, void *dst, size_t n);        /* 1 ok */
    int (*write)(void *ctx, uintptr_t addr, const void *src, size_t n); /* 1 ok */
    int (*held)(void *ctx);                                             /* 1 while the caller owns the phase thread */
    s195_call_fn call;
} s195_env;

/* Console implementation of s195_env.call (direct native call). */
uintptr_t s195_native_call(void *ctx, uintptr_t fn, const uint64_t *regs, unsigned nregs, const uint64_t *stack, unsigned nstack);

typedef struct s195_text {
    const char *lang;            /* "en-EN", "de-DE", "en_US", "de" ... ; NULL = any */
    const uint16_t *utf16;       /* not NUL-terminated is fine */
    uint32_t len;                /* 1..S195_MAX_TEXT */
} s195_text;

typedef struct s195_entry {
    uint32_t sid;                /* s195_sid_reserved() must hold */
    const s195_text *texts;      /* texts[0] = fallback for languages without a match */
    uint32_t text_count;
} s195_entry;

typedef struct s195_report {
    uintptr_t localizer;         /* Localizer seen */
    uint32_t languages;          /* language maps visited */
    uint32_t inserted;           /* nodes inserted this call */
    uint32_t present;            /* already present */
    uint32_t failed;             /* uncertain inserts */
} s195_report;

/* Validate entries without touching memory. 1 ok, 0 refused (err filled). */
int s195_check(const s195_entry *e, uint32_t n, char *err, uint32_t cap);

/* Make every entry resolvable in every loaded language map of the current
 * Localizer. 1 all present (inserted or already there) and verified through
 * the current map, 0 no Localizer yet / not held / refused before any write,
 * -1 partial. r may be NULL. */
int s195_ensure(s195_env *e, const s195_entry *entries, uint32_t n, s195_report *r);

/* Convenience: one entry, one text. */
int s195_add(s195_env *e, uint32_t sid, const uint16_t *utf16, uint32_t len);

/* Read-only helpers (exact mirror of 0xd39880's bucket walk). */
uintptr_t s195_find(s195_env *e, uintptr_t map, uint32_t sid);           /* node or 0 */
int s195_read_current(s195_env *e, uint32_t sid, uint16_t *out, uint32_t cap, uint32_t *len); /* 1 found */

/* UTF-8 -> UTF-16 (BMP + surrogate pairs); returns units written, 0 on invalid input. */
uint32_t s195_utf8_to_utf16(const char *s, uint16_t *out, uint32_t cap);

#ifdef __cplusplus
}
#endif
