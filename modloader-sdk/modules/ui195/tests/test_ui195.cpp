#include "../ui195.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks;
#define CHECK(c) do { ++g_checks; if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

int main() {
    // creative paging
    lce195_creative_layout l{};
    CHECK(lce195_ui_creative_plan(0, &l) == 1 && l.pages == 0);
    CHECK(lce195_ui_creative_plan(50, &l) == 1 && l.pages == 1);
    CHECK(lce195_ui_creative_plan(51, &l) == 1 && l.pages == 2);
    CHECK(lce195_ui_creative_plan(512, &l) == 1 && l.pages == 11);
    CHECK(lce195_ui_creative_plan(513, &l) == 0);   // over capacity
    CHECK(lce195_ui_creative_plan(120, &l) == 1 && l.pages == 3);
    uint32_t page[50];
    CHECK(lce195_ui_creative_page(&l, 0, page, 50) == 50 && page[0] == 0 && page[49] == 49);
    CHECK(lce195_ui_creative_page(&l, 2, page, 50) == 20 && page[0] == 100 && page[19] == 119);
    CHECK(lce195_ui_creative_page(&l, 3, page, 50) == 0);

    // HUD model
    lce195_hud_overlay hud{};
    const char *lines[] = {"Score: 10", "Wave 3"};
    CHECK(lce195_ui_hud_set(&hud, LCE195_HUD_TOPRIGHT, 0xffcc00, lines, 2) == 1);
    CHECK(hud.visible && hud.line_count == 2 && hud.anchor == LCE195_HUD_TOPRIGHT);
    CHECK(!strcmp(hud.lines[0], "Score: 10") && !strcmp(hud.lines[1], "Wave 3"));
    CHECK(lce195_ui_hud_set(&hud, LCE195_HUD_TOPLEFT, 0, lines, 9) == 0);   // too many lines
    lce195_ui_hud_clear(&hud); CHECK(!hud.visible && hud.line_count == 0);

    // particles
    CHECK(lce195_ui_particle_tintable("reddust") == 1);
    CHECK(lce195_ui_particle_tintable("minecraft:mobSpell") == 1);
    CHECK(lce195_ui_particle_tintable("flame") == 0);
    char err[160];
    lce_particle_desc pd{}; pd.struct_size = sizeof pd; pd.key = "m:spark"; pd.clone_of = "reddust"; pd.tint_rgb = 0xff0000;
    CHECK(lce195_ui_particle_check(&pd, err, sizeof err) == 1);
    pd.texture = "res/custom.png";
    CHECK(lce195_ui_particle_check(&pd, err, sizeof err) == 0 && strstr(err, "custom texture"));
    pd.texture = nullptr; pd.clone_of = "flame";
    CHECK(lce195_ui_particle_check(&pd, err, sizeof err) == 0 && strstr(err, "tintable"));

    // feasibility
    const char *why = nullptr;
    CHECK(lce195_ui_feasible(LCE195_FEAT_CREATIVE_PAGES, &why) == 1 && why);
    CHECK(lce195_ui_feasible(LCE195_FEAT_HUD, &why) == 1);
    CHECK(lce195_ui_feasible(LCE195_FEAT_PARTICLE_TINT, &why) == 1);
    CHECK(lce195_ui_feasible(LCE195_FEAT_CUSTOM_SOUND, &why) == 0 && strstr(why, "sound-bank"));
    CHECK(lce195_ui_feasible(LCE195_FEAT_SCREEN, &why) == 0 && strstr(why, "UIScene"));
    CHECK(lce195_ui_feasible(LCE195_FEAT_CUSTOM_TAB, &why) == 0 && strstr(why, "0x143d020"));

    printf("ui195: ALL %d CHECKS PASSED\n", g_checks);
    return 0;
}
