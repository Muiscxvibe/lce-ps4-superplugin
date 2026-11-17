#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Anticheat client report for the account server (see
   account-service/client_policy.py). Writes a JSON object:
   {"plugin","game","text","modules","debug","mac"} where mac is
   HMAC-SHA256(key, canonical JSON incl. device_id). Returns 1 on success. */
int lce_client_report_build(char *out, size_t cap, const char *device_id,
                            uintptr_t game_base, int debug_used);
/* Pure helpers, exposed for host tests. */
void lce_sha256(const uint8_t *data, size_t len, uint8_t out[32]);
/* Incremental SHA-256 (for streamed downloads). */
typedef struct { uint64_t opaque[16]; } lce_sha256_ctx;
void lce_sha256_init(lce_sha256_ctx *);
void lce_sha256_update(lce_sha256_ctx *, const uint8_t *, size_t);
void lce_sha256_final(lce_sha256_ctx *, uint8_t out[32]);
void lce_hmac_sha256(const uint8_t *key, size_t key_len, const uint8_t *msg, size_t len, uint8_t out[32]);
int lce_client_report_format(char *out, size_t cap, const char *device_id, const char *plugin,
                             const char *game, const char *text_hex, const char *const *modules,
                             size_t module_count, int debug, const uint8_t *key, size_t key_len);
/* As above with a "hw" identifier string ("psid:..;mac:..;wmac:..;np:..");
   hw may be NULL (omitted). */
int lce_client_report_format_hw(char *out, size_t cap, const char *device_id, const char *plugin,
                                const char *game, const char *text_hex, const char *hw,
                                const char *const *modules, size_t module_count, int debug,
                                const uint8_t *key, size_t key_len);
#ifdef __cplusplus
}
#endif
