#include "f3_waypoint_draw.hpp"

#include <string.h>

namespace mc195_f3_waypoints {
namespace {
constexpr int panel = 96;
constexpr double range = 64.0;
constexpr size_t visible_limit = 8;
using menu_render::DirectColor;
using menu_render::DirectSink;

int pixel(double normalized)
{
    return 3 + static_cast<int>((normalized + 1.0) * (panel - 7) * 0.5 + 0.5);
}

bool segment(const DirectSink &sink, int left, int top, const lce_waypoint_segment &edge,
             DirectColor color)
{
    int x = pixel(edge.x0);
    int y = pixel(edge.y0);
    const int x1 = pixel(edge.x1);
    const int y1 = pixel(edge.y1);
    const int dx = x < x1 ? x1 - x : x - x1;
    const int dy = y < y1 ? y - y1 : y1 - y;
    const int step_x = x < x1 ? 1 : -1;
    const int step_y = y < y1 ? 1 : -1;
    int error = dx + dy;
    for (int step = 0; step < panel; ++step) {
        if (!sink.rect(sink.context, left + x, top + y, 1, 1, color))
            return false;
        if (x == x1 && y == y1)
            return true;
        const int twice = 2 * error;
        if (twice >= dy) {
            error += dy;
            x += step_x;
        }
        if (twice <= dx) {
            error += dx;
            y += step_y;
        }
    }
    return false;
}

bool marker(const DirectSink &sink, int left, int top, const lce_waypoint_glyph &glyph)
{
    const DirectColor color{uint8_t(glyph.rgb >> 16), uint8_t(glyph.rgb >> 8),
                            uint8_t(glyph.rgb), 255};
    if (glyph.kind == LCE_WAYPOINT_POINT) {
        return sink.rect(sink.context, left + pixel(glyph.x) - 1,
                         top + pixel(glyph.y) - 1, 3, 3, color);
    }
    for (uint32_t edge = 0; edge < glyph.segment_count; ++edge) {
        if (!segment(sink, left, top, glyph.segments[edge], color))
            return false;
    }
    return true;
}
}

bool scope(unsigned slot, uint64_t session, const mc195_f3_reply &reply, lce_waypoint_scope *out)
{
    if (!out || slot >= 4 || !session || !reply.tick || !reply.sampled_us ||
        reply.world <= session || reply.world - session > 3)
        return false;
    *out = {session, static_cast<uint32_t>(reply.world - session - 1), slot};
    return true;
}

bool draw(const DirectSink &sink, int x, int y, int width, int height,
          unsigned slot, uint64_t session, const mc195_f3_reply &reply,
          const lce_waypoint_store &store)
{
    if (x < 0 || y < 0 || width < panel + 12 || height < panel + 82 ||
        width > 2048 || height > 2048 || x > 2048 - width || y > 2048 - height)
        return true;
    lce_waypoint_view view{};
    if (!scope(slot, session, reply, &view.scope))
        return true;
    view.x = reply.sample.x;
    view.y = reply.sample.y;
    view.z = reply.sample.z;
    view.yaw_degrees = reply.sample.yaw;
    view.range = range;
    view.heading_up = 1;
    lce_waypoint_glyph glyphs[visible_limit]{};
    const size_t count = lce_waypoints_project(&store, &view, glyphs, visible_limit);
    if (!count)
        return true;
    if (!sink.clip || !sink.rect || !sink.text)
        return false;

    const int left = x + 6;
    const int top = y + 68;
    const int center = panel / 2;
    const DirectColor background{10, 16, 24, 200};
    const DirectColor marker_color{244, 245, 248, 255};
    const char heading[] = "Waypoints";
    if (!sink.clip(sink.context, x, y, width, height) ||
        !sink.text(sink.context, left, top - 7, panel, 7, heading, sizeof heading - 1, marker_color) ||
        !sink.rect(sink.context, left, top, panel, panel, background))
        return false;
    for (size_t i = count; i > 0; --i) {
        if (!marker(sink, left, top, glyphs[i - 1]))
            return false;
    }
    if (!sink.rect(sink.context, left + center - 2, top + center, 5, 1, marker_color) ||
        !sink.rect(sink.context, left + center, top + center - 2, 1, 5, marker_color))
        return false;
    const char *label = glyphs[0].label;
    const size_t length = strlen(label);
    return !length || sink.text(sink.context, left, top + panel + 1, width - 12, 9,
                                label, length, marker_color);
}

}
