// Host test (ASan/UBSan) for strings195 on a fake image. The fake natives
// model the Dinkumware map the way the real 0xd5a9e0/0x7ed670 do (list with
// contiguous buckets, lo/hi iterators of 0x18, node 0x40) without rehash.
// Real-code behaviour (including rehash) is in tests/test_unicorn_retail.py.
#include "../strings195.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

static int g_checks, g_fail;
#define CHECK(c, m) do { g_checks++; if (!(c)) { g_fail++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, m); } } while (0)

namespace {
const uintptr_t BASE = 0x20000000, SIZE = 0x2000000;
std::vector<uint8_t> mem(SIZE);
uintptr_t brk_ = BASE + 0x1600000;
int fail_read_at = 0, reads = 0;    // fail the N-th read (0 = never)
int fail_alloc = 0, held_v = 1, fake_dup = 0, corrupt_node = 0;
unsigned allocs = 0, frees = 0;

bool in(uintptr_t a, size_t n) { return a >= BASE && a + n <= BASE + SIZE; }
int rd(void *, uintptr_t a, void *d, size_t n) { reads++; if (fail_read_at && reads == fail_read_at) return 0; if (!in(a, n)) return 0; memcpy(d, &mem[a - BASE], n); return 1; }
int wr(void *, uintptr_t a, const void *s, size_t n) { if (!in(a, n)) return 0; memcpy(&mem[a - BASE], s, n); return 1; }
int held(void *) { return held_v; }
uint64_t Q(uintptr_t a) { uint64_t v; memcpy(&v, &mem[a - BASE], 8); return v; }
void W(uintptr_t a, uint64_t v) { memcpy(&mem[a - BASE], &v, 8); }
uint32_t D(uintptr_t a) { uint32_t v; memcpy(&v, &mem[a - BASE], 4); return v; }
uintptr_t alloc(size_t n) { uintptr_t p = brk_; brk_ += (n + 31) & ~15ull; memset(&mem[p - BASE], 0, n); allocs++; return p; }

uint64_t hsh(uint32_t k) { uint64_t h = 0xcbf29ce484222325ull; for (int i = 0; i < 4; i++) { h ^= (k >> (8 * i)) & 0xff; h *= 0x100000001b3ull; } return h ^ (h >> 32); }
uintptr_t new_map() {
    uintptr_t map = alloc(0x50), head = alloc(0x40), vec = alloc(16 * 0x18);
    W(head, head); W(head + 8, head);
    W(map + 8, head); W(map + 0x10, 0); W(map + 0x20, vec); W(map + 0x38, 7); W(map + 0x40, 8);
    for (int i = 0; i < 16; i++) W(vec + i * 0x18 + 0x10, head);
    return map;
}
void wstr_set(uintptr_t w, const uint16_t *s, uint64_t n) {   // w = wstring (+0 proxy, +8 buf/ptr, +0x18 size, +0x20 cap)
    uintptr_t dst = w + 8;
    if (n >= 8) { dst = alloc((n + 1) * 2); W(w + 8, dst); W(w + 0x20, n); } else W(w + 0x20, 7);
    memcpy(&mem[dst - BASE], s, n * 2); uint16_t z = 0; memcpy(&mem[dst - BASE + n * 2], &z, 2); W(w + 0x18, n);
}
void unlink(uintptr_t n) { uintptr_t nx = Q(n), pv = Q(n + 8); W(pv, nx); W(nx + 8, pv); }
void link_before(uintptr_t n, uintptr_t where) { uintptr_t pv = Q(where + 8); W(n, where); W(n + 8, pv); W(pv, n); W(where + 8, n); }

uintptr_t fake_call(void *, uintptr_t fn, const uint64_t *r, unsigned n, const uint64_t *s, unsigned ns) {
    uint32_t rva = (uint32_t)(fn - BASE);
    if (rva == S195_RVA_ALLOC) { if (fail_alloc && --fail_alloc == 0) return 0; return alloc(r[0]); }
    if (rva == S195_RVA_FREE) { frees++; return 0; }
    if (rva == S195_RVA_WSTRING && n == 3) {
        std::vector<uint16_t> t(r[2] + 1);
        memcpy(t.data(), &mem[r[1] - BASE], r[2] * 2);
        wstr_set(r[0], t.data(), r[2]); return r[0];
    }
    if (rva == S195_RVA_LIST_EMPLACE && n == 2) {         // node at the list front, wstring moved
        uintptr_t map = r[0], pair = r[1], head = Q(map + 8), node = alloc(0x40);
        memcpy(&mem[node + 0x10 - BASE], &mem[pair - BASE], 4);
        memcpy(&mem[node + 0x18 - BASE], &mem[pair + 8 - BASE], 0x28);
        W(pair + 0x20, 0); W(pair + 0x28, 7);
        link_before(node, Q(head));
        W(map + 0x10, Q(map + 0x10) + 1);
        if (corrupt_node) W(node + 0x10, 0);
        return node;
    }
    if (rva == S195_RVA_HASH_INSERT && n == 3 && ns == 3) {
        uintptr_t ret = r[0], map = r[1], node = s[2], head = Q(map + 8), vec = Q(map + 0x20);
        uint32_t key = D(r[2]);
        uint64_t idx = hsh(key) & 7;
        uintptr_t lo = vec + idx * 0x30 + 0x10, hi = vec + (2 * idx + 1) * 0x18 + 0x10;
        bool dup = fake_dup;
        for (uintptr_t p = Q(lo); !dup && Q(lo) != head; p = Q(p)) { if (D(p + 0x10) == key) dup = true; if (p == Q(hi)) break; }
        if (dup) { unlink(node); W(map + 0x10, Q(map + 0x10) - 1); W(ret + 0x18, 0); W(ret + 0x10, 0); return ret; }
        if (Q(lo) == head) { W(lo, node); W(hi, node); }
        else { unlink(node); link_before(node, Q(lo)); W(lo, node); }
        W(ret + 0x10, node); mem[ret + 0x18 - BASE] = 1;
        return ret;
    }
    return 0;
}

s195_env env() { return s195_env{BASE, nullptr, rd, wr, held, fake_call}; }
uintptr_t make_localizer(const char *const *langs, int n, uintptr_t *maps) {
    uintptr_t L = alloc(0xd8), head = alloc(0x40);
    W(head, head); W(head + 8, head); W(L + 0x10, head);
    for (int i = 0; i < n; i++) {
        uintptr_t node = alloc(0xf8);
        uint16_t nm[16]; size_t k = strlen(langs[i]);
        for (size_t j = 0; j < k; j++) nm[j] = (uint8_t)langs[i][j];
        wstr_set(node + 0x10, nm, k);
        uintptr_t m = new_map();
        memcpy(&mem[node + 0x38 - BASE], &mem[m - BASE], 0x50);
        maps[i] = node + 0x38;
        link_before(node, head);
    }
    W(L + 0xa0, maps[0]);
    return L;
}
uint32_t u16(const char *s, uint16_t *o) { return s195_utf8_to_utf16(s, o, S195_MAX_TEXT); }
bool text_is(uintptr_t map, uint32_t sid, const char *want) {
    s195_env e = env(); uintptr_t node = s195_find(&e, map, sid);
    if (!node) return false;
    uint16_t w[S195_MAX_TEXT]; uint32_t n = u16(want, w);
    uint64_t size = Q(node + 0x30), cap = Q(node + 0x38);
    uintptr_t p = cap >= 8 ? Q(node + 0x20) : node + 0x20;
    return size == n && !memcmp(&mem[p - BASE], w, n * 2);
}
}  // namespace

int main() {
    // pure helpers
    CHECK(s195_sid(0) == 0x4C430000u && s195_sid(0xffff) == 0x4C43FFFFu && s195_sid(0x10000) == 0, "sid range");
    CHECK(s195_sid_reserved(0x4C430005) && !s195_sid_reserved(0x4C440000) && !s195_sid_reserved(0xd8a6b805), "reserved test");
    uint16_t b[8];
    CHECK(s195_utf8_to_utf16("A\xc3\xa9\xe2\x82\xac", b, 8) == 3 && b[1] == 0xe9 && b[2] == 0x20ac, "utf8 2/3-byte");
    CHECK(s195_utf8_to_utf16("\xf0\x9d\x84\x9e", b, 8) == 2 && b[0] == 0xd834 && b[1] == 0xdd1e, "utf8 surrogate pair");
    CHECK(s195_utf8_to_utf16("\xc0\x80", b, 8) == 0 && s195_utf8_to_utf16("\xed\xa0\x80", b, 8) == 0 && s195_utf8_to_utf16("abc", b, 2) == 0, "utf8 overlong/surrogate/overflow refused");

    uint16_t t1[S195_MAX_TEXT], t2[S195_MAX_TEXT], t3[S195_MAX_TEXT];
    uint32_t n1 = u16("Gem Sharpness", t1), n2 = u16("Edelstein", t2), n3 = u16("Hi", t3);
    s195_text a[2] = {{"en_US", t1, n1}, {"de_DE", t2, n2}}, c[1] = {{nullptr, t3, n3}};
    s195_entry ents[2] = {{0x4C430000, a, 2}, {0x4C430001, c, 1}};
    char err[160];
    CHECK(s195_check(ents, 2, err, sizeof err) == 1, "check ok");
    s195_entry bad = {0x1234, c, 1};
    CHECK(s195_check(&bad, 1, err, sizeof err) == 0 && strstr(err, "range"), "check: out of range");
    s195_entry dup[2] = {{0x4C430000, c, 1}, {0x4C430000, c, 1}};
    CHECK(s195_check(dup, 2, err, sizeof err) == 0 && strstr(err, "duplicate"), "check: duplicate");
    s195_text empty = {nullptr, t1, 0};
    s195_entry e0 = {0x4C430000, &empty, 1};
    CHECK(s195_check(&e0, 1, err, sizeof err) == 0, "check: empty text");

    s195_env e = env();
    s195_report r;
    CHECK(s195_ensure(&e, ents, 2, &r) == 0 && r.inserted == 0, "no Localizer -> 0");
    const char *langs[2] = {"en-EN", "de-DE"};
    uintptr_t maps[2];
    uintptr_t L = make_localizer(langs, 2, maps);
    W(BASE + S195_RVA_LOCALIZER, L);
    held_v = 0;
    CHECK(s195_ensure(&e, ents, 2, &r) == 0, "not held -> 0");
    held_v = 1;
    CHECK(s195_ensure(&e, ents, 2, &r) == 1, "ensure inserts");
    CHECK(r.localizer == L && r.languages == 2 && r.inserted == 4 && r.failed == 0, "report counts");
    CHECK(text_is(maps[0], 0x4C430000, "Gem Sharpness") && text_is(maps[1], 0x4C430000, "Edelstein"), "per-language text");
    CHECK(text_is(maps[0], 0x4C430001, "Hi") && text_is(maps[1], 0x4C430001, "Hi"), "fallback text");
    uint16_t out[64]; uint32_t ol = 0;
    CHECK(s195_read_current(&e, 0x4C430000, out, 64, &ol) == 1 && ol == n1 && !memcmp(out, t1, n1 * 2), "read_current");
    unsigned a0 = allocs;
    CHECK(s195_ensure(&e, ents, 2, &r) == 1 && r.inserted == 0 && r.present == 2 && allocs == a0, "fast path: no allocation");
    CHECK(frees >= 12, "temporaries freed (3 per insert)");

    // a third language appears in a rebuilt Localizer
    const char *l3[1] = {"fr-FR"};
    uintptr_t m3[1];
    uintptr_t L3 = make_localizer(l3, 1, m3);
    W(BASE + S195_RVA_LOCALIZER, L3);
    CHECK(s195_ensure(&e, ents, 2, &r) == 1 && r.inserted == 2 && text_is(m3[0], 0x4C430000, "Gem Sharpness"), "rebuilt Localizer re-inserted, fr falls back to texts[0]");

    // native reports a duplicate (id existed in a map we did not see as present): counted present, rc 1
    uintptr_t m4[1];
    uintptr_t L4 = make_localizer(l3, 1, m4);
    W(BASE + S195_RVA_LOCALIZER, L4);
    fake_dup = 1;
    CHECK(s195_ensure(&e, ents, 1, &r) == -1, "dup reported but id not findable -> -1 (uncertain)");
    fake_dup = 0;

    // allocation failure before any map change -> 0
    uintptr_t m5[1];
    uintptr_t L5 = make_localizer(l3, 1, m5);
    W(BASE + S195_RVA_LOCALIZER, L5);
    fail_alloc = 1;
    CHECK(s195_ensure(&e, ents, 1, &r) == 0 && r.failed == 1 && r.inserted == 0, "first alloc fails -> 0, nothing inserted");
    fail_alloc = 0;
    CHECK(s195_ensure(&e, ents, 1, &r) == 1 && r.inserted == 1, "retry after the transient failure succeeds");

    // corrupted node after emplace -> -1
    uintptr_t m6[1];
    uintptr_t L6 = make_localizer(l3, 1, m6);
    W(BASE + S195_RVA_LOCALIZER, L6);
    corrupt_node = 1;
    CHECK(s195_ensure(&e, ents, 1, &r) == -1, "node key mismatch after emplace -> -1");
    corrupt_node = 0;

    // read failures anywhere during a fresh ensure never crash and never report success
    for (int k = 1; k < 400; k++) {
        uintptr_t mk[2];
        uintptr_t Lk = make_localizer(langs, 2, mk);
        W(BASE + S195_RVA_LOCALIZER, Lk);
        reads = 0; fail_read_at = k;
        int rc = s195_ensure(&e, ents, 2, &r);
        fail_read_at = 0;
        bool all = text_is(mk[0], 0x4C430000, "Gem Sharpness") && text_is(mk[0], 0x4C430001, "Hi");
        if (rc == 1 && !all) { CHECK(false, "rc 1 without all entries present"); break; }
    }
    CHECK(true, "read-failure sweep done");

    // many entries in one map (fake has no rehash: 8 buckets, long chains)
    uintptr_t m7[1];
    uintptr_t L7 = make_localizer(l3, 1, m7);
    W(BASE + S195_RVA_LOCALIZER, L7);
    static s195_entry many[200];
    for (int i = 0; i < 200; i++) many[i] = {s195_sid(100 + i), c, 1};
    CHECK(s195_ensure(&e, many, 200, &r) == 1 && r.inserted == 200, "200 entries");
    int found = 0;
    for (int i = 0; i < 200; i++) found += s195_find(&e, m7[0], s195_sid(100 + i)) != 0;
    CHECK(found == 200 && Q(m7[0] + 0x10) == 200, "all 200 findable, size 200");
    CHECK(s195_find(&e, m7[0], s195_sid(99)) == 0, "absent id not found");

    printf("test_strings195: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
