#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The caller owns the frame storage and supplies a packet callback.  The
   first byte on a new connection is the ID assigned by the server. */
typedef int (*lce_frame_callback)(void *, const uint8_t *, size_t);
typedef struct {
    uint8_t *buffer;
    size_t capacity, used, expected;
    uint8_t header[4], header_used, assigned_id, has_id, failed;
    uint8_t reject_bytes[5], reject_used, rejecting, rejected;
    uint32_t reject_reason;
} lce_frame_reader;

void lce_frame_init(lce_frame_reader *, uint8_t *buffer, size_t capacity);
int lce_frame_feed(lce_frame_reader *, const uint8_t *, size_t,
                   lce_frame_callback, void *);
int lce_frame_header(uint8_t out[4], size_t payload_size);

#ifdef __cplusplus
}
#endif
