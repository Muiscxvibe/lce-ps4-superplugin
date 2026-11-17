#include "orbis_socket.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
static int map_error(int rc) { return rc >= 0 ? rc : errno == EAGAIN || errno == EWOULDBLOCK ? (int)0x80410123u : (int)0x80410136u; }
static int os_send(int fd, const void *p, size_t n, int flags) { return map_error((int)send(fd,p,n,flags)); }
static int os_recv(int fd, void *p, size_t n, int flags) { return map_error((int)recv(fd,p,n,flags)); }
static int got(void *ctx, const uint8_t *p, size_t n) { int *count=ctx; assert(n==2 && p[0]==9 && p[1]==8); ++*count; return 1; }
int main(void) {
 int pair[2]; assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));
 int flags=fcntl(pair[0],F_GETFL); assert(flags>=0 && !fcntl(pair[0],F_SETFL,flags|O_NONBLOCK));
 uint8_t in[64],out[64]; lce_connection c; lce_orbis_socket s={pair[0],os_send,os_recv,0}; int count=0;
 lce_connection_init(&c,in,sizeof in,out,sizeof out); const uint8_t request[]={4,5};
 assert(lce_connection_enqueue(&c,request,sizeof request));
 assert(lce_connection_pump(&c,lce_orbis_receive,lce_orbis_send,&s,got,&count)==LCE_CONN_OPEN);
 const uint8_t assigned_and_response[]={3,0,0,0,2,9,8};
 assert(write(pair[1],assigned_and_response,sizeof assigned_and_response)==sizeof assigned_and_response);
 assert(lce_connection_pump(&c,lce_orbis_receive,lce_orbis_send,&s,got,&count)==LCE_CONN_OPEN);
 assert(count==0 && c.reader.assigned_id==3);
 uint8_t wire[6]; const uint8_t expected[]={0,0,0,2,4,5}; assert(read(pair[1],wire,sizeof wire)==sizeof wire && !memcmp(wire,expected,sizeof wire));
 assert(lce_connection_pump(&c,lce_orbis_receive,lce_orbis_send,&s,got,&count)==LCE_CONN_OPEN && count==1 && c.reader.assigned_id==3);
 close(pair[1]); assert(lce_connection_pump(&c,lce_orbis_receive,lce_orbis_send,&s,got,&count)==LCE_CONN_CLOSED); close(pair[0]);
 puts("socket integration passed");
}
