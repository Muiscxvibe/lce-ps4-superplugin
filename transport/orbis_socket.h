#pragma once
#include "connection.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Connected, nonblocking Orbis socket. Socket creation, connection, closure,
   thread ownership and sceNet library lifetime are managed by the caller. */
typedef struct {
    int fd;
    int (*send_raw)(int, const void *, size_t, int);
    int (*recv_raw)(int, void *, size_t, int);
    int last_error;
} lce_orbis_socket;
int lce_orbis_receive(void *, uint8_t *, size_t);
int lce_orbis_send(void *, const uint8_t *, size_t);
/* Implemented by orbis_binding.c and linked against the PS4 Net library. */
void lce_orbis_bind_socket(lce_orbis_socket *, int fd);

#ifdef __cplusplus
}
#endif
