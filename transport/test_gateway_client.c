#include "session_transport.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

static int open_(void *p) {(void)p;return socket(AF_INET,SOCK_STREAM,0);}
static int start_(void *p,int fd,const uint8_t ip[4],uint16_t port) {
    (void)p;struct sockaddr_in addr={0};addr.sin_family=AF_INET;addr.sin_port=htons(port);
    __builtin_memcpy(&addr.sin_addr,ip,4);
    if(connect(fd,(struct sockaddr *)&addr,sizeof addr))return -1;
    if(fcntl(fd,F_SETFL,fcntl(fd,F_GETFL)|O_NONBLOCK)<0)return -1;
    return 1;
}
static int poll_(void *p,int fd) {(void)p;(void)fd;return 1;}
static int read_(void *p,int fd,uint8_t *out,size_t n) {
    (void)p;if(n>3)n=3;ssize_t got=recv(fd,out,n,0);
    return got>0?(int)got:got==0?-2:errno==EAGAIN||errno==EWOULDBLOCK?0:-1;
}
static int write_(void *p,int fd,const uint8_t *in,size_t n) {
    (void)p;if(n>2)n=2;ssize_t sent=send(fd,in,n,0);
    return sent>0?(int)sent:sent<0&&(errno==EAGAIN||errno==EWOULDBLOCK)?0:-1;
}
static void close_(void *p,int fd) {(void)p;close(fd);}
static int frame(void *p,const uint8_t *bytes,size_t n) {(void)p;(void)bytes;(void)n;return 1;}
int main(int argc,char **argv) {
    if(argc!=4)return 1;
    unsigned long port=strtoul(argv[1],0,10);if(!port||port>65535)return 2;
    lce_session_io io={0,open_,start_,poll_,read_,write_,close_};
    lce_session_transport s;uint8_t in[128],out[128],ip[4]={127,0,0,1};
    if(!lce_session_start_authenticated(&s,&io,ip,(uint16_t)port,0,10000,in,sizeof in,out,sizeof out,argv[2],argv[3]))return 3;
    for(unsigned i=0;i<10000;++i) {
        if(lce_session_tick(&s,i,frame,0)!=LCE_SESSION_ACTIVE)return 4;
        if(s.connection.reader.has_id) {int success=s.connection.reader.assigned_id==5;lce_session_close(&s);return success?0:5;}
        usleep(1000);
    }
    lce_session_close(&s);return 6;
}
