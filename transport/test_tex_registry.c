/* Host test: texture-pack descriptor (lce.tex) format/parse, PCK id probe, installed-packs scan
 * and the installer -> lce.tex -> scan round trip. Run with run_test_tex_registry.sh. */
#include "tex_registry.h"
#include "content_catalog.h"
#include "content_download.h"
#include "client_report.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails;
static void ck(int c, const char *m) { printf("%s %s\n", c ? "PASS" : "FAIL", m); if (!c) ++fails; }

/* ---- synthetic TexturePack.pck (metadata pack of a Java port) ---- */
static size_t put32(uint8_t *b, size_t at, uint32_t v) { for (int i = 0; i < 4; ++i) b[at + i] = (uint8_t)(v >> (8 * i)); return at + 4; }
static size_t put_named(uint8_t *b, size_t at, uint32_t kind, const char *s) {
    size_t n = strlen(s);
    at = put32(b, at, kind); at = put32(b, at, (uint32_t)n);
    for (size_t i = 0; i < n; ++i) { b[at++] = (uint8_t)s[i]; b[at++] = 0; }
    return put32(b, at, 0);
}
static size_t make_pck(uint8_t *b, const char *id, uint32_t version, size_t extra_payload) {
    size_t at = 0;
    at = put32(b, at, version); at = put32(b, at, 2);
    at = put_named(b, at, 0, "PACKID"); at = put_named(b, at, 1, "PACKVERSION");
    at = put32(b, at, 3);
    at = put32(b, at, 0); at = put_named(b, at, 4, "0");
    at = put32(b, at, 5); at = put_named(b, at, 6, "languages.loc");
    at = put32(b, at, (uint32_t)extra_payload); at = put_named(b, at, 5, "x16/x16Info.pck");
    at = put32(b, at, 1); at = put_named(b, at, 0, id);            /* record 0 params */
    at = put32(b, at, 0);                                          /* record 1 params */
    at += 5;                                                       /* record 1 data */
    at = put32(b, at, 0);
    for (size_t i = 0; i < extra_payload; ++i) b[at++] = (uint8_t)i;
    return at;
}

/* ---- POSIX adapters ---- */
static int list_dirs(void *ctx, const char *dir, void (*emit)(void *, const char *), void *ec) {
    (void)ctx; DIR *d = opendir(dir); struct dirent *x;
    if (!d) return 0;
    while ((x = readdir(d))) if (x->d_type == DT_DIR && x->d_name[0] != '.') emit(ec, x->d_name);
    closedir(d); return 1;
}
static long read_file(void *ctx, const char *p, char *b, size_t cap) {
    (void)ctx; FILE *f = fopen(p, "rb"); if (!f) return -1;
    size_t n = fread(b, 1, cap, f); fclose(f); return (long)n;
}
static int mk(void *c, const char *d) { (void)c; char t[512]; snprintf(t, sizeof t, "%s", d);
    for (char *p = t + 1; *p; p++) if (*p == '/') { *p = 0; mkdir(t, 0755); *p = '/'; }
    return (mkdir(t, 0755) == 0 || errno == EEXIST) ? 0 : -1; }
static int ow(void *c, const char *p) { (void)c; return open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644); }
static int wr(void *c, int fd, const void *p, size_t n) { (void)c; return write(fd, p, n) == (ssize_t)n ? 0 : -1; }
static int cl(void *c, int fd) { (void)c; return close(fd); }
static int rn(void *c, const char *a, const char *b) { (void)c; return rename(a, b); }
static int ul(void *c, const char *p) { (void)c; return unlink(p); }
static int exists(const char *p) { return access(p, F_OK) == 0; }
static void write_text(const char *path, const char *text) { FILE *f = fopen(path, "w"); fputs(text, f); fclose(f); }
static void write_bin(const char *path, const void *d, size_t n) { FILE *f = fopen(path, "wb"); fwrite(d, 1, n, f); fclose(f); }

static const char *H64 = "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff";
static const char *NAMES[4] = {"TexturePack.pck", "Data/x16Data.pck", "Data/media.arc", "languages.loc"};

static void desc_text(char *out, size_t cap, unsigned id, const char *name, const unsigned long sizes[4]) {
    size_t at = (size_t)snprintf(out, cap, "LCETEX1\nid %u\nname %s\n", id, name);
    for (int k = 0; k < 4; ++k) at += (size_t)snprintf(out + at, cap - at, "%lu %s %s\n", sizes[k], H64, NAMES[k]);
}
static int parse_text(const char *t) { lce_tex_entry e; return lce_tex_desc_parse(t, strlen(t), &e); }

/* install fixtures */
static uint8_t files[5][4096]; static size_t file_len[5];
static int fake_get(void *ctx, const char *url, uint64_t max, int (*sink)(void *, const uint8_t *, size_t), void *sc) {
    (void)ctx;
    const char *last = strrchr(url, '/') + 1;
    static const char *assets[5] = {"TexturePack.pck", "Data__x16Data.pck", "Data__media.arc", "languages.loc", "PORT-README.txt"};
    for (int i = 0; i < 5; ++i) if (!strcmp(last, assets[i])) return file_len[i] <= max && sink(sc, files[i], file_len[i]);
    return 0;
}
static void hexof(const uint8_t *d, size_t n, char *hex) { uint8_t dg[32]; lce_sha256(d, n, dg); for (int i = 0; i < 32; ++i) sprintf(hex + 2 * i, "%02x", dg[i]); }

int main(void) {
    static uint8_t pck[2048];
    uint32_t id = 99;

    /* ---- PCK prefix id ---- */
    size_t n = make_pck(pck, "2107", 3, 30);
    ck(lce_tex_pck_prefix_id(pck, n, &id) && id == 2107, "pck prefix id read");
    ck(lce_tex_pck_prefix_id(pck, 220, &id) && id == 2107, "only the header prefix is needed");
    int prefix_ok = 1;
    for (size_t k = 0; k < 140; ++k) if (lce_tex_pck_prefix_id(pck, k, &id) || id != 0) prefix_ok = 0;
    ck(prefix_ok, "truncated prefixes are refused");
    make_pck(pck, "2107", 4, 0); ck(!lce_tex_pck_prefix_id(pck, 300, &id), "wrong pck version refused");
    n = make_pck(pck, "21x7", 3, 0); ck(!lce_tex_pck_prefix_id(pck, n, &id), "non-digit id refused");
    n = make_pck(pck, "0", 3, 0); ck(!lce_tex_pck_prefix_id(pck, n, &id), "id 0 refused");
    n = make_pck(pck, "16777216", 3, 0); ck(!lce_tex_pck_prefix_id(pck, n, &id), "id above 24 bits refused");
    n = make_pck(pck, "16777215", 3, 0); ck(lce_tex_pck_prefix_id(pck, n, &id) && id == 16777215, "largest id accepted");
    n = make_pck(pck, "123456789", 3, 0); ck(!lce_tex_pck_prefix_id(pck, n, &id), "9-digit id refused");
    n = make_pck(pck, "2107", 3, 0); pck[8 + 0] ^= 0; pck[4] = 70; ck(!lce_tex_pck_prefix_id(pck, n, &id), "more than 64 mappings refused");
    ck(!lce_tex_pck_prefix_id(NULL, 10, &id) && !lce_tex_pck_prefix_id(pck, 10, NULL), "null arguments refused");
    {   /* the real converted pack (kept outside the repo; skipped if absent) */
        FILE *f = fopen("/home/codey/minecraft-ps4-research/texture-import/invictus-native-195/generated-invictus-2018-r1/TexturePack.pck", "rb");
        if (f) { uint8_t b[4096]; size_t m = fread(b, 1, sizeof b, f); fclose(f);
                 ck(lce_tex_pck_prefix_id(b, m, &id) && id == 2018, "real Invictus TexturePack.pck: id 2018"); }
    }

    /* ---- descriptor parse ---- */
    char t[1100];
    const unsigned long sz[4] = {7449, 749596, 981, 1897};
    desc_text(t, sizeof t, 2018, "Invictus", sz);
    lce_tex_entry e;
    ck(lce_tex_desc_parse(t, strlen(t), &e) && e.desc_id == 2018 && !strcmp(e.name, "Invictus") && e.size[0] == 7449 && e.size[1] == 749596 &&
       e.size[2] == 981 && e.size[3] == 1897 && e.sha256[2][1] == 0x11, "valid lce.tex parsed");
    desc_text(t, sizeof t, 0, "No Id", sz); ck(parse_text(t), "descriptor id 0 (take it from the pack) accepted");
    desc_text(t, sizeof t, 16777216, "X", sz); ck(!parse_text(t), "descriptor id above 24 bits rejected");
    { const unsigned long big[4] = {1048577, 1, 1, 1}; desc_text(t, sizeof t, 1, "X", big); ck(!parse_text(t), "oversize TexturePack.pck rejected"); }
    { const unsigned long big[4] = {1, 8388609, 1, 1}; desc_text(t, sizeof t, 1, "X", big); ck(!parse_text(t), "oversize x16Data.pck rejected"); }
    { const unsigned long big[4] = {1, 1, 4194305, 1}; desc_text(t, sizeof t, 1, "X", big); ck(!parse_text(t), "oversize media.arc rejected"); }
    { const unsigned long big[4] = {1, 1, 1, 65537}; desc_text(t, sizeof t, 1, "X", big); ck(!parse_text(t), "oversize languages.loc rejected"); }
    { const unsigned long zero[4] = {1, 0, 1, 1}; desc_text(t, sizeof t, 1, "X", zero); ck(!parse_text(t), "zero size rejected"); }
    desc_text(t, sizeof t, 1, "", sz); ck(!parse_text(t), "empty name rejected");
    desc_text(t, sizeof t, 1, "Tab\there", sz); ck(!parse_text(t), "control character in name rejected");
    { char longname[80]; memset(longname, 'a', 64); longname[64] = 0; desc_text(t, sizeof t, 1, longname, sz); ck(!parse_text(t), "64-char name rejected"); }
    desc_text(t, sizeof t, 1, "X", sz); strcat(t, "extra\n"); ck(!parse_text(t), "trailing data rejected");
    desc_text(t, sizeof t, 1, "X", sz); t[strlen(t) - 1] = 0; ck(!parse_text(t), "missing final newline rejected");
    desc_text(t, sizeof t, 1, "X", sz); { char *p = strstr(t, "Data/media.arc"); memcpy(p, "Data/medis.arc", 14); } ck(!parse_text(t), "wrong file name rejected");
    desc_text(t, sizeof t, 1, "X", sz); { char *p = strstr(t, H64); p[3] = 'G'; } ck(!parse_text(t), "bad hex rejected");
    desc_text(t, sizeof t, 1, "X", sz); { char *p = strstr(t, H64); p[0] = 'A'; } ck(!parse_text(t), "upper-case hex rejected");
    desc_text(t, sizeof t, 1, "X", sz); { char *p = strstr(t, " 7449"); (void)p; } ck(parse_text(t), "baseline still valid");
    ck(!lce_tex_desc_parse(t, 10, &e) && !lce_tex_desc_parse(NULL, 100, &e) && !lce_tex_desc_parse(t, 5000, &e), "short/null/long input rejected");
    desc_text(t, sizeof t, 1, "X", sz); t[0] = 'l'; ck(!parse_text(t), "wrong magic rejected");

    /* ---- paths ---- */
    memset(&e, 0, sizeof e); strcpy(e.slug, "unity-2112");
    char path[200];
    ck(lce_tex_entry_path(path, sizeof path, "/r", &e, 2) && !strcmp(path, "/r/unity-2112/Data/media.arc"), "entry path");
    ck(!lce_tex_entry_path(path, 10, "/r", &e, 2) && !lce_tex_entry_path(path, sizeof path, "/r", &e, 4), "entry path overflow / bad index");
    strcpy(e.slug, "Bad_Slug"); ck(!lce_tex_entry_path(path, sizeof path, "/r", &e, 0), "entry path refuses a bad slug");

    /* ---- folder scan ---- */
    char root[] = "/tmp/lce-tex-registry-test-XXXXXX";
    ck(mkdtemp(root) != NULL, "temp root");
    const struct { const char *slug; const char *id_in_pck; unsigned desc_id; int pck; int desc; } dirs[] = {
        {"pack-b", "2107", 0, 1, 1}, {"pack-a", "2105", 2105, 1, 1}, {"dup-of-2107", "2107", 0, 1, 1},
        {"builtin-2011", "2011", 0, 1, 1}, {"reserved-2001", "2001", 0, 1, 1}, {"id-mismatch", "2110", 2111, 1, 1},
        {"no-pck", "2120", 0, 0, 1}, {"no-desc", "2121", 0, 1, 0}, {"Bad_Slug", "2122", 0, 1, 1}, {"broken", "2123", 0, 1, 1}};
    for (size_t i = 0; i < sizeof dirs / sizeof dirs[0]; ++i) {
        char p[512]; snprintf(p, sizeof p, "%s/%s", root, dirs[i].slug); mkdir(p, 0755);
        if (dirs[i].pck) { size_t m = make_pck(pck, dirs[i].id_in_pck, 3, 20); snprintf(p, sizeof p, "%s/%s/TexturePack.pck", root, dirs[i].slug); write_bin(p, pck, m); }
        if (dirs[i].desc) {
            desc_text(t, sizeof t, dirs[i].desc_id, dirs[i].slug, sz);
            if (!strcmp(dirs[i].slug, "broken")) strcpy(t, "garbage\n");
            snprintf(p, sizeof p, "%s/%s/lce.tex", root, dirs[i].slug); write_text(p, t);
        }
    }
    lce_tex_fs fs = {list_dirs, read_file, NULL};
    lce_tex_entry out[LCE_TEX_REGISTRY_MAX];
    lce_tex_scan_stats st;
    const uint32_t reserved[] = {2001, 2002};
    size_t cnt = lce_tex_registry_scan(&fs, root, reserved, 2, out, LCE_TEX_REGISTRY_MAX, &st);
    ck(cnt == 3, "scan keeps the valid, unique, non-reserved packs");
    ck(cnt == 3 && out[0].dlc_id == 2011 && out[1].dlc_id == 2105 && out[2].dlc_id == 2107, "entries sorted by dlc id");
    ck(cnt == 3 && !strcmp(out[0].slug, "builtin-2011") && !strcmp(out[2].slug, "dup-of-2107"), "built-in ids are accepted; the first slug wins a duplicate id");
    ck(st.reserved == 1 && st.duplicate == 1 && st.invalid == 4 && st.full == 0, "scan statistics (a folder with an unusable name is ignored silently)");
    ck(cnt == 3 && out[1].desc_id == 2105 && out[1].size[1] == 749596 && out[1].sha256[3][15] == 0xff && !strcmp(out[1].name, "pack-a"), "descriptor fields carried into the entry");
    cnt = lce_tex_registry_scan(&fs, root, reserved, 2, out, 2, &st);
    ck(cnt <= 2 && st.full >= 1, "scan honours the output limit");
    cnt = lce_tex_registry_scan(&fs, "/nonexistent-root", NULL, 0, out, LCE_TEX_REGISTRY_MAX, NULL);
    ck(cnt == 0, "missing root scans as empty");
    ck(lce_tex_registry_scan(NULL, root, NULL, 0, out, 4, NULL) == 0 && lce_tex_registry_scan(&fs, root, NULL, 0, out, 0, NULL) == 0, "bad scan arguments");

    /* ---- installer -> lce.tex -> scan ---- */
    size_t pn = make_pck(files[0], "2107", 3, 40); file_len[0] = pn;
    for (int i = 1; i < 5; ++i) { file_len[i] = 300 + 100u * (unsigned)i; for (size_t k = 0; k < file_len[i]; ++k) files[i][k] = (uint8_t)(k * (unsigned)(i + 3)); }
    char hex[5][65];
    for (int i = 0; i < 5; ++i) hexof(files[i], file_len[i], hex[i]);
    static char cat[3000];
    snprintf(cat, sizeof cat,
        "{\"schema\":1,\"type\":\"texture-pack\",\"items\":[{\"id\":\"multipixel-2107\",\"name\":\"MultiPixel\",\"version\":\"1\",\"files\":["
        "{\"path\":\"Data/media.arc\",\"size\":%zu,\"sha256\":\"%s\"},{\"path\":\"Data/x16Data.pck\",\"size\":%zu,\"sha256\":\"%s\"},"
        "{\"path\":\"PORT-README.txt\",\"size\":%zu,\"sha256\":\"%s\"},{\"path\":\"TexturePack.pck\",\"size\":%zu,\"sha256\":\"%s\"},"
        "{\"path\":\"languages.loc\",\"size\":%zu,\"sha256\":\"%s\"}]},"
        "{\"id\":\"bare-bones-2001\",\"name\":\"Bare Bones\",\"version\":\"1\",\"files\":["
        "{\"path\":\"Data/media.arc\",\"size\":%zu,\"sha256\":\"%s\"},{\"path\":\"Data/x16Data.pck\",\"size\":%zu,\"sha256\":\"%s\"},"
        "{\"path\":\"TexturePack.pck\",\"size\":%zu,\"sha256\":\"%s\"}]}]}",
        file_len[2], hex[2], file_len[1], hex[1], file_len[4], hex[4], file_len[0], hex[0], file_len[3], hex[3],
        file_len[2], hex[2], file_len[1], hex[1], file_len[0], hex[0]);
    static lce_catalog c;
    ck(lce_catalog_parse(cat, strlen(cat), &c) && c.count == 2, "texture catalog parsed");
    lce_content_fs cfs = {mk, ow, wr, cl, rn, ul, NULL};
    char droot[] = "/tmp/lce-tex-install-test-XXXXXX";
    ck(mkdtemp(droot) != NULL, "temp install root");
    ck(lce_content_install_with(fake_get, NULL, &cfs, "https://x.test", "texture-packs", droot, &c.items[0]), "texture pack installed");
    snprintf(path, sizeof path, "%s/multipixel-2107/lce.tex", droot);
    ck(exists(path), "installer wrote lce.tex");
    snprintf(path, sizeof path, "%s/multipixel-2107/lce.tex.part", droot);
    ck(!exists(path), "no .part left behind");
    cnt = lce_tex_registry_scan(&fs, droot, reserved, 2, out, LCE_TEX_REGISTRY_MAX, &st);
    ck(cnt == 1 && out[0].dlc_id == 2107 && !strcmp(out[0].name, "MultiPixel") && !strcmp(out[0].slug, "multipixel-2107"), "installed pack is found by the startup scan");
    int fields = cnt == 1;
    uint8_t dg[32];
    for (unsigned k = 0; fields && k < 4; ++k) {
        static const int src[4] = {0, 1, 2, 3};
        lce_sha256(files[src[k]], file_len[src[k]], dg);
        if (out[0].size[k] != file_len[src[k]] || memcmp(out[0].sha256[k], dg, 32)) fields = 0;
    }
    ck(fields, "descriptor carries the verified catalog sizes and hashes of the four files");
    /* a pack without languages.loc (other layout) is a plain download: no descriptor */
    ck(lce_content_install_with(fake_get, NULL, &cfs, "https://x.test", "texture-packs", droot, &c.items[1]), "layout without languages.loc installed as download");
    snprintf(path, sizeof path, "%s/bare-bones-2001/lce.tex", droot);
    ck(!exists(path), "no descriptor for an unsupported layout");
    /* stale descriptor is removed on reinstall of such an item */
    write_text(path, "stale");
    ck(lce_content_install_with(fake_get, NULL, &cfs, "https://x.test", "texture-packs", droot, &c.items[1]) && !exists(path), "stale descriptor removed");
    /* a failed download leaves no descriptor: corrupt one file */
    files[1][5] ^= 1;
    snprintf(path, sizeof path, "%s/multipixel-2107/lce.tex", droot); ul(NULL, path);
    ck(!lce_content_install_with(fake_get, NULL, &cfs, "https://x.test", "texture-packs", droot, &c.items[0]) && !exists(path), "hash mismatch: install fails, nothing registered");
    files[1][5] ^= 1;
    /* format limits */
    static lce_catalog_item item; item = c.items[0];
    char buf[1100];
    ck(lce_tex_desc_format(&item, buf, sizeof buf) > 0, "format: valid item");
    ck(lce_tex_desc_format(&item, buf, 100) == 0, "format: buffer too small");
    item.files[1].size = 8u * 1024 * 1024 + 1; ck(lce_tex_desc_format(&item, buf, sizeof buf) == 0, "format: oversize file refused");
    item = c.items[0]; item.dlc_id = 2107; { size_t m = lce_tex_desc_format(&item, buf, sizeof buf); ck(m && strstr(buf, "id 2107\n") && lce_tex_desc_parse(buf, m, &e) && e.desc_id == 2107, "format: catalog dlc id carried"); }
    item = c.items[0]; item.name[0] = 0; ck(lce_tex_desc_format(&item, buf, sizeof buf) == 0, "format: empty name refused");
    ck(lce_tex_desc_format(NULL, buf, sizeof buf) == 0, "format: null item");

    char cmd[600]; snprintf(cmd, sizeof cmd, "rm -rf '%s' '%s'", root, droot); if (system(cmd)) {}
    printf("\n%d failures\n", fails);
    return fails != 0;
}
