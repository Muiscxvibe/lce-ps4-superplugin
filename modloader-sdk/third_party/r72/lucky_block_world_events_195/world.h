#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* All callbacks are synchronous, non-reentrant and must not throw. The owner
 * retains the exact server Level and every admitted loaded chunk for the whole
 * operation; scope_held returns 1 only on the authenticated server thread.
 * resolve_state returns the native state for an authenticated registered block
 * ID and metadata. No callback may generate a chunk as a side effect. */
typedef struct mc195_world_context {
 uint32_t abi,size; uintptr_t base,level; void *opaque;
 int (*scope_held)(void *,uintptr_t level);
 uint64_t (*thread_id)(void *);
 int (*read)(void *,uintptr_t,void *,size_t);
 int (*loaded)(void *,uintptr_t level,int32_t chunk_x,int32_t chunk_z);
 uintptr_t (*resolve_state)(void *,uint16_t block_id,uint8_t metadata);
} mc195_world_context;
typedef struct mc195_world_pos { int32_t x,y,z; } mc195_world_pos;
typedef struct mc195_world_place {
 mc195_world_pos pos; uint16_t block_id; uint8_t metadata; uint8_t reserved;
} mc195_world_place;
/* 1 means native call and exact readback succeeded; 0 means refused before the
 * call; -1 means native mutation was attempted but readback/scope failed. */
int mc195_world_place_block(const mc195_world_context *,const mc195_world_place *);
/* notify=1 uses native flags 3; notify=0 uses flags 2 (client update without
 * neighbor notification), matching the original drop's blockUpdate setting. */
int mc195_world_place_block_update(const mc195_world_context *,const mc195_world_place *,unsigned notify);
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif
/* Inclusive cuboid, at most 4096 cells. hollow=1 places only its boundary.
 * A successful prefix is reported in completed even when later mutation fails;
 * it is not a transaction and does not roll back physics or side effects.
 * All chunks intersecting the cuboid must be retained by the caller. */
typedef struct mc195_world_fill {
 mc195_world_pos min,max; uint16_t block_id; uint8_t metadata,hollow;
} mc195_world_fill;
int mc195_world_fill_blocks(const mc195_world_context *,const mc195_world_fill *,uint32_t *completed);
int mc195_world_fill_blocks_update(const mc195_world_context *,const mc195_world_fill *,uint32_t *completed,unsigned notify);
#ifdef __cplusplus
}
#endif
#ifdef __cplusplus
extern "C" {
#endif
/* Base LevelData only. Writes absolute nonnegative day time using the pinned
 * native setter, then verifies its native getter. The scope must retain both
 * Level and LevelData; no dimension-specific derived data is accepted. */
int mc195_world_set_daytime(const mc195_world_context *,int64_t day_time);
#ifdef __cplusplus
}
#endif
