#include "session_transport.h"
#include <string.h>
static void close_fd(lce_session_transport *s) {
    if (s->fd >= 0) { int fd=s->fd; s->fd=-1; s->io.close(s->io.ctx,fd); }
    lce_gateway_clear(&s->gateway);
}
static int recv_step(void *p,uint8_t *buf,size_t n) {
    lce_session_transport *s=p; return s->io.recv(s->io.ctx,s->fd,buf,n);
}
static int send_step(void *p,const uint8_t *buf,size_t n) {
    lce_session_transport *s=p; return s->io.send(s->io.ctx,s->fd,buf,n);
}
int lce_session_start(lce_session_transport *s,const lce_session_io *io,
                      const uint8_t ip[4],uint16_t port,uint64_t now,uint32_t timeout,
                      uint8_t *in,size_t in_size,uint8_t *out,size_t out_size) {
    if (!s||!io||!io->open||!io->start||!io->poll||!io->recv||!io->send||!io->close||
        !ip||!port||!timeout||now>UINT64_MAX-timeout||!in||!in_size||!out||out_size<5) return 0;
    /* Arguments must not alias s: initialization clears the full object. */
    lce_session_io saved=*io; uint8_t address[4]; memcpy(address,ip,4);
    memset(s,0,sizeof *s); s->fd=-1; s->io=saved; s->deadline=now+timeout; s->last_tick=now;
    lce_connection_init(&s->connection,in,in_size,out,out_size);
    s->fd=s->io.open(s->io.ctx);
    if (s->fd<0) { s->state=LCE_SESSION_FAILED; return 0; }
    int rc=s->io.start(s->io.ctx,s->fd,address,port);
    if (rc<0||rc>1) { close_fd(s); s->state=LCE_SESSION_FAILED; return 0; }
    s->state=rc?LCE_SESSION_ACTIVE:LCE_SESSION_CONNECTING; return 1;
}
int lce_session_start_authenticated(lce_session_transport *s,const lce_session_io *io,
        const uint8_t ip[4],uint16_t port,uint64_t now,uint32_t timeout,
        uint8_t *in,size_t in_size,uint8_t *out,size_t out_size,const char *id,const char *key) {
    lce_gateway_handshake gateway;
    if (!lce_gateway_init(&gateway,id,key)) return 0;
    if (!lce_session_start(s,io,ip,port,now,timeout,in,in_size,out,out_size)) {
        lce_gateway_clear(&gateway);return 0;
    }
    s->gateway=gateway;s->gateway_enabled=1;lce_gateway_clear(&gateway);return 1;
}
lce_session_state lce_session_tick(lce_session_transport *s,uint64_t now,lce_frame_callback callback,void *ctx) {
    if (!s) return LCE_SESSION_FAILED;
    if (s->state!=LCE_SESSION_ACTIVE && s->state!=LCE_SESSION_CONNECTING) return s->state;
    if (now<s->last_tick || (!s->connection.reader.has_id && now>=s->deadline)) {
        close_fd(s); return s->state=LCE_SESSION_FAILED;
    }
    s->last_tick=now;
    if (s->state==LCE_SESSION_CONNECTING) {
        int rc=s->io.poll(s->io.ctx,s->fd);
        if (rc<0||rc>1) { close_fd(s); return s->state=LCE_SESSION_FAILED; }
        if (!rc) return s->state;
        s->state=LCE_SESSION_ACTIVE;
    }
    if (s->gateway_enabled) {
        int rc=lce_gateway_step(&s->gateway,recv_step,send_step,s);
        if (rc<0) {close_fd(s);return s->state=LCE_SESSION_FAILED;}
        if (!rc) return s->state;
        s->gateway_enabled=0;
        lce_gateway_clear(&s->gateway);
    }
    lce_conn_state result=lce_connection_pump(&s->connection,recv_step,send_step,s,callback,ctx);
    if (result==LCE_CONN_OPEN) return s->state;
    close_fd(s);
    return s->state = result==LCE_CONN_REJECTED ? LCE_SESSION_REJECTED :
                      result==LCE_CONN_CLOSED ? LCE_SESSION_CLOSED : LCE_SESSION_FAILED;
}
int lce_session_send(lce_session_transport *s,const uint8_t *packet,size_t n) {
    return s && (s->state==LCE_SESSION_CONNECTING||s->state==LCE_SESSION_ACTIVE) &&
           lce_connection_enqueue(&s->connection,packet,n);
}
void lce_session_close(lce_session_transport *s) {
    if (!s) return;
    if (s->state==LCE_SESSION_CONNECTING||s->state==LCE_SESSION_ACTIVE) close_fd(s);
    s->state=LCE_SESSION_CLOSED;
}
