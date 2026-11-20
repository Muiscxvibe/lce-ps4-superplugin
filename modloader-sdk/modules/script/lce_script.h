/* lce_script: a sandboxed Lua 5.4.8 behaviour layer for LCE mods.
 *
 * A script ships as scripts/main.lua inside a mod package (LCE_SEC_SCRIPT = 14).
 * It runs in a fresh lua_State with:
 *   - no io / os / debug / package / require / dofile / loadfile;
 *   - load() accepts only text chunks (never precompiled bytecode);
 *   - a hard memory cap (a counting allocator that refuses over the cap);
 *   - an instruction budget enforced by a count hook that CANNOT be bypassed
 *     by pcall: once the budget trips, the script is marked dead and every
 *     later entry into Lua raises immediately (the hook re-arms a 1-instruction
 *     countdown, so a pcall that swallows the error still cannot run code).
 *
 * Bindings expose the registration side of lce_api (register_item, register_block,
 * register_recipe, register_smelting, register_command, subscribe, and the
 * runtime services) plus a Workshop-compat shim mapping registerItem /
 * registerBlock / registerShaped/Shapeless/SmeltingRecipe / registerEvent onto
 * them. See lce_script.c for the exact surface and STATUS.md for what reaches a
 * backend today.
 *
 * Host-independent: it talks to the game only through an lce_api table and a
 * host hook table, so it is fully testable against a fake lce_api. */
#ifndef LCE_SCRIPT_H
#define LCE_SCRIPT_H
#include <stddef.h>
#include <stdint.h>
#include "../../include/lce_mod_api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lce_script lce_script;

typedef struct {
    size_t memory_cap;        /* bytes; 0 => default 16 MiB */
    uint64_t step_budget;     /* instructions per VM lifetime; 0 => default 20,000,000 */
    uint32_t hook_interval;   /* count-hook granularity; 0 => 1000 */
    /* Returns a committed custom mix's potion key, or NULL. */
    const char *(*brewing_result)(void *host_user, const char *input, const char *reagent, const char *form);
    /* Event callbacks registered by the script are delivered through the host's
     * event bus. The host supplies the thunk that forwards lce_event payloads
     * into lua_call_event(). NULL disables script events. */
    void *host_user;
    void (*on_log)(void *host_user, lce_log_level level, const char *msg);
    /* Published SDK furnace fuel duration. Zero means no entry is available. */
    uint32_t (*fuel_ticks)(void *host_user, const char *item_key);
} lce_script_config;

/* Create a sandboxed VM bound to one mod's api+ctx. Returns NULL on OOM or bad
 * args. The api/ctx must outlive the script. */
lce_script *lce_script_new(const lce_api *api, lce_mod_ctx *ctx, const lce_script_config *cfg);
void lce_script_free(lce_script *s);

/* Load and run scripts/main.lua source. Registration calls made during the run
 * reach api->register_* immediately. Returns LCE_OK, or LCE_E_INVALID with the
 * error text in *err (Lua message, truncated). Running a dead script fails. */
lce_status lce_script_run_main(lce_script *s, const char *name, const char *source, size_t len,
                               char *err, uint32_t err_cap);

/* Deliver an engine event to the script's registered callbacks (EV name e.g.
 * "ServerTick", "PlayerJoin", "PlayerBlockBreak"). payload fields are read from
 * the matching lce_ev_* struct. Returns 1 if any callback asked to cancel. A
 * dead script is a no-op returning 0. */
int lce_script_event(lce_script *s, lce_event ev, const void *payload);

/* True once the step budget tripped or a fatal error killed the VM. */
int lce_script_is_dead(const lce_script *s);
/* Bytes currently allocated by this VM. */
size_t lce_script_memory(const lce_script *s);
/* Instructions executed so far (saturating). */
uint64_t lce_script_steps(const lce_script *s);

/* ---- Workshop-compat: counts of what the script registered (diagnostics). ---- */
typedef struct {
    uint32_t items, blocks, recipes, smeltings, commands, events, errors, denied;
} lce_script_stats;
void lce_script_get_stats(const lce_script *s, lce_script_stats *out);

#ifdef __cplusplus
}
#endif
#endif
