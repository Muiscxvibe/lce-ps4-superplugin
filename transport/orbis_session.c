#include "orbis_session.h"
#include <orbis/Net.h>
#include <string.h>
#define WOULD_BLOCK 0x80410123u
#define IN_PROGRESS 0x80410124u
#define ALREADY 0x80410125u
#define IS_CONNECTED 0x80410138u
#define INTERRUPTED 0x80410104u
static int status(int raw) {
    if (!raw || (uint32_t)raw==IS_CONNECTED) return 1;
    if ((uint32_t)raw==WOULD_BLOCK || (uint32_t)raw==IN_PROGRESS ||
        (uint32_t)raw==ALREADY || (uint32_t)raw==INTERRUPTED) return 0;
    return -1;
}
static int open_socket(void *p) {
    lce_orbis_session *s=p;
    int fd=sceNetSocket("lce_server",ORBIS_NET_AF_INET,ORBIS_NET_SOCK_STREAM,0);
    if (fd<0) { s->last_raw=fd; return -1; }
    lce_orbis_bind_socket(&s->socket,fd); return fd;
}
static int connect_again(lce_orbis_session *s,int fd) {
    int rc=sceNetConnect(fd,(const OrbisNetSockaddr *)s->sockaddr,sizeof s->sockaddr);
    s->last_raw=rc; return status(rc);
}
static int start_connect(void *p,int fd,const uint8_t ip[4],uint16_t port) {
    lce_orbis_session *s=p; int enabled=1;
    int rc=sceNetSetsockopt(fd,0xffff,0x1200,&enabled,sizeof enabled);
    if (rc<0) { s->last_raw=rc; return -1; }
    memset(s->sockaddr,0,sizeof s->sockaddr); s->sockaddr[0]=16; s->sockaddr[1]=2;
    s->sockaddr[2]=(uint8_t)(port>>8); s->sockaddr[3]=(uint8_t)port;
    memcpy(s->sockaddr+4,ip,4); return connect_again(s,fd);
}
static int poll_connect(void *p,int fd) { return connect_again(p,fd); }
static int receive(void *p,int fd,uint8_t *b,size_t n) {
    lce_orbis_session *s=p; if (s->socket.fd!=fd) return -1;
    return lce_orbis_receive(&s->socket,b,n);
}
static int send_data(void *p,int fd,const uint8_t *b,size_t n) {
    lce_orbis_session *s=p; if (s->socket.fd!=fd) return -1;
    return lce_orbis_send(&s->socket,b,n);
}
static void close_socket(void *p,int fd) {
    lce_orbis_session *s=p; s->last_raw=sceNetSocketClose(fd); s->socket.fd=-1;
}
lce_session_io lce_orbis_session_io(lce_orbis_session *s) {
    lce_session_io io={s,open_socket,start_connect,poll_connect,receive,send_data,close_socket};
    return io;
}
int lce_orbis_session_socket_api_ready(void) {
    int fd=sceNetSocket("lce_api_check",ORBIS_NET_AF_INET,ORBIS_NET_SOCK_STREAM,0);
    if (fd<0) return 0;
    return sceNetSocketClose(fd)==0;
}
