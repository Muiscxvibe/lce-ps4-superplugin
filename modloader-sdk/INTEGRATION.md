# Integrating the SDK into the shipping super plugin

**Audience:** the main engineer. Nothing here has been installed on the console and no shipping file was edited. `integration/shipping-plugin.patch` is written against the **current** shipping files; their hashes are in `integration/patch-base.sha256` (taken 2026-11-18 04:2x).

**Verified on the host** (`integration/verify-patch.sh OUTDIR`, which writes only into OUTDIR):
- the 9 base hashes match the live files;
- `patch -p0 --dry-run`, then a real apply, succeed on fresh copies;
- every patched translation unit compiles with the exact `build-crashlog.sh` flags (ABI overlay included). The **unpatched** originals compiled the same way are byte-identical to the objects in `crashlog-build/build/`, so the rebuild step in the patch reproduces the shipping objects;
- the complete `super195.elf` links with the patched objects and `lcemodloader.o` (shipping `ld.lld` line), and `create-fself` produces a PRX. The linked ELF imports **no** dynamic symbol that the current shipping ELF does not already import.

## 1. Deliverables

| Path | What |
|---|---|
| `./build-ps4.sh` → `build/ps4/lcemodloader.o` (+ `liblcemodloader.a`) | One relocatable object (54 objects): core loader, composite retail195 backend, PS4 host services, `integration/sdk_boot.cpp`, modules items195 / blocks195 / entities195 / worldgen195 / events195 (+ services) / ui195 / netsync195, Lua 5.4.8 core (no io/os/package/debug/linit) and the script sandbox. Shipping target and flags; events195 is built with `-fexceptions` as its STATUS requires |
| `build/ps4/undefined.txt` | 77 externals, all resolved by the shipping link: libc (string, ctype, math, malloc, setjmp, errno, time/clock, strtod/strcoll), libkernel (`sceKernelOpen/Read/Write/Close/Fsync/Stat/Mkdir/Rename/Unlink/Getdents/QueryMemoryProtection/GetProcessTime`, `scePthreadGetthreadid`), the C++ EH runtime the plugin already imports (`__cxa_begin_catch`, `__gxx_personality_v0`, `_Unwind_Resume`, `std::terminate`), and **5 r72 seams**: `mc195_custom_items_add_provider`, `lce195_catalog_add`, `lce195_items_window_held`, `mc195_lucky_server_current`, `mc195_lucky_server_borrow`. No weak reference stays open: the netsync weak hook is resolved inside the object |
| `integration/shipping-plugin.patch` | 9 files, see §3 |
| `integration/verify-patch.sh` | the host verification above |
| `integration/sdk_boot.cpp` | the SDK-only startup owner and every phase entry point (§2) |
| `server/server-sync.patch` | PC server side of the ID handshake (§6) |

Symbol hygiene: the SDK's SHA-256 is linked as `lce_sdk_sha256*`, because `ps4-transport/client_report.c` already exports `lce_sha256*` with a different context layout.

## 2. Wiring: phases, call sites, threads

| # | Phase | Call site (patch) | Thread | SDK entry → module | Log line |
|---|---|---|---|---|---|
| 1 | Boot / configure | `super.cpp`, after attestation, before `steel195_tools_runtime_prepare` | startup | `lce_sdk_mods_present()`, `lce_sdk_boot(base, logline)`: events195 host init, composite init with every module's natives, loader discover (scripts run here, §5 of MODDING), validate, IDs + ledger, arm item/recipe providers, services, netsync snapshot | `LCE_SDK mod=… state=… reason=…`, `LCE_SDK caps=… events=… scripts=…`, `LCE_SDK boot=… status=…` |
| 2 | Block registry data hook | `super.cpp`, right after boot; retried once before `steel195_trace_start` if the registry global was still null | startup | `lce_sdk_blocks_hook()` → `b195_hook_install`: registry object vptr → private copy, `+0x28` = relay. **No `.text`** | `LCE_SDK blocks_hook=1 waiting=0` |
| 3 | Block phase | game: Block init `0x957a0`, last stock registration id 365 at `0xb74d8` | game init | relay → `b195` publish, then the stock vptr is restored | (reported in phase 5) |
| 4a | Status effects | inside the items window, before the first item is built: `prov_collect` → `pre_collect` (or `lce_sdk_after_items` when no SDK item exists) | game init | effects195: mod MobEffects at ledger ids 32..127 (registry `0x14195c8` +0x28, count `0x14195d0` raised for the HUD), so food items can take a mod eat effect | `LCE_SDK phase=effects rc=1 N status effect(s) published, ids 32..` |
| 4 | Items window | existing: `0x477cc8` → `registry_trace items()` → `steel195_tools_runtime_after_items` → provider `collect()` | game init | `prov_collect` builds **every** item kind with `lce195x_item_construct`, routes generic items' `+0x40` to events195, adds catalog entries; `lce195_native_items_publish` registers them | existing `STEEL_TOOLS … registration=2 count=N` |
| 5 | After items | `registry_trace.cpp items()`, after the tools runtime step, before the one-shot restore | game init | `lce_sdk_after_items()` → blocks result, `b195_items_publish` (BlockItems, block→item map, names, fire odds), block break/place routes | `LCE_SDK phase=blocks rc=1 …`, `LCE_SDK phase=after_items rc=1 …` |
| 6 | Crafting pre-cache | existing `0x5a9ef0` relay → `content_process.cpp steel195_content_on_crafting_scoped` | game init | **one-line r72 change**: `lce195x_recipe_providers_publish(owner)` → ABI-2 publisher (shaped, shapeless, counts, aux). Then the call word is restored (SDK-only mode) | (phase 7 summary `recipes=1`) |
| 6b | Mods crafting tab | `craft_ui_capture.cpp` | UI | **one-line r72 change**: `lce195x_recipe_snapshot_all` (r72 + ABI-2 list, never partial); `super.cpp` binds the r72 half with `lce195x_bind_r72_snapshot(lce195_recipe_snapshot)` at startup | — |
| 7 | Late startup (title) | `super.cpp ui_tick` → `lce_sdk_ui_tick(controller)` | native UI thread | once, only while no MinecraftServer exists (`[0x14648d0] == 0`) and no server tick was seen, in this order: **mod string ids** into every Localizer language map (strings195), **potion types** (effects195, registry `0x142e938`), **enchantments** (enchants195, registry `[0x1415448]` +0x28), **biome objects** (biomes195, `PlainsBiome 0x53d140`), furnace `[0x1416a80]`, brewing mixes `0x142e948` (stock and mod potions), entity types (after `0x28dc10` long returned), spawns, renderers (`*([0x1464780]+0x220)`), worldgen prepare + publish (Biome vptrs, mod biomes adopted), **biome colours + selection** (BiomeInitLayer `getArea` slot, RELRO), **custom audio** (SoundEvents `0x6672c0` + SoundEngine private vtable). Afterwards, every tick: `lce195c_ui_tick_strings` (re-inserts the mod string ids after a language change) | `LCE_SDK phase=strings rc=…`, `phase=potions`, `phase=enchants`, `phase=smelt_brew rc=…`, `phase=entity_types`, `phase=spawns`, `phase=renderers`, `phase=worldgen`, `phase=biomes rc=… built=N coloured=C select=…`, `phase=audio`, `LCE_SDK late_startup done world_entry=1`, `LCE_SDK phases block_hook=… world_entry=…`; on a language change `LCE_SDK strings rc=1 … inserted=N` |
| 8 | World-entry gate | `lce_sdk_world_entry_allowed()` | any | 0 if entity types were registered but renderers were not (a saved custom mob would crash `getRenderer 0xbd54c0`) | `LCE_SDK WORLD_ENTRY BLOCKED: …` |
| 9 | Server tick | `super.cpp`: `Mc195SharedScope` list gets `lce_sdk_scope_tick/finish/failed`; the scope now also starts when only SDK events need it | server | events195: WORLD_LOAD, PLAYER_JOIN/LEAVE, SERVER_TICK, scheduler, command queue → loader subscribers + scripts | `events195: …` on faults |
| 10 | Scripts | loader REGISTER (runner) and the event sink | startup / server | one Lua VM per mod with `scripts/main.lua` | `LCE_SDK script: …` |
| 11 | Multiplayer | `native_bridge.c` receive_frame (weak `lce_netsync_inbound_frame`), `session_runtime.cpp run_ui_task` (weak `lce_netsync_pump`); configure in `lce_sdk_boot` | session worker / UI | netsync195 | `[netsync] …` |

**Ordering rules enforced by the code** (and by `tests/test_composite195.cpp`): the block hook must be armed before Block init; BlockItems only after the item window (a block's drop is an SDK item); smelting a block-item input only after phase 5 (else refused, nothing written); recipes are refused if the outputs are not registered yet; the late phase waits while a world exists; renderers missing ⇒ world entry blocked; without a renderer dispatcher no entity type is registered at all.

**World-entry gate on the console.** There is no world-creation hook in the plugin today, so the gate is enforced by refusing to register entity types without a dispatcher (clean), and by logging `WORLD_ENTRY BLOCKED` if renderers fail after the types (which the host tests never produced). The tester must not open a world after that line (§7 rule 6).

## 3. The patch (9 files, every bundled mod stays OFF)

| File | Change |
|---|---|
| `super.cpp` | W1 SDK-only owner (unchanged from the previous patch: SDK boot before `steel195_tools_runtime_prepare`, `steel195_tools_runtime_set_sdk_only(1)` when Steel and Lucky are off, resource/Creative prep, `gameplay_enabled` includes SDK-only, Steel crafting registration skipped). New: phase-2 block hook (+ one retry), `lce195x_bind_r72_snapshot`, the events subscriber in the shared scope (bounded by `MC195_SHARED_SCOPE_SUBSCRIBERS`), scope start when only SDK events need it, `lce_sdk_ui_tick` in `ui_tick` |
| `tools_runtime.cpp` | W1 (unchanged): SDK-only mode without the Steel pair/owner |
| `registry_trace.cpp` | W1 one-shot restores of `0x477cc8`/`0x477ce8` (unchanged) + `lce_sdk_after_items()` |
| `content_process.cpp` | `lce195x_recipe_providers_publish(owner)` after the r72 publish; then `steel195_craft_hook_restore()` |
| `craft_hook.cpp` (`steel_process_content_195`) | remembers the installed call word; `steel195_craft_hook_restore()` restores `0x5a9ef0` with `lce195_oneshot_restore` **in SDK-only mode only** (Steel/Lucky behaviour unchanged) |
| `craft_ui_capture.cpp` (`additive_recipes_195`) | `lce195x_recipe_snapshot_all` instead of `lce195_recipe_snapshot` |
| `native_bridge.c`, `session_runtime.cpp` (`ps4-transport`) | netsync195 client hunks (weak hooks; identical to `modules/netsync195/patched/`) |
| `build-crashlog.sh` | rebuilds the 6 patched r72 objects into `crashlog-build/build/` with the build's own flags (each original kept once as `*.pre-sdk.keep`), re-links the craft provider object, runs `modloader-sdk/build-ps4.sh`, adds `lcemodloader.o` to the `ld.lld` line |

`modules/netsync195/client-integration.patch` also had a hunk for `sdk_boot.cpp`; it is now merged into `integration/sdk_boot.cpp`, so only its two `ps4-transport` hunks are used (they are inside this patch).

**Not patched (on purpose):** `remote_actions.cpp` (selection wrapper; bundled mods stay off), `debug_pad_sessions_195/native_pad.cpp` (keybinds have no console producer yet, see API_STATUS §0), `shared_terrain_195` (N terrain owners: gap G1), the Creative hook `0xa33027` restore (not proven one-shot).

## 4. Anticheat: `.text` and `game_text_sha256`

`ps4-transport/client_report.c` hashes `eboot.bin` segment 0 when a report is built (join/anticheat time). The server accepts only hashes listed per plugin build in `official-builds.json` (`r8-anticheat-20261117220643` accepts `b4e71242…ae5b`).

**The SDK writes no `.text`.** Its writes are its own objects, private vtable copies, object/registry/Biome **vptrs** (aligned CAS data writes), registry slots, maps and vectors — none in segment 0. The `.text` words that differ from stock in an SDK-only session come from the existing shipping call-patch hooks that the SDK-only owner turns on:

| Site | Installed by | Fires | After startup |
|---|---|---|---|
| items init word `0x477cc8` | `steel195_trace_start` | once | **restored** (`lce195_oneshot_restore`, registry_trace) |
| recipes wrapper word `0x477ce8` | `steel195_trace_start` | once | **restored** |
| crafting pre-cache `0x5a9ef0` | `steel195_craft_hook_start` | once | **restored** in SDK-only mode (new) |
| atlas seed `0xd3d6d8`, resource growth/bind `0xd3dbd8`, `0xd3dd90` | trace / `steel195_resource_hooks_start` | every texture reload | **stays** |
| Creative group `new[]` `0xa33027` | `steel195_creative_hook_start` | each Creative build | **stays** (not proven one-shot) |
| retirement `0xd54150`, Mods crafting tab filter (`0x9ae7c0` path), Creative movie/navigation (`0xa08c65` region) | `steel195_retirement_hook_start`, `mc195_craft_candidate_start`, `mods195_private_movie_start`, `mods195_nav_start` | persistent | **stay** (same as a Steel session) |
| shared-scope call sites (Connection tick `0xc859e8`, per-player `0xce4380`, paused `0xc84750`) | `mc195_shared_scope_start` | persistent | already installed whenever the debug authority (F3) is ready; **new** only if the scope starts for SDK events alone |
| block init window `0xb74d8` | — | — | **not used** (blocks use the registry data hook) |
| terrain atlas destroy/deleting-destroy `0xd568b0`/`0xd568c0`, texture reload full/simple `0xd64490`/`0xd648b0` | `mc195_terrain_install` (shared_terrain_195) | every terrain reload | **stays**; SDK-only sessions install it only when an SDK block has a terrain texture (`lce_sdk_terrain_wanted`), the same four words as any Lucky/Steel session |

**API 1.1 registries (effects, potions, enchantments, biomes, audio) add no `.text` write either.**
- They write heap objects, game-heap vtable copies, registry entries, the effect count global `0x14195d0` and the SoundEngine vptr.
- They also make **one 8-byte write in a read-only data page**: the `BiomeInitLayer` vtable slot `0x12163d8` (RELRO, data segment). `lce_ps4_mem_patch_ro8` refuses any address in segment 0. It does `sceKernelMprotect(page, RW)` → CAS → restore → re-query.
- **API 1.9 throwables (projectiles195)** make one more 8-byte RELRO write the same way: the Snowball primary vtable slot `0x12ca530+0x860`
  (shoot, stock `0x6e15a0` → `p195_shoot_hook`), installed in the after-items phase only when a plan has THROWABLE items and events are bound
  (`LCE_SDK phase=projectiles rc=1`). Everything else is data in the plugin's own memory: SnowballItem private vtable copies built by items195
  (`+0x40` = `p195_item_use`, then the events195 ITEM_USE route) and per-kind Snowball vtable copies (`+0x880` onHit wrapper).
- `game_text_sha256` therefore changes only through the shipping hooks listed above.
- `sceKernelMprotect` is new in `lcemodloader.o`'s undefined list. The shipping plugin already imports it: `verify-patch.sh` reports no new dynamic import.

**Text-hash states of an SDK session** (every state has to be measured and registered per build):
1. No SDK mod installed: the shipping hash (no SDK hooks).
2. SDK mods without blocks or entities: the shipping hooks the SDK-only owner turns on (table above). The one-shot item/recipe/crafting words are restored after they fire.
3. Any other SDK content (blocks without terrain textures, effects, potions, enchantments, biomes, audio): **the same as 2**. These add no `.text` write.
4. SDK blocks **with** terrain textures in an SDK-only session: 2 plus the four terrain-owner words above (gap G1). With Lucky or Steel enabled these words are already present, so the hash equals that session's.

**Custom fuel is opt-in on PS4.** Build with `LCE_EXPERIMENTAL_FUEL=1` to bind fuel support on retail 1.95. The late startup phase resolves both `fuel_ticks` and `register_fuel` after the item registry is published and attaches to the new and saved furnace factories, fuel slot, Quick Move, and hopper placement. If attachment fails, follow the world-entry stop procedure in §7. Test the pack with `lce_modpack.py check --profile retail195-experimental-fuel tests/fixtures/fueltest`. The default build and profile refuse these declarations. Custom fuel can be placed directly in the furnace. Quick Move routes registered fuel to the fuel slot and gives matching smelting recipes priority. Hoppers retain the stock slot and face checks. Direct placement, Quick Move, and side-hopper insertion passed on retail 1.95 hardware using a stock-item fuel package.

**Consequences.**
1. A session **with** SDK mods reports a `game_text_sha256` different from `b4e71242…`. It is deterministic for this build (fixed base, deterministic relay hints, fixed patch bytes): measure it once in T12 and register it with `lce-ps4-superplugin/scripts/publish_plugin.sh <prx> <build-id> <text sha>` if the build is meant for others. Publishing is your decision.
2. A session **without** SDK mods (no `/data/lce-mods/<id>/mod.lcm`) installs none of these hooks and reports the unchanged hash; both hashes must be listed for the build.
3. The three restores remove three words from the difference; the remaining words are the persistent hooks above. Removing the atlas words (re-bind SDK sprites by CAS of `Item+0x20` from the title tick instead of re-seeding) is a follow-up.

## 5. What the patched startup does in SDK-only mode

With `steel` and `native-lucky-blocks` off and at least one SDK mod armed: SDK boot → block data hook → `steel195_tools_runtime_prepare(base,0)` (SDK provider only) → retirement hook → resource hooks (item atlas growth) → Creative prepare → shared scope (F3 and/or SDK events) → crafting tab base, Creative hook, crafting hook, registry observer, trace hooks (`0x477cc8`, `0x477ce8`, atlas seed) → Store/UI as before. Every failure keeps the existing "stay resident, report" pattern; an uncertain SDK step aborts with `ABORT LCE SDK …; resident`.

## 6. Multiplayer (PC server)

Custom IDs (items 2283+, blocks from the allowlist, entity ids 150+) are unknown to an unpatched `neo-ps4-mob-candidate`; it drops or refuses those stacks.

1. **Server:** apply `server/server-sync.patch` (`cd neo-ps4-mob-candidate && patch -p1 --dry-run < ../modloader-sdk/server/server-sync.patch`, then for real), build, restart. Follow `server/STATUS.md` "Test plan" steps 1–9. **First** restore `neo-ps4-mob-candidate/Minecraft.Server/Windows64/ServerMain.cpp` as decided by the user (server/STATUS.md risk 5); the SDK does not touch it.
2. **Client:** the netsync hunks are already in `shipping-plugin.patch` and netsync195 is inside `lcemodloader.o`.
3. Handshake: the server sends `LCE|ModIds` REQUEST; the plugin answers with the ledger (HELLO), the server adopts or refuses; `[netsync] … accepted … digest ok` in the plugin log.
4. Until both halves ran on hardware, treat SDK content as **offline only**. Entity masks are not part of the handshake: do not take custom mobs onto the server.

## 7. On-console test plan (main engineer)

**Mods must be enabled.** The loader runs with `LCE_LOADER_REQUIRE_ENABLED`, and the plugin's Mods list owns that choice. A hand-copied mod folder stays off until it has an `enabled` marker: create an empty file `/data/lce-mods/<id>/enabled` next to `mod.lcm`, or enable the mod in the Mods menu. Without the marker the log shows `lce-mods: <id> disabled (no enabled marker)`. `disabled` beats `enabled`, and mods installed from the menu (`lce.version`) count as on.

**Rules (apply to every step).**
1. The game is fully closed before every install or file change.
2. At every launch choose **Update Later**. Never accept a game update.
3. Do not restart unrelated services; touch only the plugin PRX, its ini and `/data/lce-mods`.
4. Use a **fresh test world** for every gameplay test; never a real save.
5. Stop at the first crash. Fetch `/data/mc-195-mods-crash.log` (`SITE CHMOD 644` first) and `/data/mc-195-combined-test.log` (all `LCE_SDK` lines go there).
6. If the log contains `LCE_SDK WORLD_ENTRY BLOCKED` or any `ABORT LCE SDK`, do not open a world; collect logs and stop.
7. Report every step as pass / fail / not run, with the log excerpt. A registration line is not proof of gameplay; the gameplay steps are.

### 7.0 Preflight, backup

1. `runtime-tests/legacy195-preflight/preflight.py`: record active plugins and AFR overrides.
2. Back up the plugins ini, the current PRX, and `/data/lce-mods/` (with `packs/` and `reservations/`).
3. Record the shipping PRX SHA-256 and the current `game_text_sha256` from a normal session (baseline).

### 7.1 Build (host)

```sh
cd /home/codey/minecraft-lce-work/modloader-sdk
make test                                  # all host suites, including every module and the composite end-to-end test
./build-ps4.sh                             # build/ps4/lcemodloader.o, undefined.txt (77 symbols, §1)
integration/verify-patch.sh /tmp/lce-verify   # base hashes, dry-run, compile, full link
cd /home/codey/minecraft-lce-work && sha256sum -c modloader-sdk/integration/patch-base.sha256
patch -p0 --dry-run < modloader-sdk/integration/shipping-plugin.patch && patch -p0 < modloader-sdk/integration/shipping-plugin.patch
ps4-metadata-snapshot/crashlog-build/build-crashlog.sh   # record the printed PRX SHA-256
```

**Already applied the previous version?** Apply only `integration/upgrade-w1b.patch` (from `minecraft-lce-work`: `patch -p0 --dry-run < modloader-sdk/integration/upgrade-w1b.patch`, then for real). It is the exact difference between your applied tree and the current full patch. `verify-patch.sh` step 6 checks that it gives the same result as pristine + full patch.

W1b (console run 1: `STEEL_TOOLS registry=4 count=0`, `recipes=3`). In SDK-only mode the Steel owner is never prepared, but `tools_runtime.cpp` `reserved()` still required `steel195_owner_status()==3`. The native item publisher therefore refused every SDK entry before writing anything (registration 4), and the recipes were then refused because their outputs did not exist. The same gate in `resource.cpp` (`item_identity` / `resource_identity` / `begin_resource`) would next have blocked the item-atlas icons. Both are fixed for SDK-only mode. The composite now checks `Item::items` after the shipping publisher runs and logs `LCE_SDK phase=items rc=0 N of M SDK item(s) built but NOT in Item::items`, instead of the misleading `items=1`.

If any base hash differs, re-base the patch first (files are edited concurrently): `integration/rebase-patch.py` handles both a pristine and an already-patched shipping tree. It reverses an applied hunk, or the previous version's hunk from `shipping-plugin.prev.patch`, regenerates the build-script hunk from the live `build-crashlog.sh` and `integration/build-crashlog.sdk-block`, and rewrites `patch-base.sha256` with **pristine** hashes. `--check` reports only; `--upgrade OUT` writes the live → new diff. Then run `integration/verify-patch.sh` again.

### 7.2 Packages

```sh
cd /home/codey/minecraft-lce-work/modloader-sdk
python3 tools/lce_modpack.py check tests/fixtures/rubyretail      # "everything in this mod is publishable"
python3 tools/lce_modpack.py check tests/fixtures/retailall       # same
python3 tools/lce_modpack.py build tests/fixtures/rubyretail out && python3 tools/lce_modpack.py build tests/fixtures/retailall out
```

`rubyretail` uses only console-ready rows (generic items, shaped recipes). `retailall` has one of everything the backend publishes: 5 package items (generic, pickaxe with an SDK repair item, sword, helmet, food with regeneration 50 %) + 1 script item, 1 block (gem ore, drops 1–3 gems), shaped / shapeless / count-4-from-`wool@14` recipes, smelting (ore block-item → 2 gems; gem → emerald), brewing (awkward + gem → regeneration), a zombie clone (plains, night), an ore feature (y 5–40) and a structure feature, a sound and a particle clone, `scripts/main.lua`, `en_US` + `de_DE`.

### 7.3 T1 — boot and arm (rubyretail only)

Install `out/rubyretail/` as `/data/lce-mods/rubyretail/`. Launch to the title.

Expect, in order:
- `lce-mods: discovered 1 mod(s), 1 staged`
- `LCE_SDK mod=rubyretail v=1.0.0 state=3 status=0 items=3 blocks=0 recipes=2 smelts=0 reason=-`
- `LCE_SDK caps=… events=1 scripts=0`
- `LCE_SDK boot=1 status=…`
- `LCE_SDK blocks_hook=1 waiting=0`
- the existing `STEEL_TOOLS … registration=2 … count=3`
- at the title: `LCE_SDK phases block_hook=2 blocks=2 items=1 after_items=2 recipes=1 smelt_brew=2 entity_types=0 spawns=0 worldgen=2 renderers=0 world_entry=1` (2 = nothing to do).
- `/data/lce-mods/reservations/sdk-ids-v1.txt` starts with `LCEIDS 1`; items 2283–2285.

Fail: `boot=-1`, any `ABORT`, no `STEEL_TOOLS` line, `recipes=3` or `4` (refused/uncertain).

### 7.4 T2 — Creative, icons, names

Creative → Mods tab: "Ruby", "Ruby Nugget", "Ruby Wand" with their icons (not the missing-texture icon). Hover each name. Screenshot.

### 7.5 T3 — crafting (first confirmed custom craft on 1.95)

Crafting table → Mods tab, Materials: "Ruby" from 9 nuggets; Tools: the wand. In survival (offline), craft both: 9 nuggets → 1 Ruby; 1 ruby + 1 stick → 1 wand. Record exactly what was consumed and produced.

### 7.6 T4 — persistence and stable IDs

Put the items in a chest and the hotbar; save and quit to title; re-enter; fully close and relaunch; re-enter. Same items, same names, ledger unchanged (SHA-256). Then add a second mod (`lce_modpack.py new t5mod --dir /tmp`, build, install): rubyretail keeps 2283–2285, t5mod gets 2286+, the ledger is rewritten with a `.bak`.

### 7.7 T5 — every item kind (retailall)

Install `out/retailall/` (keep rubyretail). Expect `LCE_SDK mod=retailall … state=3 … items=6 blocks=1 recipes=3 smelts=2`, `scripts=1`, and `STEEL_TOOLS … count=9`.
In a fresh survival world (take items from Creative first):
1. Gem Pickaxe: mines stone faster than iron? (iron tier, 900 uses). Durability bar decreases.
2. Gem Sword: damages a mob; durability decreases.
3. Gem Helmet: equips, armour bar +3, renders with the **iron** layer (expected).
4. Gem Stew: eat when hungry; hunger refills 6; regeneration about half the time; stacks to 16.
5. Anvil: repair the pickaxe with a Gem.
6. The "Script Gem" (registered by `main.lua`) is in the Mods tab.

### 7.8 T6 — recipes ABI 2, smelting, brewing

1. Shapeless: gem + bowl → Gem Stew (any slot order).
2. Count/aux: one **red** wool (`wool@14`) → 4 gems; white wool must **not** match.
3. Smelting: a Gem in a furnace → 1 emerald, XP 1.0. (The ore block-item recipe is published too but the block-item cannot be obtained in SDK-only mode, see T7.)
4. Brewing: awkward potion + gem → potion of regeneration.
Expect `LCE_SDK phase=smelt_brew rc=1 smelting 2 brewing 1 published` at the title.

### 7.9 T7 — blocks and ore generation

Expect `LCE_SDK phase=blocks rc=1 …` and `LCE_SDK phase=after_items rc=1 … block event routes 1, block-item routes 1`, and `LCE_SDK phase=worldgen rc=1 2 feature(s) published …`. Terrain (gap G1): `LCE_SDK terrain install=1` during startup, and the late status line ends with `terrain_tiles=1 binds=1 rc=1 gen=1` (binds grows by one per texture reload; `rc=0` means the faces kept the fallback, `rc=-1` an uncertain bind, which poisons the terrain owner).
1. Create a new world; dig around y 5–40: Gem Ore veins exist and show the **ore texture from the package** (not the stock fallback sprite). Change the texture pack once and back: the faces stay correct (detach + rebind per generation).
2. Mine with the Gem Pickaxe: drops 1–3 Gems. Hover the block-item: "Gem Ore".
3. The Gem Ore block-item is not in Creative in SDK-only mode and the block drops gems, so placing (BLOCK_PLACE) cannot be tested yet (gap G1); record "not obtainable".
4. Save/reload: the generated ores remain and still drop gems.

### 7.10 T8 — structures

Fly over new terrain (rarity 1/40 chunks, y 60–90 surface): a 3×1 stone row with a Gem Ore on top. Screenshot. Not finding one in ~200 chunks is "inconclusive", not fail.

### 7.11 T9 — entities

Expect at the title: `LCE_SDK phase=entity_types rc=1 registered via 0x28d9c0 after 0x28dc10`, `phase=spawns rc=1`, `phase=renderers rc=1 …`, `world_entry=1`. If instead `phase=entity_types rc=0 renderer dispatcher … not available`, record it (the Minecraft pointer `[0x1464780]+0x220` was not readable) and skip T9.
1. New world, plains, night: a zombie-looking "Gem Zombie" (30 health, faster, wanders, attacks).
2. Kill one: vanilla zombie drops (custom drops are refused by design).
3. Save with one alive nearby, reload: it is still a Gem Zombie (saved as `retailall:gem_zombie`).

### 7.12 T10 — events and scripts

On world entry the log shows `LCE_SDK script: joined: <your name>`. No `events195: scope subscriber failed` line during 5 minutes of play. Item use: right-click with a Gem (generic item) works normally (the event is delivered; no gameplay change).

### 7.13 T11 — fail-closed paths (title only, no world)

| Step | Expect |
|---|---|
| corrupt one byte of `sdk-ids-v1.txt` | `load FAILED (-11)`; no SDK items; restore from `.bak` |
| install `tests/fixtures/allcontent` built with `lce_modpack.py build` | `rejected allcontent: script main.lua: main.lua:2: subscribe: unknown event 'server_tick'` and nothing of it registered; other mods unaffected |
| put a junk `native.prx` next to `rubyretail/mod.lcm` | `native.prx present but native mods are disabled` |
| remove every SDK mod | no `LCE_SDK` lines after `lce_sdk_mods_present`; the plugin behaves as before; `game_text_sha256` equals the 7.0 baseline |

### 7.14 T12 — anticheat hash

Note the client report's `game_text_sha256` in an SDK session (expected: differs from `b4e71242…`, deterministic across relaunches — check twice). Do not join the PC server with SDK items unless T13 is in progress. Register the hash only if the build is meant for others.

### 7.15 T13 — multiplayer (optional, needs the server patch, §6)

Follow `server/STATUS.md` steps 6–9 with retailall installed: `[netsync] server requested mod IDs: sent N entries` then `accepted … digest ok`; server log `[modids] … adopt item retailall:gem (…)`; items survive a server restart in a chest; a mismatched ledger is refused with code 3.

### 7.15b T15 — new status effects and potions (newretail)

Build and install `tests/fixtures/newretail` (`lce_modpack.py build tests/fixtures/newretail out`, then copy `out/newretail/` to `/data/lce-mods/newretail/`).

Expected log lines:
- At boot: `LCE_SDK mod=newretail … state=3`, and in the ledger `effect newretail:glow 32`, `effect newretail:swift 33`, `effect newretail:calm 34`, `enchant newretail:gem_edge 100`, `enchant newretail:soulbound 101`, `biome newretail:crystal_fields 168`.
- In the item window (before `STEEL_TOOLS`): `LCE_SDK phase=effects rc=1 3 status effect(s) published, ids 32..34`.
- At the title: `phase=strings rc=1 13 string id(s) 0x4c430000.. languages=N inserted=13*N`, `phase=potions rc=1 2 potion type(s) published`, `phase=smelt_brew rc=1 … brewing 2 published`.

Test in a fresh survival world:
1. Eat "Glow Berries" (Creative → Mods): the inventory shows **Glow** with the regeneration icon, and health regenerates (regeneration clone).
2. Brewing stand: awkward potion + Glow Gem → **Potion of Glow**. Then + glowstone dust → **Potion of Strong Glow** (Glow II + Speed).
3. Gunpowder → **Splash of Glow** (lang override). Dragon's breath → **Lingering Potion of Glow**. Tipped arrows: **Arrow of Glow** (crafting table).
4. Drink, splash and shoot each one: the effect applies, and the timer and amplifier match.
5. Script: the log shows `LCE_SDK script: effect tick newretail:glow amp 0 id 32` while Glow is active.
6. Language → Deutsch: the effect is **Leuchten** and the potion **Trank des Leuchtens**. The log shows one `LCE_SDK strings rc=1 … inserted=` line after the switch.
7. Save and quit with Glow active, then reload: the effect and its remaining time are kept, and the potions are still Glow potions.

Fail if: `phase=effects rc=0/-1`, any name shows as an empty or garbage string, or a crash on the HUD (report the log; the HUD array sizing is a risk).

### 7.15c T16 — enchantments

1. Enchanting table at level 30, re-roll a sword ≥20 times: **Gem Edge** appears (rare, weight 2).
2. Gem Edge V adds the same damage as Sharpness V. Gem Edge + Sharpness on one sword is refused at the anvil.
3. **Soulbound** never appears on the table (treasure), but a book can be combined at the anvil if obtained.
4. Save and relaunch: the enchantment is kept. Record whether the books appear in Creative.

### 7.15d T17 — biome

Expect `LCE_SDK phase=biomes rc=1 built=1 coloured=1 select=1 (BiomeInitLayer getArea slot, RELRO data)`. If it shows `select=0` or `-1`, the RELRO write was refused or uncertain; report it and do not continue.

1. Create a **new** world (default type). Within about 2000 blocks of spawn, in temperate regions, there are sand/sandstone "Crystal Fields" with mint grass (#40ffd0).
2. A second new world with the same seed has the biome at the same coordinates.
3. Save, quit and reload: the biome and its colours are kept, and new chunks continue to generate it.
4. With the mod removed (test world only): the world loads, and the old area shows plains colours (placeholder biome).

### 7.15e T18 — custom audio

Expect `LCE_SDK phase=audio rc=1 1 custom sound(s) registered (ids from 523) + SoundEngine vtable copy`.

1. In a world, right-click with a **Glow Gem**: the newretail script plays `newretail:chime` at your position through `lce.play_sound`. A short 880 Hz chime plays.
2. Walk 10 blocks away: it is quieter. Beyond 16 blocks it is silent.
3. Turn so the source is on your left or right: the stereo position follows. If it is reversed, report it (one sign in `a195_spatial`).
4. Sound volume 0: silent.
5. Vanilla sounds are unchanged throughout.

### 7.15f T19 — experimental fuel

With the experimental build and the `fueltest` fixture enabled, expect `phase=fuel rc=1` at the title. In a disposable creative world, manually place one Test Fuel and a stock smeltable input in a newly placed furnace. Check ignition, fuel consumption, and the output count. Repeat with Test Fuel Ember in a furnace reloaded from a save. Also check behavior with an empty input and with a full output.

PS4 runs verified direct placement, ignition, and save/reload. Stock coal and inline 1,600-tick fuel each produced 8 glass. Explicit 800-tick fuel produced 4 glass. The `luafuel` fixture added an explicit 1,234-tick fuel that produced 6 glass and an inline 2,345-tick fuel that produced 11. The follow-up PS4 test registered ordinary Paper for 600 ticks with a fuel-only package. Direct placement, Quick Move from main inventory, and side-hopper insertion each produced three glass with one sand left. Full-output behavior still needs a console run.

### 7.16 T14 — services (informational)

World-mutating services (`give_item`, `set_block`, `spawn_entity`, `drop_item`) are not bound to the r72 world helpers yet and must return an error, never act. Chat, sound and effect services are static-analysis only; no data/script mod in this plan calls them except `lce.log`.

### 7.17 Rollback

1. Game closed. Restore the plugins ini and the PRX from 7.0.
2. Shipping tree: `patch -p0 -R < modloader-sdk/integration/shipping-plugin.patch`, then copy every `crashlog-build/build/*.o.pre-sdk.keep` (and `craft_provider/*.o.pre-sdk.keep`) back over its `.o` before the next `build-crashlog.sh`.
3. Remove `/data/lce-mods/rubyretail`, `retailall`, `t5mod`.
4. **Keep** `sdk-ids-v1.txt`. Deleting it is only safe if no world ever contained SDK content.

## 8. Open items / blocked

- **Console coverage:** the experimental fuel path has the limited PS4 evidence in §7.15f; the remaining scenarios still require their own validation.
- **Terrain textures for SDK blocks** need `shared_terrain_195` to accept N owners (`b195_terrain_*` is ready); Creative entries for block-items in SDK-only mode.
- **World mutation services**: bind `lce195_world_ops` to `lucky_block_world_events_195` (`mc195_world_place_block`, `mc195_world_spawn_entity_ex`) and `debug_give_195`.
- **Commands/keybinds** need a console producer (chat-intake `+0x300` hook, pad filter call in `native_pad.cpp`) and a package/script descriptor.
- **World-entry gate** is advisory (no world-creation hook).
- Unverified assumptions called out by the modules: plugin load vs eboot DT_INIT order (block registry global), the D/U/N/S/W/E face order, `[0x14648d0] == 0` at the title, the `+0x40` slot of tool classes.
