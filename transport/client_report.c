#include "client_report.h"
#include <string.h>

#ifndef LCE_PLUGIN_BUILD
#define LCE_PLUGIN_BUILD "dev"
#endif

/* ---- SHA-256 / HMAC (FIPS 180-4, RFC 2104) ---- */
typedef struct { uint32_t h[8]; uint8_t buf[64]; uint64_t total; size_t used; } sha_ctx;
static const uint32_t K[64]={
0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
#define ROR(x,n) (((x)>>(n))|((x)<<(32-(n))))
static void sha_block(sha_ctx *c,const uint8_t *p) {
    uint32_t w[64],a,b,d,e,f,g,h,cc,t1,t2;
    for (int i=0;i<16;++i) w[i]=(uint32_t)p[4*i]<<24|(uint32_t)p[4*i+1]<<16|(uint32_t)p[4*i+2]<<8|p[4*i+3];
    for (int i=16;i<64;++i) { uint32_t s0=ROR(w[i-15],7)^ROR(w[i-15],18)^(w[i-15]>>3), s1=ROR(w[i-2],17)^ROR(w[i-2],19)^(w[i-2]>>10); w[i]=w[i-16]+s0+w[i-7]+s1; }
    a=c->h[0];b=c->h[1];cc=c->h[2];d=c->h[3];e=c->h[4];f=c->h[5];g=c->h[6];h=c->h[7];
    for (int i=0;i<64;++i) {
        t1=h+(ROR(e,6)^ROR(e,11)^ROR(e,25))+((e&f)^(~e&g))+K[i]+w[i];
        t2=(ROR(a,2)^ROR(a,13)^ROR(a,22))+((a&b)^(a&cc)^(b&cc));
        h=g;g=f;f=e;e=d+t1;d=cc;cc=b;b=a;a=t1+t2;
    }
    c->h[0]+=a;c->h[1]+=b;c->h[2]+=cc;c->h[3]+=d;c->h[4]+=e;c->h[5]+=f;c->h[6]+=g;c->h[7]+=h;
}
static void sha_init(sha_ctx *c) {
    static const uint32_t iv[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    memcpy(c->h,iv,sizeof iv); c->total=0; c->used=0;
}
static void sha_update(sha_ctx *c,const uint8_t *p,size_t n) {
    c->total+=n;
    while (n) {
        size_t take=64-c->used; if (take>n) take=n;
        memcpy(c->buf+c->used,p,take); c->used+=take; p+=take; n-=take;
        if (c->used==64) { sha_block(c,c->buf); c->used=0; }
    }
}
static void sha_final(sha_ctx *c,uint8_t out[32]) {
    uint64_t bits=c->total*8; uint8_t pad=0x80, zero=0, len[8];
    sha_update(c,&pad,1); while (c->used!=56) sha_update(c,&zero,1);
    for (int i=0;i<8;++i) len[i]=(uint8_t)(bits>>(56-8*i));
    sha_update(c,len,8);
    for (int i=0;i<8;++i) { out[4*i]=(uint8_t)(c->h[i]>>24); out[4*i+1]=(uint8_t)(c->h[i]>>16); out[4*i+2]=(uint8_t)(c->h[i]>>8); out[4*i+3]=(uint8_t)c->h[i]; }
}
void lce_sha256(const uint8_t *data,size_t len,uint8_t out[32]) { sha_ctx c; sha_init(&c); sha_update(&c,data,len); sha_final(&c,out); }
_Static_assert(sizeof(sha_ctx)<=sizeof(lce_sha256_ctx),"lce_sha256_ctx too small");
void lce_sha256_init(lce_sha256_ctx *c) { sha_init((sha_ctx *)c); }
void lce_sha256_update(lce_sha256_ctx *c,const uint8_t *p,size_t n) { sha_update((sha_ctx *)c,p,n); }
void lce_sha256_final(lce_sha256_ctx *c,uint8_t out[32]) { sha_final((sha_ctx *)c,out); }
void lce_hmac_sha256(const uint8_t *key,size_t key_len,const uint8_t *msg,size_t len,uint8_t out[32]) {
    uint8_t k[64]={0},ipad[64],opad[64],inner[32]; sha_ctx c;
    if (key_len>64) lce_sha256(key,key_len,k); else memcpy(k,key,key_len);
    for (int i=0;i<64;++i) { ipad[i]=k[i]^0x36; opad[i]=k[i]^0x5c; }
    sha_init(&c); sha_update(&c,ipad,64); sha_update(&c,msg,len); sha_final(&c,inner);
    sha_init(&c); sha_update(&c,opad,64); sha_update(&c,inner,32); sha_final(&c,out);
}

/* ---- JSON formatting (must match client_policy.canonical) ---- */
static int put(char *o,size_t cap,size_t *n,const char *s) { size_t l=strlen(s); if (*n+l>=cap) return 0; memcpy(o+*n,s,l); *n+=l; o[*n]=0; return 1; }
static int put_str(char *o,size_t cap,size_t *n,const char *s) {
    if (!put(o,cap,n,"\"")) return 0;
    for (; *s; ++s) { unsigned char ch=(unsigned char)*s; if (ch<0x20||ch>0x7e||ch=='"'||ch=='\\') return 0; char t[2]={(char)ch,0}; if (!put(o,cap,n,t)) return 0; }
    return put(o,cap,n,"\"");
}
static void hex(const uint8_t *b,size_t len,char *out) { static const char d[]="0123456789abcdef"; for (size_t i=0;i<len;++i){out[2*i]=d[b[i]>>4];out[2*i+1]=d[b[i]&15];} out[2*len]=0; }
static int body(char *o,size_t cap,size_t *n,const char *device,const char *plugin,const char *game,const char *text,
                const char *hw,int ik,const char *const *mods,size_t count,int debug,int canonical) {
    /* canonical: sorted keys incl. device_id; report: plugin first, no device_id */
    if (canonical) {
        if (!put(o,cap,n,"{\"debug\":")||!put(o,cap,n,debug?"true":"false")||!put(o,cap,n,",\"device_id\":")||!put_str(o,cap,n,device)||
            !put(o,cap,n,",\"game\":")||!put_str(o,cap,n,game)) return 0;
        if (hw && (!put(o,cap,n,",\"hw\":")||!put_str(o,cap,n,hw))) return 0;
        if (ik && !put(o,cap,n,",\"ik\":1")) return 0;
        if (!put(o,cap,n,",\"modules\":[")) return 0;
    } else {
        if (!put(o,cap,n,"{\"plugin\":")||!put_str(o,cap,n,plugin)||!put(o,cap,n,",\"game\":")||!put_str(o,cap,n,game)||
            !put(o,cap,n,",\"text\":")||!put_str(o,cap,n,text)||!put(o,cap,n,",\"debug\":")||!put(o,cap,n,debug?"true":"false")) return 0;
        if (hw && (!put(o,cap,n,",\"hw\":")||!put_str(o,cap,n,hw))) return 0;
        if (ik && !put(o,cap,n,",\"ik\":1")) return 0;
        if (!put(o,cap,n,",\"modules\":[")) return 0;
    }
    for (size_t i=0;i<count;++i) if ((i&&!put(o,cap,n,","))||!put_str(o,cap,n,mods[i])) return 0;
    if (!put(o,cap,n,"]")) return 0;
    if (canonical && (!put(o,cap,n,",\"plugin\":")||!put_str(o,cap,n,plugin)||!put(o,cap,n,",\"text\":")||!put_str(o,cap,n,text)||!put(o,cap,n,"}"))) return 0;
    return 1;
}
int lce_client_report_format_ik(char *out,size_t cap,const char *device,const char *plugin,const char *game,const char *text_hex,
                                const char *hw,const char *const *mods,size_t count,int debug,const uint8_t *key,size_t key_len,
                                int ik,const uint8_t *install_key) {
    char canon[3072]; size_t cn=0, n=0; uint8_t mac[32]; char machex[65];
    if (!out||!cap||!device||!plugin||!game||!text_hex||!key||(install_key&&!ik)) return 0;
    canon[0]=0; out[0]=0;
    if (!body(canon,sizeof canon,&cn,device,plugin,game,text_hex,hw,ik,mods,count,debug,1)) return 0;
    lce_hmac_sha256(key,key_len,(const uint8_t *)canon,cn,mac); hex(mac,32,machex);
    if (!body(out,cap,&n,device,plugin,game,text_hex,hw,ik,mods,count,debug,0)||!put(out,cap,&n,",\"mac\":")||!put_str(out,cap,&n,machex)) { out[0]=0; return 0; }
    if (install_key) {
        lce_hmac_sha256(install_key,32,(const uint8_t *)canon,cn,mac); hex(mac,32,machex);
        if (!put(out,cap,&n,",\"imac\":")||!put_str(out,cap,&n,machex)) { out[0]=0; return 0; }
    }
    if (!put(out,cap,&n,"}")) { out[0]=0; return 0; }
    return 1;
}
int lce_client_report_format_hw(char *out,size_t cap,const char *device,const char *plugin,const char *game,const char *text_hex,
                                const char *hw,const char *const *mods,size_t count,int debug,const uint8_t *key,size_t key_len) {
    return lce_client_report_format_ik(out,cap,device,plugin,game,text_hex,hw,mods,count,debug,key,key_len,0,NULL);
}

/* ---- per-install key (provisioned by the account server) ---- */
static uint8_t install_key_value[32];
static volatile int install_key_set;
void lce_client_report_set_install_key(const uint8_t *key) {
    __atomic_store_n(&install_key_set,0,__ATOMIC_RELEASE);
    if (key) { memcpy(install_key_value,key,32); __atomic_store_n(&install_key_set,1,__ATOMIC_RELEASE); }
    else { volatile uint8_t *p=install_key_value; for (size_t i=0;i<32;++i) p[i]=0; }
}
int lce_client_report_has_install_key(void) { return __atomic_load_n(&install_key_set,__ATOMIC_ACQUIRE); }

/* ---- per-server pseudonymous console identifiers ----
   Raw IDPS/OpenPSID/MAC are stable hardware identifiers. A plugin that talks
   to third-party servers should not hand them out: each value is replaced by
   HMAC-SHA256(salt, "<kind>:<raw hex>") truncated to the field's length,
   where salt is the account service URL. Bans on one server still work (the
   value is stable per console and server), but servers cannot correlate
   consoles or learn the real IDs. */
static char hw_salt[512];
void lce_client_report_set_hw_salt(const char *salt) {
    size_t n=0;
    if (salt) for (; salt[n] && n+1<sizeof hw_salt; ++n) hw_salt[n]=salt[n];
    hw_salt[n]=0;
}
int lce_client_report_pseudonym(const char *salt,const char *kind,const uint8_t *raw,size_t len,char *out_hex) {
    char msg[8+129]; size_t k=0; uint8_t mac[32];
    if (!salt||!*salt||!kind||!raw||!out_hex||!len||len>32) return 0;
    for (; kind[k] && k<7; ++k) msg[k]=kind[k];
    if (kind[k]) return 0;
    msg[k++]=':'; hex(raw,len,msg+k); k+=2*len;
    lce_hmac_sha256((const uint8_t *)salt,strlen(salt),(const uint8_t *)msg,k,mac);
    hex(mac,len,out_hex);
    return 1;
}
int lce_client_report_format(char *out,size_t cap,const char *device,const char *plugin,const char *game,const char *text_hex,
                             const char *const *mods,size_t count,int debug,const uint8_t *key,size_t key_len) {
    return lce_client_report_format_hw(out,cap,device,plugin,game,text_hex,NULL,mods,count,debug,key,key_len);
}

#if defined(LCE_OFFLINE_PREVIEW)
/* Preview builds cannot sign or submit a server report. Never fabricate a MAC. */
int lce_client_report_build(char *out,size_t cap,const char *device,uintptr_t game_base,int debug_used) {
    (void)device; (void)game_base; (void)debug_used;
    if (out && cap) out[0]=0;
    return 0;
}
#elif defined(__FreeBSD__) || defined(__ORBIS__) || defined(MC_SUPER195)
#include <orbis/libkernel.h>
#include "client_report_key.h"   /* static const uint8_t lce_client_report_key[32] */
static int read_app_version(char out[8]) {
    uint8_t sfo[4096]; int fd=sceKernelOpen("/app0/sce_sys/param.sfo",0,0); if (fd<0) return 0;
    int64_t got=sceKernelRead(fd,sfo,sizeof sfo); sceKernelClose(fd);
    if (got<20 || memcmp(sfo,"\0PSF",4)) return 0;
    uint32_t keys=sfo[8]|sfo[9]<<8|sfo[10]<<16|(uint32_t)sfo[11]<<24, data=sfo[12]|sfo[13]<<8|sfo[14]<<16|(uint32_t)sfo[15]<<24, count=sfo[16]|sfo[17]<<8|sfo[18]<<16|(uint32_t)sfo[19]<<24;
    for (uint32_t i=0;i<count && 20+16*i+16<=(uint32_t)got;++i) {
        const uint8_t *e=sfo+20+16*i; uint32_t ko=e[0]|e[1]<<8, len=e[4]|e[5]<<8|e[6]<<16|(uint32_t)e[7]<<24, doff=e[12]|e[13]<<8|e[14]<<16|(uint32_t)e[15]<<24;
        if (keys+ko+8>(uint32_t)got || memcmp(sfo+keys+ko,"APP_VER",8)) continue;
        if (data+doff+len>(uint32_t)got || len<2 || len>8) return 0;
        memcpy(out,sfo+data+doff,len); out[len-1]=0; return 1;
    }
    return 0;
}
#include <orbis/Net.h>
/* Weak: canonical game-code hash with recorded mod patches reverted. The
   default (no mod_gate linked) returns 0 so the raw segment is hashed. */
__attribute__((weak)) int lce_mod_text_sha256(uint64_t base,const uint8_t *addr,size_t size,uint8_t out[32]) {
    (void)base; (void)addr; (void)size; (void)out; return 0;
}
/* Per-console identifiers for bans (bans are per console, not per user).
   Format: "idps:<32 hex>;psid:<32 hex>;mac:<12 hex>;fw:<8 hex>" with any
   unavailable field omitted. They are read by client code, so they raise the
   cost of ban evasion but are not unforgeable on a jailbroken console. */
static int any_nonzero(const uint8_t *b,size_t len) { for (size_t i=0;i<len;++i) if (b[i]) return 1; return 0; }
static void put_hex_field(char *out,size_t cap,size_t *n,const char *key,const uint8_t *b,size_t len) {
    char h[129]; if (len>32) return;
#if defined(LCE_HW_PSEUDONYMS)
    /* fw is informational, not an identifier; console ids are pseudonymised.
       Unset salt or an all-zero (unavailable) value: omit the field. */
    if (strcmp(key,"fw")) { if (!any_nonzero(b,len) || !lce_client_report_pseudonym(hw_salt,key,b,len,h)) return; }
    else hex(b,len,h);
#else
    (void)any_nonzero; hex(b,len,h);
#endif
    put(out,cap,n,*n?";":""); put(out,cap,n,key); put(out,cap,n,":"); put(out,cap,n,h);
}
__attribute__((weak)) int lce_console_ids_privileged(uint8_t *idps,uint8_t *psid) { (void)idps; (void)psid; return 0; }
static void read_hw(char *out,size_t cap) {
    size_t n=0; out[0]=0;
    uint8_t buf[64];
    /* IDPS/OpenPSID: sceKernelGetIdPs and sceKernelGetOpenPsIdForSystem are
       privileged syscalls; from the game process they raise SIGSYS and kill
       the game, so they are only read through lce_console_ids_privileged(). */
    if (lce_console_ids_privileged(buf,buf+16)) { put_hex_field(out,cap,&n,"idps",buf,16); put_hex_field(out,cap,&n,"psid",buf+16,16); }
    OrbisNetEtherAddr mac; memset(&mac,0,sizeof mac);
    if (sceNetGetMacAddress(&mac,0)==0) put_hex_field(out,cap,&n,"mac",(const uint8_t *)&mac,6);
    OrbisKernelSwVersion sw; memset(&sw,0,sizeof sw); sw.Size=sizeof sw;
    if (sceKernelGetSystemSwVersion(&sw)==0) {
        const uint8_t v[4]={(uint8_t)(sw.Version>>24),(uint8_t)(sw.Version>>16),(uint8_t)(sw.Version>>8),(uint8_t)sw.Version};
        put_hex_field(out,cap,&n,"fw",v,4);
    }
    if (!n) put(out,cap,&n,"none");
}
int lce_client_report_build(char *out,size_t cap,const char *device,uintptr_t game_base,int debug_used) {
    static char names[96][40]; const char *mods[96]; size_t count=0;
    char game[8]="unknown", text_hex[65]="none";
    OrbisKernelModule handles[256]; size_t total=0;
    for (unsigned i=0;i<256;++i) handles[i]=(OrbisKernelModule)-1;
    if (sceKernelGetModuleList(handles,256,&total)!=0) return 0;
    for (size_t i=0;i<total && i<256;++i) {
        if (handles[i]==(OrbisKernelModule)-1) continue;
        OrbisKernelModuleInfo m; memset(&m,0,sizeof m); m.size=sizeof m;
        if (sceKernelGetModuleInfo(handles[i],&m)!=0) continue;
        if (m.segmentCount && (uintptr_t)m.segmentInfo[0].address==game_base && m.segmentInfo[0].size) {
            uint8_t digest[32];
            /* Content mods patch the game .text; lce_mod_text_sha256 (weak,
               provided by mod_gate) hashes a canonical image with ONLY the
               recorded mod patch sites reverted to their pristine bytes, so a
               console running mods locally reports the mods-off code hash while
               any other modification still hashes as modified. Absent/failed:
               hash the live segment unchanged. */
            if (!lce_mod_text_sha256(game_base,(const uint8_t *)m.segmentInfo[0].address,m.segmentInfo[0].size,digest))
                lce_sha256((const uint8_t *)m.segmentInfo[0].address,m.segmentInfo[0].size,digest);
            hex(digest,32,text_hex);
        }
        if (count<96) {
            size_t k=0; for (; k<sizeof names[0]-1 && m.name[k]; ++k) { char c=m.name[k]; names[count][k]=(c>=0x20&&c<0x7f&&c!='"'&&c!='\\')?c:'?'; }
            names[count][k]=0; mods[count]=names[count]; ++count;
        }
    }
    (void)read_app_version(game);
    static char hw[160]; read_hw(hw,sizeof hw);
    /* ik: this plugin accepts a per-install key (always announced). */
    const int have_install=lce_client_report_has_install_key();
    uint8_t install[32]; if (have_install) memcpy(install,install_key_value,32);
    int ok=lce_client_report_format_ik(out,cap,device,LCE_PLUGIN_BUILD,game,text_hex,hw,mods,count,debug_used,
                                       lce_client_report_key,sizeof lce_client_report_key,1,have_install?install:NULL);
    { volatile uint8_t *p=install; for (size_t i=0;i<32;++i) p[i]=0; }
    return ok;
}
#endif

const char *lce_client_report_plugin_build(void) { return LCE_PLUGIN_BUILD; }
