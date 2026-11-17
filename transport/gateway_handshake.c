#include "gateway_handshake.h"
#include "auth_client.h"
#include "../r72-session-candidate/loader/native_threeway_repeat_candidate/source/startup-re/cold-selector/pinned_sha256.h"
#include <string.h>

static int digit(char c) {return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;}
static int unhex(const char *s,uint8_t *out,size_t count) {
    if (!s) return 0;
    for (size_t i=0;i<count;++i) {
        if (!s[2*i] || !s[2*i+1]) return 0;
        int high=digit(s[2*i]),low=digit(s[2*i+1]);
        if (high<0 || low<0) return 0;
        out[i]=(uint8_t)((high<<4)|low);
    }
    return s[count*2]==0;
}
void lce_gateway_clear(lce_gateway_handshake *g) {if (g) lce_auth_wipe(g,sizeof *g);}
int lce_gateway_init(lce_gateway_handshake *g,const char *id,const char *key) {
    if (!g) return 0;
    lce_gateway_clear(g);
    if (!unhex(id,g->response,16) || !unhex(key,g->key,32)) {lce_gateway_clear(g);return 0;}
    return 1;
}
static void make_proof(lce_gateway_handshake *g) {
    uint8_t inner[32],ipad[64],opad[64];SHA256_CTX ctx;
    for (size_t i=0;i<64;++i) {uint8_t v=i<32?g->key[i]:0;ipad[i]=(uint8_t)(v^0x36);opad[i]=(uint8_t)(v^0x5c);}
    static const uint8_t domain[]="LCE-GAME-1";
    mc_cold_sha256_init(&ctx);mc_cold_sha256_update(&ctx,ipad,sizeof ipad);
    mc_cold_sha256_update(&ctx,domain,sizeof domain);
    mc_cold_sha256_update(&ctx,g->response,16);
    mc_cold_sha256_update(&ctx,g->challenge+8,32);mc_cold_sha256_final(&ctx,inner);
    mc_cold_sha256_init(&ctx);mc_cold_sha256_update(&ctx,opad,sizeof opad);
    mc_cold_sha256_update(&ctx,inner,sizeof inner);mc_cold_sha256_final(&ctx,g->response+16);
    lce_auth_wipe(&ctx,sizeof ctx);lce_auth_wipe(inner,sizeof inner);
    lce_auth_wipe(ipad,sizeof ipad);lce_auth_wipe(opad,sizeof opad);lce_auth_wipe(g->key,sizeof g->key);
}
int lce_gateway_step(lce_gateway_handshake *g,lce_gateway_read read_fn,lce_gateway_write write_fn,void *ctx) {
    if (!g || !read_fn || !write_fn || g->phase<0 || g->phase>2) return -1;
    if (g->phase==0) {
        size_t remain=sizeof g->challenge-g->received;
        int n=read_fn(ctx,g->challenge+g->received,remain);
        if (n<0 || (size_t)n>remain) {lce_gateway_clear(g);g->phase=-1;return -1;}
        g->received+=(size_t)n;
        if (g->received<sizeof g->challenge) return 0;
        if (memcmp(g->challenge,"LCEAUTH1",8)) {lce_gateway_clear(g);g->phase=-1;return -1;}
        make_proof(g);g->phase=1;
    }
    if (g->phase==1) {
        size_t remain=sizeof g->response-g->sent;
        int n=write_fn(ctx,g->response+g->sent,remain);
        if (n<0 || (size_t)n>remain) {lce_gateway_clear(g);g->phase=-1;return -1;}
        g->sent+=(size_t)n;
        if (g->sent<sizeof g->response) return 0;
        lce_auth_wipe(g->response,sizeof g->response);lce_auth_wipe(g->challenge,sizeof g->challenge);g->phase=2;
    }
    return 1;
}
