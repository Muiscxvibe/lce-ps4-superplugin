#include "connection.h"

void lce_connection_init(lce_connection *c, uint8_t *incoming, size_t incoming_size,
                         uint8_t *outgoing, size_t outgoing_size) {
    lce_frame_init(&c->reader, incoming, incoming_size);
    lce_send_init(&c->outgoing, outgoing, outgoing_size);
    c->state = LCE_CONN_OPEN;
}
int lce_connection_enqueue(lce_connection *c, const uint8_t *packet, size_t size) {
    return c && c->state == LCE_CONN_OPEN && lce_send_enqueue(&c->outgoing, packet, size);
}
lce_conn_state lce_connection_pump(lce_connection *c, lce_recv_fn receive, lce_send_fn send,
                                   void *io, lce_frame_callback frame, void *frame_context) {
    uint8_t bytes[4096];
    if (!c) return LCE_CONN_ERROR;
    if (c->state != LCE_CONN_OPEN) return c->state;
    if (!receive || !send || !frame) {
        c->state = LCE_CONN_ERROR; return c->state;
    }
    /* Bound each tick so a busy peer cannot starve the game thread. */
    for (unsigned i = 0; i < 16; ++i) {
        /* Do not consume the first game frame in the same read as the player
           ID. The owner needs that ID to construct and attach the native game
           socket before any frame can be delivered. TCP may coalesce both. */
        const int waiting_for_id = !c->reader.has_id && !c->reader.rejecting;
        const size_t capacity = waiting_for_id ? 1 : sizeof(bytes);
        int n = receive(io, bytes, capacity);
        if (!n) break;
        if (n == -2) {
            /* EOF in the middle of a frame or rejection is a truncated stream. */
            c->state = (c->reader.header_used || c->reader.expected || c->reader.rejecting)
                ? LCE_CONN_ERROR : LCE_CONN_CLOSED;
            break;
        }
        if (n < 0 || (size_t)n > capacity ||
            !lce_frame_feed(&c->reader, bytes, (size_t)n, frame, frame_context)) {
            c->state = LCE_CONN_ERROR; break;
        }
        if (c->reader.rejected) { c->state = LCE_CONN_REJECTED; break; }
        if (waiting_for_id && c->reader.has_id) break;
    }
    if (c->state == LCE_CONN_OPEN && c->reader.has_id &&
        lce_send_flush(&c->outgoing, send, io) < 0) c->state = LCE_CONN_ERROR;
    return c->state;
}
