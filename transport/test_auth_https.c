#include "auth_https.h"
#include <assert.h>
#include <string.h>

static int read_count, sent, deleted, redirects;
static const char response[]="{\"ok\":true,\"game_uid\":\"42\"}";
static int cert(int h,int n,void *roots,void *c,void *k) {
    struct Pem { const char *ptr; size_t size; };
    assert(h==3 && n==1 && roots && !c && !k);
    const struct Pem *pem=*(const struct Pem **)roots;
    assert(pem && pem->size==3 && !memcmp(pem->ptr,"pem",3));
    return 0;
}
static int tmpl(int h,const char *s,int v,int p) { (void)s;(void)v;(void)p; return h==3?4:-1; }
static int redirect(int id,int on) { (void)id; redirects++; return on?-1:0; }
static int timeout(int id,uint32_t us) { (void)id; return us?0:-1; }
static int conn(int id,const char *url,int keep) { return id==4&&keep&&url?5:-1; }
static int req(int id,int method,const char *url,uint64_t length) { return id==5&&method==1&&url&&length?6:-1; }
static int header(int id,const char *n,const char *v,int mode) { (void)n;(void)v;(void)mode;return id==6?0:-1; }
static int send_data(int id,const void *data,size_t size) { sent++; return id==6&&data&&size?0:-1; }
static int status(int id,int *code) { *code=200;return id==6?0:-1; }
static int read_data(int id,void *out,uint32_t cap) {
    assert(id==6);
    if (read_count++) return 0;
    assert(cap>=sizeof(response)-1); memcpy(out,response,sizeof(response)-1); return sizeof(response)-1;
}
static int cleanup(int id) { deleted++;return id<0?-1:0; }
int main(void) {
    lce_https_api a={cert,tmpl,redirect,timeout,timeout,timeout,conn,req,header,send_data,status,read_data,cleanup,cleanup,cleanup};
    lce_https_trust trust={0}; lce_auth_result result;
    const char *device="abababababababababababababababababababababababababababababababab";
    assert(lce_auth_https_trust(&trust,&a,3,"pem",3));
    assert(!lce_auth_https(&a,&trust,"http://example/v1/auth",LCE_AUTH_LOGIN,"Codey","password123",device,0,&result));
    assert(sent==0);
    assert(lce_auth_https(&a,&trust,"https://example/v1/auth",LCE_AUTH_LOGIN,"Codey","password123",device,0,&result));
    assert(result.accepted&&result.game_uid==42&&sent==1&&redirects==1&&deleted==3);
    const char json[]="{\"ok\":true,\"game_uid\":\"42\",\"ticket_id\":\"00112233445566778899aabbccddeeff\",\"ticket_key\":\"abababababababababababababababababababababababababababababababab\"}";
    uint8_t frame[512]={0};size_t size=sizeof json-1;
    frame[0]=(uint8_t)(size>>24);frame[1]=(uint8_t)(size>>16);frame[2]=(uint8_t)(size>>8);frame[3]=(uint8_t)size;
    memcpy(frame+4,json,size);
    assert(lce_auth_parse(frame,size+4,&result));
    assert(strlen(result.ticket_id)==32 && strlen(result.ticket_key)==64);
    assert(lce_auth_build(frame,sizeof frame,&size,LCE_AUTH_LOGIN_OR_REGISTER,"Codey","password123",device,1));
    frame[size]=0;assert(strstr((char *)frame+4,"\"game_ticket\":true"));
    return 0;
}
