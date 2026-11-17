#include "content_download.h"
#include "client_report.h"
#include <string.h>
#include <stdio.h>

int lce_https_get(const lce_https_api *a, const lce_https_trust *trust, const char *url, uint64_t max,
                  int (*sink)(void *, const uint8_t *, size_t), void *sink_ctx, uint64_t *received) {
    static uint8_t chunk[16384];
    int tmpl = -1, conn = -1, req = -1, code = 0, ok = 0;
    uint64_t total = 0;
    if (received) *received = 0;
    if (!a || !trust || !trust->ready || trust->http_context < 0 || !url || strncmp(url, "https://", 8) ||
        !sink || !a->create_template || !a->set_redirect || !a->set_connect_timeout || !a->set_send_timeout ||
        !a->set_recv_timeout || !a->connect_url || !a->request_url || !a->send || !a->status || !a->read ||
        !a->delete_request || !a->delete_connection || !a->delete_template) return 0;
    tmpl = a->create_template(trust->http_context, "LCE-Plugin/1", 2, 0);
    if (tmpl < 0 || a->set_redirect(tmpl, 0) < 0 || a->set_connect_timeout(tmpl, 10000000) < 0 ||
        a->set_send_timeout(tmpl, 10000000) < 0 || a->set_recv_timeout(tmpl, 30000000) < 0) goto done;
    conn = a->connect_url(tmpl, url, 1);
    if (conn < 0) goto done;
    req = a->request_url(conn, 0 /* GET */, url, 0);
    if (req < 0 || a->send(req, NULL, 0) < 0 || a->status(req, &code) < 0 || code != 200) goto done;
    for (;;) {
        int n = a->read(req, chunk, sizeof chunk);
        if (n < 0 || (size_t)n > sizeof chunk) goto done;
        if (!n) break;
        total += (uint64_t)n;
        if (total > max || !sink(sink_ctx, chunk, (size_t)n)) goto done;
    }
    ok = 1;
done:
    if (req >= 0) a->delete_request(req);
    if (conn >= 0) a->delete_connection(conn);
    if (tmpl >= 0) a->delete_template(tmpl);
    if (received) *received = total;
    return ok;
}

/* ---- buffer sink ---- */
typedef struct { uint8_t *p; size_t cap, n; } buf_sink;
static int to_buf(void *ctx, const uint8_t *d, size_t n) {
    buf_sink *b = ctx;
    if (n > b->cap - b->n) return 0;
    memcpy(b->p + b->n, d, n); b->n += n; return 1;
}

static int url_join(char *out, size_t cap, const char *base, const char *repo, const char *a, const char *b,
                    const char *c) {
    const char *parts[] = {base, "/v1/content/", repo, a ? "/" : NULL, a, b ? "/" : NULL, b, c ? "/" : NULL, c};
    size_t n = 0;
    for (size_t i = 0; i < sizeof parts / sizeof parts[0]; ++i) {
        if (!parts[i]) continue;
        size_t l = strlen(parts[i]);
        if (n + l + 1 > cap) return 0;
        memcpy(out + n, parts[i], l); n += l;
    }
    out[n] = 0;
    return 1;
}

int lce_content_fetch_catalog_with(lce_content_getter get, void *gctx, const char *base, const char *repo,
                                   uint8_t *buf, size_t cap, lce_catalog *out) {
    char url[384], sig[160];
    buf_sink b = {buf, cap, 0}, s = {(uint8_t *)sig, sizeof sig - 1, 0};
    if (!get || !base || !repo || !buf || !out || strncmp(base, "https://", 8)) return 0;
    if (!url_join(url, sizeof url, base, repo, "index.json", NULL, NULL) || !get(gctx, url, cap, to_buf, &b)) return 0;
    if (!url_join(url, sizeof url, base, repo, "index.json.sig", NULL, NULL) || !get(gctx, url, sizeof sig - 1, to_buf, &s)) return 0;
    sig[s.n] = 0;
    if (!lce_catalog_verify(buf, b.n, sig, s.n, lce_content_public_key)) return 0;
    return lce_catalog_parse((const char *)buf, b.n, out);
}

/* ---- file sink: write + hash ---- */
typedef struct { const lce_content_fs *fs; int fd; lce_sha256_ctx sha; uint64_t n; int failed; } file_sink;
static int to_file(void *ctx, const uint8_t *d, size_t n) {
    file_sink *f = ctx;
    if (f->failed || f->fs->write(f->fs->ctx, f->fd, d, n) != 0) { f->failed = 1; return 0; }
    lce_sha256_update(&f->sha, d, n);
    f->n += n;
    return 1;
}

/* "<dlc_id> <skin_count> <size> <sha256 hex>\n" next to SkinPack.pck: what the
   startup skin registrar needs. Written last, atomically, so a pack is only
   registered once all of its files are in place. */
static int write_skin_desc(const lce_content_fs *fs, const char *root, const lce_catalog_item *item) {
    const lce_catalog_file *pck = NULL;
    char path[384], part[400], line[160];
    static const char hexd[] = "0123456789abcdef";
    for (unsigned i = 0; i < item->file_count; ++i) if (!strcmp(item->files[i].path, "SkinPack.pck")) pck = &item->files[i];
    if (!pck || pck->size > 64u * 1024u * 1024u) return 0;
    size_t n = (size_t)snprintf(line, sizeof line, "%u %u %llu ", (unsigned)item->dlc_id, (unsigned)item->skin_count,
                                (unsigned long long)pck->size);
    if (n + 66 > sizeof line) return 0;
    for (unsigned i = 0; i < 32; ++i) { line[n++] = hexd[pck->sha256[i] >> 4]; line[n++] = hexd[pck->sha256[i] & 15]; }
    line[n++] = '\n';
    if ((size_t)snprintf(path, sizeof path, "%s/%s/lce.desc", root, item->id) >= sizeof path) return 0;
    if ((size_t)snprintf(part, sizeof part, "%s.part", path) >= sizeof part) return 0;
    int fd = fs->open_write(fs->ctx, part);
    if (fd < 0) return 0;
    int ok = fs->write(fs->ctx, fd, line, n) == 0;
    fs->close(fs->ctx, fd);
    if (!ok || fs->rename(fs->ctx, part, path) != 0) { fs->unlink(fs->ctx, part); return 0; }
    return 1;
}

int lce_content_install_with(lce_content_getter get, void *gctx, const lce_content_fs *fs, const char *base,
                             const char *repo, const char *root, const lce_catalog_item *item) {
    char url[384], dest[384], part[400], dir[384];
    if (!get || !fs || !base || !repo || !root || !item || !item->file_count) return 0;
    for (unsigned i = 0; i < item->file_count; ++i) {
        const lce_catalog_file *f = &item->files[i];
        uint8_t digest[32];
        if (!lce_catalog_install_path(dest, sizeof dest, root, repo, item, f)) return 0;
        const char *slash = strrchr(dest, '/');        /* parent dir incl. any sub-folders */
        size_t dl = (size_t)(slash - dest);
        memcpy(dir, dest, dl); dir[dl] = 0;
        if (strlen(dest) + 6 > sizeof part) return 0;
        strcpy(part, dest); strcat(part, ".part");
        if (fs->mkdir_p(fs->ctx, dir) != 0) return 0;
        if (!url_join(url, sizeof url, base, repo, "file", item->id, NULL)) return 0;
        size_t ul = strlen(url);
        if (ul + 2 + strlen(item->version) + strlen(f->asset) + 1 > sizeof url) return 0;
        strcat(url, "/"); strcat(url, item->version); strcat(url, "/"); strcat(url, f->asset);
        file_sink s = {fs, fs->open_write(fs->ctx, part), {{0}}, 0, 0};
        if (s.fd < 0) return 0;
        lce_sha256_init(&s.sha);
        int got = get(gctx, url, f->size, to_file, &s);
        fs->close(fs->ctx, s.fd);
        lce_sha256_final(&s.sha, digest);
        if (!got || s.failed || s.n != f->size || memcmp(digest, f->sha256, 32) || fs->rename(fs->ctx, part, dest) != 0) {
            fs->unlink(fs->ctx, part);
            return 0;
        }
    }
    if (item->dlc_id && item->skin_count) return write_skin_desc(fs, root, item);
    return 1;
}

typedef struct { const lce_https_api *a; const lce_https_trust *t; } https_ctx;
static int https_getter(void *ctx, const char *url, uint64_t max, int (*sink)(void *, const uint8_t *, size_t),
                        void *sink_ctx) {
    https_ctx *h = ctx;
    return lce_https_get(h->a, h->t, url, max, sink, sink_ctx, NULL);
}

int lce_content_fetch_catalog(const lce_https_api *a, const lce_https_trust *t, const char *base, const char *repo,
                              uint8_t *buf, size_t cap, lce_catalog *out) {
    https_ctx h = {a, t};
    return lce_content_fetch_catalog_with(https_getter, &h, base, repo, buf, cap, out);
}

int lce_content_install(const lce_https_api *a, const lce_https_trust *t, const lce_content_fs *fs,
                        const char *base, const char *repo, const char *root, const lce_catalog_item *item) {
    https_ctx h = {a, t};
    return lce_content_install_with(https_getter, &h, fs, base, repo, root, item);
}
