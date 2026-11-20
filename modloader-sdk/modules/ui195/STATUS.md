# ui195 status — UI, sounds, particles (CUSA00265 1.95)

The game registers **no UIScene at runtime** on 1.95 (the scene map `0xe8af4c`
is a 2.72 structure). All mod UI is drawn by the shipping plugin's own
compositor (`mod_menu_frontend_*`, `mods_creative_*`). So ui195 does not open
native screens; it provides the data models the compositor consumes plus small
solid, host-tested pieces, and honest feasibility reasons for the rest.

Evidence: **host** = `tests/test_ui195.cpp` (26 checks); **console** = the r72
compositor stack this would feed.

| Feature | Status | Detail | RVAs |
|---|---|---|---|
| **Creative tabs — multiple Mods pages** | feasible, host-tested | `lce195_ui_creative_plan` / `_page`: 50 items/page, 512-row cap, matching the private Mods group publisher `additive_items_195/creative.cpp` (page helper, 12×`0x20` category array). Console-proven for Steel's 6 items; stock tabs untouched. | group append `0xa336a0`, category array `0x143d020`, TabSpecs guard `0x143d010` |
| **HUD overlay** | feasible, host-tested (model) | `lce195_hud_overlay`: up to 8 lines, anchor, colour; the plugin compositor draws it each frame (it already owns a title/HUD tick detour). Model here, draw in the compositor. | compositor tick `0xa81b50`, title op bridge |
| **Particles — tinted clone** | feasible as data, host-tested | `lce195_ui_particle_check`: a vanilla tintable particle (reddust/mobSpell/blockdust/note families) + a `tint_rgb` is representable by reusing the vanilla texture and overriding the colour. A **new particle texture is not possible** (no particle-atlas injection). | — |
| **Custom sounds** | not possible (bank) / clone yes | Custom audio samples cannot be added: the PS4 sound-bank format is not implemented (gap G11). Vanilla sound **events** can be cloned/remapped and re-pitched through `services195` `play_sound` / `register_sound`. | sound registry `0x1430570`, playSound `0x3f3fe0` |
| **Screens / menus** | not possible natively | No native UIScene registry on 1.95; a full screen must be hand-drawn by the plugin compositor, not registered (gap G12). ui195 gives no screen API. | scene map `0xe8af4c` (2.72 only) |
| **Several distinct Creative/crafting tabs** | not done | A separate stock tab or multiple distinct Creative tabs would edit the 12×`0x20` category array at `0x143d020`; the project rule forbids touching stock tabs, so mod content collapses into the single **Mods** tab with paging (gap G6). | `0x143d020` |

`lce195_ui_feasible()` returns these reasons programmatically (for the loader's
per-feature drop messages and the Mods hub).

## Integration (for the lead)

- Creative: after commit, `lce195_ui_creative_plan(item_count, &layout)`; the
  compositor pages the private Mods group with `lce195_ui_creative_page`. No new
  native call — it reuses the existing `additive_items_195` publish.
- HUD: a mod sets `lce195_ui_hud_set(hud, anchor, rgb, lines, n)`; the compositor
  reads the model in its HUD tick. (The compositor-side draw is the lead's; this
  module only defines and mutates the model.)
- Particles: validate `register_particle` with `lce195_ui_particle_check`; a
  passing descriptor is stored as a tinted clone (no backend publish yet — it is
  data the renderer path would consume once a particle hook exists).

## Not done

- No compositor draw code is included here (that lives in the plugin's
  `mod_menu_frontend_*`). ui195 is models + validation + feasibility only.
- Particle tinting has no runtime publish path yet; it is validated and stored.
