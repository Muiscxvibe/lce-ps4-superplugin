#pragma once
#include "framing.h"
#include "send_queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Single-threaded connection pump. The owner creates a nonblocking socket and
   supplies I/O functions: recv returns >0 bytes, 0 for would-block, -1 on
   error, -2 for EOF; send follows lce_send_fn. The pump returns immediately
   after receiving the assigned ID so the owner can attach a frame consumer.
   No game objects are accessed. */
typedef int (*lce_recv_fn)(void *, uint8_t *, size_t);
typedef enum { LCE_CONN_OPEN, LCE_CONN_REJECTED, LCE_CONN_CLOSED, LCE_CONN_ERROR } lce_conn_state;
typedef struct {
    lce_frame_reader reader;
    lce_send_queue outgoing;
    lce_conn_state state;
} lce_connection;
void lce_connection_init(lce_connection *, uint8_t *, size_t, uint8_t *, size_t);
int lce_connection_enqueue(lce_connection *, const uint8_t *, size_t);
lce_conn_state lce_connection_pump(lce_connection *, lce_recv_fn, lce_send_fn,
                                   void *, lce_frame_callback, void *);

#ifdef __cplusplus
}
#endif
