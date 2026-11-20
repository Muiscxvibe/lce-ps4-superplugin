#pragma once
#include <stddef.h>
#include <stdint.h>
#include "../loader.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Fill PS4 file/dir services. Native mod loading is always NULL (disabled). */
void lce_ps4_host_ops(lce_host_ops *ops, void (*log)(void *, const char *), void *log_user);
/* Protection-checked memory access (sceKernelQueryMemoryProtection), atomic aligned 8-byte writes. */
int lce_ps4_mem_read(void *, uintptr_t a, void *out, size_t n);
int lce_ps4_mem_write(void *, uintptr_t a, const void *in, size_t n);
/* 8-byte aligned compare-exchange on a writable mapping; 1 swapped. */
int lce_ps4_mem_cas8(void *, uintptr_t a, uint64_t expected, uint64_t desired);
/* 1 if a lies in the attested eboot RX segment 0. */
int lce_ps4_is_code(uintptr_t base, uintptr_t a);
/* 8-byte CAS on a read-only (RELRO) data page outside segment 0: 1 swapped, 0 nothing written, -1 protection not restored. */
int lce_ps4_mem_patch_ro8(uintptr_t base, uintptr_t a, uint64_t expected, uint64_t desired);
uint64_t lce_ps4_thread_id(void);
uint64_t lce_ps4_now_ms(void);
#ifdef __cplusplus
}
#endif
