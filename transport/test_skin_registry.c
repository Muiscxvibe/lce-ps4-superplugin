/* Host test: lce.desc parsing, packs-folder scan, catalog "lce" block, and the
 * installer -> lce.desc -> scan round trip. Build:
 *   clang -std=c11 -D_DEFAULT_SOURCE -fsanitize=address,undefined -I. -I../third_party/monocypher \
 *     test_skin_registry.c skin_registry.c content_catalog.c content_download.c client_report.c \
 *     ../third_party/monocypher/monocypher.c ../third_party/monocypher/monocypher-ed25519.c
 */
#include "skin_registry.h"
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

static const char *H64 = "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff";
static int parse_line(const char *s) { lce_skin_entry e; return lce_skin_desc_parse(s, strlen(s), &e); }

/* POSIX adapters */
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

static uint8_t pack[3000];
static int fake_get(void *ctx, const char *url, uint64_t max, int (*sink)(void *, const uint8_t *, size_t), void *sc) {
    (void)ctx; (void)url;
    return sizeof pack <= max && sink(sc, pack, sizeof pack);
}

static void write_text(const char *path, const char *text) { FILE *f = fopen(path, "w"); fputs(text, f); fclose(f); }

int main(void) {
    char line[200];
    snprintf(line, sizeof line, "3001 9 12345 %s\n", H64);
    ck(parse_line(line), "valid lce.desc parsed");
    lce_skin_entry e; lce_skin_desc_parse(line, strlen(line), &e);
    ck(e.dlc_id == 3001 && e.skin_count == 9 && e.size == 12345 && e.sha256[1] == 0x11, "fields decoded");
    snprintf(line, sizeof line, "0 9 12345 %s\n", H64); ck(!parse_line(line), "dlc id 0 rejected");
    snprintf(line, sizeof line, "16777216 9 1 %s\n", H64); ck(!parse_line(line), "dlc id > 24 bits rejected");
    snprintf(line, sizeof line, "3001 257 1 %s\n", H64); ck(!parse_line(line), "more than 256 skins rejected");
    snprintf(line, sizeof line, "3001 9 99999999999 %s\n", H64); ck(!parse_line(line), "pack over 64 MiB rejected");
    snprintf(line, sizeof line, "3001 9 1 %s", H64); ck(!parse_line(line), "missing newline rejected");
    snprintf(line, sizeof line, "3001 9 1 %.63sZ\n", H64); ck(!parse_line(line), "bad hex rejected");
    snprintf(line, sizeof line, "3001  9 1 %s\n", H64); ck(!parse_line(line), "double space rejected");
    snprintf(line, sizeof line, "3001 9 1 %s\nextra", H64); ck(!parse_line(line), "trailing data rejected");

    /* folder scan */
    char root[] = "/tmp/lce-skin-registry-test-XXXXXX";
    ck(mkdtemp(root) != NULL, "temp root");
    const struct { const char *slug; const char *desc; } dirs[] = {
        {"pack-b-3002", "3002 8 100 "}, {"pack-a-3001", "3001 9 100 "}, {"dup-3002", "3002 8 100 "},
        {"Bad_Slug", "3003 8 100 "}, {"reserved-2026", "2026 7 100 "}, {"broken-3004", "garbage"}, {"nodesc-3005", NULL}};
    for (size_t i = 0; i < sizeof dirs / sizeof dirs[0]; ++i) {
        char p[512]; snprintf(p, sizeof p, "%s/%s", root, dirs[i].slug); mkdir(p, 0755);
        if (!dirs[i].desc) continue;
        char t[256]; snprintf(t, sizeof t, "%s%s\n", dirs[i].desc, strcmp(dirs[i].desc, "garbage") ? H64 : "");
        snprintf(p, sizeof p, "%s/%s/lce.desc", root, dirs[i].slug); write_text(p, t);
    }
    lce_skin_fs fs = {list_dirs, read_file, NULL};
    lce_skin_entry out[LCE_SKIN_REGISTRY_MAX];
    const uint32_t reserved[] = {2026};
    size_t n = lce_skin_registry_scan(&fs, root, reserved, 1, out, LCE_SKIN_REGISTRY_MAX);
    ck(n == 2, "scan keeps only valid, unique, non-reserved packs");
    ck(n == 2 && out[0].dlc_id == 3001 && out[1].dlc_id == 3002, "entries sorted by dlc id");
    ck(n == 2 && !strcmp(out[0].slug, "pack-a-3001"), "slug recorded");

    /* catalog lce block */
    const char *cat = "{\"schema\":1,\"type\":\"skin-pack\",\"items\":[{\"id\":\"new-3010\",\"name\":\"N\",\"version\":\"1\","
                      "\"lce\":{\"dlc_id\":3010,\"skin_count\":8},\"files\":[{\"path\":\"SkinPack.pck\",\"size\":3000,"
                      "\"sha256\":\"%s\"}]}]}";
    uint8_t digest[32]; char hex[65]; static char buf[1024];
    for (size_t i = 0; i < sizeof pack; ++i) pack[i] = (uint8_t)(i * 7 + 3);
    lce_sha256(pack, sizeof pack, digest);
    for (int i = 0; i < 32; ++i) sprintf(hex + 2 * i, "%02x", digest[i]);
    snprintf(buf, sizeof buf, cat, hex);
    static lce_catalog c;
    ck(lce_catalog_parse(buf, strlen(buf), &c) && c.items[0].dlc_id == 3010 && c.items[0].skin_count == 8, "catalog lce block parsed");
    char dup[1400];
    snprintf(dup, sizeof dup, "{\"schema\":1,\"items\":[{\"id\":\"a-1\",\"version\":\"1\",\"lce\":{\"dlc_id\":5},\"files\":[{\"path\":\"x\",\"size\":1,\"sha256\":\"%s\"}]},"
             "{\"id\":\"b-1\",\"version\":\"1\",\"lce\":{\"dlc_id\":5},\"files\":[{\"path\":\"x\",\"size\":1,\"sha256\":\"%s\"}]}]}", H64, H64);
    static lce_catalog c2;
    ck(!lce_catalog_parse(dup, strlen(dup), &c2), "two catalog items with the same dlc id rejected");

    /* install -> lce.desc -> scan round trip */
    lce_content_fs cfs = {mk, ow, wr, cl, rn, ul, NULL};
    ck(lce_content_install_with(fake_get, NULL, &cfs, "https://x.test", "skin-packs", root, &c.items[0]), "skin pack installed");
    n = lce_skin_registry_scan(&fs, root, reserved, 1, out, LCE_SKIN_REGISTRY_MAX);
    int found = 0;
    for (size_t i = 0; i < n; ++i) if (out[i].dlc_id == 3010 && out[i].size == sizeof pack && !memcmp(out[i].sha256, digest, 32) && out[i].skin_count == 8) found = 1;
    ck(found, "installed pack is picked up by the startup scan with matching size/hash");

    char cmd[600]; snprintf(cmd, sizeof cmd, "rm -rf '%s'", root); if (system(cmd)) {}
    printf("\n%d failures\n", fails);
    return fails != 0;
}
