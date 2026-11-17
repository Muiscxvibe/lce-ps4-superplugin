#include "send_queue.h"
#include "framing.h"
#include <string.h>

void lce_send_init(lce_send_queue *q, uint8_t *storage, size_t capacity) {
    memset(q, 0, sizeof(*q));
    q->storage = storage; q->capacity = capacity;
}

int lce_send_enqueue(lce_send_queue *q, const uint8_t *packet, size_t len) {
    uint8_t header[4];
    if (!q || !q->storage || !packet || q->failed ||
        !lce_frame_header(header, len) || len > q->capacity ||
        q->capacity - len < 4) return 0;
    size_t pending = q->end - q->begin;
    if (pending > q->capacity - len - 4) return 0;
    if (q->capacity - q->end < len + 4) {
        memmove(q->storage, q->storage + q->begin, pending);
        q->begin = 0; q->end = pending;
    }
    memcpy(q->storage + q->end, header, 4); q->end += 4;
    memcpy(q->storage + q->end, packet, len); q->end += len;
    return 1;
}

int lce_send_flush(lce_send_queue *q, lce_send_fn send, void *context) {
    if (!q || !q->storage || !send || q->failed) return -1;
    while (q->begin < q->end) {
        size_t available = q->end - q->begin;
        int sent = send(context, q->storage + q->begin, available);
        if (sent < 0 || (size_t)sent > available) { q->failed = 1; return -1; }
        if (!sent) return 0;
        q->begin += (size_t)sent;
    }
    q->begin = q->end = 0;
    return 1;
}
