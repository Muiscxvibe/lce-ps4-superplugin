#pragma once
#include "connection.h"
#include "gateway_handshake.h"
#ifdef __cplusplus
extern "C" {
#endif

/* All callbacks and the session object belong to the same worker. start and
   poll return 1 connected, 0 pending, -1 failed; open returns fd or -1. The
   timeout covers TCP connect and receiving the server-assigned player ID.
   Time values are monotonic milliseconds supplied by the worker. */
typedef struct {
    void *ctx;
    int (*open)(void *);
    int (*start)(void *, int, const uint8_t ip[4], uint16_t port);
    int (*poll)(void *, int);
    int (*recv)(void *, int, uint8_t *, size_t);
    int (*send)(void *, int, const uint8_t *, size_t);
    void (*close)(void *, int);
} lce_session_io;
typedef enum { LCE_SESSION_CONNECTING, LCE_SESSION_ACTIVE, LCE_SESSION_REJECTED,
               LCE_SESSION_CLOSED, LCE_SESSION_FAILED } lce_session_state;
typedef struct {
    lce_session_io io;
    lce_connection connection;
    int fd;
    lce_session_state state;
    uint64_t deadline, last_tick;
    lce_gateway_handshake gateway;
    int gateway_enabled;
} lce_session_transport;
int lce_session_start(lce_session_transport *, const lce_session_io *,
                      const uint8_t ip[4], uint16_t port, uint64_t now,
                      uint32_t timeout_ms, uint8_t *in, size_t in_size,
                      uint8_t *out, size_t out_size);
int lce_session_start_authenticated(lce_session_transport *, const lce_session_io *,
                      const uint8_t ip[4],uint16_t port,uint64_t now,uint32_t timeout_ms,
                      uint8_t *in,size_t in_size,uint8_t *out,size_t out_size,
                      const char *ticket_id,const char *ticket_key);
lce_session_state lce_session_tick(lce_session_transport *, uint64_t now,
                                   lce_frame_callback, void *);
int lce_session_send(lce_session_transport *, const uint8_t *, size_t);
void lce_session_close(lce_session_transport *);

#ifdef __cplusplus
}
#endif
