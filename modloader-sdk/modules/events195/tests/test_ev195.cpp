// Host tests for events195 + services195 on a fake executable 1.95 image.
// Native entry points at pinned RVAs are absolute-jump trampolines into host
// fakes, so the module's vtable/RVA identity checks run unchanged.
#include "../ev195.h"
#include "../services195.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <map>
#include <vector>
#include <sys/mman.h>
#include <functional>
#include <limits>

static int g_checks;
#define CHECK(c) do { ++g_checks; if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

static uintptr_t image;
template <class T> static void put(uintptr_t a, T v) { memcpy((void *)a, &v, sizeof v); }
static void tramp(uint32_t rva, const void *fn) {
    uint8_t *p = (uint8_t *)(image + rva);
    p[0] = 0xff; p[1] = 0x25; memset(p + 2, 0, 4); memcpy(p + 6, &fn, 8);
}

static uint64_t g_tid = 7, g_now;
static uint64_t g_session = 0x400;
static uint64_t thread_id(void *) { return g_tid; }
static int rd(void *, uintptr_t a, void *o, size_t n) { if (!a) return 0; memcpy(o, (void *)a, n); return 1; }
static int cas(void *, uintptr_t a, uintptr_t e, uintptr_t v) { uintptr_t c; memcpy(&c,(void*)a,8); if(c!=e)return 0; memcpy((void*)a,&v,8); return 1; }
static int is_code(void *, uintptr_t a) { return a >= image && a < image + 0x10c0354; }
static int current(void *, const lce195_view *v) { return v->session == g_session && v->thread == g_tid; }
static int borrow(void *, const lce195_pair *p, uintptr_t level, lce195_view *o) {
    *o = lce195_view{image,0,level,p->object,p->control,g_session,g_session+1,g_tid,1}; return 1; }
static uint64_t now_ms(void *) { return g_now; }
static void hlog(void *, const char *) {}

struct Ev { lce_event ev; std::string text; uint32_t id; int32_t x,y,z; void *player; uint32_t amount; void *level; uint32_t hand; };
static std::vector<Ev> events;
static int cancel_damage, handle_use;
static int svc_ran;
static int g_client_reply;
static int sink(void *, lce_event ev, const void *pl) {
    Ev e{}; e.ev = ev;
    if (ev == LCE_EV_PLAYER_JOIN || ev == LCE_EV_PLAYER_LEAVE) { auto p=(const lce_ev_player*)pl; e.text=p->name?p->name:""; e.player=p->player; }
    else if (ev == LCE_EV_CHAT) { auto p=(const lce_ev_chat*)pl; e.text=p->message?p->message:""; e.player=p->player; }
    else if (ev == LCE_EV_ITEM_USE) { auto p=(const lce_ev_item_use*)pl; e.id=p->item_id; e.player=p->player;
        CHECK(p->struct_size >= sizeof(lce_ev_item_use)); e.level=p->level; e.x=p->x; e.y=p->y; e.z=p->z; e.hand=p->hand; }
    else if (ev == LCE_EV_BLOCK_BREAK || ev == LCE_EV_BLOCK_PLACE || ev == LCE_EV_BLOCK_USE) { auto p=(const lce_ev_block*)pl; e.id=p->block_id; e.x=p->x; e.y=p->y; e.z=p->z; e.player=p->player; e.level=p->level; e.hand=(uint32_t)p->face; e.text=lce195_ev_frame() ? "framed" : ""; }
    else if (ev == LCE_EV_BLOCK_TICK || ev == LCE_EV_BLOCK_NEIGHBOR || ev == LCE_EV_BLOCK_STEP_ON || ev == LCE_EV_BLOCK_ENTITY_INSIDE) {
        auto p=(const lce_ev_block*)pl; e.id=p->block_id; e.x=p->x; e.y=p->y; e.z=p->z; e.player=p->player; e.level=p->level;
        e.hand=(uint32_t)p->face; e.text=lce195_ev_frame() ? "framed" : ""; }
    else if (ev == LCE_EV_CUSTOM_PAYLOAD || ev == LCE_EV_CLIENT_PAYLOAD) { auto p=(const lce_ev_payload*)pl; e.text=p->channel; e.amount=p->len; e.player=p->player; e.id=p->len ? p->data[0] : 0;
        if (ev == LCE_EV_CLIENT_PAYLOAD && g_client_reply) e.hand = (uint32_t)lce195_send_to_server(nullptr, "ruby:click", "ok", 2); }
    else if (ev == LCE_EV_ITEM_INVENTORY_TICK) { auto p=(const lce_ev_item_use*)pl; e.id=p->item_id; e.player=p->player; e.level=p->level;
        e.amount=p->aux; e.hand=p->hand; e.text=lce195_ev_frame() ? "framed" : ""; }
    else if (ev == LCE_EV_ENTITY_DAMAGE || ev == LCE_EV_ENTITY_DEATH) { auto p=(const lce_ev_entity*)pl; e.amount=p->amount_milli; e.player=p->entity; e.level=p->level; e.x=p->x; e.y=p->y; e.z=p->z; }
    events.push_back(e);
    if (ev == LCE_EV_BLOCK_USE) return handle_use;
    return (ev == LCE_EV_ENTITY_DAMAGE && cancel_damage) ? 1 : 0;
}

// Build a 40-byte Dinkumware wstring (inline buffer path) at addr from ASCII.
static void ws(uintptr_t a, const char *s) {
    size_t n = strlen(a ? s : "");
    if (n < 8) {                      // Dinkumware inline buffer (cap 7)
        uint16_t *buf = (uint16_t *)(a + 8);
        for (size_t i = 0; i < n; ++i) buf[i] = (uint8_t)s[i];
        buf[n] = 0;
        put<uint64_t>(a + 0x18, n); put<uint64_t>(a + 0x20, 7);
    } else {                          // heap: pointer at +8, cap >= n
        static uintptr_t pool; if (!pool) pool = image + 0x1400000;
        uint16_t *buf = (uint16_t *)pool; pool += (n + 1) * 2;
        for (size_t i = 0; i < n; ++i) buf[i] = (uint8_t)s[i];
        buf[n] = 0;
        put<uintptr_t>(a + 8, (uintptr_t)buf); put<uint64_t>(a + 0x18, n); put<uint64_t>(a + 0x20, n + 16);
    }
}

// ---- fake natives (recorded, minimal) ---------------------------------------
static std::vector<std::string> sent_chat;
static std::vector<std::string> given;
static uint64_t g_alloc_base;
static void *fake_alloc(size_t n) { uintptr_t a = g_alloc_base; g_alloc_base += (n + 15) & ~size_t(15); memset((void*)a,0,n); return (void*)a; }
static void fake_free(void *) {}
static int g_sub_calls; static uintptr_t g_sub_last;
static int fake_sub(uintptr_t a, int, int) { ++g_sub_calls; g_sub_last = a; return 2; }   // never last ref: skip destructors
static void fake_ws(void *dst, const uint16_t *u, uint64_t n) {
    uint64_t *w = (uint64_t *)dst;
    uint16_t *b;
    if (n < 8) { b = (uint16_t *)dst + 4; w[4] = 7; }    // inline buffer (cap 7)
    else { static uintptr_t pool; if (!pool) pool = image + 0x1420000; b = (uint16_t *)pool; pool += (n + 1) * 2; w[1] = (uint64_t)(uintptr_t)b; w[4] = n + 16; }
    for (uint64_t i = 0; i < n; i++) b[i] = u[i];
    b[n] = 0; w[3] = n;
}
// chat ctor records the message text and stamps the vtable the service checks.
static std::string g_last_chat;
static void fake_chat_ctor(void *pkt, const void *wsrc, int, int) {
    const uint64_t *w=(const uint64_t*)wsrc; uint64_t n=w[3];
    const uint16_t *b = n < 8 ? (const uint16_t*)wsrc+4 : (const uint16_t*)(uintptr_t)w[1];
    std::string t; for (uint64_t i=0;i<n;i++) t+=(char)b[i]; g_last_chat=t;
    put<uintptr_t>((uintptr_t)pkt, image + 0x1228438);
}
static void fake_enable_shared(lce195_pair *o, uintptr_t obj, uintptr_t ctrl) { o->object=obj; o->control=ctrl; }
static uintptr_t g_broadcast_player_list, g_conn_sent;
static void fake_broadcast(uintptr_t pl, lce195_pair *) { g_broadcast_player_list=pl; sent_chat.push_back(g_last_chat); }
static void fake_conn_send(uintptr_t c, lce195_pair *) { g_conn_sent=c; sent_chat.push_back(g_last_chat); }


// resolver for services
static int32_t res_item(void *, const char *k) { return !strcmp(k,"m:ruby")?2300 : !strcmp(k,"minecraft:stone")?1 : -1; }
static int32_t res_block(void *, const char *k) { return !strcmp(k,"m:rubyblock")?253 : -1; }

// world ops (record calls)
static std::vector<std::string> ops_log;
static int op_give(void *, const lce195_view *, uint32_t id, uint32_t n, uint32_t aux) {
    char b[64]; snprintf(b,sizeof b,"give %u x%u/%u",id,n,aux); ops_log.push_back(b); return 1; }
static int op_set(void *, const lce195_view *, int32_t x,int32_t y,int32_t z, uint16_t id, uint8_t m) {
    char b[64]; snprintf(b,sizeof b,"set %d,%d,%d=%u:%u",x,y,z,id,m); ops_log.push_back(b); return 1; }
static uint16_t g_getblock_id=5; static uint8_t g_getblock_meta=3;
static int op_get(void *, const lce195_view *, int32_t,int32_t,int32_t, uint16_t *id, uint8_t *m) { *id=g_getblock_id; *m=g_getblock_meta; return 1; }
static int op_spawn(void *, const lce195_view *, const char *k, double,double,double, int) { ops_log.push_back(std::string("spawn ")+k); return 1; }
static int op_loaded(void *, const lce195_view *, int32_t,int32_t) { return 1; }

// API 1.2 fakes
static float g_hp = 10.0f, g_hpmax = 20.0f;
static float f_hget(uintptr_t) { return g_hp; }
static void f_hset(uintptr_t, float v) { g_hp = v; }
static float f_hmax(uintptr_t) { return g_hpmax; }
static uint8_t f_alive(uintptr_t) { return g_hp > 0; }
static void f_fire_clear(uintptr_t e) { put<int32_t>(e + 0x1c0, 0); }
static void f_set_count(uintptr_t st, int32_t n) { put<int32_t>(st + 0x10, n); put<uint8_t>(st + 0x38, n <= 0); }
static void f_changed(uintptr_t inv) { int32_t r; memcpy(&r, (void*)(inv + 0x138), 4); put<int32_t>(inv + 0x138, r + 1); }
static int32_t f_maxdmg(uintptr_t) { return 100; }
static uint8_t f_hurt(uintptr_t st, int32_t n, uintptr_t rng) { CHECK(rng != 0); int32_t d; memcpy(&d,(void*)(st+0x3c),4); d += n; put<int32_t>(st+0x3c, d); return d >= 100; }
static void f_setdmg(uintptr_t st, int32_t v) { put<int32_t>(st + 0x3c, v < 0 ? 0 : v); }
static std::vector<int32_t> added;
static uint8_t f_inv_add(uintptr_t, const lce195_pair *sp) {   // a 10-item capacity inventory
    int32_t n; memcpy(&n, (void*)(sp->object + 0x10), 4); static int32_t room = 10;
    int32_t take = n < room ? n : room; room -= take; added.push_back(take);
    put<int32_t>(sp->object + 0x10, n - take); put<uint8_t>(sp->object + 0x38, n - take <= 0); return take > 0; }
static int dropped_rest;
static void f_pop(uintptr_t, const int32_t *, const lce195_pair *) { ++dropped_rest; }
static uintptr_t f_item_by_id(unsigned) { return image + 0x1360000; }
static void f_stack_ctor(uintptr_t st, uintptr_t item, int32_t n, int32_t aux) { put<uintptr_t>(st+0x28,item); put<int32_t>(st+0x10,n); put<int32_t>(st+0x3c,aux); put<uint8_t>(st+0x38,0); }
static void f_stack_bind(lce195_pair *o, uintptr_t obj, uintptr_t ctrl) { o->object = obj; o->control = ctrl; }
static std::vector<std::string> boom;
static int op_explode(void *, const lce195_view *, double x, double y, double z, float r, int fire, int destroy) {
    char b[96]; snprintf(b, sizeof b, "boom %.1f,%.1f,%.1f r%.1f f%d d%d", x, y, z, r, fire, destroy); boom.push_back(b); return 1; }
static int svc12_ran;
static uintptr_t g_bob, g_pas_obj; static int g_src_deleted;
static uintptr_t f_pas(const lce195_pair *p) { g_pas_obj = p->object; uintptr_t src = image + 0x1390000; put<uintptr_t>(src, image + 0x124cb48); return src; }
static void f_src_delete(uintptr_t) { ++g_src_deleted; }
static std::string wkey(const void *w);
static std::string wkey_fwd(const void *w);
static uintptr_t g_mas_obj;
static std::string g_pl_channel; static int32_t g_pl_len; static uint8_t g_pl_first;
static void f_payload_ctor(void *pkt, const void *ch, uint8_t *data, int32_t len) {
    put<uintptr_t>((uintptr_t)pkt, image + 0x12287e0); g_pl_channel = wkey_fwd(ch); g_pl_len = len; g_pl_first = data ? data[0] : 0; }
// API 1.6 client send fakes: ServerboundCustomPayloadPacket ctor + ClientPacketListener::send
static std::vector<std::string> g_client_sent; static uintptr_t g_client_send_listener;
static void f_sb_payload_ctor(void *pkt, const void *ch, uint8_t *data, int32_t len) {
    put<uintptr_t>((uintptr_t)pkt, image + 0x12bb4c0); std::string t = wkey_fwd(ch) + "|"; for (int32_t i = 0; i < len; ++i) t += (char)data[i];
    g_client_sent.push_back(t); }
static void f_client_send(uintptr_t l, lce195_pair *sp) { g_client_send_listener = l; uintptr_t vt = 0; memcpy(&vt, (void*)sp->object, 8); CHECK(vt == image + 0x12bb4c0); }
// item NBT fakes: one CompoundTag; tags are 0x40-byte objects with the real tag vtables, keyed by the wstring's units
static std::map<std::string, uintptr_t> g_tags; static uintptr_t g_tag_pool;
std::string wkey_fwd(const void *w) { return wkey(w); }
static std::string wkey(const void *w) { const uint64_t *q = (const uint64_t *)w; const uint16_t *u = q[4] > 7 ? (const uint16_t *)q[1] : (const uint16_t *)&q[1];
    std::string k; for (uint64_t i = 0; i < q[3]; ++i) k += (char)u[i]; return k; }
static uintptr_t new_tag(uintptr_t vt) { uintptr_t t = image + 0x13c1000 + (g_tag_pool++) * 0x40; put<uintptr_t>(t, image + vt); return t; }
static uintptr_t f_tag_goc(uintptr_t st) { uintptr_t t = 0; memcpy(&t, (void*)(st + 0x30), 8); if (!t) { t = image + 0x13c0000; put<uintptr_t>(t, image + 0x122ede0); put<uintptr_t>(st + 0x30, t); } return t; }
static std::map<std::pair<uintptr_t, std::string>, uintptr_t> g_ctags;
static uintptr_t f_tag_get(uintptr_t tag, const void *k) {
    if (tag != image + 0x13c0000) { auto j = g_ctags.find({tag, wkey(k)}); return j == g_ctags.end() ? 0 : j->second; }
    auto i = g_tags.find(wkey(k)); return i == g_tags.end() ? 0 : i->second; }
static void f_tag_remove(uintptr_t, const void *k) { g_tags.erase(wkey(k)); }
static void f_put_int(uintptr_t, const void *k, int32_t v) { uintptr_t t = new_tag(0x126e2c8); put<int32_t>(t + 8, v); g_tags[wkey(k)] = t; }
static void f_put_double(uintptr_t, const void *k, double v) { uintptr_t t = new_tag(0x123cfd0); put<double>(t + 8, v); g_tags[wkey(k)] = t; }
static void f_put_string(uintptr_t, const void *k, const void *v) { uintptr_t t = new_tag(0x12d83a8); memcpy((void*)(t + 8), v, 40); g_tags[wkey(k)] = t; }
static uintptr_t g_ptype; static int32_t g_pcount; static double g_px, g_pspeed;
static void f_send_particles(uintptr_t lvl, uintptr_t type, uint8_t ov, int32_t n, const int32_t *data, int32_t nd, double x, double, double,
                             double, double, double, double sp) {
    CHECK(lvl == image + 0x1000000 && !ov && !data && !nd); g_ptype = type; g_pcount = n; g_px = x; g_pspeed = sp; }
static unsigned g_tinted_calls;
static void f_send_tinted_particles(uintptr_t level, uintptr_t type, int32_t count, const int32_t *data, int32_t ndata,
                                    double x, double y, double z, double dx, double dy, double dz, double speed,
                                    double red, double green, double blue) {
    CHECK(level == image + 0x1000000 && type == image + 0x13b0100);
    CHECK(count == 7 && !data && !ndata);
    CHECK(x == 10.5 && y == 65.0 && z == -3.5 && dx == 1.25 && dy == dx && dz == dx && speed == 0);
    CHECK(red == 18.0 / 255 && green == 128.0 / 255 && blue == 1.0);
    ++g_tinted_calls;
}
static uintptr_t f_mas(const lce195_pair *p) { g_mas_obj = p->object; uintptr_t src = image + 0x1390100; put<uintptr_t>(src, image + 0x124cb48); return src; }
static void f_weak_lock(lce195_pair *o, uintptr_t obj, uintptr_t ctrl, int thr) {
    CHECK(thr == 0); int32_t n; memcpy(&n, (void*)(ctrl + 8), 4); if (n <= 0) return; put<int32_t>(ctrl + 8, n + 1); o->object = obj; o->control = ctrl; }
static float g_last_hurt; static uintptr_t g_last_src;
static uint8_t f_player_hurt(uintptr_t, uintptr_t src, float v) { g_last_hurt = v; g_last_src = src; return 1; }
static void f_teleport(uintptr_t e, double x, double y, double z) { double p[3] = {x, y, z}; memcpy((void*)(e + 0x170), p, sizeof p); }
static void f_push(uintptr_t e, double x, double y, double z) { double d[3]; memcpy(d, (void*)(e + 0x188), sizeof d); d[0]+=x; d[1]+=y; d[2]+=z;
    memcpy((void*)(e + 0x188), d, sizeof d); put<uint8_t>(e + 0x2a9, 1); }
// ---- API 1.5 entity NBT fakes ----------------------------------------------------------------------------
alignas(16) static uint8_t g_arena[4 << 20]; static size_t g_arena_used;   // leak-free fake heap (never reused)
static void *arena(size_t n, size_t k = 1) { n *= k; void *p = g_arena + g_arena_used; g_arena_used += (n + 15) & ~size_t(15); if (g_arena_used > sizeof g_arena) abort(); memset(p, 0, n); return p; }
// Temporary compounds made by the service: a map per compound address; Entity::saveWithoutId fills a fixed set of keys.
static int g_compounds, g_compound_deleted, g_saves;
static void f_compound_ctor(uintptr_t t) { put<uintptr_t>(t, image + 0x122ede0); ++g_compounds; }
static void f_compound_delete(uintptr_t t) { ++g_compound_deleted; for (auto i = g_ctags.begin(); i != g_ctags.end();) i = i->first.first == t ? g_ctags.erase(i) : std::next(i); }
static uintptr_t heap_tag(uintptr_t vt, size_t n = 0x40) { uintptr_t t = (uintptr_t)arena(1, n); put<uintptr_t>(t, image + vt); return t; }
static uintptr_t f_entity_save(uintptr_t e, uintptr_t tag) {
    ++g_saves;
    double pos[3]; memcpy(pos, (void*)(e + 0x170), sizeof pos);
    uintptr_t health = heap_tag(0x1258388); put<float>(health + 8, 20.0f);
    uintptr_t list = heap_tag(0x1278268), elems = (uintptr_t)arena(3, 8);
    for (int i = 0; i < 3; ++i) { uintptr_t d = heap_tag(0x123cfd0); put<double>(d + 8, pos[i]); put<uintptr_t>(elems + 8 * i, d); }
    put<uintptr_t>(list + 0x10, elems); put<uintptr_t>(list + 0x18, elems + 24);
    uintptr_t attr = heap_tag(0x122ede0), attrs = heap_tag(0x1278268), ae = (uintptr_t)arena(1, 8);
    uintptr_t base = heap_tag(0x123cfd0); put<double>(base + 8, 0.25); g_ctags[{attr, "Base"}] = base;
    put<uintptr_t>(ae, attr); put<uintptr_t>(attrs + 0x10, ae); put<uintptr_t>(attrs + 0x18, ae + 8);
    uintptr_t name = heap_tag(0x12d83a8); ws(name + 8, "Zed the zombie");
    g_ctags[{tag, "Health"}] = health; g_ctags[{tag, "Pos"}] = list; g_ctags[{tag, "Attributes"}] = attrs; g_ctags[{tag, "CustomName"}] = name;
    return tag;
}
// Entity tag set: real Dinkumware-shaped nodes (left +0, parent +8, right +0x10, color +0x18, isnil +0x19, wstring +0x20) in an
// unbalanced BST, so the module's in-order walk and head reset run on the real layout.
static int g_nodes_freed, g_inserts;
static std::u16string node_str(uintptr_t n) { const uint64_t *w = (const uint64_t *)(n + 0x20); const uint16_t *u = w[4] > 7 ? (const uint16_t *)w[1] : (const uint16_t *)&w[1];
    return std::u16string((const char16_t *)u, w[3]); }
static void f_tagset_insert(void *out, uintptr_t set, int leftish, const void *wsv) {
    if (!out || leftish) { fprintf(stderr, "FAIL tagset insert ABI\n"); exit(1); } ++g_inserts;
    const uint64_t *w = (const uint64_t *)wsv; const uint16_t *u = w[4] > 7 ? (const uint16_t *)w[1] : (const uint16_t *)&w[1];
    std::u16string v((const char16_t *)u, w[3]);
    uintptr_t head; memcpy(&head, (void*)(set + 8), 8);
    uintptr_t n = (uintptr_t)arena(1, 0x48), *link = nullptr, parent = head, cur; memcpy(&cur, (void*)(head + 8), 8);
    while (cur != head) { std::u16string c = node_str(cur); if (c == v) { return; }
        parent = cur; link = (uintptr_t *)(cur + (v < c ? 0 : 0x10)); cur = *link; }
    uint64_t *nw = (uint64_t *)(n + 0x20);
    if (v.size() < 8) { memcpy(&nw[1], v.data(), 2 * v.size()); nw[4] = 7; }
    else { uint16_t *b = (uint16_t *)arena(v.size() + 1, 2); memcpy(b, v.data(), 2 * v.size()); nw[1] = (uint64_t)(uintptr_t)b; nw[4] = v.size() + 3; }
    nw[3] = v.size();
    put<uintptr_t>(n, head); put<uintptr_t>(n + 0x10, head); put<uintptr_t>(n + 8, parent);
    if (link) *link = n; else put<uintptr_t>(head + 8, n);
    uintptr_t mn, mx; memcpy(&mn, (void*)head, 8); memcpy(&mx, (void*)(head + 0x10), 8);
    if (mn == head || v < node_str(mn)) put<uintptr_t>(head, n);
    if (mx == head || node_str(mx) < v) put<uintptr_t>(head + 0x10, n);
    uint64_t sz; memcpy(&sz, (void*)(set + 0x10), 8); put<uint64_t>(set + 0x10, sz + 1);
}
static void f_tagset_erase(uintptr_t set, uintptr_t n) {
    uint8_t nil; memcpy(&nil, (void*)(n + 0x19), 1); if (nil) return;
    uintptr_t l, r; memcpy(&l, (void*)n, 8); memcpy(&r, (void*)(n + 0x10), 8);
    f_tagset_erase(set, l); f_tagset_erase(set, r); memset((void*)n, 0xdd, 0x48); ++g_nodes_freed;
}
static uintptr_t make_tag_entity(uintptr_t level) {
    uintptr_t e = (uintptr_t)arena(1, 0x800), head = (uintptr_t)arena(1, 0x48);
    put<uintptr_t>(e, image + 0x1300000 - 0x800); put<uintptr_t>(image + 0x1300000 - 0x800, image + 0x380000);   // vtable in the image, slot 0 code
    put<uintptr_t>(e + 0x150, level); put<double>(e + 0x170, 1.5); put<double>(e + 0x178, 70.0); put<double>(e + 0x180, -2.5);
    put<uintptr_t>(head, head); put<uintptr_t>(head + 8, head); put<uintptr_t>(head + 0x10, head); put<uint8_t>(head + 0x18, 1); put<uint8_t>(head + 0x19, 1);
    put<uintptr_t>(e + 0x368, head);
    return e;
}
static std::vector<std::string> entity_tags(uintptr_t e) {   // in order, via the head's leftmost and parent links (like the module)
    std::vector<std::string> out; uintptr_t head; memcpy(&head, (void*)(e + 0x368), 8);
    std::vector<uintptr_t> st; uintptr_t root; memcpy(&root, (void*)(head + 8), 8);
    std::function<void(uintptr_t)> rec = [&](uintptr_t n) { if (n == head) return; uintptr_t l, r; memcpy(&l,(void*)n,8); memcpy(&r,(void*)(n+0x10),8);
        rec(l); std::u16string s = node_str(n); out.push_back(std::string(s.begin(), s.end())); rec(r); };
    rec(root); return out;
}
static void entity_nbt_tests(uintptr_t alice) {
    const uintptr_t level = image + 0x1000000;
    uintptr_t zed = make_tag_entity(level);
    lce_nbt_value v{}, o{};
    // ---- read-only vanilla NBT through a temporary tag ----
    CHECK(lce195_entity_nbt_get(nullptr, (void*)zed, "Health", &o) == LCE_OK && o.type == LCE_NBT_DOUBLE && o.d == 20.0);
    CHECK(lce195_entity_nbt_get(nullptr, (void*)zed, "Pos[1]", &o) == LCE_OK && o.d == 70.0);
    CHECK(lce195_entity_nbt_get(nullptr, (void*)zed, "Pos[2]", &o) == LCE_OK && o.d == -2.5);
    CHECK(lce195_entity_nbt_get(nullptr, (void*)zed, "Pos[#]", &o) == LCE_OK && o.type == LCE_NBT_INT && o.i == 3);
    CHECK(lce195_entity_nbt_get(nullptr, (void*)zed, "Attributes[0]/Base", &o) == LCE_OK && o.d == 0.25);
    CHECK(lce195_entity_nbt_get(nullptr, (void*)zed, "CustomName", &o) == LCE_OK && o.type == LCE_NBT_STRING && !strcmp(o.s, "Zed the zombie"));
    CHECK(lce195_entity_nbt_get(nullptr, (void*)zed, "Pos[3]", &o) == LCE_E_NOTFOUND && lce195_entity_nbt_get(nullptr, (void*)zed, "Nope", &o) == LCE_E_NOTFOUND);
    CHECK(lce195_entity_nbt_get(nullptr, (void*)zed, "Health/x", &o) == LCE_E_INVALID);        // key into a float
    CHECK(lce195_entity_nbt_get(nullptr, (void*)zed, "Health[0]", &o) == LCE_E_INVALID);       // index into a non-list
    CHECK(lce195_entity_nbt_get(nullptr, (void*)zed, "Pos", &o) == LCE_E_INVALID);             // a list is not a scalar
    int saves = g_saves;
    for (const char *bad : {"", "Pos[", "Pos[x]", "Pos[#]/x", "a//b", "a/", "bad key", "Pos[1]x"})
        CHECK(lce195_entity_nbt_get(nullptr, (void*)zed, bad, &o) == LCE_E_INVALID);
    CHECK(g_saves == saves + 1);   // only "Pos[#]/x" passes the syntax check and reaches the save
    CHECK(g_compound_deleted == g_compounds && g_compounds == g_saves);   // every temporary tag was deleted through vt +0x30
    CHECK(lce195_entity_nbt_get(nullptr, nullptr, "Health", &o) == LCE_OK);                      // NULL = the frame player
    uintptr_t other_level = make_tag_entity(image + 0x1001000);
    CHECK(lce195_entity_nbt_get(nullptr, (void*)other_level, "Health", &o) == LCE_E_INVALID);     // not the frame's level
    // ---- persistent mod data in the entity tag set ----
    CHECK(lce195_entity_data_get(nullptr, (void*)zed, "mymod:mana", &o) == LCE_E_NOTFOUND);
    v.type = LCE_NBT_INT; v.i = 42; CHECK(lce195_entity_data_set(nullptr, (void*)zed, "mymod:mana", &v) == LCE_OK);
    CHECK(entity_tags(zed) == std::vector<std::string>{"lce|mymod:mana=i42"});
    CHECK(lce195_entity_data_get(nullptr, (void*)zed, "mymod:mana", &o) == LCE_OK && o.type == LCE_NBT_INT && o.i == 42);
    int ins = g_inserts; CHECK(lce195_entity_data_set(nullptr, (void*)zed, "mymod:mana", &v) == LCE_OK && g_inserts == ins);   // unchanged: no write
    { uint16_t u[5] = {'b','o','s','s','y'}; uint64_t w[5] = {0,0,0,0,7}; fake_ws(w, u, 5); uint64_t out[8]; f_tagset_insert(out, zed + 0x360, 0, w); }   // a vanilla tag
    v.i = -2147483648LL; CHECK(lce195_entity_data_set(nullptr, (void*)zed, "mymod:mana", &v) == LCE_OK);   // replace = rebuild
    CHECK(g_nodes_freed == 2 && (entity_tags(zed) == std::vector<std::string>{"bossy", "lce|mymod:mana=i-2147483648"}));
    CHECK(lce195_entity_data_get(nullptr, (void*)zed, "mymod:mana", &o) == LCE_OK && o.i == -2147483648LL);
    v.type = LCE_NBT_DOUBLE; v.d = 0.1; CHECK(lce195_entity_data_set(nullptr, (void*)zed, "mymod:speed", &v) == LCE_OK);
    CHECK(lce195_entity_data_get(nullptr, (void*)zed, "mymod:speed", &o) == LCE_OK && o.type == LCE_NBT_DOUBLE && o.d == 0.1);   // exact bits
    v.type = LCE_NBT_STRING; snprintf(v.s, sizeof v.s, "owner = Steve [team a]"); CHECK(lce195_entity_data_set(nullptr, (void*)zed, "mymod:owner", &v) == LCE_OK);
    CHECK(lce195_entity_data_get(nullptr, (void*)zed, "mymod:owner", &o) == LCE_OK && o.type == LCE_NBT_STRING && !strcmp(o.s, "owner = Steve [team a]"));
    CHECK(lce195_entity_data_get(nullptr, (void*)zed, "mymod:man", &o) == LCE_E_NOTFOUND);   // a prefix of another key is not that key
    CHECK(lce195_entity_data_remove(nullptr, (void*)zed, "mymod:mana") == LCE_OK && lce195_entity_data_get(nullptr, (void*)zed, "mymod:mana", &o) == LCE_E_NOTFOUND);
    CHECK(lce195_entity_data_remove(nullptr, (void*)zed, "mymod:mana") == LCE_E_NOTFOUND);
    CHECK((entity_tags(zed) == std::vector<std::string>{"bossy", "lce|mymod:owner=sowner = Steve [team a]", "lce|mymod:speed=d3fb999999999999a"}));
    // refusals: players (write), bad values, other frames
    v.type = LCE_NBT_INT; v.i = 1;
    CHECK(lce195_entity_data_set(nullptr, (void*)alice, "mymod:x", &v) == LCE_E_INVALID && lce195_entity_data_set(nullptr, nullptr, "mymod:x", &v) == LCE_E_INVALID);
    CHECK(lce195_entity_data_remove(nullptr, (void*)alice, "mymod:x") == LCE_E_INVALID);
    v.i = (int64_t)1 << 33; CHECK(lce195_entity_data_set(nullptr, (void*)zed, "mymod:x", &v) == LCE_E_INVALID);
    v.type = LCE_NBT_STRING; memset(v.s, 'a', 201); v.s[201] = 0; CHECK(lce195_entity_data_set(nullptr, (void*)zed, "mymod:x", &v) == LCE_E_INVALID);
    v.s[200] = 0; CHECK(lce195_entity_data_set(nullptr, (void*)zed, "mymod:long", &v) == LCE_OK);
    snprintf(v.s, sizeof v.s, "tab\there"); CHECK(lce195_entity_data_set(nullptr, (void*)zed, "mymod:x", &v) == LCE_E_INVALID);
    CHECK(lce195_entity_data_set(nullptr, (void*)zed, "bad=key", &v) == LCE_E_INVALID && lce195_entity_data_get(nullptr, (void*)zed, "", &o) == LCE_E_INVALID);
    CHECK(lce195_entity_data_set(nullptr, (void*)other_level, "mymod:x", &v) == LCE_E_INVALID);
    // the 1024-tag limit of Entity::load
    { uint64_t sz; memcpy(&sz, (void*)(zed + 0x370), 8);
      for (uint64_t r = 0, added = 0; r < 1024 && added < 1024 - sz; ++r) {   // bit-reversed order keeps the fake BST shallow (real: red-black)
          unsigned i = 0; for (int bit = 0; bit < 10; ++bit) if (r >> bit & 1) i |= 1u << (9 - bit);
          ++added; char b[16]; snprintf(b, sizeof b, "t%05u", i); uint16_t u[16]; size_t n = strlen(b);
          for (size_t j = 0; j < n; ++j) { u[j] = (uint8_t)b[j]; }
          uint64_t w[5] = {0,0,0,0,7}; fake_ws(w, u, n); uint64_t out[8]; f_tagset_insert(out, zed + 0x360, 0, w); }
      v.type = LCE_NBT_INT; v.i = 5; CHECK(lce195_entity_data_set(nullptr, (void*)zed, "mymod:new", &v) == LCE_E_FULL);
      CHECK(lce195_entity_data_set(nullptr, (void*)zed, "mymod:speed", &v) == LCE_OK);      // a replacement still fits
      CHECK(lce195_entity_data_get(nullptr, (void*)zed, "mymod:speed", &o) == LCE_OK && o.i == 5);
      memcpy(&sz, (void*)(zed + 0x370), 8); CHECK(sz == 1024 && entity_tags(zed).size() == 1024); }
    // a corrupt tree (size disagrees with the nodes) is refused before anything is written
    { uint64_t sz; memcpy(&sz, (void*)(zed + 0x370), 8); put<uint64_t>(zed + 0x370, sz + 1); int freed = g_nodes_freed;
      CHECK(lce195_entity_data_remove(nullptr, (void*)zed, "mymod:speed") == LCE_E_BACKEND && g_nodes_freed == freed);
      CHECK(lce195_entity_data_get(nullptr, (void*)zed, "mymod:nothing", &o) == LCE_E_BACKEND); put<uint64_t>(zed + 0x370, sz); }
}
static int svc12_task(void *u, lce_event, const void *) {
    uintptr_t a = *(uintptr_t*)u;
    // heal: clamped to max, readback through the native getter
    g_hp = 10.0f; CHECK(lce195_heal(nullptr, (void*)a, 4500) == LCE_OK); CHECK(g_hp == 14.5f);
    CHECK(lce195_heal(nullptr, nullptr, 100000) == LCE_OK && g_hp == 20.0f);
    g_hp = 0.0f; CHECK(lce195_heal(nullptr, (void*)a, 1000) == LCE_E_INVALID); g_hp = 20.0f;   // dead: refused
    CHECK(lce195_heal(nullptr, (void*)(a + 0x8000), 1000) == LCE_E_INVALID);                    // not a living entity of the frame
    // fire: CAS on +0x1c0 keeps the high half, never shortens, 0 extinguishes natively
    put<int32_t>(a + 0x1c4, 0x5a5a5a5a);
    CHECK(lce195_set_fire(nullptr, (void*)a, 100) == LCE_OK);
    int32_t ft = 0, hi = 0; memcpy(&ft, (void*)(a + 0x1c0), 4); memcpy(&hi, (void*)(a + 0x1c4), 4); CHECK(ft == 100 && hi == 0x5a5a5a5a);
    CHECK(lce195_set_fire(nullptr, (void*)a, 40) == LCE_OK); memcpy(&ft, (void*)(a + 0x1c0), 4); CHECK(ft == 100);
    CHECK(lce195_set_fire(nullptr, (void*)a, 0) == LCE_OK); memcpy(&ft, (void*)(a + 0x1c0), 4); CHECK(ft == 0);
    // held item: setCount + Inventory::setChanged; refuses more than remain
    CHECK(lce195_consume_held_item(nullptr, (void*)a, 2) == LCE_OK);
    int32_t cnt = 0, rev = 0; uintptr_t st = image + 0x1340000, inv = image + 0x1330000;
    memcpy(&cnt, (void*)(st + 0x10), 4); memcpy(&rev, (void*)(inv + 0x138), 4); CHECK(cnt == 3 && rev == 1);
    CHECK(lce195_consume_held_item(nullptr, (void*)a, 4) == LCE_E_INVALID);
    CHECK(lce195_consume_held_item(nullptr, (void*)a, 3) == LCE_OK); CHECK(*(uint8_t*)(st + 0x38) == 1);   // emptied like shrink()
    // explode through the world op, after the chunk check
    CHECK(lce195_explode(nullptr, nullptr, 10.5, 64.0, -3.5, 3.0f, 1, 0) == LCE_OK);
    CHECK(boom.size() == 1 && boom[0] == "boom 10.5,64.0,-3.5 r3.0 f1 d0");
    CHECK(lce195_explode(nullptr, nullptr, 0, 64, 0, 40.0f, 0, 0) == LCE_E_INVALID);
    // held item damage: native hurt with the level Random; broken -> shrink + damage reset
    { uintptr_t st = image + 0x1340000; put<int32_t>(st + 0x10, 2); put<uint8_t>(st + 0x38, 0); put<int32_t>(st + 0x3c, 90);
      put<uintptr_t>(image + 0x1000000 + 0x1b8, image + 0x1370000);
      CHECK(lce195_damage_held_item(nullptr, (void*)a, 5) == LCE_OK); int32_t d, c; memcpy(&d,(void*)(st+0x3c),4); CHECK(d == 95);
      CHECK(lce195_damage_held_item(nullptr, (void*)a, 5) == LCE_OK); memcpy(&d,(void*)(st+0x3c),4); memcpy(&c,(void*)(st+0x10),4);
      CHECK(d == 0 && c == 1); }
    // teleport / set_motion through the entity's own virtuals (+0x670 / +0x230)
    { CHECK(lce195_teleport(nullptr, (void*)a, 100.5, 70.0, -20.25) == LCE_OK);
      double pos[3]; memcpy(pos, (void*)(a + 0x170), sizeof pos); CHECK(pos[0] == 100.5 && pos[1] == 70.0 && pos[2] == -20.25);
      CHECK(lce195_set_motion(nullptr, (void*)a, 0.0, 0.8, 0.1) == LCE_OK);
      double dm[3]; memcpy(dm, (void*)(a + 0x188), sizeof dm); CHECK(dm[1] == 0.8 && *(uint8_t*)(a + 0x2a9) == 1);
      CHECK(lce195_set_motion(nullptr, (void*)a, 0, 50, 0) == LCE_E_INVALID); }
    // damage_entity: GENERIC source global -> virtual hurt
    { CHECK(lce195_damage_entity(nullptr, (void*)a, 3000, nullptr) == LCE_OK && g_last_hurt == 3.0f && g_last_src == image + 0x1380000); }
    // API 1.3 attacker: playerAttack(&frame player pair) -> EntityDamageSource, hurt, deleting dtor (vt +8)
    { g_src_deleted = 0; g_pas_obj = 0;
      CHECK(lce195_damage_entity(nullptr, (void*)g_bob, 2000, (void*)a) == LCE_OK && g_last_hurt == 2.0f && g_last_src == image + 0x1390000);
      CHECK(g_pas_obj == a && g_src_deleted == 1);
      CHECK(lce195_damage_entity(nullptr, (void*)a, 1000, (void*)a) == LCE_E_INVALID);            // self-attack
      CHECK(lce195_damage_entity(nullptr, (void*)a, 1000, (void*)g_bob) == LCE_E_INVALID);        // bob: no weak_ptr<this> (not shared-owned)
      // mob attacker: lock weak_ptr<this> (+0x8/+0x10), mobAttack, hurt, delete, release the lock
      uintptr_t mob = image + 0x13a0000, mctl = image + 0x13a1000;
      put<uintptr_t>(mob, image + 0x1341168); put<uintptr_t>(mob + 0x150, image + 0x1000000); put<uint8_t>(mob + 0x1fa, 0);
      put<uintptr_t>(mob + 8, mob); put<uintptr_t>(mob + 0x10, mctl); put<uintptr_t>(mctl, image + 0x1214808); put<int32_t>(mctl + 8, 1); put<int32_t>(mctl + 12, 1);
      g_src_deleted = 0; g_sub_last = 0;
      CHECK(lce195_damage_entity(nullptr, (void*)a, 1500, (void*)mob) == LCE_OK && g_last_hurt == 1.5f && g_last_src == image + 0x1390100);
      int32_t uses = 0; memcpy(&uses, (void*)(mctl + 8), 4);
      CHECK(g_mas_obj == mob && g_src_deleted == 1 && uses == 2 && g_sub_last == mctl + 8);   // lock taken, then released (fake sub)
      put<int32_t>(mctl + 8, 0);
      CHECK(lce195_damage_entity(nullptr, (void*)a, 1500, (void*)mob) == LCE_E_INVALID && g_src_deleted == 1);   // expired
      // particles: read-only walk of the byId map's node list (sentinel at [map+8]; node next/key +0x10/value +0x18)
      uintptr_t map = image + 0x142e1b8, head = image + 0x13b0000, n1 = head + 0x40, n2 = head + 0x80, t30 = head + 0x100, t31 = head + 0x180;
      put<uintptr_t>(map + 8, head); put<uintptr_t>(head, n1); put<uintptr_t>(n1, n2); put<uintptr_t>(n2, head);
      put<int32_t>(n1 + 0x10, 30); put<uintptr_t>(n1 + 0x18, t30); put<int32_t>(t30 + 0x28, 30);
      put<int32_t>(n2 + 0x10, 31); put<uintptr_t>(n2 + 0x18, t31); put<int32_t>(t31 + 0x28, 99);   // corrupt: id mismatch
      CHECK(lce195_spawn_particles(nullptr, nullptr, 30, 10.5, 65.0, -3.5, 12, 0.5, 0.5, 0.5, 0.1) == LCE_OK);
      CHECK(g_ptype == t30 && g_pcount == 12 && g_px == 10.5 && g_pspeed == 0.1);
      CHECK(lce195_spawn_particles(nullptr, nullptr, 31, 10.5, 65.0, -3.5, 1, 0, 0, 0, 0) == LCE_E_NOTFOUND);
      CHECK(lce195_spawn_particles(nullptr, nullptr, 77, 10.5, 65.0, -3.5, 1, 0, 0, 0, 0) == LCE_E_NOTFOUND);
      CHECK(lce195_spawn_particles(nullptr, nullptr, 30, 10.5, 65.0, -3.5, 5000, 0, 0, 0, 0) == LCE_E_INVALID);
      CHECK(lce195_spawn_particles(nullptr, (void*)(image + 0x1001000), 30, 0, 64, 0, 1, 0, 0, 0, 0) == LCE_E_PHASE);   // not the frame level
      const uintptr_t tint_global = image + LCE195_RVA_TINTED_PARTICLE_TYPE;
      put<uintptr_t>(tint_global, t30);
      CHECK(lce195_spawn_tinted_particles(nullptr, nullptr, 0x1280ff, 10.5, 65.0, -3.5, 7, 1.25) == LCE_OK);
      CHECK(lce195_spawn_tinted_particles(nullptr, (void*)(image + 0x1001000), 0, 0, 64, 0, 1, 0) == LCE_E_PHASE);
      CHECK(lce195_spawn_tinted_particles(nullptr, nullptr, 0x1000000, 0, 64, 0, 1, 0) == LCE_E_INVALID);
      CHECK(lce195_spawn_tinted_particles(nullptr, nullptr, 0, 0, 64, 0, 0, 0) == LCE_E_INVALID);
      CHECK(lce195_spawn_tinted_particles(nullptr, nullptr, 0, 0, 64, 0, 1001, 0) == LCE_E_INVALID);
      CHECK(lce195_spawn_tinted_particles(nullptr, nullptr, 0, 0, 64, 0, 1, 64.1) == LCE_E_INVALID);
      CHECK(lce195_spawn_tinted_particles(nullptr, nullptr, 0, 0, 64, 0, 1, -1) == LCE_E_INVALID);
      CHECK(lce195_spawn_tinted_particles(nullptr, nullptr, 0, std::numeric_limits<double>::quiet_NaN(), 64, 0, 1, 0) == LCE_E_INVALID);
      put<uintptr_t>(tint_global, t31);
      CHECK(lce195_spawn_tinted_particles(nullptr, nullptr, 0, 0, 64, 0, 1, 0) == LCE_E_BACKEND);
      put<uintptr_t>(tint_global, 0);
      CHECK(lce195_spawn_tinted_particles(nullptr, nullptr, 0, 0, 64, 0, 1, 0) == LCE_E_BACKEND);
      CHECK(g_tinted_calls == 1);
      // mod networking: ClientboundCustomPayloadPacket to one player / everyone
      { const uint8_t blob[3] = {0xab, 1, 2}; g_conn_sent = 0;
        CHECK(lce195_send_payload(nullptr, (void*)a, "ruby:sync", blob, 3) == LCE_OK && g_pl_channel == "ruby:sync" && g_pl_len == 3 && g_pl_first == 0xab);
        CHECK(g_conn_sent == a + 0x20000);
        CHECK(lce195_send_payload(nullptr, nullptr, "ruby:all", nullptr, 0) == LCE_OK && g_pl_len == 0);
        CHECK(lce195_send_payload(nullptr, nullptr, "MC|Brand", blob, 3) == LCE_E_INVALID && lce195_send_payload(nullptr, nullptr, "mc|x:y", blob, 1) == LCE_E_INVALID);
        CHECK(lce195_send_payload(nullptr, nullptr, "nocolon", blob, 1) == LCE_E_INVALID && lce195_send_payload(nullptr, nullptr, "ruby:a", nullptr, 4) == LCE_E_INVALID);
        CHECK(lce195_send_payload(nullptr, nullptr, "ruby:big", blob, 40000) == LCE_E_INVALID); }
      // item NBT on the held stack (+0x30 tag): set creates the tag; get maps Int/Double/String tags; remove
      { uintptr_t st = image + 0x1340000, inv = image + 0x1330000; put<int32_t>(st + 0x10, 1); put<uint8_t>(st + 0x38, 0); put<uintptr_t>(st + 0x30, 0);
        int32_t rev0 = 0; memcpy(&rev0, (void*)(inv + 0x138), 4);
        lce_nbt_value v{}, o{};
        CHECK(lce195_item_tag_get(nullptr, nullptr, "mana", &o) == LCE_E_NOTFOUND);           // no tag yet
        v.type = LCE_NBT_INT; v.i = 42; CHECK(lce195_item_tag_set(nullptr, nullptr, "mana", &v) == LCE_OK);
        uintptr_t tg = 0; memcpy(&tg, (void*)(st + 0x30), 8); CHECK(tg == image + 0x13c0000);
        CHECK(lce195_item_tag_get(nullptr, (void*)a, "mana", &o) == LCE_OK && o.type == LCE_NBT_INT && o.i == 42);
        v.type = LCE_NBT_DOUBLE; v.d = 0.75; CHECK(lce195_item_tag_set(nullptr, nullptr, "lce:speed", &v) == LCE_OK);
        CHECK(lce195_item_tag_get(nullptr, nullptr, "lce:speed", &o) == LCE_OK && o.type == LCE_NBT_DOUBLE && o.d == 0.75);
        v.type = LCE_NBT_STRING; snprintf(v.s, sizeof v.s, "a long owner name"); CHECK(lce195_item_tag_set(nullptr, nullptr, "owner", &v) == LCE_OK);
        CHECK(lce195_item_tag_get(nullptr, nullptr, "owner", &o) == LCE_OK && o.type == LCE_NBT_STRING && !strcmp(o.s, "a long owner name"));
        v.type = LCE_NBT_STRING; snprintf(v.s, sizeof v.s, "Bob"); CHECK(lce195_item_tag_set(nullptr, nullptr, "short", &v) == LCE_OK);
        CHECK(lce195_item_tag_get(nullptr, nullptr, "short", &o) == LCE_OK && !strcmp(o.s, "Bob"));   // inline wstring buffer
        CHECK(lce195_item_tag_remove(nullptr, nullptr, "mana") == LCE_OK && lce195_item_tag_get(nullptr, nullptr, "mana", &o) == LCE_E_NOTFOUND);
        CHECK(lce195_item_tag_remove(nullptr, nullptr, "mana") == LCE_E_NOTFOUND);
        CHECK(lce195_item_tag_set(nullptr, nullptr, "bad key!", &v) == LCE_E_INVALID && lce195_item_tag_get(nullptr, nullptr, "", &o) == LCE_E_INVALID);
        v.type = LCE_NBT_INT; v.i = (int64_t)1 << 40; CHECK(lce195_item_tag_set(nullptr, nullptr, "big", &v) == LCE_E_INVALID);
        CHECK(lce195_item_tag_get(nullptr, (void*)g_bob, "owner", &o) == LCE_E_PHASE);       // not the frame player
        int32_t rev1 = 0; memcpy(&rev1, (void*)(inv + 0x138), 4); CHECK(rev1 == rev0 + 5);     // 4 sets + 1 remove marked the inventory changed
      }
      CHECK(g_src_deleted == 1); }
    entity_nbt_tests(a);
    svc12_ran = 1;
    return 0;
}
static int svc_task(void *u, lce_event, const void *) {
    uintptr_t a = *(uintptr_t*)u;
    CHECK(lce195_give_item(nullptr, (void*)a, "m:ruby", 5, 0) == LCE_OK);
    CHECK(lce195_set_block(nullptr, nullptr, 10, 64, -3, "m:rubyblock", 2) == LCE_OK);
    CHECK(lce195_get_block(nullptr, nullptr, 1, 2, 3) == (int32_t)(5 | (3<<12)));
    CHECK(lce195_spawn_entity(nullptr, nullptr, "minecraft:zombie", 1, 64, 1) == LCE_OK);
    CHECK(lce195_send_chat(nullptr, nullptr, "hello all") == LCE_OK);
    CHECK(lce195_send_chat(nullptr, (void*)a, "just you") == LCE_OK);
    svc_ran = 1;
    return 0;
}
int main() {
    image = (uintptr_t)mmap(nullptr, 0x1480000, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    CHECK(image != (uintptr_t)MAP_FAILED);
    g_alloc_base = image + 0x1300000;

    lce195_host host{};
    host.image=image; host.thread_id=thread_id; host.read=rd; host.write_word=cas; host.is_code=is_code;
    host.view_current=current; host.view_borrow=borrow; host.now_ms=now_ms; host.log=hlog;
    CHECK(lce195_ev_init(&host, sink, nullptr) == 1);
    uint32_t caps = lce195_ev_capabilities();
    CHECK((caps & LCE_CAP_EV_TICK) && (caps & LCE_CAP_EV_PLAYER) && (caps & LCE_CAP_EV_ENTITY));
    CHECK((caps & LCE_CAP_COMMANDS) && (caps & LCE_CAP_KEYBINDS));

    const uintptr_t sp_vt = image + 0x1341168, lvl_vt = image + 0x1340d50;
    const uintptr_t server = image + 0x1200000;
    uintptr_t level = image + 0x1000000; put<uintptr_t>(level, lvl_vt); put<uint8_t>(level + 0x248, 0);
    put<uintptr_t>(image + 0x14648d0, server);
    put<uintptr_t>(server + 0x28, image + 0x1210000);   // PlayerList
    auto make_player = [&](uintptr_t obj, const char *uuid, const char *name) {
        put<uintptr_t>(obj, sp_vt); ws(obj + 0x2d8, uuid); ws(obj + 0x9a8, name); put<uintptr_t>(obj + 0x150, level);
        put<uintptr_t>(obj + 0xb60, obj + 0x20000);   // fake connection
    };
    uintptr_t alice = image + 0x1100000, bob = image + 0x1140000;
    make_player(alice, "uuid-alice", "Alice"); make_player(bob, "uuid-bob", "Bob"); g_bob = bob; put<uint8_t>(bob + 0x1fa, 0);

    auto view_for = [&](uintptr_t who) {
        return lce195_view{image, server, level, who, who + 0x1000, g_session, g_session + 1, g_tid, 0};
    };

    // ---- join + server tick (first player = host/anchor) ----
    { auto v = view_for(alice); lce195_ev_scope_tick(nullptr, &v); }
    CHECK(events.size() >= 2);
    CHECK(events[0].ev == LCE_EV_WORLD_LOAD);
    CHECK(events[1].ev == LCE_EV_PLAYER_JOIN && events[1].text == "Alice");
    bool saw_tick = false; for (auto &e : events) if (e.ev == LCE_EV_SERVER_TICK) saw_tick = true;
    CHECK(saw_tick);
    CHECK(lce195_ev_server_ticks() == 1);

    // second player joins; only the anchor drives SERVER_TICK
    events.clear();
    { auto v = view_for(bob); lce195_ev_scope_tick(nullptr, &v); }
    bool join_bob=false, tick_on_bob=false;
    for (auto &e : events) { if (e.ev==LCE_EV_PLAYER_JOIN && e.text=="Bob") join_bob=true; if (e.ev==LCE_EV_SERVER_TICK) tick_on_bob=true; }
    CHECK(join_bob && !tick_on_bob);

    // ---- explicit remove ----
    events.clear();
    lce195_ev_player_remove(bob);
    CHECK(events.size()==1 && events[0].ev==LCE_EV_PLAYER_LEAVE && events[0].text=="Bob");

    // ---- services: give/set/get/spawn require the frame; refuse outside it ----
    lce195_natives nat{}; lce195_natives_bind_rvas(&nat, image);
    // API 1.2 natives at their RVAs carry no pinned bytes in this fake image: init must unbind them
    CHECK(lce195_services_init(&nat, &(const lce195_world_ops &)lce195_world_ops{}, &(const lce195_resolver &)lce195_resolver{nullptr, res_item, res_block}) == 1);
    CHECK(!(lce195_services_capabilities2() & (LCE_CAP2_SVC_HEAL | LCE_CAP2_SVC_FIRE | LCE_CAP2_SVC_HELD_ITEM)));
    CHECK(!(lce195_services_capabilities2() & LCE_CAP2_SVC_TINTED_PARTICLES));
    lce195_services_reset_for_tests();
    nat.health_get = f_hget; nat.health_set = f_hset; nat.health_max = f_hmax; nat.is_alive = f_alive;
    nat.fire_clear = f_fire_clear; nat.stack_set_count = f_set_count; nat.inventory_set_changed = f_changed;
    nat.stack_max_damage = f_maxdmg; nat.stack_hurt = f_hurt; nat.stack_set_damage = f_setdmg; nat.inventory_add = f_inv_add;
    nat.player_attack_source = f_pas; nat.mob_attack_source = f_mas; nat.weak_lock = f_weak_lock; nat.send_particles = f_send_particles; nat.payload_ctor = f_payload_ctor; nat.alloc_array = fake_alloc;
    nat.send_tinted_particles = f_send_tinted_particles;
    nat.tag_get_or_create = f_tag_goc; nat.tag_get = f_tag_get; nat.tag_remove = f_tag_remove; nat.tag_put_int = f_put_int;
    nat.tag_put_double = f_put_double; nat.tag_put_string = f_put_string;
    nat.sb_payload_ctor = f_sb_payload_ctor; nat.client_send = f_client_send;
    nat.compound_ctor = f_compound_ctor; nat.entity_save = f_entity_save; nat.tagset_insert = f_tagset_insert; nat.tagset_erase = f_tagset_erase;
    tramp(0x388310, (void*)f_compound_delete); put<uintptr_t>(image + 0x122ede0 + 0x30, image + 0x388310); tramp(0x388300, (void*)f_src_delete); put<uintptr_t>(image + 0x124cb48 + 8, image + 0x388300);
    nat.pop_resource = f_pop; nat.item_by_id = f_item_by_id; nat.stack_ctor = f_stack_ctor; nat.stack_bind = f_stack_bind;
    tramp(0x67840,(void*)fake_ws); tramp(0xe6cfd0,(void*)fake_alloc); tramp(0xe6cfb0,(void*)fake_free);
    tramp(0xe6ce20,(void*)fake_sub); tramp(0x188220,(void*)fake_chat_ctor); tramp(0x14fb90,(void*)fake_enable_shared);
    tramp(0xcc7310,(void*)fake_broadcast); tramp(0xcbb710,(void*)fake_conn_send);
    lce195_world_ops ops{nullptr, op_give, op_set, op_get, op_spawn, op_loaded, op_explode};
    lce195_resolver rr{nullptr, res_item, res_block};
    CHECK(lce195_services_init(&nat, &ops, &rr) == 1);
    CHECK((lce195_services_capabilities2() & (LCE_CAP2_SVC_HEAL | LCE_CAP2_SVC_FIRE | LCE_CAP2_SVC_HELD_ITEM | LCE_CAP2_SVC_EXPLODE | LCE_CAP2_SVC_WORLD)) ==
          (LCE_CAP2_SVC_HEAL | LCE_CAP2_SVC_FIRE | LCE_CAP2_SVC_HELD_ITEM | LCE_CAP2_SVC_EXPLODE | LCE_CAP2_SVC_WORLD));
    CHECK(lce195_ev_capabilities2() == (LCE_CAP2_EV_BLOCK_USE | LCE_CAP2_EV_CONTEXT | LCE_CAP2_EV_ENTITY_TICK | LCE_CAP2_EV_ENTITY_INTERACT));
    { lce_api api{}; api.struct_size = sizeof api; lce195_services_fill(&api);
      CHECK(api.heal == lce195_heal && api.explode == lce195_explode && api.teleport == lce195_teleport && api.damage_entity == lce195_damage_entity &&
            api.damage_held_item == lce195_damage_held_item && api.set_motion == lce195_set_motion && (api.capabilities2 & LCE_CAP2_SVC_HEAL));
      CHECK(api.spawn_particles == lce195_spawn_particles && api.item_tag_get == lce195_item_tag_get && api.item_tag_set == lce195_item_tag_set &&
            api.item_tag_remove == lce195_item_tag_remove && api.send_payload == lce195_send_payload && (api.capabilities2 & LCE_CAP2_SVC_PAYLOAD) && (api.capabilities2 & (LCE_CAP2_SVC_PARTICLES | LCE_CAP2_SVC_ITEM_NBT)) == (LCE_CAP2_SVC_PARTICLES | LCE_CAP2_SVC_ITEM_NBT));
      CHECK((lce195_services_capabilities2() & (LCE_CAP2_SVC_ENTITY_NBT | LCE_CAP2_SVC_ENTITY_DATA)) == (LCE_CAP2_SVC_ENTITY_NBT | LCE_CAP2_SVC_ENTITY_DATA));
      CHECK(api.spawn_tinted_particles == lce195_spawn_tinted_particles && (api.capabilities2 & LCE_CAP2_SVC_TINTED_PARTICLES));
#ifdef LCE_HAVE_ENTITY_NBT_1_5
      CHECK(api.entity_nbt_get == lce195_entity_nbt_get && api.entity_data_get == lce195_entity_data_get && api.entity_data_set == lce195_entity_data_set &&
            api.entity_data_remove == lce195_entity_data_remove);
#endif
#ifdef LCE_HAVE_NET_1_6
      CHECK(api.send_to_server == lce195_send_to_server);
#endif
    }
    { lce_api old_api{}; old_api.struct_size = offsetof(lce_api, spawn_tinted_particles); lce195_services_fill(&old_api);
      CHECK(!old_api.spawn_tinted_particles && !(old_api.capabilities2 & LCE_CAP2_SVC_TINTED_PARTICLES));
      CHECK(old_api.spawn_particles == lce195_spawn_particles && (old_api.capabilities2 & LCE_CAP2_SVC_PARTICLES)); }
    CHECK(lce195_spawn_tinted_particles(nullptr, nullptr, 0, 0, 64, 0, 1, 0) == LCE_E_PHASE);
    // alice: living entity at the frame (getHealth slot), fire timer, an inventory with 5 items in hotbar slot 2
    put<uintptr_t>(sp_vt + 0x9b8, image + 0x4244d0); put<uintptr_t>(sp_vt, image + 0x380000);
    put<uint8_t>(alice + 0x1fa, 0); put<int32_t>(alice + 0x1c0, 0);
    { uintptr_t inv = image + 0x1330000, ictl = image + 0x1331000, items = image + 0x1332000, st = image + 0x1340000, sctl = image + 0x1341000;
      put<uintptr_t>(alice + 0x7d0, inv); put<uintptr_t>(alice + 0x7d8, ictl);
      put<uintptr_t>(ictl, image + 0x12aa3e8); put<int32_t>(ictl + 8, 1);
      put<uintptr_t>(inv, image + 0x126e390); put<uintptr_t>(inv + 0x100, alice); put<int32_t>(inv + 0xfc, 2); put<uintptr_t>(inv + 8, items);
      put<uintptr_t>(items + 48, st); put<uintptr_t>(items + 56, sctl); put<uintptr_t>(sctl, image + 0x1214808);
      put<uintptr_t>(image + 0x1416d98, image + 0x1350000); put<int32_t>(st + 0x10, 5); put<uint8_t>(st + 0x38, 0);
      double pos[3] = {12.7, 64.0, -3.2}; memcpy((void*)(alice + 0x170), pos, sizeof pos);
      // ServerPlayer virtuals used by API 1.2 services (host fakes behind trampolines inside the fake RX image)
      tramp(0x388000, (void*)f_player_hurt); put<uintptr_t>(sp_vt + 0x248, image + 0x388000);
      tramp(0x388100, (void*)f_teleport); put<uintptr_t>(sp_vt + 0x670, image + 0x388100);
      tramp(0x388200, (void*)f_push); put<uintptr_t>(sp_vt + 0x230, image + 0x388200);
      uintptr_t gen = image + 0x1380000; put<uintptr_t>(gen, image + 0x1236418); put<uintptr_t>(image + 0x1413b68, gen); }

    CHECK(lce195_give_item(nullptr, (void*)alice, "m:ruby", 3, 0) == LCE_E_PHASE);   // no frame

    // drive a tick and call services from inside the sink by scheduling is complex;
    // instead call the scope tick then, while the frame is torn down, confirm refusal,
    // and test services through a manual frame via a scheduled callback.
    lce195_ev_schedule(1, svc_task, &alice);
    { auto v = view_for(alice); lce195_ev_scope_tick(nullptr, &v); }   // runs tasks on the anchor
    CHECK(svc_ran == 1);
    CHECK(ops_log.size() == 3);   // give,set,spawn (get does not log)
    
    CHECK(sent_chat.size() == 2 && sent_chat[0] == "hello all" && sent_chat[1] == "just you");
    CHECK(g_broadcast_player_list != 0 && g_conn_sent == alice + 0x20000);
    CHECK(g_last_chat == "just you");
    lce195_ev_schedule(1, svc12_task, &alice);
    { auto v = view_for(alice); lce195_ev_scope_tick(nullptr, &v); }
    CHECK(svc12_ran == 1);
    CHECK(lce195_heal(nullptr, (void*)alice, 1000) == LCE_E_PHASE);   // outside a frame
    // give_item without a world-op: the GiveItemCommand path (Inventory::add per max-size stack, the rest dropped)
    { lce195_services_reset_for_tests();
      lce195_world_ops ops2{nullptr, nullptr, op_set, op_get, op_spawn, op_loaded, op_explode};
      CHECK(lce195_services_init(&nat, &ops2, &rr) == 1);
      put<int32_t>(image + 0x1360000 + 0x18, 16);                          // Item max stack 16
      put<uintptr_t>(image + 0x1417850, image + 0x1390000);                // air != our item
      static uintptr_t who; who = alice;
      static int gave = 0;
      lce195_ev_schedule(1, +[](void *, lce_event, const void *) -> int {
          CHECK(lce195_give_item(nullptr, (void*)who, "m:ruby", 20, 0) == LCE_OK); gave = 1; return 0; }, nullptr);
      { auto v = view_for(alice); lce195_ev_scope_tick(nullptr, &v); }
      CHECK(gave == 1);
      CHECK(added.size() == 2 && added[0] == 10 && added[1] == 0);        // 16 then 4: room for 10 only
      CHECK(dropped_rest == 2);                                             // both remainders dropped at the player's feet
    }


    // ---- item use thunk ----
    events.clear();
    { uintptr_t item = image + 0x1180000; put<uintptr_t>(item, image + 0x1250000);
      CHECK(lce195_ev_route_item(item, 2300, image + 0x3aedb0) == 1);
      // original Item::use returns the holder unchanged; stamp a fake at 0x3aedb0
      static auto use_orig = +[](void *o, void *, void *, const lce195_pair *, int) -> void * { return o; };
      tramp(0x3aedb0, (void*)(void*(*)(void*,void*,void*,const lce195_pair*,int))use_orig);
      lce195_pair pp{alice, alice + 0x1000};
      void *holder = (void*)(image + 0x1260000);
      CHECK(lce195_ev_item_use_thunk(holder, (void*)item, (void*)level, &pp, 0) == holder);
      bool used=false; for (auto &e:events) if (e.ev==LCE_EV_ITEM_USE && e.id==2300 && e.level==(void*)level && e.x==100 && e.y==70 && e.z==-21) used=true;   // after the teleport test
      CHECK(used);
      // API 1.9: a throwable's +0x40 (projectiles195 p195_item_use, a host function outside the fake image) routes only once allowed
      static int alt_calls = 0;
      static auto alt_use = +[](void *o, void *, void *, const lce195_pair *, int) -> void * { ++alt_calls; return o; };
      uintptr_t item2 = image + 0x1182000; put<uintptr_t>(item2, image + 0x1250000);
      const uintptr_t alt = (uintptr_t)(void*(*)(void*,void*,void*,const lce195_pair*,int))alt_use;
      CHECK(lce195_ev_route_item(item2, 2301, alt) == 0);
      lce195_ev_allow_item_use_original(alt);
      CHECK(lce195_ev_route_item(item2, 2301, alt) == 1);
      events.clear();
      CHECK(lce195_ev_item_use_thunk(holder, (void*)item2, (void*)level, &pp, 0) == holder && alt_calls == 1);
      bool used2=false; for (auto &e:events) if (e.ev==LCE_EV_ITEM_USE && e.id==2301) used2=true;
      CHECK(used2);
      lce195_ev_allow_item_use_original(0);
    }

    // ---- block break thunk ----
    events.clear();
    { uintptr_t block = image + 0x11c0000; put<uintptr_t>(block, image + 0x1270000);
      CHECK(lce195_ev_route_block(block, 253, image + 0x94800) == 1);
      static auto wd_orig = +[](uintptr_t, uintptr_t, uintptr_t, uintptr_t, const lce195_pair *) -> int { return 0; };
      tramp(0x94800, (void*)(int(*)(uintptr_t,uintptr_t,uintptr_t,uintptr_t,const lce195_pair*))wd_orig);
      int32_t pos[3] = {4, 70, -9}; uintptr_t ppos = image + 0x12a0000; memcpy((void*)ppos, pos, sizeof pos);
      lce195_pair pp{alice, alice + 0x1000};
      lce195_ev_block_will_destroy_thunk(block, level, ppos, image + 0x12b0000, &pp);
      bool bk=false; for (auto &e:events) if (e.ev==LCE_EV_BLOCK_BREAK && e.id==253 && e.x==4 && e.y==70 && e.z==-9) bk=true;
      CHECK(bk);
    }

    // ---- block placement thunk: native +0x28 call, neighbour and replaceable cell ----
    events.clear();
    { struct Direction { uintptr_t opaque; int32_t index; uint8_t pad[0x50]; int32_t step[3]; };
      static_assert(offsetof(Direction, index) == 8 && offsetof(Direction, step) == 0x5c);
      static Direction east{0, 5, {}, {1, 0, 0}};
      static bool replace, blocked, expected_test_only;
      static unsigned calls;
      static const int32_t clicked[3] = {3, 68, -11};
      static int32_t placed[3];
      static uintptr_t block = image + 0x11c1000, item = image + 0x1184000, state = image + 0x12b2000;
      const uintptr_t state_vt = image + 0x1272000;
      put<uintptr_t>(state, state_vt);
      put<uintptr_t>(state_vt + 0x170, image + 0x3f28c0);
      put<uintptr_t>(lvl_vt + 0x38, image + 0x3f2820);
      static auto get_block = +[](uintptr_t s) -> uintptr_t { CHECK(s == state); return block; };
      static auto get_state = +[](void *, const int32_t *pos) -> uintptr_t {
          return !memcmp(pos, placed, sizeof placed) ? state : 0;
      };
      static auto native_use = +[](void *it, const lce195_pair *player, void *lvl, const int32_t *pos,
                                   int hand, const void *direction, float x, float y, float z, bool test_only) -> int {
          ++calls;
          CHECK((uintptr_t)it == item && player->object == image + 0x1100000 && (uintptr_t)lvl == image + 0x1000000);
          CHECK(pos == clicked && hand == 1 && x == 0.25f && y == 0.5f && z == 0.75f);
          CHECK(direction == &east && test_only == expected_test_only);
          if (blocked) return 2;
          if (!test_only) {
              const auto *face = static_cast<const Direction *>(direction);
              for (int i = 0; i < 3; ++i) placed[i] = pos[i] + (replace ? 0 : face->step[i]);
          }
          return 0;
      };
      tramp(0x3f28c0, (const void *)get_block);
      tramp(0x3f2820, (const void *)get_state);
      tramp(0x7d4c0, (const void *)native_use);
      CHECK(lce195_ev_route_block_item(item, 255, block, image + 0x7d4c0) == 1);
      CHECK((uintptr_t)&east > UINT32_MAX);                   // regression needs nonzero high address bits
      lce195_pair pp{alice, alice + 0x1000};
      auto use = [&](bool test_only) {
          return lce195_ev_block_item_use_on_thunk((void *)item, &pp, (void *)level, clicked, 1, &east,
                                                0.25f, 0.5f, 0.75f, test_only);
      };
      CHECK(use(false) == 0 && calls == 1 && events.size() == 1);
      CHECK(events[0].ev == LCE_EV_BLOCK_PLACE && events[0].id == 255 && events[0].text == "framed");
      CHECK(events[0].player == (void *)alice && events[0].level == (void *)level && events[0].hand == 5);
      CHECK(events[0].x == 4 && events[0].y == clicked[1] && events[0].z == clicked[2]);
      replace = true;
      CHECK(use(false) == 0 && calls == 2 && events.size() == 2 && events[1].x == clicked[0]);
      expected_test_only = true;
      CHECK(use(true) == 0 && calls == 3 && events.size() == 2);  // the earlier block is still there
      blocked = true;
      expected_test_only = false;
      CHECK(use(false) == 2 && calls == 4 && events.size() == 2);
      blocked = false;
      put<uint8_t>(level + 0x248, 1);
      CHECK(use(false) == 0 && calls == 5 && events.size() == 2);
      put<uint8_t>(level + 0x248, 0);
    }

    // ---- block use thunk (API 1.2): Block +0x2c0, stock 0x940b0 returns false ----
    events.clear();
    { uintptr_t block = image + 0x11e0000; put<uintptr_t>(block, image + 0x1270000);
      static int orig_calls = 0;
      static auto use_orig = +[](uintptr_t, uintptr_t, uintptr_t, uintptr_t, const lce195_pair *, uintptr_t, uintptr_t, uintptr_t, uintptr_t) -> uint64_t { ++orig_calls; return 0; };
      tramp(0x940b0, (void*)(uint64_t(*)(uintptr_t,uintptr_t,uintptr_t,uintptr_t,const lce195_pair*,uintptr_t,uintptr_t,uintptr_t,uintptr_t))use_orig);
      CHECK(lce195_ev_route_block_use(block, 253, image + 0x940b0) == 1);
      CHECK(lce195_ev_route_block_use(block, 253, image + 0x940b0) == 0);   // routed once
      int32_t pos[3] = {7, 65, 2}; uintptr_t ppos = image + 0x12a1000; memcpy((void*)ppos, pos, sizeof pos);
      lce195_pair pp{alice, alice + 0x1000};
      handle_use = 0;
      CHECK(lce195_ev_block_use_thunk(block, level, ppos, image + 0x12b0000, &pp, 0, 0, 0, 0) == 0 && orig_calls == 1);
      handle_use = 1;
      CHECK(lce195_ev_block_use_thunk(block, level, ppos, image + 0x12b0000, &pp, 0, 0, 0, 0) == 1 && orig_calls == 1);   // handled
      CHECK(lce195_ev_block_use_thunk(block, level, ppos, image + 0x12b0000, &pp, 0, 0, 1, 0) == 0 && orig_calls == 2);   // simulate: no event
      handle_use = 0;
      int n = 0; for (auto &e:events) if (e.ev==LCE_EV_BLOCK_USE && e.id==253 && e.x==7 && e.y==65 && e.z==2 && e.level==(void*)level) ++n;
      CHECK(n == 2);
      // storage block: DispenserBlock::use is the original; an unhandled click reaches it (opens the container)
      uintptr_t chest = image + 0x11e2000; put<uintptr_t>(chest, image + 0x1270000);
      static int disp_calls = 0;
      static auto disp_use = +[](uintptr_t, uintptr_t, uintptr_t, uintptr_t, const lce195_pair *, uintptr_t, uintptr_t, uintptr_t, uintptr_t) -> uint64_t { ++disp_calls; return 1; };
      tramp(0x2085a0, (void*)(uint64_t(*)(uintptr_t,uintptr_t,uintptr_t,uintptr_t,const lce195_pair*,uintptr_t,uintptr_t,uintptr_t,uintptr_t))disp_use);
      CHECK(lce195_ev_route_block_use(chest, 254, image + 0x2085a0) == 1);
      CHECK(lce195_ev_block_use_thunk(chest, level, ppos, image + 0x12b0000, &pp, 0, 0, 0, 0) == 1 && disp_calls == 1 && orig_calls == 2);
      handle_use = 1;
      CHECK(lce195_ev_block_use_thunk(chest, level, ppos, image + 0x12b0000, &pp, 0, 0, 0, 0) == 1 && disp_calls == 1);   // mod handled: no screen
      handle_use = 0;
    }

    // ---- API 1.3 block callbacks: tick +0x1f0, neighbour +0x208, stepOn +0x2c8, entityInside +0x328; Item +0xc8 ----
    events.clear();
    { uintptr_t block = image + 0x11e4000, bvt = image + 0x1271000; put<uintptr_t>(block, bvt);
      static int calls[5];
      static auto t_orig = +[](uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t) { ++calls[0]; };
      static auto n_orig = +[](uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t) { ++calls[1]; };
      static auto s_orig = +[](uintptr_t, uintptr_t, uintptr_t, const lce195_pair *) { ++calls[2]; };
      static auto i_orig = +[](uintptr_t, uintptr_t, uintptr_t, uintptr_t, const lce195_pair *) { ++calls[3]; };
      static auto v_orig = +[](uintptr_t, const lce195_pair *, uintptr_t, const lce195_pair *, int, int) { ++calls[4]; };
      tramp(0x936c0, (void*)(void(*)(uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t))t_orig);
      tramp(0x936f0, (void*)(void(*)(uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t))n_orig);
      tramp(0x940c0, (void*)(void(*)(uintptr_t,uintptr_t,uintptr_t,const lce195_pair*))s_orig);
      tramp(0x94180, (void*)(void(*)(uintptr_t,uintptr_t,uintptr_t,uintptr_t,const lce195_pair*))i_orig);
      tramp(0x3af000, (void*)(void(*)(uintptr_t,const lce195_pair*,uintptr_t,const lce195_pair*,int,int))v_orig);
      CHECK(lce195_ev_set_block_random_ticking(block, 1) == 0);                 // tick not routed yet
      CHECK(lce195_ev_route_block_tick(block, 253, image + 0x936c0) == 1 && lce195_ev_route_block_tick(block, 253, image + 0x936c0) == 0);
      CHECK(lce195_ev_route_block_tick(block + 8, 253, 0x1234) == 0);          // not stock / not code
      CHECK(lce195_ev_route_block_neighbor(block, 253, image + 0x936f0) == 1);
      CHECK(lce195_ev_route_block_step_on(block, 253, image + 0x940c0) == 1);
      CHECK(lce195_ev_route_block_inside(block, 253, image + 0x94180) == 1);
      // random-tick flag: byte +0x149 inside the aligned word +0x148, only behind the stock isTicking getter (+0x140)
      put<uintptr_t>(bvt + 0x140, 0x1234); put<uint64_t>(block + 0x148, 0x1122334455660077ull);
      CHECK(lce195_ev_set_block_random_ticking(block, 1) == 0);
      put<uintptr_t>(bvt + 0x140, image + 0x92aa0);
      CHECK(lce195_ev_set_block_random_ticking(block, 1) == 1);
      uint64_t w = 0; memcpy(&w, (void*)(block + 0x148), 8); CHECK(w == 0x1122334455660177ull);
      CHECK(lce195_ev_set_block_random_ticking(block, 0) == 1); memcpy(&w, (void*)(block + 0x148), 8); CHECK(w == 0x1122334455660077ull);
      int32_t pos[3] = {3, 66, -8}, up[3] = {3, 67, -8}, west[3] = {2, 66, -8}, far[3] = {9, 66, -8};
      uintptr_t ppos = image + 0x12a2000, pup = ppos + 0x20, pwest = ppos + 0x40, pfar = ppos + 0x60;
      for (int i = 0; i < 3; ++i) { put<int32_t>(ppos + 4*i, pos[i]); put<int32_t>(pup + 4*i, up[i]); put<int32_t>(pwest + 4*i, west[i]); put<int32_t>(pfar + 4*i, far[i]); }
      lce195_ev_block_tick_thunk(block, level, ppos, image + 0x12b0000, image + 0x1370000);
      lce195_ev_block_neighbor_thunk(block, image + 0x12b0000, level, ppos, image + 0x11c0000, pup);
      lce195_ev_block_neighbor_thunk(block, image + 0x12b0000, level, ppos, image + 0x11c0000, pwest);
      lce195_ev_block_neighbor_thunk(block, image + 0x12b0000, level, ppos, image + 0x11c0000, pfar);
      lce195_pair ap{alice, alice + 0x1000}, bp{bob, bob + 0x1000};
      lce195_ev_block_step_on_thunk(block, level, ppos, &ap);
      lce195_ev_block_inside_thunk(block, image + 0x12b0000, level, ppos, &bp);
      lce195_ev_block_tick_thunk(block, image + 0x1001000, ppos, 0, 0);       // not a server level: original only
      CHECK(calls[0] == 2 && calls[1] == 3 && calls[2] == 1 && calls[3] == 1);
      int ticks = 0, faces = 0; bool step = false, inside = false;
      for (auto &e : events) {
          if (e.ev == LCE_EV_BLOCK_TICK && e.id == 253 && e.x == 3 && e.y == 66 && e.z == -8 && !e.player && e.level == (void*)level) ++ticks;
          if (e.ev == LCE_EV_BLOCK_NEIGHBOR) faces = faces * 10 + (int)e.hand + 2;   // up=1, west=4, far=-1 -> 3,6,1
          if (e.ev == LCE_EV_BLOCK_STEP_ON && e.player == (void*)alice && e.text == "framed") step = true;
          if (e.ev == LCE_EV_BLOCK_ENTITY_INSIDE && e.player == (void*)bob && e.text == "framed") inside = true;
      }
      CHECK(ticks == 1 && faces == 361 && step && inside);
      // inventory tick: aux = slot, hand 0 = selected, 2 = not held
      uintptr_t item = image + 0x1184000; put<uintptr_t>(item, image + 0x1250000);
      CHECK(lce195_ev_route_item_inventory_tick(item, 2300, image + 0x3af000) == 1);
      lce195_pair sp{image + 0x1340000, image + 0x1341000};
      events.clear();
      lce195_ev_item_inventory_tick_thunk(item, &sp, level, &ap, 5, 1);
      lce195_ev_item_inventory_tick_thunk(item, &sp, level, &ap, 30, 0);
      CHECK(calls[4] == 2 && events.size() == 2);
      CHECK(events[0].ev == LCE_EV_ITEM_INVENTORY_TICK && events[0].id == 2300 && events[0].amount == 5 && events[0].hand == 0 &&
            events[0].player == (void*)alice && events[0].text == "framed");
      CHECK(events[1].amount == 30 && events[1].hand == 2);
      const uint32_t c2 = lce195_ev_capabilities2();
      CHECK((c2 & (LCE_CAP2_EV_BLOCK_TICK | LCE_CAP2_EV_NEIGHBOR | LCE_CAP2_EV_BLOCK_COLLIDE | LCE_CAP2_EV_INVENTORY_TICK)) ==
            (LCE_CAP2_EV_BLOCK_TICK | LCE_CAP2_EV_NEIGHBOR | LCE_CAP2_EV_BLOCK_COLLIDE | LCE_CAP2_EV_INVENTORY_TICK));
      lce195_ev_stats st{}; lce195_ev_get_stats(&st);
      CHECK(st.block_ticks == 1 && st.neighbors == 3 && st.step_ons == 1 && st.insides == 1 && st.inventory_ticks == 2);
    }

    // ---- entity damage/death on a tracked (non-player) entity ----
    events.clear();
    { // Build a fake LivingEntity vtable with enough code slots and hurt/die at +0x248/+0xa00.
      uintptr_t evt = image + 0x400000;   // a vtable region inside RX
      for (uint32_t i = 0; i < 600; ++i) put<uintptr_t>(evt + 8*i, image + 0x380000 + i);  // in-RX code ptrs (past +0xe50)
      put<uintptr_t>(evt - 16, 0); put<uintptr_t>(evt - 8, image + 0x390000);              // offset-to-top / typeinfo
      static auto hurt_orig = +[](void *, void *, float) -> int { return 1; };
      static auto die_orig = +[](void *, void *) {};
      // Originals must live in the RX image; trampoline to the host fakes.
      tramp(0x386000, (void*)(int(*)(void*,void*,float))hurt_orig);
      tramp(0x387000, (void*)(void(*)(void*,void*))die_orig);
      put<uintptr_t>(evt + 0x248, image + 0x386000);
      put<uintptr_t>(evt + 0xa00, image + 0x387000);
      static int ticks = 0, interacts = 0;
      static auto tick_orig = +[](void *) { ++ticks; };
      static auto int_orig = +[](void *, const lce195_pair *, uintptr_t, uintptr_t, uintptr_t, uintptr_t) -> uint64_t { ++interacts; return 0; };
      tramp(0x387100, (void*)(void(*)(void*))tick_orig); put<uintptr_t>(evt + 0xa8, image + 0x387100);
      tramp(0x387200, (void*)(uint64_t(*)(void*,const lce195_pair*,uintptr_t,uintptr_t,uintptr_t,uintptr_t))int_orig);
      put<uintptr_t>(evt + 0xe50, image + 0x387200);
      uintptr_t mob = image + 0x1240000; put<uintptr_t>(mob, evt); put<uintptr_t>(mob + 0x150, level);
      put<uint8_t>(mob + 0x604, 0);
      // must be called on the server thread; a prior scope tick established it
      { auto v = view_for(alice); lce195_ev_scope_tick(nullptr, &v); }
      CHECK(lce195_ev_track_entity(mob) == 1);
      uintptr_t newvt = 0; rd(nullptr, mob, &newvt, 8); CHECK(newvt != evt);   // vptr swapped to a private copy
      cancel_damage = 1;
      CHECK(lce195_ev_entity_hurt_thunk((void*)mob, nullptr, 5.0f) == 0);     // cancelled
      cancel_damage = 0;
      CHECK(lce195_ev_entity_hurt_thunk((void*)mob, nullptr, 2.5f) == 1);     // original ran
      lce195_ev_entity_tick_thunk((void*)mob);
      CHECK(ticks == 1);
      lce195_pair ip{alice, alice + 0x1000};
      CHECK(lce195_ev_entity_interact_thunk((void*)mob, &ip, 0, 0, 0, 0) == 0 && interacts == 1);
      bool saw_tick=false, saw_int=false;
      for (auto &e:events) { if (e.ev==LCE_EV_ENTITY_TICK) saw_tick=true; if (e.ev==LCE_EV_ENTITY_INTERACT) saw_int=true; }
      CHECK(saw_tick && saw_int);
      // API 1.9: lce195_ev_level_call runs the callback framed (level-only) on the anchor's server level
      { static int ran = 0, framed = 0, has_player = 1;
        static auto fn = +[](void *u) { ++*(int *)u; const lce195_frame *f = lce195_ev_frame(); framed = f && f->active; has_player = f ? (int)f->has_player : 1; };
        CHECK(lce195_ev_level_call(level, fn, &ran) == 1 && ran == 1 && framed == 1 && has_player == 0);
        CHECK(lce195_ev_frame() == nullptr || !lce195_ev_frame()->active);   // frame popped
        CHECK(lce195_ev_level_call(level + 8, fn, &ran) == 0 && ran == 1);   // not a server level
        CHECK(lce195_ev_level_call(level, nullptr, &ran) == 0); }
      lce195_ev_entity_die_thunk((void*)mob, nullptr);
      bool dmg=false, dead=false;
      for (auto &e:events) { if (e.ev==LCE_EV_ENTITY_DAMAGE) dmg=true; if (e.ev==LCE_EV_ENTITY_DEATH) dead=true; }
      CHECK(dmg && dead);
    }

    // ---- mod networking receive: listener +0x340 routed once a channel is registered ----
    { static int stock_calls = 0;
      static auto stock = +[](void *, const lce195_pair *) { ++stock_calls; };
      tramp(0x389000, (void*)(void(*)(void*, const lce195_pair*))stock);
      uintptr_t lvt = image + 0x13408c8, lis = alice + 0x20000;
      for (uint32_t i = 0; i < 120; ++i) put<uintptr_t>(lvt + 8*i, image + 0x380000 + i);
      put<uintptr_t>(lvt + 0x340, image + 0x389000); put<uintptr_t>(lvt - 16, 0); put<uintptr_t>(lvt - 8, image + 0x390000);
      put<uintptr_t>(lis, lvt); put<uintptr_t>(lis + 0x78, alice);
      CHECK(lce195_net_register_channel("ruby:click") == 1 && lce195_net_register_channel("ruby:click") == 0 && lce195_net_register_channel("MC|Beacon") == 0);
      events.clear();
      { auto v = view_for(alice); lce195_ev_scope_tick(nullptr, &v); }      // retained tick installs the private listener table
      uintptr_t nv = 0; rd(nullptr, lis, &nv, 8); CHECK(nv != lvt);
      CHECK(lce195_ev_route_listener(alice) == 1);                            // idempotent
      CHECK(lce195_ev_capabilities2() & LCE_CAP2_EV_PAYLOAD);
      uintptr_t pk = image + 0x13d0000, dat = image + 0x13d1000;
      put<uintptr_t>(pk, image + 0x12bb4c0); ws(pk + 0x28, "ruby:click"); put<uintptr_t>(pk + 0x50, dat); put<int32_t>(pk + 0x58, 2);
      put<uint8_t>(dat, 7); put<uint8_t>(dat + 1, 9);
      lce195_pair pp{pk, 0};
      // dispatch through the swapped table, like the packet's handle 0x5fbfc0 does (vt +0x340)
      uintptr_t slot = 0; rd(nullptr, nv + 0x340, &slot, 8);
      ((void (*)(void *, const lce195_pair *))slot)((void*)lis, &pp);
      CHECK(stock_calls == 0);
      bool got = false; for (auto &e : events) if (e.ev == LCE_EV_CUSTOM_PAYLOAD && e.text == "ruby:click" && e.amount == 2 && e.id == 7 && e.player == (void*)alice) got = true;
      CHECK(got);
      ws(pk + 0x28, "MC|BEdit");
      ((void (*)(void *, const lce195_pair *))slot)((void*)lis, &pp);
      CHECK(stock_calls == 1);                                                // vanilla channel -> stock handler
      lce195_ev_stats st{}; lce195_ev_get_stats(&st); CHECK(st.payloads == 1 && st.listeners >= 1);
    }

    // ---- client-side networking (API 1.6): packet factory map hook, private packet vtables, client payload + pump ----
    { const uintptr_t map = image + 0x142dcb0, head = image + 0x13e0000, n1 = head + 0x40, n2 = head + 0x80, n3 = head + 0xc0;
      put<uintptr_t>(map + 8, head); put<uintptr_t>(head, n1); put<uintptr_t>(n1, n2); put<uintptr_t>(n2, n3); put<uintptr_t>(n3, head);
      put<int32_t>(n1 + 0x10, 3); put<uintptr_t>(n1 + 0x18, image + 0x388400);                 // unrelated packet
      put<int32_t>(n2 + 0x10, 250); put<uintptr_t>(n2 + 0x18, image + 0x514e40);
      put<int32_t>(n3 + 0x10, 4); put<uintptr_t>(n3 + 0x18, image + 0x511890);
      static int stock_payload = 0, stock_time = 0;
      static auto mk = [](lce195_pair *out, uintptr_t vt, uintptr_t at) { put<uintptr_t>(at, vt); out->object = at; out->control = 0; return out; };
      static auto c_payload = +[](lce195_pair *out) { static uint32_t k; return mk(out, image + 0x12287e0, image + 0x13e1000 + 0x100 * (k++ % 8)); };
      static auto c_time = +[](lce195_pair *out) { return mk(out, image + 0x122c3a0, image + 0x13e2000); };
      tramp(0x514e40, (void*)(lce195_pair*(*)(lce195_pair*))c_payload); tramp(0x511890, (void*)(lce195_pair*(*)(lce195_pair*))c_time);
      tramp(0x18bd90, (void*)(void(*)(void*, void*))+[](void *, void *) { ++stock_payload; });
      tramp(0x1a2620, (void*)(void(*)(void*, void*))+[](void *, void *) { ++stock_time; });
      for (uintptr_t vt : {image + 0x12287e0, image + 0x122c3a0}) for (uint32_t i = 0; i < 11; ++i) put<uintptr_t>(vt + 8 * i, image + 0x380000 + 8 * i);
      put<uintptr_t>(image + 0x12287e0 + 0x28, image + 0x18bd90); put<uintptr_t>(image + 0x122c3a0 + 0x28, image + 0x1a2620);
      const uintptr_t cvt = image + 0x132eb18, cl = image + 0x13e3000;
      put<uintptr_t>(cvt + 0x110, image + 0xba2a90); put<uintptr_t>(cl, cvt);
      put<int32_t>(n2 + 0x10, 251); CHECK(lce195_net_client_install() == 0); put<int32_t>(n2 + 0x10, 250);   // id 250 missing: refused
      CHECK(lce195_net_client_install() == 1 && lce195_net_client_install() == 1);
      uintptr_t cv = 0; rd(nullptr, n2 + 0x18, &cv, 8); CHECK(cv == (uintptr_t)&lce195_ev_payload_create_thunk);
      rd(nullptr, n3 + 0x18, &cv, 8); CHECK(cv == (uintptr_t)&lce195_ev_settime_create_thunk);
      rd(nullptr, n1 + 0x18, &cv, 8); CHECK(cv == image + 0x388400);                        // other packets untouched
      CHECK(lce195_ev_capabilities2() & LCE_CAP2_EV_CLIENT_PAYLOAD);
      CHECK(lce195_services_capabilities2() & LCE_CAP2_SVC_SEND_TO_SERVER);
      auto receive = [&](const char *ch, uint8_t first) {   // network thread: factory create; client thread: packet->handle(listener)
          lce195_pair out{}; CHECK(lce195_ev_payload_create_thunk(&out) == &out);
          uintptr_t vt = 0; rd(nullptr, out.object, &vt, 8); CHECK(vt != image + 0x12287e0);   // private table
          uintptr_t dat = out.object + 0x80; put<uint8_t>(dat, first);
          ws(out.object + 0x28, ch); put<uintptr_t>(out.object + 0x50, dat); put<int32_t>(out.object + 0x58, 1);
          uintptr_t h = 0; rd(nullptr, vt + 0x28, &h, 8); ((void (*)(void *, void *))h)((void*)out.object, (void*)cl);
      };
      // remote client (no server of its own ticking): delivered on the client thread, with the reply sent at once
      lce195_ev_scope_finish(nullptr, 0, 0); CHECK(!lce195_ev_host_server_active());
      events.clear(); g_client_reply = 1; g_client_sent.clear();
      receive("ruby:click", 0x42);
      bool got = false; for (auto &e : events) if (e.ev == LCE_EV_CLIENT_PAYLOAD && e.text == "ruby:click" && e.id == 0x42 && !e.player && e.hand == LCE_OK) got = true;
      CHECK(got && stock_payload == 0);
      CHECK(g_client_sent.size() == 1 && g_client_sent[0] == "ruby:click|ok" && g_client_send_listener == cl);
      g_client_reply = 0;
      receive("MC|Brand", 1); CHECK(stock_payload == 1);                                        // vanilla channel -> stock handler
      CHECK(lce195_ev_client_listener() == 0);                                                   // only inside a handler
      // send_to_server outside a handler: queued, then sent by the SetTime pump on the client thread
      CHECK(lce195_send_to_server(nullptr, "ruby:click", "q1", 2) == LCE_OK && lce195_send_to_server(nullptr, "ruby:none", "x", 1) == LCE_E_INVALID);
      CHECK(g_client_sent.size() == 1);
      { lce195_pair out{}; lce195_ev_settime_create_thunk(&out); uintptr_t vt = 0, h = 0; rd(nullptr, out.object, &vt, 8); rd(nullptr, vt + 0x28, &h, 8);
        ((void (*)(void *, void *))h)((void*)out.object, (void*)cl); }
      CHECK(stock_time == 1 && g_client_sent.size() == 2 && g_client_sent[1] == "ruby:click|q1");
      // host (its own server ticking): the client payload waits for the server tick; send_to_server loops back to its own server
      { auto v = view_for(alice); lce195_ev_scope_tick(nullptr, &v); }
      CHECK(lce195_ev_host_server_active());
      events.clear();
      receive("ruby:click", 0x55);
      CHECK(events.empty());
      CHECK(lce195_send_to_server(nullptr, "ruby:click", "lb", 2) == LCE_OK && g_client_sent.size() == 2);
      { auto v = view_for(alice); lce195_ev_scope_tick(nullptr, &v); }
      bool cp = false, lb = false;
      for (auto &e : events) { if (e.ev == LCE_EV_CLIENT_PAYLOAD && e.id == 0x55) cp = true; if (e.ev == LCE_EV_CUSTOM_PAYLOAD && e.id == 'l' && e.player == (void*)alice) lb = true; }
      CHECK(cp && lb);
      lce195_ev_stats st{}; lce195_ev_get_stats(&st); CHECK(st.client_payloads == 2 && st.client_pumps == 1 && st.loopbacks == 1 && st.client_packets == 4);
    }

    // ---- keybinds ----
    { lce_keybind_desc kb{}; kb.struct_size = sizeof kb; kb.key="m:menu"; kb.name="Menu";
      kb.default_button = LCE_PAD_L3;
      static int presses = 0;
      kb.fn = +[](void *, void *, int pressed) -> int { if (pressed) ++presses; return 0; };
      lce_keybind_desc short_kb = kb; short_kb.struct_size = offsetof(lce_keybind_desc, user);
      CHECK(lce195_keys_register(&short_kb) == 0);
      CHECK(lce195_keys_register(&kb) == 1);
      lce_keybind_desc dup = kb; CHECK(lce195_keys_register(&dup) == 0);   // same chord -> refused (one owner)
      // L3 chord = touch + L3
      uint32_t out = lce195_keys_filter(0, LCE195_PAD_TOUCH | LCE195_PAD_L3, 1, 0);
      CHECK(!(out & LCE195_PAD_L3));   // L3 hidden from the game
      // deliver on the next anchor tick
      events.clear();
      { auto v = view_for(alice); lce195_ev_scope_tick(nullptr, &v); }
      CHECK(presses == 1);
      // debug menu visible suppresses the chord
      (void)lce195_keys_filter(0, 0, 1, 0);
      uint32_t out2 = lce195_keys_filter(0, LCE195_PAD_TOUCH | LCE195_PAD_L3, 1, 1);
      CHECK(out2 == (LCE195_PAD_TOUCH | LCE195_PAD_L3));

      lce_keybind_desc hold = kb; hold.default_button = LCE_PAD_DPAD_LEFT_HOLD;
      static int holds = 0;
      hold.fn = +[](void *, void *, int pressed) -> int { if (pressed) ++holds; return 0; };
      CHECK(lce195_keys_register(&hold) == 1);
      g_now = 1000; (void)lce195_keys_filter(0, LCE195_PAD_LEFT, 1, 0);
      g_now = 500; (void)lce195_keys_filter(0, LCE195_PAD_LEFT, 1, 0);
      { auto v = view_for(alice); lce195_ev_scope_tick(nullptr, &v); }
      CHECK(holds == 0);
      g_now = 1099; (void)lce195_keys_filter(0, LCE195_PAD_LEFT, 1, 0);
      { auto v = view_for(alice); lce195_ev_scope_tick(nullptr, &v); }
      CHECK(holds == 0);
      g_now = 1100; (void)lce195_keys_filter(0, LCE195_PAD_LEFT, 1, 0);
      { auto v = view_for(alice); lce195_ev_scope_tick(nullptr, &v); }
      CHECK(holds == 1);
    }

    // ---- commands ----
    { lce_command_desc cd{}; cd.struct_size = sizeof cd; cd.name="/ruby"; cd.usage="/ruby give";
      static std::string got_arg;
      cd.fn = +[](void *, void *, int argc, const char *const *argv, char *reply, uint32_t cap) -> int {
          got_arg = argc>1?argv[1]:""; snprintf(reply, cap, "ok:%s", argc>1?argv[1]:""); return 0; };
      CHECK(lce195_cmd_register("m", &cd) == 1);
      CHECK(lce195_cmd_submit("/ruby give diamonds") == 1);
      sent_chat.clear();
      { auto v = view_for(alice); lce195_ev_scope_tick(nullptr, &v); }   // anchor drains the command line
      CHECK(got_arg == "give");
      CHECK(sent_chat.size()==1 && sent_chat[0]=="ok:give");
    }

    // ---- sounds + translate (no frame needed for id lookup) ----
    CHECK(lce195_sound_id("random.levelup") == 315);
    CHECK(lce195_sound_id("minecraft:dig.stone") >= 0);
    CHECK(lce195_sound_id("m:nope") == -1);
    { lce_sound_desc sd{}; sd.struct_size=sizeof sd; sd.key="m:ping"; sd.clone_of="random.orb"; sd.volume_milli=800; sd.pitch_milli=1500;
      char err[128]; CHECK(lce195_sound_register("m", &sd, err, sizeof err) == 1);
      lce_sound_desc bad=sd; bad.key="m:raw"; bad.clone_of=nullptr; static uint8_t pcm[2]={0,0}; bad.pcm=pcm; bad.pcm_bytes=2;
      CHECK(lce195_sound_register("m", &bad, err, sizeof err) == 0);   // PCM refused
    }
    { lce195_l10n_entry l[] = { {"en_US","m:ruby.name","Ruby"}, {"fr_FR","m:ruby.name","Rubis"} };
      CHECK(lce195_l10n_load(l, 2) == 1);
      CHECK(!strcmp(lce195_translate(nullptr,"m:ruby.name"),"Ruby"));   // no GameSettings -> en_US
      CHECK(!strcmp(lce195_l10n_from_xc(4,0),"fr_FR"));
      CHECK(!strcmp(lce195_l10n_from_xc(5,240),"es_MX"));
      CHECK(lce195_l10n_from_xc(0,0) == nullptr);
    }

    printf("events195: ALL %d CHECKS PASSED\n", g_checks);
    (void)given;
    return 0;
}
