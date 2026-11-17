#include "session_transport.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct { int opened,started,polls,closed,fail,at,assigned; unsigned char sent[32]; size_t length; } Fake;
static int op(void *p){Fake *f=p; ++f->opened; return 5;}
static int start(void *p,int fd,const uint8_t ip[4],uint16_t port){Fake *f=p; assert(fd==5&&ip[0]==192&&port==25565); ++f->started; return f->fail?-1:0;}
static int poll(void *p,int fd){Fake *f=p; assert(fd==5); return ++f->polls>=2;}
static int recv_(void *p,int fd,uint8_t *b,size_t n){Fake *f=p; (void)fd; if(f->at==1)return -2; if(f->at==2)return 0; if(!f->assigned++){assert(n);b[0]=3;return 1;} return 0;}
static int send_(void *p,int fd,const uint8_t *b,size_t n){Fake *f=p; (void)fd; assert(n<=sizeof f->sent-f->length); memcpy(f->sent+f->length,b,n); f->length+=n; return (int)n;}
static void close_(void *p,int fd){Fake *f=p; assert(fd==5); ++f->closed;}
static int frame(void *p,const uint8_t *b,size_t n){(void)p;(void)b;(void)n;return 1;}
int main(void) {
 Fake f={0}; lce_session_io io={&f,op,start,poll,recv_,send_,close_}; lce_session_transport s; uint8_t in[64],out[64],ip[]={192,168,1,165};
 assert(lce_session_start(&s,&io,ip,25565,10,10,in,sizeof in,out,sizeof out)); uint8_t msg[]={2,3}; assert(lce_session_send(&s,msg,sizeof msg));
 assert(lce_session_tick(&s,11,frame,0)==LCE_SESSION_CONNECTING && !f.length);
 assert(lce_session_tick(&s,12,frame,0)==LCE_SESSION_ACTIVE && f.length==6);
 uint8_t wire[]={0,0,0,2,2,3}; assert(!memcmp(wire,f.sent,6)); f.at=1;
 assert(lce_session_tick(&s,13,frame,0)==LCE_SESSION_CLOSED && f.closed==1); lce_session_close(&s); assert(f.closed==1);
 memset(&f,0,sizeof f); assert(lce_session_start(&s,&io,ip,25565,10,10,in,sizeof in,out,sizeof out));
 assert(lce_session_tick(&s,20,frame,0)==LCE_SESSION_FAILED && f.closed==1);
 memset(&f,0,sizeof f); f.at=2;
 assert(lce_session_start(&s,&io,ip,25565,10,10,in,sizeof in,out,sizeof out));
 assert(lce_session_tick(&s,11,frame,0)==LCE_SESSION_CONNECTING);
 assert(lce_session_tick(&s,12,frame,0)==LCE_SESSION_ACTIVE);
 assert(lce_session_tick(&s,20,frame,0)==LCE_SESSION_FAILED && f.closed==1);
 memset(&f,0,sizeof f);
 assert(lce_session_start(&s,&io,ip,25565,10,10,in,sizeof in,out,sizeof out));
 assert(lce_session_tick(&s,11,frame,0)==LCE_SESSION_CONNECTING);
 assert(lce_session_tick(&s,12,frame,0)==LCE_SESSION_ACTIVE);
 assert(lce_session_tick(&s,100,frame,0)==LCE_SESSION_ACTIVE);
 lce_session_close(&s); assert(f.closed==1);
 memset(&f,0,sizeof f); f.fail=1; assert(!lce_session_start(&s,&io,ip,25565,10,10,in,sizeof in,out,sizeof out) && f.closed==1);
 puts("session transport tests passed");
}
