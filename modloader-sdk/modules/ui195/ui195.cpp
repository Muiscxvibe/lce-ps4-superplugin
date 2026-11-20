// ui195: creative paging, HUD model, particle checks, feasibility. See ui195.h.
#include "ui195.h"
#include <string.h>
#include <stdio.h>

extern "C" int lce195_ui_creative_plan(uint32_t count, lce195_creative_layout *out) {
    if (!out || count > LCE195_CREATIVE_CAP) return 0;
    out->total = count;
    out->pages = count ? (count + LCE195_CREATIVE_PER_PAGE - 1) / LCE195_CREATIVE_PER_PAGE : 0;
    return 1;
}

extern "C" uint32_t lce195_ui_creative_page(const lce195_creative_layout *l, uint32_t page, uint32_t *out, uint32_t cap) {
    if (!l || !out || page >= l->pages) return 0;
    uint32_t first = page * LCE195_CREATIVE_PER_PAGE;
    uint32_t n = l->total - first;
    if (n > LCE195_CREATIVE_PER_PAGE) n = LCE195_CREATIVE_PER_PAGE;
    if (n > cap) n = cap;
    for (uint32_t i = 0; i < n; ++i) out[i] = first + i;
    return n;
}

extern "C" int lce195_ui_hud_set(lce195_hud_overlay *hud, lce195_hud_anchor anchor, uint32_t rgb,
                                 const char *const *lines, uint32_t line_count) {
    if (!hud || line_count > LCE195_HUD_MAX_LINES) return 0;
    memset(hud, 0, sizeof *hud);
    hud->anchor = (uint8_t)anchor;
    hud->color_rgb = rgb;
    hud->visible = 1;
    hud->line_count = (uint8_t)line_count;
    for (uint32_t i = 0; i < line_count; ++i) {
        const char *src = lines && lines[i] ? lines[i] : "";
        size_t n = 0;
        while (src[n] && n + 1 < LCE195_HUD_LINE) { hud->lines[i][n] = src[n]; ++n; }
        hud->lines[i][n] = 0;
    }
    return 1;
}
extern "C" void lce195_ui_hud_clear(lce195_hud_overlay *hud) { if (hud) memset(hud, 0, sizeof *hud); }

// Tintable 1.95 particles: those whose render colour is taken from the spawn
// data rather than a fixed sprite tint (reddust/mobSpell families). A clone with
// a tint reuses the vanilla texture and overrides the colour.
extern "C" int lce195_ui_particle_tintable(const char *name) {
    static const char *const kTintable[] = {
        "reddust", "mobSpell", "mobSpellAmbient", "spell", "instantSpell",
        "blockdust", "fallingdust", "note", "happyVillager",
    };
    if (!name) return 0;
    if (!strncmp(name, "minecraft:", 10)) name += 10;
    for (const char *t : kTintable) if (!strcmp(t, name)) return 1;
    return 0;
}

extern "C" int lce195_ui_particle_check(const lce_particle_desc *d, char *err, uint32_t cap) {
    auto fail = [&](const char *m) { if (err && cap) snprintf(err, cap, "%s", m); return 0; };
    if (!d || !d->key) return fail("particle: key required");
    if (d->texture) return fail("particle: a custom texture is not possible on 1.95 (no particle atlas injection); omit texture to tint a vanilla particle");
    if (!d->clone_of) return fail("particle: clone_of (a vanilla 1.95 particle) is required");
    if (!lce195_ui_particle_tintable(d->clone_of)) return fail("particle: clone_of is not a tintable 1.95 particle (reddust/mobSpell/blockdust/note families)");
    if (err && cap) err[0] = 0;
    return 1;   // representable as data: a tinted clone of a vanilla particle
}

extern "C" int lce195_ui_feasible(lce195_ui_feature f, const char **why) {
    static const char *const yes_creative = "Mods Creative pages (50/page, 512 cap) via the private Mods group; console-proven for Steel's 6 items";
    static const char *const yes_hud = "HUD text overlay via the plugin compositor (mod_menu_frontend_*); model here, draw in the compositor";
    static const char *const yes_particle = "tinted clone of a vanilla particle (reuses its texture) as data; no new particle textures";
    static const char *const no_sound = "custom audio samples impossible: the PS4 sound-bank format is not implemented (gap G11). Vanilla sound events can be cloned/remapped (play_sound) and re-pitched, not replaced";
    static const char *const no_screen = "no native UIScene registry on 1.95 (scene map 0xe8af4c is a 2.72 structure); a full screen must be drawn by the plugin compositor, not registered (gap G12)";
    static const char *const no_tab = "a separate stock-tab or multiple distinct Creative tabs would edit the 12x0x20 category array at 0x143d020 (project rule forbids touching stock tabs); mod content collapses into the single Mods tab (gap G6)";
    const char *w; int ok;
    switch (f) {
    case LCE195_FEAT_CREATIVE_PAGES: w = yes_creative; ok = 1; break;
    case LCE195_FEAT_HUD: w = yes_hud; ok = 1; break;
    case LCE195_FEAT_PARTICLE_TINT: w = yes_particle; ok = 1; break;
    case LCE195_FEAT_CUSTOM_SOUND: w = no_sound; ok = 0; break;
    case LCE195_FEAT_SCREEN: w = no_screen; ok = 0; break;
    case LCE195_FEAT_CUSTOM_TAB: w = no_tab; ok = 0; break;
    default: w = "unknown feature"; ok = 0; break;
    }
    if (why) *why = w;
    return ok;
}
