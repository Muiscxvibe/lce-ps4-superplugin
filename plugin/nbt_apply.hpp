#pragma once
// Applies NBT editor protocol v2 edit lines ("/lce-nbt set|add|del|mk|aset|ren <t> <path> ...")
// to a Doc, with the same semantics as the server's LceNbtEdit.h applyEdit. Used by the
// local-world backend (local_nbt.cpp): the editor sends the same tx/edit/commit script it
// sends to an LCE server, and the backend replays it on a fresh native snapshot of the
// target before loading the result into the game object. Freestanding (no allocation).
// Host tested by nbt_editor_test.cpp (diff -> script -> apply == edited document).
#include "nbt_doc.hpp"

namespace lce_nbt_ed {

struct ApplyError { char text[96]; };

// Decimal text -> double: integer mantissa (up to 19 digits) scaled by an exact power of ten in
// long double, then rounded once to double (exact for every value the editor writes: %.17g).
inline bool parse_decimal(const char *s, double *out)
{
    const char *q = s; bool neg = false;
    if (*q == '-' || *q == '+') { neg = *q == '-'; ++q; }
    uint64_t m = 0; int digits = 0, exp10 = 0; bool any = false;
    for (; *q >= '0' && *q <= '9'; ++q) { any = true; if (digits < 19) { if (m || *q != '0') { m = m * 10 + (uint64_t)(*q - '0'); ++digits; } } else ++exp10; }
    if (*q == '.') { ++q; for (; *q >= '0' && *q <= '9'; ++q) { any = true; if (digits < 19) { if (m || *q != '0') { m = m * 10 + (uint64_t)(*q - '0'); ++digits; } --exp10; } } }
    if (!any) return false;
    if (*q == 'e' || *q == 'E') {
        ++q; bool en = false; if (*q == '-' || *q == '+') { en = *q == '-'; ++q; }
        if (*q < '0' || *q > '9') return false;
        int e = 0; for (; *q >= '0' && *q <= '9'; ++q) if (e < 100000) e = e * 10 + (*q - '0');
        exp10 += en ? -e : e;
    }
    if (*q) return false;
    long double v = (long double)m, p = 1;
    const int a = exp10 < 0 ? -exp10 : exp10;
    for (int i = 0; i < a && i < 5000; ++i) p *= 10;
    v = exp10 < 0 ? v / p : v * p;
    const double d = (double)v;
    if (d != d || d > 1.7976931348623157e308 || d < -1.7976931348623157e308) return false;
    *out = neg ? -d : d; return true;
}

class ScriptApplier {
public:
    explicit ScriptApplier(Doc &d) : d_(d) {}
    // One line (with or without the "/lce-nbt " prefix). tx/commit/abort/get are accepted and
    // ignored here (the caller handles them). False with err() on a bad or failing edit;
    // the document may then be partly changed (callers discard it).
    bool line(const char *l, size_t n)
    {
        p_ = l; e_ = l + n; err_.text[0] = 0;
        static const char pre[] = "/lce-nbt ";
        if ((size_t)(e_ - p_) >= sizeof pre - 1 && !memcmp(p_, pre, sizeof pre - 1)) p_ += sizeof pre - 1;
        char op[8]; if (!word(op, sizeof op)) return fail("empty line");
        if (!strcmp(op, "tx") || !strcmp(op, "commit") || !strcmp(op, "abort") || !strcmp(op, "get") || !strcmp(op, "undo")) return true;
        char target[48]; if (!word(target, sizeof target)) return fail("missing target");
        char path[1024]; if (!word(path, sizeof path)) return fail("missing path");
        Steps st; if (!parse_path(path, &st) || !st.n) return fail("bad path");
        const Step &last = st.s[st.n - 1];
        uint32_t parent = walk(st, st.n - 1);
        if (parent == kNil) return fail("path not found");
        if (!strcmp(op, "del")) return del(parent, last);
        if (!strcmp(op, "ren")) { char nk[256]; size_t nl; if (!word(nk, sizeof nk) || !unescape(nk, nk, &nl) || !nl) return fail("bad new key"); return ren(parent, last, nk, nl); }
        if (!strcmp(op, "mk")) { char k[4]; if (!word(k, sizeof k)) return fail("missing kind"); return mk(parent, last, k); }
        if (!strcmp(op, "aset")) { char st0[16]; if (!word(st0, sizeof st0)) return fail("missing start"); return aset(parent, last, st0); }
        if (strcmp(op, "set") && strcmp(op, "add")) return fail("unknown op");
        char type[4]; if (!word(type, sizeof type) || type[1]) return fail("bad type");
        while (p_ < e_ && *p_ == ' ') ++p_;
        Value v; if (!value(type[0], p_, (size_t)(e_ - p_), &v)) return fail("bad value");
        return !strcmp(op, "set") ? set(parent, last, v) : add(parent, last, v);
    }
    // Every line of a '\n' separated script.
    bool script(const char *s, size_t n)
    {
        size_t i = 0;
        while (i < n) {
            size_t j = i; while (j < n && s[j] != '\n') ++j;
            if (j > i && !line(s + i, j - i)) return false;
            i = j + 1;
        }
        return true;
    }
    const char *err() const { return err_.text; }

private:
    struct Step { bool index, append; uint32_t i; char key[128]; size_t len; };
    struct Steps { Step s[kMaxDepth]; unsigned n; };
    struct Value { char type; int64_t i; double d; uint8_t s[1024]; size_t len; };
    Doc &d_; const char *p_ = nullptr, *e_ = nullptr; ApplyError err_;
    // scratch for long string values (max one protocol line, 250 chars on the wire; editor lines can be longer)
    bool fail(const char *m) { snprintf(err_.text, sizeof err_.text, "%s", m); return false; }
    bool word(char *out, size_t cap)
    {
        while (p_ < e_ && *p_ == ' ') ++p_;
        size_t k = 0;
        while (p_ < e_ && *p_ != ' ') { if (k + 1 >= cap) return false; out[k++] = *p_++; }
        out[k] = 0; return k > 0;
    }
    static int hexv(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; }
    // %XX unescape in place-safe (out may equal in)
    static bool unescape(const char *in, char *out, size_t *len)
    {
        size_t o = 0;
        for (size_t i = 0; in[i]; ++i) {
            if (in[i] == '%') { const int h = hexv(in[i + 1]), l = h < 0 ? -1 : hexv(in[i + 2]); if (h < 0 || l < 0) return false; out[o++] = (char)(h * 16 + l); i += 2; }
            else if (plain_key_char((unsigned char)in[i])) out[o++] = in[i];
            else return false;
        }
        out[o] = 0; *len = o; return true;
    }
    bool parse_path(const char *p, Steps *st)
    {
        st->n = 0;
        if (!strcmp(p, "/") || !strcmp(p, ".")) return true;
        size_t i = 0;
        while (p[i]) {
            if (st->n >= kMaxDepth) return false;
            char raw[128]; size_t k = 0;
            while (p[i] && p[i] != '.' && p[i] != '[') { if (k + 1 >= sizeof raw) return false; raw[k++] = p[i++]; }
            raw[k] = 0;
            Step &s = st->s[st->n]; s.index = s.append = false; s.i = 0;
            if (!k || !unescape(raw, s.key, &s.len) || !s.len) return false;
            ++st->n;
            while (p[i] == '[') {
                if (st->n >= kMaxDepth) return false;
                Step &x = st->s[st->n]; x.index = true; x.append = false; x.i = 0; x.len = 0; x.key[0] = 0;
                ++i;
                if (p[i] == '+' && p[i + 1] == ']') { x.append = true; x.index = false; i += 2; }
                else { uint32_t v = 0; size_t dg = 0; while (p[i] >= '0' && p[i] <= '9') { v = v * 10 + (uint32_t)(p[i++] - '0'); if (++dg > 6) return false; } if (!dg || p[i] != ']') return false; ++i; x.i = v; }
                ++st->n;
            }
            if (p[i]) { if (p[i] != '.') return false; ++i; if (!p[i]) return false; }
        }
        return true;
    }
    uint32_t child(uint32_t n, const Step &s) const
    {
        if (s.append) return kNil;
        if (s.index) return d_.at(n).type == T_LIST ? d_.child_at(n, s.i) : kNil;
        return d_.at(n).type == T_COMPOUND ? d_.find_n(n, s.key, s.len) : kNil;
    }
    uint32_t walk(const Steps &st, unsigned n) const { uint32_t x = d_.root; for (unsigned i = 0; i < n && x != kNil; ++i) x = child(x, st.s[i]); return x; }
    static uint8_t type_of_letter(char c)
    {
        switch (c) { case 'b': case 'z': return T_BYTE; case 's': return T_SHORT; case 'i': return T_INT; case 'l': return T_LONG; case 'f': return T_FLOAT;
        case 'd': return T_DOUBLE; case 't': case 'T': return T_STRING; case 'c': return T_COMPOUND; case 'n': return T_LIST; case 'B': return T_BYTE_ARRAY; case 'I': return T_INT_ARRAY; }
        return T_END;
    }
    bool value(char type, const char *s, size_t n, Value *v)
    {
        v->type = type; v->i = 0; v->d = 0; v->len = 0;
        char b[64];
        if (type == 't') { if (n > sizeof v->s) return false; memcpy(v->s, s, n); v->len = n; return true; }
        if (type == 'T') {
            if (n == 1 && s[0] == '%') return true;
            size_t o = 0;
            for (size_t i = 0; i < n; ++i) {
                if (o >= sizeof v->s) return false;
                if (s[i] == '%') { if (i + 2 >= n) return false; const int h = hexv(s[i + 1]), l = h < 0 ? -1 : hexv(s[i + 2]); if (h < 0 || l < 0) return false; v->s[o++] = (uint8_t)(h * 16 + l); i += 2; }
                else if (plain_key_char((unsigned char)s[i])) v->s[o++] = (uint8_t)s[i];
                else return false;
            }
            v->len = o; return true;
        }
        if (!n || n >= sizeof b) return false;
        memcpy(b, s, n); b[n] = 0;
        if (type == 'z') { if (!strcmp(b, "true")) { v->i = 1; return true; } if (!strcmp(b, "false")) { v->i = 0; return true; } }
        char *end;
        if (type == 'f' || type == 'd') { if (!parse_decimal(b, &v->d)) return false; v->i = (int64_t)v->d; return true; }
        const long long x = Snbt::strtoll_(b, &end);
        if (end == b || *end) return false;
        v->i = x; v->d = (double)x;
        int64_t lo = INT64_MIN, hi = INT64_MAX;
        if (type == 'b') { lo = -128; hi = 255; } else if (type == 'z') { lo = 0; hi = 1; } else if (type == 's') { lo = -32768; hi = 32767; } else if (type == 'i') { lo = INT32_MIN; hi = INT32_MAX; }
        else if (type != 'l') return false;
        return x >= lo && x <= hi;
    }
    static bool integer_letter(char t) { return t == 'b' || t == 's' || t == 'i' || t == 'l' || t == 'z'; }
    // assignScalar: keeps the tag's own type
    bool assign(uint32_t n, const Value &v)
    {
        const uint8_t t = d_.at(n).type;
        const bool text = v.type == 't' || v.type == 'T', isint = integer_letter(v.type);
        if (is_integer(t)) {
            if (text || !isint) return fail("integer tag needs an integer");
            int64_t x = v.i; if (t == T_BYTE && x > 127 && x <= 255) x -= 256;   // the server takes 0-255 for bytes
            return d_.set_integer(n, x) || fail("value out of range");
        }
        if (is_real(t)) { if (text) return fail("number tag needs a number"); const double x = isint ? (double)v.i : v.d; if (t == T_FLOAT && (x > 3.4e38 || x < -3.4e38)) return fail("float out of range"); return d_.set_real(n, t == T_FLOAT ? (double)(float)x : x) || fail("bad number"); }
        if (t == T_STRING) { if (!text) return fail("string tag needs type t"); return d_.set_string(n, v.s, v.len) || fail("document full"); }
        return fail("target is not a scalar tag");
    }
    uint32_t make_scalar(uint32_t parent, const char *name, const Value &v)
    {
        const uint8_t t = type_of_letter(v.type);
        if (t == T_END || t == T_COMPOUND || t == T_LIST) { fail("bad type"); return kNil; }
        const uint32_t c = d_.add_child(parent, d_.at(parent).last, t, name);
        if (c == kNil) { fail(d_.at(parent).type == T_LIST ? "list holds a different tag type" : "cannot add here"); return kNil; }
        if (!assign(c, v)) { d_.remove(c); return kNil; }
        return c;
    }
    bool set(uint32_t parent, const Step &last, const Value &v)
    {
        if (last.append) return fail("use add or mk to append");
        if (last.index) {
            if (is_array(d_.at(parent).type)) {
                if (!integer_letter(v.type)) return fail("arrays hold integers");
                if (last.i >= d_.at(parent).v.b.len) return fail("index out of range");
                if (d_.at(parent).type == T_BYTE_ARRAY && (v.i < -128 || v.i > 255)) return fail("byte array values are -128..255");
                if (d_.at(parent).type == T_INT_ARRAY && (v.i < INT32_MIN || v.i > INT32_MAX)) return fail("int array values are 32-bit");
                d_.array_put(parent, last.i, v.i); ++d_.revision; return true;
            }
            const uint32_t el = child(parent, last);
            if (el == kNil) return fail("index out of range");
            return assign(el, v);
        }
        if (d_.at(parent).type != T_COMPOUND) return fail("parent is not a compound");
        const uint32_t ex = child(parent, last);
        if (ex != kNil) return assign(ex, v);
        char name[129]; memcpy(name, last.key, last.len); name[last.len] = 0;
        if (strlen(name) != last.len) return fail("bad key");
        return make_scalar(parent, name, v) != kNil;
    }
    bool add(uint32_t parent, const Step &last, const Value &v)
    {
        if (last.append || last.index) return fail("add needs a path that is a list");
        const uint32_t l = child(parent, last);
        if (l == kNil) return fail("add needs a path that is a list");
        if (is_array(d_.at(l).type)) {
            if (!integer_letter(v.type)) return fail("arrays hold integers");
            if (d_.at(l).type == T_BYTE_ARRAY && (v.i < -128 || v.i > 255)) return fail("byte array values are -128..255");
            return d_.array_splice(l, d_.at(l).v.b.len, 0, 1, v.i) || fail("array full");
        }
        if (d_.at(l).type != T_LIST) return fail("add needs a path that is a list");
        if (d_.at(l).count && d_.at(l).elem != type_of_letter(v.type)) return fail("list holds a different tag type");
        if (!d_.at(l).count) d_.set_list_type(l, type_of_letter(v.type));
        return make_scalar(l, nullptr, v) != kNil;
    }
    bool del(uint32_t parent, const Step &last)
    {
        if (last.append) return fail("bad path");
        if (last.index && is_array(d_.at(parent).type)) {
            if (last.i >= d_.at(parent).v.b.len) return fail("index out of range");
            return d_.array_splice(parent, last.i, 1, 0, 0) || fail("array");
        }
        const uint32_t n = child(parent, last);
        if (n == kNil) return fail(last.index ? "index out of range" : "no such tag");
        return d_.remove(n) || fail("cannot delete");
    }
    bool ren(uint32_t parent, const Step &last, const char *nk, size_t nl)
    {
        if (last.index || last.append || d_.at(parent).type != T_COMPOUND) return fail("rename needs a compound key");
        const uint32_t n = child(parent, last);
        if (n == kNil) return fail("no such tag");
        if (d_.name_is(n, nk, nl)) return true;
        if (d_.find_n(parent, nk, nl) != kNil) return fail("a tag with that name exists");
        return d_.set_name(n, nk, nl) || fail("document full");
    }
    bool mk(uint32_t parent, const Step &last, const char *k)
    {
        const uint8_t t = type_of_letter(k[0]);
        if (t != T_COMPOUND && t != T_LIST && t != T_BYTE_ARRAY && t != T_INT_ARRAY) return fail("kind must be c, B, I or n<type>");
        uint8_t et = T_END;
        if (t == T_LIST) { if (!k[1]) return fail("list needs an element type"); et = type_of_letter(k[1]); if (et == T_END) return fail("bad list element type"); }
        else if (k[1]) return fail("bad kind");
        if (last.append || last.index) {
            if (d_.at(parent).type != T_LIST) return fail("parent is not a list");
            if (d_.at(parent).count && d_.at(parent).elem != t) return fail("list holds a different tag type");
            uint32_t after = d_.at(parent).last; bool front = false;
            if (last.index) {
                const uint32_t old = d_.child_at(parent, last.i);
                if (old == kNil) return fail("index out of range");
                after = d_.at(old).prev; front = after == kNil; d_.remove(old);
            }
            if (!d_.at(parent).count) d_.set_list_type(parent, t);
            const uint32_t c = d_.add_child(parent, after, t, nullptr, et);
            if (c == kNil) return fail("cannot add here");
            if (front && d_.at(parent).first != c) { d_.unlink(c); d_.link_before(parent, d_.at(parent).first, c); }
            if (t == T_LIST) d_.set_list_type(c, et);
            return true;
        }
        if (d_.at(parent).type != T_COMPOUND) return fail("parent is not a compound");
        const uint32_t old = child(parent, last);
        if (old != kNil) d_.remove(old);
        char name[129]; memcpy(name, last.key, last.len); name[last.len] = 0;
        if (strlen(name) != last.len) return fail("bad key");
        const uint32_t c = d_.add_child(parent, d_.at(parent).last, t, name, et);
        if (c == kNil) return fail("cannot add here");
        if (t == T_LIST) d_.set_list_type(c, et);
        return true;
    }
    bool aset(uint32_t parent, const Step &last, const char *start)
    {
        if (last.append) return fail("bad path");
        const uint32_t a = child(parent, last);
        if (a == kNil || !is_array(d_.at(a).type)) return fail("aset needs a byte or int array");
        char *e; const long long st = Snbt::strtoll_(start, &e);
        if (e == start || *e || st < 0 || st > 100000) return fail("bad start index");
        const uint32_t len = d_.at(a).v.b.len;
        if ((uint64_t)st > len) return fail("start index out of range");
        while (p_ < e_ && *p_ == ' ') ++p_;
        // values: comma list up to the end of the line
        uint32_t idx = (uint32_t)st;
        const char *q = p_;
        if (q >= e_) return fail("values must be a comma list of integers");
        while (q < e_) {
            char b[24]; size_t k = 0;
            while (q < e_ && *q != ',') { if (k + 1 >= sizeof b) return fail("bad value"); b[k++] = *q++; }
            b[k] = 0; if (q < e_) ++q;
            const long long v = Snbt::strtoll_(b, &e);
            if (e == b || *e) return fail("values must be a comma list of integers");
            if (d_.at(a).type == T_BYTE_ARRAY && (v < -128 || v > 255)) return fail("byte array values are -128..255");
            if (d_.at(a).type == T_INT_ARRAY && (v < INT32_MIN || v > INT32_MAX)) return fail("int array values are 32-bit");
            if (idx >= d_.at(a).v.b.len && !d_.array_splice(a, d_.at(a).v.b.len, 0, idx + 1 - d_.at(a).v.b.len, 0)) return fail("array full");
            d_.array_put(a, idx++, v);
        }
        ++d_.revision;
        return true;
    }
};

}  // namespace lce_nbt_ed
