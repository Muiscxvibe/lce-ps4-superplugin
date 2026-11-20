// Host test for the PS4 NBT editor core: nbt_doc.hpp (tree model, codec, edits, SNBT,
// diff -> protocol script), nbt_schema.hpp (hints, pick-lists, templates),
// nbt_editor_ui.hpp (screens/keys/requests) and the MC|LcePart reassembly in
// remote_nbt_core.hpp. The diff is checked end to end against a small simulated
// server that applies the protocol v2 ops the same way LceNbtEdit.h does.
//   ./run_nbt_editor_test.sh
#include "nbt_editor_ui.hpp"
#include "remote_nbt_core.hpp"
#include "nbt_apply.hpp"
#include "local_nbt_core.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <memory>
#include <random>
using namespace lce_nbt_ed;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)
typedef std::vector<uint8_t> Bytes;

static Bytes ser(const Doc &d) { static uint8_t buf[kMaxDocBytes]; size_t n = d.serialize(buf, sizeof buf); return Bytes(buf, buf + n); }
static void mkroot(Doc &d) { static const uint8_t empty[] = {10, 0, 0, 0}; d.parse(empty, sizeof empty); }
static std::unique_ptr<Doc> newdoc() { std::unique_ptr<Doc> d(new Doc()); d->clear(); return d; }

// ---- order-insensitive equality -------------------------------------------------------------
static bool same_node(const Doc &a, uint32_t x, const Doc &b, uint32_t y)
{
    const Node &p = a.at(x), &q = b.at(y);
    if (p.type != q.type) return false;
    if (is_integer(p.type)) return p.v.i == q.v.i;
    if (is_real(p.type)) return dbits(p.v.d) == dbits(q.v.d);
    if (p.type == T_STRING || p.type == T_BYTE_ARRAY) return p.v.b.len == q.v.b.len && !memcmp(a.bytes(p.v.b.off), b.bytes(q.v.b.off), p.v.b.len);
    if (p.type == T_INT_ARRAY) return p.v.b.len == q.v.b.len && !memcmp(a.bytes(p.v.b.off), b.bytes(q.v.b.off), p.v.b.len * 4);
    if (p.count != q.count) return false;
    if (p.type == T_LIST) {
        if (p.count && p.elem != q.elem) return false;
        for (uint32_t c = p.first, e = q.first; c != kNil; c = a.at(c).next, e = b.at(e).next) if (!same_node(a, c, b, e)) return false;
        return true;
    }
    for (uint32_t c = p.first; c != kNil; c = a.at(c).next) {
        const uint32_t e = b.find_n(y, (const char *)a.bytes(a.at(c).name_off), a.at(c).name_len);
        if (e == kNil || !same_node(a, c, b, e)) return false;
    }
    return true;
}

// ---- simulated server (protocol v2 edit ops on a Doc) ---------------------------------------
struct Sim {
    Doc &d; std::string err;
    explicit Sim(Doc &doc) : d(doc) {}
    static int hexv(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; }
    static bool unesc(const std::string &in, std::string *out)
    {
        out->clear();
        for (size_t i = 0; i < in.size(); ++i) {
            if (in[i] == '%') { if (i + 2 >= in.size()) return false; int h = hexv(in[i + 1]), l = hexv(in[i + 2]); if (h < 0 || l < 0) return false; out->push_back((char)(h * 16 + l)); i += 2; }
            else if (plain_key_char((unsigned char)in[i])) out->push_back(in[i]); else return false;
        }
        return true;
    }
    struct Step { std::string key; int index = -1; bool append = false; };
    bool path(const std::string &p, std::vector<Step> *out)
    {
        out->clear(); if (p == "/") return true;
        size_t i = 0;
        while (i < p.size()) {
            size_t s = i; while (i < p.size() && p[i] != '.' && p[i] != '[') ++i;
            Step st; if (!unesc(p.substr(s, i - s), &st.key) || st.key.empty()) return false; out->push_back(st);
            while (i < p.size() && p[i] == '[') { size_t c = p.find(']', i); std::string in = p.substr(i + 1, c - i - 1); Step x; if (in == "+") x.append = true; else x.index = atoi(in.c_str()); out->push_back(x); i = c + 1; }
            if (i < p.size()) { if (p[i] != '.') return false; ++i; }
        }
        return true;
    }
    uint32_t child(uint32_t n, const Step &s) { if (s.index >= 0) return d.at(n).type == T_LIST ? d.child_at(n, (uint32_t)s.index) : kNil; return d.at(n).type == T_COMPOUND ? d.find_n(n, s.key.data(), s.key.size()) : kNil; }
    uint32_t walk(const std::vector<Step> &st, size_t n) { uint32_t x = d.root; for (size_t i = 0; i < n && x != kNil; ++i) x = child(x, st[i]); return x; }
    static uint8_t tl(char c) { const char *l = "?bsilfdBtncI"; const char *p = strchr(l, c); return p ? (uint8_t)(p - l) : 0; }
    bool assign(uint32_t n, char type, const std::string &v)
    {
        const uint8_t t = d.at(n).type;
        if (is_integer(t)) return d.set_integer(n, atoll(v.c_str()));
        if (is_real(t)) return d.set_real(n, strtod(v.c_str(), nullptr));
        if (t == T_STRING) { std::string s = v; if (type == 'T') { if (v == "%") s.clear(); else if (!unesc(v, &s)) return false; } return d.set_string(n, (const uint8_t *)s.data(), s.size()); }
        return false;
    }
    uint32_t make_scalar(uint32_t parent, uint32_t after, const char *name, char type, const std::string &v)
    {
        uint8_t t = tl(type == 'T' ? 't' : type == 'z' ? 'b' : type);
        uint32_t c = d.add_child(parent, after, t, name); if (c == kNil) return kNil;
        if (!assign(c, type, v)) { d.remove(c); return kNil; }
        return c;
    }
    bool run(const std::string &line)
    {
        std::vector<std::string> w; size_t i = 0; std::string rest;
        const std::string body = line.substr(strlen("/lce-nbt "));
        auto word = [&]() { while (i < body.size() && body[i] == ' ') ++i; size_t s = i; while (i < body.size() && body[i] != ' ') ++i; return body.substr(s, i - s); };
        const std::string op = word();
        if (op == "tx" || op == "commit" || op == "abort") return true;
        word(); // target
        const std::string p = word();
        std::vector<Step> st; if (!path(p, &st) || st.empty()) { err = "path " + p; return false; }
        const Step &last = st.back();
        uint32_t parent = walk(st, st.size() - 1); if (parent == kNil) { err = "parent " + p; return false; }
        if (op == "del") {
            if (last.index >= 0 && is_array(d.at(parent).type)) return d.array_splice(parent, (uint32_t)last.index, 1, 0, 0);
            uint32_t n = child(parent, last); if (n == kNil) { err = "del missing " + p; return false; } return d.remove(n);
        }
        if (op == "ren") { std::string nk; unesc(word(), &nk); uint32_t n = child(parent, last); return n != kNil && d.set_name(n, nk.data(), nk.size()); }
        if (op == "mk") {
            const std::string k = word(); uint8_t t = k[0] == 'n' ? (uint8_t)T_LIST : tl(k[0]); uint8_t et = k[0] == 'n' ? tl(k[1]) : 0;
            if (last.append || last.index >= 0) {
                if (d.at(parent).type != T_LIST) { err = "mk not list"; return false; }
                uint32_t after = kNil;
                if (last.index >= 0) { uint32_t old = d.child_at(parent, (uint32_t)last.index); if (old == kNil) return false; after = d.at(old).prev; d.remove(old); }
                uint32_t c = d.add_child(parent, after, t, nullptr); if (c == kNil) { err = "mk add"; return false; }
                if (after == kNil && last.index == 0) { d.unlink(c); d.link_before(parent, d.at(parent).first, c); }
                if (t == T_LIST) d.set_list_type(c, et);
                return true;
            }
            uint32_t old = child(parent, last); uint32_t after = kNil; if (old != kNil) { after = d.at(old).prev; d.remove(old); }
            uint32_t c = d.add_child(parent, kNil, t, last.key.c_str()); if (c == kNil) { err = "mk"; return false; }
            if (t == T_LIST) d.set_list_type(c, et);
            (void)after; return true;
        }
        if (op == "aset") {
            const uint32_t a = child(parent, last); if (a == kNil || !is_array(d.at(a).type)) { err = "aset"; return false; }
            uint32_t start = (uint32_t)atoi(word().c_str()); std::string vals = word();
            std::vector<long long> v; size_t q = 0; while (q <= vals.size()) { size_t c = vals.find(',', q); v.push_back(atoll(vals.substr(q, c == std::string::npos ? std::string::npos : c - q).c_str())); if (c == std::string::npos) break; q = c + 1; }
            const uint32_t len = d.at(a).v.b.len; if (start > len) return false;
            if (start + v.size() > len) d.array_splice(a, len, 0, (uint32_t)(start + v.size() - len), 0);
            for (size_t k = 0; k < v.size(); ++k) d.array_put(a, start + (uint32_t)k, v[k]);
            return true;
        }
        const std::string type = word(); while (i < body.size() && body[i] == ' ') ++i; rest = body.substr(i);
        if (op == "add") {
            uint32_t l = child(parent, last); if (l == kNil) { err = "add missing"; return false; }
            if (is_array(d.at(l).type)) return d.array_splice(l, d.at(l).v.b.len, 0, 1, atoll(rest.c_str()));
            return make_scalar(l, kNil, nullptr, type[0], rest) != kNil;
        }
        if (op == "set") {
            if (last.index >= 0) {
                if (is_array(d.at(parent).type)) { d.array_put(parent, (uint32_t)last.index, atoll(rest.c_str())); return true; }
                uint32_t n = child(parent, last); return n != kNil && assign(n, type[0], rest);
            }
            uint32_t n = child(parent, last);
            if (n != kNil) return assign(n, type[0], rest);
            return make_scalar(parent, kNil, last.key.c_str(), type[0], rest) != kNil;
        }
        err = "op " + op; return false;
    }
    bool script(const char *s)
    {
        std::string all(s); size_t p = 0;
        while (p < all.size()) { size_t e = all.find('\n', p); std::string l = all.substr(p, e - p); p = e + 1; if (!l.empty() && !run(l)) { std::printf("  sim failed on: %s (%s)\n", l.c_str(), err.c_str()); return false; } }
        return true;
    }
};

static const char *kPlayerSnbt = "{Pos:[1.5d,64.0d,-3.25d],Motion:[0.0d,0.0d,0.0d],Rotation:[90.0f,0.0f],Health:20.0f,foodLevel:20,"
    "Invulnerable:0b,playerGameType:0,XpLevel:5,Inventory:[{id:276s,Count:1b,Damage:0s,Slot:0b,tag:{ench:[{id:16s,lvl:5s}],display:{Name:\"Blade\"}}},"
    "{id:1s,Count:64b,Damage:0s,Slot:1b}],abilities:{flying:0b,mayfly:0b,walkSpeed:0.1f},ActiveEffects:[;c],Dimension:0,UUIDMost:5L,Data:[I;1,2,3],Bytes:[B;1,2]}";

static void build_player(Doc &d)
{
    mkroot(d);
    // snbt_build adds a child; unwrap by building into root then moving children up
    const uint32_t c = snbt_build(d, d.root, "p", kPlayerSnbt);
    CHECK(c != kNil);
    Bytes b(65536); const size_t n = d.serialize_node(c, b.data(), b.size(), true); b.resize(n);
    // the built compound becomes the root: rename to "" and reparse
    b[1] = 0; b[2] = 0; b.erase(b.begin() + 3, b.begin() + 4);
    CHECK(d.parse(b.data(), b.size()));
}

static void test_codec()
{
    auto d = newdoc(); build_player(*d);
    CHECK(d->valid());
    CHECK(d->find(d->root, "Health") != kNil && d->at(d->find(d->root, "Health")).type == T_FLOAT);
    CHECK(d->at(d->find(d->root, "Data")).v.b.len == 3 && d->array_get(d->find(d->root, "Data"), 2) == 3);
    CHECK(d->at(d->find(d->root, "ActiveEffects")).elem == T_COMPOUND && d->at(d->find(d->root, "ActiveEffects")).count == 0);
    Bytes a = ser(*d);
    auto e = newdoc(); CHECK(e->parse(a.data(), a.size()));
    CHECK(ser(*e) == a);                                   // byte-exact round trip
    // truncated / garbage input is rejected without side effects
    for (size_t cut = 1; cut < a.size(); cut += 37) CHECK(!e->parse(a.data(), cut));
    Bytes g = a; g[0] = 9; CHECK(!e->parse(g.data(), g.size()));
    // compaction keeps the content
    uint8_t *scratch = new uint8_t[kMaxDocBytes];
    for (int i = 0; i < 200; ++i) { uint32_t h = d->find(d->root, "foodLevel"); d->set_integer(h, i % 20); uint32_t nm = d->find(d->find(d->child_at(d->find(d->root, "Inventory"), 0), "tag"), "display"); d->set_string(d->find(nm, "Name"), (const uint8_t *)"Long name here", 14); }
    Bytes before = ser(*d); CHECK(d->compact(scratch)); CHECK(ser(*d) == before);
    delete[] scratch;
    // modified UTF-8 helpers
    uint16_t u[8]; const uint8_t sect[] = {0xC2, 0xA7, 'a'}; CHECK(mutf8_to_utf16(sect, 3, u, 8) == 2 && u[0] == 0xA7);
    uint8_t m[8]; CHECK(utf16_to_mutf8(u, 2, m, 8) == 3 && !memcmp(m, sect, 3));
    char pv[16]; ascii_preview(sect, 3, pv, sizeof pv); CHECK(!strcmp(pv, "&a"));
}

static void test_edits()
{
    auto d = newdoc(); build_player(*d);
    const uint32_t inv = d->find(d->root, "Inventory");
    const uint32_t food = d->find(d->root, "foodLevel");
    CHECK(d->set_integer(food, 7) && d->at(food).v.i == 7);
    const uint32_t cnt = d->find(d->child_at(inv, 0), "Count");
    CHECK(!d->set_integer(cnt, 200) && d->set_integer(cnt, 127) && !d->set_integer(cnt, -129));
    const uint32_t hp = d->find(d->root, "Health");
    CHECK(d->set_real(hp, 0.1) && d->at(hp).v.d == (double)0.1f);    // float precision
    CHECK(!d->set_real(hp, 1e39));
    CHECK(d->add_child(d->root, kNil, T_INT, "foodLevel") == kNil);  // duplicate key
    const uint32_t nb = d->add_child(d->root, kNil, T_BYTE, "NoAI"); CHECK(nb != kNil);
    CHECK(d->add_child(inv, kNil, T_STRING, nullptr) == kNil);       // list holds compounds
    const uint32_t eff = d->find(d->root, "ActiveEffects");
    CHECK(d->set_list_type(eff, T_STRING) && d->add_child(eff, kNil, T_STRING, nullptr) != kNil && !d->set_list_type(eff, T_INT));
    CHECK(d->move(d->child_at(inv, 1), -1) && d->at(d->find(d->child_at(inv, 0), "id")).v.i == 1);
    CHECK(!d->move(d->child_at(inv, 0), -1));
    CHECK(d->set_name(nb, "Silent", 6) && d->find(d->root, "Silent") == nb && !d->set_name(nb, "Health", 6));
    const uint32_t arr = d->find(d->root, "Data");
    CHECK(d->array_splice(arr, 1, 1, 2, 9) && d->at(arr).v.b.len == 4 && d->array_get(arr, 1) == 9 && d->array_get(arr, 3) == 3);
    CHECK(!d->array_splice(arr, 9, 0, 1, 0));
    // serialized copy / paste / replace
    uint8_t buf[4096]; const size_t n = d->serialize_node(d->child_at(inv, 1), buf, sizeof buf, true);
    CHECK(n && d->insert_serialized(inv, d->child_at(inv, 1), buf, n) != kNil && d->at(inv).count == 3);
    const uint32_t abil = d->find(d->root, "abilities");
    const size_t an = d->serialize_node(abil, buf, sizeof buf, true);
    const uint32_t pasted = d->insert_serialized(d->root, kNil, buf, an);
    CHECK(pasted != kNil && d->name_eq(pasted, "abilities_2"));
    CHECK(d->insert_serialized(inv, kNil, buf, an) != kNil);          // compound into list of compounds
    const size_t sn = d->serialize_node(d->find(d->root, "XpLevel"), buf, sizeof buf, true);
    CHECK(d->insert_serialized(inv, kNil, buf, sn) == kNil);          // int into list of compounds
    const uint32_t rep = d->replace_serialized(d->find(d->root, "Silent"), buf, sn);
    CHECK(rep != kNil && d->name_eq(rep, "Silent") && d->at(rep).type == T_INT && d->at(rep).v.i == 5);
    CHECK(d->remove(d->find(d->root, "Silent")) && d->find(d->root, "Silent") == kNil && !d->remove(d->root));
    // paths
    Path p; const uint32_t nm = d->find(d->find(d->find(d->child_at(inv, 1), "tag"), "display"), "Name");
    path_of(*d, nm, &p); CHECK(node_at(*d, p) == nm);
    char pp[128]; CHECK(proto_path(*d, nm, pp, sizeof pp) && !strcmp(pp, "Inventory[1].tag.display.Name"));
    const uint32_t odd = d->add_child(d->root, kNil, T_BYTE, "a.b c%"); CHECK(proto_path(*d, odd, pp, sizeof pp) && !strcmp(pp, "a%2Eb%20c%25"));
    CHECK(proto_path(*d, d->root, pp, sizeof pp) && !strcmp(pp, "/"));
    CHECK(proto_path(*d, inv, pp, sizeof pp, "[+]") && !strcmp(pp, "Inventory[+]"));
    // limits: depth
    uint32_t deep = d->root; bool okd = true;
    for (unsigned k = 0; k < kMaxDepth + 2 && okd; ++k) { char nm2[8]; snprintf(nm2, sizeof nm2, "d%u", k); uint32_t c = d->add_child(deep, kNil, T_COMPOUND, nm2); if (c == kNil) okd = false; else deep = c; }
    CHECK(!okd);
}

static void test_snbt_schema()
{
    auto d = newdoc(); mkroot(*d);
    for (const Template &t : kGeneric) CHECK(snbt_build(*d, d->root, t.name[0] ? t.name : "x", t.snbt) != kNil || d->find(d->root, t.name) != kNil);
    CHECK(snbt_build(*d, d->root, "bad", "{a:}") == kNil && d->find(d->root, "bad") == kNil);
    auto p = newdoc(); build_player(*p);
    const Template *ts[64];
    const size_t n = templates_for(*p, p->find(p->root, "Inventory"), K_PLAYER, ts, 64);
    CHECK(n >= 2 && !strcmp(ts[0]->label, "Item stack"));
    for (size_t i = 0; i < n; ++i) CHECK(ts[i]->snbt[0] == '{');   // only compounds fit this list
    const uint32_t tag = p->find(p->child_at(p->find(p->root, "Inventory"), 0), "tag");
    const size_t tn = templates_for(*p, tag, K_PLAYER, ts, 64);
    bool has_display = false; for (size_t i = 0; i < tn; ++i) if (!strcmp(ts[i]->name, "display")) has_display = true;
    CHECK(has_display);
    // every template builds
    auto t = newdoc(); mkroot(*t);
    for (size_t i = 0; i < tn; ++i) { char nm[32]; snprintf(nm, sizeof nm, "t%zu", i); CHECK(snbt_build(*t, t->root, nm, ts[i]->snbt) != kNil); }
    // hints
    const uint32_t item = p->child_at(p->find(p->root, "Inventory"), 0);
    Hint h = hint_for(*p, p->find(item, "id"), K_PLAYER); CHECK(h.pick == Pick::Item);
    h = hint_for(*p, p->find(item, "Count"), K_PLAYER); CHECK(h.ranged && h.lo == 1 && h.hi == 127);
    const uint32_t en = p->child_at(p->find(tag, "ench"), 0);
    CHECK(hint_for(*p, p->find(en, "id"), K_PLAYER).pick == Pick::Enchant);
    CHECK(hint_for(*p, p->find(p->root, "Invulnerable"), K_PLAYER).boolean);
    CHECK(hint_for(*p, p->find(p->root, "playerGameType"), K_PLAYER).pick == Pick::GameMode);
    CHECK(hint_for(*p, p->find(p->root, "UUIDMost"), K_PLAYER).readonly && !hint_for(*p, p->find(p->root, "Health"), K_PLAYER).readonly);
    CHECK(!hint_for(*p, p->find(p->root, "Dimension"), K_PLAYER).readonly && hint_for(*p, p->find(p->root, "Dimension"), K_PLAYER).pick == Pick::Dimension);
    CHECK(choice_name(Pick::Item, 276) && !strcmp(choice_name(Pick::Item, 276), "diamond_sword"));
    CHECK(choice_name(Pick::Enchant, 16) && !strcmp(choice_name(Pick::Enchant, 16), "sharpness"));
    CHECK(choice_count(Pick::Block) > 100 && choice_at(Pick::Block, 0).id < 256);
    CHECK(choice_count(Pick::EntityType) > 40 && choice_index(Pick::EntityType, 0, "Zombie", 6) > 0);
    char v[40]; describe_value(*p, item, K_PLAYER, v, sizeof v); CHECK(!strcmp(v, "diamond_sword x1"));
    describe_value(*p, en, K_PLAYER, v, sizeof v); CHECK(!strcmp(v, "sharpness 5"));
}

// Random edits on a copy, diff, replay on the original through the simulated server.
static void test_diff_fuzz()
{
    std::mt19937 rng(1234);
    auto orig = newdoc(), work = newdoc(), replay = newdoc();
    static char script[64 * 1024];
    int checked = 0;
    for (int round = 0; round < 400; ++round) {
        build_player(*orig);
        Bytes ob = ser(*orig); work->parse(ob.data(), ob.size());
        const int edits = 1 + (int)(rng() % 8);
        for (int e = 0; e < edits; ++e) {
            // pick a random node
            std::vector<uint32_t> all; std::vector<uint32_t> stack{work->root};
            while (!stack.empty()) { uint32_t x = stack.back(); stack.pop_back(); all.push_back(x); for (uint32_t c = work->at(x).first; c != kNil; c = work->at(c).next) stack.push_back(c); }
            const uint32_t n = all[rng() % all.size()];
            const Node &x = work->at(n);
            switch (rng() % 9) {
            case 0: if (is_integer(x.type)) work->set_integer(n, (int64_t)(rng() % 100) - 50); break;
            case 1: if (is_real(x.type)) work->set_real(n, (double)(rng() % 1000) / 8.0); break;
            case 2: if (x.type == T_STRING) { const char *s[] = {"", "plain", " spaced ", "100%", "\xC2\xA7" "cRed", "a.b[c]"}; const char *t = s[rng() % 6]; work->set_string(n, (const uint8_t *)t, strlen(t)); } break;
            case 3: if (n != work->root) work->remove(n); break;
            case 4: if (x.type == T_COMPOUND) { char nm[16]; snprintf(nm, sizeof nm, "k%u", (unsigned)(rng() % 5)); const char *src[] = {"1b", "{a:1,b:[1,2]}", "\"txt\"", "[I;4,5]", "[;c]", "2.5d"}; snbt_build(*work, n, nm, src[rng() % 6]); } break;
            case 5: if (x.type == T_LIST) { const uint8_t et = x.count ? x.elem : (uint8_t)T_COMPOUND; uint32_t c = work->add_child(n, kNil, et, nullptr); (void)c; } break;
            case 6: if (n != work->root && work->at(work->at(n).parent).type == T_LIST) work->move(n, rng() % 2 ? 1 : -1); break;
            case 7: if (is_array(x.type)) work->array_splice(n, 0, x.v.b.len ? 1 : 0, (uint32_t)(rng() % 3), (int64_t)(rng() % 9)); break;
            case 8: if (n != work->root && work->at(work->at(n).parent).type == T_COMPOUND) { char nm[16]; snprintf(nm, sizeof nm, "r%u", (unsigned)(rng() % 50)); work->set_name(n, nm, strlen(nm)); } break;
            }
        }
        Script s{script, sizeof script, 0, 0, true, {0}}; script[0] = 0;
        s.line("/lce-nbt tx p");
        const bool ok = diff_script(*orig, *work, "p", s);
        s.line("/lce-nbt commit");
        CHECK(ok);
        if (!ok) { std::printf("  diff error: %s\n", s.error); continue; }
        replay->parse(ob.data(), ob.size());
        Sim sim(*replay);
        const bool applied = sim.script(script);
        CHECK(applied);
        const bool same = applied && same_node(*replay, replay->root, *work, work->root);
        CHECK(same);
        if (!same) { std::printf("  round %d script:\n%s\n", round, script); break; }
        // the local-world backend's applier must give the same document
        replay->parse(ob.data(), ob.size());
        ScriptApplier ap(*replay);
        const bool ap_ok = ap.script(script, strlen(script));
        CHECK(ap_ok); if (!ap_ok) std::printf("  applier: %s\n", ap.err());
        const bool same2 = ap_ok && same_node(*replay, replay->root, *work, work->root);
        CHECK(same2);
        if (!same2) { std::printf("  round %d (applier) script:\n%s\n", round, script); break; }
        ++checked;
    }
    CHECK(checked == 400);
    // no changes -> only tx/commit
    build_player(*orig); Bytes ob = ser(*orig); work->parse(ob.data(), ob.size());
    Script s{script, sizeof script, 0, 0, true, {0}}; CHECK(diff_script(*orig, *work, "p", s) && s.lines == 0);
    // escaping in values
    const uint32_t nm = work->find(work->find(work->child_at(work->find(work->root, "Inventory"), 0), "tag"), "display");
    work->set_string(work->find(nm, "Name"), (const uint8_t *)"\xC2\xA7" "6Gold 100%", 12);
    Script s2{script, sizeof script, 0, 0, true, {0}}; script[0] = 0; CHECK(diff_script(*orig, *work, "i0", s2));
    CHECK(!strcmp(script, "/lce-nbt set i0 Inventory[0].tag.display.Name T %C2%A76Gold%20100%25\n")); if (strcmp(script, "/lce-nbt set i0 Inventory[0].tag.display.Name T %C2%A76Gold%20100%25\n")) std::printf("  got: %s", script);
    // too long for one request
    char big[300]; memset(big, 'x', sizeof big); work->set_string(work->find(nm, "Name"), (const uint8_t *)big, sizeof big);
    Script s3{script, sizeof script, 0, 0, true, {0}}; CHECK(!diff_script(*orig, *work, "p", s3) && strstr(s3.error, "too long"));
}

// ---- UI ---------------------------------------------------------------------------------------
static Bytes reply_payload(const char *op, const char *target, bool ok, const char *msg, const Bytes &nbt)
{
    Bytes p; auto utf = [&](const char *s) { size_t n = strlen(s); p.push_back((uint8_t)(n >> 8)); p.push_back((uint8_t)n); p.insert(p.end(), s, s + n); };
    utf(op); utf(target); p.push_back(ok); utf(msg); if (ok) p.insert(p.end(), nbt.begin(), nbt.end()); return p;
}
static Bytes near_payload(const char *kind, std::vector<std::pair<const char *, const char *>> rows)
{
    Bytes near; auto u16 = [&](size_t n) { near.push_back((uint8_t)(n >> 8)); near.push_back((uint8_t)n); };
    auto utf = [&](const char *s) { u16(strlen(s)); near.insert(near.end(), s, s + strlen(s)); };
    utf(kind); near.push_back(0); near.push_back(0); near.push_back((uint8_t)(rows.size() >> 8)); near.push_back((uint8_t)rows.size());
    for (auto &r : rows) { utf(r.first); utf(r.second); }
    return near;
}
struct Harness {
    std::unique_ptr<Storage> st{new Storage()}; Editor ed{st.get()};
    uint64_t now = 1000000;
    std::string last_lines; Expect last_expect = Expect::None;
    bool take() { const char *l; size_t n; Expect e; if (!ed.take_outbox(&l, &n, &e)) return false; last_lines.assign(l, n); last_expect = e; return true; }
    // next document request, dropping background list refreshes (their replies never come here)
    bool take_doc() { while (take()) { if (last_expect == Expect::Nbt) return true; ed.on_timeout(); } return false; }
    void press(Key k, int times = 1) { for (int i = 0; i < times; ++i) ed.key(k, false); }
    void reply(const Doc &d, const char *op = "get") { Bytes b = ser(d); ed.on_nbt(op, "p", true, op[0] == 'c' ? "2 edits applied" : "ok", b.data(), b.size()); }
    void check_view()
    {
        View v; ed.view(&v);
        CHECK(v.count >= 1 && v.count <= kViewRows);
        CHECK(v.no_focus || (v.selected >= 1 && v.selected < v.count) || v.keyboard);
        for (unsigned i = 0; i < v.count; ++i) {
            CHECK(strlen(v.rows[i].label) <= kViewText && strlen(v.rows[i].value) <= kViewText);
            for (const char *c = v.rows[i].label; *c; ++c) CHECK(*c >= 0x20 && *c <= 0x7e);
            for (const char *c = v.rows[i].value; *c; ++c) CHECK(*c >= 0x20 && *c <= 0x7e);
        }
        CHECK(strlen(v.status) < 64);
        CHECK(v.category < 4);
    }
    // moves the tree cursor to the child named `name` of the current container
    bool select(const char *name)
    {
        for (int i = 0; i < 80; ++i) { uint32_t s = ed.selected_node(); if (s != kNil && ed.work().name_eq(s, name)) return true; press(Key::Down); }
        return false;
    }
};

static void test_ui()
{
    Harness h;
    auto player = newdoc(); build_player(*player);
    // Home: L1/R1/Circle belong to the tab menu
    CHECK(h.ed.key(Key::L1, false) == KeyResult::PassThrough && h.ed.key(Key::Circle, false) == KeyResult::PassThrough);
    CHECK(h.ed.key(Key::Down, false) == KeyResult::Consumed);
    h.press(Key::Up); h.check_view();
    h.press(Key::Cross);
    CHECK(h.take() && h.last_lines == "/lce-nbt get p\n" && h.last_expect == Expect::Nbt);
    CHECK(h.ed.waiting() && h.ed.key(Key::L1, false) == KeyResult::Consumed);
    h.check_view();
    h.reply(*player);
    CHECK(!h.ed.waiting() && h.ed.screen() == Editor::Screen::Tree && h.ed.work().valid());
    h.check_view();
    // toggle a boolean with Cross
    CHECK(h.select("Invulnerable")); h.press(Key::Cross);
    CHECK(h.ed.work().at(h.ed.selected_node()).v.i == 1 && h.ed.edits() == 1);
    // undo (L1) and redo it
    h.press(Key::L1); CHECK(h.ed.work().at(h.ed.selected_node()).v.i == 0 && h.ed.edits() == 0);
    h.press(Key::Cross);
    // read-only key
    CHECK(h.select("UUIDMost")); h.press(Key::Cross); CHECK(h.ed.screen() == Editor::Screen::Tree && strstr(h.ed.status(), "Read-only"));
    // number editor: foodLevel 20 -> L1 can't go below step 1; Left x3 = 17, R1 step 10, Left -> 7, Cross OK
    CHECK(h.select("foodLevel")); h.press(Key::Cross); CHECK(h.ed.screen() == Editor::Screen::Number); h.check_view();
    h.press(Key::Left, 3); h.press(Key::R1); h.press(Key::Left); h.press(Key::Right); h.press(Key::Right); h.press(Key::Right);   // clamps at 20
    h.press(Key::L1); h.press(Key::Left, 2);
    h.press(Key::Cross);
    CHECK(h.ed.screen() == Editor::Screen::Tree && h.ed.work().at(h.ed.selected_node()).v.i == 18);
    // float: Health 20 -> step 0.1 x2 -> 19.8
    CHECK(h.select("Health")); h.press(Key::Cross); h.press(Key::L1); h.press(Key::Left, 2); h.press(Key::Cross);
    CHECK(h.ed.work().at(h.ed.selected_node()).v.d == (double)19.8f);
    // into the inventory, first item, pick its id from the item list
    CHECK(h.select("Inventory")); h.press(Key::Cross); h.check_view();
    h.press(Key::Cross); CHECK(h.select("id")); h.press(Key::Cross);   // Item pick-list (scalar with pick opens number editor)
    CHECK(h.ed.screen() == Editor::Screen::Number); h.press(Key::Triangle); CHECK(h.ed.screen() == Editor::Screen::Choice); h.check_view();
    h.press(Key::Down); h.press(Key::Cross); CHECK(h.ed.screen() == Editor::Screen::Number); h.press(Key::Cross);
    CHECK(h.ed.work().at(h.ed.selected_node()).v.i != 276);
    // copy the item tag subtree and paste it into the second item
    h.press(Key::Circle); h.press(Key::Up, 40); // back to the list
    CHECK(h.ed.screen() == Editor::Screen::Tree);
    h.press(Key::Cross); CHECK(h.select("tag"));
    h.press(Key::R1); CHECK(h.ed.screen() == Editor::Screen::Actions); h.check_view();
    // run "Copy"
    for (int i = 0; i < 20; ++i) { View v; h.ed.view(&v); if (!strcmp(v.rows[v.selected].label, "Copy")) break; h.press(Key::Down); }
    h.press(Key::Cross); CHECK(strstr(h.ed.status(), "Copied"));
    h.press(Key::Circle); h.press(Key::Down); h.press(Key::Cross);    // second item
    h.press(Key::R1);
    for (int i = 0; i < 20; ++i) { View v; h.ed.view(&v); if (!strcmp(v.rows[v.selected].label, "Paste here")) break; h.press(Key::Down); }
    h.press(Key::Cross); CHECK(strstr(h.ed.status(), "Pasted"));
    CHECK(h.select("tag"));
    // add a template inside tag: display (name/lore)
    h.press(Key::R1);
    for (int i = 0; i < 20; ++i) { View v; h.ed.view(&v); if (!strcmp(v.rows[v.selected].label, "Add tag inside...")) break; h.press(Key::Down); }
    h.press(Key::Cross); CHECK(h.ed.screen() == Editor::Screen::Templates); h.check_view();
    // "display" already exists in the pasted tag -> refused; choose Unbreakable instead
    for (int i = 0; i < 30; ++i) { View v; h.ed.view(&v); if (!strcmp(v.rows[v.selected].label, "Unbreakable")) break; h.press(Key::Down); }
    h.press(Key::Cross); CHECK(h.ed.screen() == Editor::Screen::Tree && h.ed.work().name_eq(h.ed.selected_node(), "Unbreakable"));
    // string edit through the keyboard: display.Name := "Bladeab"
    CHECK(h.select("display")); h.press(Key::Cross); CHECK(h.select("Name")); h.press(Key::Cross);
    CHECK(h.ed.screen() == Editor::Screen::Text); h.check_view();
    h.press(Key::Cross); h.press(Key::Right); h.press(Key::Cross);    // 'a', 'b'
    h.press(Key::Down, 4); for (int i = 0; i < 3; ++i) h.press(Key::Right);   // bottom row col 6 = "Done" (index 43)
    { View v; h.ed.view(&v); CHECK(v.keyboard && v.kb_sel == 43 && v.kb_len == 7); }
    h.press(Key::Cross);
    { const uint32_t s = h.ed.selected_node(); CHECK(h.ed.work().at(s).v.b.len == 7 && !memcmp(h.ed.work().bytes(h.ed.work().at(s).v.b.off), "Bladeab", 7)); }
    // presets: append a colour code
    h.press(Key::Cross); h.press(Key::Triangle); CHECK(h.ed.screen() == Editor::Screen::Choice); h.press(Key::Down); h.press(Key::Cross);
    CHECK(h.ed.screen() == Editor::Screen::Text); h.press(Key::Down, 4); h.press(Key::Right, 3); h.press(Key::Cross);
    { const uint32_t s = h.ed.selected_node(); CHECK(h.ed.work().at(s).v.b.len == 10); }
    // apply: script is tx .. commit; replay it on the server copy
    const unsigned edits = h.ed.edits(); CHECK(edits >= 6);
    h.press(Key::Square);
    CHECK(h.take() && h.last_lines.rfind("/lce-nbt tx p\n", 0) == 0 && h.last_lines.find("/lce-nbt commit\n") == h.last_lines.size() - 16);
    auto server = newdoc(); { Bytes b = ser(*player); server->parse(b.data(), b.size()); }
    Sim sim(*server); CHECK(sim.script(h.last_lines.c_str()));
    CHECK(same_node(*server, server->root, h.ed.work(), h.ed.work().root));
    h.reply(*server, "commit");
    CHECK(h.ed.edits() == 0 && strstr(h.ed.status(), "applied"));
    // failed commit keeps the draft (cursor stayed in tag.display)
    { View v; h.ed.view(&v); CHECK(!strcmp(v.rows[0].label, "Me>Inventory[1]>tag>display")); }
    h.press(Key::Circle); CHECK(h.select("Unbreakable")); h.press(Key::Cross);
    h.press(Key::Square); CHECK(h.take());
    h.ed.on_nbt("commit", "p", false, "unknown item id 9999", nullptr, 0);
    CHECK(h.ed.edits() == 1 && strstr(h.ed.status(), "unknown item"));
    // leaving with changes asks first; Discard closes
    for (int i = 0; i < 6; ++i) h.press(Key::Circle);
    CHECK(h.ed.screen() == Editor::Screen::Confirm); h.check_view();
    h.press(Key::Down); h.press(Key::Cross); CHECK(h.ed.screen() == Editor::Screen::Home);
    // entity list: Entity sub-tab (Right x2) loads in the background, radius row cycles 16 -> 32
    CHECK(h.ed.sub_tab() == 0);
    h.press(Key::Right); h.press(Key::Right); CHECK(h.ed.sub_tab() == 2 && !h.ed.waiting());
    CHECK(h.take() && h.last_lines == "/lce-nbt list e 16\n" && h.last_expect == Expect::Near);
    h.press(Key::Cross);
    CHECK(h.take() && h.last_lines == "/lce-nbt list e 32\n");
    h.ed.on_near(near_payload("e", {{"e12", "Zombie|Bob|3.2|20.0"}, {"e40", "Item||5.0|"}}).data(), 0 + near_payload("e", {{"e12", "Zombie|Bob|3.2|20.0"}, {"e40", "Item||5.0|"}}).size());
    CHECK(h.ed.screen() == Editor::Screen::Home); h.check_view();
    { View v; h.ed.view(&v); CHECK(!strcmp(v.rows[2].label, "Zombie \"Bob\"") && !strcmp(v.rows[2].value, "3.2m hp 20.0") && v.category == 2); }
    h.press(Key::Down, 2); h.press(Key::Cross); CHECK(h.take_doc() && h.last_lines == "/lce-nbt get e40\n");
    // timeout of a document read returns to the sub-tab it came from
    h.ed.on_timeout(); CHECK(!h.ed.waiting() && strstr(h.ed.status(), "timeout"));
    CHECK(h.ed.screen() == Editor::Screen::Home && h.ed.sub_tab() == 2 && h.ed.home_scroll(2).sel == 2);
    // live refresh: an unedited document re-reads itself every 2 s
    h.press(Key::Up); h.press(Key::Cross); CHECK(h.take_doc() && h.last_lines == "/lce-nbt get e12\n"); h.reply(*player);
    h.ed.tick(h.now); CHECK(!h.take_doc());
    h.ed.tick(h.now + 2500000); CHECK(h.take_doc() && h.last_lines == "/lce-nbt get e12\n");
    h.reply(*player); CHECK(h.ed.edits() == 0);
    // arrays: open Data, edit element, insert
    CHECK(h.select("Data")); h.press(Key::Cross); h.check_view();
    h.press(Key::Cross); CHECK(h.ed.screen() == Editor::Screen::Number); h.press(Key::Right, 4); h.press(Key::Cross);
    { const uint32_t a = node_at(h.ed.work(), Path{{(uint16_t)h.ed.work().index_of(h.ed.work().find(h.ed.work().root, "Data"))}, 1}); CHECK(h.ed.work().array_get(a, 0) == 5); }
    h.press(Key::R1); for (int i = 0; i < 6; ++i) { View v; h.ed.view(&v); if (!strcmp(v.rows[v.selected].label, "Append element")) break; h.press(Key::Down); }
    h.press(Key::Cross);
    { const uint32_t a = h.ed.work().find(h.ed.work().root, "Data"); CHECK(h.ed.work().at(a).v.b.len == 4); }
    // apply for an item target carries the CRC of the copy it was based on
    h.press(Key::Circle); h.press(Key::Circle); h.press(Key::Down); h.press(Key::Cross); h.press(Key::Circle); // discard -> picker
    CHECK(h.ed.screen() == Editor::Screen::Home && h.ed.sub_tab() == 2);
    // every screen renders within the limits
    h.check_view();
}

static void test_scroll_model()
{
    // selection always visible, window moves only when needed, full window at the end
    Scroll s{0, 0};
    for (int i = 0; i < 4; ++i) s.move(1, 20, false);
    CHECK(s.sel == 4 && s.first == 0);                 // still inside the first window
    s.move(1, 20, false); CHECK(s.sel == 5 && s.first == 1);   // one-row scroll, no jump
    s.move(-1, 20, false); CHECK(s.sel == 4 && s.first == 1);  // moving back up does not scroll
    for (int i = 0; i < 30; ++i) s.move(1, 20, true);
    CHECK(s.sel == 19 && s.first == 15);               // held key stops at the end
    s.move(1, 20, false); CHECK(s.sel == 0 && s.first == 0);   // fresh press wraps
    s.move(-1, 20, false); CHECK(s.sel == 19 && s.first == 15);
    s.move(-(int)kVisible, 20, false); CHECK(s.sel == 14 && s.first == 10);   // page keeps the row's place
    s = Scroll{2, 0}; s.move((int)kVisible, 20, false); CHECK(s.sel == 7 && s.first == 5);
    s.move((int)kVisible, 20, false); s.move((int)kVisible, 20, false); s.move((int)kVisible, 20, false);
    CHECK(s.sel == 19 && s.first == 15);               // pages stop at the end
    s.move(-(int)kVisible, 20, true); s.move(-(int)kVisible, 20, true); s.move(-(int)kVisible, 20, true); s.move(-(int)kVisible, 20, true);
    CHECK(s.sel == 0 && s.first == 0);
    s = Scroll{3, 0}; s.move(-50, 200, false); CHECK(s.sel == 0); s.move(50, 200, false); CHECK(s.sel == 50 && s.first == 46);
    // shrinking lists clamp; tiny lists never scroll
    s = Scroll{18, 15}; s.clamp(10); CHECK(s.sel == 9 && s.first == 5);
    s.clamp(3); CHECK(s.sel == 2 && s.first == 0);
    s.clamp(0); CHECK(s.sel == 0 && s.first == 0);
    s = Scroll{0, 0}; s.move(1, 0, false); CHECK(s.sel == 0);
    // invariant sweep
    std::mt19937 rng(7);
    for (int i = 0; i < 20000; ++i) {
        const unsigned n = rng() % 40; const int d[] = {1, -1, 5, -5, 50, -50}; s.move(d[rng() % 6], n, rng() % 2);
        if (n) CHECK(s.sel < n && s.sel >= s.first && s.sel < s.first + kVisible && (n <= kVisible ? s.first == 0 : s.first + kVisible <= n));
    }
}

static void test_subtabs()
{
    Harness h;
    // Left/Right switch Player <-> Item <-> Entity <-> Block (wrapping), each keeps its own row
    CHECK(h.ed.sub_tab() == 0);
    h.press(Key::Down); CHECK(h.ed.home_scroll(0).sel == 1);
    h.press(Key::Right); CHECK(h.ed.sub_tab() == 1);
    CHECK(h.take() && h.last_lines == "/lce-nbt list i\n");           // refresh starts at once, no second press
    { View v; h.ed.view(&v); CHECK(v.category == 1 && !strcmp(v.rows[0].label, "NBT Item (0) ...") && !strcmp(v.rows[1].label, "Held item")); h.check_view(); }
    std::vector<std::pair<const char *, const char *>> inv;
    static char toks[12][8]; for (int i = 0; i < 12; ++i) { snprintf(toks[i], sizeof toks[i], "i%d", i); inv.push_back({toks[i], "1|64|0|"}); }
    Bytes ip = near_payload("i", inv); h.ed.on_near(ip.data(), ip.size());
    CHECK(h.ed.home_rows() == 14);
    for (int i = 0; i < 9; ++i) h.press(Key::Down);
    CHECK(h.ed.home_scroll(1).sel == 9 && h.ed.home_scroll(1).first == 5);
    { View v; h.ed.view(&v); CHECK(v.selected == 5 && !strcmp(v.rows[5].label, "7 stone") && v.more_above && v.more_below); }
    // the Entity tab never shows inventory rows, even before its list arrives
    h.press(Key::Right); CHECK(h.ed.sub_tab() == 2);
    { View v; h.ed.view(&v); CHECK(v.category == 2 && v.count == 3 && !strcmp(v.rows[1].label, "Search radius") && !strcmp(v.rows[1].value, "16 blocks (X: change)") && !strcmp(v.rows[2].label, "Loading...")); }
    CHECK(h.take() && h.last_lines == "/lce-nbt list e 16\n");
    // a late inventory reply while on Entity only updates the Item cache
    Bytes ip2 = near_payload("i", {{"i0", "1|1|0|"}}); h.ed.on_near(ip2.data(), ip2.size());
    { View v; h.ed.view(&v); CHECK(v.count == 3 && !strcmp(v.rows[2].label, "Loading...")); }
    Bytes ep = near_payload("e", {{"e5", "Pig||2.0|10.0"}}); h.ed.on_near(ep.data(), ep.size());
    { View v; h.ed.view(&v); CHECK(v.count == 3 && !strcmp(v.rows[2].label, "Pig")); }
    // back to Item: its row is remembered (clamped to the new, shorter list)
    h.press(Key::Left); CHECK(h.ed.sub_tab() == 1 && h.ed.home_scroll(1).sel == 2 && h.ed.home_scroll(1).first == 0);
    h.press(Key::Left); CHECK(h.ed.sub_tab() == 0 && h.ed.home_scroll(0).sel == 1);
    h.press(Key::Left); CHECK(h.ed.sub_tab() == 3); h.press(Key::Right); CHECK(h.ed.sub_tab() == 0);
    // the list kept under the cursor follows its token when the server reorders it
    h.press(Key::Right); h.press(Key::Right); h.press(Key::Down);              // Entity, row "Pig"
    Bytes ep2 = near_payload("e", {{"e9", "Cow||1.0|10.0"}, {"e5", "Pig||2.0|10.0"}}); h.ed.on_near(ep2.data(), ep2.size());
    CHECK(h.ed.home_scroll(2).sel == 2);
    { View v; h.ed.view(&v); CHECK(!strcmp(v.rows[v.selected].label, "Pig")); }
    // menu-tab keys on Home belong to the tab menu: L1 previous tab, R1 Commands overlay, Circle close
    CHECK(h.ed.key(Key::L1, false) == KeyResult::PassThrough && h.ed.leaves_tab(Key::L1));
    CHECK(h.ed.key(Key::R1, false) == KeyResult::PassThrough && !h.ed.leaves_tab(Key::R1));
    CHECK(h.ed.key(Key::Circle, false) == KeyResult::PassThrough);
    // inside a document they are the editor's (actions / undo), and the bar shows the document's kind
    h.press(Key::Cross); CHECK(h.take_doc() && h.last_lines == "/lce-nbt get e5\n");
    auto d = newdoc(); build_player(*d); Bytes b = ser(*d); h.ed.on_nbt("get", "e5", true, "ok", b.data(), b.size());
    CHECK(h.ed.key(Key::R1, false) == KeyResult::Consumed && h.ed.screen() == Editor::Screen::Actions);
    { View v; h.ed.view(&v); CHECK(v.category == 2); h.check_view(); }
    h.press(Key::Circle); CHECK(h.ed.key(Key::L1, false) == KeyResult::Consumed);
    // tree scrolling: Left/Right page, selection visible
    for (int i = 0; i < 7; ++i) h.press(Key::Down);
    { const Scroll t = h.ed.tree_scroll(); CHECK(t.sel == 7 && t.first == 3); }
    h.press(Key::Right); { const Scroll t = h.ed.tree_scroll(); CHECK(t.sel == 12 && t.first == 8); }
    h.press(Key::Left); h.press(Key::Left); { const Scroll t = h.ed.tree_scroll(); CHECK(t.sel == 2 && t.first == 0); }
    // leaving the document lands on the same sub-tab and row, and refreshes that list
    h.press(Key::Circle); CHECK(h.ed.screen() == Editor::Screen::Home && h.ed.sub_tab() == 2 && h.ed.home_scroll(2).sel == 2);
    CHECK(h.take() && h.last_lines == "/lce-nbt list e 16\n");
    // Item tab > Ender chest slots opens its list (once loaded) and Circle returns to Item
    h.press(Key::Left); h.ed.on_timeout();
    while (h.ed.home_scroll(1).sel != 1) h.press(Key::Up);
    h.press(Key::Cross); CHECK(h.take() && h.last_lines == "/lce-nbt list x\n");
    Bytes xp = near_payload("x", {{"x3", "264|5|0|"}}); h.ed.on_near(xp.data(), xp.size());
    CHECK(h.ed.screen() == Editor::Screen::Picker); { View v; h.ed.view(&v); CHECK(v.category == 1 && !strcmp(v.rows[1].label, "3 diamond")); }
    h.press(Key::Circle); CHECK(h.ed.screen() == Editor::Screen::Home && h.ed.sub_tab() == 1);
}

static void test_held_pin()
{
    Harness h; auto item = newdoc(); mkroot(*item); snbt_build(*item, item->root, "id", "276s"); snbt_build(*item, item->root, "Count", "1b"); snbt_build(*item, item->root, "Damage", "0s");
    h.press(Key::Right); CHECK(h.take() && h.last_expect == Expect::Near); h.press(Key::Cross); CHECK(h.take_doc() && h.last_lines == "/lce-nbt get h\n");
    Bytes b = ser(*item); h.ed.on_nbt("get", "i4", true, "ok", b.data(), b.size());
    CHECK(!strcmp(h.ed.target(), "i4"));
    CHECK(h.select("Count")); h.press(Key::Cross); h.press(Key::Right); h.press(Key::Cross); h.press(Key::Square);
    CHECK(h.take() && h.last_lines.rfind("/lce-nbt tx i4 ", 0) == 0 && h.last_lines.find("/lce-nbt set i4 Count b 2\n") != std::string::npos);
    char crc[16]; snprintf(crc, sizeof crc, "%08x", crc32(b.data(), b.size())); CHECK(h.last_lines.find(crc) != std::string::npos);
}

static void test_parts()
{
    std::unique_ptr<lce_remote::Assembly> a(new lce_remote::Assembly());
    Bytes payload(70001); for (size_t i = 0; i < payload.size(); ++i) payload[i] = (uint8_t)(i * 13);
    auto part = [&](uint32_t id, unsigned idx, unsigned cnt) {
        Bytes p; for (int s = 24; s >= 0; s -= 8) p.push_back((uint8_t)(id >> s));
        p.push_back((uint8_t)(idx >> 8)); p.push_back((uint8_t)idx); p.push_back((uint8_t)(cnt >> 8)); p.push_back((uint8_t)cnt);
        for (int s = 24; s >= 0; s -= 8) p.push_back((uint8_t)(payload.size() >> s));
        const size_t from = idx * 30000, n = std::min<size_t>(30000, payload.size() - from);
        p.insert(p.end(), payload.begin() + from, payload.begin() + from + n); return p;
    };
    const uint8_t *out = nullptr; size_t on = 0;
    Bytes p0 = part(7, 0, 3), p1 = part(7, 1, 3), p2 = part(7, 2, 3);
    CHECK(!lce_remote::assemble(*a, p0.data(), p0.size(), &out, &on));
    CHECK(!lce_remote::assemble(*a, p1.data(), p1.size(), &out, &on));
    CHECK(lce_remote::assemble(*a, p2.data(), p2.size(), &out, &on) && on == payload.size() && !memcmp(out, payload.data(), on));
    // out of order / foreign id restarts
    CHECK(!lce_remote::assemble(*a, p1.data(), p1.size(), &out, &on));
    Bytes q1 = part(8, 1, 3);
    CHECK(!lce_remote::assemble(*a, p0.data(), p0.size(), &out, &on) && !lce_remote::assemble(*a, q1.data(), q1.size(), &out, &on));
    CHECK(!lce_remote::assemble(*a, p2.data(), p2.size(), &out, &on));
    // frame matching for the three channels
    auto frame = [](const char *chan, const Bytes &pl) { Bytes f{250}; size_t n = strlen(chan); f.push_back(0); f.push_back((uint8_t)n); for (size_t i = 0; i < n; ++i) { f.push_back(0); f.push_back((uint8_t)chan[i]); } f.push_back((uint8_t)(pl.size() >> 8)); f.push_back((uint8_t)pl.size()); f.insert(f.end(), pl.begin(), pl.end()); return f; };
    const uint8_t *pp; size_t ps;
    Bytes f = frame("MC|LcePart", p2); CHECK(lce_remote::match_frame(f.data(), f.size(), &pp, &ps) == lce_remote::kPart && ps == p2.size());
    f = frame("MC|LceNear", {1, 2}); CHECK(lce_remote::match_frame(f.data(), f.size(), &pp, &ps) == lce_remote::kNear);
    f = frame("MC|LceNbt", {1}); CHECK(lce_remote::match_frame(f.data(), f.size(), &pp, &ps) == lce_remote::kNbt);
    f = frame("MC|LcePark", {1}); CHECK(lce_remote::match_frame(f.data(), f.size(), &pp, &ps) == 0);
    f = frame("MC|LceNbt", {1}); f.push_back(0); CHECK(lce_remote::match_frame(f.data(), f.size(), &pp, &ps) == 0);
    // reply parsing of a commit
    Bytes r = reply_payload("commit", "p", true, "3 edits applied", Bytes{10, 0, 0, 0});
    lce_remote::Reply rep; CHECK(lce_remote::parse_reply(r.data(), r.size(), &rep) && rep.ok && !strcmp(rep.op, "commit") && rep.nbt_size == 4);
}


// ---- editing model v2: every value editable from the pad ---------------------------------------
static bool run_action(Harness &h, const char *label)
{
    h.press(Key::R1);
    for (int i = 0; i < 40; ++i) { View v; h.ed.view(&v); if (!strcmp(v.rows[v.selected].label, label)) { h.press(Key::Cross); return true; } h.press(Key::Down); }
    h.press(Key::Circle); return false;
}
static bool kb_key(Harness &h, unsigned want)   // walks the keyboard cursor to key `want` (40.. = bottom row), bounded
{
    for (int i = 0; i < 12; ++i) { View v; h.ed.view(&v); const bool bottom = v.kb_sel >= 40; if (bottom == (want >= 40)) break; h.press(Key::Down); }
    for (int i = 0; i < 12; ++i) { View v; h.ed.view(&v); if (v.kb_sel == want) return true; if (want < 40 && v.kb_sel / 10 != want / 10) h.press(v.kb_sel / 10 < want / 10 ? Key::Down : Key::Up); else h.press(Key::Right); }
    View v; h.ed.view(&v); return v.kb_sel == want;
}
static void kb_done(Harness &h) { CHECK(kb_key(h, 43)); h.press(Key::Cross); }
static void open_doc(Harness &h, const char *snbt, const char *token)
{
    auto d = newdoc(); mkroot(*d);
    const uint32_t c = snbt_build(*d, d->root, "p", snbt); CHECK(c != kNil);
    Bytes b(65536); const size_t n = d->serialize_node(c, b.data(), b.size(), true); b.resize(n);
    b[1] = 0; b[2] = 0; b.erase(b.begin() + 3, b.begin() + 4);
    h.ed.on_nbt("get", token, true, "ok", b.data(), b.size());
}
static void test_applier()
{
    auto d = newdoc(); mkroot(*d);
    ScriptApplier a(*d);
    auto ok = [&](const char *l) { const bool r = a.line(l, strlen(l)); if (!r) std::printf("  applier refused %s: %s\n", l, a.err()); return r; };
    auto no = [&](const char *l) { return !a.line(l, strlen(l)); };
    CHECK(ok("/lce-nbt tx p") && ok("/lce-nbt set p Health f 20") && ok("/lce-nbt set p Name T A%20%C2%A7c") && ok("/lce-nbt mk p Inv nc"));
    CHECK(ok("/lce-nbt mk p Inv[+] c") && ok("/lce-nbt set p Inv[0].id t minecraft:stone") && ok("/lce-nbt set p Inv[0].Count b 200"));
    CHECK(d->at(d->find(d->child_at(d->find(d->root, "Inv"), 0), "Count")).v.i == 200 - 256);
    CHECK(no("/lce-nbt set p Inv[0].Count t x") && no("/lce-nbt set p Inv[1].id t x") && no("/lce-nbt add p Inv i 3"));
    CHECK(ok("/lce-nbt mk p Arr I") && ok("/lce-nbt aset p Arr 0 5,6,7") && ok("/lce-nbt set p Arr[1] i -4") && ok("/lce-nbt add p Arr i 9") && ok("/lce-nbt del p Arr[0]"));
    { const uint32_t arr = d->find(d->root, "Arr"); CHECK(d->at(arr).v.b.len == 3 && d->array_get(arr, 0) == -4 && d->array_get(arr, 2) == 9); }
    CHECK(no("/lce-nbt aset p Arr 9 1") && no("/lce-nbt set p Arr[0] f 1.5") && no("/lce-nbt mk p B B") == false);
    CHECK(ok("/lce-nbt ren p Name Label") && d->find(d->root, "Label") != kNil && no("/lce-nbt ren p Label Health"));
    CHECK(ok("/lce-nbt mk p L nd") && ok("/lce-nbt add p L d 1.5") && ok("/lce-nbt add p L d 2") && no("/lce-nbt add p L f 1"));
    CHECK(ok("/lce-nbt mk p G nn") && ok("/lce-nbt mk p G[+] ni") && ok("/lce-nbt mk p G[+] nd") && ok("/lce-nbt mk p G[0] nb"));
    { const uint32_t g = d->find(d->root, "G"); CHECK(d->at(g).count == 2 && d->at(d->child_at(g, 0)).elem == T_BYTE && d->at(d->child_at(g, 1)).elem == T_DOUBLE); }
    CHECK(no("/lce-nbt set p Health b 300") && no("/lce-nbt set p Nope.x b 1") && no("/lce-nbt del p Missing") && no("/lce-nbt set p Health"));
    CHECK(no("/lce-nbt set p Bad%zz b 1") && no("/lce-nbt frob p x b 1") && ok("/lce-nbt commit"));
    CHECK(ok("/lce-nbt set p Flag z true") && d->at(d->find(d->root, "Flag")).v.i == 1);
}
static void test_digits_and_longs()
{
    Harness h;
    h.press(Key::Cross); CHECK(h.take_doc() && h.last_lines == "/lce-nbt get p\n");
    open_doc(h, "{Big:9007199254740993L,Neg:-5,B:3b,F:1.5f,D:0.1d}", "p");
    // +1 on a long beyond 2^53 stays exact
    CHECK(h.select("Big")); h.press(Key::Cross); CHECK(h.ed.screen() == Editor::Screen::Number);
    h.press(Key::Right); h.press(Key::Cross);
    CHECK(h.ed.work().at(h.ed.selected_node()).v.i == 9007199254740994LL);
    // R1 x18 = step 1e18, saturates at the long maximum
    h.press(Key::Cross); h.press(Key::R1, 20); h.press(Key::Right, 12); h.press(Key::Cross);
    CHECK(h.ed.work().at(h.ed.selected_node()).v.i == INT64_MAX);
    // digit editor: Neg -5 -> ones digit up twice = -7, sign flip = +7, tens digit up = +17
    CHECK(h.select("Neg")); h.press(Key::Cross);
    for (int i = 0; i < 12; ++i) { View v; h.ed.view(&v); if (!strcmp(v.rows[v.selected].label, "Edit digit by digit")) break; h.press(Key::Down); }
    h.press(Key::Cross);
    { View v; h.ed.view(&v); CHECK(v.count == 4 && !strcmp(v.rows[1].value, "-000000000[5]")); h.check_view(); }
    h.press(Key::Up, 2); { View v; h.ed.view(&v); CHECK(!strcmp(v.rows[1].value, "-000000000[7]")); }
    h.press(Key::Right); { View v; h.ed.view(&v); CHECK(!strcmp(v.rows[1].value, "[-]0000000007")); }   // wraps to the sign
    h.press(Key::Up); h.press(Key::Left); h.press(Key::Left); h.press(Key::Up);
    { View v; h.ed.view(&v); CHECK(!strcmp(v.rows[1].value, "+00000000[1]7")); }
    h.press(Key::Cross); CHECK(h.ed.screen() == Editor::Screen::Number); h.press(Key::Cross);
    CHECK(h.ed.work().at(h.ed.selected_node()).v.i == 17);
    // out of range digits are refused (byte 999)
    CHECK(h.select("B")); h.press(Key::Cross);
    for (int i = 0; i < 12; ++i) { View v; h.ed.view(&v); if (!strcmp(v.rows[v.selected].label, "Edit digit by digit")) break; h.press(Key::Down); }
    h.press(Key::Cross); h.press(Key::L1); h.press(Key::Right); h.press(Key::Down);   // hundreds 0 -> 9 = 903
    h.press(Key::Cross); CHECK(strstr(h.ed.status(), "Out of range"));
    h.press(Key::Circle); h.press(Key::Circle); CHECK(h.ed.work().at(h.ed.selected_node()).v.i == 3);
    // float digits: 1.5 -> mantissa first digit up -> 2.5
    CHECK(h.select("F")); h.press(Key::Cross);
    for (int i = 0; i < 12; ++i) { View v; h.ed.view(&v); if (!strcmp(v.rows[v.selected].label, "Edit digit by digit")) break; h.press(Key::Down); }
    h.press(Key::Cross); { View v; h.ed.view(&v); CHECK(!strcmp(v.rows[1].value, "+[1].500000E+00")); }
    h.press(Key::Up); h.press(Key::Cross); h.press(Key::Cross);
    CHECK(h.ed.work().at(h.ed.selected_node()).v.d == 2.5);
    // double exponent digits: 0.1 -> exponent -01 -> -02 = 0.01
    CHECK(h.select("D")); h.press(Key::Cross);
    for (int i = 0; i < 12; ++i) { View v; h.ed.view(&v); if (!strcmp(v.rows[v.selected].label, "Edit digit by digit")) break; h.press(Key::Down); }
    h.press(Key::Cross); h.press(Key::R1); h.press(Key::Up); h.press(Key::Cross); h.press(Key::Cross);
    CHECK(h.ed.work().at(h.ed.selected_node()).v.d == 0.01);
    // the apply script carries the exact long
    h.press(Key::Square); CHECK(h.take() && h.last_lines.find("/lce-nbt set p Big l 9223372036854775807\n") != std::string::npos);
}
static void test_search_and_values()
{
    Harness h; auto player = newdoc(); build_player(*player);
    h.press(Key::Cross); CHECK(h.take_doc()); h.reply(*player);
    // search through the actions menu (keyboard): "sharp" matches the enchantment id by its pick name
    CHECK(run_action(h, "Search...")); CHECK(h.ed.screen() == Editor::Screen::Text);
    const char *word = "sharp";
    for (const char *c = word; *c; ++c) {   // type with the keyboard: find the key, press Cross
        unsigned target = 99; for (unsigned k = 0; k < 40; ++k) if (menu_feedback::keyboard_key(k, false, false) == *c) target = k;
        CHECK(target < 40 && kb_key(h, target)); h.press(Key::Cross);
    }
    { View v; h.ed.view(&v); CHECK(v.kb_len == 5); }
    kb_done(h);
    CHECK(h.ed.screen() == Editor::Screen::Tree && !strcmp(h.ed.search_text(), "sharp"));
    { View v; h.ed.view(&v); CHECK(!strcmp(v.rows[0].label, "Me>Inventory[0]>tag>ench[0]") && h.ed.work().name_eq(h.ed.selected_node(), "id")); CHECK(strstr(h.ed.status(), "Match 1/1")); }
    // R2 cycles (one match wraps onto itself), a new search for "level" finds foodLevel and XpLevel in order
    h.press(Key::R2); CHECK(h.ed.work().name_eq(h.ed.selected_node(), "id"));
    CHECK(run_action(h, "Search...")); CHECK(kb_key(h, 42));
    for (int i = 0; i < 5; ++i) h.press(Key::Cross);   // backspace the old text
    { View v; h.ed.view(&v); CHECK(v.kb_len == 0); }
    // type "level" by key index
    for (const char *c = "level"; *c; ++c) {
        unsigned target = 99; for (unsigned k = 0; k < 40; ++k) if (menu_feedback::keyboard_key(k, false, false) == *c) target = k;
        CHECK(target < 40 && kb_key(h, target)); h.press(Key::Cross);
    }
    kb_done(h);
    CHECK(h.ed.work().name_eq(h.ed.selected_node(), "foodLevel") && strstr(h.ed.status(), "Match 1/2"));
    h.press(Key::R2); CHECK(h.ed.work().name_eq(h.ed.selected_node(), "XpLevel") && strstr(h.ed.status(), "Match 2/2"));
    h.press(Key::R2); CHECK(h.ed.work().name_eq(h.ed.selected_node(), "foodLevel"));
    h.press(Key::L2); CHECK(h.ed.work().name_eq(h.ed.selected_node(), "XpLevel"));
    // copy a value and paste it into a tag of another type: Health (float 20) -> XpLevel (int)
    CHECK(h.select("Health")); CHECK(run_action(h, "Copy"));
    CHECK(h.select("XpLevel")); CHECK(run_action(h, "Paste value"));
    CHECK(h.ed.work().at(h.ed.selected_node()).v.i == 20 && strstr(h.ed.status(), "Pasted value"));
    // array elements: copy [2] = 3, paste over [0], move [0] down
    CHECK(h.select("Data")); h.press(Key::Cross); h.press(Key::Down, 2);
    CHECK(run_action(h, "Copy")); h.press(Key::Up, 2); CHECK(run_action(h, "Paste value"));
    const uint32_t arr = [&] { uint32_t r = kNil; for (uint32_t c = h.ed.work().at(h.ed.work().root).first; c != kNil; c = h.ed.work().at(c).next) if (h.ed.work().name_eq(c, "Data")) r = c; return r; }();
    CHECK(h.ed.work().array_get(arr, 0) == 3 && h.ed.work().array_get(arr, 1) == 2);
    CHECK(run_action(h, "Move element down")); CHECK(h.ed.work().array_get(arr, 0) == 2 && h.ed.work().array_get(arr, 1) == 3 && h.ed.tree_scroll().sel == 1);
    h.press(Key::L1); CHECK(h.ed.work().array_get(arr, 0) == 3);   // undo
    h.press(Key::Circle);
    // apply replays on the simulated server
    h.press(Key::Square); CHECK(h.take());
    auto server = newdoc(); { Bytes b = ser(*player); server->parse(b.data(), b.size()); }
    Sim sim(*server); CHECK(sim.script(h.last_lines.c_str())); CHECK(same_node(*server, server->root, h.ed.work(), h.ed.work().root));
}
static void test_long_text()
{
    Harness h;
    h.press(Key::Cross); CHECK(h.take_doc());
    std::string big(250, 'x'); for (size_t i = 0; i < big.size(); ++i) big[i] = (char)('a' + i % 26);
    open_doc(h, ("{Page:\"" + big + "\"}").c_str(), "p");
    CHECK(h.select("Page")); h.press(Key::Cross);
    CHECK(h.ed.screen() == Editor::Screen::Choice);
    { View v; h.ed.view(&v); CHECK(!strcmp(v.rows[0].label, "Long text: 250 chars") && !strcmp(v.rows[1].label, "Part 1 (1-100)") && !strcmp(v.rows[3].label, "Part 3 (201-250)") && !strcmp(v.rows[4].label, "Append at the end")); h.check_view(); }
    // part 2: clear it (L1) and type one letter -> the text loses 99 characters in the middle
    h.press(Key::Down); h.press(Key::Cross); CHECK(h.ed.screen() == Editor::Screen::Text);
    { View v; h.ed.view(&v); CHECK(v.kb_len == 100 && !strcmp(v.rows[0].label, "Edit text part 2/3")); }
    h.press(Key::L1); h.press(Key::Cross);   // 'key 0'
    kb_done(h);
    const uint32_t s0 = h.ed.selected_node(); const Node &x = h.ed.work().at(s0);
    std::string got((const char *)h.ed.work().bytes(x.v.b.off), x.v.b.len);
    std::string want = big.substr(0, 100) + std::string(1, menu_feedback::keyboard_key(0, false, false)) + big.substr(200);
    CHECK(got == want);
    // append at the end
    h.press(Key::Cross); h.press(Key::Down, 3); h.press(Key::Cross); h.press(Key::Cross);
    kb_done(h);
    { const Node &y = h.ed.work().at(h.ed.selected_node()); CHECK(y.v.b.len == 152); }
    h.press(Key::L1); { const Node &y = h.ed.work().at(h.ed.selected_node()); CHECK(y.v.b.len == 151); }
}
static void test_world_targets()
{
    Harness h;
    // Player tab: Me, Level data, Chunk here, Scoreboard
    CHECK(h.ed.home_rows() == 4);
    h.press(Key::Down, 2); h.press(Key::Cross); CHECK(h.take_doc() && h.last_lines == "/lce-nbt get kh\n");
    { auto c = newdoc(); mkroot(*c); snbt_build(*c, c->root, "xPos", "0"); snbt_build(*c, c->root, "zPos", "-1");
      snbt_build(*c, c->root, "Biomes", "[B;1,1,1,-127]"); snbt_build(*c, c->root, "InhabitedTime", "5L"); snbt_build(*c, c->root, "TerrainPopulated", "1022s");
      Bytes b = ser(*c); h.ed.on_nbt("get", "k0,-1", true, "ok", b.data(), b.size()); }
    CHECK(!strcmp(h.ed.target(), "k0,-1"));
    CHECK(h.select("xPos")); h.press(Key::Cross); CHECK(strstr(h.ed.status(), "chunk position"));
    // biome ids above 127 are shown and picked as 0-255
    CHECK(h.select("Biomes")); h.press(Key::Cross); h.press(Key::Down, 3);
    { View v; h.ed.view(&v); CHECK(!strcmp(v.rows[v.selected].value, "129 Sunflowers Plains")); }
    h.press(Key::Cross); CHECK(h.ed.screen() == Editor::Screen::Number); h.press(Key::Triangle); CHECK(h.ed.screen() == Editor::Screen::Choice);
    for (int i = 0; i < 80; ++i) { View v; h.ed.view(&v); if (!strcmp(v.rows[v.selected].label, "Mega Spruce Taiga")) break; h.press(Key::Down); }
    h.press(Key::Cross); h.press(Key::Cross);
    h.press(Key::Square); CHECK(h.take() && h.last_lines.find("/lce-nbt tx k0,-1\n") == 0 && h.last_lines.find("Biomes") != std::string::npos);
    { auto c = newdoc(); mkroot(*c); snbt_build(*c, c->root, "xPos", "0"); snbt_build(*c, c->root, "zPos", "-1");
      snbt_build(*c, c->root, "Biomes", "[B;1,1,1,-127]"); snbt_build(*c, c->root, "InhabitedTime", "5L"); snbt_build(*c, c->root, "TerrainPopulated", "1022s");
      Sim sim(*c); CHECK(sim.script(h.last_lines.c_str())); CHECK(c->array_get(c->find(c->root, "Biomes"), 3) == 160 - 256); }
    h.ed.on_timeout(); h.press(Key::Circle); h.press(Key::Circle); h.press(Key::Circle);
    h.ed.reset();
    // Block tab: radius, below feet, at feet, at head, then block entities
    h.press(Key::Left); CHECK(h.ed.sub_tab() == 3); CHECK(h.take() && h.last_lines == "/lce-nbt list b 16\n");
    CHECK(h.ed.home_rows() == 4);
    { View v; h.ed.view(&v); CHECK(!strcmp(v.rows[2].label, "Block below feet") && !strcmp(v.rows[4].label, "Block at head")); h.check_view(); }
    h.press(Key::Down); h.press(Key::Cross); CHECK(h.take_doc() && h.last_lines == "/lce-nbt get wb\n");
    const char *wsn = "{x:1,y:63,z:2,id:54s,Data:2b,Biome:1b,TileEntity:{id:\"Chest\",x:1,y:63,z:2,Items:[;c]}}";
    open_doc(h, wsn, "w1,63,2");
    CHECK(!strcmp(h.ed.target(), "w1,63,2"));
    { View v; h.ed.view(&v); CHECK(v.category == 3); }
    CHECK(h.select("id")); { char d[40]; describe_value(h.ed.work(), h.ed.selected_node(), K_WORLD, d, sizeof d); CHECK(strstr(d, "chest") != nullptr); }
    CHECK(hint_for(h.ed.work(), h.ed.selected_node(), K_WORLD).pick == Pick::Block);
    CHECK(h.select("TileEntity")); h.press(Key::Cross); CHECK(h.select("id"));
    CHECK(hint_for(h.ed.work(), h.ed.selected_node(), K_WORLD).pick == Pick::TileEntityType);
    h.press(Key::Circle);
    // move the block up one: y 63 -> 64; the server answers with the new token, which the editor keeps
    CHECK(h.select("y")); h.press(Key::Cross); h.press(Key::Right); h.press(Key::Cross);
    h.press(Key::Square); CHECK(h.take() && h.last_lines == "/lce-nbt tx w1,63,2\n/lce-nbt set w1,63,2 y i 64\n/lce-nbt commit\n");
    { std::string moved = wsn; moved.replace(moved.find("y:63"), 4, "y:64"); auto d = newdoc(); mkroot(*d); const uint32_t c = snbt_build(*d, d->root, "p", moved.c_str());
      Bytes b(65536); const size_t n = d->serialize_node(c, b.data(), b.size(), true); b.resize(n); b[1] = 0; b[2] = 0; b.erase(b.begin() + 3, b.begin() + 4);
      h.ed.on_nbt("commit", "w1,64,2", true, "1 edits applied", b.data(), b.size()); }
    CHECK(!strcmp(h.ed.target(), "w1,64,2") && h.ed.edits() == 0);
    h.press(Key::R1); h.press(Key::Circle);
    // live refresh / reload now asks for the moved block
    CHECK(run_action(h, "Reload from server")); CHECK(h.take() && h.last_lines == "/lce-nbt get w1,64,2\n");
}
static void test_entity_and_protection()
{
    Harness h;
    h.press(Key::Right, 2); h.ed.on_timeout();
    Bytes ep = near_payload("e", {{"e5", "Zombie||2.0|20.0"}}); h.ed.on_near(ep.data(), ep.size());
    h.press(Key::Down); h.press(Key::Cross); CHECK(h.take_doc() && h.last_lines == "/lce-nbt get e5\n");
    open_doc(h, "{id:\"Zombie\",UUID:\"ent1\",Dimension:0,Pos:[0.0d,64.0d,0.0d],Health:20.0f}", "e5");
    // id, UUID and Dimension are editable on entities; id picks an entity type
    CHECK(h.select("id")); CHECK(!hint_for(h.ed.work(), h.ed.selected_node(), K_ENTITY).readonly);
    h.press(Key::Cross); CHECK(h.ed.screen() == Editor::Screen::Choice);
    for (int i = 0; i < 80; ++i) { View v; h.ed.view(&v); if (!strcmp(v.rows[v.selected].label, "Skeleton")) break; h.press(Key::Down); }
    h.press(Key::Cross);
    CHECK(h.select("Dimension")); h.press(Key::Cross); h.press(Key::Triangle); h.press(Key::Up); h.press(Key::Cross); h.press(Key::Cross);
    CHECK(h.ed.work().at(h.ed.selected_node()).v.i == -1);
    h.press(Key::Square); { const bool t = h.take(); if (getenv("NBT_DEBUG")) std::printf("%s|%s\n", h.last_lines.c_str(), h.ed.status()); CHECK(t); }
    CHECK(h.last_lines.find("/lce-nbt set e5 id t Skeleton\n") != std::string::npos && h.last_lines.find("/lce-nbt set e5 Dimension i -1\n") != std::string::npos);
    // the respawned entity has a new network id: the editor follows it
    { auto d = newdoc(); mkroot(*d); snbt_build(*d, d->root, "id", "\"Skeleton\""); Bytes b = ser(*d); h.ed.on_nbt("commit", "e77", true, "2 edits applied", b.data(), b.size()); }
    CHECK(!strcmp(h.ed.target(), "e77"));
    // a reply for another kind of target never retargets the document
    { auto d = newdoc(); mkroot(*d); Bytes b = ser(*d); h.press(Key::R1); h.press(Key::Circle); CHECK(run_action(h, "Reload from server")); CHECK(h.take()); h.ed.on_nbt("get", "p", true, "ok", b.data(), b.size()); }
    CHECK(!strcmp(h.ed.target(), "e77"));
    // block entity: id stays read-only with the reason, x/y/z are editable
    auto d = newdoc(); mkroot(*d); snbt_build(*d, d->root, "id", "\"Chest\""); snbt_build(*d, d->root, "x", "1");
    CHECK(protected_key(K_BLOCK, *d, d->find(d->root, "id")) && !protected_key(K_BLOCK, *d, d->find(d->root, "x")));
    CHECK(!protected_key(K_LEVEL, *d, d->find(d->root, "id")) && !strcmp(protected_reason(K_BLOCK), "Read-only: change the block (World block)"));
}

// ---- local-world backend against fake natives with the 1.95 object layouts ----------------------
namespace fake195 {
using namespace lce_local_nbt;
static uint8_t *image;                      // fake image: vtables live at image + kTagVtable[t]
static size_t live_objects;
static void *alloc(size_t n) { ++live_objects; return calloc(1, n); }
static void free_(void *p) { if (p) { --live_objects; free(p); } }
static void *alloc_array(size_t n) { ++live_objects; return calloc(1, n ? n : 1); }
static void set_vt(void *o, unsigned t) { const uintptr_t vt = (uintptr_t)image + kTagVtable[t]; memcpy(o, &vt, 8); }
static uint64_t q(const void *o, size_t off) { uint64_t v; memcpy(&v, (const uint8_t *)o + off, 8); return v; }
static void setq(void *o, size_t off, uint64_t v) { memcpy((uint8_t *)o + off, &v, 8); }
static void ws_assign(void *ws, const uint16_t *u, uint64_t n)
{
    if (q(ws, 0x20) >= 8) free_((void *)q(ws, 8));
    if (n <= 7) { memcpy((uint8_t *)ws + 8, u, n * 2); memset((uint8_t *)ws + 8 + n * 2, 0, 2); setq(ws, 0x20, 7); }
    else { void *b = alloc((n + 1) * 2); memcpy(b, u, n * 2); setq(ws, 8, (uint64_t)b); setq(ws, 0x20, n); }
    setq(ws, 0x18, n);
}
static void ws_copy(void *dst, const void *src) { memset(dst, 0, kWstring); setq(dst, 0x20, 7); const uint64_t n = q(src, 0x18); const uint16_t *u = (const uint16_t *)(q(src, 0x20) <= 7 ? (const uint8_t *)src + 8 : (const uint8_t *)q(src, 8)); ws_assign(dst, u, n); }
static void ws_drop(void *ws) { if (q(ws, 0x20) >= 8) free_((void *)q(ws, 8)); }
static bool ws_eq(const void *a, const void *b)
{
    const uint64_t n = q(a, 0x18); if (n != q(b, 0x18)) return false;
    const void *ua = q(a, 0x20) <= 7 ? (const uint8_t *)a + 8 : (const void *)q(a, 8), *ub = q(b, 0x20) <= 7 ? (const uint8_t *)b + 8 : (const void *)q(b, 8);
    return !memcmp(ua, ub, n * 2);
}
static unsigned type_of(const void *o) { const uintptr_t vt = (uintptr_t)q(o, 0); for (unsigned t = 1; t <= 11; ++t) if (vt == (uintptr_t)image + kTagVtable[t]) return t; return 0; }
static void dtor(void *o)
{
    if (!o) return;
    const unsigned t = type_of(o);
    if (t == T_STRING) ws_drop((uint8_t *)o + 8);
    if (t == T_BYTE_ARRAY || t == T_INT_ARRAY) free_((void *)q(o, 8));
    if (t == T_LIST) { for (uint64_t p = q(o, 0x10); p && p < q(o, 0x18); p += 8) dtor((void *)q((void *)p, 0)); free_((void *)q(o, 0x10)); }
    if (t == T_COMPOUND) {
        void *sent = (void *)q(o, 0x10);
        for (void *n = (void *)q(sent, 0); n != sent;) { void *next = (void *)q(n, 0); ws_drop((uint8_t *)n + 0x10); dtor((void *)q(n, 0x38)); free_(n); n = next; }
        free_(sent);
    }
    free_(o);
}
static void compound_ctor(void *o) { set_vt(o, T_COMPOUND); void *sent = alloc(0x40); setq(sent, 0, (uint64_t)sent); setq(sent, 8, (uint64_t)sent); setq(o, 0x10, (uint64_t)sent); setq(o, 0x18, 0); }
static void put_tag(void *o, const void *key, void *child)
{
    void *sent = (void *)q(o, 0x10);
    for (void *n = (void *)q(sent, 0); n != sent; n = (void *)q(n, 0)) if (ws_eq((uint8_t *)n + 0x10, key)) { dtor((void *)q(n, 0x38)); setq(n, 0x38, (uint64_t)child); return; }
    void *n = alloc(0x40); ws_copy((uint8_t *)n + 0x10, key); setq(n, 0x38, (uint64_t)child);
    void *last = (void *)q(sent, 8); setq(last, 0, (uint64_t)n); setq(n, 8, (uint64_t)last); setq(n, 0, (uint64_t)sent); setq(sent, 8, (uint64_t)n);
    setq(o, 0x18, q(o, 0x18) + 1);
}
static void list_ctor(void *o, int t) { set_vt(o, T_LIST); ((uint8_t *)o)[0x28] = (uint8_t)t; }
static void list_grow(void *vec, uint64_t)
{
    void *list = (uint8_t *)vec - 8; const uint64_t b = q(list, 0x10), e = q(list, 0x18), c = q(list, 0x20);
    const uint64_t n = (e - b) / 8, cap = c ? (c - b) / 8 * 2 : 2;
    void *nb = alloc(cap * 8); if (b) { memcpy(nb, (void *)b, n * 8); free_((void *)b); }
    setq(list, 0x10, (uint64_t)nb); setq(list, 0x18, (uint64_t)nb + n * 8); setq(list, 0x20, (uint64_t)nb + cap * 8);
}
static void c_byte(void *o, int v) { set_vt(o, T_BYTE); ((uint8_t *)o)[8] = (uint8_t)v; }
static void c_short(void *o, int v) { set_vt(o, T_SHORT); const int16_t x = (int16_t)v; memcpy((uint8_t *)o + 8, &x, 2); }
static void c_int(void *o, int v) { set_vt(o, T_INT); memcpy((uint8_t *)o + 8, &v, 4); }
static void c_long(void *o, int64_t v) { set_vt(o, T_LONG); memcpy((uint8_t *)o + 8, &v, 8); }
static void c_float(void *o, float v) { set_vt(o, T_FLOAT); memcpy((uint8_t *)o + 8, &v, 4); }
static void c_double(void *o, double v) { set_vt(o, T_DOUBLE); memcpy((uint8_t *)o + 8, &v, 8); }
static void c_string(void *o, const void *ws) { set_vt(o, T_STRING); ws_copy((uint8_t *)o + 8, ws); }
static void c_barr(void *o, void *d, int n, int) { set_vt(o, T_BYTE_ARRAY); setq(o, 8, (uint64_t)d); memcpy((uint8_t *)o + 0x10, &n, 4); }
static void c_iarr(void *o, void *d, int n) { set_vt(o, T_INT_ARRAY); setq(o, 8, (uint64_t)d); memcpy((uint8_t *)o + 0x10, &n, 4); }
static int rd(void *, uintptr_t a, void *o, size_t n) { memcpy(o, (const void *)a, n); return 1; }
// the "player": its NBT as bytes; save/load go through the native graph code under test
struct Player { Bytes nbt; double tp[3]; int mode_set = -1, loads = 0; };
static Natives *nat;
static void *entity_save(uintptr_t pl, void *tag)
{
    Player *p = (Player *)pl; auto d = newdoc(); CHECK(d->parse(p->nbt.data(), p->nbt.size()));
    Builder b(*nat); void *tmp = (void *)b.compound(*d, d->root); CHECK(tmp != nullptr);
    void *sent = (void *)q(tmp, 0x10);
    for (void *node = (void *)q(sent, 0); node != sent; node = (void *)q(node, 0)) { void *child = (void *)q(node, 0x38); setq(node, 0x38, 0); put_tag(tag, (uint8_t *)node + 0x10, child); }
    dtor(tmp); return tag;
}
static void entity_load(uintptr_t pl, void *tag)
{
    Player *p = (Player *)pl; static uint8_t buf[kMaxDocBytes]; size_t n = 0;
    Encoder e(*nat, buf, sizeof buf); CHECK(e.encode((uintptr_t)tag, &n) == OK);
    p->nbt.assign(buf, buf + n); ++p->loads;
}
static void teleport(uintptr_t pl, double x, double y, double z) { Player *p = (Player *)pl; p->tp[0] = x; p->tp[1] = y; p->tp[2] = z; }
static void set_mode(uintptr_t pl, uintptr_t gt)
{
    Player *p = (Player *)pl; p->mode_set = gt == 0x1000 ? 0 : 1;
    auto d = newdoc(); d->parse(p->nbt.data(), p->nbt.size()); d->set_integer(d->find(d->root, "playerGameType"), p->mode_set); p->nbt = ser(*d);
}
static void install(Natives &n)
{
    if (!image) {
        image = (uint8_t *)calloc(1, 0x1300000);
        typedef void (*D)(void *); D dd = dtor;
        for (unsigned t = 1; t <= 11; ++t) memcpy(image + kTagVtable[t] + 0x30, &dd, sizeof dd);
    }
    n.image = (uintptr_t)image; n.read = rd; n.alloc = alloc; n.free_ = free_; n.alloc_array = alloc_array; n.ws_assign = ws_assign;
    n.compound_ctor = compound_ctor; n.put_tag = put_tag; n.list_ctor = list_ctor; n.list_grow = list_grow;
    n.ctor_byte = c_byte; n.ctor_short = c_short; n.ctor_int = c_int; n.ctor_long = c_long; n.ctor_float = c_float; n.ctor_double = c_double;
    n.ctor_string = c_string; n.ctor_byte_array = c_barr; n.ctor_int_array = c_iarr;
    n.entity_save = entity_save; n.entity_load = entity_load; n.teleport = teleport; n.set_game_mode = set_mode; n.game_type[0] = 0x1000; n.game_type[1] = 0x1001;
    nat = &n;
}
}  // namespace fake195

static void test_local_backend()
{
    using namespace lce_local_nbt;
    static Natives n; fake195::install(n);
    std::unique_ptr<Doc> live = newdoc(), work = newdoc();
    std::unique_ptr<Backend> be(new Backend(live.get(), work.get()));
    const char *snbt = "{Pos:[1.5d,64.0d,-3.25d],Motion:[0.0d,0.0d,0.0d],Rotation:[90.0f,0.0f],Health:20.0f,foodLevel:20,playerGameType:0,"
        "UUIDMost:5L,UUIDLeast:6L,Dimension:0,XpLevel:5,Name:\"Steve \xC2\xA7" "cRed name that is longer than seven\",Arr:[I;1,-2,3],Bytes:[B;1,-1],"
        "Inventory:[{Slot:0b,id:\"minecraft:diamond_sword\",Count:1b,tag:{Damage:3,Enchantments:[{id:\"minecraft:sharpness\",lvl:5s}]}}],abilities:{flying:0b,walkSpeed:0.1f},Tags:[\"a\",\"b\"]}";
    auto d0 = newdoc(); mkroot(*d0); const uint32_t c = snbt_build(*d0, d0->root, "p", snbt); CHECK(c != kNil);
    Bytes b(65536); const size_t len = d0->serialize_node(c, b.data(), b.size(), true); b.resize(len); b[1] = 0; b[2] = 0; b.erase(b.begin() + 3, b.begin() + 4);
    fake195::Player pl; pl.nbt = b;
    const size_t objects0 = fake195::live_objects;
    // get: native save -> encoder round trip is exact
    Backend::Reply r;
    CHECK(be->post("/lce-nbt get p\n", 15) && be->busy() && !be->take(&r));
    be->process(n, (uintptr_t)&pl); CHECK(be->take(&r) && r.ok && !strcmp(r.op, "get"));
    if (getenv("NBT_DEBUG")) { std::printf("get n=%zu want=%zu msg=%s\n", r.n, b.size(), r.msg); for (size_t i = 0; i < r.n && i < 96; ++i) std::printf("%02x", r.nbt[i]); std::printf("\n"); for (size_t i = 0; i < 96; ++i) std::printf("%02x", b[i]); std::printf("\n"); }
    { auto got = newdoc(); CHECK(got->parse(r.nbt, r.n)); if (!got->valid()) return; auto want = newdoc(); want->parse(b.data(), b.size()); CHECK(same_node(*got, got->root, *want, want->root)); }
    CHECK(fake195::live_objects == objects0);   // every native object of the snapshot was released
    // the editor drives the backend with its normal script; edits replay on a fresh snapshot
    Harness h;
    h.press(Key::Cross); CHECK(h.take_doc());
    h.ed.on_nbt("get", "p", true, "ok", r.nbt, r.n);
    CHECK(h.select("Health")); h.press(Key::Cross); h.press(Key::Left); h.press(Key::Cross);
    CHECK(h.select("playerGameType")); h.press(Key::Cross); h.press(Key::Right); h.press(Key::Cross);
    CHECK(h.select("Pos")); h.press(Key::Cross); h.press(Key::Down); h.press(Key::Cross); h.press(Key::R1); h.press(Key::Right); h.press(Key::Cross); h.press(Key::Circle);
    h.press(Key::Square); CHECK(h.take());
    // meanwhile the game changed foodLevel: the commit keeps it (diff, not a whole-document overwrite)
    { auto d = newdoc(); d->parse(pl.nbt.data(), pl.nbt.size()); d->set_integer(d->find(d->root, "foodLevel"), 7); pl.nbt = ser(*d); }
    CHECK(be->post(h.last_lines.data(), h.last_lines.size()));
    be->process(n, (uintptr_t)&pl); CHECK(be->take(&r) && r.ok && !strcmp(r.op, "commit") && strstr(r.msg, "3 edits"));
    { auto d = newdoc(); CHECK(d->parse(pl.nbt.data(), pl.nbt.size()));
      CHECK(d->at(d->find(d->root, "Health")).v.d == 19.0 && d->at(d->find(d->root, "foodLevel")).v.i == 7 && d->at(d->find(d->root, "playerGameType")).v.i == 1);
      CHECK(d->at(d->child_at(d->find(d->root, "Pos"), 1)).v.d == 74.0); }
    CHECK(pl.tp[1] == 74.0 && pl.mode_set == 1 && pl.loads == 1);   // teleport resync + full game-mode setter
    h.ed.on_nbt(r.op, r.target, r.ok, r.msg, r.nbt, r.n); CHECK(h.ed.edits() == 0 && strstr(h.ed.status(), "applied"));
    CHECK(fake195::live_objects == objects0);
    // identity and dimension are refused, nothing is loaded
    const char *bad = "/lce-nbt tx p\n/lce-nbt set p UUIDMost l 9\n/lce-nbt commit\n";
    CHECK(be->post(bad, strlen(bad))); be->process(n, (uintptr_t)&pl); CHECK(be->take(&r) && !r.ok && strstr(r.msg, "identity") && pl.loads == 1);
    const char *dim = "/lce-nbt tx p\n/lce-nbt set p Dimension i -1\n/lce-nbt commit\n";
    CHECK(be->post(dim, strlen(dim))); be->process(n, (uintptr_t)&pl); CHECK(be->take(&r) && !r.ok && strstr(r.msg, "portal"));
    const char *adv = "/lce-nbt tx p\n/lce-nbt set p playerGameType i 2\n/lce-nbt commit\n";
    CHECK(be->post(adv, strlen(adv))); be->process(n, (uintptr_t)&pl); CHECK(be->take(&r) && !r.ok && strstr(r.msg, "survival"));
    const char *pos = "/lce-nbt tx p\n/lce-nbt set p Pos[1] d 5000\n/lce-nbt commit\n";
    CHECK(be->post(pos, strlen(pos))); be->process(n, (uintptr_t)&pl); CHECK(be->take(&r) && !r.ok && strstr(r.msg, "Pos"));
    const char *bad_edit = "/lce-nbt tx p\n/lce-nbt set p Health t x\n/lce-nbt commit\n";
    CHECK(be->post(bad_edit, strlen(bad_edit))); be->process(n, (uintptr_t)&pl); CHECK(be->take(&r) && !r.ok && strstr(r.msg, "edit 1"));
    // structural edits incl. lists, arrays and long strings reach the native graph
    const char *more = "/lce-nbt tx p\n/lce-nbt mk p Inventory[+] c\n/lce-nbt set p Inventory[1].Slot b 1\n/lce-nbt set p Inventory[1].id t minecraft:stone\n"
                       "/lce-nbt set p Inventory[1].Count b 64\n/lce-nbt aset p Arr 3 7,8\n/lce-nbt add p Tags t c\n/lce-nbt set p Name t A much longer replacement name\n/lce-nbt commit\n";
    CHECK(be->post(more, strlen(more))); be->process(n, (uintptr_t)&pl); CHECK(be->take(&r) && r.ok);
    { auto d = newdoc(); d->parse(pl.nbt.data(), pl.nbt.size()); CHECK(d->at(d->find(d->root, "Inventory")).count == 2 && d->at(d->find(d->root, "Arr")).v.b.len == 5 && d->at(d->find(d->root, "Tags")).count == 3); }
    // server undo: back to the state before the last commit
    CHECK(be->undo_depth() == 2);
    CHECK(be->post("/lce-nbt undo p\n", 16)); be->process(n, (uintptr_t)&pl); CHECK(be->take(&r) && r.ok && !strcmp(r.op, "undo"));
    { auto d = newdoc(); d->parse(pl.nbt.data(), pl.nbt.size()); CHECK(d->at(d->find(d->root, "Inventory")).count == 1 && d->at(d->find(d->root, "Health")).v.d == 19.0); }
    // other targets need an LCE server
    CHECK(be->post("/lce-nbt get e5\n", 16)); be->process(n, (uintptr_t)&pl); CHECK(be->take(&r) && !r.ok && strstr(r.msg, "local world"));
    CHECK(fake195::live_objects == objects0);
    // the editor in local mode: only Me on the Player tab, the other tabs explain why
    Harness lh; lh.ed.set_local(true);
    CHECK(lh.ed.home_rows() == 2); lh.press(Key::Down); lh.press(Key::Cross); CHECK(lh.ed.take_stock_request() && !lh.ed.take_stock_request()); lh.press(Key::Up);
    lh.press(Key::Right); CHECK(!lh.take());
    { View v; lh.ed.view(&v); CHECK(v.count == 2 && v.rows[1].role == R_STATUS && strstr(v.rows[1].label, "local")); lh.check_view(); }
    lh.press(Key::Left); lh.press(Key::Cross); CHECK(lh.take_doc() && lh.last_lines == "/lce-nbt get p\n");
}

// Controller flow: every screen's footer names what Cross does there and how Circle goes back one
// level; pending changes are announced and first in the actions menu; Square in the number editor
// keeps the value and applies it; Circle from a long-text part returns to the part list.
static std::string footer(Harness &h, const Bytes &doc)
{
    h.now += 5000000; h.ed.tick(h.now);   // let the last message expire (an unedited document re-reads itself quietly)
    if (h.take_doc()) h.ed.on_nbt("get", "p", true, "ok", doc.data(), doc.size());
    View v; h.ed.view(&v); CHECK(strlen(v.status) <= 60); return v.status;
}
static void test_controller_flow()
{
    Harness h;
    auto d = newdoc(); mkroot(*d);
    const uint32_t c = snbt_build(*d, d->root, "p", "{Invulnerable:0b,foodLevel:20,Sub:{A:1},Note:\"hi\"}"); CHECK(c != kNil);
    Bytes doc(65536); doc.resize(d->serialize_node(c, doc.data(), doc.size(), true)); doc[1] = 0; doc[2] = 0; doc.erase(doc.begin() + 3, doc.begin() + 4);
    { std::string f = footer(h, doc); CHECK(f.find("X open") == 0 && f.find("O close") != std::string::npos && f.find("R1 Commands") != std::string::npos); }
    h.press(Key::Cross); CHECK(h.take_doc()); h.ed.on_nbt("get", "p", true, "ok", doc.data(), doc.size());
    CHECK(h.select("Invulnerable")); { std::string f = footer(h, doc); CHECK(f.find("X toggle") == 0 && f.find("O close") != std::string::npos); }
    CHECK(h.select("Sub")); { std::string f = footer(h, doc); CHECK(f.find("X open") == 0); }
    CHECK(h.select("Note")); { std::string f = footer(h, doc); CHECK(f.find("X type") == 0); }
    CHECK(h.select("foodLevel")); { std::string f = footer(h, doc); CHECK(f.find("X change") == 0); }
    // inside a compound Circle goes up one level
    CHECK(h.select("Sub")); h.press(Key::Cross); { std::string f = footer(h, doc); CHECK(f.find("O up") != std::string::npos); }
    h.press(Key::Circle); CHECK(h.ed.screen() == Editor::Screen::Tree && h.ed.work().name_eq(h.ed.selected_node(), "Sub"));
    // number editor: footer, row order (Value, Keep, Step), Square keeps the value and applies it at once
    CHECK(h.select("foodLevel")); h.press(Key::Cross); CHECK(h.ed.screen() == Editor::Screen::Number);
    { View v; h.ed.view(&v); CHECK(!strcmp(v.rows[1].label, "Value") && !strcmp(v.rows[2].label, "Keep this value (X)") && !strcmp(v.rows[3].label, "Step size (L1/R1)"));
      CHECK(strstr(v.status, "Sq keep+apply") && strstr(v.status, "O cancel")); h.check_view(); }
    h.press(Key::Circle); CHECK(h.ed.screen() == Editor::Screen::Tree && h.ed.edits() == 0);   // cancel: one level back, nothing changed
    h.press(Key::Cross); h.press(Key::Left, 2); h.press(Key::Square);
    CHECK(h.ed.screen() == Editor::Screen::Tree && h.take() && h.last_lines.rfind("/lce-nbt tx p\n", 0) == 0 && h.last_lines.find("foodLevel") != std::string::npos);
    { Bytes b = doc; h.ed.on_nbt("commit", "p", true, "1 edit applied", b.data(), b.size()); }
    // first local edit says it is not applied yet; the footer counts it and names Square
    CHECK(h.select("Invulnerable")); h.press(Key::Cross); CHECK(h.ed.edits() == 1 && strstr(h.ed.status(), "Square applies"));
    { h.now += 5000000; h.ed.tick(h.now); View v; h.ed.view(&v); CHECK(strstr(v.status, "Sq apply 1") && strstr(v.status, "L1 undo") && strstr(v.status, "O close (asks)")); }
    // actions: titled with the row, pending changes first
    h.press(Key::R1); CHECK(h.ed.screen() == Editor::Screen::Actions);
    { View v; h.ed.view(&v); CHECK(!strcmp(v.rows[0].label, "Actions: Invulnerable") && !strcmp(v.rows[1].label, "Apply 1 change to game (Sq)") && strstr(v.rows[2].label, "Undo")); h.check_view(); }
    h.press(Key::Circle); CHECK(h.ed.screen() == Editor::Screen::Tree);
    // leaving with changes: title says they are not applied; Circle keeps editing
    h.press(Key::Circle); CHECK(h.ed.screen() == Editor::Screen::Confirm);
    { View v; h.ed.view(&v); CHECK(!strcmp(v.rows[0].label, "Changes not applied yet") && !strcmp(v.rows[2].label, "Discard changes and close") && strstr(v.status, "O keep editing")); }
    h.press(Key::Circle); CHECK(h.ed.screen() == Editor::Screen::Tree && h.ed.edits() == 1);
    // long text: Circle on a part's keyboard returns to the part list, a second Circle to the tree
    {
        Harness g; g.press(Key::Cross); CHECK(g.take_doc());
        std::string big(250, 'q'); open_doc(g, ("{Page:\"" + big + "\"}").c_str(), "p");
        CHECK(g.select("Page")); g.press(Key::Cross); g.press(Key::Down); g.press(Key::Cross); CHECK(g.ed.screen() == Editor::Screen::Text);
        g.press(Key::Circle); CHECK(g.ed.screen() == Editor::Screen::Choice);
        g.ed.tick(g.now + 5000000);
        { View v; g.ed.view(&v); if (getenv("NBT_DEBUG")) std::printf("[%s] [%s]\n", v.rows[v.selected].label, v.status); CHECK(!strcmp(v.rows[v.selected].label, "Part 2 (101-200)") && strstr(v.status, "O back")); }
        g.press(Key::Circle); CHECK(g.ed.screen() == Editor::Screen::Tree && g.ed.edits() == 0);
    }
}
int main()
{
    test_codec();
    test_edits();
    test_snbt_schema();
    test_diff_fuzz();
    test_ui();
    test_held_pin();
    test_scroll_model();
    test_subtabs();
    test_parts();
    test_applier();
    test_local_backend();
    test_digits_and_longs();
    test_search_and_values();
    test_long_text();
    test_controller_flow();
    test_world_targets();
    test_entity_and_protection();
    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("nbt editor: ok\n");
    return 0;
}
