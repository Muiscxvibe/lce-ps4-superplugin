#pragma once
#include "auth_https.h"

#ifdef __cplusplus
extern "C" {
#endif
/* Worker-owned and process-lifetime: both this object and the PEM must remain
 * resident while the HTTP context exists. Initialisation is single-shot; a
 * failure is terminal for this object. No system-wide Net init/term occurs.
 */
typedef struct {
    lce_https_api api;
    lce_https_trust trust;
    int pool, ssl, http;
    int attempted, ready, stage, native_error, ssl_import_error, http_import_error;
} lce_orbis_auth;
int lce_orbis_auth_init(lce_orbis_auth *,uintptr_t game_base,const char *pem,size_t pem_length);
int lce_orbis_auth_request(lce_orbis_auth *,const char *url,lce_auth_op,
                           const char *username,const char *secret,const char *device,
                           int remember,lce_auth_result *result);
#ifdef __cplusplus
}
#endif
