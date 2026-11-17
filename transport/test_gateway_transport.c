#include "session_transport.h"
#include <assert.h>
#include <string.h>

typedef struct {uint8_t inbound[41],sent[48];size_t at,used;int closed;} Fake;
static int open_(void *ctx) {(void)ctx;return 4;}
static int start_(void *ctx,int fd,const uint8_t ip[4],uint16_t port) {(void)ctx;(void)ip;return fd==4&&port==25565?1:-1;}
static int poll_(void *ctx,int fd) {(void)ctx;(void)fd;return 1;}
static int read_(void *ctx,int fd,uint8_t *out,size_t max) {
    Fake *f=ctx;assert(fd==4);
    if (f->at==sizeof f->inbound) return 0;
    size_t n=sizeof f->inbound-f->at;if(n>3)n=3;if(n>max)n=max;
    memcpy(out,f->inbound+f->at,n);f->at+=n;return (int)n;
}
static int write_(void *ctx,int fd,const uint8_t *in,size_t n) {
    Fake *f=ctx;assert(fd==4);if(n>2)n=2;assert(f->used+n<=sizeof f->sent);
    memcpy(f->sent+f->used,in,n);f->used+=n;return (int)n;
}
static void close_(void *ctx,int fd) {Fake *f=ctx;assert(fd==4);++f->closed;}
static int frame(void *ctx,const uint8_t *p,size_t n) {(void)ctx;(void)p;(void)n;return 1;}
int main(void) {
    Fake f={0};memcpy(f.inbound,"LCEAUTH1",8);for(unsigned i=0;i<32;++i)f.inbound[8+i]=(uint8_t)i;f.inbound[40]=5;
    lce_session_io io={&f,open_,start_,poll_,read_,write_,close_};
    lce_session_transport s;uint8_t in[64],out[64],ip[4]={127,0,0,1};
    const char *id="01010101010101010101010101010101";
    const char *key="abababababababababababababababababababababababababababababababab";
    assert(lce_session_start_authenticated(&s,&io,ip,25565,0,1000,in,sizeof in,out,sizeof out,id,key));
    for (unsigned i=0;i<200&&!s.connection.reader.has_id;++i)
        assert(lce_session_tick(&s,i,frame,0)==LCE_SESSION_ACTIVE);
    const uint8_t expected[]={0x35,0x26,0xc7,0x8c,0xa4,0x50,0xf9,0xea,0x29,0xf2,0xc9,0x7d,0xa2,0x44,0x2f,0xf2,0x8f,0x35,0xbf,0xe1,0x54,0xfa,0x19,0xb7,0xfe,0x27,0xbe,0x95,0xd1,0x84,0x4b,0x10};
    assert(s.connection.reader.has_id && s.connection.reader.assigned_id==5 && f.used==48);
    for (unsigned i=0;i<16;++i) assert(f.sent[i]==1);
    assert(!memcmp(f.sent+16,expected,sizeof expected));
    lce_session_close(&s);assert(f.closed==1);
    memset(&f,0,sizeof f);memcpy(f.inbound,"NOTAUTH!",8);
    assert(lce_session_start_authenticated(&s,&io,ip,25565,0,1000,in,sizeof in,out,sizeof out,id,key));
    for (unsigned i=0;i<20 && s.state!=LCE_SESSION_FAILED;++i) lce_session_tick(&s,i,frame,0);
    assert(s.state==LCE_SESSION_FAILED && f.closed==1 && !f.used);
}
