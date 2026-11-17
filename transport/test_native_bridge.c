#include "native_bridge.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct { int held,closed,got,recv_once; uint8_t sent[16]; size_t sent_n; } Test;
static void lock_(void *p){Test *t=p;assert(!t->held);t->held=1;}
static void unlock_(void *p){Test *t=p;assert(t->held);t->held=0;}
static void push(void *p,const uint8_t *data,uint32_t n,int from_host){Test *t=p;assert(t->held&&n==2&&data[0]==8&&data[1]==9&&from_host);++t->got;}
static int open_(void *p){(void)p;return 5;}
static int start_(void *p,int fd,const uint8_t ip[4],uint16_t port){(void)p;(void)fd;(void)ip;(void)port;return 1;}
static int poll_(void *p,int fd){(void)p;(void)fd;return 1;}
static int recv_(void *p,int fd,uint8_t *out,size_t cap){Test *t=p;(void)fd;const uint8_t msg[]={2,0,0,0,2,8,9};if(t->recv_once++)return 0;assert(cap>=sizeof msg);memcpy(out,msg,sizeof msg);return sizeof msg;}
static int send_(void *p,int fd,const uint8_t *b,size_t n){Test *t=p;(void)fd;assert(n<=sizeof t->sent);memcpy(t->sent,b,n);t->sent_n=n;return (int)n;}
static void close_(void *p,int fd){Test *t=p;assert(fd==5);++t->closed;}
int main(void){Test t={0};lce_session_io io={&t,open_,start_,poll_,recv_,send_,close_};lce_session_transport s;uint8_t in[64],out[64],ip[4]={1,2,3,4};
 assert(lce_session_start(&s,&io,ip,25565,0,100,in,sizeof in,out,sizeof out));
 lce_native_bridge b={0};lce_native_bridge_ops ops={&t,lock_,unlock_,push};assert(lce_native_bridge_attach(&b,&s,&t,&ops));
 uint8_t packet[]={6,7};assert(lce_native_bridge_send(&b,packet,2));assert(lce_native_bridge_tick(&b,1)==LCE_SESSION_ACTIVE);
 const uint8_t expected[]={0,0,0,2,6,7};assert(t.got==1&&t.sent_n==sizeof expected&&!memcmp(t.sent,expected,sizeof expected));
 lce_native_bridge_detach(&b);assert(t.closed==1&&!lce_native_bridge_send(&b,packet,2));lce_native_bridge_detach(&b);assert(t.closed==1);
 puts("native bridge tests passed");}
