#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Single-owner queue: producer enqueues whole packets, worker sends bytes.
   Return -1 from send for failure, 0 for would-block, or a byte count. */
typedef int (*lce_send_fn)(void *, const uint8_t *, size_t);
typedef struct {
    uint8_t *storage;
    size_t capacity, begin, end;
    int failed;
} lce_send_queue;
void lce_send_init(lce_send_queue *, uint8_t *, size_t);
int lce_send_enqueue(lce_send_queue *, const uint8_t *, size_t);
int lce_send_flush(lce_send_queue *, lce_send_fn, void *);

#ifdef __cplusplus
}
#endif
