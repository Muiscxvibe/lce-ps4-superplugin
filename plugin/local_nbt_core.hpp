#pragma once
// Local-world NBT backend core: the same request/reply exchange the editor has with an
// LCE server (MC|LceNbt semantics), carried out in-process against the native 1.95
// game objects of the world this console hosts. Hardware independent: every native
// entry point comes through `Natives` (bound and attested in local_nbt.cpp, faked by
// nbt_editor_test.cpp with objects of the exact native layouts).
//
//  get p      native Entity::saveWithoutId into a fresh CompoundTag -> NBT bytes
//  tx/edits/commit
//             fresh snapshot -> Doc -> the editor's protocol lines (nbt_apply.hpp) ->
//             validation -> native CompoundTag graph -> Entity::load (vtable +0x290) ->
//             resync (teleport for position/rotation, ServerPlayer::setGameMode for a
//             survival/creative change) -> read back -> reply "commit"
//  undo p     the snapshot taken before the last commit, loaded the same way
// Native layouts (encode.c, r72 nbt_native_encode_195, console proven):
//   Tag vtable +0x30 deleting dtor; scalar value +8; StringTag wstring +8;
//   arrays data +8 / count +0x10; ListTag vector begin +0x10 end +0x18 cap +0x20, type +0x28;
//   CompoundTag hash list: sentinel +0x10, size +0x18, node next +0, key wstring +0x10, Tag* +0x38;
//   wstring: buffer/pointer +8, length +0x18, capacity +0x20 (UTF-16 units).
#include "nbt_apply.hpp"

namespace lce_local_nbt {
using namespace lce_nbt_ed;

// RVAs of the vtables (as stored in objects: symbol + 0x10), by NBT type 1..11.
static const uintptr_t kTagVtable[12] = {0, 0x12209a8, 0x12bd5c8, 0x126e2c8, 0x127b1e0, 0x1258388, 0x123cfd0, 0x121d6c8, 0x12d83a8, 0x1278268, 0x122ede0, 0x126e0f8};
// object sizes for operator new (from the stock put* functions)
static const size_t kTagSize[12] = {0, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x20, 0x30, 0x30, 0x58, 0x18};
constexpr size_t kWstring = 0x28;

struct Natives {
    int (*authorize)(void *, uintptr_t) = nullptr;
    void *authorize_context = nullptr;
    uintptr_t image = 0;
    int (*read)(void *ctx, uintptr_t address, void *out, size_t n) = nullptr;   // 1 ok
    void *ctx = nullptr;
    void *(*alloc)(size_t) = nullptr;            // operator new
    void (*free_)(void *) = nullptr;             // operator delete
    void *(*alloc_array)(size_t) = nullptr;      // operator new[]
    void (*ws_assign)(void *ws, const uint16_t *units, uint64_t n) = nullptr;
    void (*compound_ctor)(void *) = nullptr;
    void (*put_tag)(void *compound, const void *key, void *child) = nullptr;   // adopts child
    void (*list_ctor)(void *, int elem) = nullptr;
    void (*list_grow)(void *vector, uint64_t n) = nullptr;                  // vector object = list + 8
    void (*ctor_byte)(void *, int) = nullptr;
    void (*ctor_short)(void *, int) = nullptr;
    void (*ctor_int)(void *, int) = nullptr;
    void (*ctor_long)(void *, int64_t) = nullptr;
    void (*ctor_float)(void *, float) = nullptr;
    void (*ctor_double)(void *, double) = nullptr;
    void (*ctor_string)(void *, const void *ws) = nullptr;                   // copies
    void (*ctor_byte_array)(void *, void *data, int n, int own) = nullptr;   // adopts new[] data
    void (*ctor_int_array)(void *, void *data, int n) = nullptr;             // adopts new[] data
    void *(*entity_save)(uintptr_t entity, void *tag) = nullptr;             // Entity::saveWithoutId
    void (*entity_load)(uintptr_t entity, void *tag) = nullptr;              // Entity::load (vt +0x290)
    void (*teleport)(uintptr_t player, double x, double y, double z) = nullptr;   // ServerPlayer::teleportTo (vt +0x670)
    void (*set_game_mode)(uintptr_t player, uintptr_t game_type) = nullptr;       // ServerPlayer::setGameMode
    uintptr_t game_type[2] = {0, 0};             // canonical GameType objects: survival, creative
};

enum Status { OK = 1, UNAVAILABLE = 0, INVALID = -1, TOO_LARGE = -2, MALFORMED = -3 };
constexpr unsigned kNodes = kMaxNodes, kDepth = 40;

// ---------------------------------------------------------------- native -> NBT bytes
class Encoder {
public:
    Encoder(const Natives &n, uint8_t *out, size_t cap) : n_(n), out_(out), cap_(cap) {}
    int encode(uintptr_t root, size_t *used)
    {
        size_ = 0; count_ = 0; status_ = OK;
        if (!big(10, 1) || !big(0, 2) || !payload(root, 10, 0)) return status_;
        *used = size_; return OK;
    }
private:
    const Natives &n_; uint8_t *out_; size_t cap_, size_ = 0; unsigned count_ = 0; int status_ = OK;
    bool fail(int s) { if (status_ == OK) status_ = s; return false; }
    bool rd(uintptr_t p, size_t off, void *o, size_t k) { if (!p || p > UINTPTR_MAX - off - k) return fail(MALFORMED); return n_.read(n_.ctx, p + off, o, k) == 1 || fail(UNAVAILABLE); }
    bool word(uintptr_t p, size_t off, unsigned w, uint64_t *v) { uint8_t b[8]; if (!rd(p, off, b, w)) return false; *v = 0; for (unsigned i = 0; i < w; ++i) *v |= (uint64_t)b[i] << (8 * i); return true; }
    bool put(const void *b, size_t k) { if (k > cap_ - size_) return fail(TOO_LARGE); memcpy(out_ + size_, b, k); size_ += k; return true; }
    bool big(uint64_t v, unsigned w) { uint8_t b[8]; for (unsigned i = 0; i < w; ++i) b[w - 1 - i] = (uint8_t)(v >> (8 * i)); return put(b, w); }
    unsigned type_of(uint64_t vt) const { for (unsigned i = 1; i <= 11; ++i) if (vt == n_.image + kTagVtable[i]) return i; return 0; }
    bool type_at(uintptr_t tag, unsigned *t) { uint64_t vt; if (!word(tag, 0, 8, &vt)) return false; *t = type_of(vt); return *t || fail(MALFORMED); }
    bool utf(uintptr_t ws)
    {
        uint64_t len, cap, addr;
        if (!word(ws, 0x18, 8, &len) || !word(ws, 0x20, 8, &cap)) return false;
        if (len > cap || len > 65535) return fail(MALFORMED);
        if (cap <= 7) addr = ws + 8; else if (!word(ws, 8, 8, &addr)) return false;
        if (len && !addr) return fail(MALFORMED);
        const size_t prefix = size_; if (!big(0, 2)) return false; const size_t begin = size_;
        uint8_t u[128];
        for (size_t off = 0; off < len;) {
            size_t k = (size_t)len - off; if (k > sizeof u / 2) k = sizeof u / 2;
            if (!rd((uintptr_t)addr, off * 2, u, k * 2)) return false;
            for (size_t i = 0; i < k; ++i) {
                const unsigned c = u[2 * i] | (unsigned)u[2 * i + 1] << 8; uint8_t e[3]; size_t m;
                if (c >= 1 && c <= 0x7f) { e[0] = (uint8_t)c; m = 1; }
                else if (c <= 0x7ff) { e[0] = (uint8_t)(0xc0 | c >> 6); e[1] = (uint8_t)(0x80 | (c & 63)); m = 2; }
                else { e[0] = (uint8_t)(0xe0 | c >> 12); e[1] = (uint8_t)(0x80 | (c >> 6 & 63)); e[2] = (uint8_t)(0x80 | (c & 63)); m = 3; }
                if (!put(e, m)) return false;
            }
            off += k;
        }
        const size_t bytes = size_ - begin; if (bytes > 65535) return fail(TOO_LARGE);
        out_[prefix] = (uint8_t)(bytes >> 8); out_[prefix + 1] = (uint8_t)bytes; return true;
    }
    bool payload(uintptr_t tag, unsigned expected, unsigned depth)
    {
        if (depth > kDepth || ++count_ > kNodes) return fail(TOO_LARGE);
        unsigned t; if (!type_at(tag, &t)) return false;
        if (t != expected) return fail(MALFORMED);
        static const unsigned w[] = {0, 1, 2, 4, 8, 4, 8};
        if (t <= 6) { uint64_t b; return word(tag, 8, w[t], &b) && big(b, w[t]); }
        if (t == T_BYTE_ARRAY || t == T_INT_ARRAY) {
            uint64_t data, cnt; if (!word(tag, 8, 8, &data) || !word(tag, 0x10, 4, &cnt)) return false;
            const unsigned ew = t == T_BYTE_ARRAY ? 1 : 4;
            if (cnt > (cap_ - size_) / ew) return fail(TOO_LARGE);
            if (cnt && !data) return fail(MALFORMED);
            if (!big(cnt, 4)) return false;
            uint8_t b[128];
            for (size_t off = 0; off < cnt * ew;) {
                size_t k = (size_t)cnt * ew - off; if (k > sizeof b) k = sizeof b;
                if (!rd((uintptr_t)data, off, b, k)) return false;
                if (ew == 4) for (size_t i = 0; i < k; i += 4) { uint8_t a = b[i], c = b[i + 1]; b[i] = b[i + 3]; b[i + 1] = b[i + 2]; b[i + 2] = c; b[i + 3] = a; }
                if (!put(b, k)) return false;
                off += k;
            }
            return true;
        }
        if (t == T_STRING) return utf(tag + 8);
        if (t == T_LIST) {
            uint64_t b, e, c; if (!word(tag, 0x10, 8, &b) || !word(tag, 0x18, 8, &e) || !word(tag, 0x20, 8, &c)) return false;
            if ((!b && (e || c)) || (b && ((b & 7) || e < b || c < e || ((e - b) & 7)))) return fail(MALFORMED);
            const size_t k = b ? (size_t)((e - b) / 8) : 0;
            if (k > kNodes) return fail(TOO_LARGE);
            unsigned ct = 0; uint64_t first = 0;
            if (k && (!word((uintptr_t)b, 0, 8, &first) || !type_at((uintptr_t)first, &ct))) return false;
            if (!big(ct, 1) || !big(k, 4)) return false;
            for (size_t i = 0; i < k; ++i) { uint64_t ch; if (!word((uintptr_t)b, i * 8, 8, &ch) || !payload((uintptr_t)ch, ct, depth + 1)) return false; }
            return true;
        }
        uint64_t sentinel, k, next; if (!word(tag, 0x10, 8, &sentinel) || !word(tag, 0x18, 8, &k) || !word((uintptr_t)sentinel, 0, 8, &next)) return false;
        if (k > kNodes) return fail(TOO_LARGE);
        for (uint64_t i = 0; i < k; ++i) {
            uint64_t ch, following; unsigned ct;
            if (!next || next == sentinel) return fail(MALFORMED);
            if (!word((uintptr_t)next, 0, 8, &following) || !word((uintptr_t)next, 0x38, 8, &ch) || !type_at((uintptr_t)ch, &ct)) return false;
            if (!big(ct, 1) || !utf((uintptr_t)next + 0x10) || !payload((uintptr_t)ch, ct, depth + 1)) return false;
            next = following;
        }
        if (next != sentinel) return fail(MALFORMED);
        return big(0, 1);
    }
};

// ---------------------------------------------------------------- Doc -> native graph
class Builder {
public:
    explicit Builder(const Natives &n) : n_(n) {}
    // Native CompoundTag for the compound `node` of `d`; 0 on failure (nothing leaks).
    uintptr_t compound(const Doc &d, uint32_t node) { void *t = make(d, node); return (uintptr_t)t; }
    static void destroy(void *tag) { if (!tag) return; typedef void (*Dtor)(void *); const uintptr_t vt = *(const uintptr_t *)tag; (*(Dtor const *)(vt + 0x30))(tag); }
private:
    const Natives &n_;
    struct Ws { alignas(8) uint8_t b[kWstring]; };
    void ws_init(Ws &w) { memset(w.b, 0, sizeof w.b); const uint64_t seven = 7; memcpy(w.b + 0x20, &seven, 8); }
    void ws_free(Ws &w) { uint64_t cap; memcpy(&cap, w.b + 0x20, 8); if (cap >= 8) { void *p; memcpy(&p, w.b + 8, 8); n_.free_(p); } }
    bool ws_set(Ws &w, const uint8_t *mutf8, size_t len)
    {
        static uint16_t u[32768];
        const size_t k = len ? mutf8_to_utf16(mutf8, len, u, sizeof u / 2) : 0;
        if (len && !k) return false;
        n_.ws_assign(w.b, u, k); return true;
    }
    void *obj(uint8_t t) { void *p = n_.alloc(kTagSize[t]); if (p) memset(p, 0, kTagSize[t]); return p; }
    void *make(const Doc &d, uint32_t node)
    {
        const Node &x = d.at(node);
        void *p;
        switch (x.type) {
        case T_BYTE: if (!(p = obj(T_BYTE))) return nullptr; n_.ctor_byte(p, (int)(int8_t)x.v.i); return p;
        case T_SHORT: if (!(p = obj(T_SHORT))) return nullptr; n_.ctor_short(p, (int)(int16_t)x.v.i); return p;
        case T_INT: if (!(p = obj(T_INT))) return nullptr; n_.ctor_int(p, (int)x.v.i); return p;
        case T_LONG: if (!(p = obj(T_LONG))) return nullptr; n_.ctor_long(p, x.v.i); return p;
        case T_FLOAT: if (!(p = obj(T_FLOAT))) return nullptr; n_.ctor_float(p, (float)x.v.d); return p;
        case T_DOUBLE: if (!(p = obj(T_DOUBLE))) return nullptr; n_.ctor_double(p, x.v.d); return p;
        case T_STRING: {
            Ws w; ws_init(w);
            if (!ws_set(w, d.bytes(x.v.b.off), x.v.b.len)) { ws_free(w); return nullptr; }
            if (!(p = obj(T_STRING))) { ws_free(w); return nullptr; }
            n_.ctor_string(p, w.b); ws_free(w); return p;
        }
        case T_BYTE_ARRAY: case T_INT_ARRAY: {
            const uint32_t k = x.v.b.len; const bool b = x.type == T_BYTE_ARRAY;
            void *data = k ? n_.alloc_array(b ? k : (size_t)k * 4) : nullptr;
            if (k && !data) return nullptr;
            for (uint32_t i = 0; i < k; ++i) { const int64_t v = d.array_get(node, i); if (b) ((int8_t *)data)[i] = (int8_t)v; else { const int32_t w = (int32_t)v; memcpy((uint8_t *)data + 4 * i, &w, 4); } }
            if (!(p = obj(x.type))) return nullptr;   // (data leaks only if the allocator fails here)
            if (b) n_.ctor_byte_array(p, data, (int)k, 1); else n_.ctor_int_array(p, data, (int)k);
            return p;
        }
        case T_LIST: {
            if (!(p = obj(T_LIST))) return nullptr;
            n_.list_ctor(p, x.count ? x.elem : 0);
            for (uint32_t c = x.first; c != kNil; c = d.at(c).next) {
                void *ch = make(d, c);
                if (!ch) { destroy(p); return nullptr; }
                uintptr_t end, cap; memcpy(&end, (uint8_t *)p + 0x18, 8); memcpy(&cap, (uint8_t *)p + 0x20, 8);
                if (end == cap) { n_.list_grow((uint8_t *)p + 8, 1); memcpy(&end, (uint8_t *)p + 0x18, 8); memcpy(&cap, (uint8_t *)p + 0x20, 8); if (!end || end == cap) { destroy(ch); destroy(p); return nullptr; } }
                memcpy((void *)end, &ch, 8); end += 8; memcpy((uint8_t *)p + 0x18, &end, 8);
            }
            return p;
        }
        case T_COMPOUND: {
            if (!(p = obj(T_COMPOUND))) return nullptr;
            n_.compound_ctor(p);
            for (uint32_t c = x.first; c != kNil; c = d.at(c).next) {
                void *ch = make(d, c);
                if (!ch) { destroy(p); return nullptr; }
                Ws k; ws_init(k);
                if (!ws_set(k, d.bytes(d.at(c).name_off), d.at(c).name_len)) { ws_free(k); destroy(ch); destroy(p); return nullptr; }
                n_.put_tag(p, k.b, ch); ws_free(k);
            }
            return p;
        }
        default: return nullptr;
        }
    }
};

// ---------------------------------------------------------------- validation (1.95 player NBT)
struct Plan { int game_mode = -1; double x = 0, y = 0, z = 0; };
inline uint32_t key(const Doc &d, const char *k) { return d.valid() ? d.find(d.root, k) : kNil; }
inline bool same_value(const Doc &a, const Doc &b, const char *k)
{
    const uint32_t x = key(a, k), y = key(b, k);
    if (x == kNil || y == kNil) return x == y;
    const Node &p = a.at(x), &q = b.at(y);
    if (p.type != q.type) return false;
    if (is_integer(p.type)) return p.v.i == q.v.i;
    if (is_real(p.type)) return dbits(p.v.d) == dbits(q.v.d);
    if (p.type == T_STRING) return p.v.b.len == q.v.b.len && !memcmp(a.bytes(p.v.b.off), b.bytes(q.v.b.off), p.v.b.len);
    return false;
}
inline bool real_list(const Doc &d, const char *k, unsigned n, uint8_t type, double limit, double *out)
{
    const uint32_t l = key(d, k);
    if (l == kNil || d.at(l).type != T_LIST || d.at(l).count != n || d.at(l).elem != type) return false;
    unsigned i = 0;
    for (uint32_t c = d.at(l).first; c != kNil; c = d.at(c).next, ++i) { const double v = d.at(c).v.d; if (v != v || v > limit || v < -limit) return false; if (out) out[i] = v; }
    return true;
}
// Item lists: compounds with a byte Slot, a string id and a byte Count 1..127 (when present).
inline bool item_list(const Doc &d, const char *k, char *err, size_t cap)
{
    const uint32_t l = key(d, k);
    if (l == kNil) return true;
    if (d.at(l).type != T_LIST || (d.at(l).count && d.at(l).elem != T_COMPOUND)) { snprintf(err, cap, "%s must be a list of compounds", k); return false; }
    for (uint32_t c = d.at(l).first; c != kNil; c = d.at(c).next) {
        const uint32_t s = d.find(c, "Slot"), id = d.find(c, "id"), n = d.find(c, "Count");
        if (s == kNil || d.at(s).type != T_BYTE) { snprintf(err, cap, "%s entries need a byte Slot", k); return false; }
        if (id == kNil || d.at(id).type != T_STRING || !d.at(id).v.b.len) { snprintf(err, cap, "%s slot %lld: id must be a string", k, (long long)d.at(s).v.i); return false; }
        if (n != kNil && (d.at(n).type != T_BYTE || d.at(n).v.i < 1)) { snprintf(err, cap, "%s slot %lld: Count must be a byte 1-127", k, (long long)d.at(s).v.i); return false; }
    }
    return true;
}
inline bool validate_player(const Doc &live, const Doc &edit, Plan *p, char *err, size_t cap)
{
    double pos[3];
    if (!real_list(edit, "Pos", 3, T_DOUBLE, 3.0e7, pos) || pos[1] < -128 || pos[1] > 1024) { snprintf(err, cap, "Pos must be 3 doubles in the world (y -128..1024)"); return false; }
    if (!real_list(edit, "Motion", 3, T_DOUBLE, 100.0, nullptr)) { snprintf(err, cap, "Motion must be 3 doubles within +-100"); return false; }
    if (!real_list(edit, "Rotation", 2, T_FLOAT, 1.0e6, nullptr)) { snprintf(err, cap, "Rotation must be 2 floats"); return false; }
    p->x = pos[0]; p->y = pos[1]; p->z = pos[2];
    static const char *const ids[] = {"UUIDMost", "UUIDLeast", "UUID"};
    for (const char *k : ids) if (!same_value(live, edit, k)) { snprintf(err, cap, "%s cannot be changed (account identity)", k); return false; }
    if (!same_value(live, edit, "Dimension")) { snprintf(err, cap, "Dimension: travel through a portal in a local world"); return false; }
    const uint32_t h = key(edit, "Health");
    if (h != kNil && (edit.at(h).type != T_FLOAT || edit.at(h).v.d != edit.at(h).v.d || edit.at(h).v.d < 0 || edit.at(h).v.d > 1.0e6)) { snprintf(err, cap, "Health must be a float 0-1000000"); return false; }
    const uint32_t gm = key(edit, "playerGameType"), gl = key(live, "playerGameType");
    if (gm != kNil) {
        if (edit.at(gm).type != T_INT || edit.at(gm).v.i < 0 || edit.at(gm).v.i > 3) { snprintf(err, cap, "playerGameType must be an int 0-3"); return false; }
        const int64_t was = gl != kNil ? live.at(gl).v.i : -1;
        if (edit.at(gm).v.i != was) {
            if (edit.at(gm).v.i > 1) { snprintf(err, cap, "local worlds: game mode survival (0) or creative (1)"); return false; }
            p->game_mode = (int)edit.at(gm).v.i;
        }
    }
    if (!item_list(edit, "Inventory", err, cap) || !item_list(edit, "EnderItems", err, cap)) return false;
    return true;
}

// ---------------------------------------------------------------- request / reply exchange
// One request at a time: the UI thread posts the editor's lines; the server thread runs them
// inside the scope tick; the UI thread takes the reply. Lock-free single producer/consumer
// through `state_` (0 idle, 1 request posted, 2 running, 3 reply ready).
class Backend {
public:
    static constexpr size_t kRequest = 64 * 1024, kReply = kMaxDocBytes;
    struct Reply { char op[8]; char target[8]; bool ok; char msg[96]; const uint8_t *nbt; size_t n; };

    // [UI] false while busy. Lines are "/lce-nbt ..." separated by '\n'.
    bool post(const char *lines, size_t n)
    {
        unsigned s = 0;
        if (n >= kRequest || !__atomic_compare_exchange_n(&state_, &s, 1u, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return false;
        memcpy(req_, lines, n); req_len_ = n;
        __atomic_store_n(&state_, 4u, __ATOMIC_RELEASE);   // 4: posted and complete
        return true;
    }
    // [UI] the finished reply (valid until the next post).
    bool take(Reply *r)
    {
        if (__atomic_load_n(&state_, __ATOMIC_ACQUIRE) != 3u) return false;
        *r = reply_; r->nbt = reply_n_ ? out_ : nullptr; r->n = reply_n_;
        __atomic_store_n(&state_, 0u, __ATOMIC_RELEASE);
        return true;
    }
    // UI owner, admission serialized: discard a pending/finished exchange
    // without clearing this owner's document or undo history.
    bool discard_exchange(unsigned expected) {
        if(expected!=3u && expected!=4u)return false;
        if(!__atomic_compare_exchange_n(&state_,&expected,1u,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return false;
        memset(req_,0,sizeof req_);req_len_=0;reply_={};reply_n_=0;memset(out_,0,sizeof out_);
        __atomic_store_n(&state_,0u,__ATOMIC_RELEASE);return true;
    }
    bool reply_ready() const { return __atomic_load_n(&state_, __ATOMIC_ACQUIRE) == 3u; }
    bool busy() const { const unsigned s = __atomic_load_n(&state_, __ATOMIC_ACQUIRE); return s == 1u || s == 2u || s == 3u || s == 4u; }
    // [server thread] one pending request against the host player. Natives must be bound.
    void process(const Natives &n, uintptr_t player)
    {
        unsigned s = 4u;
        if (!__atomic_compare_exchange_n(&state_, &s, 2u, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return;
        run(n, player);
        __atomic_store_n(&state_, 3u, __ATOMIC_RELEASE);
    }
    // UI-owner thread only, with admission blocked during retirement. Pending and
    // ready requests are discarded; claimed work must finish before reset.
    bool retire() {
        unsigned s=__atomic_load_n(&state_,__ATOMIC_ACQUIRE);
        for (;;) {
            if(s==1u || s==2u) return false;
            if(__atomic_compare_exchange_n(&state_,&s,1u,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) break;
        }
        memset(req_,0,sizeof req_); req_len_=0; reply_={}; reply_n_=0;
        memset(out_,0,sizeof out_); memset(scratch_,0,sizeof scratch_);
        memset(undo_buf_,0,sizeof undo_buf_); memset(undo_len_,0,sizeof undo_len_);
        memset(keep_,0,sizeof keep_); undo_n_=0; live_->clear(); work_->clear();
        __atomic_store_n(&state_,0u,__ATOMIC_RELEASE); return true;
    }
    unsigned undo_depth() const { return undo_n_; }
    // Docs are large (kMaxNodes / kPoolBytes): the caller provides them (static on the console).
    Backend(Doc *live, Doc *work) : live_(live), work_(work) {}

private:
    static constexpr unsigned kUndo = 4;
    unsigned state_ = 0;
    char req_[kRequest]; size_t req_len_ = 0;
    Reply reply_{}; uint8_t out_[kReply]; size_t reply_n_ = 0;
    uint8_t scratch_[kReply];
    // before-commit snapshots (bytes) for "undo p"
    uint8_t undo_buf_[kUndo][kReply / 4]; size_t undo_len_[kUndo] = {}; unsigned undo_n_ = 0;
    uint8_t keep_[kReply / 4];   // the snapshot a commit starts from (pushed to undo when it succeeds)
    Doc *live_, *work_;

    void answer(const char *op, bool ok, const char *msg, const uint8_t *nbt, size_t len)
    {
        snprintf(reply_.op, sizeof reply_.op, "%s", op); snprintf(reply_.target, sizeof reply_.target, "p");
        reply_.ok = ok; snprintf(reply_.msg, sizeof reply_.msg, "%s", msg);
        reply_n_ = ok && nbt && len <= sizeof out_ ? len : 0;
        if (reply_n_ && nbt != out_) memcpy(out_, nbt, reply_n_);
    }
    // native snapshot of the player into buf
    int snapshot(const Natives &n, uintptr_t player, uint8_t *buf, size_t cap, size_t *len)
    {
        if (!n.authorize || n.authorize(n.authorize_context,player)!=1) return UNAVAILABLE;
        void *tag = n.alloc(kTagSize[T_COMPOUND]); if (!tag) return UNAVAILABLE;
        memset(tag, 0, kTagSize[T_COMPOUND]); n.compound_ctor(tag);
        n.entity_save(player, tag);
        Encoder e(n, buf, cap); const int st = e.encode((uintptr_t)tag, len);
        Builder::destroy(tag);
        return st;
    }
    // loads `doc` into the player (validated against live_) and reads it back into out_
    bool load_doc(const Natives &n, uintptr_t player, const Doc &doc, const char *op, const char *okmsg)
    {
        char err[96];
        Plan plan;
        if (!validate_player(*live_, doc, &plan, err, sizeof err)) { answer(op, false, err, nullptr, 0); return false; }
        // a survival/creative change goes through the full native setter (abilities, packets) after the load
        if (plan.game_mode >= 0) {
            if (!n.set_game_mode || !n.game_type[plan.game_mode]) { answer(op, false, "game mode setter unavailable", nullptr, 0); return false; }
        }
        Builder b(n);
        // keep the live game mode in the loaded tag; the setter changes it afterwards
        uintptr_t tag;
        if (plan.game_mode >= 0) {
            const size_t len = doc.serialize(scratch_, sizeof scratch_);
            if (!len || !work_->parse(scratch_, len)) { answer(op, false, "document too large", nullptr, 0); return false; }
            const uint32_t gm = work_->find(work_->root, "playerGameType"), gl = live_->find(live_->root, "playerGameType");
            if (gm != kNil && gl != kNil) work_->set_integer(gm, live_->at(gl).v.i);
            tag = b.compound(*work_, work_->root);
        } else tag = b.compound(doc, doc.root);
        if (!tag) { answer(op, false, "could not build the native tag", nullptr, 0); return false; }
        if (!n.authorize || n.authorize(n.authorize_context,player)!=1) {
            Builder::destroy((void *)tag); answer(op,false,"player authorization expired",nullptr,0); return false;
        }
        n.entity_load(player, (void *)tag);
        Builder::destroy((void *)tag);
        if (n.authorize(n.authorize_context,player)!=1) { answer(op,false,"applied; authorization expired",nullptr,0); return false; }
        n.teleport(player, plan.x, plan.y, plan.z);   // position/rotation reach the client
        if (plan.game_mode >= 0) { if (n.authorize(n.authorize_context,player)!=1) { answer(op,false,"applied; authorization expired",nullptr,0); return false; } n.set_game_mode(player, n.game_type[plan.game_mode]); }
        size_t len = 0;
        if (snapshot(n, player, out_, sizeof out_, &len) != OK) { answer(op, false, "applied; read back failed", nullptr, 0); return false; }
        reply_n_ = 0; answer(op, true, okmsg, out_, len);
        return true;
    }
    void push_undo(const uint8_t *b, size_t len)
    {
        if (len > sizeof undo_buf_[0]) return;   // too large to keep: no undo for this one
        if (undo_n_ == kUndo) { for (unsigned i = 1; i < kUndo; ++i) { memcpy(undo_buf_[i - 1], undo_buf_[i], undo_len_[i]); undo_len_[i - 1] = undo_len_[i]; } --undo_n_; }
        memcpy(undo_buf_[undo_n_], b, len); undo_len_[undo_n_++] = len;
    }
    static bool starts(const char *l, size_t n, const char *p) { const size_t k = strlen(p); return n >= k && !memcmp(l, p, k); }
    void run(const Natives &n, uintptr_t player)
    {
        const char *s = req_; const size_t len = req_len_;
        size_t first = 0; while (first < len && s[first] != '\n') ++first;
        // the target must be the player ("p"); other documents need an LCE server
        char tgt[8] = {}; { size_t i = 0; int w = 0; while (i < first && w < 3) { while (i < first && s[i] == ' ') ++i; size_t b = i; while (i < first && s[i] != ' ') ++i; if (++w == 3) snprintf(tgt, sizeof tgt, "%.*s", (int)(i - b > 7 ? 7 : i - b), s + b); } }
        size_t snap = 0;
        if (starts(s, first, "/lce-nbt get ")) {
            if (strcmp(tgt, "p")) { answer("get", false, "local world: only Me (player) is available", nullptr, 0); return; }
            if (snapshot(n, player, out_, sizeof out_, &snap) != OK) { answer("get", false, "native snapshot failed", nullptr, 0); return; }
            reply_n_ = 0; answer("get", true, "ok", out_, snap); return;
        }
        if (starts(s, first, "/lce-nbt undo ")) {
            if (!undo_n_) { answer("undo", false, "nothing to undo for this target", nullptr, 0); return; }
            if (snapshot(n, player, scratch_, sizeof scratch_, &snap) != OK || !live_->parse(scratch_, snap)) { answer("undo", false, "native snapshot failed", nullptr, 0); return; }
            --undo_n_;
            if (!work_->parse(undo_buf_[undo_n_], undo_len_[undo_n_])) { answer("undo", false, "undo data damaged", nullptr, 0); return; }
            // the whole previous state, position included (as on an LCE server)
            load_doc(n, player, *work_, "undo", "restored");
            return;
        }
        if (!starts(s, first, "/lce-nbt tx ")) { answer("err", false, "local world: unsupported request", nullptr, 0); return; }
        if (strcmp(tgt, "p")) { answer("commit", false, "local world: only Me (player) is editable", nullptr, 0); return; }
        if (snapshot(n, player, scratch_, sizeof scratch_, &snap) != OK || !live_->parse(scratch_, snap) || !work_->parse(scratch_, snap)) { answer("commit", false, "native snapshot failed", nullptr, 0); return; }
        // replay the edit lines on the fresh snapshot (concurrent changes to other fields are kept)
        ScriptApplier ap(*work_);
        unsigned edits = 0; bool committed = false;
        for (size_t i = first + 1; i < len;) {
            size_t j = i; while (j < len && s[j] != '\n') ++j;
            if (j > i) {
                if (starts(s + i, j - i, "/lce-nbt commit")) { committed = true; break; }
                if (!ap.line(s + i, j - i)) { char m[96]; snprintf(m, sizeof m, "edit %u: %s", edits + 1, ap.err()); answer("commit", false, m, nullptr, 0); return; }
                ++edits;
            }
            i = j + 1;
        }
        if (!committed) { answer("commit", false, "no commit line", nullptr, 0); return; }
        const size_t keep_len = snap <= sizeof keep_ ? snap : 0;
        if (keep_len) memcpy(keep_, scratch_, keep_len);
        char ok[48]; snprintf(ok, sizeof ok, "%u edits applied", edits);
        if (load_doc(n, player, *work_, "commit", ok) && keep_len) push_undo(keep_, keep_len);
    }
};

}  // namespace lce_local_nbt
