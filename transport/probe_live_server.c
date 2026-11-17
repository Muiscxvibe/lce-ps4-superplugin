#define _POSIX_C_SOURCE 200809L
#include "native_bridge.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
typedef struct { int error; } Context;
static int op(void *p){(void)p;int fd=socket(AF_INET,SOCK_STREAM,0);if(fd>=0){int flags=fcntl(fd,F_GETFL);if(flags<0||fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0){close(fd);return -1;}}return fd;}
static int start(void *p,int fd,const uint8_t ip[4],uint16_t port){(void)p;struct sockaddr_in a={0};a.sin_family=AF_INET;a.sin_port=htons(port);memcpy(&a.sin_addr,ip,4);if(!connect(fd,(struct sockaddr*)&a,sizeof a))return 1;return errno==EINPROGRESS?0:-1;}
static int poll_(void *p,int fd){(void)p;struct sockaddr_in a;socklen_t n=sizeof a;if(!getpeername(fd,(struct sockaddr*)&a,&n))return 1;if(errno==ENOTCONN||errno==EINVAL)return 0;return -1;}
static int recv_(void *p,int fd,uint8_t *b,size_t n){(void)p;int rc=(int)recv(fd,b,n,0);if(rc>0)return rc;if(!rc)return -2;return errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR?0:-1;}
static int send_(void *p,int fd,const uint8_t *b,size_t n){(void)p;int rc=(int)send(fd,b,n,0);if(rc>0)return rc;return rc<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR)?0:-1;}
static void close_(void *p,int fd){(void)p;close(fd);}
static void lock_(void *p){(void)p;}
static void unlock_(void *p){(void)p;}
static void push_(void *p,const uint8_t *b,uint32_t n,int from_host){int *seen=p;if(from_host&&n>=3&&b[0]==2&&b[1]==9&&b[2]==0x6f)*seen=1;}
static uint64_t now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+(uint64_t)t.tv_nsec/1000000;}
int main(void){Context ctx={0};lce_session_io io={&ctx,op,start,poll_,recv_,send_,close_};lce_session_transport s;static uint8_t in[3*1024*1024],out[65536];uint8_t ip[]={127,0,0,1};uint64_t begin=now();int seen=0,sent=0;
 if(!lce_session_start(&s,&io,ip,25566,begin,5000,in,sizeof in,out,sizeof out))return 2;
 lce_native_bridge b={0};lce_native_bridge_ops ops={&ctx,lock_,unlock_,push_};
 if(!lce_native_bridge_attach(&b,&s,&seen,&ops))return 2;
 while(now()-begin<10000){uint64_t t=now();lce_session_state state=lce_native_bridge_tick(&b,t);if(state!=LCE_SESSION_CONNECTING&&state!=LCE_SESSION_ACTIVE)break;
 if(!sent&&s.connection.reader.has_id&&t-begin>=1000){const uint8_t packet[]={0xe8,0x09,0x6f,0,5,0,'P',0,'r',0,'o',0,'b',0,'e'};if(!lce_native_bridge_send(&b,packet,sizeof packet))break;sent=1;}
 if(seen){printf("native bridge received PS4 prelogin response; assigned id %u\n",s.connection.reader.assigned_id);lce_native_bridge_detach(&b);return 0;}
 struct timespec pause={0,10000000};nanosleep(&pause,0);}
 fprintf(stderr,"live server probe failed: state=%d id=%d sent=%d seen=%d\n",s.state,s.connection.reader.has_id,sent,seen);lce_native_bridge_detach(&b);return 1;}
