// LCE|ModIds engine glue (server): the world's mod ID ledger, placeholder
// Item/Tile objects for those numeric IDs, and the per-connection gate that
// keeps custom stacks away from clients that have not proven they know them.
// Header-only (included by Connection.cpp and, through LceModIdHandshake.h,
// PlayerConnection.cpp) so no CMake source list changes are needed.
// Include after stdafx.h. See modloader-sdk/server/PROTOCOL.md.
#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif
#include "LceModIdCore.h"
#include "Item.h"
#include "TileItem.h"
#include "Tile.h"
#include "Material.h"
#include "ItemInstance.h"
#include "ContainerSetSlotPacket.h"
#include "ContainerSetContentPacket.h"
#include "SetEquippedItemPacket.h"

namespace lce_modids_srv {
using lce_modids::Entry;

constexpr int kItemSlots = Item::ITEM_NUM_COUNT;
constexpr int kTileSlots = Tile::TILE_NUM_COUNT;

// Stand-in for a mod item: correct ID, max stack and durability so stacks
// validate, merge, persist and serialise. No behaviour (use/attack) on the
// server; the description is the stick's so server-side messages stay valid.
class PlaceholderItem : public Item {
public:
    PlaceholderItem(int absoluteId, int maxStack, int maxDamage) : Item(absoluteId - 256) {
        setMaxStackSize(maxStack);
        if (maxDamage > 0) setMaxDamage(maxDamage);
        setDescriptionId(Item::stick->getDescriptionId());
    }
};

// Stand-in for a mod block (ID < 256 only: the server's chunk storage is
// 8-bit). Hardness matches the client so server-side dig validation agrees.
class PlaceholderTile : public Tile {
public:
    static Material *material_for(unsigned m) {
        switch (m) {
        case 1: return Material::wood;
        case 2: return Material::metal;
        case 3: return Material::dirt;
        case 4: return Material::sand;
        case 5: return Material::glass;
        case 6: return Material::plant;
        case 7: return Material::cloth;
        default: return Material::stone;
        }
    }
    explicit PlaceholderTile(const Entry &e)
        : Tile(e.id, material_for(e.material), !(e.flags & lce_modids::kTransparent)) {
        setDestroyTime(e.a / 100.0f);
        setExplodeable(e.b / 100.0f);
        setDescriptionId(Tile::stone->getDescriptionId());
    }
};

struct Registry {
    std::mutex m;
    lce_modids::ServerLedger ledger;
    lce_modids::Policy policy;
    std::string path;          // <world dir>/lce-mod-ids.txt
    bool ready = false;
    bool ownTile[kTileSlots] = {};
};
inline Registry &registry() { static Registry r; return r; }

// Item-space IDs that are mod content on this server (read by write threads).
inline std::atomic<bool> g_custom[kItemSlots];
inline std::atomic<unsigned> g_customCount{0};

inline bool is_custom_item(int id) {
    return id >= 0 && id < kItemSlots && g_custom[id].load(std::memory_order_acquire);
}

// True if e's ID is already used by the server's own content (not ours).
inline bool builtin_conflict(const Entry &e) {
    Registry &r = registry();
    auto itemTaken = [](uint32_t id) { return id >= (uint32_t)kItemSlots || (Item::items[id] != nullptr && !is_custom_item((int)id)); };
    if (e.kind == lce_modids::kItem) return itemTaken(e.id);
    if (e.id >= (uint32_t)kTileSlots) return true;
    if (Tile::tiles[e.id] != nullptr && !r.ownTile[e.id]) return true;
    return (e.flags & lce_modids::kBlockHasItem) && itemTaken(lce_modids::block_item_id(e.id));
}

// Creates the placeholder objects for one entry. Caller checked builtin_conflict.
inline void register_entry(const Entry &e) {
    Registry &r = registry();
    if (e.kind == lce_modids::kItem) {
        if (!is_custom_item(e.id)) {
            new PlaceholderItem(e.id, e.max_stack, e.a);
            g_custom[e.id].store(true, std::memory_order_release); ++g_customCount;
        }
        return;
    }
    const uint32_t itemId = lce_modids::block_item_id(e.id);
    if (e.id < 256 && !r.ownTile[e.id]) {
        new PlaceholderTile(e);
        r.ownTile[e.id] = true;
    }
    if ((e.flags & lce_modids::kBlockHasItem) && !is_custom_item((int)itemId)) {
        if (e.id < 256) (new TileItem(e.id - 256))->setMaxStackSize(e.max_stack);  // item e.id places tile e.id
        else new PlaceholderItem((int)itemId, e.max_stack, 0);                        // item only: cannot be placed here
        g_custom[itemId].store(true, std::memory_order_release); ++g_customCount;
    }
}

inline bool read_text(const std::string &path, std::string &out, bool &exists) {
    out.clear(); exists = false;
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return true;
    exists = true;
    char buf[4096]; size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) { out.append(buf, n); if (out.size() > (1u << 20)) { fclose(f); return false; } }
    bool ok = !ferror(f);
    fclose(f);
    return ok;
}

inline bool write_text_atomic(const std::string &path, const std::string &text) {
    const std::string tmp = path + ".tmp";
    FILE *f = fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
    ok = (fflush(f) == 0) && ok;
    ok = (fclose(f) == 0) && ok;
    if (!ok) { remove(tmp.c_str()); return false; }
#ifdef _WIN32
    if (!MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) { remove(tmp.c_str()); return false; }
#else
    if (rename(tmp.c_str(), path.c_str()) != 0) { remove(tmp.c_str()); return false; }
#endif
    return true;
}

inline void make_dirs(const std::string &dir) {
    for (size_t i = 1; i <= dir.size(); ++i) {
        if (i != dir.size() && dir[i] != '/' && dir[i] != '\\') continue;
        std::string part = dir.substr(0, i);
#ifdef _WIN32
        _mkdir(part.c_str());
#else
        mkdir(part.c_str(), 0755);
#endif
    }
}

// Policy file in the server directory: words "locked" and/or
// "allow-missing-items". Environment LCE_MODIDS_POLICY overrides it.
inline lce_modids::Policy read_policy() {
    std::string text; bool exists = false;
    if (const char *env = getenv("LCE_MODIDS_POLICY")) text = env;
    else read_text("lce-modids-policy.txt", text, exists);
    lce_modids::Policy p;
    if (text.find("locked") != std::string::npos) p.adopt = false;
    if (text.find("allow-missing-items") != std::string::npos) p.allow_missing_items = true;
    return p;
}

// Server start, after the world is chosen and BEFORE it loads (so saved
// stacks/chunks of mod content find their Item/Tile). Returns false (with
// *err) if the ledger is unreadable or no longer fits this server build: the
// caller must not start the world, because loading it would drop mod stacks.
inline bool startup(const std::string &worldDir, std::string *err) {
    Registry &r = registry();
    std::lock_guard<std::mutex> lock(r.m);
    r.path = worldDir + "/lce-mod-ids.txt";
    r.policy = read_policy();
    std::string text; bool exists = false;
    if (!read_text(r.path, text, exists)) { *err = "cannot read " + r.path; return false; }
    const auto st = r.ledger.load(text);
    if (st != lce_modids::ServerLedger::kLoaded) {
        *err = r.path + (st == lce_modids::ServerLedger::kBadHash ? ": checksum mismatch" : ": corrupt") +
               " (restore lce-mod-ids.txt.bak or fix the file; refusing to load the world)";
        r.ledger.entries.clear();
        return false;
    }
    for (const Entry &e : r.ledger.entries) {
        if (builtin_conflict(e)) {
            *err = std::string("world mod ID ") + e.key + " (" + std::to_string(e.id) + ") is now used by this server build";
            return false;
        }
    }
    for (const Entry &e : r.ledger.entries) register_entry(e);
    r.ready = true;
    fprintf(stderr, "[modids] world ledger %s: %u entries, policy adopt=%d allow-missing-items=%d\n", r.path.c_str(),
            (unsigned)r.ledger.entries.size(), r.policy.adopt ? 1 : 0, r.policy.allow_missing_items ? 1 : 0);
    fflush(stderr);
    return true;
}

// Persists first, then registers: an ID is only ever live if it is on disk.
inline bool adopt(const std::vector<Entry> &entries, std::string *err) {
    Registry &r = registry();
    if (entries.empty()) return true;
    lce_modids::ServerLedger next = r.ledger;
    for (const Entry &e : entries) if (!next.add(e)) { *err = std::string("ledger rejects ") + e.key; return false; }
    if (!r.ledger.entries.empty()) (void)write_text_atomic(r.path + ".bak", r.ledger.serialize());
    size_t slash = r.path.find_last_of("/\\");
    if (slash != std::string::npos) make_dirs(r.path.substr(0, slash));
    if (!write_text_atomic(r.path, next.serialize())) { *err = "cannot write " + r.path; return false; }
    r.ledger = next;
    for (const Entry &e : entries) register_entry(e);
    return true;
}

// ---- per-connection gate ------------------------------------------------------
struct Gate {
    std::mutex m;
    std::unordered_map<const void *, std::vector<bool>> allowed;
};
inline Gate &gate() { static Gate g; return g; }

inline void gate_set(const void *conn, const std::vector<uint16_t> &ids) {
    std::vector<bool> bits(kItemSlots, false);
    for (uint16_t id : ids) if (id < kItemSlots) bits[id] = true;
    std::lock_guard<std::mutex> lock(gate().m);
    gate().allowed[conn] = std::move(bits);
}
inline void gate_forget(const void *conn) {
    std::lock_guard<std::mutex> lock(gate().m);
    gate().allowed.erase(conn);
}
// Vanilla IDs always pass; a custom ID passes only after this connection's
// handshake accepted it.
inline bool gate_allows(const void *conn, int itemId) {
    if (!is_custom_item(itemId)) return true;
    std::lock_guard<std::mutex> lock(gate().m);
    auto it = gate().allowed.find(conn);
    return it != gate().allowed.end() && it->second[itemId];
}

// Called by Connection::writePS4Packet for every outgoing packet. May replace
// `packet` with a copy whose disallowed custom stacks are emptied; returns
// false if the packet must be dropped.
inline bool filter_outgoing(const void *conn, shared_ptr<Packet> &packet) {
    if (g_customCount.load(std::memory_order_acquire) == 0 || !packet) return true;
    switch (packet->getId()) {
    case 103: {
        auto p = std::dynamic_pointer_cast<ContainerSetSlotPacket>(packet);
        if (p && p->item && !gate_allows(conn, p->item->id))
            packet = std::make_shared<ContainerSetSlotPacket>(p->containerId, p->slot, nullptr);
        return true;
    }
    case 104: {
        auto p = std::dynamic_pointer_cast<ContainerSetContentPacket>(packet);
        if (!p) return true;
        bool changed = false;
        std::vector<shared_ptr<ItemInstance>> items(p->items.length);
        for (unsigned i = 0; i < p->items.length; ++i) {
            items[i] = p->items[i];
            if (items[i] && !gate_allows(conn, items[i]->id)) { items[i] = nullptr; changed = true; }
        }
        if (changed) packet = std::make_shared<ContainerSetContentPacket>(p->containerId, &items);
        return true;
    }
    case 5: {
        auto p = std::dynamic_pointer_cast<SetEquippedItemPacket>(packet);
        auto item = p ? p->getItem() : nullptr;
        return !(item && !gate_allows(conn, item->id));
    }
    default:
        return true;
    }
}

}  // namespace lce_modids_srv
