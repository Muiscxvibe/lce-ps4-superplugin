// projectiles195 host test: fake image (vtables at their real RVAs), recording natives that behave like
// the stock SnowballItem::use (build a Snowball, call its vt +0x860 through the patched slot).
#include "../projectiles195.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

static int g_checks, g_fail;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_fail; fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); } } while (0)

static const uintptr_t LO = 0x1200000u, HI = 0x1400000u;
static std::vector<uint8_t> g_img(HI - LO);
static uintptr_t B;   // fake base: B + LO == &g_img[0]
static uintptr_t *W(uintptr_t rva) { return (uintptr_t *)(B + rva); }
static uintptr_t code(uintptr_t i) { return B + 0x100000 + i * 8; }   // distinct fake code pointers

static int m_read(void *, uintptr_t a, void *o, size_t n) { if (!a) return 0; memcpy(o, (void *)a, n); return 1; }
static int m_cas(void *, uintptr_t a, uint64_t e, uint64_t d) { return __atomic_compare_exchange_n((uint64_t *)a, &e, d, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST) ? 1 : 0; }
static int g_ro_rc = 1, g_ro_calls;
static int m_ro(void *c, uintptr_t a, uint64_t e, uint64_t d) { ++g_ro_calls; return g_ro_rc == 1 ? m_cas(c, a, e, d) : g_ro_rc; }
static uint64_t g_tid = 7;
static uint64_t m_tid(void *) { return g_tid; }

// fake game objects (a static arena: process-lifetime like the game heap)
static uint8_t g_arena[1 << 16]; static size_t g_used;
static void *arena(size_t n) { void *p = g_arena + g_used; g_used += (n + 15) & ~(size_t)15; return g_used <= sizeof g_arena ? p : nullptr; }
struct Level { uint8_t pad[0x248]; uint8_t client; };
static Level g_server, g_client;
static uint8_t *new_snowball(Level *lv) {
    uint8_t *s = (uint8_t *)arena(0x4c0);
    *(uintptr_t *)s = B + P195_RVA_SNOWBALL_VT;
    *(uintptr_t *)(s + P195_F_LEVEL) = (uintptr_t)lv;
    double pos[3] = {1.5, 64.0, -3.25};
    memcpy(s + P195_F_POS, pos, sizeof pos);
    return s;
}
static uint8_t *g_last_snowball;
static int g_use_calls, g_shoot_calls, g_onhit_calls;
static float g_shot_speed, g_shot_inacc;
static uintptr_t g_shot_vptr;
static int g_switch_tid;   // simulate a shoot that runs on another thread than the use
static void *n_use(void *sret, void *, void *level, void *, int) {
    ++g_use_calls;
    if (g_switch_tid) ++g_tid;
    Level *lv = (Level *)level;
    if (lv->client) return sret;                 // the stock use throws only on the server
    g_last_snowball = new_snowball(lv);
    typedef void (*shoot_fn)(void *, void *, float, float, float, float, float);
    shoot_fn f = (shoot_fn)(*(uintptr_t **)g_last_snowball)[P195_SLOT_SHOOT / 8];   // through the (patched) vtable
    if ((uintptr_t)f == B + P195_RVA_SHOOT) { ++g_shoot_calls; g_shot_speed = 1.5f; return sret; }   // unpatched: stock
    f(g_last_snowball, nullptr, 10.f, 20.f, 0.f, 1.5f, 1.0f);
    return sret;
}
static void n_shoot(void *self, void *, float, float, float, float speed, float inacc) {
    ++g_shoot_calls; g_shot_speed = speed; g_shot_inacc = inacc; g_shot_vptr = *(uintptr_t *)self;
}
static uint8_t n_on_hit(void *, void *) { ++g_onhit_calls; return 1; }

static int g_hits; static uint32_t g_hit_kind; static void *g_hit_entity; static double g_hit_y;
static void on_hit_cb(void *ctx, uint32_t k, void *, void *e, double, double y, double) { CHECK(ctx == &g_hits); ++g_hits; g_hit_kind = k; g_hit_entity = e; g_hit_y = y; }

static void build_image() {
    B = (uintptr_t)g_img.data() - LO;
    uintptr_t *item = W(P195_RVA_ITEM_VT), *snow = W(P195_RVA_SNOWBALLITEM_VT), *ball = W(P195_RVA_SNOWBALL_VT);
    item[-2] = 0; item[-1] = code(9000);
    for (unsigned i = 0; i < P195_ITEM_SLOTS; ++i) item[i] = code(i);
    item[P195_SLOT_USE / 8] = B + P195_RVA_ITEM_USE;
    memcpy(snow - 2, item - 2, (2 + P195_ITEM_SLOTS) * 8);
    snow[-1] = code(9001); snow[2] = code(700); snow[3] = code(701); snow[4] = code(702);   // class id, destructors
    snow[P195_SLOT_USE / 8] = B + P195_RVA_SNOWBALLITEM_USE;
    ball[-2] = 0; ball[-1] = code(9002);
    for (unsigned i = 0; i < P195_SNOWBALL_SLOTS; ++i) ball[i] = code(1000 + i);
    ball[P195_SLOT_SHOOT / 8] = B + P195_RVA_SHOOT; ball[P195_SLOT_GETOWNER / 8] = B + P195_RVA_GETOWNER; ball[P195_SLOT_ONHIT / 8] = B + P195_RVA_ONHIT;
    ball[P195_SNOWBALL_SLOTS] = (uintptr_t)(intptr_t)P195_SECONDARY_OTT;
}
// an SDK generic item: object whose vptr is a private copy of Item's with the name slot replaced
static uintptr_t sdk_item_vt[3][2 + P195_ITEM_SLOTS];
static uintptr_t sdk_item(int i, uintptr_t name) {
    memcpy(sdk_item_vt[i], W(P195_RVA_ITEM_VT) - 2, sizeof sdk_item_vt[i]);
    sdk_item_vt[i][2 + P195_SLOT_NAME / 8] = name;
    uintptr_t *o = (uintptr_t *)arena(0xa0);
    o[0] = (uintptr_t)&sdk_item_vt[i][2];
    return (uintptr_t)o;
}

int main() {
    build_image();
    g_client.client = 1;
    p195_natives n = {n_use, n_shoot, n_on_hit};
    p195_memory mem = {nullptr, m_read, m_cas, m_ro, m_tid};
    p195_kind kinds[2] = {};
    snprintf(kinds[0].key, sizeof kinds[0].key, "ruby:ruby_shard"); kinds[0].speed = 2.5f; kinds[0].inaccuracy = 0.25f;
    snprintf(kinds[1].key, sizeof kinds[1].key, "ruby:pebble");       // stock speed/inaccuracy
    static p195_module M, M2;
    // ---- configure refusals
    p195_natives bad = n; bad.shoot = nullptr;
    CHECK(p195_configure(&M, B, &bad, &mem, kinds, 2, on_hit_cb, &g_hits) == 0);
    p195_kind fast = kinds[0]; fast.speed = 9.f;
    CHECK(p195_configure(&M, B, &n, &mem, &fast, 1, on_hit_cb, &g_hits) == 0);
    CHECK(p195_configure(&M, B, &n, &mem, kinds, 0, on_hit_cb, &g_hits) == 0);
    CHECK(p195_configure(&M, B, &n, &mem, kinds, 2, on_hit_cb, &g_hits) == 1);
    CHECK(M.kinds[1].speed == P195_STOCK_SPEED && M.kinds[1].inaccuracy == P195_STOCK_INACCURACY);
    CHECK(p195_configure(&M2, B, &n, &mem, kinds, 2, on_hit_cb, &g_hits) == 0);   // one per process
    // ---- adopt the throwable items
    CHECK(p195_install(&M) == 0);                                   // items first
    uintptr_t it0 = sdk_item(0, code(5000)), it1 = sdk_item(1, code(5001)), it2 = sdk_item(2, code(5002));
    sdk_item_vt[2][2 + P195_SLOT_USE / 8] = code(6000);              // use already routed elsewhere -> refused
    CHECK(p195_adopt_item(&M, 1, it2) == 0);
    CHECK(*(uintptr_t *)it2 == (uintptr_t)&sdk_item_vt[2][2]);
    uintptr_t raw = (uintptr_t)arena(0xa0); *(uintptr_t *)raw = B + P195_RVA_ITEM_VT;   // stock vtable: refused
    CHECK(p195_adopt_item(&M, 1, raw) == 0);
    CHECK(p195_adopt_item(&M, 0, it0) == 1);
    CHECK(p195_adopt_item(&M, 0, it0) == 0);                         // once
    CHECK(p195_adopt_item(&M, 1, it1) == 1);
    const uintptr_t *v0 = *(uintptr_t **)it0;
    CHECK(v0 == &M.item_vt[0][2]);
    CHECK(v0[-1] == code(9001) && v0[2] == code(700));               // SnowballItem RTTI and class id
    CHECK(v0[P195_SLOT_NAME / 8] == code(5000));                     // the SDK name dispatcher survives
    CHECK(v0[P195_SLOT_USE / 8] == (uintptr_t)&p195_item_use);
    CHECK(v0[1] == code(1));
    // ---- install: pins, RELRO slot
    W(P195_RVA_SNOWBALL_VT)[P195_SLOT_ONHIT / 8] = code(1);
    CHECK(p195_install(&M) == 0 && g_ro_calls == 0);                  // pin refused, nothing written
    W(P195_RVA_SNOWBALL_VT)[P195_SLOT_ONHIT / 8] = B + P195_RVA_ONHIT;
    g_ro_rc = 0; CHECK(p195_install(&M) == 0); g_ro_rc = 1;          // raced
    CHECK(p195_install(&M) == 1 && M.state == P195_ARMED);
    CHECK(W(P195_RVA_SNOWBALL_VT)[P195_SLOT_SHOOT / 8] == (uintptr_t)&p195_shoot_hook);
    CHECK(M.proj_vt[1][1] == code(9002) && M.proj_vt[1][2 + 5] == code(1005));
    CHECK(M.proj_vt[1][2 + P195_SLOT_ONHIT / 8] == (uintptr_t)&p195_on_hit);
    CHECK(M.proj_vt[1][2 + P195_SLOT_SHOOT / 8] == B + P195_RVA_SHOOT);   // copied before the patch: no recursion
    // ---- throw kind 0 on the server: the new Snowball gets the kind vtable and the kind speed
    uint8_t ret[0x20];
    typedef void *(*use_fn)(void *, void *, void *, void *, int);
    use_fn use0 = (use_fn)v0[P195_SLOT_USE / 8];
    CHECK(use0(ret, (void *)it0, &g_server, nullptr, 0) == ret);
    CHECK(g_use_calls == 1 && g_shoot_calls == 1);
    CHECK(g_shot_vptr == (uintptr_t)&M.proj_vt[0][2] && *(uintptr_t *)g_last_snowball == (uintptr_t)&M.proj_vt[0][2]);
    CHECK(g_shot_speed == 2.5f && g_shot_inacc == 0.25f);
    uint8_t *ball0 = g_last_snowball;
    // a plain snowball (no pending kind): untouched, stock speed
    uint8_t *plain = new_snowball(&g_server);
    p195_shoot_hook(plain, nullptr, 0, 0, 0, 1.5f, 1.0f);
    CHECK(*(uintptr_t *)plain == B + P195_RVA_SNOWBALL_VT && g_shot_speed == 1.5f && g_shoot_calls == 2);
    // kind 1 (stock speed)
    use_fn use1 = (use_fn)(*(uintptr_t **)it1)[P195_SLOT_USE / 8];
    CHECK(use1(ret, (void *)it1, &g_server, nullptr, 0) == ret);
    CHECK(*(uintptr_t *)g_last_snowball == (uintptr_t)&M.proj_vt[1][2] && g_shot_speed == 1.5f && g_shot_inacc == 1.0f);
    uint8_t *ball1 = g_last_snowball;
    // the shoot arrives on another thread than the throw: not swapped (a plain snowball)
    g_switch_tid = 1;
    CHECK(use1(ret, (void *)it1, &g_server, nullptr, 0) == ret);
    g_switch_tid = 0;
    CHECK(*(uintptr_t *)g_last_snowball == B + P195_RVA_SNOWBALL_VT && M.foreign_shoots == 0);
    // client-side use: nothing thrown, nothing pending afterwards
    CHECK(use1(ret, (void *)it1, &g_client, nullptr, 0) == ret);
    p195_shoot_hook(plain, nullptr, 0, 0, 0, 1.5f, 1.0f);
    CHECK(*(uintptr_t *)plain == B + P195_RVA_SNOWBALL_VT);
    // ---- on hit
    uint8_t hit[0x40] = {0}; int zombie = 42;
    *(uintptr_t *)(hit + P195_F_HIT_ENTITY) = (uintptr_t)&zombie;
    CHECK(p195_on_hit(ball0, hit) == 1 && g_onhit_calls == 1);
    CHECK(g_hits == 1 && g_hit_kind == 0 && g_hit_entity == &zombie && g_hit_y == 64.0);
    uint8_t block_hit[0x40] = {0};
    CHECK(p195_on_hit(ball1, block_hit) == 1);
    CHECK(g_hits == 2 && g_hit_kind == 1 && g_hit_entity == nullptr);   // a block hit is reported too
    CHECK(p195_on_hit(plain, hit) == 1 && g_hits == 2);                 // plain snowball: stock only
    *(uintptr_t *)(ball0 + P195_F_LEVEL) = (uintptr_t)&g_client;
    CHECK(p195_on_hit(ball0, hit) == 1 && g_hits == 2 && g_onhit_calls == 4);   // client copy: stock only
    CHECK(M.throws == 4 && M.swaps == 2 && M.hits == 2);
    // ---- restore
    CHECK(p195_restore(&M) == 1);
    CHECK(W(P195_RVA_SNOWBALL_VT)[P195_SLOT_SHOOT / 8] == B + P195_RVA_SHOOT && M.state == P195_READY);
    CHECK(p195_restore(&M) == 0);
    // ---- p195_bind_item (the composite path: items195 built the item as a SnowballItem copy)
    {
        g_used = 0; g_ro_calls = 0;
        static p195_module M3;
        g_tid = 7;
        // a fresh process image: the restore above put the shoot slot back; rebuild so install pins pass
        build_image();
        // M is still the process module (one per process): reuse it through a new configure is refused,
        // so exercise bind on M after resetting its items (install state READY after restore)
        CHECK(p195_configure(&M3, B, &n, &mem, kinds, 2, on_hit_cb, &g_hits) == 0);   // one per process
        M.items[0] = M.items[1] = 0;
        static uintptr_t snow_copy[2][2 + P195_ITEM_SLOTS];
        uintptr_t objs[2];
        for (int i = 0; i < 2; ++i) {
            memcpy(snow_copy[i], W(P195_RVA_SNOWBALLITEM_VT) - 2, sizeof snow_copy[i]);
            snow_copy[i][2 + P195_SLOT_NAME / 8] = code(5100 + i);
            snow_copy[i][2 + P195_SLOT_USE / 8] = i ? code(6100) : (uintptr_t)&p195_item_use;   // kind 1: an event route over it
            uintptr_t *o = (uintptr_t *)arena(0xa0); o[0] = (uintptr_t)&snow_copy[i][2]; objs[i] = (uintptr_t)o;
        }
        uintptr_t stock = (uintptr_t)arena(0xa0); *(uintptr_t *)stock = B + P195_RVA_SNOWBALLITEM_VT;
        CHECK(p195_bind_item(&M, 0, stock) == 0);                                     // stock table
        snow_copy[0][2 + P195_SLOT_USE / 8] = B + P195_RVA_SNOWBALLITEM_USE;
        CHECK(p195_bind_item(&M, 0, objs[0]) == 0);                                   // use not ours
        snow_copy[0][2 + P195_SLOT_USE / 8] = (uintptr_t)&p195_item_use;
        snow_copy[0][2 + 3] = code(1);
        CHECK(p195_bind_item(&M, 0, objs[0]) == 0);                                   // destructor slot differs
        snow_copy[0][2 + 3] = W(P195_RVA_SNOWBALLITEM_VT)[3];
        snow_copy[0][1] = code(9000);
        CHECK(p195_bind_item(&M, 0, objs[0]) == 0);                                   // Item RTTI: not a SnowballItem copy
        snow_copy[0][1] = W(P195_RVA_SNOWBALLITEM_VT)[-1];
        snow_copy[0][2 + 0xc8 / 8] = code(6200);                                      // an inventoryTick route is fine
        uintptr_t before = snow_copy[0][2 + P195_SLOT_USE / 8];
        CHECK(p195_bind_item(&M, 0, objs[0]) == 1);
        CHECK(p195_bind_item(&M, 1, objs[0]) == 0);                                   // same object twice
        CHECK(p195_bind_item(&M, 1, objs[1]) == 1);
        CHECK(p195_bind_item(&M, 1, objs[1]) == 0);                                   // once
        CHECK(*(uintptr_t *)objs[0] == (uintptr_t)&snow_copy[0][2] && snow_copy[0][2 + P195_SLOT_USE / 8] == before);   // no write
        CHECK(p195_install(&M) == 1);
        const int shots = g_shoot_calls;
        CHECK(p195_item_use(ret, (void *)objs[0], &g_server, nullptr, 0) == ret);
        CHECK(g_shoot_calls == shots + 1 && *(uintptr_t *)g_last_snowball == (uintptr_t)&M.proj_vt[0][2] && g_shot_speed == 2.5f);
        CHECK(p195_item_use(ret, (void *)objs[1], &g_server, nullptr, 0) == ret);   // what the event route calls last
        CHECK(*(uintptr_t *)g_last_snowball == (uintptr_t)&M.proj_vt[1][2]);
        CHECK(p195_restore(&M) == 1);
    }
    printf("test_projectiles195: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
