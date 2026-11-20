#pragma once
#include "world.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct mc195_world_explosion {
 double x,y,z;
 float radius;
 uint8_t fire,destroy_blocks;
 uint16_t reserved;
} mc195_world_explosion;
/* Calls the native ServerLevel explosion, including its block/entity effects
 * and client broadcast. The owner must retain all chunks within 2r+1 blocks.
 * Source is empty, matching the original Lucky Block world's explosion call.
 * 1 means the native call completed, not that a particular entity was damaged.
 * 0 refuses before mutation; -1 means the call began but ownership was lost. */
int mc195_world_explode(const mc195_world_context *,const mc195_world_explosion *);
#ifdef __cplusplus
}
#endif
