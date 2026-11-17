#include "connection.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
typedef struct { const unsigned char *in; size_t length, offset, step; unsigned char out[64]; size_t sent; int eof; } Fake;
static int receive(void *ctx, uint8_t *buf, size_t cap) {
    Fake *f = ctx; if (f->offset == f->length) return f->eof ? -2 : 0;
    size_t n = f->length - f->offset; if (n > f->step) n = f->step; if (n > cap) n = cap;
    memcpy(buf, f->in + f->offset, n); f->offset += n; return (int)n;
}
static int send_data(void *ctx, const uint8_t *buf, size_t len) {
    Fake *f = ctx; if (len > 2) len = 2; assert(f->sent + len <= sizeof(f->out));
    memcpy(f->out + f->sent, buf, len); f->sent += len; return (int)len;
}
static int frame(void *ctx, const uint8_t *p, size_t n) {
    int *seen = ctx; assert(n == 2 && p[0] == 9 && p[1] == 8); ++*seen; return 1;
}
int main(void) {
    uint8_t incoming[64], outgoing[64]; lce_connection c; int seen = 0;
    const uint8_t wire[] = {3,0,0,0,2,9,8}; Fake f = {wire,sizeof(wire),0,sizeof(wire),{0},0,0};
    lce_connection_init(&c,incoming,sizeof(incoming),outgoing,sizeof(outgoing));
    const uint8_t packet[] = {4,5}; assert(lce_connection_enqueue(&c,packet,sizeof(packet)));
    assert(lce_connection_pump(&c,receive,send_data,&f,frame,&seen) == LCE_CONN_OPEN);
    const uint8_t expected[] = {0,0,0,2,4,5};
    assert(seen == 0 && f.offset == 1 && c.reader.assigned_id == 3 && f.sent == sizeof(expected) && !memcmp(f.out,expected,sizeof(expected)));
    assert(lce_connection_pump(&c,receive,send_data,&f,frame,&seen) == LCE_CONN_OPEN && seen == 1);
    f.eof = 1; assert(lce_connection_pump(&c,receive,send_data,&f,frame,&seen) == LCE_CONN_CLOSED);
    assert(!lce_connection_enqueue(&c,packet,sizeof(packet)));
    const uint8_t reject[] = {255,255,0,0,0,7}; Fake r = {reject,sizeof(reject),0,1,{0},0,0};
    lce_connection_init(&c,incoming,sizeof(incoming),outgoing,sizeof(outgoing));
    assert(lce_connection_pump(&c,receive,send_data,&r,frame,&seen) == LCE_CONN_REJECTED && c.reader.reject_reason == 7);
    const uint8_t partial[] = {3,0,0}; Fake t = {partial,sizeof(partial),0,1,{0},0,1};
    lce_connection_init(&c,incoming,sizeof(incoming),outgoing,sizeof(outgoing));
    assert(lce_connection_pump(&c,receive,send_data,&t,frame,&seen) == LCE_CONN_OPEN);
    assert(lce_connection_pump(&c,receive,send_data,&t,frame,&seen) == LCE_CONN_ERROR);
    puts("connection pump tests passed"); return 0;
}
