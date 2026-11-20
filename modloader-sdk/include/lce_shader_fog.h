#ifndef LCE_SHADER_FOG_H
#define LCE_SHADER_FOG_H

#include <stdint.h>

#define LCE_SHADER_FOG_CONFIG_KEY_V1 "lce_visual.fog_color_v1"
#define LCE_SHADER_FOG_MAX_WEIGHT_V1 96u

typedef struct lce_shader_fog_v1 {
    uint8_t red, green, blue, weight;
} lce_shader_fog_v1;

/* A quoted "#RRGGBBWW" config value. WW is a blend weight from 00 to 60
 * inclusive. A zero weight disables the request. */
static inline int lce_shader_fog_parse_v1(const char *text, lce_shader_fog_v1 *out) {
    if (!text || !out || text[0] != '#') return 0;
    uint32_t packed = 0;
    for (unsigned byte = 1; byte < 9; ++byte) {
        const char c = text[byte];
        unsigned digit;
        if (c >= '0' && c <= '9') digit = (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') digit = (unsigned)(c - 'a') + 10;
        else if (c >= 'A' && c <= 'F') digit = (unsigned)(c - 'A') + 10;
        else return 0;
        packed = (packed << 4) | digit;
    }
    if (text[9] || (packed & 255u) > LCE_SHADER_FOG_MAX_WEIGHT_V1) return 0;
    const lce_shader_fog_v1 result = {
        (uint8_t)(packed >> 24), (uint8_t)(packed >> 16),
        (uint8_t)(packed >> 8), (uint8_t)packed
    };
    *out = result;
    return 1;
}

#endif
