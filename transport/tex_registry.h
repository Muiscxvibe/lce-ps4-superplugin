#pragma once
#include <stddef.h>
#include <stdint.h>
#include "content_catalog.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Downloaded texture packs (Mods menu -> Texture Packs).
 *
 * The installer puts a pack into <root>/<slug>/ with the files of the signed
 * catalog item and, last and atomically, writes <root>/<slug>/lce.tex built
 * from the verified catalog item (name, size and SHA-256 of the four files the
 * game needs). Text format, fixed order, one space between fields:
 *
 *   LCETEX1\n
 *   id <dlc id or 0>\n
 *   name <printable ASCII, 1..63 chars>\n
 *   <size> <sha256 hex> TexturePack.pck\n
 *   <size> <sha256 hex> Data/x16Data.pck\n
 *   <size> <sha256 hex> Data/media.arc\n
 *   <size> <sha256 hex> languages.loc\n
 *
 * The startup registrar trusts only a well-formed descriptor; the plugin's GRE
 * provider re-checks size and SHA-256 of the pack files itself and refuses the
 * pack when anything differs. The pack's DLC id is read from the (hash
 * checked) TexturePack.pck metadata; a non-zero descriptor id must agree.
 */
#define LCE_TEX_FILES 4
#define LCE_TEX_REGISTRY_MAX 32
#define LCE_TEX_DESC_FILE "lce.tex"

extern const char *const lce_tex_file_names[LCE_TEX_FILES];  /* relative paths */
extern const uint64_t lce_tex_file_max[LCE_TEX_FILES];       /* per-file size limits */

typedef struct {
    char slug[49];                  /* [a-z0-9-], also the folder name */
    char name[64];                  /* printable ASCII */
    uint32_t desc_id;               /* descriptor id (0 = take it from the pack) */
    uint32_t dlc_id;                /* final id, from the pack metadata */
    uint64_t size[LCE_TEX_FILES];
    uint8_t sha256[LCE_TEX_FILES][32];
} lce_tex_entry;

/* Installer side: descriptor text for a verified catalog item. Returns the
 * length (including the final newline) or 0 when the item does not carry the
 * four required files (or a file exceeds its size limit): such an item is only
 * downloaded, never registered. */
size_t lce_tex_desc_format(const lce_catalog_item *item, char *out, size_t cap);

/* Registrar side: strict parse. Returns 1 on success (slug and dlc_id untouched). */
int lce_tex_desc_parse(const char *text, size_t len, lce_tex_entry *out);

/* DLC id from the first bytes of a TexturePack.pck (metadata record 0 parameter
 * 0). Needs only the file prefix (the parameter sits right behind the record
 * table). Returns 1 and the id (1..0xffffff) or 0. */
int lce_tex_pck_prefix_id(const uint8_t *buf, size_t len, uint32_t *id);

/* "<root>/<slug>/<relative file>" for file 0..3. 1 if it fits. */
int lce_tex_entry_path(char *out, size_t cap, const char *root, const lce_tex_entry *e, unsigned file);

typedef struct {
    int (*list_dirs)(void *ctx, const char *dir, void (*emit)(void *, const char *), void *emit_ctx);
    /* Reads up to cap bytes of path into buf; returns bytes read or -1. */
    long (*read_file)(void *ctx, const char *path, char *buf, size_t cap);
    void *ctx;
} lce_tex_fs;

typedef struct {
    size_t invalid;     /* folders with a missing/malformed descriptor or pack header */
    size_t reserved;    /* DLC id owned by another component */
    size_t duplicate;   /* same DLC id as an earlier (lower slug) pack */
    size_t full;        /* more valid packs than max */
} lce_tex_scan_stats;

/* Fills out[] with valid entries sorted by dlc_id then slug. Ids listed in
 * reserved[] and repeated ids are skipped (first slug wins). stats may be NULL.
 * Returns the count. */
size_t lce_tex_registry_scan(const lce_tex_fs *fs, const char *root, const uint32_t *reserved, size_t reserved_count,
                             lce_tex_entry *out, size_t max, lce_tex_scan_stats *stats);

#ifdef __cplusplus
}
#endif
