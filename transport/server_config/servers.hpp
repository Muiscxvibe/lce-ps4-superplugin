#pragma once

#include <stddef.h>
#include <stdint.h>

/* A tiny allocation-free parser for the future PS4 Join-list integration.
 * Input is a UTF-8/ASCII text file, one `label|host|port[|mg=N]` record per line.
 * The optional `mg=N` (0..255) marks a minigame server for game type N: it is
 * joined by the Mini Games quick-match and hidden from the normal Join list.
 * Blank lines and lines whose first non-space character is '#' are ignored.
 */
namespace lce_servers {

static const size_t kMaxServers = 8;
static const size_t kLabelBytes = 33;
static const size_t kHostBytes = 254;
static const size_t kMaxFileBytes = 4096;

struct Server {
    char label[kLabelBytes];
    char host[kHostBytes];
    uint16_t port;
    int16_t minigame = -1; /* -1 for a normal server */
};

struct List {
    Server entries[kMaxServers];
    size_t count;
};

enum Error {
    Ok = 0,
    InputTooLarge,
    TooManyServers,
    InvalidLine,
};

static inline bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r'; }
static inline bool valid_label_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == ' ' || c == '_' || c == '-';
}

/* Accept dotted IPv4 literals or DNS-style ASCII names. IPv4 octets are
 * range-checked; DNS labels cannot be empty, start/end with '-', or exceed 63.
 * This deliberately does not resolve DNS or accept IPv6 literals.
 */
static inline bool valid_host(const char *s, size_t n) {
    if (!n || n >= kHostBytes) return false;
    bool ipv4 = true;
    size_t i = 0, label_start = 0;
    unsigned dots = 0;
    while (i < n) {
        size_t start = i;
        unsigned value = 0, digits = 0;
        while (i < n && s[i] >= '0' && s[i] <= '9') {
            if (++digits > 3) ipv4 = false;
            value = value * 10 + (unsigned)(s[i] - '0');
            ++i;
        }
        if (digits && value > 255) ipv4 = false;
        if (!digits || (i < n && s[i] != '.')) ipv4 = false;
        if (i < n && s[i] == '.') { ++dots; ++i; }
        if (i == start) break;
    }
    if (ipv4 && dots == 3 && s[n - 1] != '.') return true;
    bool numeric_dotted = true;
    for (size_t j = 0; j < n; ++j)
        if (!((s[j] >= '0' && s[j] <= '9') || s[j] == '.')) numeric_dotted = false;
    if (numeric_dotted) return false;

    i = 0;
    while (i <= n) {
        if (i == n || s[i] == '.') {
            size_t len = i - label_start;
            if (!len || len > 63 || s[label_start] == '-' || s[i - 1] == '-') return false;
            label_start = i + 1;
        } else {
            char c = s[i];
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '-')) return false;
        }
        ++i;
    }
    return true;
}

static inline Error parse(const char *data, size_t size, List *out) {
    if (!out || (!data && size)) return InvalidLine;
    out->count = 0;
    if (size > kMaxFileBytes) return InputTooLarge;
    size_t pos = 0;
    while (pos < size) {
        size_t end = pos;
        while (end < size && data[end] != '\n') ++end;
        size_t a = pos, b = end;
        while (a < b && is_space(data[a])) ++a;
        while (b > a && is_space(data[b - 1])) --b;
        if (a < b && data[a] != '#') {
            if (out->count == kMaxServers) return TooManyServers;
            size_t first = a;
            while (first < b && data[first] != '|') ++first;
            size_t second = first + 1;
            while (second < b && data[second] != '|') ++second;
            if (first == b || second == b) return InvalidLine;
            size_t label_len = first - a, host_start = first + 1;
            size_t host_len = second - host_start, port_start = second + 1;
            if (!label_len || label_len >= kLabelBytes || !host_len || host_len >= kHostBytes || port_start == b)
                return InvalidLine;
            for (size_t i = a; i < first; ++i) if (!valid_label_char(data[i])) return InvalidLine;
            if (!valid_host(data + host_start, host_len)) return InvalidLine;
            size_t port_end = port_start;
            while (port_end < b && data[port_end] != '|') ++port_end;
            int minigame = -1;
            if (port_end < b) {
                size_t m = port_end + 1;
                if (b - m < 4 || data[m] != 'm' || data[m + 1] != 'g' || data[m + 2] != '=') return InvalidLine;
                unsigned v = 0;
                for (size_t i = m + 3; i < b; ++i) {
                    if (data[i] < '0' || data[i] > '9') return InvalidLine;
                    v = v * 10 + (unsigned)(data[i] - '0');
                    if (v > 255) return InvalidLine;
                }
                minigame = (int)v;
            }
            if (port_end == port_start) return InvalidLine;
            unsigned port = 0;
            for (size_t i = port_start; i < port_end; ++i) {
                if (data[i] < '0' || data[i] > '9') return InvalidLine;
                port = port * 10 + (unsigned)(data[i] - '0');
                if (port > 65535) return InvalidLine;
            }
            if (!port) return InvalidLine;
            Server &entry = out->entries[out->count++];
            size_t i = 0;
            for (; i < label_len; ++i) entry.label[i] = data[a + i];
            entry.label[i] = 0;
            for (i = 0; i < host_len; ++i) entry.host[i] = data[host_start + i];
            entry.host[i] = 0;
            entry.port = (uint16_t)port;
            entry.minigame = (int16_t)minigame;
        }
        pos = end < size ? end + 1 : end;
    }
    return Ok;
}

} // namespace lce_servers
