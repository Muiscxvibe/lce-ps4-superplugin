// Mod channel relay policy for the PC server (API 1.6 client networking). STL only, host-tested
// (tests/test_modchannel.cpp). The PC server runs no mod code, so a mod channel message from one PS4
// client is relayed to every other connected client: client-side mods on several PS4s can exchange
// messages through the server ("hub"). Vanilla, LCE| and MC| channels are never touched.
#pragma once
#include <stdint.h>
#include <algorithm>
#include <string>
#include <unordered_map>

namespace lce_modch {

constexpr size_t kMaxPayload = 32000;      // the SDK's send_to_server / send_payload limit
constexpr uint32_t kBurstMessages = 64;    // token bucket per connection
constexpr uint32_t kMessagesPerSecond = 32;
constexpr size_t kBurstBytes = 256 * 1024;
constexpr size_t kBytesPerSecond = 128 * 1024;

// Same rules as events195 lce195_net_channel_ok: "modid:name", 3..20 of [a-z0-9_:.-], not "mc|"/"lce|".
inline bool channel_ok(const std::wstring &c) {
    if (c.size() < 3 || c.size() > 20 || c.find(L':') == std::wstring::npos) return false;
    for (wchar_t x : c)
        if (!((x >= L'a' && x <= L'z') || (x >= L'0' && x <= L'9') || x == L'_' || x == L':' || x == L'.' || x == L'-')) return false;
    return c.compare(0, 3, L"mc|") != 0 && c.compare(0, 4, L"lce|") != 0;
}

struct Bucket { double messages = kBurstMessages, bytes = kBurstBytes; double last_ms = -1; };

// Per-connection rate limit; returns true if a message of `len` bytes may be relayed at time `now_ms`.
inline bool admit(Bucket &b, size_t len, double now_ms) {
    if (b.last_ms >= 0 && now_ms > b.last_ms) {
        const double dt = (now_ms - b.last_ms) / 1000.0;
        b.messages = std::min<double>(kBurstMessages, b.messages + dt * kMessagesPerSecond);
        b.bytes = std::min<double>(kBurstBytes, b.bytes + dt * kBytesPerSecond);
    }
    b.last_ms = now_ms;
    if (b.messages < 1 || b.bytes < (double)len) return false;
    b.messages -= 1; b.bytes -= (double)len;
    return true;
}

enum Verdict { kNotMod = 0, kRelay = 1, kDrop = 2 };
// Decide what to do with a client -> server custom payload: not a mod channel (pass to the vanilla handler),
// relay it to the other clients, or drop it (oversized / rate-limited; never relayed, never given to vanilla).
inline Verdict classify(const std::wstring &channel, size_t len, Bucket &b, double now_ms) {
    if (!channel_ok(channel)) return kNotMod;
    if (len > kMaxPayload) return kDrop;
    return admit(b, len, now_ms) ? kRelay : kDrop;
}

} // namespace lce_modch
