#include "framing.h"
#include <string.h>

#define LCE_MAX_FRAME (3u * 1024u * 1024u)

void lce_frame_init(lce_frame_reader *r, uint8_t *buffer, size_t capacity) {
    memset(r, 0, sizeof(*r));
    r->buffer = buffer;
    r->capacity = capacity;
}

int lce_frame_header(uint8_t out[4], size_t n) {
    if (!out || n == 0 || n > LCE_MAX_FRAME) return 0;
    out[0] = (uint8_t)(n >> 24); out[1] = (uint8_t)(n >> 16);
    out[2] = (uint8_t)(n >> 8); out[3] = (uint8_t)n;
    return 1;
}

int lce_frame_feed(lce_frame_reader *r, const uint8_t *p, size_t n,
                   lce_frame_callback cb, void *context) {
    if (!r || (!p && n) || !r->buffer || !cb || r->failed) return 0;
    while (n) {
        if (r->rejected) { r->failed = 1; return 0; }
        if (r->rejecting) {
            while (n && r->reject_used < sizeof(r->reject_bytes)) {
                r->reject_bytes[r->reject_used++] = *p++; --n;
            }
            if (r->reject_used < sizeof(r->reject_bytes)) break;
            if (r->reject_bytes[0] != 0xff || n) { r->failed = 1; return 0; }
            r->reject_reason = ((uint32_t)r->reject_bytes[1] << 24) |
                ((uint32_t)r->reject_bytes[2] << 16) |
                ((uint32_t)r->reject_bytes[3] << 8) | r->reject_bytes[4];
            r->rejected = 1;
            r->rejecting = 0;
            continue;
        }
        if (!r->has_id) {
            r->assigned_id = *p++; --n;
            if (r->assigned_id == 0xff) r->rejecting = 1;
            /* Zero identifies the server host and cannot identify a client. */
            else if (r->assigned_id == 0) { r->failed = 1; return 0; }
            else r->has_id = 1;
            continue;
        }
        if (!r->expected) {
            while (n && r->header_used < 4) {
                r->header[r->header_used++] = *p++; --n;
            }
            if (r->header_used < 4) break;
            size_t len = ((size_t)r->header[0] << 24) |
                         ((size_t)r->header[1] << 16) |
                         ((size_t)r->header[2] << 8) | r->header[3];
            r->header_used = 0;
            if (!len || len > LCE_MAX_FRAME || len > r->capacity) {
                r->failed = 1; return 0;
            }
            r->expected = len;
        }
        size_t count = r->expected - r->used;
        if (count > n) count = n;
        memcpy(r->buffer + r->used, p, count);
        r->used += count; p += count; n -= count;
        if (r->used == r->expected) {
            if (!cb(context, r->buffer, r->used)) { r->failed = 1; return 0; }
            r->used = r->expected = 0;
        }
    }
    return 1;
}
