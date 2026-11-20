/* fuel195 - mod-defined furnace fuel on the retail CUSA00265 1.95 image
 * (ELF c675e6ce...029f). DATA-ONLY: no .text byte is ever written.
 *
 * getBurnDuration (0x320f20) is an identity if-chain over stock Item and Tier pointers
 * with no virtual/map/data route (API_STATUS Fuel row). This module therefore
 * never calls or hooks it. Instead it injects mod fuel through the
 * FurnaceBlockEntity *serverTick* virtual, using the same technique the SDK
 * already uses for biome decorate and block entities: a private copy of the
 * class vtable in game heap plus an object-vptr swap. Nothing in segment 0
 * (.text) changes; only game-heap vtable copies and object vptr words are
 * written, exactly like blocks195 / biomes195.
 *
 * Proof the swap reaches ticking: serverTick 0x321750 has no direct call/jmp in
 * .text; it is reachable only as a vtable slot (+0x40) in the 5 furnace-family
 * vtables, so dispatch is purely virtual and a vptr swap fully intercepts it.
 *
 * The private vtable overrides exactly two slots:
 *   +0x40  serverTick   -> fuel195_tick_wrapper
 *   +0x188 canPlaceItem -> fuel195_canplace_wrapper   (hopper/manual fuel slot)
 * Every other slot is copied verbatim from the stock vtable.
 *
 * tick wrapper: if the furnace is empty or expiring (litTime 0 or 1), on the server, the fuel
 * slot holds a REGISTERED mod fuel Item*, and the retail canBurn says the input
 * can smelt, it sets litTime=litDuration=burn_ticks, shrinks the fuel stack by
 * one (or swaps in the declared remainder item when it empties), calls the
 * retail setLit when igniting an unlit furnace, then calls the stock serverTick.
 * Having a positive remaining timer after the stock decrement skips the stock
 * relight branch, so the stack is only consumed once.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "../../include/lce_mod_api.h"
#include "../../loader/backend.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Pinned RVAs (base-relative; base 0 under Unicorn) ---- */
#define FUEL195_RVA_SERVERTICK   0x321750u /* FurnaceBlockEntity::serverTick(this) */
#define FUEL195_RVA_CANPLACEITEM 0x3233b0u /* canPlaceItem(this,int,sp<II>&)       */
#define FUEL195_RVA_CANBURN      0x322060u /* canBurn(this) -> bool                */
#define FUEL195_RVA_SETCOUNT     0x3626c0u /* ItemInstance::setCount(ii,int)       */
#define FUEL195_RVA_ISEMPTY      0x3560e0u /* ItemInstance::isEmpty(ii) -> bool    */
#define FUEL195_RVA_GETITEM      0x356110u /* ItemInstance::getItem(ii) -> Item*   */
#define FUEL195_RVA_SETLIT       0x31e130u /* setLit(bool,Level*,BlockPos*)        */
#define FUEL195_RVA_CHANGED      0x322b30u /* FurnaceBlockEntity::setChanged       */
#define FUEL195_RVA_FBE_VTABLE   0x125b190u /* runtime FurnaceBlockEntity vtable   */
#define FUEL195_RVA_FBE_RTTI     0x1262790u /* FurnaceBlockEntity typeinfo         */
#define FUEL195_RVA_FBE_CTOR     0x31e9f0u /* ctor, installs 0x125b190            */
#define FUEL195_RVA_FBE_FACTORY  0x0b9cf0u /* "furnace" BlockEntityType factory    */
#define FUEL195_RVA_BE_REGISTRY  0x140ae70u /* BlockEntityType registry            */
#define FUEL195_RVA_BLOCK_ENTITY_VT 0x125b158u /* FurnaceBlock EntityBlock vtable */
#define FUEL195_RVA_BLOCK_RTTI      0x1261e40u /* FurnaceBlock typeinfo */
#define FUEL195_RVA_NEW_FURNACE     0x31ec10u /* EntityBlock::newBlockEntity */
#define FUEL195_RVA_FUEL_SLOT_VT    0x125b658u /* FurnaceFuelSlot vtable */
#define FUEL195_RVA_FUEL_SLOT_RTTI  0x12627d0u /* FurnaceFuelSlot typeinfo */
#define FUEL195_RVA_FUEL_MAY_PLACE  0x324200u /* FurnaceFuelSlot::mayPlace */

/* ---- Object / ItemInstance layout ---- */
#define FUEL195_OFF_LITTIME      0xb8u
#define FUEL195_OFF_LITDURATION  0xbcu
#define FUEL195_OFF_COOKPROGRESS 0xc0u
#define FUEL195_OFF_COOKTOTAL    0xc4u
#define FUEL195_OFF_CONTAINER    0x58u   /* this+0x58 = C (slots storage ptr)  */
#define FUEL195_OFF_LEVEL        0x20u
#define FUEL195_OFF_POS          0x28u
#define FUEL195_OFF_SLOT_INPUT   0x00u   /* input stack sp inside C            */
#define FUEL195_OFF_SLOT_FUEL    0x18u   /* fuel  stack sp inside C            */
#define FUEL195_OFF_LEVEL_CLIENT 0x248u  /* nonzero = client side              */
#define FUEL195_II_ITEM          0x28u   /* ItemInstance Item*                 */
#define FUEL195_II_COUNT         0x10u   /* ItemInstance count (int)           */
#define FUEL195_II_EMPTY         0x38u   /* ItemInstance empty flag (byte)     */

/* ---- vtable slot byte-offsets (from the vptr) ---- */
#define FUEL195_VSLOT_TICK       0x40u
#define FUEL195_VSLOT_CANPLACE   0x188u
#define FUEL195_VTABLE_LEN       0x418u  /* bytes to copy (last code slot +0x410) */
#define FUEL195_VTABLE_PREFIX    0x20u   /* virtual-base offsets, offset-to-top and RTTI */
#define FUEL195_FBE_CONTAINER_OFFSET 0x50u

#define FUEL195_MAX              64u
#define FUEL195_MAX_TICKS        (20u * 60u * 60u)   /* loader bound (72000) */
#define FUEL195_PAGE            0x4000u

/* Registered fuel: item = resolved Item*, ticks = burn time, remainder = Item*
 * left when a stack of one is consumed (0 = none). */
typedef struct { uint64_t item; uint32_t ticks; uint32_t pad; uint64_t remainder; } fuel195_entry;
typedef struct { uint32_t count; uint32_t pad; fuel195_entry e[FUEL195_MAX]; } fuel195_table;

/* Resolved retail entry points for the running image (base+rva). Kept in a
 * global the wrappers read. Separated out so host tests can substitute stubs. */
typedef struct fuel195_natives {
    uintptr_t base;
    uint8_t  (*canBurn)(void *be);
    void     (*setCount)(void *ii, int32_t n);
    uint8_t  (*isEmpty)(void *ii);
    void    *(*getItem)(void *ii);
    void     (*setLit)(uint64_t lit, void *level, void *pos);
    void     (*setChanged)(void *be);
    void     (*origTick)(void *be);        /* stock serverTick (base+0x321750) */
} fuel195_natives;

struct fuel195_ops;
typedef struct fuel195_runtime {
    fuel195_table   table;
    fuel195_natives nat;
    int (*origCanPlaceFn)(void *be, int32_t slot, void *stackSp); /* stock +0x188 -> bool */
    uintptr_t private_vtable;   /* shared private vtable, 0 until prepared */
    const struct fuel195_ops *factory_ops;
    void *(*origFactory)(void);
    uintptr_t factory_slot;
    const struct fuel195_ops *new_ops;
    void *(*origNew)(void *result, void *entity_block, void *level, int32_t data);
    uintptr_t new_slot;
    const struct fuel195_ops *slot_ops;
    int (*origMayPlace)(void *slot, void *stackSp);
    uintptr_t may_place_slot;
    int installed;
    const char *failed_step;  /* static diagnostic for the first failed attachment */
} fuel195_runtime;

/* Platform ops (console: libkernel; host tests: fakes). Mirrors blocks195. */
typedef struct fuel195_ops {
    void *ctx;
    int (*read)(void *ctx, uintptr_t addr, void *out, size_t n);        /* 1 ok */
    int (*write)(void *ctx, uintptr_t addr, const void *in, size_t n);  /* data, 1 ok */
    int (*cas8)(void *ctx, uintptr_t addr, uint64_t expected, uint64_t repl); /* object vptr swap, 1 ok */
    int (*query)(void *ctx, uintptr_t addr, int *prot);                 /* 1 mapped; bits 1R 2W 4X */
    uintptr_t (*alloc)(void *ctx, size_t n);                            /* RW game-heap page, 0 fail */
} fuel195_ops;

/* ---- pure, host-testable ---- */

/* Validate one descriptor (no game access). */
lce_status fuel195_check(const lce_fuel_desc *d, char *err, uint32_t cap);

/* Add/replace one fuel in the table (append; count written last). 1 ok. */
int fuel195_add(fuel195_runtime *rt, uintptr_t item, uint32_t ticks, uintptr_t remainder);

/* Look up a resolved Item* in the table; returns the entry or NULL. */
const fuel195_entry *fuel195_lookup(const fuel195_runtime *rt, uintptr_t item);

/* Resolve item-level and register_fuel declarations after items are published.
 * On failure the output is unchanged. Duplicate declarations are refused. */
lce_status fuel195_collect_plan(const lce_plan *plan, uintptr_t (*item_by_id)(unsigned),
                                fuel195_table *out, char *err, uint32_t cap);

/* Build a private vtable: copy `len` bytes of the stock vtable from `src` into
 * `dst`, then redirect slot +0x40 -> tick and +0x188 -> canplace. Returns len
 * or 0 on bad args. (dst/src are raw buffers; addresses are absolute.) */
size_t fuel195_build_vtable(uint8_t *dst, size_t cap, const uint8_t *src, size_t len,
                            uintptr_t tick_wrapper, uintptr_t canplace_wrapper);

/* The two wrapper functions installed into the private vtable. They read the
 * global runtime (fuel195_rt()). Declared here so the vtable builder and the
 * Unicorn/host harness can take their addresses. */
void fuel195_tick_wrapper(void *be);
int  fuel195_canplace_wrapper(void *be, int32_t slot, void *stackSp);

/* The decision+mutation core, factored out of the tick wrapper so host tests
 * can drive it directly. Returns 1 if it lit the furnace (and consumed fuel),
 * 0 if it left the furnace untouched. Uses rt->nat for every retail call. */
int fuel195_relight(fuel195_runtime *rt, void *be);

/* Accessors to the process-wide runtime the wrappers use. */
fuel195_runtime *fuel195_rt(void);
void fuel195_bind_natives(fuel195_runtime *rt, uintptr_t base);

/* Install: validate the stock vtable, allocate + build the private copy, and
 * vptr-swap one furnace object `be` (its +0x00 primary vptr) to it. Used by the
 * newBlockEntity / factory wrappers once per created furnace. 1 ok / 0 / -1. */
int fuel195_swap_object(fuel195_runtime *rt, const fuel195_ops *ops, void *be,
                        uintptr_t private_vtable);

/* Prepare the shared private vtable once (alloc + build). Stores the address in
 * *out_vtable. 1 ok. Idempotent-safe: caller keeps the result. */
int fuel195_prepare_vtable(fuel195_runtime *rt, const fuel195_ops *ops,
                           uintptr_t base, uintptr_t *out_vtable);

/* Attach to the saved block-entity factory registry, only during a quiescent
 * startup phase. Refuses missing, duplicate, malformed or changed entries.
 * This covers saved furnaces only; it does not enable the fuel capability. */
int fuel195_install_load_factory(fuel195_runtime *rt, const fuel195_ops *ops);
void *fuel195_load_factory_wrapper(void);

/* Attach the placed-furnace creation slot during quiescent startup. */
int fuel195_install_new_factory(fuel195_runtime *rt, const fuel195_ops *ops);
void *fuel195_new_factory_wrapper(void *result, void *entity_block, void *level, int32_t data);
int fuel195_install_fuel_slot(fuel195_runtime *rt, const fuel195_ops *ops);
int fuel195_may_place_wrapper(void *slot, void *stackSp);

/* Activate a fully resolved table at quiescent startup. On an ordinary failure
 * previously installed slots are restored; -1 means the result is uncertain
 * and the caller must not retry or enter a world. 1 is success, 0 is refusal. */
int fuel195_activate(fuel195_runtime *rt, const fuel195_ops *ops, uintptr_t base,
                     const fuel195_table *table);

#if defined(MC_LEGACY195)
const fuel195_ops *fuel195_ps4_ops(void);
#endif

#ifdef __cplusplus
}
#endif
