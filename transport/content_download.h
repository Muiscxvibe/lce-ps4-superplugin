#pragma once
#include "auth_https.h"
#include "content_catalog.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Downloads signed content from the LCE server's content mirror
 * (GET https://<server>:<port>/v1/content/<repo>/...) and installs it under
 * <root>/<repo>/<id>/. Run on a worker thread (blocking network and file I/O).
 */

/* File operations (sceKernel* on the PS4, POSIX in host tests). */
typedef struct {
    int (*mkdir_p)(void *ctx, const char *dir);                 /* 0 on success (exists ok) */
    int (*open_write)(void *ctx, const char *path);             /* fd >= 0, truncates */
    int (*write)(void *ctx, int fd, const void *p, size_t n);  /* 0 on success (all bytes) */
    int (*close)(void *ctx, int fd);
    int (*rename)(void *ctx, const char *from, const char *to); /* 0 on success, replaces */
    int (*unlink)(void *ctx, const char *path);
    void *ctx;
} lce_content_fs;

/* Streamed HTTPS GET. sink returns 0 to abort. Fails on any status but 200,
   on redirects, and when more than max bytes arrive. */
int lce_https_get(const lce_https_api *, const lce_https_trust *, const char *url, uint64_t max,
                  int (*sink)(void *, const uint8_t *, size_t), void *sink_ctx, uint64_t *received);

/* Fetch index.json + index.json.sig for repo from mirror_base (e.g.
   "https://192.168.1.165:25567"), verify the signature and parse.
   buf must hold the catalog (up to 4 MiB). Returns 1 on success. */
int lce_content_fetch_catalog(const lce_https_api *, const lce_https_trust *, const char *mirror_base,
                              const char *repo, uint8_t *buf, size_t cap, lce_catalog *out);

/* Download every file of item, verify size + SHA-256 and install atomically.
   Returns 1 only if every file was installed. */
int lce_content_install(const lce_https_api *, const lce_https_trust *, const lce_content_fs *,
                        const char *mirror_base, const char *repo, const char *root,
                        const lce_catalog_item *item);

/* Same as lce_content_install but the download step is injected (host tests). */
typedef int (*lce_content_getter)(void *ctx, const char *url, uint64_t max,
                                  int (*sink)(void *, const uint8_t *, size_t), void *sink_ctx);
int lce_content_install_with(lce_content_getter get, void *get_ctx, const lce_content_fs *,
                             const char *mirror_base, const char *repo, const char *root,
                             const lce_catalog_item *item);
int lce_content_fetch_catalog_with(lce_content_getter get, void *get_ctx, const char *mirror_base,
                                   const char *repo, uint8_t *buf, size_t cap, lce_catalog *out);

#ifdef __cplusplus
}
#endif
