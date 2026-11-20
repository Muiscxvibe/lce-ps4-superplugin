#ifndef LCE_WORLDSIZE195_HPP
#define LCE_WORLDSIZE195_HPP
// "Huge" local worlds (2048 x 2048 chunks = 32768 blocks, Nether scale 8 =
// 256 x 256 Nether chunks) for Minecraft PS4 1.95 (CUSA00265).
//
// Pinned image: legacy-195-analysis.elf c675e6ce...029f, RX 231a6bfb...c682.
// All addresses are RVAs from the game base. Evidence: agent-worldsize/README.md.
//
// The retail game supports any xzSize at runtime (ServerChunkCache 0xcf95d0 and
// ClientChunkCache 0xb874e0 size their arrays from it), but five places stop a
// world past Large (320 chunks):
//   W1/W2  LevelData(CompoundTag*) clamps XZSize to [54,320]      0x4139dc/0x4139e2 imm
//   W3     LevelData(LevelSettings*) clamps to {320,8} (rodata)   0xe978d0
//   W4     BuriedTreasureFeature ctor: xz==320 sets the Large
//          placement config, anything else keeps stale values    0x1381f7 je
//   W5     CreateWorldMenu: worldSize index 0..3 -> table          0x9c8148 (relay)
//   W6     LaunchMoreOptions world-size slider max 3              0xa6abf6 imm
//   W7     LevelRenderer::getGlobalIndexForChunk core: fixed 356
//          (overworld) / 42 (Nether) chunk window, -1 outside      0xc58380 (relay)
//   D1     world-size title table entry 4 (RW .data padding)       0x1406580
// Every RX change is one 8-byte-aligned word, swapped with a single CAS, and
// each word is consistent on its own. For any world of <= 320 chunks every
// patched site computes exactly what the retail code computes (host- and
// Unicorn-tested), so the words only change behaviour for a Huge world.
// They are installed only while this console is NOT joining or joined to an
// LCE server, and are recorded with mod_gate (lce_mod_gate_record) so the
// anticheat canonical hash is unchanged while they are present.
#include <stddef.h>
#include <stdint.h>

namespace ws195 {

constexpr uint32_t kHugeChunks = 2048;   // xzSize of a Huge world (32768 blocks, +-16384)
constexpr uint32_t kHugeHellScale = 8;   // Nether = 2048/8 = 256 chunks
constexpr uint32_t kLabelSid = 0x09d0e79fu;  // stock string "Custom" (languages.loc en-EN)

enum SiteKind : uint8_t { kFixed = 0, kRenderRelay = 1, kCreateRelay = 2 };
struct Site {
    uint32_t rva;            // 8-byte aligned
    uint8_t pristine[8];     // bytes as in the pinned image
    uint8_t patched[8];      // kFixed only; relay words are computed (jmp rel32 + nops)
    SiteKind kind;
    const char *what;
};
uint64_t le64(const uint8_t b[8]);
constexpr size_t kSiteCount = 7 + 59;   // W1..W7 + 59 ambient-tick words
extern const Site kSites[kSiteCount];

// D1: RW .data, not hashed. Low dword of this word is padding (0) in retail.
constexpr uint32_t kLabelWordRva = 0x1406580;
constexpr uint64_t kLabelWordPristine = 0x6353b2e300000000ull;
constexpr uint64_t kLabelWordPatched = kLabelWordPristine | kLabelSid;

// Relay code. Returns bytes written (0 if cap too small).
constexpr size_t kRenderRelaySize = 0x97;
constexpr size_t kCreateRelaySize = 0x4e;
constexpr size_t kDiagRelaySize = 0x1e;
size_t build_render_relay(uint8_t *out, size_t cap, uintptr_t base);
// created_flag: u32 incremented each time a Huge world is created (rel32 from the relay).
size_t build_create_relay(uint8_t *out, size_t cap, uintptr_t base, uintptr_t self, uintptr_t created_flag);

// Creation-stall diagnostics: function-entry counters. Every listed entry starts with
// 55 48 89 e5 41 57 41 56; the relay does lock incq counter, replays those 8 bytes and
// jumps to entry+8. Installed only for a few minutes after a Huge world is created.
constexpr uint64_t kDiagPrologue = 0x56415741e5894855ull;
struct DiagSite { uint32_t rva; const char *name; };
constexpr size_t kDiagCount = 17;
extern const DiagSite kDiagSites[kDiagCount];
size_t build_diag_relay(uint8_t *out, size_t cap, uintptr_t self, uintptr_t counter, uintptr_t entry);
constexpr size_t kDiagRelayStride = 0x20;  // relay i at relays + i * stride
// Word with "jmp rel32 relay" in its first 5 bytes and nop in the rest.
// 0 if the relay is out of rel32 range of site.
uint64_t jmp_word(uintptr_t site, uintptr_t relay);

// Memory access used by apply/revert (console: mprotect + CAS; tests: fake).
struct Mem {
    void *ctx;
    int (*read8)(void *ctx, uintptr_t addr, uint64_t *out);               // 1 ok
    int (*cas8)(void *ctx, uintptr_t addr, uint64_t expect, uint64_t want); // 1 swapped
    // Optional: called for every RX word BEFORE it is swapped (mod_gate canonical record).
    void (*record)(void *ctx, uintptr_t addr, uint64_t pristine, uint64_t patched);
};
struct Relays { uintptr_t render, create; };

enum Result { kRefused = 0, kOk = 1, kPartial = -1 };
// Verifies every site holds pristine or its own patched value first; refuses
// (nothing written) on any other content. Then swaps the remaining words.
// A failed swap rolls back the words already swapped in this call.
int apply(uintptr_t base, const Relays &r, const Mem &m, unsigned *changed);
// Restores pristine on every word currently holding its patched value.
// Words holding anything else are left alone. kOk when all are pristine after.
int revert(uintptr_t base, const Relays &r, const Mem &m, unsigned *changed);
// Diagnostic words (independent; records each with Mem::record first).
int diag_apply(uintptr_t base, uintptr_t relays, const Mem &m, unsigned *changed);
int diag_revert(uintptr_t base, uintptr_t relays, const Mem &m, unsigned *changed);
// Patched value for site i (relay words need r).
uint64_t patched_word(const Site &s, uintptr_t base, const Relays &r);

// Install policy, driven from the plugin monitor thread (100 ms).
struct Policy {
    uint64_t first_us = 0;     // first pump
    bool armed = false;        // words believed installed
    unsigned failures = 0;     // apply refusals/partials (stop retrying after 3)
};
// Desired armed state. remote: joining or joined to an LCE server.
// disabled: /data/lce-mods/worldsize.off exists; server_off: /data/lce-mods/worldsize-server.off
// (disarm while joining/joined to an LCE server). Boot delay keeps the swap
// clear of the boot-time pristine RX checks and mod-hook snapshot/diffs.
constexpr uint64_t kBootDelayUs = 20000000ull;
bool want_armed(const Policy &p, uint64_t now_us, bool remote, bool disabled, bool server_off);

// ---- BiomeCacheLayer (RTTI 15BiomeCacheLayer, vtable 0x12162f0, ctor 0x863a0) ----
// Built by Layer::getDefaultLayers (0x3e402c) with W = xzSize*4 biome cells (+0x40) and an
// origin (+0x38/+0x3c). Its getArea (0x86620, vtable slot 4 at 0x1216320 in RELRO) fills a
// W*W byte cache of the WHOLE world from its parent on first use. Large: 1280^2 = 1.6 M
// cells; 1024: 4096^2 = 16.8 M; 2048: 8192^2 = 67 M cells, with full-resolution int arrays
// in every parent layer -> the "Finding Seed" freeze (setInitialSpawn -> findBiome 0x8e130
// -> getArea). The slot hook keeps the retail cache for a Large-sized window (1280 cells,
// +-2560 blocks) centred on the retail origin and answers requests outside it from the parent
// layer directly (serialised). Values are identical: the cache holds parent(x, z).
constexpr uint32_t kBiomeCacheSlotRva = 0x1216320;
constexpr uint32_t kBiomeCacheGetArea = 0x86620;
constexpr int32_t kBiomeCacheWindow = 1280;
constexpr uint32_t kBiomeWindowMagic = 0x57534c48u;  // 'HLSW' at +0x54 marks a shrunk (Huge) cache
struct IntArr { int32_t *data; uint32_t length; };
typedef IntArr (*GetAreaFn)(void *self, int x, int z, int w, int h);
struct CacheEnv {
    GetAreaFn original;           // retail 0x86620
    void (*lock)(void), (*unlock)(void);
};
// 1 if the layer was shrunk to the window (first call, cache not yet built).
int biome_cache_prepare(uint8_t *self);
IntArr biome_cache_get_area(void *self, int x, int z, int w, int h, const CacheEnv &env);

// Pure helpers mirroring the patched game code (tests and docs).
int clamp_xz_compound(int xz);                 // W1/W2: [54,1024]
int render_index(int dim_idx, int cx, int cy, int cz, const int sizes[2], const int offs[2]);

}  // namespace ws195

extern "C" {
// Console glue (worldsize_lce.cpp). base = game base, remote = 1 while
// joining/joined to an LCE server.
void lce_worldsize_pump(uintptr_t base, int remote);
int lce_worldsize_armed(void);
}
#endif
