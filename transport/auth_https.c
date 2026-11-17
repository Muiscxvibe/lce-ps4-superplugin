#include "auth_https.h"
#include <string.h>

int lce_auth_https_trust(lce_https_trust *trust,const lce_https_api *api,int http,
                         const char *pem,size_t length) {
    if (!trust || !api || !api->load_cert || http<0 || !pem || !length || length>65536) return 0;
    memset(trust,0,sizeof *trust);
    trust->pem.ptr=pem; trust->pem.size=length; trust->roots[0]=&trust->pem;
    trust->http_context=http;
    if (api->load_cert(http,1,trust->roots,0,0)<0) return 0;
    trust->ready=1;
    return 1;
}

int lce_auth_https(const lce_https_api *a,const lce_https_trust *trust,const char *url,
        lce_auth_op op,const char *username,
        const char *secret,const char *device,int remember,lce_auth_result *result) {
    uint8_t request[4100]={0}, response[4101]={0};
    size_t request_size=0, received=0;
    int tmpl=-1, conn=-1, req=-1, code=0, success=0;
    if (result) memset(result,0,sizeof *result);
    if (!a || !result || !trust || !trust->ready || trust->http_context<0 ||
        !url || strncmp(url,"https://",8) || !url[8] ||
        !a->load_cert || !a->create_template || !a->set_redirect ||
        !a->set_connect_timeout || !a->set_send_timeout || !a->set_recv_timeout ||
        !a->connect_url || !a->request_url || !a->header || !a->send ||
        !a->status || !a->read || !a->delete_request ||
        !a->delete_connection || !a->delete_template) goto done;
    if (!lce_auth_build(request,sizeof request,&request_size,op,username,secret,device,remember)) goto done;
    tmpl=a->create_template(trust->http_context,"LCE-Plugin/1",2,0);
    if (tmpl<0 || a->set_redirect(tmpl,0)<0 ||
        a->set_connect_timeout(tmpl,10000000)<0 ||
        a->set_send_timeout(tmpl,10000000)<0 ||
        a->set_recv_timeout(tmpl,10000000)<0) goto done;
    conn=a->connect_url(tmpl,url,1);
    if (conn<0) goto done;
    req=a->request_url(conn,1,url,request_size-4);
    if (req<0 || a->header(req,"Content-Type","application/json",0)<0 ||
        a->send(req,request+4,request_size-4)<0 || a->status(req,&code)<0 || code!=200) goto done;
    for (;;) {
        int n=a->read(req,response+4+received,(uint32_t)(sizeof response-4-received));
        if (n<0 || (size_t)n>sizeof response-4-received) goto done;
        if (!n) break;
        received+=(size_t)n;
        if (received>4096) goto done;
    }
    response[0]=(uint8_t)(received>>24); response[1]=(uint8_t)(received>>16);
    response[2]=(uint8_t)(received>>8); response[3]=(uint8_t)received;
    success=lce_auth_parse(response,received+4,result);
done:
    if (req>=0) a->delete_request(req);
    if (conn>=0) a->delete_connection(conn);
    if (tmpl>=0) a->delete_template(tmpl);
    lce_auth_wipe(request,sizeof request);
    lce_auth_wipe(response,sizeof response);
    return success;
}
