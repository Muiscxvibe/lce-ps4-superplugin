# LCE mod loader SDK for Minecraft PS4 Edition 1.95 (CUSA00265)

A versioned mod API, loader, console backend, tools and docs for porting content mods to Minecraft PS4 Edition. [INTEGRATION.md](INTEGRATION.md) describes the shipping integration, experimental features and console test plan.

| Doc | For |
|---|---|
| [MODDING.md](MODDING.md) | **Mod authors.** Quick start, the `mod.json` reference, the retail profile (§4.1), scripting and services (§8), the native C API |
| [PORTING.md](PORTING.md) | Converting Java (Forge/Fabric) and Bedrock mods |
| [API_STATUS.md](API_STATUS.md) | §0 coverage matrix (console-ready / simulated / impossible, with RVAs), the mechanisms, gaps G1–G16 |
| [INTEGRATION.md](INTEGRATION.md) | Startup phases, the shipping patch, anticheat consequences, multiplayer, the numbered on-console test plan |

## Layout

```
include/lce_mod_api.h          versioned C ABI (1.12): descriptors, lifecycle, events, services, lce_api table
include/lce_mod_package.h      mod.lcm binary format (v1)
include/lce_tick_pacing.h      mod-local server tick pacing for native callbacks
loader/                        core (C++17, no exceptions/RTTI/STL): package parser, ID ledger, loader, schema-driven 1.1 content
loader/retail195/              the console backend:
  composite195.{h,cpp}           composite "retail195" backend + startup phase dispatcher
  backend195.{h,cpp}             items (every kind via items195), ABI-2 recipes, smelting, brewing
  services_glue195.cpp           events195/services195 glue (sounds, commands, keybinds, strings, services)
  host_ps4.cpp, id_space_195.h, oneshot195.{h,c}
modules/                       host-tested backend modules, each with STATUS.md, test.sh, ps4check.sh:
  items195 blocks195 entities195 worldgen195 events195 script (Lua 5.4.8) ui195 netsync195
  strings195 (Localizer ids) effects195 (effects + potions) enchants195 biomes195 audio195 (custom sounds)
  fuel195 (experimental custom furnace fuel)
server/                        PC server side of the multiplayer ID handshake (server-sync.patch)
integration/                   sdk_boot.cpp (SDK-only startup owner), shipping-plugin.patch, patch-base.sha256, verify-patch.sh
third_party/r72/               verbatim seam headers from r72-session-candidate (hashes in SOURCES.txt)
tools/                         lce_modpack.py (new/check/build/dump, retail profile), lce_port.py (Java + Bedrock), vanilla tables
tests/                         host tests (g++ + ASan/UBSan), fixtures (retailall = everything the console accepts, allcontent = everything the format allows)
```

## Build and test

```sh
make test                              # every host suite: core, composite end-to-end, every module, netsync, server, python tools
./build-ps4.sh                         # build/ps4/lcemodloader.o with the OpenOrbis toolchain (+ undefined.txt)
integration/verify-patch.sh /tmp/v     # base hashes, patch dry-run, compile with shipping flags, full plugin link
```

| Test | Checks |
|---|---|
| `test_sha256`, `test_package`, `test_ledger`, `test_loader`, `test_ext`, `test_oneshot` | core: FIPS vectors, 35k package mutations, stable IDs and reserved ranges, loader lifecycle and fail-closed paths, 1.1 schemas, one-shot `.text` restore |
| `test_retail195`, `test_retail195_bridge` | items through the items195 constructors and ABI-2 recipes on a fake image; the same provider through the **real** r72 bridge/window/catalog code |
| `test_composite195` | Fork-isolated scenarios cover the startup phases, fail-closed paths, and packaged Lua registration and event dispatch. `--verdicts` prints per-content verdicts. |
| `tests/test_tools.py` | Converter, deterministic builds, and parity between the Python `retail195` profile and the C backend, including biome filters. |
| `make modules-test` | Backend, Lua, network and server suites. Image, virtual environment and toolchain checks can skip when their inputs are absent. |

## What works, what does not (summary; API_STATUS.md §0 is the full matrix)

- **Console-ready (pattern-proven, host-tested, not yet run through the SDK on a PS4):** generic items, tool/sword classes (Steel's classes), names, icons, the Mods Creative tab, shaped recipes, the server-tick/join/item-use/block-break event hooks, item display names, persistent IDs.
- **Simulated / host-tested only (wired, needs the console run):**
  - items and recipes: armour, food (vanilla or mod eat effect), shapeless/count/aux recipes, smelting;
  - effects and potions: **new status effects** (strings195 + effects195), **new potion types** with splash/lingering/arrow forms, brewing between stock **and mod** potions;
  - **new enchantments** (enchants195);
  - world: opaque blocks (data hook), entity clones (types, AI, spawns, eggs, renderers), ores/trees/lakes/structures, **new overworld biomes** (biomes195);
  - audio and visuals: sound clones, **custom audio from WAV** (audio195), and tinted stock particles from event callbacks;
  - localisation, including mod string ids in the game's Localizer;
  - Lua scripts with texture, block, mob, biome and structure registration, server tick pacing, services, commands/keybinds, and multiplayer item/block ID sync.
- **Experimental fuel:** custom fuel is opt-in. A PS4 test verified manually placed inline and explicit custom fuels in a furnace, including save and reload. Hopper and Quick Move behavior remain unverified. See [INTEGRATION.md](INTEGRATION.md) for the build flag, profile and limitations.
- **Not possible on 1.95 yet (refused with the reason):**
  - dimensions, custom mob skins and loot, custom armour layers, native screens/menus;
  - per-type limits: identity-checked effect/enchantment clones, new effect icon art, nether/end biomes.
