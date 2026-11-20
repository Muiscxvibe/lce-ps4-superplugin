#pragma once

#include "f3_server.h"
#include "lce_mod_package.h"
#include "lce_waypoints.h"

namespace mc195_f3_waypoints {

/* Supplied by the existing UI-thread F3 admission owner and retained-pad
 * identity copy. No native pointer or gameplay coordinates are stored here. */
struct PackageOwner {
    uint64_t session;
    mc195_pad_watch watch;
    mc195_nbt_player player;
};

using CopyPackageWaypoints = size_t (*)(uint32_t, lce_pkg_waypoint *, size_t);

class PackageMarkers {
    PackageOwner owner_{};
    uint64_t requested_us_ = 0;
    lce_waypoint_store store_{};

public:
    void clear();
    bool selected(const PackageOwner &) const;
    bool toggle(const PackageOwner &, uint64_t now, bool offline);
    void update(const PackageOwner &, uint64_t now, bool offline, const lce_waypoint_scope *,
                uint64_t sampled_us, CopyPackageWaypoints);
    const lce_waypoint_store *visible(const PackageOwner &, const lce_waypoint_scope &,
                                       bool offline) const;
};

}
