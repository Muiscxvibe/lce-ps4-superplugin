#include "skin_registry.h"
#include <string.h>
#include <stdio.h>

static int slug_ok(const char *s) {
    size_t n = strlen(s);
    if (n < 2 || n > 48 || s[0] == '-') return 0;
    for (size_t i = 0; i < n; ++i) if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= '0' && s[i] <= '9') || s[i] == '-')) return 0;
    return 1;
}

static int read_u64(const char **p, const char *end, uint64_t *out) {
    uint64_t v = 0; int any = 0;
    while (*p < end && **p >= '0' && **p <= '9') {
        if (v > 0xffffffffffffull) return 0;
        v = v * 10 + (uint64_t)(*(*p)++ - '0'); any = 1;
    }
    *out = v;
    return any;
}

int lce_skin_desc_parse(const char *text, size_t len, lce_skin_entry *out) {
    const char *p = text, *end = text + len;
    uint64_t dlc, count, size;
    if (!text || !out || len < 70 || len > 160) return 0;
    if (!read_u64(&p, end, &dlc) || p >= end || *p++ != ' ') return 0;
    if (!read_u64(&p, end, &count) || p >= end || *p++ != ' ') return 0;
    if (!read_u64(&p, end, &size) || p >= end || *p++ != ' ') return 0;
    if (end - p != 65 || p[64] != '\n') return 0;
    if (!dlc || dlc > 0xffffff || !count || count > 256 || !size || size > 64ull * 1024 * 1024) return 0;
    for (int i = 0; i < 32; ++i) {
        int v = 0;
        for (int k = 0; k < 2; ++k) {
            char c = p[2 * i + k];
            int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
            if (d < 0) return 0;
            v = v * 16 + d;
        }
        out->sha256[i] = (uint8_t)v;
    }
    out->dlc_id = (uint32_t)dlc; out->skin_count = (uint32_t)count; out->size = size;
    return 1;
}

typedef struct {
    const lce_skin_fs *fs; const char *root; const uint32_t *reserved; size_t reserved_count;
    lce_skin_entry *out; size_t max, count;
} scan_ctx;

static void visit(void *vctx, const char *name) {
    scan_ctx *c = vctx;
    char path[256], buf[200];
    lce_skin_entry e;
    if (!name || !slug_ok(name) || c->count >= c->max) return;
    if ((size_t)snprintf(path, sizeof path, "%s/%s/lce.desc", c->root, name) >= sizeof path) return;
    long n = c->fs->read_file(c->fs->ctx, path, buf, sizeof buf);
    if (n <= 0 || (size_t)n >= sizeof buf || !lce_skin_desc_parse(buf, (size_t)n, &e)) return;
    for (size_t i = 0; i < c->reserved_count; ++i) if (c->reserved[i] == e.dlc_id) return;
    for (size_t i = 0; i < c->count; ++i) if (c->out[i].dlc_id == e.dlc_id || !strcmp(c->out[i].slug, name)) return;
    memset(e.slug, 0, sizeof e.slug);
    memcpy(e.slug, name, strlen(name));
    size_t at = c->count++;                       /* insertion sort by dlc_id: stable registration order */
    while (at && c->out[at - 1].dlc_id > e.dlc_id) { c->out[at] = c->out[at - 1]; --at; }
    c->out[at] = e;
}

size_t lce_skin_registry_scan(const lce_skin_fs *fs, const char *root, const uint32_t *reserved, size_t reserved_count,
                              lce_skin_entry *out, size_t max) {
    if (!fs || !fs->list_dirs || !fs->read_file || !root || !out || !max) return 0;
    if (max > LCE_SKIN_REGISTRY_MAX) max = LCE_SKIN_REGISTRY_MAX;
    scan_ctx c = {fs, root, reserved, reserved_count, out, max, 0};
    if (!fs->list_dirs(fs->ctx, root, visit, &c)) return 0;
    return c.count;
}
