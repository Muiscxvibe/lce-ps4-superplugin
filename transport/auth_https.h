#pragma once
#include "auth_client.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Native HTTP functions must all come from the same loaded libSceHttp.
 * The caller owns an initialized HTTP context and a verified, explicitly
 * configured PEM trust anchor for the target server. Run on a worker thread.
 */
typedef struct {
    int (*load_cert)(int,int,void *,void *,void *);
    int (*create_template)(int,const char *,int,int);
    int (*set_redirect)(int,int);
    int (*set_connect_timeout)(int,uint32_t);
    int (*set_send_timeout)(int,uint32_t);
    int (*set_recv_timeout)(int,uint32_t);
    int (*connect_url)(int,const char *,int);
    int (*request_url)(int,int,const char *,uint64_t);
    int (*header)(int,const char *,const char *,int);
    int (*send)(int,const void *,size_t);
    int (*status)(int,int *);
    int (*read)(int,void *,uint32_t);
    int (*delete_request)(int);
    int (*delete_connection)(int);
    int (*delete_template)(int);
} lce_https_api;

typedef struct {
    struct { const char *ptr; size_t size; } pem;
    void *roots[1];
    int http_context;
    int ready;
} lce_https_trust;

/* Keep trust and PEM storage alive until the HTTP context is terminated. */
int lce_auth_https_trust(lce_https_trust *,const lce_https_api *,int,
                         const char *pem,size_t pem_length);

/* Returns 1 only on an accepted, well-formed response. Never accepts HTTP,
 * redirects, oversized replies, or a missing trust anchor. Credentials are
 * wiped from local buffers; the caller must wipe its own copies too.
 */
int lce_auth_https(const lce_https_api *,const lce_https_trust *,const char *url,lce_auth_op,
                   const char *username,const char *secret,const char *device,
                   int remember,lce_auth_result *result);
#ifdef __cplusplus
}
#endif
