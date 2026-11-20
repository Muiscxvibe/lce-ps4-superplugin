#include "lce_waypoints.h"

#include <math.h>
#include <string.h>

typedef struct radar_point {
    double x, y;
} radar_point;

static int valid_scope(const lce_waypoint_scope *scope)
{
    return scope && scope->session && scope->player < 4;
}

static int matches(const lce_waypoint_store *store, const lce_waypoint_scope *scope)
{
    return store && valid_scope(scope) && store->count <= LCE_WAYPOINT_LIMIT &&
           store->scope.session == scope->session && store->scope.layer == scope->layer &&
           store->scope.player == scope->player;
}

static int coordinate(double value)
{
    return value >= -LCE_WAYPOINT_COORD_LIMIT && value <= LCE_WAYPOINT_COORD_LIMIT;
}

static int valid_marker(const lce_waypoint *marker)
{
    if (!marker || !marker->id || marker->rgb > UINT32_C(0xffffff) ||
        !coordinate(marker->x) || !coordinate(marker->y) || !coordinate(marker->z))
        return 0;
    if (marker->kind == LCE_WAYPOINT_AREA) {
        if (!coordinate(marker->end_x) || !coordinate(marker->end_z) ||
            marker->x == marker->end_x || marker->z == marker->end_z)
            return 0;
    } else if (marker->kind != LCE_WAYPOINT_POINT) {
        return 0;
    }
    for (size_t i = 0; i < sizeof marker->label; ++i) {
        unsigned char ch = (unsigned char)marker->label[i];
        if (!ch)
            return 1;
        if (ch < 32 || ch > 126)
            return 0;
    }
    return 0;
}

int lce_waypoints_reset(lce_waypoint_store *store, const lce_waypoint_scope *scope)
{
    if (!store)
        return 0;
    lce_waypoint_scope binding = {0, 0, 0};
    if (valid_scope(scope))
        binding = *scope;
    memset(store, 0, sizeof *store);
    store->scope = binding;
    return binding.session != 0;
}

int lce_waypoints_put(lce_waypoint_store *store, const lce_waypoint_scope *scope,
                      const lce_waypoint *marker)
{
    if (!matches(store, scope) || !valid_marker(marker))
        return 0;
    size_t entry = 0;
    while (entry < store->count && store->markers[entry].id != marker->id)
        ++entry;
    if (entry == LCE_WAYPOINT_LIMIT)
        return 0;
    lce_waypoint copy = *marker;
    size_t label_length = strlen(copy.label);
    memset(copy.label + label_length, 0, sizeof copy.label - label_length);
    if (copy.kind == LCE_WAYPOINT_POINT) {
        copy.end_x = copy.x;
        copy.end_z = copy.z;
    }
    store->markers[entry] = copy;
    if (entry == store->count)
        ++store->count;
    return 1;
}

int lce_waypoints_remove(lce_waypoint_store *store, const lce_waypoint_scope *scope,
                         uint32_t id)
{
    if (!matches(store, scope) || !id)
        return 0;
    for (size_t i = 0; i < store->count; ++i) {
        if (store->markers[i].id == id) {
            --store->count;
            store->markers[i] = store->markers[store->count];
            memset(&store->markers[store->count], 0, sizeof store->markers[0]);
            return 1;
        }
    }
    return 0;
}

static double clamp(double value, double low, double high)
{
    if (value < low)
        return low;
    if (value > high)
        return high;
    return value;
}

static radar_point radar(const lce_waypoint_view *view, double sine, double cosine,
                         double x, double z)
{
    double dx = (x - view->x) / view->range;
    double dz = (z - view->z) / view->range;
    if (!view->heading_up)
        return (radar_point){dx, dz};
    return (radar_point){-dx * cosine - dz * sine, dx * sine - dz * cosine};
}

/* Liang–Barsky interval clipping. It handles axis-aligned edges without
 * dividing by zero. The interval t describes the visible part of a segment. */
static int clip_edge(double p, double q, double *first, double *last)
{
    if (p == 0)
        return q >= 0;
    double t = q / p;
    if (p < 0) {
        if (t > *first)
            *first = t;
    } else if (t < *last) {
        *last = t;
    }
    return *first <= *last;
}

static int clip(radar_point a, radar_point b, lce_waypoint_segment *segment)
{
    double dx = b.x - a.x;
    double dy = b.y - a.y;
    double first = 0.0;
    double last = 1.0;
    if (!clip_edge(-dx, a.x + 1.0, &first, &last) ||
        !clip_edge(dx, 1.0 - a.x, &first, &last) ||
        !clip_edge(-dy, a.y + 1.0, &first, &last) ||
        !clip_edge(dy, 1.0 - a.y, &first, &last))
        return 0;
    *segment = (lce_waypoint_segment){
        clamp(a.x + first * dx, -1.0, 1.0), clamp(a.y + first * dy, -1.0, 1.0),
        clamp(a.x + last * dx, -1.0, 1.0), clamp(a.y + last * dy, -1.0, 1.0)
    };
    return 1;
}

static int project_marker(const lce_waypoint *marker, const lce_waypoint_view *view,
                           double sine, double cosine, lce_waypoint_glyph *glyph)
{
    const radar_point start = radar(view, sine, cosine, marker->x, marker->z);
    double nearest_x = marker->x;
    double nearest_z = marker->z;
    if (marker->kind == LCE_WAYPOINT_POINT) {
        if (start.x < -1.0 || start.x > 1.0 || start.y < -1.0 || start.y > 1.0)
            return 0;
        glyph->x = start.x;
        glyph->y = start.y;
    } else {
        radar_point corners[4] = {
            start,
            radar(view, sine, cosine, marker->end_x, marker->z),
            radar(view, sine, cosine, marker->end_x, marker->end_z),
            radar(view, sine, cosine, marker->x, marker->end_z)
        };
        for (size_t i = 0; i < 4; ++i) {
            if (clip(corners[i], corners[(i + 1) % 4], &glyph->segments[glyph->segment_count]))
                ++glyph->segment_count;
        }
        if (!glyph->segment_count)
            return 0;
        nearest_x = clamp(view->x, fmin(marker->x, marker->end_x), fmax(marker->x, marker->end_x));
        nearest_z = clamp(view->z, fmin(marker->z, marker->end_z), fmax(marker->z, marker->end_z));
    }
    double dx = nearest_x - view->x;
    double dz = nearest_z - view->z;
    glyph->id = marker->id;
    glyph->kind = marker->kind;
    glyph->rgb = marker->rgb;
    glyph->distance_squared = dx * dx + dz * dz;
    memcpy(glyph->label, marker->label, sizeof glyph->label);
    return 1;
}

size_t lce_waypoints_project(const lce_waypoint_store *store, const lce_waypoint_view *view,
                             lce_waypoint_glyph *out, size_t capacity)
{
    if (!out || !capacity || !view || !matches(store, &view->scope) ||
        !coordinate(view->x) || !coordinate(view->y) || !coordinate(view->z) ||
        !(view->range >= LCE_WAYPOINT_RANGE_MIN && view->range <= LCE_WAYPOINT_RANGE_MAX) ||
        view->heading_up > 1 || !isfinite(view->yaw_degrees))
        return 0;

    double sine = 0.0;
    double cosine = 1.0;
    if (view->heading_up) {
        double radians = fmod(view->yaw_degrees, 360.0) * 0.017453292519943295;
        sine = sin(radians);
        cosine = cos(radians);
    }

    size_t count = 0;
    for (size_t i = 0; i < store->count; ++i) {
        const lce_waypoint *marker = &store->markers[i];
        lce_waypoint_glyph glyph = {0};
        if (!valid_marker(marker) || !project_marker(marker, view, sine, cosine, &glyph))
            continue;
        size_t index = 0;
        while (index < count &&
               (out[index].distance_squared < glyph.distance_squared ||
                (out[index].distance_squared == glyph.distance_squared && out[index].id < glyph.id)))
            ++index;
        if (index == capacity)
            continue;
        if (count < capacity)
            ++count;
        memmove(out + index + 1, out + index, (count - index - 1) * sizeof *out);
        out[index] = glyph;
    }
    return count;
}
