/* LCE|ModIds wire codec (protocol v1), shared by the PS4 client module
 * (modules/netsync195) and the PC server (server/new/Minecraft.World/LceModIdWire.h
 * is a byte-identical copy; the test suites check that).
 *
 * Header-only, freestanding C++ (no STL, no exceptions, no heap) so that the
 * same file builds in the OpenOrbis plugin and in the Windows server.
 * Everything multi-byte is big-endian. See server/PROTOCOL.md.
 */
// Include guard (not #pragma once): the two byte-identical copies must not
// both define the codec when a test includes them via different paths.
#ifndef LCE_MODIDS_WIRE_H
#define LCE_MODIDS_WIRE_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace lce_modids {

constexpr uint8_t kVersion = 1;
constexpr char kChannel[] = "LCE|ModIds";            // 10 chars (CustomPayload channel limit: 20)
constexpr unsigned kChannelLen = sizeof kChannel - 1;
constexpr uint8_t kServerboundPacket = 225;          // ServerboundCustomPayloadPacket (reader 0x5fbe00)
constexpr uint8_t kClientboundPacket = 250;          // ClientboundCustomPayloadPacket (reader 0x18bbd0)
constexpr size_t kMaxPacket = 4096;                  // Coordinator::send_game_packet cap
constexpr size_t kPacketOverhead = 1 + 2 + 2 * kChannelLen + 2;
constexpr size_t kChunkBudget = 3968;                // payload bytes per generated chunk (<= kMaxPacket - overhead)
constexpr size_t kMaxPayload = kMaxPacket - kPacketOverhead;
constexpr unsigned kMaxChunks = 64;
constexpr unsigned kMaxEntries = 2048;               // LCE_LEDGER_MAX
constexpr unsigned kMaxKey = 63;                     // LCE_MAX_KEY - 1
constexpr unsigned kMaxMessage = 480;                // RESULT text bytes
constexpr size_t kHeaderSize = 10;                   // u8 version, u8 type, u32 session, u16 index, u16 count
constexpr size_t kEntryFixed = 11;

enum MsgType : uint8_t { kRequest = 1, kHello = 2, kResult = 3, kTable = 4 };
enum Kind : uint8_t { kItem = 0, kBlock = 1 };
enum EntryFlag : uint8_t {
    kLive = 1,          // registered in the client's game this session
    kBlockHasItem = 2,  // block: a block-item exists (not LCE_BLOCK_FLAG_NO_ITEM)
    kTransparent = 4,   // block: LCE_BLOCK_FLAG_TRANSPARENT
    kNoCollide = 8,     // block: LCE_BLOCK_FLAG_NO_COLLIDE
};
enum RequestFlag : uint8_t { kPolicyAdopt = 1, kPolicyAllowMissingItems = 2 };
enum Verdict : uint8_t { kAccept = 0, kReject = 1 };
enum Code : uint16_t {
    kOk = 0,
    kMalformed = 1,        // bad header/chunk order/digest/entry
    kBadVersion = 2,       // unsupported protocol version
    kKeyIdConflict = 3,    // same key, different numeric ID
    kIdTaken = 4,          // client's ID belongs to another key on the server
    kIdBuiltin = 5,        // client's ID is a built-in server item/tile
    kPropsMismatch = 6,    // same key+ID, different max stack/durability/hardness (mod version mismatch)
    kMissing = 7,          // the world uses content this client does not have
    kServerLedgerBad = 8,  // server ledger unreadable/unwritable
    kLimit = 9,            // too many entries
    kTimeout = 10,         // no HELLO although the world has mod IDs
    kNotAllowed = 11,      // locked server: new mod content is not accepted
    kDuplicate = 12,       // client sent the same key or ID twice
};

/* One ledger entry on the wire (kEntryFixed + key_len bytes):
 *   u8 kind, u8 flags, u16 id, u8 max_stack (1..64), u8 material/item-kind,
 *   u16 a, u16 b, u8 key_len, key bytes (lce_valid_key charset)
 * items:  a = max_damage (0 = not damageable), b = 0
 * blocks: a = hardness*100, b = resistance*100 (both saturate at 0xfffe; 0xffff reserved) */
struct Entry {
    uint8_t kind, flags;
    uint16_t id;
    uint8_t max_stack, material;
    uint16_t a, b;
    uint8_t key_len;
    char key[kMaxKey + 1];
};

inline uint32_t block_item_id(uint32_t block) { return block < 256 ? block : block + 768; }
inline uint32_t item_space_id(const Entry &e) { return e.kind == kItem ? e.id : block_item_id(e.id); }

/* Same rule as lce_valid_key (loader/package.cpp). */
inline bool valid_key(const char *key, size_t n) {
    if (!key || n == 0 || n > kMaxKey) return false;
    size_t ns = 0;
    while (ns < n && key[ns] != ':') ++ns;
    if (ns == n || ns < 2 || ns >= 32 || n == ns + 1) return false;
    for (size_t i = 0; i < ns; ++i) {
        char c = key[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    }
    for (size_t i = ns + 1; i < n; ++i) {
        char c = key[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '/' || c == '-' || c == '#'))
            return false;
    }
    for (size_t i = 0; i + 1 < n; ++i) if (key[i] == '.' && key[i + 1] == '.') return false;
    return true;
}

inline bool set_key(Entry &e, const char *key) {
    size_t n = key ? strlen(key) : 0;
    if (!valid_key(key, n)) return false;
    memcpy(e.key, key, n); e.key[n] = 0; e.key_len = (uint8_t)n;
    return true;
}

/* ---- SHA-256 (FIPS 180-4) ---------------------------------------------- */
struct Sha256 {
    uint32_t h[8]; uint8_t buf[64]; uint64_t len; uint32_t used;
    static uint32_t ror(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }
    void init() {
        static const uint32_t iv[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
        memcpy(h, iv, sizeof h); len = 0; used = 0;
    }
    void block(const uint8_t *p) {
        static const uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be,
            0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa,
            0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85,
            0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
            0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
            0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t t1 = hh + (ror(e, 6) ^ ror(e, 11) ^ ror(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
            uint32_t t2 = (ror(a, 2) ^ ror(a, 13) ^ ror(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    void update(const void *data, size_t n) {
        const uint8_t *p = (const uint8_t *)data;
        len += n;
        while (n) {
            size_t take = 64 - used; if (take > n) take = n;
            memcpy(buf + used, p, take); used += (uint32_t)take; p += take; n -= take;
            if (used == 64) { block(buf); used = 0; }
        }
    }
    void final(uint8_t out[32]) {
        uint64_t bits = len * 8;
        uint8_t pad = 0x80; update(&pad, 1);
        uint8_t z = 0; while (used != 56) update(&z, 1);
        uint8_t l[8]; for (int i = 0; i < 8; ++i) l[i] = (uint8_t)(bits >> (56 - 8 * i));
        update(l, 8);
        for (int i = 0; i < 8; ++i) { out[4 * i] = (uint8_t)(h[i] >> 24); out[4 * i + 1] = (uint8_t)(h[i] >> 16); out[4 * i + 2] = (uint8_t)(h[i] >> 8); out[4 * i + 3] = (uint8_t)h[i]; }
    }
};

/* Digest of an entry list = SHA-256 of its canonical LCEIDS 1 ledger text
 * ("LCEIDS 1\n" + "item|block <key> <id>\n" per entry, in order). For the
 * client's full ledger in file order this equals the file's sha256 trailer. */
struct Digest {
    Sha256 s;
    void init() { s.init(); s.update("LCEIDS 1\n", 9); }
    void add(const Entry &e) {
        char line[96]; size_t n = 0;
        const char *k = e.kind == kItem ? "item " : "block ";
        size_t kl = strlen(k); memcpy(line, k, kl); n = kl;
        memcpy(line + n, e.key, e.key_len); n += e.key_len; line[n++] = ' ';
        char d[8]; int dn = 0; uint32_t v = e.id;
        do { d[dn++] = (char)('0' + v % 10); v /= 10; } while (v);
        while (dn) line[n++] = d[--dn];
        line[n++] = '\n';
        s.update(line, n);
    }
    void final(uint8_t out[32]) { s.final(out); }
};

/* ---- bounded big-endian reader/writer ------------------------------------ */
struct Writer {
    uint8_t *p; size_t cap, n; bool ok;
    void init(uint8_t *buf, size_t c) { p = buf; cap = c; n = 0; ok = buf != nullptr; }
    void u8(uint32_t v) { if (!ok || n + 1 > cap) { ok = false; return; } p[n++] = (uint8_t)v; }
    void u16(uint32_t v) { u8(v >> 8); u8(v); }
    void u32(uint32_t v) { u16(v >> 16); u16(v); }
    void bytes(const void *d, size_t len) { if (!ok || len > cap - n) { ok = false; return; } if (len) memcpy(p + n, d, len); n += len; }
};
struct Reader {
    const uint8_t *p; size_t n, pos; bool ok;
    void init(const uint8_t *buf, size_t len) { p = buf; n = buf ? len : 0; pos = 0; ok = buf != nullptr; }
    uint32_t u8() { if (!ok || pos + 1 > n) { ok = false; return 0; } return p[pos++]; }
    uint32_t u16() { uint32_t a = u8(); return a << 8 | u8(); }
    uint32_t u32() { uint32_t a = u16(); return a << 16 | u16(); }
    bool bytes(void *out, size_t len) { if (!ok || len > n - pos) { ok = false; return false; } if (len) memcpy(out, p + pos, len); pos += len; return true; }
    size_t left() const { return ok ? n - pos : 0; }
};

struct Header { uint8_t version, type; uint32_t session; uint16_t index, count; };

inline void put_header(Writer &w, uint8_t type, uint32_t session, unsigned index, unsigned count) {
    w.u8(kVersion); w.u8(type); w.u32(session); w.u16(index); w.u16(count);
}
/* Returns false on truncation. The version is returned, not checked, so the
 * caller can answer kBadVersion instead of treating it as noise. */
inline bool get_header(Reader &r, Header &h) {
    h.version = (uint8_t)r.u8(); h.type = (uint8_t)r.u8(); h.session = r.u32(); h.index = (uint16_t)r.u16(); h.count = (uint16_t)r.u16();
    return r.ok;
}

inline size_t entry_size(const Entry &e) { return kEntryFixed + e.key_len; }
inline void put_entry(Writer &w, const Entry &e) {
    w.u8(e.kind); w.u8(e.flags); w.u16(e.id); w.u8(e.max_stack); w.u8(e.material); w.u16(e.a); w.u16(e.b);
    w.u8(e.key_len); w.bytes(e.key, e.key_len);
}
/* Validates kind, key charset, max_stack range and the ID range of the kind. */
inline bool get_entry(Reader &r, Entry &e) {
    memset(&e, 0, sizeof e);
    e.kind = (uint8_t)r.u8(); e.flags = (uint8_t)r.u8(); e.id = (uint16_t)r.u16(); e.max_stack = (uint8_t)r.u8();
    e.material = (uint8_t)r.u8(); e.a = (uint16_t)r.u16(); e.b = (uint16_t)r.u16(); e.key_len = (uint8_t)r.u8();
    if (!r.ok || e.key_len == 0 || e.key_len > kMaxKey) return false;
    if (!r.bytes(e.key, e.key_len)) return false;
    e.key[e.key_len] = 0;
    if (e.kind > kBlock || e.max_stack < 1 || e.max_stack > 64 || (e.flags & 0xf0)) return false;
    if (e.kind == kItem ? (e.id < 256 || e.id >= 32000) : (e.id < 1 || e.id >= 4096)) return false;
    return valid_key(e.key, e.key_len);
}

/* ---- CustomPayload framing ----------------------------------------------- */
/* Serverbound packet bytes as the native writer emits them: id 225, UTF channel
 * (u16 char count + UTF-16BE), u16 payload length, payload. */
inline size_t build_serverbound(const uint8_t *payload, size_t len, uint8_t *out, size_t cap) {
    if (len == 0 || len > kMaxPayload) return 0;
    Writer w; w.init(out, cap);
    w.u8(kServerboundPacket); w.u16(kChannelLen);
    for (unsigned i = 0; i < kChannelLen; ++i) w.u16((uint8_t)kChannel[i]);
    w.u16((uint32_t)len); w.bytes(payload, len);
    return w.ok ? w.n : 0;
}
/* Matches one whole clientbound frame [250][UTF "LCE|ModIds"][u16 len][payload]
 * exactly (the server queues these as single-packet frames). */
inline bool match_clientbound(const uint8_t *frame, size_t n, const uint8_t **payload, size_t *len) {
    if (!frame || n < kPacketOverhead + kHeaderSize || frame[0] != kClientboundPacket) return false;
    if (frame[1] != 0 || frame[2] != kChannelLen) return false;
    for (unsigned i = 0; i < kChannelLen; ++i)
        if (frame[3 + 2 * i] != 0 || frame[4 + 2 * i] != (uint8_t)kChannel[i]) return false;
    size_t at = 3 + 2 * kChannelLen;
    size_t l = (size_t)frame[at] << 8 | frame[at + 1];
    if (l != n - kPacketOverhead) return false;
    *payload = frame + kPacketOverhead; *len = l;
    return true;
}

/* ---- message bodies ------------------------------------------------------- */
struct HelloInfo { uint32_t api_version; uint8_t flags; uint16_t total; uint8_t digest[32]; };
constexpr size_t kHelloPrefix = 4 + 1 + 2 + 32;
struct RequestInfo { uint16_t total; uint8_t digest[32]; uint8_t flags; };
struct ResultInfo { uint8_t verdict; uint16_t code; uint16_t msg_len; char msg[kMaxMessage + 1]; uint16_t total; uint8_t digest[32]; uint8_t table_chunks; };

/* Splits `entries` into HELLO/TABLE chunk boundaries: returns the number of
 * chunks and fills first[i] (index of the first entry of chunk i), or 0 if it
 * cannot fit in kMaxChunks. `prefix` is the extra body of chunk 0. */
inline unsigned plan_chunks(const Entry *entries, unsigned count, size_t prefix, unsigned *first, unsigned first_cap) {
    unsigned chunks = 0, i = 0;
    do {
        if (chunks >= kMaxChunks || chunks >= first_cap) return 0;
        first[chunks] = i;
        size_t used = kHeaderSize + (chunks == 0 ? prefix : 0) + 2;
        while (i < count && used + entry_size(entries[i]) <= kChunkBudget) used += entry_size(entries[i++]);
        ++chunks;
    } while (i < count);
    return chunks;
}

}  // namespace lce_modids

#endif  // LCE_MODIDS_WIRE_H
