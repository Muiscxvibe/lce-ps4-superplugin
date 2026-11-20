#include "f3_package_waypoints.hpp"

#include <string.h>

namespace mc195_f3_waypoints {
namespace {
constexpr uint64_t consent_window_us = 30000000;
constexpr uint64_t max_sample_age_us = 250000;

bool valid(const PackageOwner &owner)
{
    return owner.session && owner.watch.slot >= 0 && owner.watch.slot < 4 &&
           owner.watch.handle >= 0 && owner.watch.generation;
}

bool same(const PackageOwner &a, const PackageOwner &b)
{
    return valid(b) && a.session == b.session && a.watch.slot == b.watch.slot &&
           a.watch.handle == b.watch.handle && a.watch.generation == b.watch.generation &&
           a.player.entity_id == b.player.entity_id &&
           !memcmp(a.player.offline, b.player.offline, sizeof a.player.offline) &&
           !memcmp(a.player.online, b.player.online, sizeof a.player.online);
}

bool scope_matches(const PackageOwner &owner, const lce_waypoint_scope &scope)
{
    return scope.session == owner.session && scope.player == static_cast<uint32_t>(owner.watch.slot) &&
           scope.layer < LCE_PKG_WAYPOINT_LAYERS;
}

bool sample_current(uint64_t now, uint64_t sampled_us)
{
    return sampled_us && now >= sampled_us && now - sampled_us <= max_sample_age_us;
}
}

void PackageMarkers::clear()
{
    owner_ = {};
    requested_us_ = 0;
    lce_waypoints_reset(&store_, nullptr);
}

bool PackageMarkers::selected(const PackageOwner &owner) const
{
    return requested_us_ && same(owner_, owner);
}

bool PackageMarkers::toggle(const PackageOwner &owner, uint64_t now, bool offline)
{
    const bool was_selected = selected(owner);
    clear();
    if (!offline || !valid(owner) || !now)
        return false;
    if (!was_selected) {
        owner_ = owner;
        requested_us_ = now;
    }
    return true;
}

void PackageMarkers::update(const PackageOwner &owner, uint64_t now, bool offline,
                            const lce_waypoint_scope *scope, uint64_t sampled_us, CopyPackageWaypoints copy)
{
    if (!requested_us_)
        return;
    if (!offline || !selected(owner) || now < requested_us_ ||
        (!store_.scope.session && now - requested_us_ > consent_window_us)) {
        clear();
        return;
    }
    if (!scope || !sample_current(now, sampled_us) || sampled_us <= requested_us_)
        return;
    if (!scope_matches(owner, *scope) || (store_.scope.session && scope->layer != store_.scope.layer)) {
        clear();
        return;
    }
    if (store_.scope.session)
        return;
    if (!copy || !lce_waypoints_reset(&store_, scope)) {
        clear();
        return;
    }

    lce_pkg_waypoint pins[LCE_WAYPOINT_LIMIT]{};
    const size_t count = copy(scope->layer, pins, LCE_WAYPOINT_LIMIT);
    if (count > LCE_WAYPOINT_LIMIT) {
        clear();
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        const auto &pin = pins[i];
        static_assert(LCE_WAYPOINT_LABEL_CAPACITY == LCE_PKG_WAYPOINT_LABEL_CAPACITY);
        lce_waypoint marker{};
        marker.id = static_cast<uint32_t>(i + 1);
        if (pin.kind == LCE_PKG_WAYPOINT_POINT) marker.kind = LCE_WAYPOINT_POINT;
        if (pin.kind == LCE_PKG_WAYPOINT_AREA) marker.kind = LCE_WAYPOINT_AREA;
        marker.x = pin.x;
        marker.y = pin.y;
        marker.z = pin.z;
        marker.end_x = pin.end_x;
        marker.end_z = pin.end_z;
        marker.rgb = pin.rgb;
        memcpy(marker.label, pin.label, sizeof marker.label);
        if (pin.layer != scope->layer || !lce_waypoints_put(&store_, scope, &marker)) {
            clear();
            return;
        }
    }
}

const lce_waypoint_store *PackageMarkers::visible(const PackageOwner &owner, const lce_waypoint_scope &scope,
                                                   bool offline) const
{
    if (!offline || !selected(owner) || !scope_matches(owner, scope) || !store_.scope.session ||
        store_.scope.layer != scope.layer)
        return nullptr;
    return &store_;
}

}
