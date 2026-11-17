#include "tex_registry.h"
#include <stdio.h>
#include <string.h>

const char *const lce_tex_file_names[LCE_TEX_FILES] = {"TexturePack.pck", "Data/x16Data.pck", "Data/media.arc", "languages.loc"};
const uint64_t lce_tex_file_max[LCE_TEX_FILES] = {1u << 20, 8u << 20, 4u << 20, 64u << 10};

static int slug_ok(const char *s) {
    size_t n = strlen(s);
    if (n < 2 || n > 48 || s[0] == '-') return 0;
    for (size_t i = 0; i < n; ++i)
        if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= '0' && s[i] <= '9') || s[i] == '-')) return 0;
    return 1;
}

static int name_ok(const char *s, size_t n) {
    if (!n || n > 63) return 0;
    for (size_t i = 0; i < n; ++i) if ((unsigned char)s[i] < 0x20 || (unsigned char)s[i] > 0x7e) return 0;
    return 1;
}

size_t lce_tex_desc_format(const lce_catalog_item *item, char *out, size_t cap) {
    static const char hexd[] = "0123456789abcdef";
    const lce_catalog_file *f[LCE_TEX_FILES] = {0};
    if (!item || !out || !cap || !name_ok(item->name, strlen(item->name))) return 0;
    for (unsigned i = 0; i < item->file_count; ++i)
        for (unsigned k = 0; k < LCE_TEX_FILES; ++k)
            if (!strcmp(item->files[i].path, lce_tex_file_names[k])) f[k] = &item->files[i];
    for (unsigned k = 0; k < LCE_TEX_FILES; ++k)
        if (!f[k] || !f[k]->size || f[k]->size > lce_tex_file_max[k]) return 0;
    int n = snprintf(out, cap, "LCETEX1\nid %u\nname %s\n", (unsigned)item->dlc_id, item->name);
    if (n <= 0 || (size_t)n >= cap) return 0;
    size_t at = (size_t)n;
    for (unsigned k = 0; k < LCE_TEX_FILES; ++k) {
        n = snprintf(out + at, cap - at, "%llu ", (unsigned long long)f[k]->size);
        if (n <= 0 || (size_t)n >= cap - at) return 0;
        at += (size_t)n;
        if (at + 64 + 1 >= cap) return 0;
        for (unsigned i = 0; i < 32; ++i) { out[at++] = hexd[f[k]->sha256[i] >> 4]; out[at++] = hexd[f[k]->sha256[i] & 15]; }
        n = snprintf(out + at, cap - at, " %s\n", lce_tex_file_names[k]);
        if (n <= 0 || (size_t)n >= cap - at) return 0;
        at += (size_t)n;
    }
    return at;
}

/* ---- descriptor parser ---- */
typedef struct { const char *p, *end; } cur;

static int lit(cur *c, const char *s) {
    size_t n = strlen(s);
    if ((size_t)(c->end - c->p) < n || memcmp(c->p, s, n)) return 0;
    c->p += n;
    return 1;
}
static int num(cur *c, uint64_t *out, uint64_t max) {
    uint64_t v = 0; int any = 0;
    while (c->p < c->end && *c->p >= '0' && *c->p <= '9') {
        v = v * 10 + (uint64_t)(*c->p++ - '0'); any = 1;
        if (v > max) return 0;
    }
    *out = v;
    return any;
}
static int hex32(cur *c, uint8_t out[32]) {
    if (c->end - c->p < 64) return 0;
    for (int i = 0; i < 32; ++i) {
        int v = 0;
        for (int k = 0; k < 2; ++k) {
            char ch = c->p[2 * i + k];
            int d = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : -1;
            if (d < 0) return 0;
            v = v * 16 + d;
        }
        out[i] = (uint8_t)v;
    }
    c->p += 64;
    return 1;
}

int lce_tex_desc_parse(const char *text, size_t len, lce_tex_entry *out) {
    lce_tex_entry e;
    cur c;
    uint64_t v;
    if (!text || !out || len < 32 || len > 1024) return 0;
    memset(&e, 0, sizeof e);
    c.p = text; c.end = text + len;
    if (!lit(&c, "LCETEX1\nid ") || !num(&c, &v, 0xffffff) || !lit(&c, "\nname ")) return 0;
    e.desc_id = (uint32_t)v;
    const char *nm = c.p;
    while (c.p < c.end && *c.p != '\n') ++c.p;
    if (c.p >= c.end || !name_ok(nm, (size_t)(c.p - nm))) return 0;
    memcpy(e.name, nm, (size_t)(c.p - nm));
    ++c.p;
    for (unsigned k = 0; k < LCE_TEX_FILES; ++k) {
        if (!num(&c, &v, lce_tex_file_max[k]) || !v || !lit(&c, " ") || !hex32(&c, e.sha256[k]) || !lit(&c, " ") ||
            !lit(&c, lce_tex_file_names[k]) || !lit(&c, "\n")) return 0;
        e.size[k] = v;
    }
    if (c.p != c.end) return 0;
    *out = e;
    return 1;
}

/* ---- PCK prefix: same structure as the strict full parser, stopped behind the
   parameters of record 0. ---- */
typedef struct { const uint8_t *p; size_t n, pos; } rd;
static int word(rd *r, uint32_t *x) {
    if (r->pos > r->n || r->n - r->pos < 4) return 0;
    const uint8_t *q = r->p + r->pos;
    *x = (uint32_t)q[0] | ((uint32_t)q[1] << 8) | ((uint32_t)q[2] << 16) | ((uint32_t)q[3] << 24);
    r->pos += 4;
    return 1;
}
/* kind, units, utf16 name, 4 zero bytes. Returns name offset in *off. */
static int named(rd *r, uint32_t *kind, uint32_t *off, uint32_t *units) {
    uint32_t k, u;
    if (!word(r, &k) || !word(r, &u) || u > 32768 || r->n - r->pos < 4 || u > (r->n - r->pos - 4) / 2) return 0;
    for (uint32_t i = 0; i < u; ++i) if (!r->p[r->pos + 2 * i] && !r->p[r->pos + 2 * i + 1]) return 0;
    size_t end = r->pos + 2u * (size_t)u;
    if (r->p[end] || r->p[end + 1] || r->p[end + 2] || r->p[end + 3]) return 0;
    *kind = k; *off = (uint32_t)r->pos; *units = u;
    r->pos += 2u * ((size_t)u + 2);
    return 1;
}

int lce_tex_pck_prefix_id(const uint8_t *buf, size_t len, uint32_t *id) {
    rd r = {buf, len, 0};
    uint32_t version, mappings, count, kind, off, units, size, params, rec_kind[3], rec_off[3], rec_units[3];
    if (!buf || !id) return 0;
    *id = 0;
    if (!word(&r, &version) || version != 3 || !word(&r, &mappings) || mappings > 64) return 0;
    for (uint32_t i = 0; i < mappings; ++i) if (!named(&r, &kind, &off, &units)) return 0;
    if (!word(&r, &count) || count != 3) return 0;
    for (unsigned i = 0; i < 3; ++i)
        if (!word(&r, &size) || !named(&r, &rec_kind[i], &rec_off[i], &rec_units[i])) return 0;
    /* record 0: type 4, name "0" */
    if (rec_kind[0] != 4 || rec_units[0] != 1 || buf[rec_off[0]] != '0' || buf[rec_off[0] + 1]) return 0;
    if (!word(&r, &params) || !params || params > 64) return 0;
    for (uint32_t i = 0; i < params; ++i) {
        if (!named(&r, &kind, &off, &units)) return 0;
        if (kind != 0) continue;
        if (!units || units > 8) return 0;
        uint32_t v = 0;
        for (uint32_t k = 0; k < units; ++k) {
            if (buf[off + 2 * k + 1] || buf[off + 2 * k] < '0' || buf[off + 2 * k] > '9') return 0;
            v = v * 10 + (uint32_t)(buf[off + 2 * k] - '0');
        }
        if (!v || v > 0xffffff) return 0;
        *id = v;
        return 1;
    }
    return 0;
}

int lce_tex_entry_path(char *out, size_t cap, const char *root, const lce_tex_entry *e, unsigned file) {
    if (!out || !cap || !root || !e || file >= LCE_TEX_FILES || !slug_ok(e->slug)) return 0;
    int n = snprintf(out, cap, "%s/%s/%s", root, e->slug, lce_tex_file_names[file]);
    return n > 0 && (size_t)n < cap;
}

typedef struct {
    const lce_tex_fs *fs; const char *root; const uint32_t *reserved; size_t reserved_count;
    lce_tex_entry *out; size_t max, count; lce_tex_scan_stats *st;
} scan_ctx;

static void visit(void *vctx, const char *name) {
    scan_ctx *c = vctx;
    char path[256], text[1100];
    uint8_t head[4096];
    lce_tex_entry e;
    if (!name || !slug_ok(name)) return;
    if ((size_t)snprintf(path, sizeof path, "%s/%s/" LCE_TEX_DESC_FILE, c->root, name) >= sizeof path) return;
    long n = c->fs->read_file(c->fs->ctx, path, text, sizeof text);
    if (n <= 0 || (size_t)n >= sizeof text || !lce_tex_desc_parse(text, (size_t)n, &e)) { c->st->invalid++; return; }
    memset(e.slug, 0, sizeof e.slug);
    memcpy(e.slug, name, strlen(name));
    if (!lce_tex_entry_path(path, sizeof path, c->root, &e, 0)) { c->st->invalid++; return; }
    n = c->fs->read_file(c->fs->ctx, path, (char *)head, sizeof head);
    if (n <= 0 || !lce_tex_pck_prefix_id(head, (size_t)n, &e.dlc_id) || (e.desc_id && e.desc_id != e.dlc_id)) { c->st->invalid++; return; }
    for (size_t i = 0; i < c->reserved_count; ++i) if (c->reserved[i] == e.dlc_id) { c->st->reserved++; return; }
    if (c->count >= c->max) { c->st->full++; return; }
    size_t at = c->count++;           /* insertion sort by (dlc_id, slug): stable registration order */
    while (at && (c->out[at - 1].dlc_id > e.dlc_id || (c->out[at - 1].dlc_id == e.dlc_id && strcmp(c->out[at - 1].slug, e.slug) > 0))) {
        c->out[at] = c->out[at - 1]; --at;
    }
    c->out[at] = e;
}

size_t lce_tex_registry_scan(const lce_tex_fs *fs, const char *root, const uint32_t *reserved, size_t reserved_count,
                             lce_tex_entry *out, size_t max, lce_tex_scan_stats *stats) {
    lce_tex_scan_stats local;
    if (!stats) stats = &local;
    memset(stats, 0, sizeof *stats);
    if (!fs || !fs->list_dirs || !fs->read_file || !root || !out || !max) return 0;
    if (max > LCE_TEX_REGISTRY_MAX) max = LCE_TEX_REGISTRY_MAX;
    scan_ctx c = {fs, root, reserved, reserved_count, out, max, 0, stats};
    if (!fs->list_dirs(fs->ctx, root, visit, &c)) return 0;
    /* Same DLC id twice: the first slug (sorted) wins, the rest is dropped. */
    size_t w = 0;
    for (size_t i = 0; i < c.count; ++i) {
        if (w && out[w - 1].dlc_id == out[i].dlc_id) { stats->duplicate++; continue; }
        if (w != i) out[w] = out[i];
        ++w;
    }
    return w;
}
