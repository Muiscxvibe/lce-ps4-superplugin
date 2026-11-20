// Host-test stand-ins for the engine types LceModChannelRelay.h uses (shapes as in neo-ps4-mob-candidate).
#pragma once
#include <memory>
#include <string>
#include <vector>
using std::shared_ptr;
using std::wstring;
struct byteArray {
    unsigned char *data = nullptr; unsigned length = 0;
    byteArray() = default;
    explicit byteArray(unsigned n) : data(new unsigned char[n ? n : 1]), length(n) {}
};
struct CRITICAL_SECTION { int depth = 0, entered = 0; };
inline void EnterCriticalSection(CRITICAL_SECTION *c) { ++c->depth; ++c->entered; }
inline void LeaveCriticalSection(CRITICAL_SECTION *c) { --c->depth; }
struct Packet { virtual ~Packet() {} bool shouldDelay = false; };
struct CustomPayloadPacket : Packet {
    wstring identifier; int length = 0; byteArray data;
    CustomPayloadPacket() = default;
    CustomPayloadPacket(const wstring &id, byteArray d) : identifier(id), length((int)d.length), data(d) {}
    ~CustomPayloadPacket() override { delete[] data.data; }
};
