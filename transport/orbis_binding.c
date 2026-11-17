#include "orbis_socket.h"
#include <orbis/Net.h>
/* The caller must provide an already connected, nonblocking socket. */
void lce_orbis_bind_socket(lce_orbis_socket *socket, int fd) {
    socket->fd = fd;
    socket->send_raw = sceNetSend;
    socket->recv_raw = sceNetRecv;
    socket->last_error = 0;
}
