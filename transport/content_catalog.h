#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Signed content catalogs (mods, texture packs, maps, skin packs).
 *
 * A catalog is index.json from a content repository plus index.json.sig, an
 * Ed25519 signature (hex) by the content signing key. Nothing from a catalog
 * may be used unless lce_catalog_verify() accepted it. Each file is then
 * checked against its SHA-256 before installation.
 */
#define LCE_CATALOG_MAX_ITEMS 64
#define LCE_CATALOG_MAX_FILES 8

typedef struct {
    char path[128];       /* relative path, '/'-separated segments of [A-Za-z0-9._-], no "."/".."/hidden */
    char asset[128];      /* release asset name (path with '/' -> "__"); no '/' */
    uint64_t size;
    uint8_t sha256[32];
} lce_catalog_file;

typedef struct {
    char id[48];          /* [a-z0-9-] */
    char name[64];        /* printable ASCII */
    char version[16];     /* digits and dots */
    char description[128];
    unsigned file_count;
    lce_catalog_file files[LCE_CATALOG_MAX_FILES];
    uint32_t dlc_id;      /* skin packs: native DLC pack id (0 = none) */
    uint32_t skin_count;  /* skin packs: number of skins in SkinPack.pck */
} lce_catalog_item;

typedef struct {
    char type[16];        /* "mod", "texture-pack", "map", "skin-pack" */
    size_t count;
    lce_catalog_item items[LCE_CATALOG_MAX_ITEMS];
} lce_catalog;

/* Built-in content signing public key (Ed25519). */
extern const uint8_t lce_content_public_key[32];

/* 1 if sig_hex (128 hex chars, optional trailing newline) is a valid
   signature of json[0..len) under public_key. */
int lce_catalog_verify(const uint8_t *json, size_t len, const char *sig_hex, size_t sig_len,
                       const uint8_t public_key[32]);

/* Parse a verified catalog. Returns 1 on success; rejects anything malformed,
   oversize or with unsafe identifiers (all or nothing). */
int lce_catalog_parse(const char *json, size_t len, lce_catalog *out);

/* 1 if data matches the file's size and SHA-256. */
int lce_catalog_file_ok(const lce_catalog_file *f, const uint8_t *data, size_t len);

/* Install path for a file: "<root>/<id>/<path>" (root chosen per repo by the
   caller, e.g. /data/lce-mods/packs for skin and texture packs). repo must be
   one of the four content repos. Returns 1 if it fits. */
int lce_catalog_install_path(char *out, size_t cap, const char *root, const char *repo,
                             const lce_catalog_item *item, const lce_catalog_file *f);

#ifdef __cplusplus
}
#endif
