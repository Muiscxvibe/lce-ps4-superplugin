#pragma once
// NBT document model for the PS4 NBT editor (plugin side). Freestanding: no heap,
// no exceptions, fixed pools; host tested by nbt_editor_test.cpp.
//
//  * Doc: node pool + byte pool. parse()/serialize() use the Java NBT wire format
//    (big endian, named root compound, modified UTF-8 names/strings).
//  * Edits keep the tree valid (list element types, unique compound keys, limits).
//  * Paths: a node is addressed by child indices from the root (stable across
//    compaction and re-reads); proto_path() renders the server path syntax.
//  * diff_script(): minimal-ish set of server ops (LceDebugCommand.h protocol v2)
//    turning the original document into the edited one.
//  * snbt_build(): tiny SNBT subset used for templates ({k:1b,l:[..],s:"x"}).
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

namespace lce_nbt_ed {

enum Type : uint8_t { T_END = 0, T_BYTE, T_SHORT, T_INT, T_LONG, T_FLOAT, T_DOUBLE, T_BYTE_ARRAY, T_STRING, T_LIST, T_COMPOUND, T_INT_ARRAY };
constexpr uint32_t kNil = 0xffffffffu;
constexpr unsigned kMaxNodes = 12288;
constexpr size_t kPoolBytes = 320 * 1024;
constexpr size_t kMaxDocBytes = 256 * 1024;
constexpr unsigned kMaxDepth = 48;
constexpr unsigned kMaxList = 1000;
constexpr unsigned kMaxArray = 65536;
constexpr unsigned kMaxName = 255;

inline bool is_container(uint8_t t) { return t == T_LIST || t == T_COMPOUND; }
inline bool is_array(uint8_t t) { return t == T_BYTE_ARRAY || t == T_INT_ARRAY; }
inline bool is_integer(uint8_t t) { return t >= T_BYTE && t <= T_LONG; }
inline bool is_real(uint8_t t) { return t == T_FLOAT || t == T_DOUBLE; }
inline const char *type_name(uint8_t t)
{
    static const char *n[] = {"end", "byte", "short", "int", "long", "float", "double", "byte[]", "string", "list", "compound", "int[]"};
    return t <= T_INT_ARRAY ? n[t] : "?";
}
// Protocol letters (LceDebugCommand.h): scalar value / list element / mk kind.
inline char type_letter(uint8_t t)
{
    static const char l[] = "?bsilfdBtncI";
    return t <= T_INT_ARRAY ? l[t] : '?';
}
inline void int_range(uint8_t t, int64_t *lo, int64_t *hi)
{
    switch (t) {
    case T_BYTE: *lo = -128; *hi = 127; break;
    case T_SHORT: *lo = -32768; *hi = 32767; break;
    case T_INT: *lo = -2147483647LL - 1; *hi = 2147483647LL; break;
    default: *lo = INT64_MIN; *hi = INT64_MAX; break;
    }
}

struct Node {
    uint8_t type, elem;      // elem: list element type
    uint16_t name_len;
    uint32_t name_off;
    uint32_t parent, first, last, next, prev, count;
    union { int64_t i; double d; struct { uint32_t off, len; } b; } v;   // b.len: bytes (string) or elements (arrays)
};

struct Writer {
    uint8_t *p; size_t cap, n; bool ok;
    void put(const void *src, size_t k) { if (!ok || n + k > cap) { ok = false; return; } memcpy(p + n, src, k); n += k; }
    void u8(unsigned v) { uint8_t b = (uint8_t)v; put(&b, 1); }
    void be(uint64_t v, unsigned bytes) { uint8_t b[8]; for (unsigned i = 0; i < bytes; ++i) b[i] = (uint8_t)(v >> (8 * (bytes - 1 - i))); put(b, bytes); }
};
struct Reader {
    const uint8_t *p; size_t n, pos; bool ok;
    uint64_t be(unsigned bytes) { if (!ok || pos + bytes > n) { ok = false; return 0; } uint64_t v = 0; for (unsigned i = 0; i < bytes; ++i) v = v << 8 | p[pos + i]; pos += bytes; return v; }
    const uint8_t *take(size_t k) { if (!ok || k > n - pos) { ok = false; return nullptr; } const uint8_t *r = p + pos; pos += k; return r; }
};

inline uint64_t dbits(double d) { uint64_t u; memcpy(&u, &d, 8); return u; }
inline double bitsd(uint64_t u) { double d; memcpy(&d, &u, 8); return d; }
inline uint32_t fbits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
inline float bitsf(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }

inline uint32_t crc32(const uint8_t *p, size_t n)
{
    uint32_t c = 0xffffffffu;
    for (size_t i = 0; i < n; ++i) { c ^= p[i]; for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u))); }
    return ~c;
}

class Doc {
public:
    Node nodes[kMaxNodes];
    uint8_t pool[kPoolBytes];
    // No member initializers: a static Doc must stay in .bss (clear() before first use).
    uint32_t free_head, node_top, live;
    uint32_t pool_used;
    uint32_t root;
    uint32_t revision;

    void clear() { free_head = kNil; node_top = 0; live = 0; pool_used = 0; root = kNil; ++revision; }
    bool valid() const { return root != kNil; }
    const Node &at(uint32_t i) const { return nodes[i]; }
    Node &at(uint32_t i) { return nodes[i]; }
    const uint8_t *bytes(uint32_t off) const { return pool + off; }
    uint32_t free_nodes() const { return kMaxNodes - live; }
    size_t free_bytes() const { return kPoolBytes - pool_used; }

    // ---- allocation ----
    uint32_t alloc_node(uint8_t type)
    {
        uint32_t i;
        if (free_head != kNil) { i = free_head; free_head = nodes[i].next; }
        else if (node_top < kMaxNodes) i = node_top++;
        else return kNil;
        Node &n = nodes[i];
        memset(&n, 0, sizeof n);
        n.type = type; n.parent = n.first = n.last = n.next = n.prev = kNil;
        ++live;
        return i;
    }
    uint32_t alloc_bytes(const void *src, size_t len)
    {
        if (len > kPoolBytes - pool_used) return kNil;
        const uint32_t off = pool_used;
        if (src && len) memcpy(pool + off, src, len);
        else if (len) memset(pool + off, 0, len);
        pool_used += (uint32_t)len;
        return off;
    }
    void free_subtree(uint32_t i)
    {
        for (uint32_t c = nodes[i].first; c != kNil;) { const uint32_t nx = nodes[c].next; free_subtree(c); c = nx; }
        nodes[i].type = T_END; nodes[i].next = free_head; free_head = i; --live;
    }

    // ---- structure ----
    uint32_t child_at(uint32_t p, uint32_t index) const
    {
        uint32_t c = nodes[p].first;
        while (c != kNil && index--) c = nodes[c].next;
        return c;
    }
    uint32_t index_of(uint32_t n) const
    {
        uint32_t k = 0;
        for (uint32_t c = nodes[nodes[n].parent].first; c != kNil && c != n; c = nodes[c].next) ++k;
        return k;
    }
    bool name_is(uint32_t n, const char *s, size_t len) const { return nodes[n].name_len == len && !memcmp(pool + nodes[n].name_off, s, len); }
    bool name_eq(uint32_t n, const char *s) const { return name_is(n, s, strlen(s)); }
    uint32_t find(uint32_t compound, const char *name) const
    {
        if (compound == kNil || nodes[compound].type != T_COMPOUND) return kNil;
        const size_t len = strlen(name);
        for (uint32_t c = nodes[compound].first; c != kNil; c = nodes[c].next) if (name_is(c, name, len)) return c;
        return kNil;
    }
    uint32_t find_n(uint32_t compound, const char *name, size_t len) const
    {
        for (uint32_t c = nodes[compound].first; c != kNil; c = nodes[c].next) if (name_is(c, name, len)) return c;
        return kNil;
    }
    unsigned depth(uint32_t n) const { unsigned d = 0; while (n != root && n != kNil) { n = nodes[n].parent; ++d; } return d; }
    void link_after(uint32_t parent, uint32_t after, uint32_t c)   // after == kNil: at the end
    {
        Node &p = nodes[parent]; Node &n = nodes[c];
        n.parent = parent;
        if (after == kNil) { n.prev = p.last; n.next = kNil; if (p.last != kNil) nodes[p.last].next = c; else p.first = c; p.last = c; }
        else { n.prev = after; n.next = nodes[after].next; if (n.next != kNil) nodes[n.next].prev = c; else p.last = c; nodes[after].next = c; }
        ++p.count;
    }
    void link_before(uint32_t parent, uint32_t before, uint32_t c)
    {
        if (before == kNil) { link_after(parent, kNil, c); return; }
        if (nodes[before].prev == kNil) {
            Node &p = nodes[parent]; Node &n = nodes[c];
            n.parent = parent; n.prev = kNil; n.next = before; nodes[before].prev = c; p.first = c; ++p.count;
        } else link_after(parent, nodes[before].prev, c);
    }
    void unlink(uint32_t c)
    {
        Node &n = nodes[c]; Node &p = nodes[n.parent];
        if (n.prev != kNil) nodes[n.prev].next = n.next; else p.first = n.next;
        if (n.next != kNil) nodes[n.next].prev = n.prev; else p.last = n.prev;
        n.prev = n.next = kNil; --p.count;
    }

    // ---- parsing ----
    bool parse_payload(Reader &r, uint32_t n, unsigned depth)
    {
        if (depth > kMaxDepth) return false;
        Node &node = nodes[n];
        switch (node.type) {
        case T_BYTE: node.v.i = (int8_t)r.be(1); break;
        case T_SHORT: node.v.i = (int16_t)r.be(2); break;
        case T_INT: node.v.i = (int32_t)r.be(4); break;
        case T_LONG: node.v.i = (int64_t)r.be(8); break;
        case T_FLOAT: node.v.d = bitsf((uint32_t)r.be(4)); break;
        case T_DOUBLE: node.v.d = bitsd(r.be(8)); break;
        case T_STRING: {
            const uint32_t len = (uint32_t)r.be(2); const uint8_t *s = r.take(len);
            if (!r.ok) return false;
            const uint32_t off = alloc_bytes(s, len); if (off == kNil && len) return false;
            nodes[n].v.b.off = off == kNil ? 0 : off; nodes[n].v.b.len = len; break;
        }
        case T_BYTE_ARRAY: case T_INT_ARRAY: {
            const uint32_t cnt = (uint32_t)r.be(4);
            if (!r.ok || cnt > kMaxArray) return false;
            const size_t width = node.type == T_INT_ARRAY ? 4 : 1;
            const uint8_t *s = r.take((size_t)cnt * width);
            if (!r.ok) return false;
            const uint32_t off = alloc_bytes(s, (size_t)cnt * width); if (off == kNil && cnt) return false;
            nodes[n].v.b.off = off == kNil ? 0 : off; nodes[n].v.b.len = cnt; break;   // int arrays kept big endian
        }
        case T_LIST: {
            const uint8_t et = (uint8_t)r.be(1); const uint32_t cnt = (uint32_t)r.be(4);
            if (!r.ok || cnt > 65536 || et > T_INT_ARRAY || (cnt && et == T_END)) return false;
            nodes[n].elem = et;
            for (uint32_t i = 0; i < cnt; ++i) {
                const uint32_t c = alloc_node(et); if (c == kNil) return false;
                link_after(n, kNil, c);
                if (!parse_payload(r, c, depth + 1)) return false;
            }
            break;
        }
        case T_COMPOUND:
            for (;;) {
                const uint8_t t = (uint8_t)r.be(1);
                if (!r.ok) return false;
                if (t == T_END) break;
                if (t > T_INT_ARRAY) return false;
                const uint32_t c = alloc_node(t); if (c == kNil) return false;
                if (!read_name(r, c)) return false;
                link_after(n, kNil, c);
                if (!parse_payload(r, c, depth + 1)) return false;
            }
            break;
        default: return false;
        }
        return r.ok;
    }
    bool read_name(Reader &r, uint32_t c)
    {
        const uint32_t len = (uint32_t)r.be(2); const uint8_t *s = r.take(len);
        if (!r.ok) return false;
        const uint32_t off = alloc_bytes(s, len); if (off == kNil && len) return false;
        nodes[c].name_off = off == kNil ? 0 : off; nodes[c].name_len = (uint16_t)len;
        return true;
    }
    // Named root compound (what the server sends).
    bool parse(const uint8_t *p, size_t n)
    {
        clear();
        Reader r{p, n, 0, true};
        if (r.be(1) != T_COMPOUND) { clear(); return false; }
        const uint32_t rt = alloc_node(T_COMPOUND);
        if (!read_name(r, rt) || !parse_payload(r, rt, 0) || r.pos != n) { clear(); return false; }
        root = rt;
        return true;
    }
    // One named tag (type, name, payload) as a new child of `parent` (after `after`, or last).
    uint32_t parse_child(uint32_t parent, uint32_t after, const uint8_t *p, size_t n, bool named)
    {
        Reader r{p, n, 0, true};
        const uint8_t t = (uint8_t)r.be(1);
        if (!r.ok || t == T_END || t > T_INT_ARRAY) return kNil;
        const uint32_t c = alloc_node(t); if (c == kNil) return kNil;
        if (named) { if (!read_name(r, c)) { free_subtree(c); return kNil; } }
        if (!parse_payload(r, c, depth(parent) + 1) || r.pos != n) { free_subtree(c); return kNil; }
        if (after == kNil) link_after(parent, kNil, c); else link_after(parent, after, c);
        return c;
    }

    // ---- serialization ----
    void write_payload(Writer &w, uint32_t n) const
    {
        const Node &node = nodes[n];
        switch (node.type) {
        case T_BYTE: w.be((uint64_t)node.v.i, 1); break;
        case T_SHORT: w.be((uint64_t)node.v.i, 2); break;
        case T_INT: w.be((uint64_t)node.v.i, 4); break;
        case T_LONG: w.be((uint64_t)node.v.i, 8); break;
        case T_FLOAT: w.be(fbits((float)node.v.d), 4); break;
        case T_DOUBLE: w.be(dbits(node.v.d), 8); break;
        case T_STRING: w.be(node.v.b.len, 2); w.put(pool + node.v.b.off, node.v.b.len); break;
        case T_BYTE_ARRAY: w.be(node.v.b.len, 4); w.put(pool + node.v.b.off, node.v.b.len); break;
        case T_INT_ARRAY: w.be(node.v.b.len, 4); w.put(pool + node.v.b.off, (size_t)node.v.b.len * 4); break;
        case T_LIST:
            w.u8(node.count ? node.elem : (node.elem ? node.elem : (uint8_t)T_END)); w.be(node.count, 4);
            for (uint32_t c = node.first; c != kNil; c = nodes[c].next) write_payload(w, c);
            break;
        case T_COMPOUND:
            for (uint32_t c = node.first; c != kNil; c = nodes[c].next) { w.u8(nodes[c].type); w.be(nodes[c].name_len, 2); w.put(pool + nodes[c].name_off, nodes[c].name_len); write_payload(w, c); }
            w.u8(T_END);
            break;
        }
    }
    size_t serialize_node(uint32_t n, uint8_t *out, size_t cap, bool named) const
    {
        Writer w{out, cap, 0, true};
        w.u8(nodes[n].type);
        if (named) { w.be(nodes[n].name_len, 2); w.put(pool + nodes[n].name_off, nodes[n].name_len); }
        write_payload(w, n);
        return w.ok ? w.n : 0;
    }
    size_t serialize(uint8_t *out, size_t cap) const { return valid() ? serialize_node(root, out, cap, true) : 0; }

    // Re-packs both pools (after many edits). `scratch` holds kMaxDocBytes.
    bool compact(uint8_t *scratch)
    {
        const size_t n = serialize(scratch, kMaxDocBytes);
        if (!n) return false;
        const uint32_t rev = revision;
        const bool ok = parse(scratch, n);
        revision = rev + 1;
        return ok;
    }
    // Makes room for an edit; node ids are invalid afterwards if this returns true.
    bool reserve(uint8_t *scratch, unsigned want_nodes, size_t want_bytes, bool *compacted)
    {
        *compacted = false;
        if (free_nodes() >= want_nodes && free_bytes() >= want_bytes) return true;
        if (!compact(scratch)) return false;
        *compacted = true;
        return free_nodes() >= want_nodes && free_bytes() >= want_bytes;
    }

    // ---- values ----
    int64_t array_get(uint32_t n, uint32_t i) const
    {
        const Node &a = nodes[n];
        if (a.type == T_BYTE_ARRAY) return (int8_t)pool[a.v.b.off + i];
        const uint8_t *q = pool + a.v.b.off + (size_t)i * 4;
        return (int32_t)((uint32_t)q[0] << 24 | (uint32_t)q[1] << 16 | (uint32_t)q[2] << 8 | q[3]);
    }
    void array_put(uint32_t n, uint32_t i, int64_t v)
    {
        Node &a = nodes[n];
        if (a.type == T_BYTE_ARRAY) { pool[a.v.b.off + i] = (uint8_t)v; return; }
        uint8_t *q = pool + a.v.b.off + (size_t)i * 4;
        const uint32_t u = (uint32_t)v; q[0] = (uint8_t)(u >> 24); q[1] = (uint8_t)(u >> 16); q[2] = (uint8_t)(u >> 8); q[3] = (uint8_t)u;
    }
    // Resizes an array (copy-on-grow into the pool); new elements are `fill`. Insert at `at` shifts.
    bool array_splice(uint32_t n, uint32_t at, uint32_t remove, uint32_t insert, int64_t fill)
    {
        Node &a = nodes[n];
        const uint32_t len = a.v.b.len;
        if (at > len || remove > len - at) return false;
        const uint64_t nl = (uint64_t)len - remove + insert;
        if (nl > kMaxArray) return false;
        const size_t w = a.type == T_INT_ARRAY ? 4 : 1;
        const uint32_t off = alloc_bytes(nullptr, (size_t)nl * w);
        if (off == kNil && nl) return false;
        const uint32_t old = nodes[n].v.b.off;
        uint8_t *dst = pool + (off == kNil ? 0 : off);
        memcpy(dst, pool + old, (size_t)at * w);
        memcpy(dst + (size_t)(at + insert) * w, pool + old + (size_t)(at + remove) * w, (size_t)(len - at - remove) * w);
        nodes[n].v.b.off = off == kNil ? 0 : off; nodes[n].v.b.len = (uint32_t)nl;
        for (uint32_t i = 0; i < insert; ++i) array_put(n, at + i, fill);
        return true;
    }
    bool set_integer(uint32_t n, int64_t v)
    {
        int64_t lo, hi; int_range(nodes[n].type, &lo, &hi);
        if (!is_integer(nodes[n].type) || v < lo || v > hi) return false;
        nodes[n].v.i = v; ++revision; return true;
    }
    bool set_real(uint32_t n, double v)
    {
        if (!is_real(nodes[n].type) || v != v || v > 1.7e308 || v < -1.7e308) return false;
        if (nodes[n].type == T_FLOAT && (v > 3.4e38 || v < -3.4e38)) return false;
        nodes[n].v.d = nodes[n].type == T_FLOAT ? (double)(float)v : v; ++revision; return true;
    }
    bool set_string(uint32_t n, const uint8_t *s, size_t len)
    {
        if (nodes[n].type != T_STRING || len > 32767) return false;
        const uint32_t off = alloc_bytes(s, len); if (off == kNil && len) return false;
        nodes[n].v.b.off = off == kNil ? 0 : off; nodes[n].v.b.len = (uint32_t)len; ++revision; return true;
    }
    bool set_name(uint32_t n, const char *s, size_t len)
    {
        if (len > kMaxName) return false;
        const uint32_t p = nodes[n].parent;
        if (p == kNil || nodes[p].type != T_COMPOUND) return false;
        for (uint32_t c = nodes[p].first; c != kNil; c = nodes[c].next) if (c != n && name_is(c, s, len)) return false;
        const uint32_t off = alloc_bytes(s, len); if (off == kNil && len) return false;
        nodes[n].name_off = off == kNil ? 0 : off; nodes[n].name_len = (uint16_t)len; ++revision; return true;
    }

    // ---- structural edits ----
    // Adds a default-valued child. Compound parents need a unique name; list parents
    // must hold `type` (an empty list adopts it).
    uint32_t add_child(uint32_t parent, uint32_t after, uint8_t type, const char *name, uint8_t elem = T_END)
    {
        Node &p = nodes[parent];
        if (!is_container(p.type) || type == T_END || type > T_INT_ARRAY) return kNil;
        if (depth(parent) + 1 > kMaxDepth) return kNil;
        if (p.type == T_LIST) {
            if (p.count >= kMaxList) return kNil;
            if (p.count && p.elem != type) return kNil;
        } else {
            if (!name || find(parent, name) != kNil || strlen(name) > kMaxName) return kNil;
        }
        const uint32_t c = alloc_node(type); if (c == kNil) return kNil;
        if (nodes[parent].type == T_COMPOUND) {
            const size_t len = strlen(name);
            const uint32_t off = alloc_bytes(name, len); if (off == kNil && len) { free_subtree(c); return kNil; }
            nodes[c].name_off = off == kNil ? 0 : off; nodes[c].name_len = (uint16_t)len;
        } else nodes[parent].elem = type;
        nodes[c].elem = elem;
        link_after(parent, after, c);
        ++revision;
        return c;
    }
    bool remove(uint32_t n)
    {
        if (n == root || n == kNil) return false;
        unlink(n); free_subtree(n); ++revision; return true;
    }
    // Moves a list element (or compound child) one place up (-1) or down (+1).
    bool move(uint32_t n, int dir)
    {
        const uint32_t p = nodes[n].parent;
        if (p == kNil) return false;
        if (dir < 0) { const uint32_t pr = nodes[n].prev; if (pr == kNil) return false; unlink(n); link_before(p, pr, n); }
        else { const uint32_t nx = nodes[n].next; if (nx == kNil) return false; unlink(n); link_after(p, nx, n); }
        ++revision; return true;
    }
    // Empty list -> new element type.
    bool set_list_type(uint32_t n, uint8_t elem)
    {
        if (nodes[n].type != T_LIST || nodes[n].count || elem > T_INT_ARRAY) return false;
        nodes[n].elem = elem; ++revision; return true;
    }
    // Unique "base", "base_2", ... within a compound.
    void unique_name(uint32_t compound, const char *base, char *out, size_t cap) const
    {
        snprintf(out, cap, "%s", base);
        for (unsigned k = 2; find(compound, out) != kNil && k < 10000; ++k) snprintf(out, cap, "%s_%u", base, k);
    }
    // Inserts a copy of serialized `tag` (named form, from serialize_node) into parent.
    // Compound parents: name kept (made unique). List parents: type must match.
    uint32_t insert_serialized(uint32_t parent, uint32_t after, const uint8_t *tag, size_t n)
    {
        if (n < 3) return kNil;
        const uint8_t t = tag[0];
        if (nodes[parent].type == T_LIST) {
            if (nodes[parent].count && nodes[parent].elem != t) return kNil;
            if (nodes[parent].count >= kMaxList) return kNil;
            const size_t nl = (size_t)tag[1] << 8 | tag[2];
            if (3 + nl > n) return kNil;
            const uint32_t c = alloc_node(t); if (c == kNil) return kNil;
            Reader r{tag + 3 + nl, n - 3 - nl, 0, true};
            if (!parse_payload(r, c, depth(parent) + 1) || r.pos != r.n) { free_subtree(c); return kNil; }
            nodes[parent].elem = t;
            link_after(parent, after, c);
            ++revision;
            return c;
        }
        if (nodes[parent].type != T_COMPOUND) return kNil;
        const uint32_t c = parse_child(parent, after, tag, n, true);
        if (c == kNil) return kNil;
        // make the name unique
        char want[kMaxName + 1]; const size_t len = nodes[c].name_len < kMaxName ? nodes[c].name_len : kMaxName;
        memcpy(want, pool + nodes[c].name_off, len); want[len] = 0;
        bool clash = false;
        for (uint32_t o = nodes[parent].first; o != kNil; o = nodes[o].next) if (o != c && name_is(o, want, len)) clash = true;
        if (clash) {
            char buf[kMaxName + 8]; unsigned k = 2;
            for (;; ++k) { snprintf(buf, sizeof buf, "%.*s_%u", (int)(len > 240 ? 240 : len), want, k); bool used = false; for (uint32_t o = nodes[parent].first; o != kNil; o = nodes[o].next) if (o != c && name_eq(o, buf)) used = true; if (!used) break; }
            const size_t bl = strlen(buf); const uint32_t off = alloc_bytes(buf, bl);
            if (off == kNil) { unlink(c); free_subtree(c); return kNil; }
            nodes[c].name_off = off; nodes[c].name_len = (uint16_t)bl;
        }
        ++revision;
        return c;
    }
    // Replaces the value of `n` with serialized `tag` keeping n's name/position. Types may
    // differ only when the parent is a compound.
    uint32_t replace_serialized(uint32_t n, const uint8_t *tag, size_t len)
    {
        const uint32_t p = nodes[n].parent;
        if (p == kNil || len < 3) return kNil;
        if (nodes[p].type == T_LIST && tag[0] != nodes[n].type && nodes[p].count > 1) return kNil;
        char name[kMaxName + 1]; const size_t nl = nodes[n].name_len; memcpy(name, pool + nodes[n].name_off, nl); name[nl] = 0;
        const uint32_t prev = nodes[n].prev;
        unlink(n); free_subtree(n);
        uint32_t c;
        if (nodes[p].type == T_LIST) { if (!nodes[p].count) nodes[p].elem = tag[0]; c = insert_serialized(p, prev, tag, len); if (c != kNil && prev == kNil) { unlink(c); link_before(p, nodes[p].first, c); } }
        else {
            c = parse_child(p, prev, tag, len, true);
            if (c != kNil) { const uint32_t off = alloc_bytes(name, nl); nodes[c].name_off = off == kNil ? 0 : off; nodes[c].name_len = (uint16_t)nl; if (prev == kNil) { unlink(c); link_before(p, nodes[p].first, c); } }
        }
        ++revision;
        return c;
    }
};

// ---- paths ------------------------------------------------------------------------------
struct Path { uint16_t idx[kMaxDepth]; unsigned n; };
inline void path_of(const Doc &d, uint32_t node, Path *p)
{
    uint16_t tmp[kMaxDepth]; unsigned k = 0;
    while (node != d.root && node != kNil && k < kMaxDepth) { tmp[k++] = (uint16_t)d.index_of(node); node = d.at(node).parent; }
    p->n = k; for (unsigned i = 0; i < k; ++i) p->idx[i] = tmp[k - 1 - i];
}
// Follows the path as far as it exists; returns the deepest node reached (root at worst).
inline uint32_t node_at(const Doc &d, const Path &p, unsigned *reached = nullptr)
{
    uint32_t n = d.root; unsigned i = 0;
    for (; n != kNil && i < p.n; ++i) {
        if (!is_container(d.at(n).type)) break;
        const uint32_t c = d.child_at(n, p.idx[i]);
        if (c == kNil) break;
        n = c;
    }
    if (reached) *reached = i;
    return n;
}
// Name-based path (compound keys by name, list elements by index): survives a re-read
// from the server, whose compound key order is not stable.
struct KeyPath { unsigned n; struct Step { bool index; uint32_t i; uint16_t len; char name[64]; } s[kMaxDepth]; };
inline void key_path_of(const Doc &d, uint32_t node, KeyPath *kp)
{
    uint32_t chain[kMaxDepth]; unsigned k = 0;
    for (uint32_t n = node; n != d.root && n != kNil && k < kMaxDepth; n = d.at(n).parent) chain[k++] = n;
    kp->n = 0;
    for (unsigned i = k; i-- > 0;) {
        const uint32_t n = chain[i]; auto &st = kp->s[kp->n++];
        st.index = d.at(d.at(n).parent).type == T_LIST; st.i = d.index_of(n);
        st.len = d.at(n).name_len < sizeof st.name ? d.at(n).name_len : (uint16_t)sizeof st.name;
        memcpy(st.name, d.bytes(d.at(n).name_off), st.len);
    }
}
inline uint32_t node_at_key(const Doc &d, const KeyPath &kp, unsigned *reached)
{
    uint32_t n = d.root; unsigned i = 0;
    for (; i < kp.n; ++i) {
        const auto &st = kp.s[i]; uint32_t c = kNil;
        if (d.at(n).type == T_LIST && st.index) c = d.child_at(n, st.i);
        else if (d.at(n).type == T_COMPOUND && !st.index) c = d.find_n(n, st.name, st.len);
        if (c == kNil) break;
        n = c;
    }
    if (reached) *reached = i;
    return n;
}
inline bool plain_key_char(unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '+' || c == ':' || c == '-'; }
inline void esc_append(char *out, size_t cap, size_t *len, const uint8_t *s, size_t n, bool *ok)
{
    static const char hex[] = "0123456789ABCDEF";
    for (size_t i = 0; i < n; ++i) {
        if (plain_key_char(s[i])) { if (*len + 1 >= cap) { *ok = false; return; } out[(*len)++] = (char)s[i]; }
        else { if (*len + 3 >= cap) { *ok = false; return; } out[(*len)++] = '%'; out[(*len)++] = hex[s[i] >> 4]; out[(*len)++] = hex[s[i] & 15]; }
    }
    out[*len] = 0;
}
// Server path ("Inventory[3].tag.display.Name"; root = "/"). false if it does not fit.
inline bool proto_path(const Doc &d, uint32_t node, char *out, size_t cap, const char *suffix = "")
{
    uint32_t chain[kMaxDepth]; unsigned k = 0;
    for (uint32_t n = node; n != d.root && n != kNil && k < kMaxDepth; n = d.at(n).parent) chain[k++] = n;
    size_t len = 0; bool ok = cap > 1; if (ok) out[0] = 0;
    for (unsigned i = k; ok && i-- > 0;) {
        const uint32_t n = chain[i]; const uint32_t p = d.at(n).parent;
        if (d.at(p).type == T_LIST) { const int w = snprintf(out + len, cap - len, "[%u]", d.index_of(n)); if (w < 0 || (size_t)w >= cap - len) ok = false; else len += (size_t)w; }
        else {
            if (len) { if (len + 1 >= cap) { ok = false; break; } out[len++] = '.'; out[len] = 0; }
            esc_append(out, cap, &len, d.bytes(d.at(n).name_off), d.at(n).name_len, &ok);
        }
    }
    if (ok && !len) { if (cap < 2) return false; out[0] = '/'; out[1] = 0; len = 1; if (*suffix) len = 0, out[0] = 0; }
    if (ok && *suffix) { const size_t sl = strlen(suffix); if (len + sl >= cap) ok = false; else { memcpy(out + len, suffix, sl + 1); len += sl; } }
    return ok;
}

// ---- text helpers -----------------------------------------------------------------------
// Modified UTF-8 -> printable ASCII for the raster font ('\xa7' shown as '&', others '?').
inline size_t ascii_preview(const uint8_t *s, size_t n, char *out, size_t cap)
{
    size_t o = 0;
    for (size_t i = 0; i < n && o + 1 < cap;) {
        const uint8_t c = s[i];
        uint32_t cp; size_t k = 1;
        if (c < 0x80) cp = c;
        else if ((c >> 5) == 6 && i + 1 < n) { cp = (uint32_t)(c & 31) << 6 | (s[i + 1] & 63); k = 2; }
        else if ((c >> 4) == 14 && i + 2 < n) { cp = (uint32_t)(c & 15) << 12 | (uint32_t)(s[i + 1] & 63) << 6 | (s[i + 2] & 63); k = 3; }
        else cp = '?';
        i += k;
        out[o++] = cp == 0xA7 ? '&' : (cp >= 0x20 && cp <= 0x7e) ? (char)cp : '?';
    }
    out[o] = 0;
    return o;
}
// UTF-16 <-> modified UTF-8 (BMP; NUL encoded as C0 80).
inline size_t utf16_to_mutf8(const uint16_t *u, size_t n, uint8_t *out, size_t cap)
{
    size_t o = 0;
    for (size_t i = 0; i < n; ++i) {
        const uint16_t c = u[i];
        if (c && c < 0x80) { if (o + 1 > cap) return 0; out[o++] = (uint8_t)c; }
        else if (c < 0x800) { if (o + 2 > cap) return 0; out[o++] = (uint8_t)(0xC0 | c >> 6); out[o++] = (uint8_t)(0x80 | (c & 63)); }
        else { if (o + 3 > cap) return 0; out[o++] = (uint8_t)(0xE0 | c >> 12); out[o++] = (uint8_t)(0x80 | (c >> 6 & 63)); out[o++] = (uint8_t)(0x80 | (c & 63)); }
    }
    return o;
}
inline size_t mutf8_to_utf16(const uint8_t *s, size_t n, uint16_t *out, size_t cap)
{
    size_t o = 0;
    for (size_t i = 0; i < n && o < cap;) {
        const uint8_t c = s[i];
        if (c < 0x80) { out[o++] = c; i += 1; }
        else if ((c >> 5) == 6 && i + 1 < n) { out[o++] = (uint16_t)((c & 31) << 6 | (s[i + 1] & 63)); i += 2; }
        else if ((c >> 4) == 14 && i + 2 < n) { out[o++] = (uint16_t)((c & 15) << 12 | (s[i + 1] & 63) << 6 | (s[i + 2] & 63)); i += 3; }
        else { out[o++] = '?'; i += 1; }
    }
    return o;
}
// Modified UTF-8 -> standard UTF-8 (server 'T' values take UTF-8).
inline size_t mutf8_to_utf8(const uint8_t *s, size_t n, uint8_t *out, size_t cap)
{
    size_t o = 0;
    for (size_t i = 0; i < n; ++i) {
        if (s[i] == 0xC0 && i + 1 < n && s[i + 1] == 0x80) { if (o + 1 > cap) return (size_t)-1; out[o++] = 0; ++i; continue; }
        if (o + 1 > cap) return (size_t)-1;
        out[o++] = s[i];
    }
    return o;
}

// Number formatting used for previews and the protocol (round-trips through strtod).
inline void fmt_number(const Node &n, char *out, size_t cap)
{
    if (is_integer(n.type)) snprintf(out, cap, "%lld", (long long)n.v.i);
    else if (n.type == T_FLOAT) snprintf(out, cap, "%.9g", (double)(float)n.v.d);
    else snprintf(out, cap, "%.17g", n.v.d);
}
inline void fmt_short_real(double v, char *out, size_t cap)
{
    snprintf(out, cap, "%.4g", v);
    // prefer fixed point for moderate values
    if (v > -1e6 && v < 1e6) { char b[40]; snprintf(b, sizeof b, "%.3f", v); size_t l = strlen(b); while (l > 1 && b[l - 1] == '0') b[--l] = 0; if (l && b[l - 1] == '.') b[--l] = 0; snprintf(out, cap, "%s", b); }
}

// ---- script output (diff) ---------------------------------------------------------------
// Lines are "/lce-nbt <op> ..." separated by '\n'.
struct Script {
    char *buf; size_t cap, len; unsigned lines; bool ok; char error[96];
    void fail(const char *why) { if (ok) { ok = false; snprintf(error, sizeof error, "%s", why); } }
    void line(const char *fmt, ...) __attribute__((format(printf, 2, 3)))
    {
        if (!ok) return;
        char tmp[512];
        __builtin_va_list ap; __builtin_va_start(ap, fmt); const int n = vsnprintf(tmp, sizeof tmp, fmt, ap); __builtin_va_end(ap);
        if (n <= 0 || n > 250) { fail("an edit is too long for one request (deep path or long text)"); return; }
        if (len + (size_t)n + 1 >= cap) { fail("too many edits for one apply"); return; }
        memcpy(buf + len, tmp, (size_t)n); len += (size_t)n; buf[len++] = '\n'; buf[len] = 0; ++lines;
    }
};
inline bool needs_escape_text(const uint8_t *s, size_t n)
{
    if (!n || s[0] == ' ' || s[n - 1] == ' ') return true;
    for (size_t i = 0; i < n; ++i) if (s[i] < 0x20 || s[i] > 0x7e || s[i] == '%') return true;
    return false;
}
// "<type> <value>" for a scalar node.
inline bool scalar_arg(const Doc &d, uint32_t n, char *out, size_t cap)
{
    const Node &x = d.at(n);
    if (x.type == T_STRING) {
        const uint8_t *s = d.bytes(x.v.b.off); const size_t len = x.v.b.len;
        if (!needs_escape_text(s, len)) { if (len + 3 > cap) return false; out[0] = 't'; out[1] = ' '; memcpy(out + 2, s, len); out[len + 2] = 0; return true; }
        static uint8_t utf8[1024];
        const size_t ul = mutf8_to_utf8(s, len, utf8, sizeof utf8);
        if (ul == (size_t)-1) return false;
        size_t ol = 2; bool ok = true; out[0] = 'T'; out[1] = ' '; out[2] = 0;
        if (!ul) { if (cap < 4) return false; out[2] = '%'; out[3] = 0; return true; }
        esc_append(out, cap, &ol, utf8, ul, &ok);
        return ok;
    }
    char num[48]; fmt_number(x, num, sizeof num);
    return snprintf(out, cap, "%c %s", type_letter(x.type), num) < (int)cap;
}

class Differ {
public:
    const Doc &o, &w; const char *target; Script &s;
    Differ(const Doc &orig, const Doc &work, const char *t, Script &sc) : o(orig), w(work), target(t), s(sc) {}
    void array_fill(uint32_t n, const char *path, uint32_t from)
    {
        const Node &a = w.at(n);
        for (uint32_t i = from; i < a.v.b.len && s.ok;) {
            char vals[200]; size_t vl = 0; const uint32_t start = i;
            while (i < a.v.b.len) {
                char one[16]; const int k = snprintf(one, sizeof one, "%s%lld", vl ? "," : "", (long long)w.array_get(n, i));
                if (vl + (size_t)k >= 150) break;
                memcpy(vals + vl, one, (size_t)k); vl += (size_t)k; ++i;
            }
            vals[vl] = 0;
            s.line("/lce-nbt aset %s %s %u %s", target, path, start, vals);
        }
    }
    // Creates work node n at `path` (key path, or list path with [+] when append).
    void build(uint32_t n, const char *path, bool append, const char *list_path)
    {
        if (!s.ok) return;
        const Node &x = w.at(n);
        char p[300];
        if (!is_container(x.type) && !is_array(x.type)) {
            char arg[260]; if (!scalar_arg(w, n, arg, sizeof arg)) { s.fail("a value is too long to send"); return; }
            if (append) s.line("/lce-nbt add %s %s %s", target, list_path, arg);
            else s.line("/lce-nbt set %s %s %s", target, path, arg);
            return;
        }
        char kind[4];
        if (x.type == T_LIST) snprintf(kind, sizeof kind, "n%c", type_letter(x.count ? x.elem : (x.elem ? x.elem : (uint8_t)T_BYTE)));
        else snprintf(kind, sizeof kind, "%c", type_letter(x.type));
        if (append) { snprintf(p, sizeof p, "%s[+]", list_path); s.line("/lce-nbt mk %s %s %s", target, p, kind); }
        else s.line("/lce-nbt mk %s %s %s", target, path, kind);
        if (is_array(x.type)) { array_fill(n, path, 0); return; }
        fill_children(n, path);
    }
    void fill_children(uint32_t n, const char *path)
    {
        const Node &x = w.at(n);
        unsigned idx = 0;
        for (uint32_t c = x.first; c != kNil && s.ok; c = w.at(c).next, ++idx) {
            char cp[300];
            if (x.type == T_LIST) { snprintf(cp, sizeof cp, "%s[%u]", path, idx); build(c, cp, true, path); }
            else { if (!child_path(w, c, path, cp, sizeof cp)) { s.fail("path too long"); return; } build(c, cp, false, nullptr); }
        }
    }
    static bool child_path(const Doc &d, uint32_t c, const char *parent_path, char *out, size_t cap)
    {
        size_t len = 0; bool ok = true;
        if (strcmp(parent_path, "/")) { len = strlen(parent_path); if (len + 2 >= cap) return false; memcpy(out, parent_path, len); out[len++] = '.'; }
        out[len] = 0;
        esc_append(out, cap, &len, d.bytes(d.at(c).name_off), d.at(c).name_len, &ok);
        return ok;
    }
    bool same_scalar(uint32_t a, uint32_t b) const
    {
        const Node &x = o.at(a), &y = w.at(b);
        if (x.type != y.type) return false;
        if (is_integer(x.type)) return x.v.i == y.v.i;
        if (is_real(x.type)) return dbits(x.v.d) == dbits(y.v.d);
        if (x.type == T_STRING || x.type == T_BYTE_ARRAY) return x.v.b.len == y.v.b.len && !memcmp(o.bytes(x.v.b.off), w.bytes(y.v.b.off), x.v.b.len);
        if (x.type == T_INT_ARRAY) return x.v.b.len == y.v.b.len && !memcmp(o.bytes(x.v.b.off), w.bytes(y.v.b.off), (size_t)x.v.b.len * 4);
        return false;
    }
    // Replace in place (same key / index).
    void replace(uint32_t b, const char *path, bool in_list)
    {
        const Node &y = w.at(b);
        if (!is_container(y.type) && !is_array(y.type)) {
            if (in_list) { char arg[260]; if (!scalar_arg(w, b, arg, sizeof arg)) { s.fail("a value is too long to send"); return; } s.line("/lce-nbt set %s %s %s", target, path, arg); return; }
            s.line("/lce-nbt del %s %s", target, path);
        }
        build(b, path, false, nullptr);
    }
    void diff(uint32_t a, uint32_t b, const char *path, bool in_list)
    {
        if (!s.ok) return;
        const Node &x = o.at(a), &y = w.at(b);
        if (x.type != y.type) { replace(b, path, in_list); return; }
        if (!is_container(x.type)) {
            if (same_scalar(a, b)) return;
            if (is_array(x.type)) {
                if (x.v.b.len == y.v.b.len) {
                    uint32_t first = 0; while (first < x.v.b.len && o.array_get(a, first) == w.array_get(b, first)) ++first;
                    array_fill(b, path, first);
                } else build(b, path, false, nullptr);
                return;
            }
            char arg[260]; if (!scalar_arg(w, b, arg, sizeof arg)) { s.fail("a value is too long to send"); return; }
            s.line("/lce-nbt set %s %s %s", target, path, arg);
            return;
        }
        if (x.type == T_COMPOUND) {
            for (uint32_t c = x.first; c != kNil && s.ok; c = o.at(c).next)
                if (w.find_n(b, (const char *)o.bytes(o.at(c).name_off), o.at(c).name_len) == kNil) {
                    char cp[300]; if (!child_path(o, c, path, cp, sizeof cp)) { s.fail("path too long"); return; }
                    s.line("/lce-nbt del %s %s", target, cp);
                }
            for (uint32_t c = y.first; c != kNil && s.ok; c = w.at(c).next) {
                char cp[300]; if (!child_path(w, c, path, cp, sizeof cp)) { s.fail("path too long"); return; }
                const uint32_t oc = o.find_n(a, (const char *)w.bytes(w.at(c).name_off), w.at(c).name_len);
                if (oc == kNil) build(c, cp, false, nullptr); else diff(oc, c, cp, false);
            }
            return;
        }
        // lists
        const uint8_t ot = x.count ? x.elem : (uint8_t)T_END, wt = y.count ? y.elem : (uint8_t)T_END;
        if (ot != T_END && wt != T_END && ot != wt) { build(b, path, false, nullptr); return; }
        if (!strcmp(path, "/")) { s.fail("root is not a list"); return; }
        uint32_t ca = x.first, cb = y.first; unsigned i = 0;
        for (; ca != kNil && cb != kNil && s.ok; ca = o.at(ca).next, cb = w.at(cb).next, ++i) {
            char cp[300]; snprintf(cp, sizeof cp, "%s[%u]", path, i);
            diff(ca, cb, cp, true);
        }
        if (ca != kNil) { // work list is shorter: delete from the end
            const unsigned keep = i; unsigned total = x.count;
            for (unsigned k = total; k-- > keep && s.ok;) s.line("/lce-nbt del %s %s[%u]", target, path, k);
        }
        for (; cb != kNil && s.ok; cb = w.at(cb).next, ++i) { char cp[300]; snprintf(cp, sizeof cp, "%s[%u]", path, i); build(cb, cp, true, path); }
    }
};
// Appends the ops that turn `orig` into `work` to the script. Returns false (with s.error) on failure.
inline bool diff_script(const Doc &orig, const Doc &work, const char *target, Script &s)
{
    if (!orig.valid() || !work.valid()) { s.fail("no document"); return false; }
    Differ df(orig, work, target, s);
    df.diff(orig.root, work.root, "/", false);
    return s.ok;
}

// ---- SNBT subset (templates and tests) -----------------------------------------------------
// value  := compound | list | string | number
// compound := '{' [key ':' value (',' key ':' value)*] '}'      key: [A-Za-z0-9_.+-]+ or "quoted"
// list   := '[' [type-hint ';'] value (',' value)* ']'  ("[;b]" = empty list of bytes)  ; "[B;1,2]" / "[I;1,2]" arrays
// number := -?digits[.digits][b|s|L|f|d]   (no suffix: int, or double if it has a '.')
class Snbt {
public:
    Doc &d; const char *p; bool ok = true;
    Snbt(Doc &doc, const char *src) : d(doc), p(src) {}
    void ws() { while (*p == ' ') ++p; }
    bool key(char *out, size_t cap)
    {
        ws(); size_t n = 0;
        if (*p == '"') { ++p; while (*p && *p != '"' && n + 1 < cap) out[n++] = *p++; if (*p != '"') return false; ++p; }
        else while (((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_' || *p == '.' || *p == '+' || *p == '-') && n + 1 < cap) out[n++] = *p++;
        out[n] = 0; return n > 0;
    }
    // Parses a value as child of parent (compound: named `name`; list: element).
    bool value(uint32_t parent, const char *name)
    {
        ws();
        if (*p == '{') {
            ++p; const uint32_t c = d.add_child(parent, kNil, T_COMPOUND, name); if (c == kNil) return false;
            ws(); if (*p == '}') { ++p; return true; }
            for (;;) {
                char k[64]; if (!key(k, sizeof k)) return false; ws(); if (*p++ != ':') return false;
                if (!value(c, k)) return false;
                ws(); if (*p == ',') { ++p; continue; } if (*p == '}') { ++p; return true; } return false;
            }
        }
        if (*p == '[') {
            ++p; ws();
            if ((p[0] == 'B' || p[0] == 'I') && p[1] == ';') {
                const uint8_t t = p[0] == 'B' ? T_BYTE_ARRAY : T_INT_ARRAY; p += 2;
                const uint32_t c = d.add_child(parent, kNil, t, name); if (c == kNil) return false;
                ws(); if (*p == ']') { ++p; return true; }
                for (;;) { ws(); char *e; long long v = strtoll_(p, &e); if (e == p) return false; p = e; if (*p == 'b') ++p;
                    if (!d.array_splice(c, d.at(c).v.b.len, 0, 1, v)) return false;
                    ws();
                    if (*p == ',') { ++p; continue; }
                    if (*p == ']') { ++p; return true; }
                    return false; }
            }
            const uint32_t c = d.add_child(parent, kNil, T_LIST, name); if (c == kNil) return false;
            if (*p == ';') { ++p; uint8_t et = hint(*p++); if (!et) return false; d.set_list_type(c, et); ws(); if (*p++ != ']') return false; return true; }
            if (*p == ']') { ++p; return true; }
            for (;;) { if (!value(c, nullptr)) return false; ws(); if (*p == ',') { ++p; continue; } if (*p == ']') { ++p; return true; } return false; }
        }
        if (*p == '"') {
            ++p; const char *s = p; while (*p && *p != '"') ++p; if (*p != '"') return false;
            const uint32_t c = d.add_child(parent, kNil, T_STRING, name); if (c == kNil) return false;
            if (!d.set_string(c, (const uint8_t *)s, (size_t)(p - s))) return false;
            ++p; return true;
        }
        char *e; const char *start = p; const long long iv = strtoll_(p, &e);
        if (e == p) return false;
        bool real = *e == '.' || *e == 'e';
        double dv = (double)iv;
        if (real) { dv = strtod_(start, &e); }
        p = e;
        uint8_t t = real ? T_DOUBLE : T_INT;
        switch (*p) { case 'b': t = T_BYTE; ++p; break; case 's': t = T_SHORT; ++p; break; case 'L': t = T_LONG; ++p; break; case 'f': t = T_FLOAT; ++p; break; case 'd': t = T_DOUBLE; ++p; break; default: break; }
        const uint32_t c = d.add_child(parent, kNil, t, name); if (c == kNil) return false;
        return is_real(t) ? d.set_real(c, real ? dv : (double)iv) : d.set_integer(c, iv);
    }
    static uint8_t hint(char c)
    {
        switch (c) { case 'b': return T_BYTE; case 's': return T_SHORT; case 'i': return T_INT; case 'l': return T_LONG; case 'f': return T_FLOAT; case 'd': return T_DOUBLE;
                     case 't': return T_STRING; case 'c': return T_COMPOUND; case 'n': return T_LIST; default: return 0; }
    }
    static long long strtoll_(const char *s, char **end)
    {
        const char *q = s; bool neg = false; if (*q == '-') { neg = true; ++q; }
        if (*q < '0' || *q > '9') { *end = (char *)s; return 0; }
        unsigned long long v = 0; while (*q >= '0' && *q <= '9') v = v * 10 + (unsigned)(*q++ - '0');
        *end = (char *)q; return neg ? -(long long)v : (long long)v;
    }
    static double strtod_(const char *s, char **end)
    {
        const char *q = s; bool neg = false; if (*q == '-') { neg = true; ++q; }
        double v = 0; while (*q >= '0' && *q <= '9') v = v * 10 + (*q++ - '0');
        if (*q == '.') { ++q; double f = 0.1; while (*q >= '0' && *q <= '9') { v += f * (*q++ - '0'); f *= 0.1; } }
        if (*q == 'e' || *q == 'E') { ++q; bool en = false; if (*q == '-') { en = true; ++q; } else if (*q == '+') ++q; int ex = 0; while (*q >= '0' && *q <= '9') ex = ex * 10 + (*q++ - '0'); double m = 1; while (ex--) m *= 10; v = en ? v / m : v * m; }
        *end = (char *)q; return neg ? -v : v;
    }
};
// Builds `src` (one SNBT value) as a new child of `parent`. Returns the node or kNil.
inline uint32_t snbt_build(Doc &d, uint32_t parent, const char *name, const char *src)
{
    const uint32_t before = d.at(parent).last;
    Snbt s(d, src);
    if (!s.value(parent, name)) {
        const uint32_t made = d.at(parent).last;
        if (made != kNil && made != before) d.remove(made);
        return kNil;
    }
    return d.at(parent).last;
}
// Decimal parsing for the editor (strtod is not used on the console).
inline bool parse_double(const char *s, double *out)
{
    char *e; const double v = Snbt::strtod_(s, &e);
    if (e == s || *e) return false;
    *out = v; return true;
}

} // namespace lce_nbt_ed
