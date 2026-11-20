#include "netsync_client.h"
#include <stdio.h>

namespace lce_netsync {
using namespace lce_modids;

namespace {
template <class T> T load_acq(const T *p) { return __atomic_load_n(p, __ATOMIC_ACQUIRE); }
template <class T> void store_rel(T *p, T v) { __atomic_store_n(p, v, __ATOMIC_RELEASE); }

bool same_key(const Entry &a, const Entry &b) {
    return a.kind == b.kind && a.key_len == b.key_len && !memcmp(a.key, b.key, a.key_len);
}
void say(LogFn log, void *ctx, const char *line) { if (log) log(ctx, line); }
// "modid" of an entry key "modid:name" (keys are validated by the codec).
bool mod_namespace(const Entry &e, char out[kModIdCap]) {
    unsigned n = 0;
    while (n < e.key_len && e.key[n] != ':') { if (n + 1 >= kModIdCap) return false; out[n] = e.key[n]; ++n; }
    out[n] = 0;
    return n >= 2 && n < e.key_len;
}
}  // namespace

bool Client::configure(const Entry *entries, unsigned count, uint32_t api_version, bool ledger_ok) {
    configured_ = false; count_ = 0; chunks_ = 0;
    if (count > kMaxEntries || (count && !entries)) return false;
    for (unsigned i = 0; i < count; ++i) {
        // Round-trip every entry through the codec: whatever we would send
        // must be accepted by the decoder on the server.
        uint8_t buf[kEntryFixed + kMaxKey];
        Writer w; w.init(buf, sizeof buf); put_entry(w, entries[i]);
        Reader r; r.init(buf, w.n);
        Entry back;
        if (!w.ok || !get_entry(r, back) || r.left() || entries[i].key[entries[i].key_len] != 0) return false;
        for (unsigned j = 0; j < i; ++j) {
            const Entry &o = entries[j];
            if (same_key(o, back) || item_space_id(o) == item_space_id(back) ||
                (o.kind == kBlock && back.kind == kBlock && o.id == back.id))
                return false;
        }
        entries_[i] = back;
    }
    count_ = count;
    Digest d; d.init();
    for (unsigned i = 0; i < count_; ++i) d.add(entries_[i]);
    d.final(digest_);
    chunks_ = plan_chunks(entries_, count_, kHelloPrefix, first_, kMaxChunks);
    if (!chunks_) { count_ = 0; return false; }
    first_[chunks_] = count_;
    api_version_ = api_version; ledger_ok_ = ledger_ok; configured_ = true;
    return true;
}

size_t Client::build_hello_packet(unsigned chunk, uint32_t session, uint8_t *out, size_t cap) const {
    if (chunk >= chunks_) return 0;
    uint8_t payload[kChunkBudget];
    Writer w; w.init(payload, sizeof payload);
    put_header(w, kHello, session, chunk, chunks_);
    if (chunk == 0) {
        w.u32(api_version_); w.u8(ledger_ok_ ? 1 : 0); w.u16(count_); w.bytes(digest_, 32);
    }
    const unsigned a = first_[chunk], b = first_[chunk + 1];
    w.u16(b - a);
    for (unsigned i = a; i < b; ++i) put_entry(w, entries_[i]);
    if (!w.ok) return 0;
    return build_serverbound(payload, w.n, out, cap);
}

void Client::reset_assembly(uint32_t session) {
    memset(&asm_, 0, sizeof asm_);
    asm_.active = true; asm_.session = session;
    asm_.digest.init();
}

void Client::handle_table_entry(const Entry &e) {
    asm_.digest.add(e);
    ++asm_.seen;
    for (unsigned i = 0; i < count_; ++i) {
        const Entry &l = entries_[i];
        if (!same_key(l, e)) continue;
        if (l.id != e.id) { ++asm_.conflicts; asm_.conflicted[i] = 1; }
        else if (l.flags & kLive) { ++asm_.matches; asm_.matched[i] = 1; }
        else ++asm_.missing;
        return;
    }
    ++asm_.missing;
}

void Client::publish(bool table_ok) {
    asm_.active = false;
    // One report at a time: if the UI thread has not taken the previous one,
    // this (second) result for the session is dropped.
    if (load_acq(&result_seen_) != result_seq_) return;
    Report &p = pending_;
    memset(&p, 0, sizeof p);
    p.state = asm_.result.verdict == kAccept ? State::Accepted : State::Rejected;
    p.code = asm_.result.code;
    memcpy(p.message, asm_.result.msg, asm_.result.msg_len); p.message[asm_.result.msg_len] = 0;
    p.server_entries = asm_.result.total;
    p.table_matches = asm_.matches; p.table_missing = asm_.missing; p.table_conflicts = asm_.conflicts;
    p.table_ok = table_ok;
    // Declared mods: namespaces with at least one matched entry and none in conflict.
    for (unsigned i = 0; i < count_ && p.mod_count < kMaxServerMods; ++i) {
        if (!asm_.matched[i]) continue;
        char ns[kModIdCap];
        if (!mod_namespace(entries_[i], ns)) continue;
        bool skip = false;
        for (unsigned k = 0; k < p.mod_count && !skip; ++k) skip = !strcmp(p.mods[k], ns);
        for (unsigned j = 0; j < count_ && !skip; ++j) {
            char other[kModIdCap];
            skip = asm_.conflicted[j] && mod_namespace(entries_[j], other) && !strcmp(other, ns);
        }
        if (!skip) memcpy(p.mods[p.mod_count++], ns, kModIdCap);
    }
    store_rel(&result_seq_, result_seq_ + 1);
}

unsigned Client::server_mods(char (*out)[kModIdCap], unsigned cap) const {
    const Report &r = report_;
    if (r.state != State::Accepted || !r.table_ok || r.table_conflicts || !out) return 0;
    unsigned n = 0;
    for (; n < r.mod_count && n < cap && n < kMaxServerMods; ++n) memcpy(out[n], r.mods[n], kModIdCap);
    return n;
}

bool Client::on_frame(const uint8_t *frame, size_t n) {
    const uint8_t *payload; size_t len;
    if (!match_clientbound(frame, n, &payload, &len)) return false;
    const uint32_t gen = load_acq(&reset_gen_);
    if (gen != worker_gen_) { worker_gen_ = gen; asm_.active = false; }
    Reader r; r.init(payload, len);
    Header h;
    if (!get_header(r, h) || h.version != kVersion) return true;  // ours, but unusable: swallow
    if (h.type == kRequest) {
        RequestInfo info;
        info.total = (uint16_t)r.u16(); r.bytes(info.digest, 32); info.flags = (uint8_t)r.u8();
        if (!r.ok || r.left()) return true;
        reset_assembly(h.session);
        const uint64_t seq = (__atomic_load_n(&request_word_, __ATOMIC_RELAXED) >> 32) + 1;
        store_rel(&request_word_, (seq << 32) | h.session);
        return true;
    }
    if (!asm_.active || h.session != asm_.session) return true;
    if (h.type == kResult) {
        ResultInfo &res = asm_.result;
        res.verdict = (uint8_t)r.u8(); res.code = (uint16_t)r.u16(); res.msg_len = (uint16_t)r.u16();
        if (!r.ok || res.msg_len > kMaxMessage || !r.bytes(res.msg, res.msg_len)) { asm_.active = false; return true; }
        res.msg[res.msg_len] = 0;
        for (unsigned i = 0; i < res.msg_len; ++i) if ((uint8_t)res.msg[i] < 0x20 || (uint8_t)res.msg[i] > 0x7e) res.msg[i] = '?';
        res.total = (uint16_t)r.u16(); r.bytes(res.digest, 32); res.table_chunks = (uint8_t)r.u8();
        if (!r.ok || r.left() || h.index != 0 || h.count != 1 || res.verdict > kReject || res.table_chunks > kMaxChunks) {
            asm_.active = false; return true;
        }
        asm_.table_chunks = res.table_chunks; asm_.next_table = 0;
        if (!res.table_chunks) {
            uint8_t d[32]; asm_.digest.final(d);
            publish(res.total == 0 && !memcmp(d, res.digest, 32));
        }
        return true;
    }
    if (h.type == kTable && asm_.table_chunks) {
        if (h.index != asm_.next_table || h.count != asm_.table_chunks) { publish(false); return true; }
        unsigned k = r.u16();
        for (unsigned i = 0; i < k && r.ok; ++i) {
            Entry e;
            if (!get_entry(r, e)) { publish(false); return true; }
            handle_table_entry(e);
        }
        if (!r.ok || r.left() || asm_.seen > asm_.result.total) { publish(false); return true; }
        if (++asm_.next_table == asm_.table_chunks) {
            uint8_t d[32]; asm_.digest.final(d);
            publish(asm_.seen == asm_.result.total && !memcmp(d, asm_.result.digest, 32));
        }
    }
    return true;
}

bool Client::pump(bool active, SendFn send, void *send_ctx, LogFn log, void *log_ctx) {
    char line[lce_modids::kMaxMessage + 200];
    if (!active) {
        if (was_active_) {
            store_rel(&reset_gen_, reset_gen_ + 1);
            request_seen_ = (uint32_t)(load_acq(&request_word_) >> 32);
            store_rel(&result_seen_, load_acq(&result_seq_));
            memset(&report_, 0, sizeof report_);
            sent_session_ = 0;
            was_active_ = false;
        }
        return false;
    }
    was_active_ = true;
    const uint64_t word = load_acq(&request_word_);
    const uint32_t rq = (uint32_t)(word >> 32);
    if (rq != request_seen_) {
        request_seen_ = rq;
        const uint32_t session = (uint32_t)word;
        if (!configured_) (void)configure(nullptr, 0, 0, false);  // no SDK content: report an empty table
        memset(&report_, 0, sizeof report_);
        report_.state = State::Requested;
        unsigned k = 0;
        for (; k < chunks_; ++k) {
            uint8_t packet[kMaxPacket];
            size_t n = build_hello_packet(k, session, packet, sizeof packet);
            if (!n || !send || !send(send_ctx, packet, n)) break;
        }
        if (k != chunks_) {
            report_.state = State::Failed; report_.code = kMalformed;
            snprintf(report_.message, sizeof report_.message, "could not send mod ID table (packet %u of %u)", k + 1, chunks_);
            snprintf(line, sizeof line, "[netsync] %s\n", report_.message); say(log, log_ctx, line);
            return true;
        }
        report_.state = State::Sent; sent_session_ = session;
        snprintf(line, sizeof line, "[netsync] server requested mod IDs: sent %u entries in %u packet(s)\n", count_, chunks_);
        say(log, log_ctx, line);
    }
    const uint32_t rs = load_acq(&result_seq_);
    if (rs != result_seen_) {
        report_ = pending_;
        store_rel(&result_seen_, rs);
        snprintf(line, sizeof line, "[netsync] server %s mod IDs (code %u): %s | table %u entries, %u match, %u missing, %u conflict, digest %s\n",
                 report_.state == State::Accepted ? "accepted" : "REJECTED", report_.code, report_.message,
                 report_.server_entries, report_.table_matches, report_.table_missing, report_.table_conflicts,
                 report_.table_ok ? "ok" : "BAD");
        say(log, log_ctx, line);
        if (report_.state == State::Accepted && (!report_.table_ok || report_.table_conflicts)) {
            snprintf(line, sizeof line, "[netsync] WARNING: server table disagrees with this console; leave the server before using mod items\n");
            say(log, log_ctx, line);
        }
        return true;
    }
    return false;
}

}  // namespace lce_netsync
