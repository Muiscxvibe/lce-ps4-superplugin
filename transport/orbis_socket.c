#include "orbis_socket.h"
#include <stdint.h>
/* Orbis net error values, returned directly by sceNetSend and sceNetRecv. */
#define ORBIS_WOULD_BLOCK 0x80410123u
#define ORBIS_INTERRUPTED 0x80410104u
static int retry(int rc) {
    return (uint32_t)rc == ORBIS_WOULD_BLOCK || (uint32_t)rc == ORBIS_INTERRUPTED;
}
int lce_orbis_receive(void *context, uint8_t *buffer, size_t size) {
    lce_orbis_socket *socket = context;
    if (!socket || socket->fd < 0 || !socket->recv_raw || !buffer || !size) return -1;
    int rc = socket->recv_raw(socket->fd, buffer, size, 0);
    if (rc > 0) return (size_t)rc <= size ? rc : -1;
    if (!rc) return -2; /* socket EOF */
    if (retry(rc)) return 0;
    socket->last_error = rc;
    return -1;
}
int lce_orbis_send(void *context, const uint8_t *buffer, size_t size) {
    lce_orbis_socket *socket = context;
    if (!socket || socket->fd < 0 || !socket->send_raw || !buffer || !size) return -1;
    int rc = socket->send_raw(socket->fd, buffer, size, 0);
    if (rc > 0) return (size_t)rc <= size ? rc : -1;
    if (rc < 0 && retry(rc)) return 0;
    socket->last_error = rc;
    return -1;
}
