#pragma once
#include "../include/lce_viewport_tint.h"
#include "../loader/loader.h"
#include <string.h>

namespace lce_viewport_tint {
inline uint32_t committed_color(lce_loader *loader) {
    const lce_api *api = lce_loader_api(loader);
    if (!api || !api->config_string || lce_loader_phase(loader) != LCE_PHASE_RUNNING) return 0;

    uint32_t rgba = 0;
    const char *selected = nullptr;
    for (uint32_t i = 0; i < lce_loader_mod_count(loader); ++i) {
        const lce_mod_report *mod = lce_loader_mod(loader, i);
        if (!mod || mod->state != LCE_MOD_LIVE || (selected && strcmp(mod->modid, selected) >= 0)) continue;
        const char *text = api->config_string(lce_loader_mod_ctx(loader, i), LCE_VIEWPORT_TINT_CONFIG_KEY_V1, nullptr);
        lce_viewport_tint_v1 color{};
        if (!lce_viewport_tint_parse_v1(text, &color) || !color.alpha) continue;
        selected = mod->modid;
        rgba = (uint32_t(color.red) << 24) | (uint32_t(color.green) << 16) |
               (uint32_t(color.blue) << 8) | color.alpha;
    }
    return rgba;
}
}
