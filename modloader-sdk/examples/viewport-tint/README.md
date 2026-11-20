# Viewport tint for CUSA00265 1.95

This optional data-only effect blends a uniform RGBA color over the primary local player's full-screen view at post-UI Present. A port can use it for a fixed color cast. Stock HUD elements beneath the effect receive the same tint as the world. The effect suspends for the Debug Menu, Mods menu, F3 with its waypoint panel, or a player overlay already drawn for the frame.

Build the example from `modloader-sdk` with `python3 tools/lce_modpack.py build examples/viewport-tint build/pkg`. Install its `mod.lcm` and `config` together in `/data/lce-mods/viewtint/`, then enable it through the existing mod activation flow and restart. The `config` setting is `lce_visual.viewport_tint_v1`, a quoted `#RRGGBBAA` string. Alpha `00` disables the request and `60` is the maximum. The loader ignores invalid requests and picks the lexicographically first enabled, committed mod ID with a valid nonzero tint.

The offline-preview PS4 build and SDK must both use `LCE_EXPERIMENTAL_VIEWPORT_TINT=1`. The adapter requires the existing exact 1.95 post-UI compositor, an offline local world, a current controller watch, exactly one attached player, and a full-target viewport. The compositor validates the target and retains its draw resources through GPU retirement. An unavailable lease or missing prerequisites skip the frame.

The effect uses the plugin-owned compositor shader and its fixed source-over blend. Gameplay shader packs, custom bytecode, depth sampling, lighting and material changes require a gameplay renderer contract. This candidate covers the full-screen local player only.

Run `bash tests/test_viewport_tint.sh` from `modloader-sdk` for loader and compositor-adapter tests. Pass the patched `r72-session-candidate` directory as the first argument if it is in a different location. Console visual and frame-time validation are pending.
