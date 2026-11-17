#pragma once
#include <stddef.h>
#include <stdint.h>
namespace lce_socket_native {
// The pinned executable and native player must remain live; caller owns the
// game socket lifecycle and must drain the transport before its destruction.
bool bind(uintptr_t base);
// Construct a client socket for a valid NetworkPlayerSony. The caller must
// connect it to the game's connection.
void *create_client(void *player);
void push(void *socket, const uint8_t *data, uint32_t size, int from_host);

// The guard must be nonblocking and return true only after game and bridge
// users can no longer retain or access this socket. Set it before activation.
using QuiescenceGuard = bool (*)(void *local_wrapper, void *socket);
bool set_quiescence_guard(QuiescenceGuard guard);
// Mirrors the stock Sony close path: close client end, zero-time wait for its
// closed event, clear NetworkPlayerSony::m_pSocket, run Socket dtor and free.
// It returns false if quiescence or the close event is not established.
bool retire_client(void *local_wrapper, void *socket);
// Close only the client end (Socket::close(false)) so the game's Connection
// observes isClosing() and leaves with "connection lost". Does not destroy.
bool close_client_end(void *socket);
}
