/* Optional data-only client visual effect for the 1.95 viewport compositor. */
#ifndef LCE_VIEWPORT_TINT_H
#define LCE_VIEWPORT_TINT_H
#include <stdint.h>

#define LCE_VIEWPORT_TINT_CONFIG_KEY_V1 "lce_visual.viewport_tint_v1"
#define LCE_VIEWPORT_TINT_MAX_ALPHA_V1 96u

typedef struct lce_viewport_tint_v1 {
    uint8_t red, green, blue, alpha;
} lce_viewport_tint_v1;

/* Accepts exactly "#RRGGBBAA" and bounds alpha. Alpha zero disables the
 * effect. Returns 1 if valid and leaves the output untouched on failure. */
static inline int lce_viewport_tint_parse_v1(const char *text, lce_viewport_tint_v1 *out) {
    if (!out || !text || text[0] != '#') return 0;
    uint32_t rgba = 0;
    for (unsigned i = 1; i <= 8; ++i) {
        unsigned digit;
        const char c = text[i];
        if (c >= '0' && c <= '9') digit = (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') digit = (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') digit = (unsigned)(c - 'A' + 10);
        else return 0;
        rgba = (rgba << 4) | digit;
    }
    if (text[9] || (rgba & 255u) > LCE_VIEWPORT_TINT_MAX_ALPHA_V1) return 0;
    const lce_viewport_tint_v1 color = {
        (uint8_t)(rgba >> 24), (uint8_t)(rgba >> 16), (uint8_t)(rgba >> 8), (uint8_t)rgba
    };
    *out = color;
    return 1;
}
#endif
