#pragma once
#include "session_transport.h"
#include "orbis_socket.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    lce_orbis_socket socket;
    uint8_t sockaddr[16];
    int last_raw;
} lce_orbis_session;
/* Provides raw socket creation/connect/send/receive/close for a single session.
   Caller supplies monotonic ticks, loads libSceNet and runs on one worker. */
lce_session_io lce_orbis_session_io(lce_orbis_session *);
/* True only if the imported SceNet socket API can open and close a stream
   socket. This does not establish internet/LAN reachability. */
int lce_orbis_session_socket_api_ready(void);

#ifdef __cplusplus
}
#endif
