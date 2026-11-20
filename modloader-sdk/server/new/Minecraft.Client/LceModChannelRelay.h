// Mod channel relay (server side), included by PlayerConnection.cpp only; runs on the server main thread
// inside PlayerConnection::handleCustomPayload. Policy: ../Minecraft.World/LceModChannelCore.h.
#pragma once
#include <chrono>
#include <memory>
#include <string.h>
#include <unordered_map>
#include "../Minecraft.World/LceModChannelCore.h"
#include "../Minecraft.World/CustomPayloadPacket.h"

namespace lce_modch_srv {

inline std::unordered_map<const void *, lce_modch::Bucket> &buckets() {
    static std::unordered_map<const void *, lce_modch::Bucket> b;
    return b;
}
inline void forget(const void *conn) { buckets().erase(conn); }
inline double now_ms() {
    using namespace std::chrono;
    return (double)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// true = consumed (relayed or dropped); false = not a mod channel, continue with the vanilla handler.
template <class PC, class Player, class List>
bool relay(PC *self, const Player &sender, List *list, const shared_ptr<CustomPayloadPacket> &packet) {
    if (!packet) return false;
    const size_t len = packet->data.data ? (size_t)packet->data.length : 0;
    const lce_modch::Verdict v = lce_modch::classify(packet->identifier, len, buckets()[self], now_ms());
    if (v == lce_modch::kNotMod) return false;
    if (v == lce_modch::kDrop || !list) return true;
    EnterCriticalSection(&list->m_playersCS);
    for (auto &p : list->players) {
        if (!p || p == sender || !p->connection || p->connection.get() == self) continue;
        byteArray copy(static_cast<unsigned>(len));   // each packet owns its buffer
        if (len) memcpy(copy.data, packet->data.data, len);
        auto out = std::make_shared<CustomPayloadPacket>(packet->identifier, copy);
        out->shouldDelay = true;   // whole-frame delivery, as LceModIdHandshake.h send_frame
        p->connection->queueSend(out);
    }
    LeaveCriticalSection(&list->m_playersCS);
    return true;
}

} // namespace lce_modch_srv
