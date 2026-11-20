// Host tests for the PC-server mod channel relay (policy + the real glue template on fake engine types).
#include <stdio.h>
#include <vector>
#include "../new/Minecraft.Client/LceModChannelRelay.h"
static int checks, fails;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

struct Conn { std::vector<shared_ptr<CustomPayloadPacket>> sent; void queueSend(shared_ptr<Packet> p) { sent.push_back(std::static_pointer_cast<CustomPayloadPacket>(p)); } };
struct ServerPlayer { shared_ptr<Conn> connection; };
struct List { std::vector<shared_ptr<ServerPlayer>> players; CRITICAL_SECTION m_playersCS; };

int main() {
    using namespace lce_modch;
    // channel rules mirror events195 lce195_net_channel_ok
    CHECK(channel_ok(L"ruby:sync") && channel_ok(L"a:b") && channel_ok(L"my_mod:x.y-z"));
    CHECK(!channel_ok(L"MC|Brand") && !channel_ok(L"LCE|ModIds") && !channel_ok(L"mc|x:y") && !channel_ok(L"lce|a:b"));
    CHECK(!channel_ok(L"nocolon") && !channel_ok(L"ab") && !channel_ok(L"Ruby:sync") && !channel_ok(L"abcdefghij:klmnopqrstu"));
    // token bucket: burst of 64, then 32/s
    Bucket b;
    int ok = 0; for (int i = 0; i < 100; ++i) ok += admit(b, 10, 1000.0);
    CHECK(ok == (int)kBurstMessages);
    CHECK(!admit(b, 10, 1000.0) && admit(b, 10, 1100.0));            // 0.1 s -> 3.2 tokens
    Bucket big; CHECK(classify(L"ruby:sync", kMaxPayload + 1, big, 0) == kDrop && classify(L"MC|Brand", 5, big, 0) == kNotMod);
    Bucket bytes; int n = 0; while (admit(bytes, 30000, 0)) ++n; CHECK(n == (int)(kBurstBytes / 30000));
    // glue: relay to every other player's connection, each with its own copy; vanilla channels pass through
    List list;
    for (int i = 0; i < 3; ++i) { auto p = std::make_shared<ServerPlayer>(); p->connection = std::make_shared<Conn>(); list.players.push_back(p); }
    list.players.push_back(nullptr);
    Conn *self = list.players[0]->connection.get();
    byteArray d(3); d.data[0] = 7; d.data[1] = 8; d.data[2] = 9;
    auto in = std::make_shared<CustomPayloadPacket>(L"ruby:sync", d);
    CHECK(lce_modch_srv::relay(self, list.players[0], &list, in));
    CHECK(list.players[0]->connection->sent.empty());
    for (int i = 1; i < 3; ++i) {
        auto &s = list.players[i]->connection->sent;
        CHECK(s.size() == 1 && s[0]->identifier == L"ruby:sync" && s[0]->length == 3 && s[0]->data.data[2] == 9 && s[0]->shouldDelay);
        CHECK(s[0]->data.data != in->data.data);
    }
    CHECK(list.players[1]->connection->sent[0]->data.data != list.players[2]->connection->sent[0]->data.data);
    CHECK(list.m_playersCS.depth == 0 && list.m_playersCS.entered == 1);
    auto vanilla = std::make_shared<CustomPayloadPacket>(L"MC|Brand", byteArray(1));
    CHECK(!lce_modch_srv::relay(self, list.players[0], &list, vanilla));
    auto empty = std::make_shared<CustomPayloadPacket>(L"ruby:ping", byteArray());
    CHECK(lce_modch_srv::relay(self, list.players[0], &list, empty) && list.players[1]->connection->sent.size() == 2 &&
          list.players[1]->connection->sent[1]->length == 0);
    // flood from one connection: dropped (consumed) after the burst, other connections unaffected
    int relayed = 0;
    for (int i = 0; i < 200; ++i) { size_t before = list.players[1]->connection->sent.size();
        CHECK(lce_modch_srv::relay(self, list.players[0], &list, empty)); relayed += list.players[1]->connection->sent.size() > before; }
    CHECK(relayed < 100);
    Conn *other = list.players[2]->connection.get();
    CHECK(lce_modch_srv::relay(other, list.players[2], &list, empty));
    lce_modch_srv::forget(self); CHECK(lce_modch_srv::buckets().count(self) == 0);
    printf("test_modchannel: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
