#include "orbis_socket.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
static int next;
static int raw_send(int fd, const void *p, size_t n, int flags) { (void)fd;(void)p;(void)n;(void)flags;return next; }
static int raw_recv(int fd, void *p, size_t n, int flags) { (void)fd;(void)p;(void)n;(void)flags;return next; }
int main(void) {
    lce_orbis_socket s = {3,raw_send,raw_recv,0}; uint8_t buf[8];
    next=(int)0x80410123u; assert(!lce_orbis_receive(&s,buf,sizeof buf) && !lce_orbis_send(&s,buf,sizeof buf));
    next=(int)0x80410104u; assert(!lce_orbis_receive(&s,buf,sizeof buf));
    next=3; assert(lce_orbis_receive(&s,buf,sizeof buf)==3 && lce_orbis_send(&s,buf,sizeof buf)==3);
    next=0; assert(lce_orbis_receive(&s,buf,sizeof buf)==-2 && lce_orbis_send(&s,buf,sizeof buf)==-1);
    next=(int)0x80410136u; assert(lce_orbis_receive(&s,buf,sizeof buf)==-1 && s.last_error==next);
    next=9; assert(lce_orbis_send(&s,buf,sizeof buf)==-1);
    puts("Orbis socket mapping tests passed");
}
