/* Retail 1.95 world-start worker for local play and multiplayer. */
#include <orbis/libkernel.h>
#include <stdint.h>
#include <string.h>

#include "call_publish.h"
#include "loader/retail195/host_ps4.h"
#include "loader/retail195/oneshot195.h"

extern "C" int lce_sdk_world_entry_allowed(void);

namespace {
constexpr uintptr_t kWord = 0x945db8;
constexpr uintptr_t kRunNetworkGame = 0x943440;
constexpr uintptr_t kNetworkManager = 0x143b050;
constexpr uintptr_t kDeleteCall = 0x944cc6;
constexpr uintptr_t kFreeParams = 0xe6d080;
constexpr uintptr_t kDeleteObject = 0xe6cfb0;
constexpr uintptr_t kPageSize = 0x4000;
constexpr int kRx = 5;
constexpr unsigned char kWorkerCall[] = {
    0x48, 0x89, 0xdf, 0xc6, 0x83, 0x9c, 0x00, 0x00, 0x00, 0x01, // rdi = manager; mark worker active
    0xe8, 0x80, 0xd6, 0xff, 0xff,                               // call _RunNetworkGame
    0x84, 0xc0, 0xc6, 0x83, 0x9c, 0x00, 0x00, 0x00, 0x00,       // test return; clear worker-active flag
    0x0f, 0x85, 0x9b, 0x00, 0x00, 0x00,                         // jump on success
};
constexpr unsigned char kReleaseParams[] = {
    0x4d, 0x85, 0xff, 0x74, 0x08, 0x4c, 0x89, 0xff,              // ServerThreadProc: delete non-null loading data
    0xe8, 0xb5, 0x83, 0x52, 0x00,
};
constexpr unsigned char kAllocateSaveBytes[] = {0xe8, 0xd2, 0xc8, 0x4a, 0x00, 0x48, 0x89, 0xc3};
constexpr unsigned char kInitializeSaveData[] = {
    0x49, 0x89, 0x06, 0xb8, 0x07, 0x00, 0x00, 0x00, 0x45, 0x89, 0x6e, 0x08, 0xc4, 0xe1, 0xf9, 0x6e,
    0xc0, 0xc5, 0xf9, 0x73, 0xf8, 0x08, 0xc4, 0xc1, 0x7a, 0x7f, 0x46, 0x28, 0x66, 0x41, 0xc7, 0x46,
    0x18, 0x00, 0x00, 0xe8, 0x04, 0x70, 0x6a, 0xff,
};
constexpr unsigned char kAttachSaveData[] = {0x48, 0xc7, 0x03, 0x00, 0x00, 0x00, 0x00, 0x4c, 0x89, 0x73, 0x08};
constexpr unsigned char kFreeSaveBytes[] = {
    0x49, 0x8b, 0x47, 0x08, 0x48, 0x85, 0xc0, 0x74, 0x1a, 0x48, 0x8b, 0x38, 0xe8, 0x01, 0xb2, 0x1e, 0x00,
};
constexpr uint64_t kStockWord = UINT64_C(0xffffd680e8010000);

uintptr_t g_original;
uintptr_t g_manager;
using Release = void (*)(void *);
Release g_free, g_delete_object;
void (*g_log)(const char *);
unsigned g_denial_logged;
lce195_patch_word g_patch;

int protect(void *, void *address, size_t size, int prot) {
    return sceKernelMprotect(address, size, prot);
}

template<size_t N>
bool matches(uintptr_t address, const unsigned char (&expected)[N]) {
    unsigned char observed[N];
    return lce_ps4_mem_read(nullptr, address, observed, N) && memcmp(observed, expected, N) == 0;
}

bool guard_bytes(uintptr_t base) {
    constexpr uintptr_t kWorkerCallStart = kWord - 7;
    constexpr uintptr_t kReleaseStart = kDeleteCall - 8;
    void *lo, *hi;
    int32_t prot;
    if (sceKernelQueryMemoryProtection(reinterpret_cast<void *>(base + kWord), &lo, &hi, &prot) != 0 || prot != kRx ||
        reinterpret_cast<uintptr_t>(lo) > base + kWorkerCallStart ||
        reinterpret_cast<uintptr_t>(hi) < base + kWorkerCallStart + sizeof kWorkerCall)
        return false;
    return lce_ps4_is_code(base, base + kRunNetworkGame) && lce_ps4_is_code(base, base + kFreeParams) &&
        lce_ps4_is_code(base, base + kDeleteObject) &&
        matches(base + kWorkerCallStart, kWorkerCall) && matches(base + kReleaseStart, kReleaseParams) &&
        matches(base + 0x9c06e9, kAllocateSaveBytes) && matches(base + 0x9c09e4, kInitializeSaveData) &&
        matches(base + 0x9c0a7a, kAttachSaveData) && matches(base + 0xc81e6e, kFreeSaveBytes);
}

bool plan_call(uintptr_t base, uintptr_t relay, mc195_call_plan *out) {
    if (!relay || (relay & (kPageSize - 1))) return false;
    const uintptr_t next = base + kWord + 8;
    const uintptr_t distance = relay >= next ? relay - next : next - relay;
    const uintptr_t limit = relay >= next ? INT32_MAX : uintptr_t{INT32_MAX} + 1;
    if (distance > limit) return false;
    const uint32_t delta = relay >= next ? static_cast<uint32_t>(distance) : 0u - static_cast<uint32_t>(distance);
    const uint64_t replacement = (kStockWord & UINT32_MAX) | (uint64_t{delta} << 32);
    if (replacement == kStockWord) return false;
    *out = {base + kWord, relay, base + kRunNetworkGame, kStockWord, replacement};
    return true;
}

void release_loading_params(void *params) {
    // The local save loader transfers its byte array and copied name through +0x08.
    const auto *init = static_cast<const unsigned char *>(params);
    const unsigned char *save = nullptr;
    memcpy(&save, init + 0x08, sizeof save);
    if (save) {
        void *data = nullptr, *name = nullptr;
        uint64_t name_capacity = 0;
        memcpy(&data, save, sizeof data);
        memcpy(&name_capacity, save + 0x30, sizeof name_capacity);
        if (name_capacity >= 8) {
            memcpy(&name, save + 0x18, sizeof name);
            if (name) g_delete_object(name);
        }
        if (data) g_free(data);
        g_delete_object(const_cast<unsigned char *>(save));
    }
    g_free(params);
}

bool run_network_game(void *manager, void *params) {
    using Run = bool (*)(void *, void *);
    auto run = reinterpret_cast<Run>(__atomic_load_n(&g_original, __ATOMIC_ACQUIRE));
    if (lce_sdk_world_entry_allowed()) return run(manager, params);
    if (!__atomic_exchange_n(&g_denial_logged, 1u, __ATOMIC_RELAXED) && g_log)
        g_log("LCE_SDK WORLD_ENTRY: native world startup denied; see SDK late-startup status\n");
    // The server worker normally takes ownership of local loading data.
    // Remote joins pass nullptr. Returning false takes the native failure path.
    if (params && reinterpret_cast<uintptr_t>(manager) == g_manager) release_loading_params(params);
    return false;
}
} // namespace

// -1 means publication could have occurred before page protection failed.
extern "C" int lce195_world_entry_guard_arm(uintptr_t base, void (*log)(const char *)) {
    if (!base || base > UINTPTR_MAX - kNetworkManager || ((base + kWord) & 7u) || !guard_bytes(base)) return 0;
    constexpr intptr_t deltas[] = {0x10000000, -0x10000000, 0x20000000, -0x20000000};
    for (const intptr_t delta : deltas) {
        const uintptr_t source = base + kWord;
        if (delta < 0 ? source < uintptr_t(-delta) : source > UINTPTR_MAX - uintptr_t(delta)) continue;
        const uintptr_t hint = (source + static_cast<uintptr_t>(delta)) & ~(kPageSize - 1);
        void *page = nullptr;
        if (sceKernelMmap(reinterpret_cast<void *>(hint), kPageSize, 3, 0x1002, -1, 0, &page) != 0 || !page) continue;
        mc195_call_plan plan;
        if (!plan_call(base, reinterpret_cast<uintptr_t>(page), &plan)) {
            sceKernelMunmap(page, kPageSize);
            continue;
        }
        if (!guard_bytes(base)) {
            sceKernelMunmap(page, kPageSize);
            return 0;
        }
        g_manager = base + kNetworkManager;
        g_free = reinterpret_cast<Release>(base + kFreeParams);
        g_delete_object = reinterpret_cast<Release>(base + kDeleteObject);
        g_log = log;
        const auto result = mc195_publish_call(&plan, reinterpret_cast<uintptr_t>(page),
            reinterpret_cast<uintptr_t>(&run_network_game), &g_original, kRx, protect, nullptr);
        if (result == MC195_PUBLISH_REJECTED) {
            sceKernelMunmap(page, kPageSize);
            return 0;
        }
        // The mapping remains valid for an in-flight caller even after restoring the call.
        g_patch = {plan.word_address, plan.expected_word, plan.replacement_word};
        return result == MC195_PUBLISH_INSTALLED ? 1 : -1;
    }
    return 0;
}

extern "C" int lce195_world_entry_guard_retire(void) {
    if (!g_patch.word_address) return 1;
    return lce195_oneshot_restore(&g_patch, kRx, protect, nullptr);
}
