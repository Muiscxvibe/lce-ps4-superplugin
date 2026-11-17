#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef int (*lce_gateway_read)(void *,uint8_t *,size_t);
typedef int (*lce_gateway_write)(void *,const uint8_t *,size_t);
typedef struct {
    uint8_t challenge[40], response[48], key[32];
    size_t received, sent;
    int phase;
} lce_gateway_handshake;
/* Ticket data comes only from an authenticated TLS account response. */
int lce_gateway_init(lce_gateway_handshake *,const char *id_hex,const char *key_hex);
/* 1 complete, 0 pending, -1 rejected/closed/error. */
int lce_gateway_step(lce_gateway_handshake *,lce_gateway_read,lce_gateway_write,void *);
void lce_gateway_clear(lce_gateway_handshake *);
#ifdef __cplusplus
}
#endif
