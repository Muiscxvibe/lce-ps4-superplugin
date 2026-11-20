/* audio195: custom sound assets (real new audio) for Minecraft PS4 Edition 1.95 (CUSA00265).
 *
 * Mechanism (data writes + retail natives only, no .text):
 *   1. every mod sound becomes a real SoundEvent (0x38 bytes, operator new 0xe6cfd0, wstring name
 *      via 0x67840, +0x28 id = -1, +0x2c range 16.0f, +0x30 type 1, exactly like the stock static
 *      ctors at 0x63c540..), registered by the game's own SoundEvent::register 0x6672c0 (next id from
 *      [0x1430568] into the MappedRegistry 0x1430570, ids 523+, varint on the wire);
 *   2. the SoundEngine object ([Minecraft 0x1464780] +0x298, ctor 0x7c27a0, vtable 0x12ffc90, 22
 *      slots, RTTI 11SoundEngine) gets a private vtable copy (vptr CAS) whose +0x28 play(ev, inst,
 *      x, y, z, vol, pitch) and +0x38 playUI(ev, vol, pitch) check the SoundEvent* against the mod
 *      table and forward everything else to the stock slots 0x7c2a80 / 0x7c2c10;
 *   3. a mod event plays its 16-bit PCM WAV image (built here from the package PCM) on a pool of
 *      Miles sample handles allocated from the game's digital driver ([SoundEngine+0x240]+0x10,
 *      opened 48 kHz/16-bit/stereo at 0xdf7e02) with the statically linked Miles API:
 *      AIL_allocate_sample_handle 0xe1f970, AIL_set_sample_file 0xe1fe30 (RIFF/PCM parsed by
 *      AIL_WAV_info 0xe22110), AIL_set_sample_volume_levels 0xe20c50, AIL_set_sample_playback_rate_factor
 *      0xe205a0, AIL_start_sample 0xe20360, AIL_sample_status 0xe20f30, AIL_end_sample 0xe204b0.
 *      Gain = vol * linear distance fade (range 16 * max(vol,1), Minecraft rule) * the engine's
 *      sound volume (+0x138, 0 while +0x134 mute is set; playUI uses +0x13c), pan from listener 0
 *      (+0x154 x/y/z, +0x160/+0x168 forward x/z).
 * Every publisher returns 1 done, 0 refused before any game write, -1 partial/uncertain. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define A195_RVA_MINECRAFT        0x1464780u  /* Minecraft* global */
#define A195_MC_SOUNDENGINE       0x298u      /* Minecraft+0x298 = SoundEngine* (0xc72114) */
#define A195_RVA_SE_VTABLE        0x12ffc90u  /* SoundEngine address point (0x7c27af lea 0x12ffc80 + 0x10) */
#define A195_SE_SLOTS             22u
#define A195_SLOT_PLAY            5u          /* +0x28 -> 0x7c2a80 */
#define A195_SLOT_PLAYUI          7u          /* +0x38 -> 0x7c2c10 */
#define A195_RVA_PLAY             0x7c2a80u
#define A195_RVA_PLAYUI           0x7c2c10u
#define A195_SE_IMPL              0x240u      /* PlatformSoundEngineImpl* (play queues through its +0x20) */
#define A195_IMPL_DRIVER          0x10u       /* HDIGDRIVER (0xdf7e0a) */
#define A195_SE_MUTE              0x134u      /* u32, nonzero -> world sounds silent (0x7c1e36) */
#define A195_SE_VOLUME            0x138u      /* float world sound volume */
#define A195_SE_UI_VOLUME         0x13cu      /* float UI sound volume */
#define A195_SE_LISTENER_X        0x154u      /* floats x,y,z; forward x at +0x160, z at +0x168 (0x7c224e..) */
#define A195_SE_LISTENER_FX       0x160u
#define A195_SE_LISTENER_FZ       0x168u
#define A195_RVA_EVENT_COUNTER    0x1430568u  /* next SoundEvent id */
#define A195_RVA_EVENT_REGISTRY   0x1430570u
#define A195_EVENT_SIZE           0x38u
#define A195_MAX_SOUNDS           64u
#define A195_POOL                 8u
#define A195_MAX_PCM_BYTES        (4u << 20)  /* 4 MiB of PCM per sound (~95 s at 22.05 kHz mono) */
#define A195_SMP_PLAYING          4u          /* Miles SMP_PLAYING */

typedef struct a195_natives {
    void *(*op_new)(size_t);                                        /* 0xe6cfd0 */
    void (*wstr_assign)(void *str, const uint16_t *s, size_t n);    /* 0x67840: basic_string<wchar_t>::assign */
    void *(*event_register)(void *ev);                              /* 0x6672c0: SoundEvent::register, returns ev */
    void (*inst_mark)(void *inst, int v);                           /* 0xd2ba20, called by the stock play on inst */
    void *(*ail_allocate)(void *driver);                            /* 0xe1f970 */
    int32_t (*ail_set_file)(void *s, const void *image, int32_t block);  /* 0xe1fe30 */
    void (*ail_levels)(void *s, float left, float right);           /* 0xe20c50 */
    void (*ail_rate_factor)(void *s, float f);                      /* 0xe205a0 */
    void (*ail_start)(void *s);                                     /* 0xe20360 */
    uint32_t (*ail_status)(void *s);                                /* 0xe20f30 */
    void (*ail_end)(void *s);                                       /* 0xe204b0 */
    uintptr_t play_stock, playui_stock;                             /* 0x7c2a80, 0x7c2c10 */
} a195_natives;

typedef struct a195_memory {
    void *ctx;
    int (*read)(void *ctx, uintptr_t addr, void *out, size_t n);    /* 1 ok */
    int (*write)(void *ctx, uintptr_t addr, const void *in, size_t n);
    int (*cas)(void *ctx, uintptr_t addr, uintptr_t expect, uintptr_t desired); /* aligned 8-byte CAS, 1 ok */
} a195_memory;

typedef struct a195_spec {
    const char *key;            /* "modid:name": also the SoundEvent name */
    const uint8_t *pcm;         /* 16-bit little-endian mono PCM */
    uint32_t pcm_bytes, sample_rate;
    float volume, pitch;        /* defaults 1.0 */
} a195_spec;

typedef struct a195_sound {
    char key[64];
    uint8_t *wav; uint32_t wav_bytes;   /* RIFF image, process lifetime (Miles reads it while playing) */
    float volume, pitch;
    uintptr_t event;            /* SoundEvent* after a195_register_events */
    int32_t id;                 /* registry id, -1 before */
} a195_sound;

typedef struct a195_module {
    uintptr_t base;
    a195_natives n;
    a195_memory m;
    a195_sound sounds[A195_MAX_SOUNDS]; uint32_t count;
    uint32_t events_state;      /* 0 not run, 1 done, 2 refused, 3 partial */
    uintptr_t engine, stock_vptr;
    uintptr_t vtable[2 + A195_SE_SLOTS];   /* offset-to-top, RTTI, slots */
    uint32_t hooked;
    void *pool[A195_POOL]; uint32_t pool_next;
    uint32_t plays, forwards, failures;
    char last_error[160];
} a195_module;

/* Validation of one descriptor (used by the backend check): 1 ok, else 0 with err. */
int a195_check(const a195_spec *s, char *err, uint32_t cap);
/* Build a mono 16-bit RIFF/WAVE image of pcm into out (cap >= 44 + pcm_bytes). Returns bytes or 0. */
uint32_t a195_wav_build(const uint8_t *pcm, uint32_t pcm_bytes, uint32_t rate, uint8_t *out, uint32_t cap);
/* Copy specs, build WAV images (heap). No game memory. 1 ok, 0 refused. */
int a195_configure(a195_module *a, uintptr_t base, const a195_natives *n, const a195_memory *m, const a195_spec *s, uint32_t count);
/* Free the WAV images (only while not hooked and before register; tests and re-configuration). */
void a195_release(a195_module *a);
/* Late title phase, main thread, before any Level: construct + register one SoundEvent per sound. */
int a195_register_events(a195_module *a);
/* Same phase (after register): private SoundEngine vtable, vptr CAS. Needs Minecraft and its engine built. */
int a195_hook_engine(a195_module *a);
/* Restore the stock vptr (1 ok). */
int a195_unhook_engine(a195_module *a);
/* Registry id for a mod key (-1) and SoundEvent* (0), for services/entities. */
int32_t a195_id_of(const a195_module *a, const char *key);
uintptr_t a195_event_of(const a195_module *a, const char *key);
/* Pure maths (exported for tests): gain/pan for a source relative to the listener. */
void a195_spatial(float sx, float sy, float sz, float lx, float ly, float lz, float fx, float fz, float vol, float master,
                  float *left, float *right);
/* The thunks written into the private vtable. */
void a195_play_thunk(void *engine, void *ev, void *inst, float x, float y, float z, float vol, float pitch);
void a195_playui_thunk(void *engine, void *ev, float vol, float pitch);
/* The module the thunks use (one per process). */
void a195_set_active(a195_module *a);
/* Console binding of every native from the attested base. */
void a195_natives_bind(a195_natives *n, uintptr_t base);

#ifdef __cplusplus
}
#endif
