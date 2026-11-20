// LCE|ModIds handshake (server side), included by PlayerConnection.cpp only.
// All functions run on the server main thread (PlayerConnection::tick and
// packet handling); PlayerConnection passes its private state explicitly.
// See modloader-sdk/server/PROTOCOL.md.
#pragma once
#include <algorithm>
#include <chrono>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include "../Minecraft.World/LceModIdRegistry.h"
#include "../Minecraft.World/CustomPayloadPacket.h"

namespace lce_modids_srv {

constexpr int kHelloTimeoutTicks = 200;  // 10 s at 20 TPS
constexpr int kKickDelayTicks = 60;      // let the chat lines/RESULT arrive before the disconnect

struct Session {
    uint32_t nonce = 0;
    int startTick = 0;
    int kickAt = -1;
    bool decided = false;
    std::unique_ptr<lce_modids::Reassembler> hello;
};
inline std::unordered_map<const void *, Session> &sessions() {
    static std::unordered_map<const void *, Session> s;
    return s;
}

inline std::wstring channel() { return std::wstring(lce_modids::kChannel, lce_modids::kChannel + lce_modids::kChannelLen); }
inline std::wstring widen_ascii(const std::string &s) { return std::wstring(s.begin(), s.end()); }

template <class PC>
void send_frame(PC *pc, const std::vector<uint8_t> &payload) {
    if (payload.empty()) return;
    byteArray bytes(static_cast<unsigned>(payload.size()));
    memcpy(bytes.data, payload.data(), payload.size());
    auto packet = std::make_shared<CustomPayloadPacket>(channel(), bytes);
    // Whole-frame delivery (see LceDebugEngine.h): the PS4 plugin matches
    // [250][LCE|ModIds] frames exactly and keeps them from the retail client.
    packet->shouldDelay = true;
    pc->queueSend(packet);
}

template <class PC, class Player>
void decide(PC *pc, const void *conn, Player &player, Session &s, int tickCount, uint8_t verdict, uint16_t code,
            const std::string &message) {
    Registry &r = registry();
    s.decided = true;
    s.hello.reset();
    {
        std::lock_guard<std::mutex> lock(r.m);
        for (const auto &frame : lce_modids::encode_result(s.nonce, verdict, code, message, r.ledger)) send_frame(pc, frame);
    }
    fprintf(stderr, "[modids] %ls: %s code=%u: %s\n", player->getName().c_str(), verdict == lce_modids::kAccept ? "accept" : "REFUSE",
            (unsigned)code, message.c_str());
    fflush(stderr);
    if (verdict != lce_modids::kAccept) {
        gate_forget(conn);
        for (const std::string &line : lce_modids::chat_lines("[Mods] " + message)) player->sendMessage(widen_ascii(line));
        s.kickAt = tickCount + kKickDelayTicks;
    }
}

// PlayerConnection::tick, after connection->tick() (done == false).
template <class PC, class Player>
void tick(PC *pc, const void *conn, Player &player, int tickCount) {
    auto &all = sessions();
    auto it = all.find(pc);
    if (it == all.end()) {
        static std::mt19937 rng(static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
        Session s;
        s.nonce = rng() | 1u;
        s.startTick = tickCount;
        s.hello.reset(new lce_modids::Reassembler());
        Registry &r = registry();
        std::vector<uint8_t> request;
        {
            std::lock_guard<std::mutex> lock(r.m);
            request = lce_modids::encode_request(s.nonce, r.ledger, r.policy);
        }
        all.emplace(pc, std::move(s));
        send_frame(pc, request);
        return;
    }
    Session &s = it->second;
    if (s.kickAt >= 0) {
        if (tickCount >= s.kickAt) { s.kickAt = -1; pc->disconnect(DisconnectPacket::eDisconnect_Kicked); }
        return;
    }
    if (s.decided || tickCount - s.startTick < kHelloTimeoutTicks) return;
    // No (complete) HELLO. A world without mod IDs needs nothing; with item-only
    // mod IDs and allow-missing-items the client stays, with mod stacks hidden.
    Registry &r = registry();
    bool needs = false, placeable = false;
    {
        std::lock_guard<std::mutex> lock(r.m);
        needs = !r.ledger.entries.empty();
        for (const Entry &e : r.ledger.entries) placeable |= lce_modids::block_placeable(e);
        if (needs && r.policy.allow_missing_items && !placeable) needs = false;
    }
    if (!needs) { s.decided = true; s.hello.reset(); return; }
    decide(pc, conn, player, s, tickCount, lce_modids::kReject, lce_modids::kTimeout,
           "Refused to protect your items: this world uses mod content and your console did not report its mod IDs. "
           "Install the LCE mod loader with the same mods, or play offline.");
}

// Top of PlayerConnection::handleCustomPayload. Returns true if the packet was
// an LCE|ModIds message (handled here).
template <class PC, class Player>
bool handle_payload(PC *pc, const void *conn, Player &player, const shared_ptr<CustomPayloadPacket> &packet, int tickCount) {
    if (!packet || channel().compare(packet->identifier) != 0) return false;
    auto it = sessions().find(pc);
    if (it == sessions().end() || it->second.decided || !it->second.hello) return true;  // unsolicited / late: ignore
    Session &s = it->second;
    size_t n = 0;
    if (packet->data.data && packet->length > 0) n = static_cast<size_t>(packet->length) < packet->data.length ? static_cast<size_t>(packet->length) : packet->data.length;
    auto st = s.hello->feed(reinterpret_cast<const uint8_t *>(packet->data.data), n);
    if (st == lce_modids::Reassembler::kNeedMore) return true;
    if (st == lce_modids::Reassembler::kError) {
        fprintf(stderr, "[modids] bad HELLO: %s\n", s.hello->detail.c_str());
        decide(pc, conn, player, s, tickCount, lce_modids::kReject, s.hello->error,
               "Refused: your console sent an unreadable mod ID table (" + s.hello->detail + ").");
        return true;
    }
    if (s.hello->session != s.nonce) {
        decide(pc, conn, player, s, tickCount, lce_modids::kReject, lce_modids::kMalformed, "Refused: mod ID handshake session mismatch.");
        return true;
    }
    Registry &r = registry();
    lce_modids::MergeResult m;
    std::string err;
    bool persisted = true;
    {
        std::lock_guard<std::mutex> lock(r.m);
        if (!r.ready) {
            m.verdict = lce_modids::kReject; m.code = lce_modids::kServerLedgerBad;
            m.message = "Refused: this server's mod ID table is not available.";
        } else {
            m = lce_modids::merge(r.ledger, s.hello->entries, r.policy, builtin_conflict);
            if (m.verdict == lce_modids::kAccept) persisted = adopt(m.adopt, &err);
        }
    }
    for (const std::string &line : m.log) fprintf(stderr, "[modids] %ls: %s\n", player->getName().c_str(), line.c_str());
    if (!persisted) {
        fprintf(stderr, "[modids] ledger write failed: %s\n", err.c_str());
        decide(pc, conn, player, s, tickCount, lce_modids::kReject, lce_modids::kServerLedgerBad,
               "Refused: the server could not record this world's mod IDs.");
        return true;
    }
    if (m.verdict == lce_modids::kAccept) {
        gate_set(conn, m.allowed);
        // Stacks withheld before the handshake (inventory, open container) are resent now.
        player->refreshContainer(player->inventoryMenu);
        if (player->containerMenu && player->containerMenu != player->inventoryMenu) player->refreshContainer(player->containerMenu);
        if (!m.adopt.empty() || m.missing_items)
            for (const std::string &line : lce_modids::chat_lines("[Mods] " + m.message)) player->sendMessage(widen_ascii(line));
    }
    decide(pc, conn, player, s, tickCount, m.verdict, m.code, m.message);
    return true;
}

// ~PlayerConnection (before the Connection is deleted).
inline void forget(const void *pc, const void *conn) {
    sessions().erase(pc);
    gate_forget(conn);
}

}  // namespace lce_modids_srv
