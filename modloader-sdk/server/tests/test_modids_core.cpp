// Host tests for the server core (LceModIdCore.h): wire codec, SHA-256,
// world ledger persistence, HELLO reassembly, merge rules and the
// server->client encoders, cross-checked against the PS4 client module.
#include "testkit.h"
#include "LceModIdCore.h"
#include "../../modules/netsync195/netsync_client.h"

using namespace lce_modids;
using tk::item;
using tk::block;

static std::string hex(const uint8_t d[32]) {
    static const char hx[] = "0123456789abcdef";
    std::string s;
    for (int i = 0; i < 32; ++i) { s += hx[d[i] >> 4]; s += hx[d[i] & 15]; }
    return s;
}
static std::string sha(const std::string &s) {
    Sha256 h; h.init(); h.update(s.data(), s.size()); uint8_t d[32]; h.final(d); return hex(d);
}

static void test_sha256() {
    CHECK(sha("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    Sha256 h; h.init();
    std::string a(1000, 'a');
    for (int i = 0; i < 1000; ++i) h.update(a.data(), a.size());
    uint8_t d[32]; h.final(d);
    CHECK(hex(d) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

static void test_digest_matches_ledger_text() {
    std::vector<Entry> es = {item("rubymod:ruby", 2300), block("rubymod:ruby_block", 254), item("gem:sapphire", 2301)};
    Digest d; d.init();
    for (auto &e : es) d.add(e);
    uint8_t out[32]; d.final(out);
    CHECK(hex(out) == sha("LCEIDS 1\nitem rubymod:ruby 2300\nblock rubymod:ruby_block 254\nitem gem:sapphire 2301\n"));
}

static void test_entry_codec() {
    Entry e = block("mod_a:some/path-1.b#x", 300, 2550);
    e.material = 5; e.flags |= kTransparent;
    uint8_t buf[128]; Writer w; w.init(buf, sizeof buf); put_entry(w, e);
    CHECK(w.ok); CHECK_EQ(w.n, kEntryFixed + e.key_len);
    Reader r; r.init(buf, w.n); Entry back;
    CHECK(get_entry(r, back)); CHECK_EQ(r.left(), 0);
    CHECK(!memcmp(&back, &e, sizeof e));
    // Every truncation fails cleanly.
    for (size_t n = 0; n < w.n; ++n) { Reader t; t.init(buf, n); Entry x; CHECK(!get_entry(t, x)); }
    auto bad = [&](void (*mut)(Entry &)) {
        Entry m = item("a1:ok", 2300); mut(m);
        uint8_t b[128]; Writer ww; ww.init(b, sizeof b); put_entry(ww, m);
        Reader rr; rr.init(b, ww.n); Entry x; return !get_entry(rr, x);
    };
    CHECK(bad([](Entry &m) { m.kind = 2; }));
    CHECK(bad([](Entry &m) { m.max_stack = 0; }));
    CHECK(bad([](Entry &m) { m.max_stack = 65; }));
    CHECK(bad([](Entry &m) { m.id = 255; }));
    CHECK(bad([](Entry &m) { m.id = 32000; }));
    CHECK(bad([](Entry &m) { m.flags = 0x10; }));
    CHECK(bad([](Entry &m) { m.key[0] = 'A'; }));
    CHECK(bad([](Entry &m) { memcpy(m.key, "a1:x..y", 8); m.key_len = 7; }));
    CHECK(bad([](Entry &m) { m.kind = kBlock; m.id = 0; }));
    CHECK(bad([](Entry &m) { m.kind = kBlock; m.id = 4096; }));
    CHECK(!bad([](Entry &m) { m.kind = kBlock; m.id = 1; }));
    Entry k; memset(&k, 0, sizeof k);
    CHECK(!set_key(k, "nocolon")); CHECK(!set_key(k, "a:b")); CHECK(!set_key(k, "ab:")); CHECK(set_key(k, "ab:c"));
    std::string longkey = "ab:" + std::string(61, 'x');
    CHECK(!set_key(k, longkey.c_str()));
    CHECK(set_key(k, longkey.substr(0, 63).c_str()));
    CHECK_EQ(block_item_id(254), 254); CHECK_EQ(block_item_id(300), 1068);
}

static void test_ledger_persistence() {
    ServerLedger l;
    CHECK(l.add(item("rubymod:ruby", 2300, 16, 0)));
    CHECK(l.add(block("rubymod:ore", 254, 300)));
    CHECK(l.add(block("rubymod:big", 300, 50)));
    CHECK(l.add(item("tools:pick", 2301, 1, 250)));
    CHECK(!l.add(item("rubymod:ruby", 2390)));          // same key
    CHECK(!l.add(item("x1:alias", 1068)));              // block 300's block-item
    CHECK(!l.add(block("x1:b", 254)));                  // same block
    std::string text = l.serialize();
    CHECK(text.rfind("LCESRVIDS 1\nitem rubymod:ruby 2300 16 0 0 0 0\n", 0) == 0);
    ServerLedger m;
    CHECK_EQ(m.load(text), ServerLedger::kLoaded);
    CHECK_EQ(m.entries.size(), 4);
    CHECK(m.serialize() == text);
    CHECK_EQ(m.load(""), ServerLedger::kLoaded); CHECK(m.entries.empty());
    // Any single byte flip is detected (hash or structure), never "repaired".
    for (size_t i = 0; i < text.size(); ++i) {
        std::string t = text; t[i] ^= 0x01;
        CHECK(m.load(t) != ServerLedger::kLoaded);
        CHECK(m.entries.empty());
    }
    auto rehash = [](const std::string &body) {
        std::string s = body; s += "sha256 " + sha(body) + "\n"; return s;
    };
    CHECK_EQ(m.load(rehash("LCESRVIDS 1\nitem a1:b 2300 64 0 0 0 0\n")), ServerLedger::kLoaded);
    CHECK_EQ(m.load(rehash("LCESRVIDS 1\nitem a1:b 2300 64 0 0 0 1\n")), ServerLedger::kCorrupt);   // live flag never stored
    CHECK_EQ(m.load(rehash("LCESRVIDS 1\nitem a1:b 02300 64 0 0 0 0\n")), ServerLedger::kCorrupt);  // non-canonical
    CHECK_EQ(m.load(rehash("LCESRVIDS 1\nitem a1:b 2300 65 0 0 0 0\n")), ServerLedger::kCorrupt);
    CHECK_EQ(m.load(rehash("LCESRVIDS 1\nitem a1:b 2300 64 0 0 0\n")), ServerLedger::kCorrupt);
    CHECK_EQ(m.load(rehash("LCESRVIDS 1\nitem a1:b 2300 64 0 0 0 0\nitem a1:b 2301 64 0 0 0 0\n")), ServerLedger::kCorrupt);
    CHECK_EQ(m.load(rehash("LCESRVIDS 1\nitem a1:b 2300 64 0 0 0 0\nitem a1:c 2300 64 0 0 0 0\n")), ServerLedger::kCorrupt);
    CHECK_EQ(m.load(rehash("LCEIDS 1\nitem a1:b 2300\n")), ServerLedger::kCorrupt);
    CHECK_EQ(m.load("LCESRVIDS 1\n"), ServerLedger::kCorrupt);
    std::string tampered = text; tampered.replace(text.find("2300"), 4, "2302");
    CHECK_EQ(m.load(tampered), ServerLedger::kBadHash);
}

// HELLO packets exactly as the PS4 module produces them.
static std::vector<std::vector<uint8_t>> hello_payloads(const std::vector<Entry> &es, uint32_t session) {
    static lce_netsync::Client c;
    std::vector<std::vector<uint8_t>> out;
    if (!c.configure(es.empty() ? nullptr : es.data(), (unsigned)es.size(), 0x10001, true)) return out;
    for (unsigned k = 0; k < c.hello_chunks(); ++k) {
        uint8_t pkt[kMaxPacket];
        size_t n = c.build_hello_packet(k, session, pkt, sizeof pkt);
        std::string ch; std::vector<uint8_t> payload;
        if (!n || !tk::parse_serverbound(pkt, n, ch, payload) || ch != "LCE|ModIds") return {};
        out.push_back(payload);
    }
    return out;
}

static std::vector<Entry> many(unsigned n, unsigned keypad = 0) {
    std::vector<Entry> es;
    for (unsigned i = 0; i < n; ++i) {
        std::string k = "m" + std::to_string(i % 7) + "x:" + std::string(keypad, 'k') + std::to_string(i);
        es.push_back(item(k.substr(0, 63).c_str(), (uint16_t)(2283 + i)));
    }
    return es;
}

static void test_reassembly() {
    for (unsigned n : {0u, 1u, 40u, 500u}) {
        auto es = many(n, n == 500 ? 50 : 0);
        auto ps = hello_payloads(es, 0x1234567);
        CHECK(!ps.empty());
        if (n == 500) CHECK(ps.size() > 5);
        Reassembler r;
        Reassembler::Status st = Reassembler::kNeedMore;
        for (size_t i = 0; i < ps.size(); ++i) {
            st = r.feed(ps[i].data(), ps[i].size());
            CHECK(st == (i + 1 == ps.size() ? Reassembler::kComplete : Reassembler::kNeedMore));
        }
        CHECK_EQ(r.entries.size(), n);
        CHECK_EQ(r.session, 0x1234567);
        CHECK_EQ(r.info.api_version, 0x10001);
        bool same = r.entries.size() == es.size();
        for (size_t i = 0; same && i < es.size(); ++i) same = !memcmp(&r.entries[i], &es[i], sizeof(Entry));
        CHECK(same);
        CHECK(r.feed(ps[0].data(), ps[0].size()) == Reassembler::kComplete);  // sticky
    }
    auto es = many(500, 50);
    auto ps = hello_payloads(es, 7);
    {   Reassembler r; CHECK(r.feed(ps[1].data(), ps[1].size()) == Reassembler::kError); CHECK_EQ(r.error, kMalformed); }
    {   Reassembler r; r.feed(ps[0].data(), ps[0].size());
        CHECK(r.feed(ps[2].data(), ps[2].size()) == Reassembler::kError);
        CHECK(r.feed(ps[1].data(), ps[1].size()) == Reassembler::kError); }  // errors are sticky
    {   auto p = ps[0]; p[0] = 2; Reassembler r; CHECK(r.feed(p.data(), p.size()) == Reassembler::kError); CHECK_EQ(r.error, kBadVersion); }
    {   auto p = ps[0]; p[1] = kTable; Reassembler r; CHECK(r.feed(p.data(), p.size()) == Reassembler::kError); }
    {   auto p1 = ps[1]; p1[5] ^= 1; Reassembler r; r.feed(ps[0].data(), ps[0].size()); CHECK(r.feed(p1.data(), p1.size()) == Reassembler::kError); }  // session changes
    {   auto p = ps[0]; p.push_back(0); Reassembler r; CHECK(r.feed(p.data(), p.size()) == Reassembler::kError); }
    {   // digest tamper in chunk 0
        auto p = ps[0]; p[kHeaderSize + 7 + 3] ^= 0x80;
        Reassembler r; for (size_t i = 0; i < ps.size(); ++i) r.feed(i ? ps[i].data() : p.data(), i ? ps[i].size() : p.size());
        CHECK_EQ(r.error, kMalformed); CHECK(r.detail == "digest mismatch");
    }
    {   // duplicate keys/IDs that a broken client could send: build by hand
        std::vector<Entry> d = {item("aa:x", 2300), item("aa:x", 2301)};
        uint8_t buf[512]; Writer w; w.init(buf, sizeof buf);
        put_header(w, kHello, 9, 0, 1); w.u32(1); w.u8(1); w.u16(2);
        Digest dg; dg.init(); for (auto &e : d) dg.add(e); uint8_t dd[32]; dg.final(dd); w.bytes(dd, 32);
        w.u16(2); for (auto &e : d) put_entry(w, e);
        Reassembler r; CHECK(r.feed(buf, w.n) == Reassembler::kError); CHECK_EQ(r.error, kDuplicate);
        Writer w2; w2.init(buf, sizeof buf);
        put_header(w2, kHello, 9, 0, 1); w2.u32(1); w2.u8(1); w2.u16(3000);
        Reassembler r2; CHECK(r2.feed(buf, w2.n + 32) == Reassembler::kError);
    }
    // Fuzz: random corruption of a valid multi-chunk HELLO never crashes and
    // never yields entries that fail validation.
    tk::Rng rng{0x9e3779b97f4a7c15ull};
    unsigned completes = 0;
    for (int iter = 0; iter < 3000; ++iter) {
        Reassembler r;
        for (size_t i = 0; i < ps.size(); ++i) {
            auto p = ps[i];
            unsigned flips = iter % 10 == 0 ? 0 : rng.next() % 3;
            for (unsigned f = 0; f < flips; ++f) p[rng.next() % p.size()] ^= (uint8_t)(1u << (rng.next() % 8));
            if (flips && rng.next() % 50 == 0) p.resize(rng.next() % p.size());
            if (r.feed(p.data(), p.size()) != Reassembler::kNeedMore) break;
        }
        if (!r.entries.empty() && r.error == kOk) {
            ++completes;
            ServerLedger chk; bool ok = true;
            for (auto &e : r.entries) ok = ok && chk.add(e);
            CHECK(ok);
        }
    }
    CHECK(completes > 0);  // unflipped runs complete
}

static auto no_builtin = [](const Entry &) { return false; };

static void test_merge() {
    Policy adopt;
    ServerLedger empty;
    {   // first client teaches the world its IDs
        std::vector<Entry> c = {item("rubymod:ruby", 2300), block("rubymod:ore", 254), block("rubymod:big", 300), item("old:gone", 2301, 64, 0, false)};
        auto m = merge(empty, c, adopt, no_builtin);
        CHECK_EQ(m.verdict, kAccept); CHECK_EQ(m.adopt.size(), 3);
        CHECK_EQ(m.allowed.size(), 3);
        CHECK(std::find(m.allowed.begin(), m.allowed.end(), 1068) != m.allowed.end());  // block-item of 300
        CHECK(m.message.find("3 new ID") != std::string::npos);
        for (auto &e : m.adopt) CHECK(!(e.flags & kLive));
    }
    ServerLedger s;
    s.add(item("rubymod:ruby", 2300, 16)); s.add(block("rubymod:ore", 254)); s.add(item("tools:pick", 2301, 1, 250));
    std::vector<Entry> same = {item("rubymod:ruby", 2300, 16), block("rubymod:ore", 254), item("tools:pick", 2301, 1, 250)};
    {   auto m = merge(s, same, adopt, no_builtin);
        CHECK_EQ(m.verdict, kAccept); CHECK(m.adopt.empty()); CHECK_EQ(m.allowed.size(), 3); CHECK(m.message == "mod IDs match this world"); }
    {   auto c = same; c[0].id = 2302;
        auto m = merge(s, c, adopt, no_builtin);
        CHECK_EQ(m.verdict, kReject); CHECK_EQ(m.code, kKeyIdConflict);
        CHECK(m.message.find("rubymod:ruby is item 2300 on this server but 2302 on your console") != std::string::npos);
        CHECK(m.adopt.empty()); CHECK(m.allowed.empty()); }
    {   auto c = same; c.push_back(item("other:thing", 2301));   // ID owned by tools:pick
        c.erase(c.begin() + 2);
        auto m = merge(s, c, adopt, no_builtin);
        CHECK_EQ(m.verdict, kReject); CHECK_EQ(m.code, kIdTaken);
        CHECK(m.message.find("tools:pick") != std::string::npos); }
    {   ServerLedger s2; s2.add(item("x1:alias", 1068));
        std::vector<Entry> c = {block("y1:big", 300)};
        auto m = merge(s2, c, adopt, no_builtin);
        CHECK_EQ(m.code, kIdTaken); }   // block-item aliasing is a conflict
    {   auto c = same; c[2].a = 300;
        auto m = merge(s, c, adopt, no_builtin);
        CHECK_EQ(m.code, kPropsMismatch); }
    {   auto c = same; c[1].flags &= (uint8_t)~kBlockHasItem;
        CHECK_EQ(merge(s, c, adopt, no_builtin).code, kPropsMismatch); }
    {   auto c = same; c.push_back(item("new:thing", 2310));
        auto m = merge(s, c, adopt, [](const Entry &e) { return e.id == 2310; });
        CHECK_EQ(m.code, kIdBuiltin); }
    {   Policy locked; locked.adopt = false;
        auto c = same; c.push_back(item("new:thing", 2310));
        CHECK_EQ(merge(s, c, locked, no_builtin).code, kNotAllowed);
        CHECK_EQ(merge(s, same, locked, no_builtin).verdict, kAccept); }
    {   // client lacks content the world uses
        std::vector<Entry> c = {item("rubymod:ruby", 2300, 16), block("rubymod:ore", 254)};
        auto m = merge(s, c, adopt, no_builtin);
        CHECK_EQ(m.code, kMissing); CHECK(m.message.find("tools") != std::string::npos);
        Policy lenient; lenient.allow_missing_items = true;
        m = merge(s, c, lenient, no_builtin);
        CHECK_EQ(m.verdict, kAccept); CHECK_EQ(m.missing_items, 1); CHECK_EQ(m.allowed.size(), 2);
        CHECK(std::find(m.allowed.begin(), m.allowed.end(), 2301) == m.allowed.end());
        // a missing PLACEABLE block is never waived (chunks would carry it)
        std::vector<Entry> c2 = {item("rubymod:ruby", 2300, 16), item("tools:pick", 2301, 1, 250)};
        CHECK_EQ(merge(s, c2, lenient, no_builtin).code, kMissing);
        // a missing block >= 256 is item-only on this server: waivable
        ServerLedger s3; s3.add(block("big:b", 300));
        auto m3 = merge(s3, {}, lenient, no_builtin);
        CHECK_EQ(m3.verdict, kAccept); CHECK_EQ(m3.missing_items, 1);
        // a vanilla client (empty HELLO) on a modded world
        CHECK_EQ(merge(s, {}, adopt, no_builtin).code, kMissing);
    }
    {   // unloaded (non-live) client entries are neither adopted nor conflicts
        std::vector<Entry> c = same;
        c.push_back(item("rubymod:ruby_old", 2390, 64, 0, false));
        Entry stale = item("tools:pick", 2399, 1, 250, false);
        std::vector<Entry> c2 = {item("rubymod:ruby", 2300, 16), block("rubymod:ore", 254), stale};
        auto m = merge(s, c, adopt, no_builtin);
        CHECK_EQ(m.verdict, kAccept); CHECK(m.adopt.empty());
        auto m2 = merge(s, c2, adopt, no_builtin);
        CHECK_EQ(m2.code, kMissing);   // stale entry does not count as having it
    }
    {   // several failures: first reported, the rest counted; message is printable ASCII and bounded
        auto c = same; c[0].id = 2302; c[2].a = 1;
        std::string longkey = "zz:" + std::string(60, 'q');
        c.push_back(item(longkey.c_str(), 2350));
        auto m = merge(s, c, adopt, [](const Entry &e) { return e.id == 2350; });
        CHECK_EQ(m.code, kKeyIdConflict);
        CHECK(m.message.find("(+3 more; see server log)") != std::string::npos);
        CHECK(m.message.size() <= kMaxMessage);
        for (char ch : m.message) CHECK(ch >= 0x20 && ch <= 0x7e);
        unsigned refusals = 0; for (auto &l : m.log) refusals += l.rfind("refuse", 0) == 0;
        CHECK_EQ(refusals, 4);   // conflict, props, builtin, and the conflicting/mismatched server entries count as missing
    }
    {   // capacity
        ServerLedger full;
        for (unsigned i = 0; i < kMaxEntries; ++i) {
            std::string k = "aa:i" + std::to_string(i);
            full.add(item(k.c_str(), (uint16_t)(4000 + i)));
        }
        CHECK_EQ(full.entries.size(), kMaxEntries);
        std::vector<Entry> c;
        for (auto e : full.entries) { e.flags |= kLive; c.push_back(e); }
        c.push_back(item("bb:new", 2300));
        CHECK_EQ(merge(full, c, adopt, no_builtin).code, kLimit);
    }
}

static void test_server_to_client() {
    ServerLedger s;
    for (unsigned i = 0; i < 600; ++i) {
        std::string k = "mod" + std::to_string(i % 5) + ":" + std::string(40, 'v') + std::to_string(i);
        s.add(item(k.c_str(), (uint16_t)(2283 + i), 64, (uint16_t)(i % 3)));
    }
    s.add(block("blk:a", 254));
    auto req = encode_request(77, s, Policy());
    CHECK_EQ(req.size(), kHeaderSize + 35);
    auto frames = encode_result(77, kAccept, kOk, "fine", s);
    CHECK(frames.size() > 8);
    for (auto &f : frames) CHECK(f.size() <= kChunkBudget);
    // Client with the same live content sees a verified, matching table.
    std::vector<Entry> local;
    for (auto e : s.entries) { e.flags |= kLive; local.push_back(e); }
    static lce_netsync::Client c;
    CHECK(c.configure(local.data(), (unsigned)local.size(), 1, true));
    CHECK(c.on_frame(tk::clientbound(req).data(), tk::clientbound(req).size()));
    std::vector<std::vector<uint8_t>> sent;
    auto send = [](void *ctx, const uint8_t *p, size_t n) -> int {
        static_cast<std::vector<std::vector<uint8_t>> *>(ctx)->emplace_back(p, p + n); return 1; };
    c.pump(true, send, &sent, nullptr, nullptr);
    CHECK_EQ(sent.size(), c.hello_chunks());
    for (auto &p : sent) CHECK(p.size() <= kMaxPacket);
    for (auto &f : frames) { auto fr = tk::clientbound(f); CHECK(c.on_frame(fr.data(), fr.size())); }
    CHECK(c.pump(true, send, &sent, nullptr, nullptr));
    CHECK(c.report().state == lce_netsync::State::Accepted);
    CHECK(c.report().table_ok);
    CHECK_EQ(c.report().server_entries, 601);
    CHECK_EQ(c.report().table_matches, 601);
    CHECK_EQ(c.report().table_conflicts, 0);
    CHECK(!strcmp(c.report().message, "fine"));
}

static void test_chat_lines() {
    auto l = chat_lines("[Mods] Refused to protect your items: rubymod:ruby is item 2300 on this server but 2302 on your console.", 40);
    CHECK(l.size() >= 3);
    for (auto &x : l) CHECK(x.size() <= 40);
    std::string joined; for (auto &x : l) joined += (joined.empty() ? "" : " ") + x;
    CHECK(joined == "[Mods] Refused to protect your items: rubymod:ruby is item 2300 on this server but 2302 on your console.");
    auto w = chat_lines(std::string(130, 'x'), 60);
    CHECK_EQ(w.size(), 3); CHECK_EQ(w[2].size(), 10);
}

int main() {
    test_sha256();
    test_digest_matches_ledger_text();
    test_entry_codec();
    test_ledger_persistence();
    test_reassembly();
    test_merge();
    test_server_to_client();
    test_chat_lines();
    TEST_MAIN_END("test_modids_core");
}
