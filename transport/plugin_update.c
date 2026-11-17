#include "plugin_update.h"
#include <string.h>

static uint64_t digits14(const char *s) {
    uint64_t v = 0;
    for (int i = 0; i < 14; ++i) {
        if (s[i] < '0' || s[i] > '9') return 0;
        v = v * 10 + (uint64_t)(s[i] - '0');
    }
    return v;
}

uint64_t lce_plugin_build_version(const char *build) {
    if (!build) return 0;
    size_t n = strlen(build);
    if (n < 15 || build[n - 15] != '-') return 0;
    return digits14(build + n - 14);
}

uint64_t lce_plugin_catalog_version(const char *version) {
    if (!version || strlen(version) != 14) return 0;
    return digits14(version);
}

static int safe_prx_path(const char *p) {
    size_t n = strlen(p);
    if (n < 6 || n > 200 || p[0] != '/' || strcmp(p + n - 4, ".prx")) return 0;
    for (size_t i = 0; i < n; ++i) {
        char c = p[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '/' || c == '_' || c == '-' || c == '.')) return 0;
        if (c == '.' && i && p[i - 1] == '.') return 0;
    }
    return 1;
}

/* Is [b,e) "<anything>/mc195_lce_session_<hex>.prx=true" (optional trailing \r)? */
static int active_session_line(const char *b, const char *e, const char **eq) {
    while (e > b && (e[-1] == '\r' || e[-1] == ' ')) --e;
    static const char tail[] = ".prx=true", tag[] = "/mc195_lce_session_";
    size_t tl = sizeof tail - 1, gl = sizeof tag - 1;
    if ((size_t)(e - b) < tl + gl + 1 || memcmp(e - tl, tail, tl)) return 0;
    const char *name_end = e - tl;                 /* points at ".prx" */
    const char *p = name_end;
    while (p > b && ((p[-1] >= '0' && p[-1] <= '9') || (p[-1] >= 'a' && p[-1] <= 'f'))) --p;
    if (p == name_end || (size_t)(p - b) < gl || memcmp(p - gl, tag, gl)) return 0;
    *eq = name_end + 4;                            /* the '=' */
    return 1;
}

int lce_plugin_ini_switch(const char *ini, size_t len, const char *new_path, char *out, size_t cap, size_t *out_len) {
    if (!ini || !new_path || !out || !safe_prx_path(new_path)) return 0;
    const char *found_b = 0, *found_e = 0, *eq = 0;
    int count = 0;
    for (const char *b = ini, *end = ini + len; b < end;) {
        const char *e = memchr(b, '\n', (size_t)(end - b));
        if (!e) e = end;
        const char *q;
        if (active_session_line(b, e, &q)) { ++count; found_b = b; found_e = e; eq = q; }
        b = e < end ? e + 1 : end;
    }
    (void)found_b; (void)found_e;
    if (count != 1) return 0;
    /* the new plugin must not already be listed as the active one */
    size_t np = strlen(new_path);
    size_t head = (size_t)(eq - ini);              /* up to '=' */
    const char *after = eq + 5;                    /* just past "=true" */
    size_t rest = len - (size_t)(after - ini);     /* keeps this line's own ending */
    const int crlf = after < ini + len && *after == '\r';
    static const char f[] = "=false", t[] = "=true";
    size_t need = head + (sizeof f - 1) + (crlf ? 2 : 1) + np + (sizeof t - 1) + rest;
    if (need > cap) return 0;
    size_t o = 0;
    memcpy(out + o, ini, head); o += head;
    memcpy(out + o, f, sizeof f - 1); o += sizeof f - 1;
    if (crlf) out[o++] = '\r';
    out[o++] = '\n';
    memcpy(out + o, new_path, np); o += np;
    memcpy(out + o, t, sizeof t - 1); o += sizeof t - 1;
    memcpy(out + o, after, rest); o += rest;
    if (out_len) *out_len = o;
    return 1;
}

int lce_plugin_final_path(const uint8_t sha256[32], char *out, size_t cap) {
    static const char pre[] = "/data/GoldHEN/plugins/mc195_lce_session_", hex[] = "0123456789abcdef";
    if (!sha256 || !out || cap < sizeof pre + 16 + 4) return 0;
    memcpy(out, pre, sizeof pre - 1);
    size_t o = sizeof pre - 1;
    for (int i = 0; i < 8; ++i) { out[o++] = hex[sha256[i] >> 4]; out[o++] = hex[sha256[i] & 15]; }
    memcpy(out + o, ".prx", 5);
    return 1;
}
