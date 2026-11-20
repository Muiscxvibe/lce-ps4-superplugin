// Minimal harness (same shape as modloader-sdk/tests/check.h) plus LCE|ModIds helpers.
#pragma once
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include "LceModIdWire.h"

static int g_checks, g_failures;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ(a, b) do { ++g_checks; long long _a = (long long)(a), _b = (long long)(b); if (_a != _b) { ++g_failures; fprintf(stderr, "%s:%d: CHECK_EQ failed: %s=%lld %s=%lld\n", __FILE__, __LINE__, #a, _a, #b, _b); } } while (0)
#define TEST_MAIN_END(name) do { printf("%s: %d checks, %d failures\n", name, g_checks, g_failures); return g_failures ? 1 : 0; } while (0)

namespace tk {
using lce_modids::Entry;

inline Entry item(const char *key, uint16_t id, uint8_t stack = 64, uint16_t dmg = 0, bool live = true) {
    Entry e; memset(&e, 0, sizeof e);
    lce_modids::set_key(e, key);
    e.kind = lce_modids::kItem; e.id = id; e.max_stack = stack; e.a = dmg; e.flags = live ? lce_modids::kLive : 0;
    return e;
}
inline Entry block(const char *key, uint16_t id, uint16_t hard = 150, bool live = true, bool hasItem = true) {
    Entry e; memset(&e, 0, sizeof e);
    lce_modids::set_key(e, key);
    e.kind = lce_modids::kBlock; e.id = id; e.max_stack = 64; e.a = hard; e.b = 1000;
    e.flags = (uint8_t)((live ? lce_modids::kLive : 0) | (hasItem ? lce_modids::kBlockHasItem : 0));
    return e;
}
// The exact clientbound frame the server's PC writer produces for a
// CustomPayloadPacket: [250][u16 10][UTF-16BE channel][u16 len][payload].
inline std::vector<uint8_t> clientbound(const std::vector<uint8_t> &payload, const char *channel = lce_modids::kChannel) {
    std::vector<uint8_t> f;
    size_t cl = strlen(channel);
    f.push_back(250); f.push_back((uint8_t)(cl >> 8)); f.push_back((uint8_t)cl);
    for (size_t i = 0; i < cl; ++i) { f.push_back(0); f.push_back((uint8_t)channel[i]); }
    f.push_back((uint8_t)(payload.size() >> 8)); f.push_back((uint8_t)payload.size());
    f.insert(f.end(), payload.begin(), payload.end());
    return f;
}
// Inverse of build_serverbound, as Packet::readPacket(225 -> 250) +
// CustomPayloadPacket::read see it. Returns false if the layout is wrong.
inline bool parse_serverbound(const uint8_t *p, size_t n, std::string &channel, std::vector<uint8_t> &payload) {
    if (n < 5 || p[0] != 225) return false;
    size_t cl = (size_t)p[1] << 8 | p[2];
    if (cl > 20 || n < 3 + 2 * cl + 2) return false;
    channel.clear();
    for (size_t i = 0; i < cl; ++i) channel += (char)((p[3 + 2 * i] << 8) | p[4 + 2 * i]);
    size_t at = 3 + 2 * cl, len = (size_t)p[at] << 8 | p[at + 1];
    if (at + 2 + len != n) return false;
    payload.assign(p + at + 2, p + n);
    return true;
}
// Small deterministic PRNG for fuzzing.
struct Rng { uint64_t s; uint32_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return (uint32_t)s; } };
}  // namespace tk
