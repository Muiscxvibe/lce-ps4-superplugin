/* Per-mod config store (API 1.8): /data/lce-mods/<modid>/config.
 *
 * Header-only so the loader core needs no new translation unit. The loader reads the file once in DISCOVER (before
 * natives and scripts run), parses it here and keeps the store read-only for the process lifetime, so lookups are safe
 * from any thread and any phase.
 *
 * Format: the TOML subset Forge 1.13+ configs use (tools/lce_modpack.py writes it; tools/autoport maps Forge specs):
 *   # comment                     whole-line or after a value
 *   [general]                     section; keys below it are "general.<key>" ([a.b] nests: "a.b.<key>")
 *   ore_rate = 8                  integer (decimal, optional sign, 0x hex)
 *   speed = 1.25                  number (decimal point or exponent)
 *   enabled = true                boolean, stored as integer 1 / 0
 *   greeting = "Hi \"there\""     basic string (escapes \" \\ \n \t), or 'literal' without escapes
 *   ores = ["a", "b"]             anything else (arrays, inline tables) is kept as raw text (a string)
 * Keys: [A-Za-z0-9_-] per part, the full dotted key <= 63 bytes. Strings <= 255 bytes. At most LCE_CFG_MAX_ENTRIES.
 * A bad line is skipped and counted (first_bad_line); the rest of the file still applies. A repeated key keeps the first
 * value. The store never fails a mod: a missing or broken file means the mod sees its defaults. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../include/lce_mod_api.h"

#define LCE_CFG_MAX_FILE 16384u
#define LCE_CFG_MAX_ENTRIES 128u
#define LCE_CFG_MAX_KEY 64u

struct lce_cfg_entry { char key[LCE_CFG_MAX_KEY]; lce_nbt_value v; };
struct lce_cfg_store {
    uint32_t count;
    uint32_t bad_lines;        /* lines skipped (syntax, key too long, value too long, table full) */
    uint32_t first_bad_line;   /* 1-based, 0 = none */
    uint32_t duplicates;       /* repeated keys (first value kept) */
    lce_cfg_entry e[LCE_CFG_MAX_ENTRIES];
};

namespace lce_cfg_detail {
inline bool key_ch(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-'; }
inline const char *skip_ws(const char *p, const char *e) { while (p < e && (*p == ' ' || *p == '\t')) ++p; return p; }
inline const char *rtrim(const char *b, const char *e) { while (e > b && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) --e; return e; }

/* Dotted key "a.b.c" (bare parts only). Returns the end, or NULL. Appends to out (cap LCE_CFG_MAX_KEY). */
inline const char *parse_key(const char *p, const char *e, char *out, size_t &len) {
    for (;;) {
        p = skip_ws(p, e);
        const char *s = p;
        while (p < e && key_ch(*p)) ++p;
        if (p == s) return nullptr;
        size_t n = (size_t)(p - s);
        if (len + n + 1 >= LCE_CFG_MAX_KEY) return nullptr;
        memcpy(out + len, s, n); len += n; out[len] = 0;
        p = skip_ws(p, e);
        if (p < e && *p == '.') { if (len + 2 >= LCE_CFG_MAX_KEY) return nullptr; out[len++] = '.'; out[len] = 0; ++p; continue; }
        return p;
    }
}

/* Strip a trailing "# comment" that is outside quotes. */
inline const char *value_end(const char *p, const char *e) {
    char q = 0;
    for (const char *c = p; c < e; ++c) {
        if (q) { if (q == '"' && *c == '\\' && c + 1 < e) { ++c; continue; } if (*c == q) q = 0; continue; }
        if (*c == '"' || *c == '\'') q = *c;
        else if (*c == '#') return rtrim(p, c);
    }
    return rtrim(p, e);
}

inline bool parse_number(const char *s, const char *e, lce_nbt_value &v) {
    size_t n = (size_t)(e - s);
    if (n == 0 || n > 40) return false;
    char buf[48]; memcpy(buf, s, n); buf[n] = 0;
    for (size_t i = 0; i < n; ++i) if (buf[i] == '_') return false;   /* keep it simple: no 1_000 */
    char *end = nullptr;
    bool is_float = false;
    const char *d = buf + (buf[0] == '+' || buf[0] == '-');
    bool hex = d[0] == '0' && (d[1] == 'x' || d[1] == 'X');
    if (!hex) for (size_t i = 0; i < n; ++i) if (buf[i] == '.' || buf[i] == 'e' || buf[i] == 'E') is_float = true;
    if (!(d[0] >= '0' && d[0] <= '9') && !(d[0] == '.' && d[1] >= '0' && d[1] <= '9')) return false;
    if (is_float) {
        double x = strtod(buf, &end);
        if (end != buf + n || x != x) return false;
        v.type = LCE_NBT_DOUBLE; v.d = x; return true;
    }
    /* own digit loop (no strtoll: the plugin link must not gain a dynamic import) */
    bool neg = buf[0] == '-';
    const char *c = d + (hex ? 2 : 0);
    if (!*c) return false;
    uint64_t acc = 0, base = hex ? 16 : 10, lim = neg ? (uint64_t)1 << 63 : ((uint64_t)1 << 63) - 1;
    for (; *c; ++c) {
        unsigned dv = (*c >= '0' && *c <= '9') ? (unsigned)(*c - '0') : (hex && *c >= 'a' && *c <= 'f') ? (unsigned)(*c - 'a' + 10)
                    : (hex && *c >= 'A' && *c <= 'F') ? (unsigned)(*c - 'A' + 10) : 99u;
        if (dv >= base || acc > (lim - dv) / base) return false;
        acc = acc * base + dv;
    }
    (void)end;
    v.type = LCE_NBT_INT; v.i = neg ? (int64_t)(0 - acc) : (int64_t)acc; return true;
}

inline bool parse_value(const char *p, const char *e, lce_nbt_value &v) {
    memset(&v, 0, sizeof v);
    if (p >= e) return false;
    if (*p == '"' || *p == '\'') {
        char q = *p++;
        size_t o = 0;
        while (p < e && *p != q) {
            char c = *p++;
            if (q == '"' && c == '\\') {
                if (p >= e) return false;
                char x = *p++;
                c = x == 'n' ? '\n' : x == 't' ? '\t' : x == '"' ? '"' : x == '\\' ? '\\' : 0;
                if (!c) return false;
            }
            if (o + 1 >= sizeof v.s) return false;
            v.s[o++] = c;
        }
        if (p >= e || p + 1 != e) return false;   /* closing quote must end the value */
        v.type = LCE_NBT_STRING; v.s[o] = 0; return true;
    }
    size_t n = (size_t)(e - p);
    if (n == 4 && !memcmp(p, "true", 4)) { v.type = LCE_NBT_INT; v.i = 1; return true; }
    if (n == 5 && !memcmp(p, "false", 5)) { v.type = LCE_NBT_INT; v.i = 0; return true; }
    if (parse_number(p, e, v)) return true;
    if (*p == '[' || *p == '{') {                 /* arrays / inline tables: raw text */
        if (n + 1 > sizeof v.s) return false;
        memcpy(v.s, p, n); v.s[n] = 0; v.type = LCE_NBT_STRING; return true;
    }
    return false;
}
}  // namespace lce_cfg_detail

inline const lce_cfg_entry *lce_cfg_find(const lce_cfg_store *c, const char *key) {
    if (!c || !key) return nullptr;
    for (uint32_t i = 0; i < c->count; ++i) if (!strcmp(c->e[i].key, key)) return &c->e[i];
    return nullptr;
}

/* Parses text into c (zeroed first). Always succeeds; problems are counted in bad_lines / duplicates. */
inline void lce_cfg_parse(lce_cfg_store *c, const char *text, size_t len) {
    using namespace lce_cfg_detail;
    memset(c, 0, sizeof *c);
    char section[LCE_CFG_MAX_KEY] = {0};
    size_t sec_len = 0;
    bool sec_ok = true;
    const char *p = text, *end = text + len;
    uint32_t line = 0;
    while (p < end) {
        const char *nl = (const char *)memchr(p, '\n', (size_t)(end - p));
        const char *le = nl ? nl : end;
        ++line;
        const char *s = skip_ws(p, le), *e = rtrim(s, le);
        p = nl ? nl + 1 : end;
        if (line == 1 && e - s >= 3 && (uint8_t)s[0] == 0xef && (uint8_t)s[1] == 0xbb && (uint8_t)s[2] == 0xbf) s += 3;  /* BOM */
        if (s == e || *s == '#') continue;
        bool bad = false;
        if (*s == '[') {
            char tmp[LCE_CFG_MAX_KEY] = {0}; size_t tl = 0;
            bool arr = s + 1 < e && s[1] == '[';
            const char *k = parse_key(s + 1 + arr, e, tmp, tl);
            if (k) { k = skip_ws(k, e); if (arr) { if (k < e && *k == ']') ++k; else k = nullptr; } }
            if (k && k < e && *k == ']') { k = skip_ws(k + 1, e); if (k < e && *k != '#') k = nullptr; } else k = nullptr;
            if (!k || arr) { bad = true; sec_ok = false; }      /* [[array of tables]] is not supported: skip its keys */
            else { memcpy(section, tmp, sizeof section); sec_len = tl; sec_ok = true; }
        } else {
            char key[LCE_CFG_MAX_KEY] = {0}; size_t kl = 0;
            if (sec_len) { memcpy(key, section, sec_len); key[sec_len] = '.'; kl = sec_len + 1; key[kl] = 0; }
            const char *k = sec_ok ? parse_key(s, e, key, kl) : nullptr;
            lce_nbt_value v;
            if (!k || k >= e || *k != '=') bad = true;
            else {
                const char *vs = skip_ws(k + 1, e), *ve = value_end(vs, e);
                if (!parse_value(vs, ve, v)) bad = true;
                else if (lce_cfg_find(c, key)) ++c->duplicates;
                else if (c->count >= LCE_CFG_MAX_ENTRIES) bad = true;
                else { lce_cfg_entry &en = c->e[c->count++]; memcpy(en.key, key, sizeof en.key); en.v = v; }
            }
        }
        if (bad) { ++c->bad_lines; if (!c->first_bad_line) c->first_bad_line = line; }
    }
}

/* Typed lookups with Forge-like fallback: missing, wrong type or out of range -> def. */
inline int64_t lce_cfg_int(const lce_cfg_store *c, const char *key, int64_t def, int64_t min, int64_t max) {
    const lce_cfg_entry *en = lce_cfg_find(c, key);
    if (!en) return def;
    int64_t x;
    if (en->v.type == LCE_NBT_INT) x = en->v.i;
    else if (en->v.type == LCE_NBT_DOUBLE && en->v.d >= -9.0e18 && en->v.d <= 9.0e18 && (double)(int64_t)en->v.d == en->v.d) x = (int64_t)en->v.d;
    else return def;
    return (min <= max && (x < min || x > max)) ? def : x;
}
inline double lce_cfg_double(const lce_cfg_store *c, const char *key, double def, double min, double max) {
    const lce_cfg_entry *en = lce_cfg_find(c, key);
    if (!en) return def;
    double x;
    if (en->v.type == LCE_NBT_DOUBLE) x = en->v.d;
    else if (en->v.type == LCE_NBT_INT) x = (double)en->v.i;
    else return def;
    return (min <= max && (x < min || x > max)) ? def : x;
}
inline const char *lce_cfg_string(const lce_cfg_store *c, const char *key, const char *def) {
    const lce_cfg_entry *en = lce_cfg_find(c, key);
    return en && en->v.type == LCE_NBT_STRING ? en->v.s : def;
}
