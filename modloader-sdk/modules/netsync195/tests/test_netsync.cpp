// Host tests for the netsync195 client module: packet bytes, limits, SDK
// entry extraction (digest == the real SDK ledger trailer), the full
// handshake against the server core, frame matching, reset and failure
// paths, the C entry points, a fuzz pass and a two-thread stress run.
#include <thread>
#include <atomic>
#include "../../../server/tests/testkit.h"
#include "../../../server/new/Minecraft.World/LceModIdCore.h"
#include "../netsync_client.h"
#include "../netsync_sdk.h"
#include "../../../loader/loader.h"

using namespace lce_modids;
using tk::item;
using tk::block;

extern "C" int lce_netsync_configure_from_loader(const lce_loader *, int, void (*)(const char *));
extern "C" int lce_netsync_inbound_frame(const uint8_t *, size_t);
extern "C" void lce_netsync_pump(int, int (*)(void *, const uint8_t *, size_t), void *);
extern "C" unsigned lce_netsync_state(void);
extern "C" const char *lce_netsync_message(void);

typedef std::vector<std::vector<uint8_t>> Packets;
static int capture(void *ctx, const uint8_t *p, size_t n) { static_cast<Packets *>(ctx)->emplace_back(p, p + n); return 1; }
static int refuse(void *, const uint8_t *, size_t) { return 0; }
static std::vector<std::string> g_log;
static void logcb(void *, const char *l) { g_log.push_back(l); }

static lce_netsync::Client g_c;   // ~160 KB: static

static void test_golden_packet() {
    Entry e = item("ab:c", 2300);
    CHECK(g_c.configure(&e, 1, 0x00010001, true));
    CHECK_EQ(g_c.hello_chunks(), 1);
    uint8_t pkt[kMaxPacket];
    size_t n = g_c.build_hello_packet(0, 0xA1B2C3D4, pkt, sizeof pkt);
    std::vector<uint8_t> want = {225, 0, 10};
    for (char ch : std::string("LCE|ModIds")) { want.push_back(0); want.push_back((uint8_t)ch); }
    size_t body = kHeaderSize + kHelloPrefix + 2 + kEntryFixed + 4;
    want.push_back((uint8_t)(body >> 8)); want.push_back((uint8_t)body);
    std::vector<uint8_t> payload = {1, kHello, 0xA1, 0xB2, 0xC3, 0xD4, 0, 0, 0, 1, 0, 1, 0, 1, 1, 0, 1};
    want.insert(want.end(), payload.begin(), payload.end());
    const uint8_t *d = g_c.digest();
    want.insert(want.end(), d, d + 32);
    // u16 entry count, then kind, flags(live), u16 id, max_stack, material, u16 a, u16 b, key_len, key
    std::vector<uint8_t> ent = {0, 1, 0, 1, 2300 >> 8, 2300 & 255, 64, 0, 0, 0, 0, 0, 4, 'a', 'b', ':', 'c'};
    want.insert(want.end(), ent.begin(), ent.end());
    CHECK_EQ(n, want.size());
    CHECK(n == want.size() && !memcmp(pkt, want.data(), n));
    // digest of one entry == SHA-256 of its LCEIDS text
    Sha256 h; h.init(); const char *t = "LCEIDS 1\nitem ab:c 2300\n"; h.update(t, strlen(t)); uint8_t dd[32]; h.final(dd);
    CHECK(!memcmp(dd, d, 32));
    CHECK_EQ(g_c.build_hello_packet(1, 1, pkt, sizeof pkt), 0);
    CHECK_EQ(g_c.build_hello_packet(0, 1, pkt, 20), 0);
}

static void test_limits_and_validation() {
    // worst case: 2048 entries with 63-byte keys
    static Entry es[kMaxEntries];
    for (unsigned i = 0; i < kMaxEntries; ++i) {
        std::string k = "m" + std::to_string(i % 10) + "x:" + std::to_string(i);
        k += std::string(63 - k.size(), 'z');
        es[i] = item(k.c_str(), (uint16_t)(4000 + i));
    }
    CHECK(g_c.configure(es, kMaxEntries, 1, true));
    CHECK(g_c.hello_chunks() > 30 && g_c.hello_chunks() <= kMaxChunks);
    size_t total = 0;
    for (unsigned k = 0; k < g_c.hello_chunks(); ++k) {
        uint8_t pkt[kMaxPacket]; size_t n = g_c.build_hello_packet(k, 5, pkt, sizeof pkt);
        CHECK(n > 0 && n <= kMaxPacket);
        total += n;
    }
    CHECK(total > 2048 * 74);
    CHECK(!g_c.configure(es, kMaxEntries + 1, 1, true));
    Entry d[2] = {item("aa:x", 2300), item("aa:x", 2301)};
    CHECK(!g_c.configure(d, 2, 1, true));                     // duplicate key
    Entry d2[2] = {block("aa:b", 300), item("aa:i", 1068)};
    CHECK(!g_c.configure(d2, 2, 1, true));                    // block-item alias
    Entry bad = item("aa:x", 2300); bad.max_stack = 0;
    CHECK(!g_c.configure(&bad, 1, 1, true));
    CHECK(g_c.configure(nullptr, 0, 1, false));
    CHECK_EQ(g_c.hello_chunks(), 1);
}

// The SDK ledger as the loader writes it, plus a committed plan.
// Effect/enchant/biome ledger kinds are not on the LCE|ModIds v1 wire: never sent (no aliasing with items).
static void test_entries_skip_ext_kinds() {
    static lce_ledger l; lce_ledger_init(&l);
    auto add = [&](uint8_t k, const char *key, uint32_t id) {
        lce_ledger_entry &e = l.entries[l.count++]; memset(&e, 0, sizeof e);
        e.kind = k; e.id = id; strcpy(e.key, key);
    };
    add(LCE_KIND_BIOME, "gem:fields", 168);
    add(LCE_KIND_ITEM, "gem:gem", 2283);
    add(LCE_KIND_EFFECT, "gem:glow", 32);
    add(LCE_KIND_ENCHANT, "gem:edge", 100);
    add(LCE_KIND_BLOCK, "gem:ore", 254);
    Entry out[8];
    CHECK_EQ(lce_netsync::entries_from_sdk(&l, nullptr, false, out, 8), 2);
    CHECK_EQ(out[0].kind, kItem); CHECK_EQ(out[0].id, 2283);
    CHECK_EQ(out[1].kind, kBlock); CHECK_EQ(out[1].id, 254);
}

static void test_entries_from_sdk() {
    static lce_ledger l; lce_ledger_init(&l);
    auto add = [&](lce_id_kind k, const char *key, uint32_t id) {
        lce_ledger_entry &e = l.entries[l.count++]; memset(&e, 0, sizeof e);
        e.kind = (uint8_t)k; e.id = id; strcpy(e.key, key);
    };
    add(LCE_KIND_ITEM, "rubymod:ruby", 2283);
    add(LCE_KIND_BLOCK, "rubymod:ruby_block", 254);
    add(LCE_KIND_ITEM, "oldmod:gone", 2284);
    add(LCE_KIND_BLOCK, "rubymod:glass", 262);
    add(LCE_KIND_ITEM, "rubymod:pick", 2285);
    lce_item_desc ruby{}; ruby.max_stack = 0; ruby.kind = LCE_ITEM_GENERIC;
    lce_item_desc pick{}; pick.max_stack = 1; pick.max_damage = 250; pick.kind = LCE_ITEM_PICKAXE;
    lce_block_desc ore{}; ore.hardness_milli = 3000; ore.resistance_milli = 15000; ore.material = LCE_MAT_STONE;
    lce_block_desc glass{}; glass.hardness_milli = 900000; glass.material = LCE_MAT_GLASS; glass.flags = LCE_BLOCK_FLAG_TRANSPARENT | LCE_BLOCK_FLAG_NO_ITEM;
    lce_plan_item items[2]{}; items[0].desc = &ruby; items[0].id = 2283; items[1].desc = &pick; items[1].id = 2285;
    lce_plan_block blocks[2]{}; blocks[0].desc = &ore; blocks[0].id = 254; blocks[0].item_id = 254;
    blocks[1].desc = &glass; blocks[1].id = 262; blocks[1].item_id = 0;
    lce_plan plan{}; plan.items = items; plan.item_count = 2; plan.blocks = blocks; plan.block_count = 2;
    Entry out[8];
    CHECK_EQ(lce_netsync::entries_from_sdk(&l, &plan, true, out, 8), 5);
    CHECK(out[0].flags & kLive); CHECK_EQ(out[0].max_stack, 64); CHECK_EQ(out[0].a, 0);
    CHECK_EQ(out[1].kind, kBlock); CHECK_EQ(out[1].a, 300); CHECK_EQ(out[1].b, 1500); CHECK(out[1].flags & kBlockHasItem);
    CHECK(!(out[2].flags & kLive));
    CHECK(out[3].flags & kTransparent); CHECK(!(out[3].flags & kBlockHasItem)); CHECK_EQ(out[3].a, 0xfffe); CHECK_EQ(out[3].material, LCE_MAT_GLASS);
    CHECK_EQ(out[4].max_stack, 1); CHECK_EQ(out[4].a, 250); CHECK_EQ(out[4].material, LCE_ITEM_PICKAXE);
    CHECK_EQ(lce_netsync::entries_from_sdk(&l, &plan, false, out, 8), 5);
    for (int i = 0; i < 5; ++i) CHECK(!(out[i].flags & kLive));
    CHECK_EQ(lce_netsync::entries_from_sdk(&l, &plan, true, out, 4), -1);
    // Digest == the sha256 trailer the real SDK serializer writes.
    CHECK(g_c.configure(out, 5, 1, true));
    static char text[4096]; size_t len = 0;
    CHECK_EQ(lce_ledger_serialize(&l, text, sizeof text, &len), LCE_OK);
    static const char hx[] = "0123456789abcdef";
    std::string mine;
    for (int i = 0; i < 32; ++i) { mine += hx[g_c.digest()[i] >> 4]; mine += hx[g_c.digest()[i] & 15]; }
    CHECK(std::string(text + len - 65, 64) == mine);
    // and the SDK loads its own text back (sanity of the fixture)
    static lce_ledger back; CHECK_EQ(lce_ledger_load(&back, text, len), LCE_OK); CHECK_EQ(back.count, 5);
}

// Full handshake: REQUEST -> HELLO -> server reassembly/merge -> RESULT+TABLE.
struct Server {
    ServerLedger ledger; Policy policy; uint32_t nonce = 0x5eed;
    std::vector<std::vector<uint8_t>> frames;
    MergeResult m;
    bool run(lce_netsync::Client &c, Packets &hello, std::function<bool(const Entry &)> builtin = [](const Entry &) { return false; }) {
        Reassembler r; Reassembler::Status st = Reassembler::kNeedMore;
        for (auto &p : hello) {
            std::string ch; std::vector<uint8_t> pl;
            if (!tk::parse_serverbound(p.data(), p.size(), ch, pl) || ch != kChannel) return false;
            st = r.feed(pl.data(), pl.size());
        }
        if (st != Reassembler::kComplete || r.session != nonce) return false;
        m = merge(ledger, r.entries, policy, builtin);
        if (m.verdict == kAccept) for (auto &e : m.adopt) ledger.add(e);
        frames = encode_result(nonce, m.verdict, m.code, m.message, ledger);
        for (auto &f : frames) { auto fr = tk::clientbound(f); if (!c.on_frame(fr.data(), fr.size())) return false; }
        return true;
    }
};

static void handshake(lce_netsync::Client &c, Server &s, Packets &hello) {
    hello.clear();
    auto req = tk::clientbound(encode_request(s.nonce, s.ledger, s.policy));
    CHECK(c.on_frame(req.data(), req.size()));
    CHECK(!c.pump(true, capture, &hello, logcb, nullptr) || true);
    CHECK(c.report().state == lce_netsync::State::Sent);
    CHECK(s.run(c, hello));
    CHECK(c.pump(true, capture, &hello, logcb, nullptr));
}

static void test_roundtrip() {
    Entry local[3] = {item("rubymod:ruby", 2283), block("rubymod:ore", 254), item("tools:pick", 2285, 1, 250)};
    static lce_netsync::Client c;
    CHECK(c.configure(local, 3, 1, true));
    Server s; Packets hello;
    handshake(c, s, hello);
    CHECK(c.report().state == lce_netsync::State::Accepted);
    CHECK(c.report().table_ok); CHECK_EQ(c.report().server_entries, 3); CHECK_EQ(c.report().table_matches, 3);
    CHECK_EQ(s.ledger.entries.size(), 3);
    {   // the server-declared SDK mods (plugin server gate): both namespaces, once each
        char mods[4][lce_netsync::kModIdCap]; memset(mods, 0, sizeof mods);
        CHECK_EQ(c.server_mods(mods, 4), 2u);
        CHECK(std::string(mods[0]) == "rubymod"); CHECK(std::string(mods[1]) == "tools");
        CHECK_EQ(c.server_mods(mods, 1), 1u);
        CHECK_EQ(c.server_mods(nullptr, 4), 0u);
    }
    // second console with a conflicting ID is refused, with the reason
    Entry other[1] = {item("rubymod:ruby", 2290)};
    static lce_netsync::Client c2;
    CHECK(c2.configure(other, 1, 1, true));
    s.nonce = 99; g_log.clear();
    handshake(c2, s, hello);
    CHECK(c2.report().state == lce_netsync::State::Rejected);
    CHECK_EQ(c2.report().code, kKeyIdConflict);
    CHECK(strstr(c2.report().message, "rubymod:ruby is item 2283 on this server but 2290 on your console") != nullptr);
    CHECK(c2.report().table_ok); CHECK_EQ(c2.report().table_conflicts, 1); CHECK_EQ(c2.report().table_missing, 2);
    CHECK(!g_log.empty() && g_log.back().find("REJECTED") != std::string::npos);
    CHECK_EQ(s.ledger.entries.size(), 3);   // nothing adopted on refusal
    { char mods[4][lce_netsync::kModIdCap]; CHECK_EQ(c2.server_mods(mods, 4), 0u); }   // refused: no mod declared
    // vanilla console (no SDK) on the modded world
    static lce_netsync::Client v;
    s.nonce = 100;
    handshake(v, s, hello);
    CHECK_EQ(hello.size(), 1);
    CHECK_EQ(v.report().code, kMissing);
    // tampered table digest -> accepted but table_ok false + warning logged
    static lce_netsync::Client c3; CHECK(c3.configure(local, 3, 1, true));
    s.nonce = 101;
    auto req = tk::clientbound(encode_request(s.nonce, s.ledger, s.policy));
    c3.on_frame(req.data(), req.size()); hello.clear(); c3.pump(true, capture, &hello, logcb, nullptr);
    auto frames = encode_result(s.nonce, kAccept, kOk, "ok", s.ledger);
    frames[1].back() ^= 1;   // last key byte of the table
    for (auto &f : frames) { auto fr = tk::clientbound(f); c3.on_frame(fr.data(), fr.size()); }
    g_log.clear();
    CHECK(c3.pump(true, capture, &hello, logcb, nullptr));
    CHECK(c3.report().state == lce_netsync::State::Accepted); CHECK(!c3.report().table_ok);
    CHECK(g_log.size() == 2 && g_log[1].find("WARNING") != std::string::npos);
    { char mods[4][lce_netsync::kModIdCap]; CHECK_EQ(c3.server_mods(mods, 4), 0u); }   // unverified table: none
    // leaving the server forgets the declared list
    { char mods[4][lce_netsync::kModIdCap]; CHECK_EQ(c.server_mods(mods, 4), 2u);
      c.pump(false, capture, &hello, logcb, nullptr); CHECK_EQ(c.server_mods(mods, 4), 0u); }
}

static void test_frames_and_reset() {
    static lce_netsync::Client c;
    Entry e = item("aa:x", 2300); CHECK(c.configure(&e, 1, 1, true));
    // other 250 channels and other packets pass through untouched
    auto nbt = tk::clientbound({1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}, "MC|LceNbt");
    CHECK(!c.on_frame(nbt.data(), nbt.size()));
    auto req = tk::clientbound(encode_request(7, ServerLedger(), Policy()));
    auto partial = req; partial.pop_back();
    CHECK(!c.on_frame(partial.data(), partial.size()));     // length must match the frame exactly
    auto two = req; two.insert(two.end(), req.begin(), req.end());
    CHECK(!c.on_frame(two.data(), two.size()));             // not a single-packet frame
    uint8_t chat[] = {3, 0, 1, 0, 'x'};
    CHECK(!c.on_frame(chat, sizeof chat));
    auto v2 = encode_request(7, ServerLedger(), Policy()); v2[0] = 2;
    auto v2f = tk::clientbound(v2);
    CHECK(c.on_frame(v2f.data(), v2f.size()));             // ours, swallowed, ignored
    Packets out;
    CHECK(!c.pump(true, capture, &out, nullptr, nullptr)); CHECK(out.empty());
    // send failure
    CHECK(c.on_frame(req.data(), req.size()));
    CHECK(c.pump(true, refuse, nullptr, logcb, nullptr));
    CHECK(c.report().state == lce_netsync::State::Failed);
    // a result for a session we left is dropped after reset
    CHECK(c.on_frame(req.data(), req.size()));
    c.pump(true, capture, &out, nullptr, nullptr);
    CHECK(!c.pump(false, capture, &out, nullptr, nullptr));
    CHECK(c.report().state == lce_netsync::State::Idle);
    for (auto &f : encode_result(7, kAccept, kOk, "late", ServerLedger())) { auto fr = tk::clientbound(f); c.on_frame(fr.data(), fr.size()); }
    CHECK(!c.pump(true, capture, &out, nullptr, nullptr));
    CHECK(c.report().state == lce_netsync::State::Idle);
    // result with a wrong session is ignored
    CHECK(c.on_frame(req.data(), req.size())); c.pump(true, capture, &out, nullptr, nullptr);
    for (auto &f : encode_result(8, kAccept, kOk, "x", ServerLedger())) { auto fr = tk::clientbound(f); c.on_frame(fr.data(), fr.size()); }
    CHECK(!c.pump(true, capture, &out, nullptr, nullptr));
    CHECK(c.report().state == lce_netsync::State::Sent);
    // non-printable message bytes are sanitised
    auto rf = encode_result(7, kReject, kMissing, std::string("bad\x01text\xff"), ServerLedger());
    for (auto &f : rf) { auto fr = tk::clientbound(f); c.on_frame(fr.data(), fr.size()); }
    CHECK(c.pump(true, capture, &out, nullptr, nullptr));
    CHECK(!strcmp(c.report().message, "bad?text?"));
}

static void test_c_api() {
    std::vector<std::string> lines;
    CHECK_EQ(lce_netsync_configure_from_loader(nullptr, 0, nullptr), 1);
    ServerLedger empty;
    auto req = tk::clientbound(encode_request(42, empty, Policy()));
    CHECK_EQ(lce_netsync_inbound_frame(req.data(), req.size()), 1);
    Packets out;
    lce_netsync_pump(1, capture, &out);
    CHECK_EQ(out.size(), 1);
    for (auto &f : encode_result(42, kAccept, kOk, "mod IDs match this world", empty)) {
        auto fr = tk::clientbound(f); CHECK_EQ(lce_netsync_inbound_frame(fr.data(), fr.size()), 1);
    }
    lce_netsync_pump(1, capture, &out);
    CHECK_EQ(lce_netsync_state() & 0xff, (unsigned)lce_netsync::State::Accepted);
    CHECK(!strcmp(lce_netsync_message(), "mod IDs match this world"));
    lce_netsync_pump(0, capture, &out);
    CHECK_EQ(lce_netsync_state(), 0);
    uint8_t junk[3] = {250, 0, 0};
    CHECK_EQ(lce_netsync_inbound_frame(junk, 3), 0);
}

static void test_fuzz_on_frame() {
    static lce_netsync::Client c;
    Entry e = item("aa:x", 2300); c.configure(&e, 1, 1, true);
    ServerLedger s;
    for (unsigned i = 0; i < 300; ++i) { std::string k = "zz:k" + std::to_string(i); s.add(item(k.c_str(), (uint16_t)(3000 + i))); }
    auto frames = encode_result(9, kAccept, kOk, "fuzz", s);
    auto req = tk::clientbound(encode_request(9, s, Policy()));
    tk::Rng rng{12345};
    Packets out;
    for (int it = 0; it < 4000; ++it) {
        c.on_frame(req.data(), req.size()); c.pump(true, capture, &out, nullptr, nullptr); out.clear();
        for (auto &f : frames) {
            auto fr = tk::clientbound(f);
            int flips = it % 8 == 0 ? 0 : (int)(rng.next() % 3);
            for (int k = 0; k < flips; ++k) fr[25 + rng.next() % (fr.size() - 25)] ^= (uint8_t)(1u << (rng.next() % 8));
            c.on_frame(fr.data(), fr.size());
        }
        if (c.pump(true, capture, &out, nullptr, nullptr) && it % 8 == 0) {
            CHECK(c.report().table_ok); CHECK_EQ(c.report().server_entries, 300);
        }
        CHECK(strlen(c.report().message) <= kMaxMessage);
    }
}

// Worker thread feeds frames while the UI thread pumps (ASan run; the
// mailbox uses acquire/release, so every report must be internally consistent).
static void test_threads() {
    static lce_netsync::Client c;
    Entry e[2] = {item("aa:x", 2300), item("aa:y", 2301)}; c.configure(e, 2, 1, true);
    ServerLedger s; s.add(e[0]); s.add(e[1]);
    std::atomic<bool> stop{false};
    std::atomic<unsigned> reports{0};
    std::thread ui([&] {
        Packets out;
        while (!stop.load()) {
            if (c.pump(true, capture, &out, nullptr, nullptr) && c.report().state == lce_netsync::State::Accepted) {
                CHECK(c.report().table_ok); CHECK_EQ(c.report().table_matches, 2); ++reports;
            }
            if (out.size() > 1000) out.clear();
        }
    });
    for (uint32_t i = 1; i <= 3000; ++i) {
        auto req = tk::clientbound(encode_request(i, s, Policy()));
        c.on_frame(req.data(), req.size());
        for (auto &f : encode_result(i, kAccept, kOk, "ok", s)) { auto fr = tk::clientbound(f); c.on_frame(fr.data(), fr.size()); }
    }
    stop = true; ui.join();
    CHECK(reports.load() > 0);
}

int main() {
    test_golden_packet();
    test_limits_and_validation();
    test_entries_from_sdk();
    test_entries_skip_ext_kinds();
    test_roundtrip();
    test_frames_and_reset();
    test_c_api();
    test_fuzz_on_frame();
    test_threads();
    TEST_MAIN_END("test_netsync");
}
