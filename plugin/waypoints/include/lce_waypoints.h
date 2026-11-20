#ifndef LCE_WAYPOINTS_H
#define LCE_WAYPOINTS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LCE_WAYPOINT_LIMIT 64u
#define LCE_WAYPOINT_LABEL_CAPACITY 25u
#define LCE_WAYPOINT_COORD_LIMIT 30000000.0
#define LCE_WAYPOINT_RANGE_MIN 16.0
#define LCE_WAYPOINT_RANGE_MAX 2048.0

typedef struct lce_waypoint_scope {
    uint64_t session;
    uint32_t layer; /* Opaque dimension key assigned by the world owner. */
    uint32_t player; /* Local player slot, 0 through 3. */
} lce_waypoint_scope;

typedef enum lce_waypoint_kind {
    LCE_WAYPOINT_POINT = 1,
    LCE_WAYPOINT_AREA = 2
} lce_waypoint_kind;

typedef struct lce_waypoint {
    uint32_t id; /* Nonzero and unique in a store. */
    uint32_t kind;
    double x, y, z;
    double end_x, end_z; /* Opposite corner for areas. Ignored for points. */
    uint32_t rgb; /* 0xRRGGBB */
    char label[LCE_WAYPOINT_LABEL_CAPACITY]; /* Printable ASCII, NUL-terminated. */
} lce_waypoint;

/* Caller owns the storage and serializes reads/writes. Zero-init before binding
 * or bind with lce_waypoints_reset. Callers supply their own authored/discovered
 * positions; this API does not read world, render or network state. */
typedef struct lce_waypoint_store {
    lce_waypoint_scope scope;
    size_t count;
    lce_waypoint markers[LCE_WAYPOINT_LIMIT];
} lce_waypoint_store;

typedef struct lce_waypoint_view {
    lce_waypoint_scope scope;
    double x, y, z;
    double yaw_degrees; /* Minecraft yaw: 0 faces +Z, 90 faces -X. */
    double range; /* Half-width of the radar in blocks. */
    uint32_t heading_up; /* 0: north is up; 1: the player's heading is up. */
} lce_waypoint_view;

typedef struct lce_waypoint_segment {
    double x0, y0, x1, y1; /* Normalized radar space. Each axis lies in [-1, 1]. */
} lce_waypoint_segment;

typedef struct lce_waypoint_glyph {
    uint32_t id, kind, rgb;
    char label[LCE_WAYPOINT_LABEL_CAPACITY];
    double x, y; /* Normalized point position. */
    double distance_squared; /* Horizontal distance to the point or area. */
    uint32_t segment_count;
    lce_waypoint_segment segments[4]; /* Clipped outlines for areas. */
} lce_waypoint_glyph;

/* Reset clears the store even when scope is NULL or invalid, then returns 1
 * only when a usable scope was supplied. */
int lce_waypoints_reset(lce_waypoint_store *store, const lce_waypoint_scope *scope);
/* Insert/replace/remove for the exact bound scope. Failure leaves the store
 * unchanged. Either order of opposite area corners is accepted. */
int lce_waypoints_put(lce_waypoint_store *store, const lce_waypoint_scope *scope,
                      const lce_waypoint *marker);
int lce_waypoints_remove(lce_waypoint_store *store, const lce_waypoint_scope *scope,
                         uint32_t id);
/* Returns at most capacity glyphs sorted by increasing distance then ID.
 * The view is a square, so a corner can be range * sqrt(2) blocks away.
 * An area covering the radar with every outline edge offscreen is omitted.
 * Invalid input or a scope mismatch returns zero without writing to out.
 * Output storage must not overlap store or view. */
size_t lce_waypoints_project(const lce_waypoint_store *store, const lce_waypoint_view *view,
                             lce_waypoint_glyph *out, size_t capacity);

#ifdef __cplusplus
}
#endif

#endif
