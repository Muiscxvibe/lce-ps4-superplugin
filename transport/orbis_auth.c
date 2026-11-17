#include "orbis_auth.h"
#include <orbis/libkernel.h>
#include <orbis/Net.h>
#include <string.h>

/* Resolve from the same module instance that owns each context. No callback
 * disables certificate validation and redirects are disabled by auth_https.
 * The native firmware's certificate/hostname behavior needs console testing.
 */
static int symbol(int module,const char *name,void *destination,size_t size) {
    void *address=0;
    if (size!=sizeof address || sceKernelDlsym(module,name,&address)<0 || !address) return 0;
    memcpy(destination,&address,size);
    return 1;
}
#define BIND(module,field,name) \
    do { if (!symbol((module),(name),&(field),sizeof(field))) return 0; } while (0)

/* The game's imported library can be invisible to GetModuleList and may be
 * named libSceSsl2. Identify the real module from its resolved import target,
 * then verify its export resolves to the same address before using its ID. */
typedef struct {
    size_t size; char name[256]; int32_t id; uint32_t tls_index;
    void *tls_init_addr; uint32_t tls_init_size,tls_size,tls_offset,tls_align;
    void *init_proc_addr,*fini_proc_addr; uint64_t reserved1,reserved2;
    void *eh_frame_hdr_addr,*eh_frame_addr; uint32_t eh_frame_hdr_size,eh_frame_size;
    OrbisKernelModuleSegmentInfo segmentInfo[4]; uint32_t segmentCount,ref_count;
} lce_module_info_ex;
_Static_assert(sizeof(lce_module_info_ex)==424,"unexpected module ABI");
extern int32_t lce_module_from_address(void *,uint32_t,lce_module_info_ex *)
    __asm__("sceKernelGetModuleInfoFromAddr");
static int module_from_game_import(uintptr_t base,uintptr_t plt,uintptr_t slot,const unsigned char expected[6],
                                   const char *export_name,const char *module_prefix,int *error) {
    *error=1;
    if (!base || base>UINTPTR_MAX-slot-8) return -1;
    void *lo=0,*hi=0;int32_t prot=0;
    *error=2;
    if (sceKernelQueryMemoryProtection((void *)(base+plt),&lo,&hi,&prot) || (prot&5)!=5 ||
        (uintptr_t)lo>base+plt || (uintptr_t)hi<base+plt+6 ||
        memcmp((const void *)(base+plt),expected,6)) return -1;
    *error=3;lo=hi=0;prot=0;
    if (sceKernelQueryMemoryProtection((void *)(base+slot),&lo,&hi,&prot) || !(prot&1) ||
        (uintptr_t)lo>base+slot || (uintptr_t)hi<base+slot+8) return -1;
    uintptr_t target=__atomic_load_n((uintptr_t *)(base+slot),__ATOMIC_ACQUIRE);
    *error=4;
    if (!target) return -1;
    lce_module_info_ex owner;memset(&owner,0,sizeof owner);owner.size=sizeof owner;
    *error=5;
    if (lce_module_from_address((void *)target,1,&owner)) return -1;
    *error=6;
    if (!memchr(owner.name,0,sizeof owner.name) || strncmp(owner.name,module_prefix,strlen(module_prefix))) return -1;
    void *resolved=0;
    *error=7;
    if (sceKernelDlsym(owner.id,export_name,&resolved) || (uintptr_t)resolved!=target) return -1;
    *error=0;
    return owner.id;
}

/* A module may already be present in the game, and a system module ID is
 * different from the handle accepted by sceKernelDlsym. Enumerate the actual
 * handles after each load attempt; never treat the sysmodule return as one. */
static int module_name_matches(const char *actual,const char *name) {
    size_t n=strlen(name);
    if (n<5 || strcmp(name+n-5,".sprx")) return 0;
    n-=5;
    return !strncmp(actual,name,n) &&
        (!strcmp(actual+n,".sprx") || !strcmp(actual+n,".prx") || !actual[n]);
}
static int find_module(const char *name) {
    OrbisKernelModule modules[256];
    size_t count=0;
    if (sceKernelGetModuleList(modules,256,&count)<0 || count>256) return -1;
    for (size_t i=0;i<count;++i) {
        OrbisKernelModuleInfo info;
        memset(&info,0,sizeof info);info.size=sizeof info;
        if (sceKernelGetModuleInfo(modules[i],&info)==0 &&
            memchr(info.name,0,sizeof info.name) && module_name_matches(info.name,name)) return (int)modules[i];
    }
    return -1;
}
static int acquire_module(const char *name,const char *path,uint32_t id,int *error) {
    int handle=find_module(name);
    if (handle>=0) return handle;
    int loaded=(int)sceKernelLoadStartModule(name,0,0,0,0,0);
    if (loaded>=0) return loaded;
    *error=loaded;
    loaded=(int)sceKernelLoadStartModule(path,0,0,0,0,0);
    if (loaded>=0) return loaded;
    *error=loaded;
    int sys=find_module("libSceSysmodule.sprx");
    int (*load_internal)(uint32_t)=0;
    if (sys>=0 && symbol(sys,"sceSysmoduleLoadModuleInternal",&load_internal,sizeof load_internal)) {
        int rc=load_internal(id);
        handle=find_module(name);
        if (handle>=0) return handle;
        if (rc<0) *error=rc;
    }
    return -1;
}

int lce_orbis_auth_init(lce_orbis_auth *owner,uintptr_t game_base,const char *pem,size_t pem_length) {
    if (!owner || owner->attempted || !pem || !pem_length) return 0;
    owner->attempted=1;owner->pool=owner->ssl=owner->http=-1;owner->stage=1;
    static const unsigned char ssl_plt[6]={0xff,0x25,0x8a,0xed,0x4d,0x00};
    static const unsigned char http_plt[6]={0xff,0x25,0xd2,0xed,0x4d,0x00};
    int ssl_mod=module_from_game_import(game_base,0xe6def0,0x134cc80,ssl_plt,"sceSslInit","libSceSsl",&owner->ssl_import_error);
    if (ssl_mod<0) ssl_mod=acquire_module("libSceSsl.sprx","/system/common/lib/libSceSsl.sprx",0x8000000b,&owner->native_error);
    if (ssl_mod<0) return 0;
    owner->stage=8;
    int http_mod=module_from_game_import(game_base,0xe6de60,0x134cc38,http_plt,"sceHttpInit","libSceHttp",&owner->http_import_error);
    if (http_mod<0) http_mod=acquire_module("libSceHttp.sprx","/system/common/lib/libSceHttp.sprx",0x8000000a,&owner->native_error);
    if (http_mod<0) return 0;
    owner->stage=2;
    int (*ssl_init)(size_t)=0;
    int (*http_init)(int,int,size_t)=0;
    BIND(ssl_mod,ssl_init,"sceSslInit");
    BIND(http_mod,http_init,"sceHttpInit");
    BIND(http_mod,owner->api.load_cert,"sceHttpsLoadCert");
    BIND(http_mod,owner->api.create_template,"sceHttpCreateTemplate");
    BIND(http_mod,owner->api.set_redirect,"sceHttpSetAutoRedirect");
    BIND(http_mod,owner->api.set_connect_timeout,"sceHttpSetConnectTimeOut");
    BIND(http_mod,owner->api.set_send_timeout,"sceHttpSetSendTimeOut");
    BIND(http_mod,owner->api.set_recv_timeout,"sceHttpSetRecvTimeOut");
    BIND(http_mod,owner->api.connect_url,"sceHttpCreateConnectionWithURL");
    BIND(http_mod,owner->api.request_url,"sceHttpCreateRequestWithURL");
    BIND(http_mod,owner->api.header,"sceHttpAddRequestHeader");
    BIND(http_mod,owner->api.send,"sceHttpSendRequest");
    BIND(http_mod,owner->api.status,"sceHttpGetStatusCode");
    BIND(http_mod,owner->api.read,"sceHttpReadData");
    BIND(http_mod,owner->api.delete_request,"sceHttpDeleteRequest");
    BIND(http_mod,owner->api.delete_connection,"sceHttpDeleteConnection");
    BIND(http_mod,owner->api.delete_template,"sceHttpDeleteTemplate");
    owner->stage=3;owner->pool=sceNetPoolCreate("lce_auth",1024*1024,0);
    if (owner->pool<0) {owner->native_error=owner->pool;return 0;}
    owner->stage=4;
    owner->ssl=ssl_init(1024*1024);
    if (owner->ssl<0) {owner->native_error=owner->ssl;return 0;}
    owner->stage=5;
    owner->http=http_init(owner->pool,owner->ssl,1024*1024);
    if (owner->http<0) {owner->native_error=owner->http;return 0;}
    owner->stage=6;
    if (!lce_auth_https_trust(&owner->trust,&owner->api,owner->http,pem,pem_length)) return 0;
    owner->stage=7;
    owner->ready=1;
    return 1;
}
int lce_orbis_auth_request(lce_orbis_auth *owner,const char *url,lce_auth_op op,
        const char *username,const char *secret,const char *device,int remember,lce_auth_result *result) {
    if (!owner || !owner->ready) {
        if (result) memset(result,0,sizeof *result);
        return 0;
    }
    return lce_auth_https(&owner->api,&owner->trust,url,op,username,secret,device,remember,result);
}
