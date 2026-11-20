/* Small shared helpers (header-only, no libc beyond memcpy/strlen). */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace lce195u {
/* UTF-8 -> UTF-16 (BMP + surrogate pairs). Control characters (< 0x20) are
 * dropped. Returns units written (<= cap-1, NUL-terminated) or -1 on invalid UTF-8. */
inline int utf8_to_16(const char *s, uint16_t *out, uint32_t cap) {
    if (!s || !out || cap < 1) return -1;
    uint32_t n = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        uint32_t c = *p, extra = 0;
        if (c < 0x80) { ++p; }
        else if ((c & 0xe0) == 0xc0) { c &= 0x1f; extra = 1; }
        else if ((c & 0xf0) == 0xe0) { c &= 0x0f; extra = 2; }
        else if ((c & 0xf8) == 0xf0) { c &= 0x07; extra = 3; }
        else return -1;
        if (extra) {
            ++p;
            for (uint32_t i = 0; i < extra; ++i, ++p) { if ((*p & 0xc0) != 0x80) return -1; c = (c << 6) | (*p & 0x3f); }
            if ((extra == 1 && c < 0x80) || (extra == 2 && c < 0x800) || (extra == 3 && c < 0x10000) || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) return -1;
        }
        if (c < 0x20) continue;
        if (c >= 0x10000) {
            if (n + 2 >= cap) break;
            c -= 0x10000; out[n++] = (uint16_t)(0xd800 | (c >> 10)); out[n++] = (uint16_t)(0xdc00 | (c & 0x3ff));
        } else {
            if (n + 1 >= cap) break;
            out[n++] = (uint16_t)c;
        }
    }
    out[n] = 0;
    return (int)n;
}
/* UTF-16 -> UTF-8; invalid surrogates become '?'. Returns bytes written (NUL-terminated). */
inline uint32_t utf16_to_8(const uint16_t *s, uint32_t len, char *out, uint32_t cap) {
    if (!out || !cap) return 0;
    uint32_t n = 0;
    for (uint32_t i = 0; i < len; ++i) {
        uint32_t c = s[i];
        if (c >= 0xd800 && c <= 0xdbff && i + 1 < len && s[i + 1] >= 0xdc00 && s[i + 1] <= 0xdfff) { c = 0x10000 + ((c - 0xd800) << 10) + (s[i + 1] - 0xdc00); ++i; }
        else if (c >= 0xd800 && c <= 0xdfff) c = '?';
        if (!c) break;
        uint32_t w = c < 0x80 ? 1 : c < 0x800 ? 2 : c < 0x10000 ? 3 : 4;
        if (n + w >= cap) break;
        if (w == 1) out[n++] = (char)c;
        else {
            out[n++] = (char)((w == 2 ? 0xc0 : w == 3 ? 0xe0 : 0xf0) | (c >> (6 * (w - 1))));
            for (uint32_t j = w - 1; j; --j) out[n++] = (char)(0x80 | ((c >> (6 * (j - 1))) & 0x3f));
        }
    }
    out[n] = 0;
    return n;
}
inline void copy(char *dst, size_t cap, const char *src) {
    if (!cap) return;
    size_t n = 0;
    if (src) while (src[n] && n + 1 < cap) { dst[n] = src[n]; ++n; }
    dst[n] = 0;
}
/* Bounded multi-producer byte-message queue (spinlock; short critical sections). take() moves everything into `out`
 * (a caller-owned snapshot, single consumer). */
template <uint32_t Msgs, uint32_t Bytes> struct MsgQueue {
    struct M { char ch[24]; uint32_t off, len; uint8_t kind; };
    uint32_t lock, count, used;
    M m[Msgs];
    uint8_t buf[Bytes];
    void acquire() { while (__atomic_exchange_n(&lock, 1u, __ATOMIC_ACQUIRE)) {} }
    void release() { __atomic_store_n(&lock, 0u, __ATOMIC_RELEASE); }
    bool push(uint8_t kind, const char *ch, const void *data, uint32_t len) {
        size_t cl = ch ? strlen(ch) : 0;
        if (cl >= sizeof m[0].ch || (len && !data)) return false;
        acquire();
        const bool ok = count < Msgs && len <= Bytes - used;
        if (ok) {
            M &x = m[count++]; memcpy(x.ch, ch, cl + 1); x.off = used; x.len = len; x.kind = kind;
            if (len) memcpy(buf + used, data, len);
            used += len;
        }
        release();
        return ok;
    }
    uint32_t take(MsgQueue &out) {
        acquire();
        const uint32_t n = count;
        if (n) { memcpy(out.m, m, sizeof(M) * n); memcpy(out.buf, buf, used); out.count = n; out.used = used; count = used = 0; }
        release();
        return n;
    }
};
/* Dinkumware std::wstring on 1.95: 40 bytes {proxy, buf[16]|ptr, size, cap};
 * inline buffer when cap < 8 (layout from r72 native_owner.cpp utf8()). */
struct WsImage { uint64_t w[5]; };
} // namespace lce195u
