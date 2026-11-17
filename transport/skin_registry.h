#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Downloaded skin packs installed by the content downloader live in
 * <root>/<slug>/SkinPack.pck with <root>/<slug>/lce.desc:
 *   "<dlc_id> <skin_count> <size> <sha256 hex>\n"
 * The startup registrar trusts only well-formed descriptors and re-checks the
 * pack's size and SHA-256 when it loads the file. */
#define LCE_SKIN_REGISTRY_MAX 32

typedef struct {
    char slug[49];          /* [a-z0-9-], also the folder name */
    uint32_t dlc_id;        /* 1..0xffffff */
    uint32_t skin_count;    /* 1..256 */
    uint64_t size;          /* SkinPack.pck bytes, <= 64 MiB */
    uint8_t sha256[32];
} lce_skin_entry;

int lce_skin_desc_parse(const char *text, size_t len, lce_skin_entry *out);

typedef struct {
    /* Calls emit(emit_ctx, name) for each sub-directory of dir; 1 on success. */
    int (*list_dirs)(void *ctx, const char *dir, void (*emit)(void *, const char *), void *emit_ctx);
    /* Reads up to cap bytes of path into buf; returns bytes read or -1. */
    long (*read_file)(void *ctx, const char *path, char *buf, size_t cap);
    void *ctx;
} lce_skin_fs;

/* Fills out[] with valid entries (sorted by dlc_id, duplicates of a dlc_id or
 * of any id in reserved[] skipped); returns the count. */
size_t lce_skin_registry_scan(const lce_skin_fs *fs, const char *root, const uint32_t *reserved, size_t reserved_count,
                              lce_skin_entry *out, size_t max);

#ifdef __cplusplus
}
#endif
