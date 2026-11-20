// LCE|ModIds server core: persistent per-world ledger, HELLO reassembly and
// the merge/validation rules. Engine-independent (STL only) so it is
// host-tested under ASan/UBSan (modloader-sdk/server/tests). See
// modloader-sdk/server/PROTOCOL.md.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <functional>
#include <set>
#include <string>
#include <vector>
#include "LceModIdWire.h"

namespace lce_modids {

struct Policy {
    bool adopt = true;                 // learn unknown live client entries into the world ledger
    bool allow_missing_items = false;  // admit clients lacking some item-only entries (stacks withheld)
};

inline const char *kind_name(uint8_t kind) { return kind == kItem ? "item" : "block"; }

inline bool props_equal(const Entry &a, const Entry &b) {
    if (a.max_stack != b.max_stack) return false;
    if (a.kind == kItem) return a.a == b.a;
    return a.a == b.a && ((a.flags ^ b.flags) & kBlockHasItem) == 0;
}

// A block with an ID the server's 8-bit chunk storage can hold is placeable.
inline bool block_placeable(const Entry &e) { return e.kind == kBlock && e.id < 256; }

// ---- server ledger -----------------------------------------------------------
// Text format (UTF-8, '\n'):
//   LCESRVIDS 1
//   <item|block> <key> <id> <max_stack> <material> <a> <b> <flags>
//   sha256 <64 hex of all preceding bytes>
// Entries are append-only, like the client ledger: an ID is never reused.
class ServerLedger {
public:
    std::vector<Entry> entries;

    const Entry *find(uint8_t kind, const char *key) const {
        for (const Entry &e : entries) if (e.kind == kind && !strcmp(e.key, key)) return &e;
        return nullptr;
    }
    const Entry *find_item_space(uint32_t item_id) const {
        for (const Entry &e : entries) if (item_space_id(e) == item_id) return &e;
        return nullptr;
    }
    const Entry *find_block(uint32_t block_id) const {
        for (const Entry &e : entries) if (e.kind == kBlock && e.id == block_id) return &e;
        return nullptr;
    }
    // Rejects key/ID collisions; never "repairs".
    bool add(const Entry &in) {
        if (entries.size() >= kMaxEntries || find(in.kind, in.key) || find_item_space(item_space_id(in)) ||
            (in.kind == kBlock && find_block(in.id)))
            return false;
        Entry e = in;
        e.flags &= (uint8_t)~kLive;
        entries.push_back(e);
        return true;
    }
    void digest(uint8_t out[32]) const {
        Digest d; d.init();
        for (const Entry &e : entries) d.add(e);
        d.final(out);
    }

    std::string serialize() const {
        std::string s = "LCESRVIDS 1\n";
        char line[160];
        for (const Entry &e : entries) {
            snprintf(line, sizeof line, "%s %s %u %u %u %u %u %u\n", kind_name(e.kind), e.key, (unsigned)e.id, (unsigned)e.max_stack,
                     (unsigned)e.material, (unsigned)e.a, (unsigned)e.b, (unsigned)(e.flags & (uint8_t)~kLive));
            s += line;
        }
        uint8_t d[32];
        Sha256 h; h.init(); h.update(s.data(), s.size()); h.final(d);
        static const char hx[] = "0123456789abcdef";
        s += "sha256 ";
        for (int i = 0; i < 32; ++i) { s += hx[d[i] >> 4]; s += hx[d[i] & 15]; }
        s += '\n';
        return s;
    }

    enum Load { kLoaded = 0, kCorrupt = 1, kBadHash = 2 };
    // Empty text = new world (empty ledger).
    Load load(const std::string &text) {
        entries.clear();
        if (text.empty()) return kLoaded;
        static const char hdr[] = "LCESRVIDS 1\n";
        const size_t hl = sizeof hdr - 1;
        if (text.size() < hl + 72 || text.compare(0, hl, hdr) || text.back() != '\n') return kCorrupt;
        const size_t body = text.size() - 72;
        if (text.compare(body, 7, "sha256 ")) return kCorrupt;
        uint8_t want[32], got[32];
        for (int i = 0; i < 32; ++i) {
            int hi = hexval(text[body + 7 + 2 * i]), lo = hexval(text[body + 8 + 2 * i]);
            if (hi < 0 || lo < 0) return kCorrupt;
            want[i] = (uint8_t)(hi << 4 | lo);
        }
        Sha256 h; h.init(); h.update(text.data(), body); h.final(got);
        if (memcmp(want, got, 32)) return kBadHash;
        size_t pos = hl;
        while (pos < body) {
            size_t nl = text.find('\n', pos);
            if (nl == std::string::npos || nl > body) { entries.clear(); return kCorrupt; }
            Entry e;
            if (!parse_line(text.substr(pos, nl - pos), e) || !add(e)) { entries.clear(); return kCorrupt; }
            pos = nl + 1;
        }
        return kLoaded;
    }

private:
    static int hexval(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    }
    static bool num(const std::string &t, uint32_t max, uint32_t &out) {
        if (t.empty() || t.size() > 5 || (t.size() > 1 && t[0] == '0')) return false;
        uint32_t v = 0;
        for (char c : t) { if (c < '0' || c > '9') return false; v = v * 10 + (uint32_t)(c - '0'); }
        if (v > max) return false;
        out = v; return true;
    }
    static bool parse_line(const std::string &line, Entry &e) {
        std::vector<std::string> f;
        size_t p = 0;
        while (p <= line.size()) {
            size_t sp = line.find(' ', p);
            if (sp == std::string::npos) sp = line.size();
            f.push_back(line.substr(p, sp - p));
            p = sp + 1;
        }
        if (f.size() != 8) return false;
        memset(&e, 0, sizeof e);
        if (f[0] == "item") e.kind = kItem; else if (f[0] == "block") e.kind = kBlock; else return false;
        if (!set_key(e, f[1].c_str())) return false;
        uint32_t id, ms, mat, a, b, fl;
        if (!num(f[2], 0xffff, id) || !num(f[3], 64, ms) || !num(f[4], 255, mat) || !num(f[5], 0xffff, a) ||
            !num(f[6], 0xffff, b) || !num(f[7], 0xff, fl))
            return false;
        e.id = (uint16_t)id; e.max_stack = (uint8_t)ms; e.material = (uint8_t)mat; e.a = (uint16_t)a; e.b = (uint16_t)b;
        e.flags = (uint8_t)fl;
        // Same validity rules as an entry received from the wire.
        uint8_t buf[kEntryFixed + kMaxKey]; Writer w; w.init(buf, sizeof buf); put_entry(w, e);
        Reader r; r.init(buf, w.n); Entry back;
        return w.ok && get_entry(r, back) && !(e.flags & kLive);
    }
};

// ---- HELLO reassembly ---------------------------------------------------------
class Reassembler {
public:
    enum Status { kNeedMore, kComplete, kError };
    uint16_t error = kOk;
    std::string detail;
    HelloInfo info{};
    uint32_t session = 0;
    std::vector<Entry> entries;

    Status feed(const uint8_t *p, size_t n) {
        if (state_ != kNeedMore) return state_;
        Reader r; r.init(p, n);
        Header h;
        if (!get_header(r, h)) return fail(kMalformed, "truncated header");
        if (h.version != kVersion) return fail(kBadVersion, "protocol version " + std::to_string(h.version));
        if (h.type != kHello) return fail(kMalformed, "unexpected message type " + std::to_string(h.type));
        if (h.count == 0 || h.count > kMaxChunks) return fail(kMalformed, "bad chunk count");
        if (h.index != next_) return fail(kMalformed, "chunk " + std::to_string(h.index) + " out of order");
        if (h.index == 0) {
            count_ = h.count; session = h.session;
            info.api_version = r.u32(); info.flags = (uint8_t)r.u8(); info.total = (uint16_t)r.u16(); r.bytes(info.digest, 32);
            if (!r.ok) return fail(kMalformed, "truncated HELLO");
            if (info.total > kMaxEntries) return fail(kLimit, "too many entries");
        } else if (h.count != count_ || h.session != session) {
            return fail(kMalformed, "chunk header mismatch");
        }
        unsigned k = r.u16();
        if (!r.ok) return fail(kMalformed, "truncated chunk");
        for (unsigned i = 0; i < k; ++i) {
            Entry e;
            if (!get_entry(r, e)) return fail(kMalformed, "bad entry");
            if (entries.size() >= info.total) return fail(kMalformed, "more entries than announced");
            entries.push_back(e);
        }
        if (r.left()) return fail(kMalformed, "trailing bytes");
        if (++next_ < count_) return kNeedMore;
        if (entries.size() != info.total) return fail(kMalformed, "entry count mismatch");
        uint8_t d[32];
        Digest dg; dg.init();
        for (const Entry &e : entries) dg.add(e);
        dg.final(d);
        if (memcmp(d, info.digest, 32)) return fail(kMalformed, "digest mismatch");
        ServerLedger dup;
        for (const Entry &e : entries)
            if (!dup.add(e)) return fail(kDuplicate, std::string("duplicate key or ID: ") + e.key);
        state_ = kComplete;
        return state_;
    }

private:
    Status fail(uint16_t code, const std::string &why) { error = code; detail = why; state_ = kError; return state_; }
    Status state_ = kNeedMore;
    unsigned next_ = 0, count_ = 0;
};

// ---- merge ----------------------------------------------------------------------
struct MergeResult {
    uint8_t verdict = kAccept;
    uint16_t code = kOk;
    std::string message;               // ASCII, <= kMaxMessage, shown to the player
    std::vector<std::string> log;      // full detail for the server log
    std::vector<Entry> adopt;          // entries to append to the world ledger (accept only)
    std::vector<uint16_t> allowed;     // custom item-space IDs this client may receive
    unsigned missing_items = 0;        // server item entries the client lacks (allow_missing_items)
};

// `builtin(e)` returns true when e's ID is already used by the server's own
// (non-placeholder) items/tiles.
inline MergeResult merge(const ServerLedger &server, const std::vector<Entry> &client, const Policy &policy,
                         const std::function<bool(const Entry &)> &builtin) {
    MergeResult r;
    struct Fail { uint16_t code; std::string msg; };
    std::vector<Fail> fails;
    auto desc = [](const Entry &e) { return std::string(kind_name(e.kind)) + " " + e.key + " (" + std::to_string(e.id) + ")"; };
    ServerLedger after = server;
    std::set<std::string> missing_mods;
    unsigned missing_blocks = 0;

    for (const Entry &c : client) {
        if (!(c.flags & kLive)) {
            const Entry *s = server.find(c.kind, c.key);
            if (s && s->id != c.id)
                r.log.push_back("note: unloaded client entry " + desc(c) + " differs from server ID " + std::to_string(s->id));
            continue;
        }
        if (const Entry *s = server.find(c.kind, c.key)) {
            if (s->id != c.id) {
                fails.push_back({kKeyIdConflict, std::string(c.key) + " is " + kind_name(c.kind) + " " + std::to_string(s->id) +
                                                     " on this server but " + std::to_string(c.id) + " on your console"});
            } else if (!props_equal(*s, c)) {
                fails.push_back({kPropsMismatch, std::string(c.key) + " has different properties (stack " + std::to_string(c.max_stack) +
                                                     " vs " + std::to_string(s->max_stack) + "): different mod version"});
            } else {
                r.allowed.push_back((uint16_t)item_space_id(c));
            }
            continue;
        }
        const Entry *other = server.find_item_space(item_space_id(c));
        if (!other && c.kind == kBlock) other = server.find_block(c.id);
        if (other) {
            fails.push_back({kIdTaken, std::string(kind_name(c.kind)) + " ID " + std::to_string(c.id) + " is " + other->key +
                                           " on this server but " + c.key + " on your console"});
            continue;
        }
        if (builtin(c)) {
            fails.push_back({kIdBuiltin, desc(c) + " uses an ID this server already uses for built-in content"});
            continue;
        }
        if (!policy.adopt) {
            fails.push_back({kNotAllowed, std::string(c.key) + " is not installed on this server (server mod list is locked)"});
            continue;
        }
        if (!after.add(c)) {
            fails.push_back({kLimit, "server mod ID table is full"});
            continue;
        }
        r.adopt.push_back(after.entries.back());
        r.allowed.push_back((uint16_t)item_space_id(c));
        r.log.push_back("adopt " + desc(c));
    }

    for (const Entry &s : server.entries) {
        bool have = false;
        for (const Entry &c : client)
            if ((c.flags & kLive) && c.kind == s.kind && c.id == s.id && !strcmp(c.key, s.key)) { have = true; break; }
        if (have) continue;
        std::string key = s.key;
        missing_mods.insert(key.substr(0, key.find(':')));
        if (block_placeable(s)) ++missing_blocks; else ++r.missing_items;
        r.log.push_back("client lacks " + desc(s));
    }
    if (!missing_mods.empty() && (missing_blocks || !policy.allow_missing_items)) {
        std::string list;
        unsigned shown = 0;
        for (const std::string &m : missing_mods) {
            if (shown == 4) { list += ", ..."; break; }
            list += (shown++ ? ", " : "") + m;
        }
        fails.push_back({kMissing, "this world uses mod content you do not have: " + list});
    }

    if (fails.empty()) {
        r.message = r.adopt.empty() ? "mod IDs match this world" : "mod IDs accepted; " + std::to_string(r.adopt.size()) + " new ID(s) recorded for this world";
        if (r.missing_items) r.message += "; " + std::to_string(r.missing_items) + " item(s) from mods you lack are hidden";
        return r;
    }
    r.verdict = kReject;
    r.code = fails[0].code;
    r.adopt.clear();
    r.allowed.clear();
    r.message = "Refused to protect your items: " + fails[0].msg + ".";
    if (fails.size() > 1) r.message += " (+" + std::to_string(fails.size() - 1) + " more; see server log)";
    for (const Fail &f : fails) r.log.push_back("refuse code " + std::to_string(f.code) + ": " + f.msg);
    for (char &ch : r.message) if ((uint8_t)ch < 0x20 || (uint8_t)ch > 0x7e) ch = '?';
    if (r.message.size() > kMaxMessage) r.message.resize(kMaxMessage);
    return r;
}

// ---- server -> client messages -----------------------------------------------
inline std::vector<uint8_t> encode_request(uint32_t session, const ServerLedger &ledger, const Policy &policy) {
    std::vector<uint8_t> buf(kHeaderSize + 2 + 32 + 1);
    Writer w; w.init(buf.data(), buf.size());
    put_header(w, kRequest, session, 0, 1);
    w.u16((uint32_t)ledger.entries.size());
    uint8_t d[32]; ledger.digest(d); w.bytes(d, 32);
    w.u8((policy.adopt ? kPolicyAdopt : 0) | (policy.allow_missing_items ? kPolicyAllowMissingItems : 0));
    buf.resize(w.n);
    return buf;
}

// RESULT followed by the server's authoritative table (TABLE chunks).
inline std::vector<std::vector<uint8_t>> encode_result(uint32_t session, uint8_t verdict, uint16_t code, const std::string &message,
                                                       const ServerLedger &ledger) {
    std::vector<std::vector<uint8_t>> out;
    unsigned first[kMaxChunks + 1];
    const Entry *es = ledger.entries.empty() ? nullptr : ledger.entries.data();
    unsigned chunks = ledger.entries.empty() ? 0 : plan_chunks(es, (unsigned)ledger.entries.size(), 0, first, kMaxChunks);
    first[chunks] = (unsigned)ledger.entries.size();
    std::string msg = message.substr(0, kMaxMessage);
    std::vector<uint8_t> res(kHeaderSize + 5 + msg.size() + 2 + 32 + 1);
    Writer w; w.init(res.data(), res.size());
    put_header(w, kResult, session, 0, 1);
    w.u8(verdict); w.u16(code); w.u16((uint32_t)msg.size()); w.bytes(msg.data(), msg.size());
    w.u16((uint32_t)ledger.entries.size());
    uint8_t d[32]; ledger.digest(d); w.bytes(d, 32);
    w.u8(chunks);
    res.resize(w.n);
    out.push_back(res);
    for (unsigned c = 0; c < chunks; ++c) {
        std::vector<uint8_t> t(kChunkBudget);
        Writer tw; tw.init(t.data(), t.size());
        put_header(tw, kTable, session, c, chunks);
        tw.u16(first[c + 1] - first[c]);
        for (unsigned i = first[c]; i < first[c + 1]; ++i) put_entry(tw, ledger.entries[i]);
        t.resize(tw.n);
        out.push_back(t);
    }
    return out;
}

// Chat-sized lines (the PS4 chat line is short); splits at spaces.
inline std::vector<std::string> chat_lines(const std::string &text, size_t width = 60) {
    std::vector<std::string> lines;
    std::string cur;
    size_t p = 0;
    while (p < text.size()) {
        size_t sp = text.find(' ', p);
        if (sp == std::string::npos) sp = text.size();
        std::string word = text.substr(p, sp - p);
        while (word.size() > width) { if (!cur.empty()) { lines.push_back(cur); cur.clear(); } lines.push_back(word.substr(0, width)); word = word.substr(width); }
        if (!cur.empty() && cur.size() + 1 + word.size() > width) { lines.push_back(cur); cur.clear(); }
        if (!word.empty()) cur += (cur.empty() ? "" : " ") + word;
        p = sp + 1;
    }
    if (!cur.empty()) lines.push_back(cur);
    return lines;
}

}  // namespace lce_modids
