#include "auth_client.h"
#include <string.h>
#include <limits.h>

void lce_auth_wipe(void *ptr,size_t size) {
    volatile unsigned char *p=(volatile unsigned char *)ptr;
    if (!p) return;
    while (size--) *p++=0;
}
static char client_report[1537];
static char last_error[160];
int lce_auth_set_client_report(const char *json) {
    if (!json) { client_report[0]=0; return 1; }
    size_t n=strlen(json);
    if (n>=sizeof client_report || n<2 || json[0]!='{' || json[n-1]!='}') { client_report[0]=0; return 0; }
    memcpy(client_report,json,n+1); return 1;
}
const char *lce_auth_last_error(void) { return last_error; }
static int put(uint8_t *out,size_t cap,size_t *pos,const char *s) {
    size_t n=strlen(s); if (*pos>cap || n>cap-*pos) return 0;
    memcpy(out+*pos,s,n); *pos+=n; return 1;
}
static int quoted(uint8_t *out,size_t cap,size_t *pos,const char *s,size_t max) {
    size_t n=0;
    if (!s || !put(out,cap,pos,"\"")) return 0;
    while (*s) {
        unsigned char c=(unsigned char)*s++;
        if (++n>max || c<0x20) return 0;
        if ((c=='\"'||c=='\\') && !put(out,cap,pos,"\\")) return 0;
        if (*pos>=cap) return 0;
        out[(*pos)++]=c;
    }
    return put(out,cap,pos,"\"");
}
static int valid_name(const char *s) {
    if (!s) return 0;
    size_t n=0;
    while (*s) {
        unsigned char c=(unsigned char)*s++;
        if (++n>16 || !((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_')) return 0;
    }
    return n>=3;
}
static int valid_device(const char *s) {
    if (!s) return 0;
    for (size_t i=0;i<64;++i) { unsigned char c=(unsigned char)s[i]; if (!c || !((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F'))) return 0; }
    return s[64]==0;
}
int lce_auth_build(uint8_t *out,size_t cap,size_t *used,lce_auth_op op,
                   const char *username,const char *secret,const char *device,int remember) {
    if (used) *used=0;
    if (!out || !used || cap<5 || !secret || !valid_device(device) || (remember!=0 && remember!=1) ||
        (op!=LCE_AUTH_LOGIN && op!=LCE_AUTH_REGISTER && op!=LCE_AUTH_RESUME && op!=LCE_AUTH_LOGIN_OR_REGISTER) ||
        (op!=LCE_AUTH_RESUME && !valid_name(username))) return 0;
    size_t slen=strlen(secret);
    if ((op==LCE_AUTH_RESUME && slen!=43) || (op!=LCE_AUTH_RESUME && (slen<8||slen>256))) return 0;
    size_t n=4;
    const char *operation=op==LCE_AUTH_LOGIN?"login":op==LCE_AUTH_REGISTER?"register":op==LCE_AUTH_RESUME?"resume":"login_or_register";
    if (!put(out,cap,&n,"{\"operation\":") || !quoted(out,cap,&n,operation,17) ||
        !put(out,cap,&n,",\"device_id\":") || !quoted(out,cap,&n,device,64) ||
        !put(out,cap,&n,",\"remember\":") || !put(out,cap,&n,remember?"true":"false") ||
        ((op==LCE_AUTH_LOGIN_OR_REGISTER || op==LCE_AUTH_RESUME) && !put(out,cap,&n,",\"game_ticket\":true")) ||
        (op!=LCE_AUTH_RESUME && (!put(out,cap,&n,",\"username\":") || !quoted(out,cap,&n,username,16))) ||
        !put(out,cap,&n,op==LCE_AUTH_RESUME?",\"token\":":",\"password\":") ||
        !quoted(out,cap,&n,secret,256) ||
        (client_report[0] && (!put(out,cap,&n,",\"client\":") || !put(out,cap,&n,client_report))) ||
        !put(out,cap,&n,"}")) {
        lce_auth_wipe(out,cap); return 0;
    }
    size_t payload=n-4;
    if (payload>4096) { lce_auth_wipe(out,cap); return 0; }
    out[0]=(uint8_t)(payload>>24);out[1]=(uint8_t)(payload>>16);out[2]=(uint8_t)(payload>>8);out[3]=(uint8_t)payload;
    *used=n; return 1;
}
static int token_char(uint8_t c) { return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'; }
static int hex_char(uint8_t c) {return (c>='0'&&c<='9')||(c>='a'&&c<='f');}
int lce_auth_parse(const uint8_t *frame,size_t length,lce_auth_result *result) {
    static const char prefix[]="{\"ok\":true,\"game_uid\":\"";
    static const char token_key[]=",\"token\":\"";
    static const char ticket_id_key[]=",\"ticket_id\":\"";
    static const char ticket_key_key[]=",\"ticket_key\":\"";
    if (!result) return 0;
    memset(result,0,sizeof *result);
    last_error[0]=0;
    if (!frame || length<4 || length>4100) return 0;
    size_t declared=((size_t)frame[0]<<24)|((size_t)frame[1]<<16)|((size_t)frame[2]<<8)|frame[3];
    if (declared!=length-4) return 0;
    const uint8_t *p=frame+4,*end=frame+length;
    {   /* Rejections carry {"ok":false,"error":"..."}: keep the message for the UI. */
        static const char err_prefix[]="{\"ok\":false,\"error\":\"";
        if ((size_t)(end-p)>=sizeof(err_prefix)-1 && !memcmp(p,err_prefix,sizeof(err_prefix)-1)) {
            const uint8_t *q=p+sizeof(err_prefix)-1; size_t k=0;
            while (q<end && *q!='"' && k+1<sizeof last_error) { uint8_t ch=*q++; if (ch=='\\' && q<end) ch=*q++; if (ch>=0x20 && ch<0x7f) last_error[k++]=(char)ch; }
            last_error[k]=0;
            return 0;
        }
    }
    if ((size_t)(end-p)<sizeof(prefix)-1 || memcmp(p,prefix,sizeof(prefix)-1)) return 0;
    p+=sizeof(prefix)-1; uint64_t value=0; size_t digits=0;
    while (p<end && *p>='0'&&*p<='9') {
        unsigned d=*p++-'0'; if (++digits>19 || value>((uint64_t)INT64_MAX-d)/10) return 0; value=value*10+d;
    }
    if (!digits||!value||p==end||*p++!='\"') return 0;
    char token[44]={0};
    if ((size_t)(end-p)>=sizeof(token_key)-1 && !memcmp(p,token_key,sizeof(token_key)-1)) {
        if ((size_t)(end-p)<sizeof(token_key)-1 || memcmp(p,token_key,sizeof(token_key)-1)) return 0;
        p+=sizeof(token_key)-1;
        if ((size_t)(end-p)<44) return 0;
        for (size_t i=0;i<43;++i) { if (!token_char(p[i])) return 0; token[i]=(char)p[i]; }
        p+=43; if (*p++!='\"') return 0;
    }
    char ticket_id[33]={0},ticket_key[65]={0};
    if ((size_t)(end-p)>=sizeof(ticket_id_key)-1 && !memcmp(p,ticket_id_key,sizeof(ticket_id_key)-1)) {
        p+=sizeof(ticket_id_key)-1;
        if ((size_t)(end-p)<33) return 0;
        for (size_t i=0;i<32;++i) {if (!hex_char(p[i])) return 0;ticket_id[i]=(char)p[i];}
        p+=32;if (*p++!='"' || (size_t)(end-p)<sizeof(ticket_key_key)-1 || memcmp(p,ticket_key_key,sizeof(ticket_key_key)-1)) return 0;
        p+=sizeof(ticket_key_key)-1;
        if ((size_t)(end-p)<65) return 0;
        for (size_t i=0;i<64;++i) {if (!hex_char(p[i])) return 0;ticket_key[i]=(char)p[i];}
        p+=64;if (*p++!='"') return 0;
    }
    static const char debug_key[]=",\"debug_allowed\":";
    int debug_allowed=0;
    if ((size_t)(end-p)>=sizeof(debug_key)-1 && !memcmp(p,debug_key,sizeof(debug_key)-1)) {
        p+=sizeof(debug_key)-1;
        if ((size_t)(end-p)>=4 && !memcmp(p,"true",4)) {debug_allowed=1;p+=4;}
        else if ((size_t)(end-p)>=5 && !memcmp(p,"false",5)) p+=5;
        else return 0;
    }
    if (p==end || *p++!='}' || p!=end) return 0;
    result->debug_allowed=debug_allowed;
    result->game_uid=value;memcpy(result->token,token,sizeof token);memcpy(result->ticket_id,ticket_id,sizeof ticket_id);memcpy(result->ticket_key,ticket_key,sizeof ticket_key);result->accepted=1;
    lce_auth_wipe(token,sizeof token);lce_auth_wipe(ticket_key,sizeof ticket_key);
    return 1;
}
