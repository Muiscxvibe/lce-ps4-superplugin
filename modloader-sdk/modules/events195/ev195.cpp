// events195 core: scope subscriber, presence, world lifecycle, scheduler,
// item/block/entity thunks, keybind filter, command queue. See ev195.h.
#include "ev195.h"
#include "natives195.h"
#include "services195.h"
#include "util195.h"
#include <stdio.h>
#include <string.h>

namespace {
using lce195u::copy;

constexpr uint32_t kPlayers = 8, kRouted = 64, kCopies = 16, kCopySlots = 640, kLines = 8, kKeyRing = 64;
constexpr uint64_t kLeaveTicks = 40;      // 2 s of server ticks without a connection tick
constexpr uint32_t kReanchor = 2 * kPlayers + 2;

struct Routed { uintptr_t object; uint32_t id; uintptr_t original; uintptr_t extra; };
struct Player { uint8_t used, view_set; uintptr_t object; uint64_t seen; char uuid[48]; char name[64]; lce195_view view; };
struct Task { uint8_t used; uint64_t due; lce_event_fn fn; void *user; };
struct Key {
    lce_keybind_desc d; uint32_t mask; uint8_t hold, down, fired; uint64_t since;
};
struct Cmd { char modid[LCE_MAX_MODID]; char name[32]; lce_command_desc d; };
struct VtCopy { uintptr_t original; uint32_t slots; uintptr_t hurt, die, tick, interact; uintptr_t table[2 + kCopySlots]; };
struct Line { char uuid[48]; char text[LCE195_CMD_LINE]; };

struct State {
    uint32_t ready, failed;
    lce195_host host;
    lce195_sink sink; void *sink_user;
    lce195_frame frame;
    uint64_t server_thread, session, ticks, anchor_seen_tick;
    char anchor[48]; uint32_t anchor_miss; uint8_t world_live;
    lce195_view anchor_view; uint8_t anchor_view_set;   // last anchor view: frame for entity events (level services)
    Player players[kPlayers];
    Task tasks[LCE195_MAX_SCHEDULED];
    Routed items[kRouted], blocks[kRouted], block_items[kRouted], block_uses[kRouted];
    Routed block_ticks[kRouted], neighbors[kRouted], step_ons[kRouted], insides[kRouted], inv_ticks[kRouted];   // API 1.3
    Key keys[LCE195_MAX_KEYBINDS]; uint32_t key_count;
    uint32_t key_ring[kKeyRing]; uint32_t key_head, key_tail;   // SPSC pad -> server
    Cmd cmds[LCE195_MAX_COMMANDS]; uint32_t cmd_count;
    Line lines[kLines]; uint32_t line_lock, line_count;
    VtCopy copies[kCopies]; uint32_t copy_count;
    char channels[16][24]; uint32_t channel_count;                       // API 1.4 mod channels
    uintptr_t lis_original, lis_stock; uint32_t lis_slots; uintptr_t lis_table[2 + 160];
    // API 1.6 client networking
    uintptr_t pk_node[2], pk_orig[2], pk_handle[2], pk_stock_vt[2];
    uintptr_t pk_table[2][2 + LCE195_PACKET_VT_SLOTS];
    uint32_t net_client, net_attempts;
    uintptr_t client_listener; uint64_t client_thread;   // set while a routed client packet handler runs
    uint64_t last_tick_ms; uint32_t last_tick_set;
    lce195u::MsgQueue<32, 65536> host_rx, host_rx_snap;  // host: client payloads (kind 0) + loopback sends (kind 1)
    lce195_ev_stats st;
};
State g;

bool rd(uintptr_t a, void *v, size_t n) { return a && a <= UINTPTR_MAX - n && g.host.read && g.host.read(g.host.opaque, a, v, n) == 1; }
uint64_t tid() { return g.host.thread_id ? g.host.thread_id(g.host.opaque) : 0; }
void logf(const char *m) { if (g.host.log) g.host.log(g.host.opaque, m); }

int dispatch(lce_event ev, const void *payload) {
    if (!g.sink || g.failed) return 0;
    ++g.st.dispatched;
    int c = g.sink(g.sink_user, ev, payload);
    if (c) ++g.st.cancelled;
    return c;
}

// Nested-safe frame publication for the duration of one native callback.
struct FrameScope {
    lce195_frame prev; bool set = false;
    FrameScope(const lce195_view *v, bool player) {
        prev = g.frame;
        if (!v) return;
        g.frame.view = *v; g.frame.active = 1; g.frame.has_player = player ? 1u : 0u; set = true;
    }
    ~FrameScope() { if (set) g.frame = prev; }
};

// Host: client payloads the host's own client received (kind 0) and its send_to_server loopback (kind 1), on the server tick.
void drain_host_rx(void *player) {
    if (!__atomic_load_n(&g.host_rx.count, __ATOMIC_ACQUIRE) || !g.host_rx.take(g.host_rx_snap)) return;
    for (uint32_t i = 0; i < g.host_rx_snap.count; ++i) {
        const auto &m = g.host_rx_snap.m[i];
        const char *ch = nullptr;
        for (uint32_t j = 0; j < g.channel_count && !ch; ++j) if (!strcmp(g.channels[j], m.ch)) ch = g.channels[j];
        if (!ch) continue;
        lce_ev_payload ev{};
        ev.struct_size = sizeof ev; ev.channel = ch; ev.data = g.host_rx_snap.buf + m.off; ev.len = m.len;
        if (m.kind == 1) { ev.player = player; ++g.st.loopbacks; dispatch(LCE_EV_CUSTOM_PAYLOAD, &ev); }
        else { ++g.st.client_payloads; dispatch(LCE_EV_CLIENT_PAYLOAD, &ev); }
    }
}

bool server_level(uintptr_t level) {
    uintptr_t vt = 0; uint8_t client = 1;
    return rd(level, &vt, 8) && vt == g.host.image + LCE195_RVA_SERVERLEVEL_VT &&
           rd(level + LCE195_OFF_LEVEL_CLIENT, &client, 1) && !client;
}

// Block position of an entity (Entity +0x170 double x, y, z; floor). 0 if unreadable / out of range.
bool entity_block_pos(uintptr_t e, int32_t out[3]) {
    double p[3];
    if (!rd(e + LCE195_OFF_ENTITY_POS, p, sizeof p)) return false;
    for (int i = 0; i < 3; ++i) {
        if (!(p[i] == p[i]) || p[i] < -30000000.0 || p[i] > 30000000.0) return false;
        double f = (double)(int64_t)p[i];
        out[i] = (int32_t)(f > p[i] ? f - 1 : f);
    }
    return true;
}

// Read a 1.95 wstring (40 bytes) as UTF-8.
bool wstring_utf8(uintptr_t w, char *out, uint32_t cap) {
    uint64_t len = 0, wcap = 0; uintptr_t data = 0;
    if (!cap || !rd(w + 0x18, &len, 8) || !rd(w + 0x20, &wcap, 8) || len > 128 || wcap < len || wcap > 0x100000) return false;
    if (wcap < 8) data = w + 8; else if (!rd(w + 8, &data, 8) || !data) return false;
    uint16_t text[128];
    if (len && !rd(data, text, (size_t)len * 2)) return false;
    lce195u::utf16_to_8(text, (uint32_t)len, out, cap);
    return true;
}

Player *find_player(const char *uuid) {
    for (auto &p : g.players) if (p.used && !strcmp(p.uuid, uuid)) return &p;
    return nullptr;
}

void leave(Player &p, void *object) {
    lce_ev_player ev{sizeof ev, object, p.name};
    ++g.st.leaves; --g.st.players;
    dispatch(LCE_EV_PLAYER_LEAVE, &ev);
    if (!strcmp(p.uuid, g.anchor)) g.anchor[0] = 0;
    p = Player{};
}

void world_unload() {
    if (!g.world_live) return;
    for (auto &p : g.players) if (p.used) leave(p, nullptr);
    g.world_live = 0; g.anchor[0] = 0; g.anchor_miss = 0;
    for (auto &t : g.tasks) t = Task{};       // tasks belong to the world they were scheduled in
    ++g.st.worlds;
    dispatch(LCE_EV_WORLD_UNLOAD, nullptr);
}

void run_tasks() {
    for (auto &t : g.tasks) {
        if (!t.used || t.due > g.ticks) continue;
        Task run = t; t = Task{};
        ++g.st.scheduled_run;
        run.fn(run.user, LCE_EV_SERVER_TICK, nullptr);
    }
}

void drain_keys(void *player) {
    uint32_t tail = __atomic_load_n(&g.key_tail, __ATOMIC_RELAXED);
    uint32_t head = __atomic_load_n(&g.key_head, __ATOMIC_ACQUIRE);
    while (tail != head) {
        uint32_t e = g.key_ring[tail % kKeyRing];
        ++tail;
        uint32_t k = e & 0xff;
        if (k < g.key_count && g.keys[k].d.fn) { ++g.st.keys; g.keys[k].d.fn(g.keys[k].d.user, player, (int)(e >> 8)); }
    }
    __atomic_store_n(&g.key_tail, tail, __ATOMIC_RELEASE);
}

void lock_lines() { while (__atomic_exchange_n(&g.line_lock, 1u, __ATOMIC_ACQUIRE)) {} }
void unlock_lines() { __atomic_store_n(&g.line_lock, 0u, __ATOMIC_RELEASE); }

void reply(void *player, const char *text) { if (text && text[0]) lce195_send_chat(nullptr, player, text); }

void run_command(void *player, bool host, char *line) {
    char *argv[17]; int argc = 0;
    for (char *p = line + 1; *p && argc < 16;) {
        while (*p == ' ') ++p;
        if (!*p) break;
        if (*p == '"') {
            argv[argc++] = ++p;
            while (*p && *p != '"') ++p;
        } else {
            argv[argc++] = p;
            while (*p && *p != ' ') ++p;
        }
        if (*p) *p++ = 0;
    }
    argv[argc] = nullptr;
    if (!argc) return;
    char out[256]; out[0] = 0;
    if (!strcmp(argv[0], "help")) {
        size_t n = (size_t)snprintf(out, sizeof out, "Commands:");
        for (uint32_t i = 0; i < g.cmd_count && n < sizeof out; ++i)
            n += (size_t)snprintf(out + n, sizeof out - n, " /%s", g.cmds[i].name);
        reply(player, out); return;
    }
    const Cmd *c = nullptr;
    const char *colon = strchr(argv[0], ':');
    for (uint32_t i = 0; i < g.cmd_count && !c; ++i) {
        const Cmd &x = g.cmds[i];
        if (colon) { size_t m = (size_t)(colon - argv[0]); if (strlen(x.modid) == m && !strncmp(x.modid, argv[0], m) && !strcmp(x.name, colon + 1)) c = &x; }
        else if (!strcmp(x.name, argv[0])) c = &x;
    }
    if (!c) { snprintf(out, sizeof out, "Unknown command /%.40s (try /help)", argv[0]); reply(player, out); return; }
    if (c->d.op_only && !host) { reply(player, "Only the host can use this command"); return; }
    ++g.st.commands;
    int rc = c->d.fn(c->d.user, player, argc, argv, out, sizeof out);
    out[sizeof out - 1] = 0;
    if (!out[0] && rc) snprintf(out, sizeof out, "Usage: %.200s", c->d.usage ? c->d.usage : argv[0]);
    reply(player, out);
}

// Deliver queued lines addressed to this player (empty uuid = host/anchor).
void drain_lines(const char *uuid, bool host, void *player) {
    for (;;) {
        Line l{}; bool got = false;
        lock_lines();
        for (uint32_t i = 0; i < g.line_count; ++i) {
            if (!(g.lines[i].uuid[0] ? !strcmp(g.lines[i].uuid, uuid) : host)) continue;
            l = g.lines[i];
            memmove(&g.lines[i], &g.lines[i + 1], sizeof(Line) * (g.line_count - i - 1));
            --g.line_count; got = true; break;
        }
        unlock_lines();
        if (!got) return;
        ++g.st.chat;
        lce_ev_chat ev{sizeof ev, player, l.text};
        if (dispatch(LCE_EV_CHAT, &ev)) continue;
        if (l.text[0] == '/') run_command(player, host, l.text);
    }
}

Routed *find_routed(Routed *t, uintptr_t object) {
    for (uint32_t i = 0; i < kRouted; ++i) if (t[i].object == object) return &t[i];
    return nullptr;
}
int route(Routed *t, uintptr_t object, uint32_t id, uintptr_t original, uintptr_t extra, uintptr_t stock) {
#ifdef LCE195_HOST_TEST
    // Host tests cannot execute the image: accept any host code pointer the fake marks as code.
    const bool stock_ok = original == g.host.image + stock || (g.host.is_code && g.host.is_code(g.host.opaque, original) == 1);
#else
    const bool stock_ok = original == g.host.image + stock;
#endif
    if (!g.ready || !object || !stock_ok || find_routed(t, object)) return 0;
    for (uint32_t i = 0; i < kRouted; ++i) if (!t[i].object) { t[i] = Routed{object, id, original, extra}; return 1; }
    return 0;
}

const VtCopy *copy_of(uintptr_t vptr) {
    for (uint32_t i = 0; i < g.copy_count; ++i) if ((uintptr_t)&g.copies[i].table[2] == vptr) return &g.copies[i];
    return nullptr;
}
} // namespace

// ---------------------------------------------------------------------------
extern "C" int lce195_ev_init(const lce195_host *h, lce195_sink sink, void *user) {
    if (g.ready || !h || !h->image || !h->thread_id || !h->read || !h->view_current || !sink) return 0;
    g.host = *h; g.sink = sink; g.sink_user = user; g.ready = 1;
    return 1;
}
extern "C" uint32_t lce195_ev_capabilities(void) {
    if (!g.ready) return 0;
    uint32_t c = LCE_CAP_EV_TICK | LCE_CAP_EV_WORLD | LCE_CAP_EV_PLAYER | LCE_CAP_EV_ITEM_USE | LCE_CAP_EV_BLOCK |
                 LCE_CAP_COMMANDS | LCE_CAP_KEYBINDS;
    if (g.host.write_word && g.host.is_code) c |= LCE_CAP_EV_ENTITY;
    return c;
}
namespace {
bool any_routed(const Routed *t) { for (uint32_t i = 0; i < kRouted; ++i) if (t[i].object) return true; return false; }
}
extern "C" uint32_t lce195_ev_capabilities2(void) {
    if (!g.ready) return 0;
    uint32_t c = LCE_CAP2_EV_BLOCK_USE | LCE_CAP2_EV_CONTEXT;
    if (g.host.write_word && g.host.is_code) c |= LCE_CAP2_EV_ENTITY_TICK | LCE_CAP2_EV_ENTITY_INTERACT;   // mod-spawned entities
    // API 1.3 sources exist only once the composite installed at least one route of that kind.
    if (any_routed(g.block_ticks)) c |= LCE_CAP2_EV_BLOCK_TICK;
    if (any_routed(g.neighbors)) c |= LCE_CAP2_EV_NEIGHBOR;
    if (any_routed(g.step_ons) || any_routed(g.insides)) c |= LCE_CAP2_EV_BLOCK_COLLIDE;
    if (any_routed(g.inv_ticks)) c |= LCE_CAP2_EV_INVENTORY_TICK;
    if (g.lis_original) c |= LCE_CAP2_EV_PAYLOAD;
    if (g.net_client) c |= LCE_CAP2_EV_CLIENT_PAYLOAD;
    return c;
}
extern "C" const lce195_host *lce195_ev_host(void) { return g.ready ? &g.host : nullptr; }
extern "C" int lce195_ev_on_server_thread(void) { uint64_t t = tid(); return g.ready && t && t == __atomic_load_n(&g.server_thread, __ATOMIC_ACQUIRE); }
extern "C" const lce195_frame *lce195_ev_frame(void) {
    if (!g.ready || !g.frame.active) return nullptr;
    uint64_t t = tid();
    return t && t == g.frame.view.thread ? &g.frame : nullptr;
}
extern "C" uint64_t lce195_ev_server_ticks(void) { return g.ticks; }
extern "C" void lce195_ev_get_stats(lce195_ev_stats *out) { if (out) *out = g.st; }

// ---- scope subscriber -----------------------------------------------------
extern "C" void lce195_ev_scope_tick(void *, const lce195_view *v) {
    if (!g.ready || g.failed || !v || !v->player_object || !v->session) return;
    uint64_t t = tid();
    if (!t || t != v->thread || v->image != g.host.image || g.host.view_current(g.host.opaque, v) != 1) { ++g.st.refused; return; }
    __atomic_store_n(&g.server_thread, t, __ATOMIC_RELEASE);
    ++g.st.scope_ticks;
    if (v->session != g.session) { world_unload(); g.session = v->session; }
    FrameScope frame(v, true);
    void *player = (void *)v->player_object;
    if (!g.world_live) { g.world_live = 1; ++g.st.worlds; dispatch(LCE_EV_WORLD_LOAD, nullptr); }

    // Identity: the UUID wstring survives respawn (new ServerPlayer object).
    char uuid[48], name[64];
    uintptr_t vt = 0;
    if (!rd(v->player_object, &vt, 8) || vt != g.host.image + LCE195_RVA_SERVERPLAYER_VT ||
        !wstring_utf8(v->player_object + 0x2d8, uuid, sizeof uuid) || !uuid[0] ||
        !wstring_utf8(v->player_object + LCE195_OFF_PLAYER_NAME, name, sizeof name)) { ++g.st.refused; return; }
    Player *p = find_player(uuid);
    if (!p) {
        for (auto &s : g.players) if (!s.used) { p = &s; break; }
        if (!p) { ++g.st.refused; return; }
        *p = Player{}; p->used = 1; copy(p->uuid, sizeof p->uuid, uuid); copy(p->name, sizeof p->name, name);
        p->object = v->player_object; p->seen = g.ticks;
        ++g.st.joins; ++g.st.players;
        lce_ev_player ev{sizeof ev, player, p->name};
        dispatch(LCE_EV_PLAYER_JOIN, &ev);
        if (g.host.view_current(g.host.opaque, v) != 1) return;
    }
    p->object = v->player_object; p->seen = g.ticks; p->view = *v; p->view_set = 1;   // per-player frame for its own packets
    if (g.channel_count) {
        (void)lce195_ev_route_listener(v->player_object);   // API 1.4 mod channels (idempotent)
        if (!g.net_client) (void)lce195_net_client_install();
    }

    if (!g.anchor[0] || (strcmp(g.anchor, uuid) && ++g.anchor_miss > kReanchor)) { copy(g.anchor, sizeof g.anchor, uuid); g.anchor_miss = 0; }
    const bool host = !strcmp(g.anchor, uuid);
    if (host) {
        g.anchor_miss = 0;
        g.anchor_view = *v; g.anchor_view_set = 1;
        ++g.ticks; ++g.st.server_ticks; p->seen = g.ticks;
        lce_ev_tick ev{sizeof ev, g.ticks};
        if (g.host.now_ms) { __atomic_store_n(&g.last_tick_ms, g.host.now_ms(g.host.opaque), __ATOMIC_RELEASE); __atomic_store_n(&g.last_tick_set, 1u, __ATOMIC_RELEASE); }
        dispatch(LCE_EV_SERVER_TICK, &ev);
        run_tasks();
        drain_keys(player);
        drain_host_rx(player);
        for (auto &o : g.players)
            if (o.used && &o != p && g.ticks - o.seen > kLeaveTicks) leave(o, nullptr);
    }
    drain_lines(uuid, host, player);
}
extern "C" void lce195_ev_scope_finish(void *, uint64_t, uint64_t) { g.frame = lce195_frame{}; g.anchor_view_set = 0; }
extern "C" void lce195_ev_scope_failed(void *, unsigned) { g.failed = 1; ++g.st.faults; logf("events195: scope subscriber failed; events disabled"); }

extern "C" void lce195_ev_player_login(uintptr_t) { /* JOIN is emitted on the player's first retained tick (identity readable) */ }
extern "C" void lce195_ev_player_remove(uintptr_t player) {
    if (!g.ready || !player || !lce195_ev_on_server_thread()) return;
    char uuid[48];
    if (!wstring_utf8(player + 0x2d8, uuid, sizeof uuid)) return;
    Player *p = find_player(uuid);
    if (p && p->object == player) leave(*p, (void *)player);
}
extern "C" void lce195_ev_world_unloading(void) { if (g.ready && lce195_ev_on_server_thread()) world_unload(); }

// ---- scheduler --------------------------------------------------------------
extern "C" int lce195_ev_schedule(uint32_t ticks, lce_event_fn fn, void *user) {
    if (!g.ready || !fn || !ticks || ticks > 20u * 60 * 60) return 0;
    if (g.server_thread && !lce195_ev_on_server_thread()) return 0;
    for (auto &t : g.tasks) if (!t.used) { t = Task{1, g.ticks + ticks, fn, user}; return 1; }
    return 0;
}

// ---- item use ---------------------------------------------------------------
uintptr_t g_item_use_alt;   // API 1.9: one more accepted original +0x40 (projectiles195 p195_item_use), 0 = none
extern "C" void lce195_ev_allow_item_use_original(uintptr_t fn) { __atomic_store_n(&g_item_use_alt, fn, __ATOMIC_RELEASE); }
extern "C" int lce195_ev_route_item(uintptr_t item, uint32_t id, uintptr_t original) {
    const uintptr_t alt = __atomic_load_n(&g_item_use_alt, __ATOMIC_ACQUIRE);
    if (alt && original == alt && g.host.image) return route(g.items, item, id, original, 0, original - g.host.image);   // the thunk then calls it
    if (g.host.image && original == g.host.image + LCE195_RVA_BOATITEM_USE) return route(g.items, item, id, original, 0, LCE195_RVA_BOATITEM_USE);
    return route(g.items, item, id, original, 0, LCE195_RVA_ITEM_USE);
}
extern "C" void *lce195_ev_item_use_thunk(void *out, void *item, void *level, const lce195_pair *player, int hand) {
    Routed *r = g.ready ? find_routed(g.items, (uintptr_t)item) : nullptr;
    uintptr_t original = r ? r->original : g.host.image + LCE195_RVA_ITEM_USE;
    if (r && !g.failed && level && player && player->object && player->control && server_level((uintptr_t)level)) {
        lce195_view v{};
        bool framed = g.host.view_borrow && g.host.view_borrow(g.host.opaque, player, (uintptr_t)level, &v) == 1;
        FrameScope f(framed ? &v : nullptr, framed);
        lce_ev_item_use ev{};
        ev.struct_size = sizeof ev; ev.item_id = r->id; ev.aux = 0; ev.player = (void *)player->object;
        ev.level = level; ev.hand = hand == 1 ? 1u : 0u;
        int32_t at[3];
        if (entity_block_pos(player->object, at)) { ev.x = at[0]; ev.y = at[1]; ev.z = at[2]; }
        ++g.st.item_uses;
        dispatch(LCE_EV_ITEM_USE, &ev);   // not cancellable: stock Item::use (PASS) always runs
    }
    return ((void *(*)(void *, void *, void *, const lce195_pair *, int))original)(out, item, level, player, hand);
}

// ---- block break / place ----------------------------------------------------
extern "C" int lce195_ev_route_block(uintptr_t block, uint32_t id, uintptr_t original) { return route(g.blocks, block, id, original, 0, LCE195_RVA_BLOCK_WILL_DESTROY); }
extern "C" int lce195_ev_block_will_destroy_thunk(uintptr_t block, uintptr_t level, uintptr_t pos, uintptr_t state, const lce195_pair *player) {
    Routed *r = g.ready ? find_routed(g.blocks, block) : nullptr;
    uintptr_t original = r ? r->original : g.host.image + LCE195_RVA_BLOCK_WILL_DESTROY;
    int32_t xyz[3];
    if (r && !g.failed && player && player->object && server_level(level) && rd(pos, xyz, sizeof xyz) && xyz[1] >= 0 && xyz[1] < 256) {
        lce195_view v{};
        bool framed = g.host.view_borrow && g.host.view_borrow(g.host.opaque, player, level, &v) == 1;
        FrameScope f(framed ? &v : nullptr, framed);
        lce_ev_block ev{};
        ev.struct_size = sizeof ev; ev.x = xyz[0]; ev.y = xyz[1]; ev.z = xyz[2]; ev.block_id = r->id; ev.player = (void *)player->object;
        ev.level = (void *)level; ev.face = -1;
        ++g.st.block_breaks;
        dispatch(LCE_EV_BLOCK_BREAK, &ev);   // not cancellable: removal is decided by the caller
    }
    return ((int (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, const lce195_pair *))original)(block, level, pos, state, player);
}
extern "C" int lce195_ev_route_block_use(uintptr_t block, uint32_t id, uintptr_t original) {
    // stock Block::use, or DispenserBlock::use on a storage block (the thunk dispatches first, then opens the container)
    const uint32_t stock = g.ready && original == g.host.image + LCE195_RVA_DISPENSER_USE ? LCE195_RVA_DISPENSER_USE : LCE195_RVA_BLOCK_USE;
    return route(g.block_uses, block, id, original, 0, stock);
}
extern "C" uint64_t lce195_ev_block_use_thunk(uintptr_t block, uintptr_t level, uintptr_t pos, uintptr_t state, const lce195_pair *player,
                                              uintptr_t r9, uintptr_t s0, uintptr_t s1, uintptr_t s2) {
    Routed *r = g.ready ? find_routed(g.block_uses, block) : nullptr;
    uintptr_t original = r ? r->original : g.host.image + LCE195_RVA_BLOCK_USE;
    int32_t xyz[3];
    // stack word 1 is the "simulate" flag (CraftingTableBlock::use 0x1d4ac8 returns false when set): no event then
    if (r && !g.failed && !(s1 & 0xff) && player && player->object && server_level(level) && rd(pos, xyz, sizeof xyz) &&
        xyz[1] >= 0 && xyz[1] < 256) {
        lce195_view v{};
        bool framed = g.host.view_borrow && g.host.view_borrow(g.host.opaque, player, level, &v) == 1;
        FrameScope f(framed ? &v : nullptr, framed);
        lce_ev_block ev{};
        ev.struct_size = sizeof ev; ev.x = xyz[0]; ev.y = xyz[1]; ev.z = xyz[2]; ev.block_id = r->id; ev.player = (void *)player->object;
        ev.level = (void *)level; ev.face = -1;
        ++g.st.block_uses;
        if (dispatch(LCE_EV_BLOCK_USE, &ev)) return 1;   // handled: the click is consumed (no placement / item use)
    }
    return ((uint64_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, const lce195_pair *, uintptr_t, uintptr_t, uintptr_t, uintptr_t))original)(
        block, level, pos, state, player, r9, s0, s1, s2);
}
// Entity events have no player: publish the anchor's view (same level, still current) as a level-only frame so
// level/entity services work from ENTITY_* handlers; player services stay refused (has_player = 0).
struct LevelFrame {
    FrameScope f;
    explicit LevelFrame(uintptr_t level)
        : f(g.anchor_view_set && g.anchor_view.level == level && g.host.view_current(g.host.opaque, &g.anchor_view) == 1 &&
            tid() == g.anchor_view.thread ? &g.anchor_view : nullptr, false) {}
};
extern "C" int lce195_ev_level_call(uintptr_t level, void (*fn)(void *), void *user) {
    if (!fn || !g.ready || g.failed || !level || !server_level(level)) return 0;
    LevelFrame lf(level);
    if (!lf.f.set) return 0;   // not the anchor's level, view stale or another thread: services would refuse anyway
    fn(user);
    return 1;
}
// ---- API 1.3 block / item callbacks ---------------------------------------
namespace {
bool block_xyz(uintptr_t pos, int32_t xyz[3]) { return rd(pos, xyz, 12) && xyz[1] >= 0 && xyz[1] < 256; }
lce_ev_block block_event(const Routed *r, uintptr_t level, const int32_t xyz[3]) {
    lce_ev_block ev{};
    ev.struct_size = sizeof ev; ev.x = xyz[0]; ev.y = xyz[1]; ev.z = xyz[2]; ev.block_id = r->id; ev.level = (void *)level; ev.face = -1;
    return ev;
}
// Frame for an entity-driven callback: the player's own view when the entity is a retained player, else level-only.
const lce195_view *anchor_for(uintptr_t level) {
    return g.anchor_view_set && g.anchor_view.level == level && g.host.view_current(g.host.opaque, &g.anchor_view) == 1 &&
           tid() == g.anchor_view.thread ? &g.anchor_view : nullptr;
}
bool borrow(const lce195_pair *e, uintptr_t level, lce195_view *v) {
    return e && e->object && e->control && g.host.view_borrow && g.host.view_borrow(g.host.opaque, e, level, v) == 1;
}
struct EntityFrame {
    lce195_view v; bool framed; FrameScope f;
    EntityFrame(const lce195_pair *e, uintptr_t level) : v{}, framed(borrow(e, level, &v)), f(framed ? &v : anchor_for(level), framed) {}
};
} // namespace
extern "C" int lce195_ev_route_block_tick(uintptr_t b, uint32_t id, uintptr_t o) { return route(g.block_ticks, b, id, o, 0, LCE195_RVA_BLOCK_TICK); }
extern "C" void lce195_ev_block_tick_thunk(uintptr_t block, uintptr_t level, uintptr_t pos, uintptr_t state, uintptr_t random) {
    Routed *r = g.ready ? find_routed(g.block_ticks, block) : nullptr;
    uintptr_t original = r ? r->original : g.host.image + LCE195_RVA_BLOCK_TICK;
    int32_t xyz[3];
    if (r && !g.failed && server_level(level) && block_xyz(pos, xyz)) {
        LevelFrame lf(level);
        lce_ev_block ev = block_event(r, level, xyz);
        ++g.st.block_ticks;
        dispatch(LCE_EV_BLOCK_TICK, &ev);
    }
    ((void (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t))original)(block, level, pos, state, random);
}
extern "C" int lce195_ev_set_block_random_ticking(uintptr_t block, int on) {
    uintptr_t vt = 0, getter = 0, word = 0;
    if (!g.ready || !block || !g.host.write_word || !find_routed(g.block_ticks, block)) return 0;
    if (!rd(block, &vt, 8) || !rd(vt + LCE195_OFF_BLOCK_IS_TICKING, &getter, 8)) return 0;
#ifdef LCE195_HOST_TEST
    if (getter != g.host.image + LCE195_RVA_BLOCK_IS_TICKING && !(g.host.is_code && g.host.is_code(g.host.opaque, getter) == 1)) return 0;
#else
    if (getter != g.host.image + LCE195_RVA_BLOCK_IS_TICKING) return 0;   // tickBlocks must read the flag we set
#endif
    const uintptr_t at = block + (LCE195_OFF_BLOCK_TICKING & ~7u), shift = (LCE195_OFF_BLOCK_TICKING & 7u) * 8;
    if (!rd(at, &word, 8)) return 0;
    uintptr_t want = (word & ~((uintptr_t)0xff << shift)) | ((uintptr_t)(on ? 1 : 0) << shift);
    return want == word || g.host.write_word(g.host.opaque, at, word, want) == 1 ? 1 : 0;
}
extern "C" int lce195_ev_route_block_neighbor(uintptr_t b, uint32_t id, uintptr_t o) { return route(g.neighbors, b, id, o, 0, LCE195_RVA_BLOCK_NEIGHBOR); }
extern "C" void lce195_ev_block_neighbor_thunk(uintptr_t block, uintptr_t state, uintptr_t level, uintptr_t pos, uintptr_t from_block,
                                               uintptr_t from_pos) {
    Routed *r = g.ready ? find_routed(g.neighbors, block) : nullptr;
    uintptr_t original = r ? r->original : g.host.image + LCE195_RVA_BLOCK_NEIGHBOR;
    int32_t xyz[3], from[3];
    if (r && !g.failed && server_level(level) && block_xyz(pos, xyz)) {
        LevelFrame lf(level);
        lce_ev_block ev = block_event(r, level, xyz);
        if (from_pos && rd(from_pos, from, sizeof from)) {   // Java Direction order: down up north south west east
            const int32_t dx = from[0] - xyz[0], dy = from[1] - xyz[1], dz = from[2] - xyz[2];
            if (!dx && !dz && (dy == -1 || dy == 1)) ev.face = dy < 0 ? 0 : 1;
            else if (!dx && !dy && (dz == -1 || dz == 1)) ev.face = dz < 0 ? 2 : 3;
            else if (!dy && !dz && (dx == -1 || dx == 1)) ev.face = dx < 0 ? 4 : 5;
        }
        ++g.st.neighbors;
        dispatch(LCE_EV_BLOCK_NEIGHBOR, &ev);
    }
    ((void (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t))original)(block, state, level, pos, from_block, from_pos);
}
extern "C" int lce195_ev_route_block_step_on(uintptr_t b, uint32_t id, uintptr_t o) { return route(g.step_ons, b, id, o, 0, LCE195_RVA_BLOCK_STEP_ON); }
extern "C" void lce195_ev_block_step_on_thunk(uintptr_t block, uintptr_t level, uintptr_t pos, const lce195_pair *entity) {
    Routed *r = g.ready ? find_routed(g.step_ons, block) : nullptr;
    uintptr_t original = r ? r->original : g.host.image + LCE195_RVA_BLOCK_STEP_ON;
    int32_t xyz[3];
    if (r && !g.failed && entity && entity->object && server_level(level) && block_xyz(pos, xyz)) {
        EntityFrame ef(entity, level);
        lce_ev_block ev = block_event(r, level, xyz);
        ev.player = (void *)entity->object;
        ++g.st.step_ons;
        dispatch(LCE_EV_BLOCK_STEP_ON, &ev);
    }
    ((void (*)(uintptr_t, uintptr_t, uintptr_t, const lce195_pair *))original)(block, level, pos, entity);
}
extern "C" int lce195_ev_route_block_inside(uintptr_t b, uint32_t id, uintptr_t o) { return route(g.insides, b, id, o, 0, LCE195_RVA_BLOCK_INSIDE); }
extern "C" void lce195_ev_block_inside_thunk(uintptr_t block, uintptr_t state, uintptr_t level, uintptr_t pos, const lce195_pair *entity) {
    Routed *r = g.ready ? find_routed(g.insides, block) : nullptr;
    uintptr_t original = r ? r->original : g.host.image + LCE195_RVA_BLOCK_INSIDE;
    int32_t xyz[3];
    if (r && !g.failed && entity && entity->object && server_level(level) && block_xyz(pos, xyz)) {
        EntityFrame ef(entity, level);
        lce_ev_block ev = block_event(r, level, xyz);
        ev.player = (void *)entity->object;
        ++g.st.insides;
        dispatch(LCE_EV_BLOCK_ENTITY_INSIDE, &ev);
    }
    ((void (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, const lce195_pair *))original)(block, state, level, pos, entity);
}
extern "C" int lce195_ev_route_item_inventory_tick(uintptr_t item, uint32_t id, uintptr_t o) { return route(g.inv_ticks, item, id, o, 0, LCE195_RVA_ITEM_INV_TICK); }
extern "C" void lce195_ev_item_inventory_tick_thunk(uintptr_t item, const lce195_pair *stack, uintptr_t level, const lce195_pair *entity,
                                                    int slot, int selected) {
    Routed *r = g.ready ? find_routed(g.inv_ticks, item) : nullptr;
    uintptr_t original = r ? r->original : g.host.image + LCE195_RVA_ITEM_INV_TICK;
    if (r && !g.failed && entity && entity->object && server_level(level)) {   // MapItem 0x445340: client levels return early too
        EntityFrame ef(entity, level);
        lce_ev_item_use ev{};
        ev.struct_size = sizeof ev; ev.item_id = r->id; ev.aux = slot >= 0 ? (uint32_t)slot : 0; ev.player = (void *)entity->object;
        ev.level = (void *)level; ev.hand = (selected & 0xff) ? 0u : 2u;
        int32_t at[3];
        if (entity_block_pos(entity->object, at)) { ev.x = at[0]; ev.y = at[1]; ev.z = at[2]; }
        ++g.st.inventory_ticks;
        dispatch(LCE_EV_ITEM_INVENTORY_TICK, &ev);
    }
    ((void (*)(uintptr_t, const lce195_pair *, uintptr_t, const lce195_pair *, int, int))original)(item, stack, level, entity, slot, selected);
}
extern "C" int lce195_ev_route_block_item(uintptr_t item, uint32_t id, uintptr_t block, uintptr_t original) {
    return block ? route(g.block_items, item, id, original, block, LCE195_RVA_BLOCKITEM_USEON) : 0;
}
extern "C" int lce195_ev_block_item_use_on_thunk(void *item, const lce195_pair *player, void *level, const int32_t *pos,
                                                 int hand, int face, float cx, float cy, float cz) {
    Routed *r = g.ready ? find_routed(g.block_items, (uintptr_t)item) : nullptr;
    uintptr_t original = r ? r->original : g.host.image + LCE195_RVA_BLOCKITEM_USEON;
    int32_t at[3] = {0, 0, 0};
    bool have = pos && rd((uintptr_t)pos, at, sizeof at);
    int rc = ((int (*)(void *, const lce195_pair *, void *, const int32_t *, int, int, float, float, float))original)(
        item, player, level, pos, hand, face, cx, cy, cz);
    if (!r || g.failed || rc != 0 || !have || !player || !player->object || !server_level((uintptr_t)level)) return rc;
    // Find where the block landed: clicked cell (replaceable) or its neighbour.
    static const int8_t d[6][3] = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};
    uintptr_t lvt = 0, getter = 0;
    if (!rd((uintptr_t)level, &lvt, 8) || !rd(lvt + LCE195_OFF_LEVEL_GETSTATE, &getter, 8) || getter != g.host.image + 0x3f2820u) return rc;
    for (int k = 0; k < 2; ++k) {
        int32_t p[3] = {at[0], at[1], at[2]};
        if (k) { if (face < 0 || face > 5) break; p[0] += d[face][0]; p[1] += d[face][1]; p[2] += d[face][2]; }
        if (p[1] < 0 || p[1] > 255) continue;
        uintptr_t state = ((uintptr_t (*)(void *, const int32_t *))getter)(level, p), svt = 0, gb = 0;
        if (!state || !rd(state, &svt, 8) || !rd(svt + LCE195_OFF_STATE_GETBLOCK, &gb, 8) || !g.host.is_code || g.host.is_code(g.host.opaque, gb) != 1) continue;
        if (((uintptr_t (*)(uintptr_t))gb)(state) != r->extra) continue;
        lce195_view v{};
        bool framed = g.host.view_borrow && g.host.view_borrow(g.host.opaque, player, (uintptr_t)level, &v) == 1;
        FrameScope f(framed ? &v : nullptr, framed);
        lce_ev_block ev{};
        ev.struct_size = sizeof ev; ev.x = p[0]; ev.y = p[1]; ev.z = p[2]; ev.block_id = r->id; ev.player = (void *)player->object;
        ev.level = level; ev.face = face >= 0 && face <= 5 ? face : -1;
        ++g.st.block_places;
        dispatch(LCE_EV_BLOCK_PLACE, &ev);
        break;
    }
    return rc;
}

// ---- entity damage / death ----------------------------------------------------
extern "C" int lce195_ev_track_entity(uintptr_t e) {
    if (!g.ready || g.failed || !e || !g.host.write_word || !g.host.is_code || !lce195_ev_on_server_thread()) return 0;
    uintptr_t vt = 0, level = 0;
    if (!rd(e, &vt, 8) || !vt || vt == g.host.image + LCE195_RVA_SERVERPLAYER_VT || copy_of(vt) ||
        !rd(e + LCE195_OFF_ENTITY_LEVEL, &level, 8) || !server_level(level)) return 0;
    VtCopy *c = nullptr;
    for (uint32_t i = 0; i < g.copy_count; ++i) if (g.copies[i].original == vt) c = &g.copies[i];
    if (!c) {
        if (g.copy_count >= kCopies) return 0;
        VtCopy &n = g.copies[g.copy_count];
        uint32_t slots = 0;
        for (; slots < kCopySlots; ++slots) {
            uintptr_t fn = 0;
            if (!rd(vt + 8 * slots, &fn, 8) || g.host.is_code(g.host.opaque, fn) != 1) break;
        }
        // Must be a LivingEntity-derived primary vtable (die at +0xa00 exists).
        if (slots <= LCE195_OFF_DIE / 8 || slots >= kCopySlots || !rd(vt - 16, n.table, (size_t)(2 + slots) * 8)) return 0;
        n.original = vt; n.slots = slots;
        n.hurt = n.table[2 + LCE195_OFF_HURT / 8]; n.die = n.table[2 + LCE195_OFF_DIE / 8]; n.tick = n.table[2 + LCE195_OFF_TICK / 8];
        n.table[2 + LCE195_OFF_TICK / 8] = (uintptr_t)&lce195_ev_entity_tick_thunk;
        if (slots > LCE195_OFF_INTERACT / 8) {   // Entity::interact (+0xe50) exists in this vtable
            n.interact = n.table[2 + LCE195_OFF_INTERACT / 8];
            n.table[2 + LCE195_OFF_INTERACT / 8] = (uintptr_t)&lce195_ev_entity_interact_thunk;
        }
        n.table[2 + LCE195_OFF_HURT / 8] = (uintptr_t)&lce195_ev_entity_hurt_thunk;
        n.table[2 + LCE195_OFF_DIE / 8] = (uintptr_t)&lce195_ev_entity_die_thunk;
        c = &n; ++g.copy_count;
    }
    if (g.host.write_word(g.host.opaque, e, vt, (uintptr_t)&c->table[2]) != 1) return 0;
    ++g.st.tracked_entities;
    return 1;
}
extern "C" void lce195_ev_entity_tick_thunk(void *entity) {
    uintptr_t vt = 0, level = 0;
    const VtCopy *c = rd((uintptr_t)entity, &vt, 8) ? copy_of(vt) : nullptr;
    if (!c) __builtin_trap();
    if (!g.failed && rd((uintptr_t)entity + LCE195_OFF_ENTITY_LEVEL, &level, 8) && server_level(level)) {
        LevelFrame lf(level);
        lce_ev_entity ev{};
        ev.struct_size = sizeof ev; ev.entity = entity; ev.type_id = -1; ev.level = (void *)level;
        int32_t at[3];
        if (entity_block_pos((uintptr_t)entity, at)) { ev.x = at[0]; ev.y = at[1]; ev.z = at[2]; }
        ++g.st.entity_ticks;
        dispatch(LCE_EV_ENTITY_TICK, &ev);
    }
    ((void (*)(void *))c->tick)(entity);
}
extern "C" uint64_t lce195_ev_entity_interact_thunk(void *entity, const lce195_pair *player, uintptr_t a2, uintptr_t a3, uintptr_t a4,
                                                    uintptr_t a5) {
    uintptr_t vt = 0, level = 0;
    const VtCopy *c = rd((uintptr_t)entity, &vt, 8) ? copy_of(vt) : nullptr;
    if (!c || !c->interact) __builtin_trap();
    if (!g.failed && player && player->object && player->control && rd((uintptr_t)entity + LCE195_OFF_ENTITY_LEVEL, &level, 8) &&
        server_level(level)) {
        lce195_view v{};
        bool framed = g.host.view_borrow && g.host.view_borrow(g.host.opaque, player, level, &v) == 1;
        FrameScope f(framed ? &v : nullptr, framed);
        lce_ev_entity ev{};
        ev.struct_size = sizeof ev; ev.entity = entity; ev.source = (void *)player->object; ev.type_id = -1; ev.level = (void *)level;
        ev.attacker = (void *)player->object;
        int32_t at[3];
        if (entity_block_pos((uintptr_t)entity, at)) { ev.x = at[0]; ev.y = at[1]; ev.z = at[2]; }
        ++g.st.interacts;
        if (dispatch(LCE_EV_ENTITY_INTERACT, &ev)) return 1;   // handled: the entity's own interaction does not run
    }
    return ((uint64_t (*)(void *, const lce195_pair *, uintptr_t, uintptr_t, uintptr_t, uintptr_t))c->interact)(entity, player, a2, a3, a4, a5);
}
extern "C" int lce195_ev_entity_hurt_thunk(void *entity, void *source, float damage) {
    uintptr_t vt = 0, level = 0;
    const VtCopy *c = rd((uintptr_t)entity, &vt, 8) ? copy_of(vt) : nullptr;
    if (!c) __builtin_trap();   // only reachable through our private table
    if (!g.failed && rd((uintptr_t)entity + LCE195_OFF_ENTITY_LEVEL, &level, 8) && server_level(level) &&
        damage == damage && damage >= 0 && damage < 4.0e6f) {
        LevelFrame lf(level);
        lce_ev_entity ev{};
        ev.struct_size = sizeof ev; ev.entity = entity; ev.source = source; ev.type_id = -1; ev.amount_milli = (uint32_t)(damage * 1000.0f);
        ev.level = (void *)level;
        int32_t at[3];
        if (entity_block_pos((uintptr_t)entity, at)) { ev.x = at[0]; ev.y = at[1]; ev.z = at[2]; }
        ++g.st.hurts;
        if (dispatch(LCE_EV_ENTITY_DAMAGE, &ev)) return 0;   // cancelled: no damage
    }
    return ((int (*)(void *, void *, float))c->hurt)(entity, source, damage);
}
extern "C" void lce195_ev_entity_die_thunk(void *entity, void *source) {
    uintptr_t vt = 0, level = 0;
    const VtCopy *c = rd((uintptr_t)entity, &vt, 8) ? copy_of(vt) : nullptr;
    if (!c) __builtin_trap();
    uint8_t dead = 1;
    if (!g.failed && rd((uintptr_t)entity + LCE195_OFF_ENTITY_LEVEL, &level, 8) && server_level(level) &&
        rd((uintptr_t)entity + 0x604, &dead, 1) && !dead) {   // LivingEntity::die returns early when +0x604 is set
        LevelFrame lf(level);
        lce_ev_entity ev{};
        ev.struct_size = sizeof ev; ev.entity = entity; ev.source = source; ev.type_id = -1; ev.level = (void *)level;
        int32_t at[3];
        if (entity_block_pos((uintptr_t)entity, at)) { ev.x = at[0]; ev.y = at[1]; ev.z = at[2]; }
        ++g.st.deaths;
        dispatch(LCE_EV_ENTITY_DEATH, &ev);
    }
    ((void (*)(void *, void *))c->die)(entity, source);
}

// ---- keybinds ---------------------------------------------------------------
extern "C" int lce195_keys_register(const lce_keybind_desc *d) {
    if (!d || d->struct_size < sizeof(lce_keybind_desc) || !d->fn || g.key_count >= LCE195_MAX_KEYBINDS) return 0;
    Key k{}; k.d = *d;
    switch (d->default_button) {
    case LCE_PAD_L3: k.mask = LCE195_PAD_TOUCH | LCE195_PAD_L3; break;
    case LCE_PAD_R3: k.mask = LCE195_PAD_TOUCH | LCE195_PAD_R3; break;
    case LCE_PAD_TOUCHPAD: k.mask = LCE195_PAD_TOUCH; k.hold = 1; break;
    case LCE_PAD_DPAD_LEFT_HOLD: k.mask = LCE195_PAD_LEFT; k.hold = 1; break;
    case LCE_PAD_DPAD_RIGHT_HOLD: k.mask = LCE195_PAD_RIGHT; k.hold = 1; break;
    default: return 0;
    }
    for (uint32_t i = 0; i < g.key_count; ++i) if (g.keys[i].mask == k.mask) return 0;   // one owner per chord
    g.keys[g.key_count++] = k;
    return 1;
}
namespace {
void push_key(uint32_t index, int pressed) {
    uint32_t head = __atomic_load_n(&g.key_head, __ATOMIC_RELAXED);
    if (head - __atomic_load_n(&g.key_tail, __ATOMIC_ACQUIRE) >= kKeyRing) return;   // full: drop
    g.key_ring[head % kKeyRing] = index | (pressed ? 0x100u : 0u);
    __atomic_store_n(&g.key_head, head + 1, __ATOMIC_RELEASE);
}
}
extern "C" uint32_t lce195_keys_filter(int32_t, uint32_t b, int connected, int debug_visible) {
    // remote clients have no server tick: the pad path also retries the client packet hook (bounded, cheap once installed)
    if (g.ready && g.channel_count && !g.net_client && (++g.net_attempts & 255) == 1) (void)lce195_net_client_install();
    if (!g.key_count) return b;
    const bool blocked = !connected || debug_visible || (b & (LCE195_PAD_L3 | LCE195_PAD_R3)) == (LCE195_PAD_L3 | LCE195_PAD_R3);
    uint64_t now = g.host.now_ms ? g.host.now_ms(g.host.opaque) : 0;
    uint32_t consume = 0;
    for (uint32_t i = 0; i < g.key_count; ++i) {
        Key &k = g.keys[i];
        bool on = !blocked && (b & k.mask) == k.mask;
        if (k.hold && k.mask == LCE195_PAD_TOUCH) on = on && !(b & (LCE195_PAD_L3 | LCE195_PAD_R3));   // touch alone
        if (on && !k.down) { k.down = 1; k.since = now; k.fired = 0; if (!k.hold) { k.fired = 1; push_key(i, 1); } }
        else if (on && k.hold && !k.fired) {
            if (now < k.since) k.since = now; // Clock reset: start a fresh hold interval.
            else if (now - k.since >= LCE195_HOLD_MS) { k.fired = 1; push_key(i, 1); }
        }
        else if (!on && k.down) { if (k.fired) push_key(i, 0); k.down = 0; k.fired = 0; }
        if (on && !k.hold) consume |= k.mask & ~LCE195_PAD_TOUCH;   // hide L3/R3 from the game while chorded
    }
    return b & ~consume;
}

// ---- commands -----------------------------------------------------------------
extern "C" int lce195_cmd_register(const char *modid, const lce_command_desc *d) {
    if (!modid || !*modid || !d || d->struct_size < offsetof(lce_command_desc, user) || !d->fn || !d->name || g.cmd_count >= LCE195_MAX_COMMANDS) return 0;
    const char *n = d->name[0] == '/' ? d->name + 1 : d->name;
    size_t len = strlen(n);
    if (!len || len >= 32 || !strcmp(n, "help")) return 0;
    for (size_t i = 0; i < len; ++i) { char c = n[i]; if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return 0; }
    for (uint32_t i = 0; i < g.cmd_count; ++i) if (!strcmp(g.cmds[i].name, n)) return 0;   // first owner keeps the name
    Cmd &c = g.cmds[g.cmd_count++];
    copy(c.modid, sizeof c.modid, modid); copy(c.name, sizeof c.name, n); c.d = *d;
    return 1;
}
namespace {
int submit(const char *uuid, const char *line) {
    if (!g.ready || !line || !line[0]) return 0;
    size_t n = strlen(line);
    if (n >= LCE195_CMD_LINE) return 0;
    lock_lines();
    int ok = 0;
    if (g.line_count < kLines) { Line &l = g.lines[g.line_count++]; copy(l.uuid, sizeof l.uuid, uuid); copy(l.text, sizeof l.text, line); ok = 1; }
    unlock_lines();
    return ok;
}
}
extern "C" int lce195_cmd_submit(const char *line) { return submit("", line); }
extern "C" void lce195_ev_chat_packet_thunk(void *listener, const lce195_pair *packet) {
    // Static candidate: stock slot is the "unhandled" default, so nothing is lost.
    uintptr_t player = 0; char uuid[48], text[LCE195_CMD_LINE];
    if (!g.ready || !packet || !packet->object || !rd((uintptr_t)listener + 0x78, &player, 8) || !player ||
        !wstring_utf8(player + 0x2d8, uuid, sizeof uuid) || !uuid[0] || !wstring_utf8(packet->object + 0x28, text, sizeof text)) return;
    submit(uuid, text);
}

// ---- mod networking (API 1.4) ------------------------------------------------
extern "C" int lce195_net_register_channel(const char *c) {
    if (!g.ready || !lce195_net_channel_ok(c) || g.channel_count >= 16) return 0;
    for (uint32_t i = 0; i < g.channel_count; ++i) if (!strcmp(g.channels[i], c)) return 0;   // one owner per channel
    copy(g.channels[g.channel_count], sizeof g.channels[0], c);
    ++g.channel_count;
    return 1;
}
extern "C" int lce195_ev_route_listener(uintptr_t player) {
    uintptr_t lis = 0, vt = 0;
    if (!g.ready || g.failed || !player || !g.host.write_word || !g.host.is_code || !rd(player + LCE195_OFF_PLAYER_CONN, &lis, 8) || !lis ||
        !rd(lis, &vt, 8)) return 0;
    if (g.lis_original && vt == (uintptr_t)&g.lis_table[2]) return 1;
    if (vt != g.host.image + LCE195_RVA_LISTENER_VT) return 0;
    if (!g.lis_original) {
        uint32_t slots = 0;
        for (; slots < 160; ++slots) {
            uintptr_t fn = 0;
            if (!rd(vt + 8 * slots, &fn, 8) || g.host.is_code(g.host.opaque, fn) != 1) break;
        }
        uintptr_t stock = 0;
        if (slots <= LCE195_OFF_LISTENER_PAYLOAD / 8 || slots >= 160 || !rd(vt - 16, g.lis_table, (size_t)(2 + slots) * 8)) return 0;
        stock = g.lis_table[2 + LCE195_OFF_LISTENER_PAYLOAD / 8];
#ifndef LCE195_HOST_TEST
        if (stock != g.host.image + LCE195_RVA_LISTENER_PAYLOAD) return 0;
#endif
        g.lis_stock = stock; g.lis_slots = slots;
        g.lis_table[2 + LCE195_OFF_LISTENER_PAYLOAD / 8] = (uintptr_t)&lce195_ev_payload_thunk;
        g.lis_original = vt;
    }
    if (g.host.write_word(g.host.opaque, lis, vt, (uintptr_t)&g.lis_table[2]) != 1) return 0;
    ++g.st.listeners;
    return 1;
}
extern "C" void lce195_ev_payload_thunk(void *listener, const lce195_pair *packet) {
    if (!g.lis_stock) __builtin_trap();   // only reachable through our private table
    uintptr_t pvt = 0, player = 0, data = 0; int32_t len = 0; char ch[32];
    if (g.ready && !g.failed && packet && packet->object && rd(packet->object, &pvt, 8) && pvt == g.host.image + LCE195_RVA_SB_PAYLOAD_VT &&
        rd((uintptr_t)listener + 0x78, &player, 8) && player && wstring_utf8(packet->object + 0x28, ch, sizeof ch)) {
        for (uint32_t i = 0; i < g.channel_count; ++i) {
            if (strcmp(g.channels[i], ch)) continue;
            if (!rd(packet->object + 0x50, &data, 8) || !rd(packet->object + 0x58, &len, 4) || len < 0 || len > 32767 || (len && !data)) return;
            lce195_view v{};   // the sender's own last view (host or remote player), still current on this thread
            const Player *sp = nullptr;
            for (const auto &o : g.players) if (o.used && o.view_set && o.object == player) sp = &o;
            const bool framed = sp && sp->view.player_object == player && g.host.view_current(g.host.opaque, &sp->view) == 1 &&
                                tid() == sp->view.thread;
            if (framed) v = sp->view;
            FrameScope f(framed ? &v : nullptr, framed);
            lce_ev_payload ev{};
            ev.struct_size = sizeof ev; ev.player = (void *)player; ev.channel = g.channels[i]; ev.data = (const uint8_t *)data; ev.len = (uint32_t)len;
            ++g.st.payloads;
            dispatch(LCE_EV_CUSTOM_PAYLOAD, &ev);   // consumed: a mod channel never reaches the vanilla handler
            return;
        }
    }
    ((void (*)(void *, const lce195_pair *))g.lis_stock)(listener, packet);
}

// ---- client-side mod networking (API 1.6) -------------------------------------------------------------
namespace {
const char *registered(const char *ch) {
    for (uint32_t j = 0; j < g.channel_count; ++j) if (!strcmp(g.channels[j], ch)) return g.channels[j];
    return nullptr;
}
bool client_listener_ok(uintptr_t l) {
    uintptr_t vt = 0;
    return l && rd(l, &vt, 8) && vt == g.host.image + LCE195_RVA_CLIENT_LISTENER_VT;
}
struct ClientScope {   // publishes the listener for send_to_server while a routed handler runs on this thread
    uintptr_t pl; uint64_t pt;
    explicit ClientScope(uintptr_t l) : pl(g.client_listener), pt(g.client_thread) { g.client_thread = tid(); g.client_listener = l; }
    ~ClientScope() { g.client_listener = pl; g.client_thread = pt; }
};
lce195_pair *create_wrap(int k, lce195_pair *out) {
    ((lce195_pair * (*)(lce195_pair *))g.pk_orig[k])(out);   // stock create (sret shared_ptr<Packet>)
    uintptr_t p = out ? out->object : 0, vt = 0;
    if (p && rd(p, &vt, 8) && vt == g.pk_stock_vt[k] && g.host.write_word(g.host.opaque, p, vt, (uintptr_t)&g.pk_table[k][2]) == 1)
        ++g.st.client_packets;
    return out;
}
} // namespace
extern "C" int lce195_net_client_install(void) {
    if (!g.ready || g.failed || !g.channel_count || !g.host.write_word || !g.host.is_code) return 0;
    if (__atomic_load_n(&g.net_client, __ATOMIC_ACQUIRE)) return 1;
    const uintptr_t b = g.host.image;
    const uintptr_t stock_vt[2] = {b + LCE195_RVA_PAYLOAD_VT, b + LCE195_RVA_SET_TIME_VT};
    const uintptr_t handle[2] = {b + LCE195_RVA_PAYLOAD_HANDLE, b + LCE195_RVA_SET_TIME_HANDLE};
    const uintptr_t create[2] = {b + LCE195_RVA_PAYLOAD_CREATE, b + LCE195_RVA_SET_TIME_CREATE};
    const int32_t ids[2] = {LCE195_PACKET_ID_PAYLOAD, LCE195_PACKET_ID_SET_TIME};
    uintptr_t fn = 0;
    if (!rd(b + LCE195_RVA_CLIENT_LISTENER_VT + 0x110, &fn, 8) || fn != b + LCE195_RVA_CLIENT_LISTENER_PAYLOAD) return 0;
    uintptr_t node[2] = {0, 0}, head = 0, n = 0;
    if (!rd(b + LCE195_RVA_PACKET_MAP + 8, &head, 8) || !head || !rd(head, &n, 8)) return 0;
    for (uint32_t i = 0; n && n != head && i < 512; ++i) {   // read-only walk of the factory's node list
        int32_t key = 0; uintptr_t val = 0;
        if (!rd(n + 0x10, &key, 4) || !rd(n + 0x18, &val, 8)) return 0;
        for (int k = 0; k < 2; ++k) if (key == ids[k] && val == create[k]) node[k] = n;
        if (!rd(n, &n, 8)) return 0;
    }
    if (!node[0] || !node[1]) return 0;   // static init not finished, or not the stock factory
    for (int k = 0; k < 2; ++k) {
        if (!rd(stock_vt[k] - 16, g.pk_table[k], sizeof g.pk_table[k]) || g.pk_table[k][2 + LCE195_OFF_PACKET_HANDLE / 8] != handle[k]) return 0;
        for (uint32_t i = 0; i < LCE195_PACKET_VT_SLOTS; ++i) if (g.host.is_code(g.host.opaque, g.pk_table[k][2 + i]) != 1) return 0;
        g.pk_handle[k] = handle[k]; g.pk_stock_vt[k] = stock_vt[k]; g.pk_orig[k] = create[k]; g.pk_node[k] = node[k];
    }
    g.pk_table[0][2 + LCE195_OFF_PACKET_HANDLE / 8] = (uintptr_t)&lce195_ev_client_payload_handle_thunk;
    g.pk_table[1][2 + LCE195_OFF_PACKET_HANDLE / 8] = (uintptr_t)&lce195_ev_client_pump_handle_thunk;
    if (g.host.write_word(g.host.opaque, node[0] + 0x18, create[0], (uintptr_t)&lce195_ev_payload_create_thunk) != 1) return 0;
    if (g.host.write_word(g.host.opaque, node[1] + 0x18, create[1], (uintptr_t)&lce195_ev_settime_create_thunk) != 1) {
        (void)g.host.write_word(g.host.opaque, node[0] + 0x18, (uintptr_t)&lce195_ev_payload_create_thunk, create[0]);   // undo
        return 0;
    }
    __atomic_store_n(&g.net_client, 1u, __ATOMIC_RELEASE);
    logf("events195: client packet hook installed (payload id 250, pump id 4)");
    return 1;
}
extern "C" lce195_pair *lce195_ev_payload_create_thunk(lce195_pair *out) { return create_wrap(0, out); }
extern "C" lce195_pair *lce195_ev_settime_create_thunk(lce195_pair *out) { return create_wrap(1, out); }
extern "C" void lce195_ev_client_payload_handle_thunk(void *packet, void *listener) {
    const uintptr_t p = (uintptr_t)packet, l = (uintptr_t)listener;
    uintptr_t data = 0; int32_t len = -1; char ch[32];
    const char *chan = nullptr;
    if (g.ready && !g.failed && client_listener_ok(l) && wstring_utf8(p + 0x28, ch, sizeof ch) && (chan = registered(ch)) &&
        rd(p + 0x50, &data, 8) && rd(p + 0x58, &len, 4) && len >= 0 && len <= 32767 && (!len || data)) {
        if (lce195_ev_host_server_active()) {   // host: never run a mod on the client thread while its server thread runs it
            if (!g.host_rx.push(0, chan, (const void *)data, (uint32_t)len)) ++g.st.client_dropped;
            return;
        }
        ClientScope cs(l);
        lce_ev_payload ev{};
        ev.struct_size = sizeof ev; ev.player = nullptr; ev.channel = chan; ev.data = (const uint8_t *)data; ev.len = (uint32_t)len;
        ++g.st.client_payloads;
        dispatch(LCE_EV_CLIENT_PAYLOAD, &ev);   // consumed: a mod channel never reaches the vanilla handler
        lce195_net_client_flush(l);
        return;
    }
    ((void (*)(void *, void *))(g.pk_handle[0] ? g.pk_handle[0] : g.host.image + LCE195_RVA_PAYLOAD_HANDLE))(packet, listener);
}
extern "C" void lce195_ev_client_pump_handle_thunk(void *packet, void *listener) {
    ((void (*)(void *, void *))(g.pk_handle[1] ? g.pk_handle[1] : g.host.image + LCE195_RVA_SET_TIME_HANDLE))(packet, listener);
    const uintptr_t l = (uintptr_t)listener;
    if (!g.ready || g.failed || !client_listener_ok(l) || lce195_ev_host_server_active()) return;
    ClientScope cs(l);
    ++g.st.client_pumps;
    lce195_net_client_flush(l);   // queued send_to_server calls go out on the client thread
}
extern "C" uintptr_t lce195_ev_client_listener(void) {
    const uint64_t t = tid();
    return g.client_listener && t && t == g.client_thread ? g.client_listener : 0;
}
extern "C" int lce195_ev_host_server_active(void) {
    if (!g.ready || !__atomic_load_n(&g.anchor_view_set, __ATOMIC_ACQUIRE)) return 0;
    if (!g.host.now_ms) return 1;
    const uint64_t now = g.host.now_ms(g.host.opaque), last = __atomic_load_n(&g.last_tick_ms, __ATOMIC_ACQUIRE);
    return __atomic_load_n(&g.last_tick_set, __ATOMIC_ACQUIRE) && now >= last && now - last < 3000;
}
extern "C" int lce195_ev_loopback_payload(const char *channel, const void *data, uint32_t len) {
    if (!g.ready || !channel || !registered(channel) || len > 32000) return 0;
    if (!g.host_rx.push(1, channel, data, len)) { ++g.st.client_dropped; return 0; }
    return 1;
}
extern "C" int lce195_net_channel_registered(const char *channel) { return g.ready && channel && registered(channel) ? 1 : 0; }

#ifdef LCE195_HOST_TEST
extern "C" void lce195_ev_reset_for_tests(void) { memset(&g, 0, sizeof g); }
#endif
