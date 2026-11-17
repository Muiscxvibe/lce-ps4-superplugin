#include "orbis_auth.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <orbis/libkernel.h>

static int count, loads, use_existing, use_internal, internal_calls;
static int ssl_loaded, http_loaded;
static unsigned char *game_image;
static void *ssl_target,*http_target;
int32_t sceKernelGetModuleList(OrbisKernelModule *array,size_t size,size_t *available) {
    assert(size>=3);size_t n=0;array[n++]=3;
    if (use_existing || ssl_loaded) array[n++]=1;
    if (use_existing || http_loaded) array[n++]=2;
    *available=n;return 0;
}
int32_t sceKernelGetModuleInfo(OrbisKernelModule h,OrbisKernelModuleInfo *info) {
    assert(info->size==sizeof *info);
    if (h==0 && game_image) {
        strcpy(info->name,"Minecraft.Client.elf");info->segmentCount=1;
        info->segmentInfo[0].address=game_image;info->segmentInfo[0].size=0x1350000;
        return 0;
    }
    strcpy(info->name,h==1?"libSceSsl.prx":h==2?"libSceHttp":"libSceSysmodule.prx");
    return 0;
}
/* No game import in this fixture: exercise the existing module fallback. */
int32_t mock_module_from_addr(void *address,uint32_t flags,void *info) __asm__("sceKernelGetModuleInfoFromAddr");
int32_t mock_module_from_addr(void *address,uint32_t flags,void *info) {
    struct Head { size_t size;char name[256];int32_t id; };
    struct Head *h=info;assert(flags==1 && h->size==424);
    if (game_image && (address==ssl_target || address==http_target)) {
        strcpy(h->name,address==ssl_target?"libSceSsl2.sprx":"libSceHttp.sprx");
        h->id=address==ssl_target?1:2;return 0;
    }
    return -1;
}
int32_t sceKernelQueryMemoryProtection(void *addr,void **lo,void **hi,int32_t *prot) {
    if (!game_image) return -1;
    *lo=game_image;*hi=game_image+0x1350000;*prot=5;(void)addr;return 0;
}
static int load_internal(uint32_t id) {
    ++internal_calls;
    if (id==0x8000000b) ssl_loaded=1;
    else if (id==0x8000000a) http_loaded=1;
    else return -1;
    return 0;
}
static int cert(int h,int n,void *p,void *c,void *k) {(void)c;(void)k;return h==12&&n==1&&p?0:-1;}
static int tmpl(int h,const char *s,int v,int p) {(void)s;(void)v;(void)p;return h==12?4:-1;}
static int redirect(int id,int on) {(void)id;return on?-1:0;}
static int timeout(int id,uint32_t us) {(void)id;return us?0:-1;}
static int conn(int id,const char *url,int keep) {return id==4&&url&&keep?5:-1;}
static int req(int id,int method,const char *url,uint64_t size) {return id==5&&method==1&&url&&size?6:-1;}
static int header(int id,const char *n,const char *v,int mode) {(void)n;(void)v;(void)mode;return id==6?0:-1;}
static int send_data(int id,const void *p,size_t n) {return id==6&&p&&n?0:-1;}
static int status(int id,int *s) {*s=200;return id==6?0:-1;}
static int read_data(int id,void *out,uint32_t n) {
    const char response[]="{\"ok\":true,\"game_uid\":\"24\"}";
    assert(id==6);
    if (count++) return 0;
    assert(n>=sizeof(response)-1);memcpy(out,response,sizeof(response)-1);return sizeof(response)-1;
}
static int del(int id) {return id>=0?0:-1;}
static int ssl_init(size_t n) {return n?11:-1;}
static int http_init(int pool,int ssl,size_t n) {return pool==10&&ssl==11&&n?12:-1;}
uint32_t sceKernelLoadStartModule(const char *p,size_t n,const void *args,uint32_t flags,void *a,void *b) {
    (void)n;(void)args;(void)flags;(void)a;(void)b;loads++;
    if (use_internal) return (uint32_t)-1;
    return strstr(p,"libSceSsl")?1:2;
}
int32_t sceNetPoolCreate(const char *name,int32_t size,int32_t flags) {(void)flags;return name&&size?10:-1;}
int32_t sceKernelDlsym(int32_t module,const char *name,void **address) {
    (void)module;
#define MAP(n,f) if (!strcmp(name,(n))) { union { void *p; __typeof__(&f) fn; } u;u.fn=&f;*address=u.p;return 0; }
    MAP("sceSysmoduleLoadModuleInternal",load_internal)
    MAP("sceSslInit",ssl_init) MAP("sceHttpInit",http_init)
    MAP("sceHttpsLoadCert",cert) MAP("sceHttpCreateTemplate",tmpl)
    MAP("sceHttpSetAutoRedirect",redirect) MAP("sceHttpSetConnectTimeOut",timeout)
    MAP("sceHttpSetSendTimeOut",timeout) MAP("sceHttpSetRecvTimeOut",timeout)
    MAP("sceHttpCreateConnectionWithURL",conn) MAP("sceHttpCreateRequestWithURL",req)
    MAP("sceHttpAddRequestHeader",header) MAP("sceHttpSendRequest",send_data)
    MAP("sceHttpGetStatusCode",status) MAP("sceHttpReadData",read_data)
    MAP("sceHttpDeleteRequest",del) MAP("sceHttpDeleteConnection",del)
    MAP("sceHttpDeleteTemplate",del)
#undef MAP
    return -1;
}
int main(void) {
    lce_orbis_auth owner={0};lce_auth_result result;
    const char *device="abababababababababababababababababababababababababababababababab";
    assert(!lce_orbis_auth_request(&owner,"https://test/v1/auth",LCE_AUTH_LOGIN,"Codey","password123",device,0,&result));
    assert(lce_orbis_auth_init(&owner,0,"CA-PEM",6));
    assert(!lce_orbis_auth_init(&owner,0,"CA-PEM",6) && loads==2);
    assert(!lce_orbis_auth_request(&owner,"http://test/v1/auth",LCE_AUTH_LOGIN,"Codey","password123",device,0,&result));
    assert(lce_orbis_auth_request(&owner,"https://test/v1/auth",LCE_AUTH_LOGIN,"Codey","password123",device,0,&result));
    assert(result.accepted && result.game_uid==24);
    lce_orbis_auth existing={0}; use_existing=1;
    assert(lce_orbis_auth_init(&existing,0,"CA-PEM",6) && loads==2);
    lce_orbis_auth fallback={0}; use_existing=0;use_internal=1;
    assert(lce_orbis_auth_init(&fallback,0,"CA-PEM",6));
    assert(loads==6 && internal_calls==2 && ssl_loaded && http_loaded);
    game_image=calloc(1,0x1350000);assert(game_image);
    const unsigned char ssl_plt[]={0xff,0x25,0x8a,0xed,0x4d,0x00};
    const unsigned char http_plt[]={0xff,0x25,0xd2,0xed,0x4d,0x00};
    memcpy(game_image+0xe6def0,ssl_plt,6);memcpy(game_image+0xe6de60,http_plt,6);
    union { void *p; __typeof__(&ssl_init) fn; } su;su.fn=ssl_init;ssl_target=su.p;
    union { void *p; __typeof__(&http_init) fn; } hu;hu.fn=http_init;http_target=hu.p;
    memcpy(game_image+0x134cc80,&ssl_target,sizeof ssl_target);
    memcpy(game_image+0x134cc38,&http_target,sizeof http_target);
    lce_orbis_auth imported={0};ssl_loaded=http_loaded=0;
    assert(lce_orbis_auth_init(&imported,(uintptr_t)game_image,"CA-PEM",6));
    assert(imported.ssl_import_error==0 && imported.http_import_error==0);
    assert(loads==6 && internal_calls==2); /* no extra loads */
    free(game_image);
}
