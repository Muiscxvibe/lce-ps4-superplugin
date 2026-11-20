#pragma once
#include "world.h"
#ifdef __cplusplus
extern "C" {
#endif
/* These helpers do not acquire lifetime or establish a server-thread owner.
 * Use only inside a real retained native owner scope. They never request a new
 * chunk: the exact native cache entry, Level and coordinates must match. */
int mc195_world_loaded_chunk(const mc195_world_context *,int32_t x,int32_t z,uintptr_t *chunk);
/* Resolves only the pinned normal registry range 0..365, including additive
 * Lucky253. Extended/minigame block IDs need their separately audited resolver. */
uintptr_t mc195_world_registered_state(const mc195_world_context *,uint16_t id,uint8_t metadata);
#ifdef __cplusplus
}
#endif
