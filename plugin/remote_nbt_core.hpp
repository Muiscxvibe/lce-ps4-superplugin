#pragma once
// Server debug bridge, plugin side, hardware-independent core (host tested by
// tools-style test remote_nbt_core_test.cpp). No heap, no exceptions.
//
// Wire (server neo-ps4-mob-candidate, LceDebugEngine.h):
//   request  : chat line "/lce-nbt get|set|add|del|list ..."  (sent with mc195_remote_send_text)
//   reply    : clientbound packet 250, channel "MC|LceNbt" or "MC|LceNear", delivered as ONE frame
//              [250][u16 n][n UTF-16BE chars][u16 len][len bytes]
//   MC|LceNbt: UTF(op) UTF(target) u8 ok UTF(message) [named-root NBT when ok]   (UTF = u16 len + modified UTF-8)
// The plugin consumes exactly-matching frames (never forwarded to the retail client).
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

namespace lce_remote {

enum { kMaxPayload = 32768 };
enum Kind : uint8_t { kNone = 0, kNbt = 1, kNear = 2 };

inline unsigned be16(const uint8_t *p) { return (unsigned)p[0] << 8 | p[1]; }

// 1 = MC|LceNbt, 2 = MC|LceNear, 0 = not one of ours. The frame must be exactly one packet.
inline int match_frame(const uint8_t *f, size_t n, const uint8_t **payload, size_t *psize)
{
    static const char kNbtName[] = "MC|LceNbt", kNearName[] = "MC|LceNear";
    if (!f || n < 5 || f[0] != 250) return 0;
    const size_t chars = be16(f + 1);
    if (chars != sizeof kNbtName - 1 && chars != sizeof kNearName - 1) return 0;
    if (n < 3 + chars * 2 + 2) return 0;
    const char *name = chars == sizeof kNbtName - 1 ? kNbtName : kNearName;
    for (size_t i = 0; i < chars; ++i)
        if (f[3 + 2 * i] != 0 || f[4 + 2 * i] != (uint8_t)name[i]) return 0;
    const size_t at = 3 + chars * 2;
    const size_t len = be16(f + at);
    if (n != at + 2 + len || len > kMaxPayload) return 0;
    *payload = f + at + 2; *psize = len;
    return chars == sizeof kNbtName - 1 ? kNbt : kNear;
}

struct Reply {
    char op[8], target[40], message[96];
    bool ok;
    const uint8_t *nbt;
    size_t nbt_size;
};

// Reads a Java-style UTF string (u16 byte length). Copies at most cap-1 bytes.
inline bool read_utf(const uint8_t *p, size_t n, size_t *pos, char *out, size_t cap)
{
    if (*pos + 2 > n) return false;
    const size_t len = be16(p + *pos); *pos += 2;
    if (*pos + len > n) return false;
    size_t c = len < cap - 1 ? len : cap - 1;
    memcpy(out, p + *pos, c); out[c] = 0;
    *pos += len;
    return true;
}

inline bool parse_reply(const uint8_t *p, size_t n, Reply *r)
{
    memset(r, 0, sizeof *r);
    size_t pos = 0;
    if (!read_utf(p, n, &pos, r->op, sizeof r->op) || !read_utf(p, n, &pos, r->target, sizeof r->target)) return false;
    if (pos >= n) return false;
    r->ok = p[pos++] == 1;
    if (!read_utf(p, n, &pos, r->message, sizeof r->message)) return false;
    if (r->ok) { r->nbt = p + pos; r->nbt_size = n - pos; if (!r->nbt_size) return false; }
    return true;
}

// ---- single-slot mailbox (worker thread posts, owner thread takes) -----------------------
struct Mailbox {
    unsigned lock;
    uint32_t seq;
    uint8_t kind;
    uint32_t size;
    uint8_t data[kMaxPayload + 16];
};
inline void lock_(Mailbox &m) { while (__atomic_exchange_n(&m.lock, 1u, __ATOMIC_ACQUIRE)) {} }
inline void unlock_(Mailbox &m) { __atomic_store_n(&m.lock, 0u, __ATOMIC_RELEASE); }
inline void post(Mailbox &m, uint8_t kind, const uint8_t *p, size_t n)
{
    if (n > kMaxPayload) return;
    lock_(m); memcpy(m.data, p, n); m.size = (uint32_t)n; m.kind = kind; ++m.seq; unlock_(m);
}
// Copies the newest message if its sequence differs from *last.
inline bool take(Mailbox &m, uint32_t *last, uint8_t *kind, uint8_t *out, size_t cap, size_t *size)
{
    bool got = false;
    lock_(m);
    if (m.seq != *last && m.size <= cap) { memcpy(out, m.data, m.size); *size = m.size; *kind = m.kind; *last = m.seq; got = true; }
    else if (m.seq != *last) *last = m.seq; // too big for the caller: drop
    unlock_(m);
    return got;
}
inline void discard_pending(Mailbox &m, uint32_t *last) { lock_(m); *last = m.seq; unlock_(m); }

// ---- NBT reader for items ---------------------------------------------------------------------
struct Cursor { const uint8_t *b; size_t n, pos; bool ok; };
inline uint32_t rd(Cursor &c, unsigned bytes)
{
    if (!c.ok || c.pos + bytes > c.n) { c.ok = false; return 0; }
    uint32_t v = 0;
    for (unsigned i = 0; i < bytes; ++i) v = v << 8 | c.b[c.pos + i];
    c.pos += bytes; return v;
}
inline void skip(Cursor &c, size_t bytes)
{
    if (!c.ok || bytes > c.n - c.pos) { c.ok = false; return; }
    c.pos += bytes;
}
inline void skip_payload(Cursor &c, unsigned type, unsigned depth)
{
    if (depth > 16) { c.ok = false; return; }
    switch (type) {
    case 1: skip(c, 1); break;
    case 2: skip(c, 2); break;
    case 3: case 5: skip(c, 4); break;
    case 4: case 6: skip(c, 8); break;
    case 7: { uint32_t l = rd(c, 4); skip(c, l); break; }
    case 8: { uint32_t l = rd(c, 2); skip(c, l); break; }
    case 9: {
        unsigned t = rd(c, 1); uint32_t cnt = rd(c, 4);
        if (cnt > 65536) { c.ok = false; break; }
        for (uint32_t i = 0; c.ok && i < cnt; ++i) skip_payload(c, t, depth + 1);
        break;
    }
    case 10:
        while (c.ok) {
            unsigned t = rd(c, 1);
            if (!c.ok || t == 0) break;
            uint32_t l = rd(c, 2); skip(c, l);
            skip_payload(c, t, depth + 1);
        }
        break;
    case 11: { uint32_t l = rd(c, 4); if (l > 65536) { c.ok = false; break; } skip(c, (size_t)l * 4); break; }
    default: c.ok = false;
    }
}
struct Ench { int16_t id, level; };
struct Item {
    bool has_id, has_count, has_damage, ench_valid;
    int id, count, damage;
    unsigned ench_n;
    Ench ench[64];
};
// Reads a tag name into buf (truncated, NUL terminated).
inline void read_name(Cursor &c, char *buf, size_t cap)
{
    uint32_t l = rd(c, 2);
    if (!c.ok || c.pos + l > c.n) { c.ok = false; buf[0] = 0; return; }
    size_t k = l < cap - 1 ? l : cap - 1;
    memcpy(buf, c.b + c.pos, k); buf[k] = 0;
    c.pos += l;
}
inline void read_ench_list(Cursor &c, Item *out)
{
    unsigned et = rd(c, 1); uint32_t cnt = rd(c, 4);
    if (!c.ok) return;
    if (cnt == 0) { out->ench_valid = true; return; }
    if (et != 10 || cnt > 64) { c.ok = false; return; }
    for (uint32_t i = 0; c.ok && i < cnt; ++i) {
        int id = -1, lvl = -1;
        for (;;) {
            unsigned t = rd(c, 1);
            if (!c.ok || t == 0) break;
            char nm[8]; read_name(c, nm, sizeof nm);
            if (t == 2 && !strcmp(nm, "id")) id = (int16_t)rd(c, 2);
            else if (t == 2 && !strcmp(nm, "lvl")) lvl = (int16_t)rd(c, 2);
            else skip_payload(c, t, 3);
        }
        if (id < 0 || lvl < 1) { c.ok = false; return; }
        out->ench[out->ench_n].id = (int16_t)id; out->ench[out->ench_n].level = (int16_t)lvl; ++out->ench_n;
    }
    out->ench_valid = c.ok;
}
inline void read_item_tag(Cursor &c, Item *out)
{
    for (;;) {
        unsigned t = rd(c, 1);
        if (!c.ok || t == 0) break;
        char nm[8]; read_name(c, nm, sizeof nm);
        if (t == 9 && !strcmp(nm, "ench")) read_ench_list(c, out);
        else skip_payload(c, t, 2);
    }
}
// Parses {id:short, Count:byte, Damage:short, tag:{ench:[{id,lvl}]}} from a named-root compound.
inline bool read_item(const uint8_t *b, size_t n, Item *out)
{
    memset(out, 0, sizeof *out);
    Cursor c{b, n, 0, true};
    if (rd(c, 1) != 10) return false;
    uint32_t nl = rd(c, 2); skip(c, nl);
    for (;;) {
        unsigned t = rd(c, 1);
        if (!c.ok || t == 0) break;
        char nm[8]; read_name(c, nm, sizeof nm);
        if (t == 2 && !strcmp(nm, "id")) { out->id = (int16_t)rd(c, 2); out->has_id = true; }
        else if (t == 1 && !strcmp(nm, "Count")) { out->count = (int8_t)rd(c, 1); out->has_count = true; }
        else if (t == 2 && !strcmp(nm, "Damage")) { out->damage = (int16_t)rd(c, 2); out->has_damage = true; }
        else if (t == 10 && !strcmp(nm, "tag")) read_item_tag(c, out);
        else skip_payload(c, t, 1);
    }
    if (!c.ok) return false;
    if (!out->ench_valid) out->ench_valid = true; // no ench tag = empty list
    return out->has_id && out->has_count && out->has_damage;
}

// ---- debug-action job state machine (count / damage on a hotbar slot) -------------------------
// Replaces the native observe/apply/undo transactions: each step talks to the server through
// "/lce-nbt get|set i<slot>" and the result is the server's own read-back.
struct CountOut {
    uint64_t observation;
    int32_t slot, count, damage;
    bool damage_valid, undo_available, ench_valid;
    unsigned ench_n;
    Ench ench[64];
};
struct Undo { bool count_valid, damage_valid; int slot_c, slot_d, count, damage; };
struct Ctx { uint64_t next_observation; Undo undo; };
enum class Step { Send, Wait, Ok, Reject };
struct Job {
    unsigned action;
    int slot, expected, desired;
    int stage, before;
};
inline bool job_supported(unsigned a) { return a == 30 || a == 31 || a == 32 || a == 87 || a == 88; }
inline void job_begin(Job *j, unsigned action, int slot, int expected, int desired)
{
    memset(j, 0, sizeof *j); j->action = action; j->slot = slot; j->expected = expected; j->desired = desired;
}
inline void fill_out(Ctx &ctx, const Item &it, int slot, CountOut *o)
{
    memset(o, 0, sizeof *o);
    o->observation = ctx.next_observation++; o->slot = slot; o->count = it.count; o->damage = it.damage; o->damage_valid = true;
    o->ench_valid = it.ench_valid; o->ench_n = it.ench_n;
    for (unsigned i = 0; i < it.ench_n; ++i) o->ench[i] = it.ench[i];
}
// Advance the job. `reply` is the newest MC|LceNbt reply after the last Send, or null.
// On Send, `cmd` holds the chat line to send. On Ok, `*out` is filled.
inline Step job_step(Job &j, Ctx &ctx, const Reply *reply, char *cmd, size_t cap, CountOut *out)
{
    const bool damage = j.action == 87 || j.action == 88;
    const char *field = damage ? "Damage" : "Count", *type = damage ? "s" : "b";
    if (j.slot < 0 || j.slot > 8) return Step::Reject;
    if (j.stage == 0) {
        if (j.action == 32 || j.action == 88) {
            const bool have = damage ? ctx.undo.damage_valid && ctx.undo.slot_d == j.slot : ctx.undo.count_valid && ctx.undo.slot_c == j.slot;
            if (!have) return Step::Reject;
            snprintf(cmd, cap, "/lce-nbt set i%d %s %s %d", j.slot, field, type, damage ? ctx.undo.damage : ctx.undo.count);
            j.stage = 3; return Step::Send;
        }
        if (j.action == 31 && (j.desired < 1 || j.desired > 127)) return Step::Reject;
        if (j.action == 87 && (j.desired < 0 || j.desired > 32767)) return Step::Reject;
        snprintf(cmd, cap, "/lce-nbt get i%d", j.slot);
        j.stage = 1; return Step::Send;
    }
    if (!reply) return Step::Wait;
    if (!reply->ok) return Step::Reject;
    Item it;
    if (!read_item(reply->nbt, reply->nbt_size, &it)) return Step::Reject;
    if (j.stage == 1) {
        if (j.action == 30) { fill_out(ctx, it, j.slot, out); out->undo_available = false; return Step::Ok; }
        const int cur = damage ? it.damage : it.count;
        if (j.action == 31 && j.expected >= 1 && cur != j.expected) return Step::Reject; // stale observation
        j.before = cur;
        snprintf(cmd, cap, "/lce-nbt set i%d %s %s %d", j.slot, field, type, j.desired);
        j.stage = 2; return Step::Send;
    }
    if (j.stage == 2) {
        const int now = damage ? it.damage : it.count;
        if (now != j.desired) return Step::Reject;
        if (damage) { ctx.undo.damage_valid = true; ctx.undo.slot_d = j.slot; ctx.undo.damage = j.before; }
        else { ctx.undo.count_valid = true; ctx.undo.slot_c = j.slot; ctx.undo.count = j.before; }
        fill_out(ctx, it, j.slot, out); out->undo_available = true; return Step::Ok;
    }
    // stage 3: undo applied
    const int now = damage ? it.damage : it.count;
    const int want = damage ? ctx.undo.damage : ctx.undo.count;
    if (now != want) return Step::Reject;
    if (damage) ctx.undo.damage_valid = false; else ctx.undo.count_valid = false;
    fill_out(ctx, it, j.slot, out); out->undo_available = false; return Step::Ok;
}

}  // namespace lce_remote
