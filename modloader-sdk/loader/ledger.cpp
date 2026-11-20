#include "ledger.h"
#include <stdio.h>
#include <string.h>
#include "package.h"
#include "sha256.h"

extern "C" uint32_t lce_block_item_id(uint32_t block_id) { return block_id < 256 ? block_id : block_id + 768; }

extern "C" void lce_ledger_init(lce_ledger *l) { memset(l, 0, sizeof *l); }

namespace {
const char kHeader[] = "LCEIDS 1\n";

int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

bool parse_u32(const char *s, size_t n, uint32_t *out) {
    if (!n || n > 7) return false;
    uint32_t v = 0;
    for (size_t i = 0; i < n; ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
        v = v * 10 + (uint32_t)(s[i] - '0');
    }
    if (n > 1 && s[0] == '0') return false;  // canonical form only
    *out = v;
    return true;
}

// Does any ledger entry already own numeric item-space id `item_id`?
bool item_space_taken(const lce_ledger *l, uint32_t item_id) {
    for (uint32_t i = 0; i < l->count; ++i) {
        const lce_ledger_entry &e = l->entries[i];
        if (e.kind == LCE_KIND_ITEM && e.id == item_id) return true;
        if (e.kind == LCE_KIND_BLOCK && lce_block_item_id(e.id) == item_id) return true;
    }
    return false;
}
bool block_taken(const lce_ledger *l, uint32_t block_id) {
    for (uint32_t i = 0; i < l->count; ++i)
        if (l->entries[i].kind == LCE_KIND_BLOCK && l->entries[i].id == block_id) return true;
    return false;
}
const char *const kKindName[LCE_KIND_COUNT] = {"item", "block", "effect", "enchant", "biome", "potion"};
bool is_ext(uint32_t kind) { return kind >= LCE_KIND_EXT_FIRST && kind < LCE_KIND_COUNT; }
bool ext_taken(const lce_ledger *l, uint32_t kind, uint32_t id) {
    for (uint32_t i = 0; i < l->count; ++i)
        if (l->entries[i].kind == kind && l->entries[i].id == id) return true;
    return false;
}
bool ext_in_range(const lce_id_space *s, uint32_t kind, uint32_t id) {
    for (int r = 0; r < LCE_EXT_RANGES; ++r) {
        const uint32_t *g = s->ext_range[kind - LCE_KIND_EXT_FIRST][r];
        if ((g[0] || g[1]) && id >= g[0] && id <= g[1]) return true;
    }
    return false;
}
bool ext_foreign(const lce_id_space *s, uint32_t kind, uint32_t id) {
    return !ext_in_range(s, kind, id) || (s->ext_in_use && s->ext_in_use(s->user, kind, id));
}
bool in_list(const uint32_t *list, uint32_t n, uint32_t id) {
    for (uint32_t i = 0; i < n; ++i) if (list[i] == id) return true;
    return false;
}
// The game or another provider owns this item-space ID.
bool item_foreign(const lce_id_space *s, uint32_t id) {
    return in_list(s->reserved_items, s->reserved_item_count, id) || (s->item_in_use && s->item_in_use(s->user, id));
}
bool block_foreign(const lce_id_space *s, uint32_t id) {
    return in_list(s->reserved_blocks, s->reserved_block_count, id) || (s->block_in_use && s->block_in_use(s->user, id)) ||
           item_foreign(s, lce_block_item_id(id));
}
}  // namespace

extern "C" lce_status lce_ledger_load(lce_ledger *l, const char *text, size_t len) {
    lce_ledger_init(l);
    if (len == 0) return LCE_OK;
    const size_t hl = sizeof kHeader - 1;
    if (len < hl || memcmp(text, kHeader, hl)) return LCE_E_LEDGER;
    // Trailer: "sha256 " + 64 hex + "\n" = 72 bytes
    if (len < hl + 72 || text[len - 1] != '\n') return LCE_E_LEDGER;
    size_t body_end = len - 72;
    if (memcmp(text + body_end, "sha256 ", 7)) return LCE_E_LEDGER;
    uint8_t want[32], got[32];
    for (int i = 0; i < 32; ++i) {
        int hi = hexval(text[body_end + 7 + 2 * i]), lo = hexval(text[body_end + 8 + 2 * i]);
        if (hi < 0 || lo < 0) return LCE_E_LEDGER;
        want[i] = (uint8_t)(hi << 4 | lo);
    }
    lce_sha256(text, body_end, got);
    if (memcmp(want, got, 32)) return LCE_E_HASH;
    size_t pos = hl;
    while (pos < body_end) {
        const char *line = text + pos;
        const char *nl = (const char *)memchr(line, '\n', body_end - pos);
        if (!nl) return LCE_E_LEDGER;
        size_t n = (size_t)(nl - line);
        const char *sp1 = (const char *)memchr(line, ' ', n);
        if (!sp1) return LCE_E_LEDGER;
        const char *sp2 = (const char *)memchr(sp1 + 1, ' ', (size_t)(nl - sp1 - 1));
        if (!sp2) return LCE_E_LEDGER;
        size_t kl = (size_t)(sp1 - line), keyl = (size_t)(sp2 - sp1 - 1);
        uint8_t kind = LCE_KIND_COUNT;
        for (uint8_t k = 0; k < LCE_KIND_COUNT; ++k)
            if (kl == strlen(kKindName[k]) && !memcmp(line, kKindName[k], kl)) kind = k;
        if (kind == LCE_KIND_COUNT) return LCE_E_LEDGER;
        if (!keyl || keyl >= LCE_MAX_KEY || l->count >= LCE_LEDGER_MAX) return LCE_E_LEDGER;
        lce_ledger_entry &e = l->entries[l->count];
        memcpy(e.key, sp1 + 1, keyl); e.key[keyl] = 0;
        if (!lce_valid_key(e.key)) return LCE_E_LEDGER;
        uint32_t id;
        if (!parse_u32(sp2 + 1, (size_t)(nl - sp2 - 1), &id)) return LCE_E_LEDGER;
        // reject duplicates (key or id) - a corrupt ledger must never be "repaired"
        if (lce_ledger_find(l, (lce_id_kind)kind, e.key) >= 0) return LCE_E_LEDGER;
        if (is_ext(kind) ? (ext_taken(l, kind, id) || id > 65535) :
            kind == LCE_KIND_ITEM ? item_space_taken(l, id) : (block_taken(l, id) || item_space_taken(l, lce_block_item_id(id)))) return LCE_E_LEDGER;
        e.kind = kind; e.id = id; e.live = 0;
        ++l->count;
        pos += n + 1;
    }
    return LCE_OK;
}

extern "C" lce_status lce_ledger_serialize(const lce_ledger *l, char *out, size_t cap, size_t *out_len) {
    size_t pos = 0;
    int n = snprintf(out, cap, "%s", kHeader);
    if (n < 0 || (size_t)n >= cap) return LCE_E_FULL;
    pos = (size_t)n;
    for (uint32_t i = 0; i < l->count; ++i) {
        const lce_ledger_entry &e = l->entries[i];
        n = snprintf(out + pos, cap - pos, "%s %s %u\n", e.kind < LCE_KIND_COUNT ? kKindName[e.kind] : "?", e.key, (unsigned)e.id);
        if (n < 0 || (size_t)n >= cap - pos) return LCE_E_FULL;
        pos += (size_t)n;
    }
    uint8_t d[32];
    lce_sha256(out, pos, d);
    if (cap - pos < 73) return LCE_E_FULL;
    memcpy(out + pos, "sha256 ", 7);
    static const char hx[] = "0123456789abcdef";
    for (int i = 0; i < 32; ++i) { out[pos + 7 + 2 * i] = hx[d[i] >> 4]; out[pos + 8 + 2 * i] = hx[d[i] & 15]; }
    out[pos + 71] = '\n';
    out[pos + 72] = 0;
    *out_len = pos + 72;
    return LCE_OK;
}

extern "C" void lce_ledger_reset_live(lce_ledger *l) {
    for (uint32_t i = 0; i < l->count; ++i) l->entries[i].live = 0;
}

extern "C" int64_t lce_ledger_find(const lce_ledger *l, lce_id_kind kind, const char *key) {
    for (uint32_t i = 0; i < l->count; ++i)
        if (l->entries[i].kind == kind && !strcmp(l->entries[i].key, key)) return l->entries[i].id;
    return -1;
}

extern "C" lce_status lce_ledger_claim(lce_ledger *l, const lce_id_space *s, lce_id_kind kind, const char *key, uint32_t *out_id) {
    if (!lce_valid_key(key)) return LCE_E_INVALID;
    for (uint32_t i = 0; i < l->count; ++i) {
        lce_ledger_entry &e = l->entries[i];
        if (e.kind != kind || strcmp(e.key, key)) continue;
        if (e.live) return LCE_E_COLLISION;  // claimed twice in one session
        // A recorded ID that is now owned by the game/another provider means the
        // game build or another plugin changed under us: fail closed.
        if (is_ext(kind) ? (s->ext_in_use && s->ext_in_use(s->user, kind, e.id)) :
            kind == LCE_KIND_ITEM ? item_foreign(s, e.id) : block_foreign(s, e.id)) return LCE_E_COLLISION;
        e.live = 1;
        *out_id = e.id;
        return LCE_OK;
    }
    if (l->count >= LCE_LEDGER_MAX) return LCE_E_FULL;
    if (is_ext(kind)) {
        for (int r = 0; r < LCE_EXT_RANGES; ++r) {
            const uint32_t *g = s->ext_range[kind - LCE_KIND_EXT_FIRST][r];
            if (!g[0] && !g[1]) continue;
            for (uint32_t id = g[0]; id <= g[1] && id <= 65535; ++id) {
                if (ext_taken(l, kind, id) || ext_foreign(s, kind, id)) continue;
                lce_ledger_entry &e = l->entries[l->count++];
                memset(&e, 0, sizeof e);
                e.kind = (uint8_t)kind; e.id = id; e.live = 1;
                strncpy(e.key, key, LCE_MAX_KEY - 1);
                l->dirty = 1;
                *out_id = id;
                return LCE_OK;
            }
        }
        return LCE_E_FULL;
    }
    uint32_t lo = kind == LCE_KIND_ITEM ? s->item_min : s->block_min;
    uint32_t hi = kind == LCE_KIND_ITEM ? s->item_max : s->block_max;
    const bool allow = kind == LCE_KIND_BLOCK && s->block_allow_count;
    uint32_t n = allow ? s->block_allow_count : hi - lo + 1;
    for (uint32_t k = 0; k < n && (allow || lo + k >= lo); ++k) {
        uint32_t id = allow ? s->block_allow[k] : lo + k;
        if (id < lo || id > hi) continue;
        bool busy = kind == LCE_KIND_ITEM
                        ? (item_space_taken(l, id) || item_foreign(s, id))
                        : (block_taken(l, id) || item_space_taken(l, lce_block_item_id(id)) || block_foreign(s, id));
        if (busy) continue;
        lce_ledger_entry &e = l->entries[l->count++];
        memset(&e, 0, sizeof e);
        e.kind = (uint8_t)kind; e.id = id; e.live = 1;
        strncpy(e.key, key, LCE_MAX_KEY - 1);
        l->dirty = 1;
        *out_id = id;
        return LCE_OK;
    }
    return LCE_E_FULL;
}
