#include "content_catalog.h"
#include "client_report.h"   /* lce_sha256 */
#include "monocypher-ed25519.h"
#include <string.h>

const uint8_t lce_content_public_key[32] = {
    0xa2,0x16,0x74,0x0b,0x83,0xc4,0xc9,0x2a,0x30,0xc3,0x6c,0x57,0x51,0x4e,0x85,0x9b,
    0xc0,0x07,0x78,0xe8,0xda,0x9c,0xc5,0xd0,0xc1,0xf8,0x0d,0xe5,0xe3,0x07,0x3e,0xed};

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static int unhex(const char *s, size_t n, uint8_t *out) {
    for (size_t i = 0; i < n; ++i) {
        int hi = hexval(s[2 * i]), lo = hexval(s[2 * i + 1]);
        if (hi < 0 || lo < 0) return 0;
        out[i] = (uint8_t)(hi << 4 | lo);
    }
    return 1;
}

int lce_catalog_verify(const uint8_t *json, size_t len, const char *sig_hex, size_t sig_len,
                       const uint8_t public_key[32]) {
    uint8_t sig[64];
    if (!json || !sig_hex || !public_key) return 0;
    while (sig_len && (sig_hex[sig_len - 1] == '\n' || sig_hex[sig_len - 1] == '\r')) --sig_len;
    if (sig_len != 128 || !unhex(sig_hex, 64, sig)) return 0;
    return crypto_ed25519_check(sig, public_key, json, len) == 0;
}

/* ---- minimal strict JSON reader for the catalog schema ---- */
typedef struct { const char *p, *end; int depth; } rd;

static void ws(rd *r) { while (r->p < r->end && (*r->p == ' ' || *r->p == '\n' || *r->p == '\r' || *r->p == '\t')) ++r->p; }
static int lit(rd *r, char c) { ws(r); if (r->p < r->end && *r->p == c) { ++r->p; return 1; } return 0; }

/* Reads a JSON string with no escapes other than \" \\ \/ and printable ASCII only. */
static int str(rd *r, char *out, size_t cap) {
    size_t n = 0;
    if (!lit(r, '"')) return 0;
    while (r->p < r->end && *r->p != '"') {
        char c = *r->p++;
        if (c == '\\') {
            if (r->p >= r->end) return 0;
            c = *r->p++;
            if (c != '"' && c != '\\' && c != '/') return 0;
        }
        if ((unsigned char)c < 0x20 || (unsigned char)c > 0x7e) return 0;
        if (out) { if (n + 1 >= cap) return 0; out[n++] = c; }
    }
    if (r->p >= r->end) return 0;
    ++r->p;
    if (out) out[n] = 0;
    return 1;
}
static int num(rd *r, uint64_t *out) {
    uint64_t v = 0; int any = 0;
    ws(r);
    while (r->p < r->end && *r->p >= '0' && *r->p <= '9') {
        if (v > (UINT64_MAX - 9) / 10) return 0;
        v = v * 10 + (uint64_t)(*r->p++ - '0'); any = 1;
    }
    if (out) *out = v;
    return any;
}
static int skip(rd *r);
static int skip_container(rd *r, char open, char close) {
    if (!lit(r, open) || ++r->depth > 16) return 0;
    if (lit(r, close)) { --r->depth; return 1; }
    for (;;) {
        if (open == '{') { if (!str(r, NULL, 0) || !lit(r, ':')) return 0; }
        if (!skip(r)) return 0;
        if (lit(r, ',')) continue;
        if (!lit(r, close)) return 0;
        --r->depth; return 1;
    }
}
static int skip(rd *r) {
    ws(r);
    if (r->p >= r->end) return 0;
    if (*r->p == '"') return str(r, NULL, 0);
    if (*r->p == '{') return skip_container(r, '{', '}');
    if (*r->p == '[') return skip_container(r, '[', ']');
    if (*r->p >= '0' && *r->p <= '9') return num(r, NULL);
    static const char *const words[] = {"true", "false", "null"};
    for (unsigned i = 0; i < 3; ++i) {
        size_t l = strlen(words[i]);
        if ((size_t)(r->end - r->p) >= l && !memcmp(r->p, words[i], l)) { r->p += l; return 1; }
    }
    return 0;
}

static int id_ok(const char *s) {
    size_t n = strlen(s);
    if (n < 2 || n > 47) return 0;
    for (size_t i = 0; i < n; ++i) if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= '0' && s[i] <= '9') || s[i] == '-')) return 0;
    return s[0] != '-';
}
static int version_ok(const char *s) {
    size_t n = strlen(s);
    if (!n || n > 15 || s[0] == '.' || s[n - 1] == '.') return 0;
    for (size_t i = 0; i < n; ++i) if (!((s[i] >= '0' && s[i] <= '9') || s[i] == '.')) return 0;
    return 1;
}
static int segment_ok(const char *s, size_t n) {
    if (!n || n > 63 || s[0] == '.') return 0;       /* also rejects "." and ".." */
    for (size_t i = 0; i < n; ++i)
        if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= '0' && s[i] <= '9') ||
              s[i] == '.' || s[i] == '_' || s[i] == '-')) return 0;
    return 1;
}
static int relpath_ok(const char *s) {               /* one or more segments, max depth 4 */
    size_t n = strlen(s), start = 0, depth = 0;
    if (!n || n > 127 || s[0] == '/' || s[n - 1] == '/') return 0;
    for (size_t i = 0; i <= n; ++i)
        if (i == n || s[i] == '/') {
            if (!segment_ok(s + start, i - start) || ++depth > 4) return 0;
            start = i + 1;
        }
    return 1;
}
static int asset_ok(const char *s) {
    size_t n = strlen(s);
    if (!n || n > 127 || s[0] == '.') return 0;
    for (size_t i = 0; i < n; ++i)
        if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= '0' && s[i] <= '9') ||
              s[i] == '.' || s[i] == '_' || s[i] == '-')) return 0;
    return 1;
}

static int parse_file(rd *r, lce_catalog_file *f) {
    char key[16], sha[65] = {0}; int have_path = 0, have_size = 0, have_sha = 0;
    if (!lit(r, '{')) return 0;
    if (!lit(r, '}')) for (;;) {
        if (!str(r, key, sizeof key) || !lit(r, ':')) return 0;
        if (!strcmp(key, "path")) { if (!str(r, f->path, sizeof f->path)) return 0; have_path = 1; }
        else if (!strcmp(key, "asset")) { if (!str(r, f->asset, sizeof f->asset)) return 0; }
        else if (!strcmp(key, "size")) { if (!num(r, &f->size)) return 0; have_size = 1; }
        else if (!strcmp(key, "sha256")) { if (!str(r, sha, sizeof sha) || strlen(sha) != 64 || !unhex(sha, 32, f->sha256)) return 0; have_sha = 1; }
        else if (!skip(r)) return 0;     /* e.g. "url": the mirror path is derived, never trusted */
        if (lit(r, ',')) continue;
        if (!lit(r, '}')) return 0;
        break;
    }
    if (!have_path || !have_size || !have_sha || !relpath_ok(f->path) || f->size > (256ull << 20)) return 0;
    if (!f->asset[0]) {                                  /* derive: '/' -> "__" */
        size_t o = 0;
        for (const char *p = f->path; *p; ++p) {
            if (o + 3 >= sizeof f->asset) return 0;
            if (*p == '/') { f->asset[o++] = '_'; f->asset[o++] = '_'; } else f->asset[o++] = *p;
        }
        f->asset[o] = 0;
    }
    return asset_ok(f->asset);
}

static int parse_item(rd *r, lce_catalog_item *it) {
    char key[16]; int have_id = 0, have_version = 0, have_files = 0;
    if (!lit(r, '{')) return 0;
    if (!lit(r, '}')) for (;;) {
        if (!str(r, key, sizeof key) || !lit(r, ':')) return 0;
        if (!strcmp(key, "id")) { if (!str(r, it->id, sizeof it->id)) return 0; have_id = 1; }
        else if (!strcmp(key, "name")) { if (!str(r, it->name, sizeof it->name)) return 0; }
        else if (!strcmp(key, "version")) { if (!str(r, it->version, sizeof it->version)) return 0; have_version = 1; }
        else if (!strcmp(key, "description")) { if (!str(r, it->description, sizeof it->description)) return 0; }
        else if (!strcmp(key, "lce")) {
            char k2[16]; uint64_t v;
            if (!lit(r, '{')) return 0;
            if (!lit(r, '}')) for (;;) {
                if (!str(r, k2, sizeof k2) || !lit(r, ':')) return 0;
                if (!strcmp(k2, "dlc_id")) { if (!num(r, &v) || !v || v > 0xffffff) return 0; it->dlc_id = (uint32_t)v; }
                else if (!strcmp(k2, "skin_count")) { if (!num(r, &v) || !v || v > 256) return 0; it->skin_count = (uint32_t)v; }
                else if (!skip(r)) return 0;
                if (lit(r, ',')) continue;
                if (!lit(r, '}')) return 0;
                break;
            }
        }
        else if (!strcmp(key, "files")) {
            if (!lit(r, '[')) return 0;
            if (!lit(r, ']')) for (;;) {
                if (it->file_count >= LCE_CATALOG_MAX_FILES || !parse_file(r, &it->files[it->file_count])) return 0;
                for (unsigned k = 0; k < it->file_count; ++k)
                    if (!strcmp(it->files[k].path, it->files[it->file_count].path)) return 0;   /* duplicate name */
                ++it->file_count;
                if (lit(r, ',')) continue;
                if (!lit(r, ']')) return 0;
                break;
            }
            have_files = 1;
        }
        else if (!skip(r)) return 0;
        if (lit(r, ',')) continue;
        if (!lit(r, '}')) return 0;
        break;
    }
    return have_id && have_version && have_files && it->file_count && id_ok(it->id) && version_ok(it->version);
}

int lce_catalog_parse(const char *json, size_t len, lce_catalog *out) {
    rd r = {json, json + len, 0};
    char key[16]; uint64_t schema = 0; int have_items = 0;
    if (!json || !out || len > (4u << 20)) return 0;
    memset(out, 0, sizeof *out);
    if (!lit(&r, '{')) return 0;
    if (!lit(&r, '}')) for (;;) {
        if (!str(&r, key, sizeof key) || !lit(&r, ':')) goto fail;
        if (!strcmp(key, "schema")) { if (!num(&r, &schema)) goto fail; }
        else if (!strcmp(key, "type")) { if (!str(&r, out->type, sizeof out->type)) goto fail; }
        else if (!strcmp(key, "items")) {
            if (!lit(&r, '[')) goto fail;
            if (!lit(&r, ']')) for (;;) {
                if (out->count >= LCE_CATALOG_MAX_ITEMS || !parse_item(&r, &out->items[out->count])) goto fail;
                for (size_t k = 0; k < out->count; ++k)
                    if (!strcmp(out->items[k].id, out->items[out->count].id) ||
                        (out->items[k].dlc_id && out->items[k].dlc_id == out->items[out->count].dlc_id)) goto fail;
                ++out->count;
                if (lit(&r, ',')) continue;
                if (!lit(&r, ']')) goto fail;
                break;
            }
            have_items = 1;
        }
        else if (!skip(&r)) goto fail;
        if (lit(&r, ',')) continue;
        if (!lit(&r, '}')) goto fail;
        break;
    }
    ws(&r);
    if (r.p != r.end || schema != 1 || !have_items) goto fail;
    return 1;
fail:
    memset(out, 0, sizeof *out);
    return 0;
}

int lce_catalog_file_ok(const lce_catalog_file *f, const uint8_t *data, size_t len) {
    uint8_t digest[32];
    if (!f || !data || len != f->size) return 0;
    lce_sha256(data, len, digest);
    return memcmp(digest, f->sha256, 32) == 0;
}

int lce_catalog_install_path(char *out, size_t cap, const char *root, const char *repo,
                             const lce_catalog_item *item, const lce_catalog_file *f) {
    /* "plugin" is the signed session-plugin update channel (auto-update). */
    static const char *const repos[] = {"mods", "texture-packs", "maps", "skin-packs", "plugin"};
    int known = 0;
    if (!out || !cap || !root || !repo || !item || !f) return 0;
    for (unsigned i = 0; i < 5; ++i) if (!strcmp(repo, repos[i])) known = 1;
    if (!known || !id_ok(item->id) || !relpath_ok(f->path)) return 0;
    size_t need = strlen(root) + 1 + strlen(item->id) + 1 + strlen(f->path) + 1;
    if (need > cap) return 0;
    out[0] = 0;
    strcat(out, root); strcat(out, "/"); strcat(out, item->id); strcat(out, "/"); strcat(out, f->path);
    return 1;
}
