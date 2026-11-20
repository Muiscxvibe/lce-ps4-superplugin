/* ui195: UI / sounds / particles assessment and the small solid pieces for the
 * retail 1.95 backend.
 *
 * The game registers no UIScene at runtime on 1.95 (the scene map 0xe8af4c is a
 * 2.72 structure). All mod UI is drawn by the shipping plugin's own compositor
 * (mod_menu_frontend_*, mods_creative_*). So ui195 does NOT open native screens.
 * What it provides:
 *   - Creative tab paging: lay mod items out into the private Mods Creative
 *     group the way additive_items_195/creative.cpp publishes it (50 per page,
 *     512 rows, 12x0x20 category array at 0x143d020). Pure, host-tested logic
 *     that the compositor consumes; multiple "Mods" pages, no stock tab edits.
 *   - A HUD text overlay plan for the plugin compositor (lines + anchor); the
 *     compositor owns the draw, we own the model.
 *   - Particle clone validation: a vanilla particle + tint is representable as
 *     data; a brand-new particle texture is not (no bank). Returns feasibility.
 *   - Feasibility strings for custom sounds, screens and menus (see STATUS.md).
 *
 * Nothing here patches code or opens a native scene. */
#ifndef LCE_UI195_H
#define LCE_UI195_H
#include <stddef.h>
#include <stdint.h>
#include "../../include/lce_mod_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LCE195_CREATIVE_PER_PAGE 50u
#define LCE195_CREATIVE_CAP      512u

/* ---- creative paging ------------------------------------------------------- */
typedef struct {
    uint32_t total;        /* mod item count (<= 512) */
    uint32_t pages;        /* ceil(total / 50) */
} lce195_creative_layout;
/* Plan the Mods Creative pages for `count` items. Returns 1, or 0 if count
 * exceeds the 512-row capacity. */
int lce195_ui_creative_plan(uint32_t count, lce195_creative_layout *out);
/* Items on a page: writes the item indices [page*50 .. ) into out (<=50),
 * returns how many. 0 for an out-of-range page. */
uint32_t lce195_ui_creative_page(const lce195_creative_layout *l, uint32_t page, uint32_t *out, uint32_t cap);

/* ---- HUD overlay model (compositor-drawn) --------------------------------- */
typedef enum { LCE195_HUD_TOPLEFT = 0, LCE195_HUD_TOPRIGHT, LCE195_HUD_BOTTOMLEFT, LCE195_HUD_BOTTOMRIGHT } lce195_hud_anchor;
#define LCE195_HUD_MAX_LINES 8u
#define LCE195_HUD_LINE 64u
typedef struct {
    uint8_t anchor;        /* lce195_hud_anchor */
    uint8_t visible;
    uint8_t line_count;
    uint8_t reserved;
    uint32_t color_rgb;
    char lines[LCE195_HUD_MAX_LINES][LCE195_HUD_LINE];
} lce195_hud_overlay;
/* Set a mod's HUD lines (UTF-8, truncated). Returns 1. The compositor reads the
 * model each frame; this only mutates it. */
int lce195_ui_hud_set(lce195_hud_overlay *hud, lce195_hud_anchor anchor, uint32_t rgb,
                      const char *const *lines, uint32_t line_count);
void lce195_ui_hud_clear(lce195_hud_overlay *hud);

/* ---- particles ------------------------------------------------------------- */
/* Vanilla 1.95 particle name -> 1 if it is a tintable base particle (reddust,
 * mobSpell...), else 0. */
int lce195_ui_particle_tintable(const char *vanilla_name);
/* Validate a mod particle descriptor. 1 = representable as a tinted clone of a
 * vanilla particle; 0 with a reason otherwise (new texture / unknown base). */
int lce195_ui_particle_check(const lce_particle_desc *d, char *err, uint32_t err_cap);

/* ---- feasibility (for STATUS and the loader's per-feature reasons) --------- */
typedef enum { LCE195_FEAT_CREATIVE_PAGES = 0, LCE195_FEAT_HUD, LCE195_FEAT_PARTICLE_TINT,
               LCE195_FEAT_CUSTOM_SOUND, LCE195_FEAT_SCREEN, LCE195_FEAT_CUSTOM_TAB } lce195_ui_feature;
/* 1 = feasible now, 0 = not; *why points to a static reason string. */
int lce195_ui_feasible(lce195_ui_feature f, const char **why);

#ifdef __cplusplus
}
#endif
#endif
