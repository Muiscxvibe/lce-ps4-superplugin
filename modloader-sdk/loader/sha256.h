/* Minimal, dependency-free SHA-256 (FIPS 180-4) for the LCE mod loader.
 * Used to verify package integrity (mod.lcm payload hash) and the ID ledger.
 * Host-tested against the FIPS vectors in tests/test_sha256.cpp. */
#pragma once
/* The shipping plugin already exports lce_sha256* (ps4-transport/client_report.c,
 * different ctx layout). Link the SDK's implementation under distinct names so
 * lcemodloader.o never collides with it. */
#define lce_sha256_ctx lce_sdk_sha256_ctx
#define lce_sha256_init lce_sdk_sha256_init
#define lce_sha256_update lce_sdk_sha256_update
#define lce_sha256_final lce_sdk_sha256_final
#define lce_sha256 lce_sdk_sha256
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t h[8];
    uint64_t length;      /* bytes processed */
    uint8_t block[64];
    uint32_t used;
} lce_sha256_ctx;

void lce_sha256_init(lce_sha256_ctx *c);
void lce_sha256_update(lce_sha256_ctx *c, const void *data, size_t len);
void lce_sha256_final(lce_sha256_ctx *c, uint8_t out[32]);
void lce_sha256(const void *data, size_t len, uint8_t out[32]);

#ifdef __cplusplus
}
#endif
