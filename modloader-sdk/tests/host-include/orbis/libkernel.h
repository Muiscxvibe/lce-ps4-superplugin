#pragma once
/* Host-test stand-in for the two OpenOrbis kernel calls the r72 item-atlas owner uses (tests only). */
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef void *OrbisPthread;
int32_t scePthreadGetthreadid(void);
int32_t sceKernelQueryMemoryProtection(void *, void **, void **, int32_t *);
#ifdef __cplusplus
}
#endif
