// Host test for remote_nbt_core.hpp: g++ -std=c++17 -Wall -Wextra -Werror remote_nbt_core_test.cpp && ./a.out
#include "remote_nbt_core.hpp"
#include <vector>
#include <string>
#include <cstdio>
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace lce_remote;
typedef std::vector<uint8_t> Bytes;
static void u16(Bytes &b, unsigned v) { b.push_back(v >> 8); b.push_back(v & 255); }
static void utf(Bytes &b, const std::string &s) { u16(b, s.size()); b.insert(b.end(), s.begin(), s.end()); }
static void name(Bytes &b, const char *n) { utf(b, n); }
static Bytes frame(const std::string &chan, const Bytes &payload)
{
    Bytes f; f.push_back(250); u16(f, chan.size());
    for (char c : chan) { f.push_back(0); f.push_back(c); }
    u16(f, payload.size()); f.insert(f.end(), payload.begin(), payload.end()); return f;
}
static Bytes item_nbt(int id, int count, int damage, std::vector<std::pair<int,int>> ench)
{
    Bytes b; b.push_back(10); u16(b, 0);
    b.push_back(2); name(b, "id"); u16(b, id);
    b.push_back(1); name(b, "Count"); b.push_back(count);
    b.push_back(2); name(b, "Damage"); u16(b, damage);
    b.push_back(8); name(b, "junk"); utf(b, "hello");
    if (!ench.empty()) {
        b.push_back(10); name(b, "tag");
        b.push_back(3); name(b, "RepairCost"); b.insert(b.end(), {0,0,0,5});
        b.push_back(9); name(b, "ench"); b.push_back(10); b.insert(b.end(), {0,0,0,(uint8_t)ench.size()});
        for (auto &e : ench) { b.push_back(2); name(b, "id"); u16(b, e.first); b.push_back(2); name(b, "lvl"); u16(b, e.second); b.push_back(0); }
        b.push_back(0);
    }
    b.push_back(0); return b;
}
static Bytes reply_payload(const std::string &op, const std::string &target, bool ok, const std::string &msg, const Bytes &nbt)
{
    Bytes p; utf(p, op); utf(p, target); p.push_back(ok ? 1 : 0); utf(p, msg); if (ok) p.insert(p.end(), nbt.begin(), nbt.end()); return p;
}
int main()
{
    int failures = 0;
    // frame matching
    Bytes nbt = item_nbt(276, 1, 12, {{16, 3}, {34, 2}});
    Bytes pl = reply_payload("get", "i0", true, "ok", nbt);
    Bytes f = frame("MC|LceNbt", pl);
    const uint8_t *p; size_t n;
    CHECK(match_frame(f.data(), f.size(), &p, &n) == kNbt && n == pl.size());
    Bytes fn = frame("MC|LceNear", Bytes{1, 2, 3});
    CHECK(match_frame(fn.data(), fn.size(), &p, &n) == kNear && n == 3);
    Bytes other = frame("MC|Other1", pl); CHECK(match_frame(other.data(), other.size(), &p, &n) == 0);
    Bytes cipher = frame("MC|CKey", Bytes(32, 7)); CHECK(match_frame(cipher.data(), cipher.size(), &p, &n) == 0);
    Bytes trailing = f; trailing.push_back(0); CHECK(match_frame(trailing.data(), trailing.size(), &p, &n) == 0); // more than one packet
    Bytes shortf(f.begin(), f.end() - 1); CHECK(match_frame(shortf.data(), shortf.size(), &p, &n) == 0);
    Bytes wrongid = f; wrongid[0] = 249; CHECK(match_frame(wrongid.data(), wrongid.size(), &p, &n) == 0);
    Bytes badchar = f; badchar[3] = 1; CHECK(match_frame(badchar.data(), badchar.size(), &p, &n) == 0);
    CHECK(match_frame(nullptr, 10, &p, &n) == 0); CHECK(match_frame(f.data(), 3, &p, &n) == 0);
    // reply parse
    Reply r;
    CHECK(match_frame(f.data(), f.size(), &p, &n) && parse_reply(p, n, &r) && r.ok && !strcmp(r.op, "get") && !strcmp(r.target, "i0") && r.nbt_size == nbt.size());
    Bytes err = reply_payload("err", "", false, "slot is empty", {});
    CHECK(parse_reply(err.data(), err.size(), &r) && !r.ok && !strcmp(r.message, "slot is empty"));
    Bytes trunc(pl.begin(), pl.begin() + 5); CHECK(!parse_reply(trunc.data(), trunc.size(), &r));
    // item reader
    Item it;
    CHECK(read_item(nbt.data(), nbt.size(), &it) && it.id == 276 && it.count == 1 && it.damage == 12 && it.ench_n == 2 && it.ench[1].id == 34 && it.ench[1].level == 2 && it.ench_valid);
    Bytes plain = item_nbt(4, 64, 0, {}); CHECK(read_item(plain.data(), plain.size(), &it) && it.count == 64 && it.ench_n == 0 && it.ench_valid);
    Bytes cut(nbt.begin(), nbt.end() - 3); CHECK(!read_item(cut.data(), cut.size(), &it));
    Bytes notcompound = nbt; notcompound[0] = 9; CHECK(!read_item(notcompound.data(), notcompound.size(), &it));
    // mailbox
    static Mailbox mb; uint32_t last = 0; uint8_t kind; static uint8_t buf[kMaxPayload + 16]; size_t sz;
    CHECK(!take(mb, &last, &kind, buf, sizeof buf, &sz));
    post(mb, kNbt, pl.data(), pl.size()); CHECK(take(mb, &last, &kind, buf, sizeof buf, &sz) && kind == kNbt && sz == pl.size() && !memcmp(buf, pl.data(), sz));
    CHECK(!take(mb, &last, &kind, buf, sizeof buf, &sz));
    post(mb, kNbt, pl.data(), 3); post(mb, kNear, pl.data(), 4); CHECK(take(mb, &last, &kind, buf, sizeof buf, &sz) && kind == kNear && sz == 4);
    post(mb, kNbt, pl.data(), 5); discard_pending(mb, &last); CHECK(!take(mb, &last, &kind, buf, sizeof buf, &sz));
    // job machine: set count with undo
    Ctx ctx{}; ctx.next_observation = 100; Job j; CountOut o; char cmd[96];
    auto ok_reply = [&](int count, int dmg) { static Bytes keep; keep = item_nbt(276, count, dmg, {}); static Bytes pp; pp = reply_payload("get", "i3", true, "ok", keep); static Reply rr; parse_reply(pp.data(), pp.size(), &rr); return &rr; };
    job_begin(&j, 30, 3, 0, 0);
    CHECK(job_step(j, ctx, nullptr, cmd, sizeof cmd, &o) == Step::Send && !strcmp(cmd, "/lce-nbt get i3"));
    CHECK(job_step(j, ctx, nullptr, cmd, sizeof cmd, &o) == Step::Wait);
    CHECK(job_step(j, ctx, ok_reply(10, 7), cmd, sizeof cmd, &o) == Step::Ok && o.count == 10 && o.damage == 7 && o.observation == 100 && !o.undo_available && o.ench_valid);
    job_begin(&j, 31, 3, 10, 64);
    CHECK(job_step(j, ctx, nullptr, cmd, sizeof cmd, &o) == Step::Send);
    CHECK(job_step(j, ctx, ok_reply(10, 7), cmd, sizeof cmd, &o) == Step::Send && !strcmp(cmd, "/lce-nbt set i3 Count b 64"));
    CHECK(job_step(j, ctx, ok_reply(64, 7), cmd, sizeof cmd, &o) == Step::Ok && o.count == 64 && o.undo_available && o.observation == 101);
    job_begin(&j, 32, 3, 0, 0);
    CHECK(job_step(j, ctx, nullptr, cmd, sizeof cmd, &o) == Step::Send && !strcmp(cmd, "/lce-nbt set i3 Count b 10"));
    CHECK(job_step(j, ctx, ok_reply(10, 7), cmd, sizeof cmd, &o) == Step::Ok && o.count == 10 && !o.undo_available);
    job_begin(&j, 32, 3, 0, 0); CHECK(job_step(j, ctx, nullptr, cmd, sizeof cmd, &o) == Step::Reject); // nothing to undo
    job_begin(&j, 31, 3, 11, 64); job_step(j, ctx, nullptr, cmd, sizeof cmd, &o);
    CHECK(job_step(j, ctx, ok_reply(10, 7), cmd, sizeof cmd, &o) == Step::Reject); // stale expected
    job_begin(&j, 31, 3, 0, 128); CHECK(job_step(j, ctx, nullptr, cmd, sizeof cmd, &o) == Step::Reject);
    job_begin(&j, 31, 9, 0, 5); CHECK(job_step(j, ctx, nullptr, cmd, sizeof cmd, &o) == Step::Reject);
    job_begin(&j, 87, 3, 0, 500); job_step(j, ctx, nullptr, cmd, sizeof cmd, &o);
    CHECK(job_step(j, ctx, ok_reply(1, 7), cmd, sizeof cmd, &o) == Step::Send && !strcmp(cmd, "/lce-nbt set i3 Damage s 500"));
    CHECK(job_step(j, ctx, ok_reply(1, 400), cmd, sizeof cmd, &o) == Step::Reject); // server did not apply it
    job_begin(&j, 87, 3, 0, 500); job_step(j, ctx, nullptr, cmd, sizeof cmd, &o); job_step(j, ctx, ok_reply(1, 7), cmd, sizeof cmd, &o);
    CHECK(job_step(j, ctx, ok_reply(1, 500), cmd, sizeof cmd, &o) == Step::Ok && o.damage == 500 && o.undo_available);
    job_begin(&j, 88, 3, 0, 0); CHECK(job_step(j, ctx, nullptr, cmd, sizeof cmd, &o) == Step::Send && !strcmp(cmd, "/lce-nbt set i3 Damage s 7"));
    Bytes errp = reply_payload("err", "", false, "slot is empty", {}); Reply er; parse_reply(errp.data(), errp.size(), &er);
    job_begin(&j, 30, 3, 0, 0); job_step(j, ctx, nullptr, cmd, sizeof cmd, &o); CHECK(job_step(j, ctx, &er, cmd, sizeof cmd, &o) == Step::Reject);
    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    puts("ok"); return 0;
}
