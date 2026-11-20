/* projectiles195: thrown items for CUSA00265 1.95 (snowball-class projectiles with mod on-hit behaviour).
 *
 * A mod throwable item is an SDK item whose private vtable is a copy of SnowballItem's (0x12cae00, an Item
 * with no extra fields: the 57 slots differ from Item only in the class id, the destructors and +0x40 use)
 * with +0x108 (name) kept from the SDK item and +0x40 = p195_item_use. That wrapper runs the stock
 * SnowballItem::use 0x637970 with a "pending kind" for the calling thread. The stock use builds a real
 * Snowball (0x6371a0) and calls its vt +0x860 shoot; that one Snowball vtable word (RELRO, outside segment 0,
 * the biomes195 patch_ro8 precedent) points at p195_shoot_hook, which, only while a kind is pending on that
 * thread, CASes the new Snowball's primary vptr to a per-kind private copy of the Snowball primary vtable
 * (RTTI and all 273 slots kept; +0x880 onHit = p195_on_hit) and shoots it with the kind's speed and
 * inaccuracy. p195_on_hit reports the hit (server side only) through the module's hit callback, then runs the
 * stock onHit 0x637490 (snowball damage/knockback, particles, removal).
 *
 * Limits: the projectile renders as a snowball (the 1.95 renderer is per entity type and has no item-sprite
 * data); a projectile saved in flight reloads as a plain snowball; client copies are plain snowballs.
 * No .text write. Every address is an RVA in the 1.95 image (legacy-195-analysis.elf c675e6ce...029f).
 */
#ifndef LCE_PROJECTILES195_H
#define LCE_PROJECTILES195_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define P195_RVA_ITEM_VT          0x126e638u   /* Item (address point) */
#define P195_RVA_SNOWBALLITEM_VT  0x12cae00u   /* SnowballItem (address point), 57 slots */
#define P195_RVA_SNOWBALL_VT      0x12ca530u   /* Snowball primary (address point), slots 0..+0x880 */
#define P195_RVA_ITEM_USE         0x3aedb0u    /* Item::use (stock SDK item +0x40) */
#define P195_RVA_SNOWBALLITEM_USE 0x637970u    /* (sret, this, level, shared_ptr<Player>*, hand) */
#define P195_RVA_SHOOT            0x6e15a0u    /* (this, shared_ptr<Entity>*, xRot, yRot, z, speed, inaccuracy) */
#define P195_RVA_GETOWNER         0x6e36c0u
#define P195_RVA_ONHIT            0x637490u    /* (this, HitResult*) -> bool */
#define P195_SLOT_USE             0x40u
#define P195_SLOT_NAME            0x108u
#define P195_SLOT_SHOOT           0x860u
#define P195_SLOT_GETOWNER        0x878u
#define P195_SLOT_ONHIT           0x880u
#define P195_ITEM_SLOTS           57u
#define P195_SNOWBALL_SLOTS       273u          /* 0 .. +0x880 */
#define P195_SECONDARY_OTT        (-0x440)      /* the word after +0x880: offset-to-top of the secondary vtable */
#define P195_F_LEVEL              0x150u        /* Entity -> Level* */
#define P195_F_LEVEL_CLIENT       0x248u        /* Level isClientSide byte */
#define P195_F_POS                0x170u        /* Entity x,y,z doubles */
#define P195_F_HIT_ENTITY         0x20u         /* HitResult shared_ptr<Entity> (raw pointer) */
#define P195_STOCK_SPEED          1.5f
#define P195_STOCK_INACCURACY     1.0f
#define P195_MAX_KINDS            16u

typedef struct p195_natives {
    void *(*item_use)(void *sret, void *self, void *level, void *player_sp, int hand);
    void (*shoot)(void *self, void *shooter_sp, float x_rot, float y_rot, float z, float speed, float inaccuracy);
    uint8_t (*on_hit)(void *self, void *hit);
} p195_natives;
void p195_natives_bind(p195_natives *n, uintptr_t base);   /* console only */

/* Same contract as lce_b195_memory: read/cas8 on writable data, patch_ro8 = CAS on a RELRO page
 * (1 swapped, 0 mismatch, -1 protection not restored). thread_id: the calling native thread. */
typedef struct p195_memory {
    void *ctx;
    int (*read)(void *ctx, uintptr_t at, void *out, size_t n);
    int (*cas8)(void *ctx, uintptr_t at, uint64_t expected, uint64_t desired);
    int (*patch_ro8)(void *ctx, uintptr_t at, uint64_t expected, uint64_t desired);
    uint64_t (*thread_id)(void *ctx);
} p195_memory;

typedef struct p195_kind {
    char key[64];            /* the throwable item key */
    float speed, inaccuracy; /* 0 = stock 1.5 / 1.0 */
} p195_kind;

/* Server-side hit report: kind index, the projectile, the hit entity (NULL = a block) and the projectile
 * position. Runs before the stock onHit (which still applies snowball knockback and removes it). */
typedef void (*p195_hit_fn)(void *ctx, uint32_t kind, void *projectile, void *entity, double x, double y, double z);

enum { P195_IDLE = 0, P195_READY = 1, P195_ARMED = 2, P195_UNCERTAIN = 3 };

typedef struct p195_module {
    uint32_t state, count;
    uintptr_t base;
    p195_natives n;
    p195_memory mem;
    p195_hit_fn hit; void *hit_ctx;
    p195_kind kinds[P195_MAX_KINDS];
    uintptr_t items[P195_MAX_KINDS];                                   /* adopted item objects */
    uintptr_t item_vt[P195_MAX_KINDS][2 + P195_ITEM_SLOTS];
    uintptr_t proj_vt[P195_MAX_KINDS][2 + P195_SNOWBALL_SLOTS];
    uint64_t throws, swaps, hits, foreign_shoots;
    char last_error[128];
} p195_module;

/* Validate and stage (no game memory). One module per process (the thunks find it globally). 1 ok, 0 refused. */
int p195_configure(p195_module *m, uintptr_t base, const p195_natives *n, const p195_memory *mem,
                   const p195_kind *kinds, uint32_t count, p195_hit_fn hit, void *hit_ctx);
/* After the SDK item of kind k exists (generic Item: its vptr is a private copy of Item's with only +0x108
 * and optionally +0x40 changed). Builds the SnowballItem copy and CASes the item vptr. 1 / 0 refused / -1. */
int p195_adopt_item(p195_module *m, uint32_t k, uintptr_t item);
/* API 1.9 composite path: the item was BUILT as a SnowballItem by items195 (lce195x_set_throw_use(p195_item_use)),
 * so its private vtable is a SnowballItem copy whose +0x40 is p195_item_use or an event route over it. Records the
 * object for kind k after checking that table (no write). 1 / 0 refused. */
int p195_bind_item(p195_module *m, uint32_t k, uintptr_t item);
/* Pins the Snowball vtable, builds the per-kind projectile vtables and installs the shoot slot (RELRO).
 * 1 armed, 0 refused (nothing written), -1 uncertain. */
int p195_install(p195_module *m);
/* Puts the stock shoot slot back (thrown mod projectiles in flight keep their private vtables). */
int p195_restore(p195_module *m);

/* Game-ABI thunks. */
void *p195_item_use(void *sret, void *self, void *level, void *player_sp, int hand);
void p195_shoot_hook(void *self, void *shooter_sp, float x_rot, float y_rot, float z, float speed, float inaccuracy);
uint8_t p195_on_hit(void *self, void *hit);

#ifdef __cplusplus
}
#endif
#endif
