// strings195: insert mod (id -> UTF-16) entries into the live 1.95 Localizer
// with the game's own wstring/list/hash functions. See strings195.h.
#include "strings195.h"
#include <stdio.h>
#include <string.h>

namespace {

void set_err(char *err, uint32_t cap, const char *m) { if (err && cap) snprintf(err, cap, "%s", m); }
bool held(s195_env *e) { return e->held && e->held(e->ctx) == 1; }
bool rd(s195_env *e, uintptr_t a, void *v, size_t n) { return a && a <= UINTPTR_MAX - n && held(e) && e->read(e->ctx, a, v, n) == 1; }
bool wr(s195_env *e, uintptr_t a, const void *v, size_t n) { return a && a <= UINTPTR_MAX - n && held(e) && e->write(e->ctx, a, v, n) == 1; }
bool rq(s195_env *e, uintptr_t a, uintptr_t *v) { uint64_t x = 0; if (!rd(e, a, &x, 8)) return false; *v = (uintptr_t)x; return true; }

uintptr_t callv(s195_env *e, uint32_t rva, unsigned n, uint64_t a, uint64_t b = 0, uint64_t c = 0, const uint64_t *stack = nullptr, unsigned ns = 0) {
    uint64_t r[3] = {a, b, c};
    return e->call(e->ctx, e->base + rva, r, n, stack, ns);
}

// FNV-1a 64 over the 4 little-endian key bytes, then the Dinkumware
// "h ^ (h >> 32)" fold, exactly as 0xd39893..0xd398e6 / 0x7ed691..0x7ed6e6.
uint64_t key_hash(uint32_t k) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (int i = 0; i < 4; i++) { h ^= (k >> (8 * i)) & 0xff; h *= 0x100000001b3ull; }
    return h ^ (h >> 32);
}

struct Map { uintptr_t head, vec, mask, maxidx; };
bool map_read(s195_env *e, uintptr_t map, Map *m) {
    return rq(e, map + 0x08, &m->head) && m->head && rq(e, map + 0x20, &m->vec) && m->vec &&
           rq(e, map + 0x38, &m->mask) && rq(e, map + 0x40, &m->maxidx) && m->maxidx && m->mask && m->mask < (1u << 24);
}

bool same_lang(const char *a, const uint16_t *b, uint32_t blen, bool prefix_only) {
    // compare with '-' == '_' and ASCII case folding; prefix_only: first 2 letters
    uint32_t i = 0;
    for (; a[i] && i < blen; i++) {
        uint16_t ca = (uint8_t)a[i], cb = b[i];
        if (ca == '_') ca = '-';
        if (cb == '_') cb = '-';
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return prefix_only && i >= 2;
        if (prefix_only && i == 1) return true;
    }
    return !a[i] && i == blen;
}

const s195_text *pick_text(const s195_entry *en, const uint16_t *lang, uint32_t llen) {
    for (uint32_t i = 0; i < en->text_count; i++)
        if (en->texts[i].lang && same_lang(en->texts[i].lang, lang, llen, false)) return &en->texts[i];
    for (uint32_t i = 0; i < en->text_count; i++)
        if (en->texts[i].lang && same_lang(en->texts[i].lang, lang, llen, true)) return &en->texts[i];
    return &en->texts[0];
}

// node text equality (node +0x18 wstring)
int node_text(s195_env *e, uintptr_t node, uint16_t *out, uint32_t cap, uint32_t *len) {
    uint64_t size = 0, capw = 0; uintptr_t p = node + 0x20;
    if (!rd(e, node + 0x30, &size, 8) || !rd(e, node + 0x38, &capw, 8) || size > cap || size > 0x100000) return 0;
    if (capw >= 8 && !rq(e, node + 0x20, &p)) return 0;
    if (size && !rd(e, p, out, size * 2)) return 0;
    *len = (uint32_t)size;
    return 1;
}

// insert one (sid, text) into one string map with the game's functions
// (0xd38e1e..0xd38f77 loop body). 1 inserted, 2 already present, 0 refused before writes, -1 uncertain.
int insert_one(s195_env *e, uintptr_t map, uint32_t sid, const s195_text *t) {
    if (s195_find(e, map, sid)) return 2;
    uintptr_t text = callv(e, S195_RVA_ALLOC, 1, (uint64_t)(t->len + 1) * 2);
    if (!text) return 0;
    uintptr_t pair = 0, ret = 0;
    int rc = 0;                                // nothing linked into the map yet
    uint16_t z = 0;
    if (!wr(e, text, t->utf16, (size_t)t->len * 2) || !wr(e, text + (uintptr_t)t->len * 2, &z, 2)) goto out;
    pair = callv(e, S195_RVA_ALLOC, 1, 0x30);
    ret = callv(e, S195_RVA_ALLOC, 1, 0x20);
    if (!pair || !ret) goto out;
    {
        // pair<const u32, wstring>: key +0, wstring +8 (proxy 0, empty SSO buffer, size 0, cap 7)
        uint8_t init[0x30];
        memset(init, 0, sizeof init);
        memcpy(init, &sid, 4);
        uint64_t seven = 7;
        memcpy(init + 0x28, &seven, 8);
        uint8_t r0[0x20];
        memset(r0, 0, sizeof r0);
        if (!wr(e, pair, init, sizeof init) || !wr(e, ret, r0, sizeof r0)) goto out;
    }
    callv(e, S195_RVA_WSTRING, 3, pair + 8, text, t->len);
    {
        uint64_t size = 0;
        if (!rd(e, pair + 0x20, &size, 8) || size != t->len) goto out;   // assign failed: nothing linked yet
    }
    rc = -1;                                   // from here the map changes
    callv(e, S195_RVA_LIST_EMPLACE, 2, map, pair);
    {
        uintptr_t head = 0, node = 0; uint32_t key = 0;
        if (!rq(e, map + 0x08, &head) || !rq(e, head, &node) || !node || node == head || !rd(e, node + 0x10, &key, 4) || key != sid) goto out;
        uint64_t it[3] = {0, 0, node};
        callv(e, S195_RVA_HASH_INSERT, 3, ret, map, node + 0x10, it, 3);
        uint8_t ok = 0; uintptr_t where = 0;
        if (!rd(e, ret + 0x18, &ok, 1) || !rq(e, ret + 0x10, &where)) goto out;
        if (!ok) { rc = s195_find(e, map, sid) ? 2 : -1; goto out; }   // native freed our node: id existed
        if (where != node || s195_find(e, map, sid) != node) goto out;
        uint16_t buf[S195_MAX_TEXT]; uint32_t len = 0;
        if (!node_text(e, node, buf, S195_MAX_TEXT, &len) || len != t->len || memcmp(buf, t->utf16, (size_t)len * 2)) goto out;
        rc = 1;
    }
out:
    if (ret) callv(e, S195_RVA_FREE, 1, ret);
    if (pair) callv(e, S195_RVA_FREE, 1, pair);     // moved-from: SSO, owns no heap
    callv(e, S195_RVA_FREE, 1, text);
    return rc;
}

}  // namespace

extern "C" {

uint32_t s195_sid(uint32_t index) { return index < S195_SID_COUNT ? S195_SID_BASE + index : 0; }
int s195_sid_reserved(uint32_t sid) { return sid >= S195_SID_BASE && sid - S195_SID_BASE < S195_SID_COUNT; }

uintptr_t s195_native_call(void *, uintptr_t fn, const uint64_t *r, unsigned n, const uint64_t *s, unsigned ns) {
    typedef uint64_t (*f6)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
    struct q3 { uint64_t a, b, c; };
    typedef uint64_t (*f3s)(uint64_t, uint64_t, uint64_t, q3);
    if (!fn || n > 6) return 0;
    if (ns == 0) return (uintptr_t)((f6)fn)(n > 0 ? r[0] : 0, n > 1 ? r[1] : 0, n > 2 ? r[2] : 0, n > 3 ? r[3] : 0, n > 4 ? r[4] : 0, n > 5 ? r[5] : 0);
    if (ns == 3 && n <= 3) { q3 v = {s[0], s[1], s[2]}; return (uintptr_t)((f3s)fn)(n > 0 ? r[0] : 0, n > 1 ? r[1] : 0, n > 2 ? r[2] : 0, v); }
    return 0;
}

int s195_check(const s195_entry *en, uint32_t n, char *err, uint32_t cap) {
    if (!en || !n) { set_err(err, cap, "strings195: no entries"); return 0; }
    for (uint32_t i = 0; i < n; i++) {
        if (!s195_sid_reserved(en[i].sid)) { set_err(err, cap, "strings195: string id outside the mod range 0x4C430000..0x4C43FFFF"); return 0; }
        if (!en[i].texts || !en[i].text_count) { set_err(err, cap, "strings195: entry without text"); return 0; }
        for (uint32_t k = 0; k < en[i].text_count; k++)
            if (!en[i].texts[k].utf16 || !en[i].texts[k].len || en[i].texts[k].len > S195_MAX_TEXT) { set_err(err, cap, "strings195: text empty or longer than 512 UTF-16 units"); return 0; }
        for (uint32_t j = 0; j < i; j++)
            if (en[j].sid == en[i].sid) { set_err(err, cap, "strings195: duplicate string id"); return 0; }
    }
    return 1;
}

uintptr_t s195_find(s195_env *e, uintptr_t map, uint32_t sid) {
    Map m;
    if (!e || !map || !map_read(e, map, &m)) return 0;
    uint64_t idx = key_hash(sid) & m.mask;
    if (idx >= m.maxidx) idx += ~(m.mask >> 1);
    uintptr_t lo = 0, hi = 0, stop = 0;
    if (!rq(e, m.vec + idx * 0x30 + 0x10, &lo) || lo == m.head) return 0;
    if (!rq(e, m.vec + (2 * idx + 1) * 0x18 + 0x10, &hi) || !rq(e, hi, &stop)) return 0;
    for (uintptr_t p = lo, guard = 0; p && p != stop && guard < 100000; guard++) {
        uint32_t k = 0;
        if (!rd(e, p + 0x10, &k, 4)) return 0;
        if (k == sid) return p == m.head ? 0 : p;
        if (!rq(e, p, &p)) return 0;
    }
    return 0;
}

int s195_read_current(s195_env *e, uint32_t sid, uint16_t *out, uint32_t cap, uint32_t *len) {
    uintptr_t loc = 0, map = 0;
    if (!e || !rq(e, e->base + S195_RVA_LOCALIZER, &loc) || !loc || !rq(e, loc + 0xa0, &map) || !map) return 0;
    uintptr_t node = s195_find(e, map, sid);
    return node ? node_text(e, node, out, cap, len) : 0;
}

int s195_ensure(s195_env *e, const s195_entry *en, uint32_t n, s195_report *r) {
    s195_report tmp;
    if (!r) r = &tmp;
    memset(r, 0, sizeof *r);
    if (!e || !e->read || !e->write || !e->call || !held(e) || !s195_check(en, n, nullptr, 0)) return 0;
    uintptr_t loc = 0, cur = 0, ohead = 0;
    if (!rq(e, e->base + S195_RVA_LOCALIZER, &loc) || !loc) return 0;            // not created yet
    r->localizer = loc;
    if (!rq(e, loc + 0xa0, &cur) || !cur || !rq(e, loc + 0x10, &ohead) || !ohead) return 0;
    Map cm;
    if (!map_read(e, cur, &cm)) return 0;
    // fast path: everything already resolves through the current map
    bool all = true;
    for (uint32_t i = 0; i < n && all; i++) all = s195_find(e, cur, en[i].sid) != 0;
    if (all) { r->present = n; r->languages = 1; return 1; }

    // language maps: every node of the outer list, plus the current map if not among them
    uintptr_t maps[S195_MAX_LANGS + 1]; uint16_t names[S195_MAX_LANGS + 1][16]; uint32_t nlen[S195_MAX_LANGS + 1];
    uint32_t nm = 0; bool cur_seen = false;
    uintptr_t p = 0;
    if (!rq(e, ohead, &p)) return 0;
    for (uint32_t guard = 0; p && p != ohead && guard < 64; guard++) {
        if (nm < S195_MAX_LANGS) {
            uint64_t size = 0, capw = 0; uintptr_t sp = p + 0x18;
            nlen[nm] = 0;
            if (rd(e, p + 0x28, &size, 8) && rd(e, p + 0x30, &capw, 8) && size < 16 && (capw < 8 || rq(e, p + 0x18, &sp)) && rd(e, sp, names[nm], size * 2)) nlen[nm] = (uint32_t)size;
            Map t;
            if (map_read(e, p + 0x38, &t)) { maps[nm] = p + 0x38; if (maps[nm] == cur) cur_seen = true; nm++; }
        }
        if (!rq(e, p, &p)) return 0;
    }
    if (!cur_seen) { maps[nm] = cur; nlen[nm] = 0; nm++; }
    r->languages = nm;
    int rc = 1;
    for (uint32_t m = 0; m < nm; m++)
        for (uint32_t i = 0; i < n; i++) {
            const s195_text *t = nlen[m] ? pick_text(&en[i], names[m], nlen[m]) : &en[i].texts[0];
            int one = insert_one(e, maps[m], en[i].sid, t);
            if (one == 1) r->inserted++;
            else if (one == 2) r->present++;
            else { r->failed++; if (one < 0 || r->inserted) rc = -1; else if (rc == 1) rc = 0; }
        }
    if (rc == 0 && r->inserted) rc = -1;
    if (rc == 1)
        for (uint32_t i = 0; i < n; i++)
            if (!s195_find(e, cur, en[i].sid)) return -1;
    return rc;
}

int s195_add(s195_env *e, uint32_t sid, const uint16_t *utf16, uint32_t len) {
    s195_text t = {nullptr, utf16, len};
    s195_entry en = {sid, &t, 1};
    return s195_ensure(e, &en, 1, nullptr);
}

uint32_t s195_utf8_to_utf16(const char *s, uint16_t *out, uint32_t cap) {
    uint32_t n = 0;
    const unsigned char *u = (const unsigned char *)s;
    if (!s || !out) return 0;
    while (*u) {
        uint32_t c, extra;
        if (*u < 0x80) { c = *u; extra = 0; }
        else if ((*u & 0xe0) == 0xc0) { c = *u & 0x1f; extra = 1; }
        else if ((*u & 0xf0) == 0xe0) { c = *u & 0x0f; extra = 2; }
        else if ((*u & 0xf8) == 0xf0) { c = *u & 0x07; extra = 3; }
        else return 0;
        u++;
        for (uint32_t k = 0; k < extra; k++, u++) { if ((*u & 0xc0) != 0x80) return 0; c = (c << 6) | (*u & 0x3f); }
        if ((extra == 1 && c < 0x80) || (extra == 2 && c < 0x800) || (extra == 3 && c < 0x10000) || c > 0x10ffff || (c >= 0xd800 && c < 0xe000)) return 0;
        if (c >= 0x10000) {
            if (n + 2 > cap) return 0;
            c -= 0x10000; out[n++] = (uint16_t)(0xd800 | (c >> 10)); out[n++] = (uint16_t)(0xdc00 | (c & 0x3ff));
        } else {
            if (n + 1 > cap) return 0;
            out[n++] = (uint16_t)c;
        }
    }
    return n;
}

}  // extern "C"
