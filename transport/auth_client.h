#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Build/parse the account service's length-prefixed JSON protocol. A caller
   must supply an authenticated TLS transport; these functions do not connect. */
typedef enum { LCE_AUTH_LOGIN, LCE_AUTH_REGISTER, LCE_AUTH_RESUME, LCE_AUTH_LOGIN_OR_REGISTER } lce_auth_op;
typedef struct { uint64_t game_uid; char token[44]; char ticket_id[33]; char ticket_key[65]; int accepted; int debug_allowed; } lce_auth_result;
int lce_auth_build(uint8_t *out, size_t cap, size_t *used, lce_auth_op op,
                   const char *username, const char *secret, const char *device_id, int remember);
int lce_auth_parse(const uint8_t *frame, size_t length, lce_auth_result *result);
void lce_auth_wipe(void *ptr, size_t size);
/* Optional client integrity report (JSON object) appended to the next request
   as "client"; NULL clears it. Copied, max 1536 bytes. */
int lce_auth_set_client_report(const char *json);
/* Human-readable "error" text from the most recent rejected response, or "". */
const char *lce_auth_last_error(void);
#ifdef __cplusplus
}
#endif
