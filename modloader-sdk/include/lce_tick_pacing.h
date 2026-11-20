/* Header-only pacing for callbacks receiving LCE_EV_SERVER_TICK. */
#ifndef LCE_TICK_PACING_H
#define LCE_TICK_PACING_H

#include <stddef.h>
#include "lce_mod_api.h"

/* An interval of one with offset zero accepts all ticks. Offsets range from
 * zero to interval - 1 and use the runtime's delivered server tick sequence. */
static inline int lce_tick_pacing_valid(uint32_t interval, uint32_t offset) {
    return interval != 0 && offset < interval;
}

/* Pass the still-valid payload from a server tick callback. Missing or short
 * payloads and invalid intervals are skipped. Missed ticks are never replayed. */
static inline int lce_tick_pacing_due(const lce_ev_tick *tick, uint32_t interval, uint32_t offset) {
    return tick && tick->struct_size >= offsetof(lce_ev_tick, tick) + sizeof tick->tick &&
           lce_tick_pacing_valid(interval, offset) && tick->tick % interval == offset;
}

#endif
