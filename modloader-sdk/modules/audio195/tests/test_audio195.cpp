// Host tests for audio195 (fake natives and memory, ASan/UBSan).
#include "../audio195.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, fails;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

// ---- fake game: flat memory addressed by host pointers --------------------------
static uint8_t img[0x1500000 / 64];   // only the globals we need, remapped below
static uintptr_t kBase = 0x10000000;
struct Fake {
    uintptr_t mc_ptr = 0;                // value of [base+0x1464780]
    int32_t counter = 523;               // [base+0x1430568]
    uintptr_t vtable[2 + A195_SE_SLOTS]; // fake stock SoundEngine vtable (at base+0x12ffc90-16)
} F;
static uint8_t engine[0x300], mc[0x300], impl[0x40];
static int cas_fail;

static bool in(uintptr_t a, size_t n, const void *p, size_t sz) { return a >= (uintptr_t)p && a + n <= (uintptr_t)p + sz; }
static int mread(void *, uintptr_t a, void *out, size_t n) {
    if (a == kBase + A195_RVA_MINECRAFT && n == 8) { memcpy(out, &F.mc_ptr, 8); return 1; }
    if (a == kBase + A195_RVA_EVENT_COUNTER && n == 4) { memcpy(out, &F.counter, 4); return 1; }
    uintptr_t vt0 = kBase + A195_RVA_SE_VTABLE - 16;
    if (a >= vt0 && a + n <= vt0 + sizeof F.vtable) { memcpy(out, (uint8_t *)F.vtable + (a - vt0), n); return 1; }
    if (in(a, n, engine, sizeof engine) || in(a, n, mc, sizeof mc) || in(a, n, impl, sizeof impl)) { memcpy(out, (void *)a, n); return 1; }
    if (a > 0x100000 && a < kBase) { memcpy(out, (void *)a, n); return 1; }   // heap objects (events, inst)
    return 0;
}
static int mwrite(void *, uintptr_t a, const void *in_, size_t n) { memcpy((void *)a, in_, n); return 1; }
static int mcas(void *, uintptr_t a, uintptr_t e, uintptr_t d) {
    if (cas_fail) return 0;
    uintptr_t cur; memcpy(&cur, (void *)a, 8);
    if (cur != e) return 0;
    memcpy((void *)a, &d, 8); return 1;
}

// ---- fake natives ---------------------------------------------------------------
static uint8_t heap[16][A195_EVENT_SIZE]; static int heap_n;
static void *f_new(size_t n) { return n == A195_EVENT_SIZE && heap_n < 16 ? heap[heap_n++] : nullptr; }
static uint16_t last_name[64]; static size_t last_len;
static void f_assign(void *s, const uint16_t *w, size_t n) { memcpy(last_name, w, 2 * (n + 1)); last_len = n; uint64_t sz = n; memcpy((uint8_t *)s + 0x18, &sz, 8); }
static int reg_calls, reg_bad;
static void *f_register(void *ev) { ++reg_calls; if (reg_bad) return ev; memcpy((uint8_t *)ev + 0x28, &F.counter, 4); ++F.counter; return ev; }
static int mark_calls; static void f_mark(void *, int v) { mark_calls += v; }
struct Smp { int alloc; uint32_t status; const void *file; float l, r, rate; int started, ended; };
static Smp smp[16]; static int smp_n; static void *alloc_driver;
static void *f_alloc(void *drv) { alloc_driver = drv; return smp_n < 16 ? &smp[smp_n++] : nullptr; }
static int set_file_ok = 1;
static int32_t f_file(void *s, const void *img_, int32_t) { ((Smp *)s)->file = img_; return set_file_ok; }
static void f_levels(void *s, float l, float r) { ((Smp *)s)->l = l; ((Smp *)s)->r = r; }
static void f_rate(void *s, float f) { ((Smp *)s)->rate = f; }
static void f_start(void *s) { ((Smp *)s)->started++; ((Smp *)s)->status = A195_SMP_PLAYING; }
static uint32_t f_status(void *s) { return ((Smp *)s)->status; }
static void f_end(void *s) { ((Smp *)s)->ended++; ((Smp *)s)->status = 2; }
static int stock_play, stock_ui; static void *stock_ev;
static void s_play(void *, void *ev, void *, float, float, float, float, float) { ++stock_play; stock_ev = ev; }
static void s_ui(void *, void *ev, float, float) { ++stock_ui; stock_ev = ev; }

static a195_natives nat() {
    a195_natives n; memset(&n, 0, sizeof n);
    n.op_new = f_new; n.wstr_assign = f_assign; n.event_register = f_register; n.inst_mark = f_mark;
    n.ail_allocate = f_alloc; n.ail_set_file = f_file; n.ail_levels = f_levels; n.ail_rate_factor = f_rate;
    n.ail_start = f_start; n.ail_status = f_status; n.ail_end = f_end;
    return n;
}

static void reset_game() {
    memset(engine, 0, sizeof engine); memset(mc, 0, sizeof mc); memset(impl, 0, sizeof impl); memset(heap, 0, sizeof heap); heap_n = 0;
    memset(smp, 0, sizeof smp); smp_n = 0; F.counter = 523; reg_calls = reg_bad = 0; cas_fail = 0; set_file_ok = 1; stock_play = stock_ui = 0; mark_calls = 0;
    for (uint32_t i = 0; i < 2 + A195_SE_SLOTS; ++i) F.vtable[i] = 0x7c0000 + i;
    F.vtable[2 + A195_SLOT_PLAY] = kBase + A195_RVA_PLAY;
    F.vtable[2 + A195_SLOT_PLAYUI] = kBase + A195_RVA_PLAYUI;
    uintptr_t e = (uintptr_t)engine, m = (uintptr_t)mc, im = (uintptr_t)impl, vp = kBase + A195_RVA_SE_VTABLE;
    memcpy(engine, &vp, 8);
    memcpy(mc + A195_MC_SOUNDENGINE, &e, 8); F.mc_ptr = m;
    memcpy(engine + A195_SE_IMPL, &im, 8);
    uintptr_t drv = 0xd0d0d0; memcpy(impl + A195_IMPL_DRIVER, &drv, 8);
    float vol = 1.0f; memcpy(engine + A195_SE_VOLUME, &vol, 4); memcpy(engine + A195_SE_UI_VOLUME, &vol, 4);
    float fz = -1.0f; memcpy(engine + A195_SE_LISTENER_FZ, &fz, 4);   // facing north
}

int main() {
    (void)img;
    static uint8_t pcm[2000];
    for (int i = 0; i < 1000; ++i) { int16_t v = (int16_t)(8000 * sin(i * 0.05)); memcpy(pcm + 2 * i, &v, 2); }
    // WAV builder
    static uint8_t wav[44 + 2000];
    CHECK(a195_wav_build(pcm, 2000, 22050, wav, sizeof wav) == 2044);
    CHECK(!memcmp(wav, "RIFF", 4) && !memcmp(wav + 8, "WAVEfmt ", 8) && !memcmp(wav + 36, "data", 4));
    uint32_t v32; uint16_t v16;
    memcpy(&v32, wav + 4, 4); CHECK(v32 == 2036);
    memcpy(&v16, wav + 20, 2); CHECK(v16 == 1);
    memcpy(&v16, wav + 22, 2); CHECK(v16 == 1);
    memcpy(&v32, wav + 24, 4); CHECK(v32 == 22050);
    memcpy(&v32, wav + 28, 4); CHECK(v32 == 44100);
    memcpy(&v16, wav + 34, 2); CHECK(v16 == 16);
    memcpy(&v32, wav + 40, 4); CHECK(v32 == 2000);
    CHECK(!memcmp(wav + 44, pcm, 2000));
    if (const char *out = getenv("A195_WAV_OUT")) { FILE *f = fopen(out, "wb"); if (f) { fwrite(wav, 1, 2044, f); fclose(f); } }
    CHECK(a195_wav_build(pcm, 2000, 22050, wav, 100) == 0);
    CHECK(a195_wav_build(pcm, 0, 22050, wav, sizeof wav) == 0);

    // checks
    char err[160];
    a195_spec ok = {"mymod:bell", pcm, 2000, 22050, 1.0f, 1.0f};
    CHECK(a195_check(&ok, err, sizeof err) == 1);
    a195_spec bad = ok; bad.key = "bell"; CHECK(a195_check(&bad, err, sizeof err) == 0);
    bad = ok; bad.key = "My:Bell"; CHECK(a195_check(&bad, err, sizeof err) == 0);
    bad = ok; bad.pcm_bytes = 1999; CHECK(a195_check(&bad, err, sizeof err) == 0);
    bad = ok; bad.sample_rate = 4000; CHECK(a195_check(&bad, err, sizeof err) == 0);
    bad = ok; bad.pitch = 3.0f; CHECK(a195_check(&bad, err, sizeof err) == 0);
    bad = ok; bad.pcm = nullptr; CHECK(a195_check(&bad, err, sizeof err) == 0);

    // spatial maths
    float l, r;
    a195_spatial(0, 0, 0, 0, 0, 0, 0, -1, 1, 1, &l, &r); CHECK(fabsf(l - 1) < 1e-3f && fabsf(r - 1) < 1e-3f);
    a195_spatial(8, 0, 0, 0, 0, 0, 0, -1, 1, 1, &l, &r); CHECK(r > l && r > 0 && l > 0);       // east of a north-facing listener: right
    a195_spatial(-8, 0, 0, 0, 0, 0, 0, -1, 1, 1, &l, &r); CHECK(l > r);
    a195_spatial(0, 0, -20, 0, 0, 0, 0, -1, 1, 1, &l, &r); CHECK(l == 0 && r == 0);             // out of range 16
    a195_spatial(0, 0, -20, 0, 0, 0, 0, -1, 2, 1, &l, &r); CHECK(l > 0);                        // range 32 at vol 2
    a195_spatial(0, 0, -4, 0, 0, 0, 0, -1, 1, 0, &l, &r); CHECK(l == 0 && r == 0);              // master 0

    // configure / register / hook
    a195_natives n = nat();
    a195_memory m = {nullptr, mread, mwrite, mcas};
    static a195_module a;
    reset_game();
    a195_spec two[2] = {ok, ok}; two[1].key = "mymod:horn"; two[1].volume = 2.0f; two[1].pitch = 1.5f;
    a195_spec dup[2] = {ok, ok};
    CHECK(a195_configure(&a, kBase, &n, &m, dup, 2) == 0);
    a195_spec badone[1] = {bad};
    CHECK(a195_configure(&a, kBase, &n, &m, badone, 1) == 0);
    CHECK(a195_hook_engine(&a) == 1);   // nothing configured: nothing to do
    a195_release(&a);
    CHECK(a195_configure(&a, kBase, &n, &m, two, 2) == 1);
    CHECK(a.count == 2 && a.sounds[0].wav_bytes == 2044 && a.sounds[1].volume == 2.0f);
    CHECK(a195_hook_engine(&a) == 0);   // events first
    F.counter = 100;
    CHECK(a195_register_events(&a) == 0 && reg_calls == 0 && heap_n == 0);   // counter sanity: refused before allocating
    CHECK(a195_register_events(&a) == 0);                                     // no retry after a refusal
    a195_release(&a);
    CHECK(a195_configure(&a, kBase, &n, &m, two, 2) == 1);
    F.counter = 523;
    CHECK(a195_register_events(&a) == 1);
    CHECK(reg_calls == 2 && a.sounds[0].id == 523 && a.sounds[1].id == 524 && F.counter == 525);
    CHECK(a.sounds[0].event == (uintptr_t)heap[0] && a195_event_of(&a, "mymod:horn") == (uintptr_t)heap[1]);
    CHECK(a195_id_of(&a, "mymod:horn") == 524 && a195_id_of(&a, "x:y") == -1);
    { float range; int32_t id; uint32_t type; uint64_t cap;
      memcpy(&range, heap[0] + 0x2c, 4); memcpy(&id, heap[0] + 0x28, 4); memcpy(&type, heap[0] + 0x30, 4); memcpy(&cap, heap[0] + 0x20, 8);
      CHECK(range == 16.0f && id == 523 && type == 1 && cap == 7); }
    CHECK(last_len == 10 && last_name[0] == 'm' && last_name[5] == ':' && last_name[10] == 0);
    CHECK(a195_register_events(&a) == 1 && reg_calls == 2);   // idempotent
    // hook: wrong vptr refused
    uintptr_t other = 0x1234; memcpy(engine, &other, 8);
    CHECK(a195_hook_engine(&a) == 0 && !a.hooked);
    uintptr_t vp = kBase + A195_RVA_SE_VTABLE; memcpy(engine, &vp, 8);
    F.vtable[2 + A195_SLOT_PLAY] = 0x999;
    CHECK(a195_hook_engine(&a) == 0);
    F.vtable[2 + A195_SLOT_PLAY] = kBase + A195_RVA_PLAY;
    cas_fail = 1; CHECK(a195_hook_engine(&a) == 0 && !a.hooked); cas_fail = 0;
    CHECK(a195_hook_engine(&a) == 1 && a.hooked);
    uintptr_t now; memcpy(&now, engine, 8);
    CHECK(now == (uintptr_t)&a.vtable[2]);
    CHECK(a.vtable[0] == F.vtable[0] && a.vtable[1] == F.vtable[1] && a.vtable[2 + 3] == F.vtable[2 + 3]);
    CHECK(a.vtable[2 + A195_SLOT_PLAY] == (uintptr_t)&a195_play_thunk && a.vtable[2 + A195_SLOT_PLAYUI] == (uintptr_t)&a195_playui_thunk);
    CHECK(a195_hook_engine(&a) == 1);   // already
    // the thunks: forward non-mod events to the stock slots (fake stock functions here)
    a.n.play_stock = (uintptr_t)&s_play; a.n.playui_stock = (uintptr_t)&s_ui;
    int vanilla_ev;
    a195_play_thunk(engine, &vanilla_ev, nullptr, 1, 2, 3, 1, 1);
    CHECK(stock_play == 1 && stock_ev == &vanilla_ev && a.plays == 0);
    a195_playui_thunk(engine, &vanilla_ev, 1, 1);
    CHECK(stock_ui == 1);
    a195_play_thunk(engine, nullptr, nullptr, 1, 2, 3, 1, 1);
    CHECK(stock_play == 2);   // null forwarded: the stock code returns early itself
    // mod event: plays through Miles
    struct { uint8_t b[0x20]; } inst; memset(&inst, 0xff, sizeof inst);
    a195_play_thunk(engine, heap[0], &inst, 4, 0, 0, 1, 1);
    CHECK(stock_play == 2 && a.plays == 1 && smp_n == 1 && alloc_driver == (void *)0xd0d0d0);
    CHECK(smp[0].file == a.sounds[0].wav && smp[0].started == 1 && smp[0].r > smp[0].l && smp[0].rate == 1.0f);
    uint32_t z; memcpy(&z, inst.b + 0xc, 4); CHECK(z == 0 && mark_calls == 1);
    // pitch: descriptor pitch 1.5 * call pitch 1.2 -> clamped 1.8; volume 2 extends range
    a195_play_thunk(engine, heap[1], nullptr, 0, 0, -20, 1, 1.2f);
    CHECK(a.plays == 2 && smp_n == 2 && fabsf(smp[1].rate - 1.8f) < 1e-4f && smp[1].l > 0);
    // out of range: silent, no handle
    a195_play_thunk(engine, heap[0], nullptr, 0, 0, -100, 1, 1);
    CHECK(a.plays == 2 && smp_n == 2);
    // mute
    uint32_t mute = 1; memcpy(engine + A195_SE_MUTE, &mute, 4);
    a195_play_thunk(engine, heap[0], nullptr, 1, 0, 0, 1, 1);
    CHECK(a.plays == 2);
    mute = 0; memcpy(engine + A195_SE_MUTE, &mute, 4);
    // UI path
    a195_playui_thunk(engine, heap[0], 0.5f, 1.0f);
    CHECK(a.plays == 3 && fabsf(smp[2].l - 0.5f) < 1e-4f && smp[2].l == smp[2].r);
    // pool: 8 handles, then reuse finished ones, then steal
    for (int i = 0; i < 10; ++i) a195_play_thunk(engine, heap[0], nullptr, 1, 0, 0, 1, 1);
    CHECK(smp_n == 8 && a.plays == 13);
    int ended = 0; for (int i = 0; i < 8; ++i) ended += smp[i].ended;
    CHECK(ended == 5);   // all 8 playing: every further play stole one
    smp[3].status = 2;
    a195_play_thunk(engine, heap[0], nullptr, 1, 0, 0, 1, 1);
    CHECK(smp_n == 8 && a.plays == 14);
    // set_file failure counted, nothing started
    set_file_ok = 0; int before = 0; for (int i = 0; i < 8; ++i) before += smp[i].started;
    a195_play_thunk(engine, heap[0], nullptr, 1, 0, 0, 1, 1);
    int after = 0; for (int i = 0; i < 8; ++i) after += smp[i].started;
    CHECK(after == before && a.failures == 1); set_file_ok = 1;
    // no driver: counted failure
    uintptr_t zero = 0; memcpy(impl + A195_IMPL_DRIVER, &zero, 8);
    a195_play_thunk(engine, heap[0], nullptr, 1, 0, 0, 1, 1);
    CHECK(a.failures == 2);
    // unhook restores the stock vptr
    CHECK(a195_unhook_engine(&a) == 1); memcpy(&now, engine, 8); CHECK(now == kBase + A195_RVA_SE_VTABLE && !a.hooked);
    CHECK(a195_unhook_engine(&a) == 1);
    // registration that does not assign an id -> uncertain
    reset_game();
    static a195_module b;
    CHECK(a195_configure(&b, kBase, &n, &m, two, 2) == 1);
    reg_bad = 1;
    CHECK(a195_register_events(&b) == -1 && b.events_state == 3);
    CHECK(a195_register_events(&b) == -1);
    // missing natives
    a195_natives none = n; none.event_register = nullptr;
    static a195_module c;
    CHECK(a195_configure(&c, kBase, &none, &m, two, 1) == 1 && a195_register_events(&c) == 0);
    for (uint32_t i = 0; i < a.count; ++i) free(a.sounds[i].wav);
    for (uint32_t i = 0; i < b.count; ++i) free(b.sounds[i].wav);
    for (uint32_t i = 0; i < c.count; ++i) free(c.sounds[i].wav);
    printf("test_audio195: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
