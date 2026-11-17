#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Plugin auto-update helpers (pure logic, host-testable).
 *
 * Builds are named "r<N>-<tag>-<YYYYMMDDhhmmss>"; the update channel's catalog
 * version is the trailing 14-digit timestamp. Returns 0 when malformed. */
uint64_t lce_plugin_build_version(const char *build);

/* Same, for a catalog version string (digits only, 14 long). */
uint64_t lce_plugin_catalog_version(const char *version);

/* Rewrite GoldHEN plugins.ini so the single active session plugin line
 * ("<dir>/mc195_lce_session_<hex>.prx=true") becomes "=false" and
 * "<new_path>=true" is inserted right after it (same section).
 * Fails (returns 0) unless exactly one active session plugin line exists,
 * new_path is a safe absolute path ending in ".prx", and the result fits. */
int lce_plugin_ini_switch(const char *ini, size_t len, const char *new_path, char *out, size_t cap, size_t *out_len);

/* "/data/GoldHEN/plugins/mc195_lce_session_<first 16 hex of sha256>.prx" */
int lce_plugin_final_path(const uint8_t sha256[32], char *out, size_t cap);

#ifdef __cplusplus
}
#endif
