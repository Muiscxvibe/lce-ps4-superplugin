// audio195: custom audio assets through the game's own SoundEvent registry and Miles API. See audio195.h.
#include "audio195.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

a195_module *g_a;

void fail(a195_module *a, const char *msg) { snprintf(a->last_error, sizeof a->last_error, "%s", msg); }
void set_err(char *err, uint32_t cap, const char *msg) { if (err && cap) snprintf(err, cap, "%s", msg); }
void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
void put32(uint8_t *p, uint32_t v) { put16(p, v & 0xffff); put16(p + 2, v >> 16); }

bool key_ok(const char *k) {
    if (!k) return false;
    size_t n = strlen(k);
    const char *colon = strchr(k, ':');
    if (n < 3 || n >= 64 || !colon || colon == k || !colon[1] || strchr(colon + 1, ':')) return false;
    for (const char *p = k; *p; ++p)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_' || *p == '.' || *p == ':' || *p == '/' || *p == '-')) return false;
    return true;
}

int rd(const a195_module *a, uintptr_t addr, void *out, size_t n) { return addr && a->m.read && a->m.read(a->m.ctx, addr, out, n) == 1; }
template <class T> T rdv(const a195_module *a, uintptr_t addr, T fallback) { T v; return rd(a, addr, &v, sizeof v) ? v : fallback; }

a195_sound *find_event(a195_module *a, uintptr_t ev) {
    if (!a || !ev) return nullptr;
    for (uint32_t i = 0; i < a->count; ++i) if (a->sounds[i].event == ev) return &a->sounds[i];
    return nullptr;
}

void *take_handle(a195_module *a, uintptr_t driver) {
    // Reuse a handle that is not playing; otherwise steal round-robin (oldest started).
    for (uint32_t k = 0; k < A195_POOL; ++k) {
        uint32_t i = (a->pool_next + k) % A195_POOL;
        if (!a->pool[i]) {
            a->pool[i] = a->n.ail_allocate((void *)driver);
            if (!a->pool[i]) return nullptr;
            a->pool_next = (i + 1) % A195_POOL;
            return a->pool[i];
        }
        if (a->n.ail_status(a->pool[i]) != A195_SMP_PLAYING) { a->pool_next = (i + 1) % A195_POOL; return a->pool[i]; }
    }
    uint32_t i = a->pool_next;
    a->pool_next = (i + 1) % A195_POOL;
    a->n.ail_end(a->pool[i]);
    return a->pool[i];
}

void start(a195_module *a, uintptr_t engine, const a195_sound *s, float left, float right, float pitch) {
    uintptr_t impl = rdv<uintptr_t>(a, engine + A195_SE_IMPL, 0);
    uintptr_t driver = impl ? rdv<uintptr_t>(a, impl + A195_IMPL_DRIVER, 0) : 0;
    if (!driver) { ++a->failures; return; }
    void *h = take_handle(a, driver);
    if (!h || a->n.ail_set_file(h, s->wav, 0) == 0) { ++a->failures; return; }
    a->n.ail_levels(h, left, right);
    a->n.ail_rate_factor(h, pitch < 0.5f ? 0.5f : pitch > 2.0f ? 2.0f : pitch);
    a->n.ail_start(h);
    ++a->plays;
}

}  // namespace

extern "C" int a195_check(const a195_spec *s, char *err, uint32_t cap) {
    if (!s || !key_ok(s->key)) { set_err(err, cap, "audio195: sound key must be <modid>:<name> ([a-z0-9_./-], < 64)"); return 0; }
    if (!s->pcm || s->pcm_bytes < 2 || (s->pcm_bytes & 1)) { set_err(err, cap, "audio195: pcm must be 16-bit mono samples (even, non-empty)"); return 0; }
    if (s->pcm_bytes > A195_MAX_PCM_BYTES) { set_err(err, cap, "audio195: pcm larger than 4 MiB"); return 0; }
    if (s->sample_rate < 8000 || s->sample_rate > 48000) { set_err(err, cap, "audio195: sample_rate must be 8000..48000"); return 0; }
    float v = s->volume ? s->volume : 1.0f, p = s->pitch ? s->pitch : 1.0f;
    if (!(v > 0 && v <= 4) || !(p >= 0.5f && p <= 2.0f)) { set_err(err, cap, "audio195: volume 0..4, pitch 0.5..2"); return 0; }
    return 1;
}

extern "C" uint32_t a195_wav_build(const uint8_t *pcm, uint32_t pcm_bytes, uint32_t rate, uint8_t *out, uint32_t cap) {
    if (!pcm || !out || !pcm_bytes || pcm_bytes > A195_MAX_PCM_BYTES || cap < 44u + pcm_bytes || !rate) return 0;
    memcpy(out, "RIFF", 4); put32(out + 4, 36 + pcm_bytes); memcpy(out + 8, "WAVEfmt ", 8);
    put32(out + 16, 16); put16(out + 20, 1); put16(out + 22, 1); put32(out + 24, rate); put32(out + 28, rate * 2);
    put16(out + 32, 2); put16(out + 34, 16); memcpy(out + 36, "data", 4); put32(out + 40, pcm_bytes);
    memcpy(out + 44, pcm, pcm_bytes);
    return 44 + pcm_bytes;
}

extern "C" int a195_configure(a195_module *a, uintptr_t base, const a195_natives *n, const a195_memory *m, const a195_spec *s, uint32_t count) {
    if (!a) return 0;
    memset(a, 0, sizeof *a);
    a->base = base;
    if (!n || !m || !m->read || !m->cas) { fail(a, "natives/memory not bound"); return 0; }
    a->n = *n; a->m = *m;
    if (count > A195_MAX_SOUNDS) { fail(a, "more than 64 custom sounds"); return 0; }
    for (uint32_t i = 0; i < count; ++i) {
        char e[120];
        if (!a195_check(&s[i], e, sizeof e)) { snprintf(a->last_error, sizeof a->last_error, "%s: %s", s[i].key ? s[i].key : "?", e); return 0; }
        for (uint32_t j = 0; j < i; ++j) if (!strcmp(s[j].key, s[i].key)) { fail(a, "duplicate sound key"); return 0; }
    }
    for (uint32_t i = 0; i < count; ++i) {
        a195_sound &d = a->sounds[i];
        snprintf(d.key, sizeof d.key, "%s", s[i].key);
        d.wav = (uint8_t *)malloc(44 + s[i].pcm_bytes);
        if (!d.wav) { for (uint32_t j = 0; j < i; ++j) free(a->sounds[j].wav); memset(a->sounds, 0, sizeof a->sounds); fail(a, "out of memory"); return 0; }
        d.wav_bytes = a195_wav_build(s[i].pcm, s[i].pcm_bytes, s[i].sample_rate, d.wav, 44 + s[i].pcm_bytes);
        d.volume = s[i].volume ? s[i].volume : 1.0f; d.pitch = s[i].pitch ? s[i].pitch : 1.0f;
        d.id = -1;
    }
    a->count = count;
    return 1;
}

extern "C" void a195_release(a195_module *a) {
    if (!a || a->hooked || a->events_state == 1) return;
    for (uint32_t i = 0; i < a->count; ++i) { free(a->sounds[i].wav); a->sounds[i].wav = nullptr; }
    a->count = 0;
}

extern "C" int a195_register_events(a195_module *a) {
    if (!a) return 0;
    if (a->events_state) return a->events_state == 1 ? 1 : a->events_state == 2 ? 0 : -1;
    if (!a->count) { a->events_state = 1; return 1; }
    if (!a->n.op_new || !a->n.wstr_assign || !a->n.event_register) { a->events_state = 2; fail(a, "SoundEvent natives not bound"); return 0; }
    int32_t next = rdv<int32_t>(a, a->base + A195_RVA_EVENT_COUNTER, -1);
    if (next < 523 || next > 100000) { a->events_state = 2; fail(a, "SoundEvent id counter [0x1430568] unreadable or below the 523 stock events"); return 0; }
    for (uint32_t i = 0; i < a->count; ++i) {
        a195_sound &s = a->sounds[i];
        uint8_t *ev = (uint8_t *)a->n.op_new(A195_EVENT_SIZE);
        if (!ev) { a->events_state = i ? 3 : 2; fail(a, "operator new failed"); return i ? -1 : 0; }
        memset(ev, 0, A195_EVENT_SIZE);
        const uint64_t cap7 = 7;                       // empty wstring: size 0 (+0x18), capacity 7 (+0x20), as 0x63c4c0
        memcpy(ev + 0x20, &cap7, 8);
        uint16_t w[64];
        size_t n = strlen(s.key);
        for (size_t k = 0; k < n; ++k) w[k] = (uint8_t)s.key[k];
        w[n] = 0;
        a->n.wstr_assign(ev, w, n);
        const int32_t none = -1; const float range = 16.0f; const uint32_t type = 1;
        memcpy(ev + 0x28, &none, 4); memcpy(ev + 0x2c, &range, 4); memcpy(ev + 0x30, &type, 4);
        void *r = a->n.event_register(ev);
        int32_t id;
        memcpy(&id, ev + 0x28, 4);
        if (r != ev || id < 523) { a->events_state = 3; fail(a, "SoundEvent::register 0x6672c0 did not assign an id"); return -1; }
        s.event = (uintptr_t)ev; s.id = id;
    }
    a->events_state = 1;
    return 1;
}

extern "C" int a195_hook_engine(a195_module *a) {
    if (!a) return 0;
    if (a->hooked) return 1;
    if (!a->count) return 1;
    if (a->events_state != 1) { fail(a, "hook before SoundEvents were registered"); return 0; }
    uintptr_t mc = rdv<uintptr_t>(a, a->base + A195_RVA_MINECRAFT, 0);
    uintptr_t engine = mc ? rdv<uintptr_t>(a, mc + A195_MC_SOUNDENGINE, 0) : 0;
    if (!engine) { fail(a, "Minecraft or SoundEngine (+0x298) not built yet"); return 0; }
    uintptr_t vptr = rdv<uintptr_t>(a, engine, 0), stock = a->base + A195_RVA_SE_VTABLE;
    if (vptr != stock) { fail(a, "SoundEngine vptr is not the stock 0x12ffc90 (another hook?)"); return 0; }
    for (uint32_t i = 0; i < 2 + A195_SE_SLOTS; ++i)
        if (!rd(a, stock - 16 + 8 * i, &a->vtable[i], 8)) { fail(a, "SoundEngine vtable unreadable"); return 0; }
    if (a->vtable[2 + A195_SLOT_PLAY] != a->base + A195_RVA_PLAY || a->vtable[2 + A195_SLOT_PLAYUI] != a->base + A195_RVA_PLAYUI) { fail(a, "SoundEngine play/playUI slots are not the stock 0x7c2a80/0x7c2c10"); return 0; }
    a->n.play_stock = a->vtable[2 + A195_SLOT_PLAY];
    a->n.playui_stock = a->vtable[2 + A195_SLOT_PLAYUI];
    a->vtable[2 + A195_SLOT_PLAY] = (uintptr_t)&a195_play_thunk;
    a->vtable[2 + A195_SLOT_PLAYUI] = (uintptr_t)&a195_playui_thunk;
    a->engine = engine; a->stock_vptr = vptr;
    a195_set_active(a);
    if (a->m.cas(a->m.ctx, engine, vptr, (uintptr_t)&a->vtable[2]) != 1) { fail(a, "SoundEngine vptr CAS failed"); return 0; }
    a->hooked = 1;
    return 1;
}

extern "C" int a195_unhook_engine(a195_module *a) {
    if (!a || !a->hooked) return 1;
    if (a->m.cas(a->m.ctx, a->engine, (uintptr_t)&a->vtable[2], a->stock_vptr) != 1) return 0;
    a->hooked = 0;
    return 1;
}

extern "C" int32_t a195_id_of(const a195_module *a, const char *key) {
    for (uint32_t i = 0; a && key && i < a->count; ++i) if (!strcmp(a->sounds[i].key, key)) return a->sounds[i].id;
    return -1;
}
extern "C" uintptr_t a195_event_of(const a195_module *a, const char *key) {
    for (uint32_t i = 0; a && key && i < a->count; ++i) if (!strcmp(a->sounds[i].key, key)) return a->sounds[i].event;
    return 0;
}

extern "C" void a195_spatial(float sx, float sy, float sz, float lx, float ly, float lz, float fx, float fz, float vol, float master,
                             float *left, float *right) {
    float dx = sx - lx, dy = sy - ly, dz = sz - lz;
    float dist = sqrtf(dx * dx + dy * dy + dz * dz);
    float range = 16.0f * (vol > 1.0f ? vol : 1.0f);
    float fade = dist >= range ? 0.0f : 1.0f - dist / range;
    float gain = (vol > 1.0f ? 1.0f : vol) * fade * master;
    // right vector of the forward (fx, 0, fz) in Minecraft axes (x east, z south): (-fz, 0, fx)
    float pan = 0.0f, hd = sqrtf(dx * dx + dz * dz), fl = sqrtf(fx * fx + fz * fz);
    if (hd > 0.001f && fl > 0.001f) pan = 0.6f * ((dx * -fz + dz * fx) / (hd * fl));   // |pan| <= 0.6: never fully one-sided
    if (pan > 0.6f) pan = 0.6f;
    if (pan < -0.6f) pan = -0.6f;
    *left = gain * sqrtf((1.0f - pan) * 0.5f) * 1.41421356f;
    *right = gain * sqrtf((1.0f + pan) * 0.5f) * 1.41421356f;
    if (*left > 1.0f) *left = 1.0f;
    if (*right > 1.0f) *right = 1.0f;
}

extern "C" void a195_set_active(a195_module *a) { g_a = a; }

extern "C" void a195_play_thunk(void *engine, void *ev, void *inst, float x, float y, float z, float vol, float pitch) {
    a195_module *a = g_a;
    const a195_sound *s = a ? find_event(a, (uintptr_t)ev) : nullptr;
    if (!s) {
        if (a) ++a->forwards;
        typedef void (*play_fn)(void *, void *, void *, float, float, float, float, float);
        if (a && a->n.play_stock) ((play_fn)a->n.play_stock)(engine, ev, inst, x, y, z, vol, pitch);
        return;
    }
    if (inst && a->n.inst_mark) {             // same bookkeeping the stock play does on inst (0x7c2b99..0x7c2ba6)
        a->n.inst_mark(inst, 1);
        const uint32_t zero = 0;
        if (a->m.write) a->m.write(a->m.ctx, (uintptr_t)inst + 0xc, &zero, 4);
    }
    uintptr_t e = (uintptr_t)engine;
    float master = rdv<uint32_t>(a, e + A195_SE_MUTE, 1) ? 0.0f : rdv<float>(a, e + A195_SE_VOLUME, 0.0f);
    float lx = rdv<float>(a, e + A195_SE_LISTENER_X, 0), ly = rdv<float>(a, e + A195_SE_LISTENER_X + 4, 0), lz = rdv<float>(a, e + A195_SE_LISTENER_X + 8, 0);
    float fx = rdv<float>(a, e + A195_SE_LISTENER_FX, 0), fz = rdv<float>(a, e + A195_SE_LISTENER_FZ, 0);
    float left, right;
    a195_spatial(x, y, z, lx, ly, lz, fx, fz, vol * s->volume, master, &left, &right);
    if (left <= 0.0f && right <= 0.0f) return;   // out of range or muted
    start(a, e, s, left, right, pitch * s->pitch);
}

extern "C" void a195_playui_thunk(void *engine, void *ev, float vol, float pitch) {
    a195_module *a = g_a;
    const a195_sound *s = a ? find_event(a, (uintptr_t)ev) : nullptr;
    if (!s) {
        if (a) ++a->forwards;
        typedef void (*ui_fn)(void *, void *, float, float);
        if (a && a->n.playui_stock) ((ui_fn)a->n.playui_stock)(engine, ev, vol, pitch);
        return;
    }
    float g = vol * s->volume * rdv<float>(a, (uintptr_t)engine + A195_SE_UI_VOLUME, 0.0f);
    if (g > 1.0f) g = 1.0f;
    if (g <= 0.0f) return;
    start(a, (uintptr_t)engine, s, g, g, pitch * s->pitch);
}

extern "C" void a195_natives_bind(a195_natives *n, uintptr_t b) {
    memset(n, 0, sizeof *n);
    n->op_new = (void *(*)(size_t))(b + 0xe6cfd0);
    n->wstr_assign = (void (*)(void *, const uint16_t *, size_t))(b + 0x67840);
    n->event_register = (void *(*)(void *))(b + 0x6672c0);
    n->inst_mark = (void (*)(void *, int))(b + 0xd2ba20);
    n->ail_allocate = (void *(*)(void *))(b + 0xe1f970);
    n->ail_set_file = (int32_t (*)(void *, const void *, int32_t))(b + 0xe1fe30);
    n->ail_levels = (void (*)(void *, float, float))(b + 0xe20c50);
    n->ail_rate_factor = (void (*)(void *, float))(b + 0xe205a0);
    n->ail_start = (void (*)(void *))(b + 0xe20360);
    n->ail_status = (uint32_t (*)(void *))(b + 0xe20f30);
    n->ail_end = (void (*)(void *))(b + 0xe204b0);
}
