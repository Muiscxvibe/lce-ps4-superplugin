#pragma once

#include "f3_draw.hpp"
#include "lce_waypoints.h"

namespace mc195_f3_waypoints {

/* Value-only helper for the F3 HUD owner. It derives an opaque layer from
 * the verified native level-array key in the existing server reply. */
bool scope(unsigned slot, uint64_t session, const mc195_f3_reply &, lce_waypoint_scope *);

/* Invoke only within the existing F3 watch compositor transaction, after
 * its pad identity, lifecycle epoch, sample age and viewport checks. */
bool draw(const menu_render::DirectSink &, int x, int y, int width, int height,
          unsigned slot, uint64_t session, const mc195_f3_reply &, const lce_waypoint_store &);

}
