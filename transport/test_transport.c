#include "framing.h"
#include "send_queue.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct capture { unsigned count; unsigned char bytes[16]; size_t size; };
static int receive(void *context, const uint8_t *data, size_t size) {
    struct capture *c = context;
    assert(c->size + size <= sizeof c->bytes);
    memcpy(c->bytes + c->size, data, size); c->size += size; c->count++;
    return 1;
}
static int partial_send(void *context, const uint8_t *data, size_t size) {
    struct capture *c = context;
    if (size > 2) size = 2;
    assert(c->size + size <= sizeof c->bytes);
    memcpy(c->bytes + c->size, data, size); c->size += size;
    return (int)size;
}
int main(void) {
    uint8_t buffer[16], queue_buffer[16];
    const uint8_t wire[] = {4,0,0,0,2,0xe2,0,0,0,0,1,2};
    lce_frame_reader reader; struct capture c = {0};
    lce_frame_init(&reader, buffer, sizeof buffer);
    for (size_t i = 0; i < sizeof wire; ++i)
        assert(lce_frame_feed(&reader, wire + i, 1, receive, &c));
    assert(reader.has_id && reader.assigned_id == 4 && c.count == 2);
    assert(c.size == 3 && !memcmp(c.bytes, "\xe2\0\x02", 3));

    const uint8_t reject[] = {0xff,0xff,0,0,0,7};
    lce_frame_init(&reader, buffer, sizeof buffer);
    for (size_t i = 0; i < sizeof reject; ++i)
        assert(lce_frame_feed(&reader, reject + i, 1, receive, &c));
    assert(reader.rejected && !reader.has_id && reader.reject_reason == 7);
    assert(!lce_frame_feed(&reader, wire, 1, receive, &c));

    const uint8_t malformed[] = {0xff,0,0,0,0,7};
    lce_frame_init(&reader, buffer, sizeof buffer);
    assert(!lce_frame_feed(&reader, malformed, sizeof malformed, receive, &c));
    const uint8_t host_id[] = {0};
    lce_frame_init(&reader, buffer, sizeof buffer);
    assert(!lce_frame_feed(&reader, host_id, sizeof host_id, receive, &c));
    assert(reader.failed && !reader.has_id);
    lce_send_queue queue; lce_send_init(&queue, queue_buffer, sizeof queue_buffer);
    uint8_t payload[] = {0xe2,0xac,0x02}; c = (struct capture){0};
    assert(lce_send_enqueue(&queue, payload, sizeof payload));
    assert(lce_send_flush(&queue, partial_send, &c) == 1);
    const uint8_t expected[] = {0,0,0,3,0xe2,0xac,0x02};
    assert(c.size == sizeof expected && !memcmp(c.bytes, expected, sizeof expected));
    puts("transport framing checks passed");
    return 0;
}
