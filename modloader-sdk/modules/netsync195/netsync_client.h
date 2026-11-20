/* netsync195 client: LCE|ModIds handshake for the PS4 session plugin.
 *
 * Threading (matches ps4-transport):
 *   configure()  game thread, once after lce_loader_commit, before any session
 *   on_frame()   session worker thread, from native_bridge.c receive_frame with
 *                the bridge spin lock HELD: it never sends, it only posts
 *   pump()       UI/game thread, from session_runtime Runtime::run_ui_task:
 *                sends the HELLO through Coordinator::send_game_packet
 * No heap, no STL, no exceptions: static storage only.
 */
#pragma once
#include "lce_modids_wire.h"

namespace lce_netsync {

using lce_modids::Entry;
typedef int (*SendFn)(void *ctx, const uint8_t *packet, size_t size);  // nonzero = sent
typedef void (*LogFn)(void *ctx, const char *line);

enum class State : unsigned { Idle, Requested, Sent, Accepted, Rejected, Failed };
constexpr unsigned kMaxServerMods = 32, kModIdCap = 32;

struct Report {
    State state;
    uint16_t code;               // lce_modids::Code of the RESULT (or local failure)
    char message[lce_modids::kMaxMessage + 1];
    uint16_t server_entries;     // entries in the server's table
    uint16_t table_matches;      // server entries live locally with the same ID
    uint16_t table_missing;      // server entries this console does not have
    uint16_t table_conflicts;    // same key, different ID (must be 0 when accepted)
    bool table_ok;               // table chunks complete and digest verified
    // Mod namespaces ("modid") of the server's table that this console has
    // live with the same IDs and no conflicting entry: the SDK mods the server
    // declares. Meaningful only for Accepted + table_ok (see server_mods()).
    uint8_t mod_count;
    char mods[kMaxServerMods][kModIdCap];
};

class Client {
public:
    /* Copies the entries (ledger order, live flags and properties filled in).
     * Returns false if they do not validate or exceed the protocol limits. */
    bool configure(const Entry *entries, unsigned count, uint32_t api_version, bool ledger_ok);
    unsigned entry_count() const { return count_; }
    const uint8_t *digest() const { return digest_; }

    /* Number of HELLO packets and their exact bytes (complete 225 packet). */
    unsigned hello_chunks() const { return chunks_; }
    size_t build_hello_packet(unsigned chunk, uint32_t session, uint8_t *out, size_t cap) const;

    /* Worker thread. Returns true when the frame was an LCE|ModIds message
     * (consumed; it must not reach the retail client). */
    bool on_frame(const uint8_t *frame, size_t n);

    /* UI thread. active=false (no session / left the server) resets the
     * handshake. Logs through `log` (may be null). Returns true when a new
     * final report became available this call. */
    bool pump(bool active, SendFn send, void *send_ctx, LogFn log, void *log_ctx);
    const Report &report() const { return report_; }
    /* UI thread. The server-declared SDK mods of the current session: copies
     * up to cap namespaces and returns their number; 0 unless the last RESULT
     * accepted this console with a complete, verified, conflict-free table. */
    unsigned server_mods(char (*out)[kModIdCap], unsigned cap) const;

private:
    struct Assembly {               // worker-thread only
        bool active;
        uint32_t session;
        lce_modids::ResultInfo result;
        unsigned next_table, table_chunks;
        uint16_t seen, matches, missing, conflicts;
        lce_modids::Digest digest;
        uint8_t matched[lce_modids::kMaxEntries];    // local entry i: server has it, same ID, live here
        uint8_t conflicted[lce_modids::kMaxEntries]; // local entry i: server has the key with another ID
    };
    void reset_assembly(uint32_t session);
    void handle_table_entry(const Entry &e);
    void publish(bool table_ok);

    Entry entries_[lce_modids::kMaxEntries];
    unsigned count_ = 0, chunks_ = 0;
    unsigned first_[lce_modids::kMaxChunks + 1];
    uint8_t digest_[32];
    uint32_t api_version_ = 0;
    bool ledger_ok_ = false, configured_ = false;

    // worker -> UI mailbox
    uint64_t request_word_ = 0;             // (seq << 32) | session, one atomic word
    uint32_t request_seen_ = 0;
    Report pending_;                        // written before result_seq_ (release)
    uint32_t result_seq_ = 0, result_seen_ = 0;
    uint32_t reset_gen_ = 0;                // UI -> worker: drop in-flight assembly
    uint32_t worker_gen_ = 0;
    Assembly asm_{};

    // UI-thread state
    Report report_{};
    uint32_t sent_session_ = 0;
    bool was_active_ = false;
};

}  // namespace lce_netsync
