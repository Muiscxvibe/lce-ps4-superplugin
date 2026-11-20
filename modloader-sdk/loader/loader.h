/* LCE mod loader core: discovery, lifecycle, staging, validation, ID
 * allocation, commit through a backend, and event dispatch.
 * Single-threaded by contract: every lce_loader_* call must come from the one
 * host thread that owns the loader (the startup/registration thread), except
 * lce_loader_dispatch which must come from the thread named by the event. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "../include/lce_mod_api.h"
#include "backend.h"
#include "ledger.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LCE_LOADER_MAX_MODS 32
#define LCE_LOADER_MAX_ITEMS 512
#define LCE_LOADER_MAX_BLOCKS 128
#define LCE_LOADER_MAX_TEXTURES 640
#define LCE_LOADER_MAX_RECIPES 1024
#define LCE_LOADER_MAX_SMELTS 256
#define LCE_LOADER_MAX_TABS 32
#define LCE_LOADER_MAX_STRINGS 2048
#define LCE_LOADER_MAX_SUBS 128

/* Host services. Paths are absolute. All callbacks may be NULL in tests that
 * do not use them. */
typedef struct {
    void *user;
    /* Read whole file into buf; returns bytes read, -1 if absent/error, -2 if larger than cap. */
    long (*read_file)(void *user, const char *path, uint8_t *buf, size_t cap);
    /* Atomically replace path with data (write tmp, fsync, rename). 1 ok, 0 failed. */
    int (*write_file_atomic)(void *user, const char *path, const uint8_t *data, size_t len);
    /* Calls emit(ctx,name) for each subdirectory of dir. 1 ok, 0 failed. */
    int (*list_dirs)(void *user, const char *dir, void (*emit)(void *ctx, const char *name), void *ctx);
    /* 1 if path exists. */
    int (*exists)(void *user, const char *path);
    /* EXPERIMENTAL native mods: load PRX and resolve LCE_MOD_ENTRY_SYMBOL. NULL disables natives. */
    lce_mod_main_fn (*load_native)(void *user, const char *path);
    void (*log)(void *user, const char *line);
} lce_host_ops;

#define LCE_LOADER_ALLOW_NATIVE   (1u << 0)  /* honour native.prx (experimental) */
#define LCE_LOADER_PLACEHOLDERS   (1u << 1)  /* keep orphaned ledger IDs alive with placeholders */
#define LCE_LOADER_STRICT         (1u << 2)  /* any mod failure fails the whole load */
#define LCE_LOADER_REQUIRE_ENABLED (1u << 3) /* only load mods whose <dir>/enabled marker exists */

typedef struct {
    const char *root;          /* "/data/lce-mods" */
    const char *ledger_path;   /* "/data/lce-mods/reservations/sdk-ids-v1.txt" */
    uint32_t flags;
    lce_id_space space;
    const lce_backend *backend;
    lce_host_ops host;
} lce_loader_config;

typedef enum {
    LCE_MOD_DISCOVERED = 0, LCE_MOD_REJECTED, LCE_MOD_STAGED, LCE_MOD_LIVE, LCE_MOD_DROPPED
} lce_mod_state;

typedef struct lce_loader lce_loader;

typedef struct {
    char modid[LCE_MAX_MODID];
    char version[16];
    uint32_t state;            /* lce_mod_state */
    int32_t status;            /* lce_status of the failure, LCE_OK otherwise */
    char reason[256];
    uint32_t items, blocks, recipes, smelts, textures, strings, tabs;
} lce_mod_report;

/* Allocates the loader (host malloc). Returns NULL on bad config. */
lce_loader *lce_loader_create(const lce_loader_config *cfg);
/* Tests / failed loads only. After a successful publish the backend keeps
 * pointers to staged descriptors inside the loader: a published loader is
 * process-lifetime and must never be destroyed (there is no unload). */
void lce_loader_destroy(lce_loader *l);

/* DISCOVER + INIT + REGISTER: scan root, parse every package, run natives,
 * stage content. Never touches the game. */
lce_status lce_loader_discover(lce_loader *l);
/* Adds an in-memory package (tests, bundled mods). Buffer must outlive the loader. */
lce_status lce_loader_add_package(lce_loader *l, const uint8_t *data, size_t size, const char *source);
/* Adds an in-process native mod (tests, statically linked mods). */
lce_status lce_loader_add_native(lce_loader *l, const char *modid, const char *version, lce_mod_main_fn fn);

/* Validate everything, drop failing mods (or all, if STRICT), allocate IDs,
 * persist the ledger, backend prepare + publish. Moves to POST_INIT/RUNNING or FAILED. */
lce_status lce_loader_commit(lce_loader *l);

lce_phase lce_loader_phase(const lce_loader *l);
uint32_t lce_loader_mod_count(const lce_loader *l);
const lce_mod_report *lce_loader_mod(const lce_loader *l, uint32_t i);
/* Final plan (valid after a successful prepare); for diagnostics and tests. */
const lce_plan *lce_loader_plan(const lce_loader *l);
const lce_ledger *lce_loader_ledger(const lce_loader *l);
const lce_api *lce_loader_api(const lce_loader *l);

/* Lua sources packaged with a mod (LCE_SEC_SCRIPT), for the script layer.
 * Valid for the loader lifetime. Returns the number written. */
typedef struct { const char *name; const uint8_t *data; uint32_t size; } lce_mod_script;
uint32_t lce_loader_scripts(const lce_loader *l, uint32_t mod_index, lce_mod_script *out, uint32_t cap);
/* Script runner: called once per packaged script during REGISTER, while the
 * mod accepts register_* calls (the runner calls lce_api on the mod's ctx).
 * Return LCE_OK, or an error with err filled: the mod is then rejected.
 * Set before lce_loader_discover(); NULL (default) leaves scripts unrun. */
typedef lce_status (*lce_script_runner_fn)(void *user, lce_loader *l, uint32_t mod_index, const lce_mod_script *script, char *err, uint32_t err_cap);
void lce_loader_set_script_runner(lce_loader *l, lce_script_runner_fn fn, void *user);
/* The mod context of a mod (for the script layer to call lce_api on its behalf). */
lce_mod_ctx *lce_loader_mod_ctx(lce_loader *l, uint32_t mod_index);
/* API 1.8: config text for an added mod (tests, bundled mods; discovery reads <root>/<modid>/config itself).
 * DISCOVER phase only (before lce_loader_discover runs the mods); replaces any earlier config of that mod. */
lce_status lce_loader_set_config_text(lce_loader *l, uint32_t mod_index, const char *text, size_t len);
/* Replace runtime service entries of the API table (backend/host provides them). */
void lce_loader_set_services(lce_loader *l, const lce_api *services);

/* Host hook entry: deliver an event to subscribers. Returns nonzero if any
 * subscriber asked to cancel. No-op unless phase is RUNNING. */
int lce_loader_dispatch(lce_loader *l, lce_event ev, const void *payload);

/* Resolve helpers usable by the host after commit. */
int32_t lce_loader_item_id(const lce_loader *l, const char *key);
int32_t lce_loader_block_id(const lce_loader *l, const char *key);

/* Vanilla table (tools/data/vanilla_195.json, generated into vanilla_195.inc). */
int32_t lce_vanilla_item_id(const char *name);   /* "iron_ingot" -> 265, -1 unknown */
int32_t lce_vanilla_block_id(const char *name);  /* "stone" -> 1, -1 unknown */
int lce_vanilla_item_exists(uint32_t id);

#ifdef __cplusplus
}
#endif
