# effects195: new status effects and new potion types for 1.95 (CUSA00265)

Image: `legacy-195-analysis.elf`, SHA-256 `c675e6ce…029f`. All addresses are RVAs. No 2.72 addresses.

The module never patches `.text`. Effects are new heap objects built by the retail MobEffect constructor, given a **private vtable copy** (data), and registered through the registry's own virtual. Potions are new heap objects registered the same way. The only global writes are the two id counters (`0x14195d0`, `0x142e940`), both data.

Return values of both publishers: `1` done and post-verified, `0` refused before any game write or retail allocation (the potion publisher creates and destroys temporary ResourceLocations to probe its keys), `-1` partial/uncertain (keep resident, never retry; nothing is visible to the lookups/thunks).

## Evidence levels

| Level | Meaning |
|---|---|
| **unicorn** | The module's **own C publishers** (built into `libeffects195_uc.so` with `tests/uc_shim.cpp`) ran against the real retail code in Unicorn: every native they call executes the pinned image, after the real startup sequence (registry ctor, effect initializer `0x4781e0`, statics `0x47b780`, Potion registry construction `0x588f5f..0x589032`, potion initializer `0x55e6d0`) |
| **pinned** | Bytes / RTTI / call targets checked in `tests/test_image_pins.py` |
| **static** | Disassembly only |
| **host** | `tests/test_effects195.cpp` with fake natives |

Nothing in this module has run on a PS4.

## Why the old refusals no longer hold

| Old blocker (items195 STATUS) | Answer | Evidence |
|---|---|---|
| "the name needs a string-table id" | Effects read the name sid at `+0x58` (`getDescriptionId 0x491590`), potions read four sids at `+0x00/+0x04/+0x08/+0x0c` by item kind (`Potion::getName 0x55e620`: potion 0, splash 1 `0x67140d`, lingering 2 `0x4196bd`, tipped arrow 3 `0x6eba5d`) and tooltips at `+0x10/+0x14`. The spec takes these sids from the caller; strings195 makes them resolve in the live Localizer map | unicorn (real `0x55e620` read each mod sid), pinned |
| "NBT and packets carry a byte id" | Save `putByte("Id", movzbl getId)` `0x491ccc`, load `movzbl getByte → byId` `0x491ee5`, RemoveMobEffect read `0x19e3a2`, UpdateMobEffect handler `0xba15f3` all zero-extend: ids up to 255 round-trip. The module allows **32..127** so a Java server's signed byte also stays positive | unicorn (real save→load restored ids 32 and 64 with duration/amplifier), pinned |
| "icons are stock frames 1..29" | Still true and kept: `+0x5c` is a frame of the `fourj.MobEffect` UI movie (`0x4915b0`, HUD `0xa657a7` → `0x81e940`). A mod effect reuses any stock frame (`icon_texture = "minecraft:<effect>"`) or none (0). A new frame needs an SWF edit; refused with that reason | static, unicorn (frame read through `0x4915b0`) |
| "clones of regen/poison/wither/hunger/instant do nothing (`0x490ed0` compares `this`)" | Slots 0/1/2 of the private vtable are thunks that call the **clone class's** functions with `this` replaced by the stock effect, so the identity tests succeed | unicorn: real `MobEffectInstance::tick 0x491bc0` on a regeneration clone → slot 2 → real isDurationEffectTick (`50 >> amp`) → slot 0 → real `applyEffectTick 0x490ed0` with `this` = stock regeneration → `heal(1.0)` on the fake entity, exactly at durations 100 and 50 |
| "new potion types: Creative lists only static pointers" | Still true for the **stock** Creative list. Mod potions are obtainable by brewing (every item kind works: splash/lingering/tipped arrow are the same Potion object under a different item) and from saved/commanded NBT. See "Creative" below for adding them to the Mods group | static |

## Coverage

| Feature | Status | RVAs / layout | Evidence |
|---|---|---|---|
| MobEffect object | works | ctor `0x490db0(this, bool harmful, eMinecraftColour)`; 0x78 (AttackDamageMobEffect 0x80, `+0x78` double): `+0x08` modifier map (list head `+0x10`, size `+0x18`), `+0x58` name sid, `+0x5c` icon frame, `+0x60` harmful, `+0x68` duration modifier (double), `+0x70` beneficial flag, `+0x74` colour enum. The module allocates 0x80 for every effect | unicorn, pinned |
| Registration at a free id | works | `MappedRegistry<RL, MobEffect*>` global `0x14195c8` (vt `0x1294038`): register `+0x28` (`0x4c3610`, grows the id vector), byId `+0x40` (`0x4c3a10`, bounds-checked, null for gaps). Count global `0x14195d0` = last id | unicorn (byId 32/33/34/35/64 = mod objects; 1..31 intact; 36..63 null) |
| HUD array sizing | handled | The HUD scene ctor `0xa00900` allocates `int[count()+1]` (`0xa00abb`, `count 0x490da0` reads `0x14195d0`) and indexes it by effect id. The publisher sets the count to the highest mod id; it must run before the first HUD scene is created | pinned; the phase rule below |
| Private vtable | works | 15 slots + RTTI prefix copied from the clone's class (MobEffect `0x1287760`, AttackDamage `0x120b560`, Instantenous `0x126e070`, HealthBoost `0x12678e8`, Absorption), slots 0 `applyEffectTick`, 1 `applyInstantenousEffect`, 2 `isDurationEffectTick` → thunks | unicorn, pinned |
| Attribute effects (speed, slowness, strength, weakness, luck, unluck, health boost) | works | the clone's modifier list is replayed through the real `addAttributeModifier 0x4917d0` (modifier `{+0 amount, +8 op, +0x38 id}`); AttackDamage's `+0x78` multiplier copied; `getAttributeModifierValue` (slot 12) gives the same result on the clone | unicorn |
| Forwarded effects (regeneration, poison, wither, hunger, saturation, instant health/damage, absorption) | works | thunks above; instant clones keep the Instantenous class, so `isInstantenous` (slot 3) = 1 | unicorn (regeneration tick, instant class) |
| Haste / mining fatigue clones | partial | only their attack-speed modifier is copied; the dig-speed part of the game is assumed to test the stock pointer (not audited) | static |
| Identity-only effects (jump boost, nausea, resistance, fire resistance, water breathing, invisibility, blindness, night vision, glowing, levitation, conduit power, slow falling, bad omen, hero of the village) | **refused as clone_of** | game code tests `hasEffect(MobEffects::X)` by pointer, so a clone would only show an icon. Use no `clone_of` plus a tick callback/script | static |
| Effect without clone | works | MobEffect class; slot 2 answers `duration % tick_interval == 0` (if a callback is set), slot 0 calls `on_tick(user, id, LivingEntity*, amplifier)` on the server thread | unicorn (interval 20 at durations 40/39), host |
| Colour | nearest stock | `+0x74` is an `eMinecraftColour` enum looked up in the texture pack's ColourTable (`0xaf2a60` → `0x7c5840`, plain `uint32[enum]`). An RGB is mapped to the nearest of the 32 `Effect_*` enums `0xfd..0x11c` using the values in `app0/Common/res/TitleUpdate/res/colours.col` | static; host |
| Names | via strings195 | spec `name_sid` | unicorn (`0x491590` returns it) |
| Persistence | works | NBT stores the byte id (save/load above) and the registry key `modid:name` (`get` `+0x00` is `unordered_map::at`: it **throws** on unknown keys `0x4c336b`, so nothing here ever probes the effect registry by key) | unicorn |
| Potion object | works | 0x38: `+0x00` u32 sid[4] names, `+0x10` description sid, `+0x14` tipped-arrow description sid (stock `0x2453270c`), `+0x18` `vector<MobEffectInstance*>` (`+0x20` begin, `+0x28` end, `+0x30` cap), grown by `0x6fd00`; MEIs from `0x491aa0` (`+0x14 = -1`: no stat source) | unicorn |
| Potion registration | works | `DefaultedMappedRegistry` global `0x142e938` (vt `0x12aa668`), register `+0x28` (`0x57f8e0`), byId `+0x40`, getKey `+0x30` (`0x57f980`, out RL: path `+0x18`, namespace `+0x40`). Default = `minecraft:water` (key `0x142e930`): unknown keys and ids answer water. Ids follow the counter `0x142e940` (43 after `0x55e6d0`) | unicorn (ids 43/44, get by key, getKey round trip, default) |
| Potion items | works | the item stores the key in NBT `"Potion"` (`setPotion 0x562cd0` puts `getKey`, `getPotion 0x562670`/`0x565e10` reads it back through `get`), so potion, splash, lingering and tipped arrow all use the one object. A removed mod potion loads as water | unicorn (registry half), static (NBT half) |
| Potion colour | automatic | computed from the potion's MEIs through each effect's `getColor` (slot 7) and the colour table (`0x5698f6` → `0xaf2a60`) | static |
| Brewing with mod potions | ready for items195 | Mix `{from Potion*, predicate, to Potion*}`; `lce195e_potion_id()` returns the mod id, so items195's `potion_by_id` gives the mod object | host (resolver), static |
| Creative entries for mod potions | **not done here** | stock Creative builds `MenuBuilder::POTION` objects (`0xa34352`: vt `0x1328e30`, ItemInstance copy at `+0x08`, `Potion*` at `+0xb0`, built by `0xaa4870` → `setPotion`) appended with `0xaa4250`; the SDK's Mods group (r72) only adds Item rows | static |

## API (effects195.h)

- `lce195e_natives_bind(env, base)` (console): natives + `env.thunk[]` = the three C thunks.
- `lce195e_effect_check`, `lce195e_effect_spec_from_desc(desc, id, name_sid, &spec, err, cap)`.
- `lce195e_effects_publish(&pub, specs, n)` → 1/0/-1. Ids 32..127, ascending, free; afterwards `0x14195d0` = highest id.
- `lce195e_effect_object(key)`, `lce195e_effect_id(key)` (mod or stock), `lce195e_forward_target`, the thunks.
- `lce195e_potion_check`, `lce195e_potion_spec_from_desc(env, desc, sid[6], &spec, err, cap)` (sid[4] defaults to sid[0], sid[5] to `0x2453270c`).
- `lce195e_potions_publish(&pub, specs, n)` → 1/0/-1.
- `lce195e_potion_id(key)` for brewing (mod key, else stock `minecraft:` key, else -1).
- Stock tables: `lce195e_stock_effect_id`, `lce195e_stock_potion_id`, `lce195e_clone_kind`, `lce195e_stock_icon`, `lce195e_stock_colour`, `lce195e_nearest_colour`. The Unicorn suite checks every table entry against the real initializers.

## Phases (the order matters)

| Step | Phase | Why |
|---|---|---|
| effects | the held post-stock-items window (`registry_trace items()` / `lce_sdk_after_items`, before `lce195x_item_construct` builds food items that use mod effects) | the effect initializer `0x4781e0` resets the count to 1 (`0x478235`) and runs, with its statics `0x47b780`, **before** `Items::init 0x386070` (`0x477cb3 → 0x477cb8 → 0x477ccc`); no UI scene exists yet, so the HUD array is sized with the mod ids |
| potions | after the potion initializer `0x55e6d0` (`0x477ce0`): the late startup phase (title, `[0x14648d0] == 0`), **before** `lce195x_brew_publish` | ids continue from the counter; brewing needs the Potion objects |
| brewing | items195 `lce195x_brew_publish`, from/to via `lce195e_potion_id` | unchanged publisher |

## Tests

| Suite | Checks | Covers |
|---|---|---|
| `tests/test_effects195.cpp` | 127 | tables, desc→spec, refusals before any allocation (15 cases), uncertainty (register not taking, failing write), private vtable, modifier replay, AttackDamage field, thunks (forward, callback, interval, unknown `this`), potions (sids, MEIs, defaults, collisions, defaulted registry, phase checks), brew resolver |
| `tests/test_image_pins.py` | 39 | image hash, RTTI of 6 vtables, slot counts, identity compares, ctor field stores, one-byte id sites, HUD sizing, init order, potion name kinds, NBT key sites |
| `tests/test_unicorn_retail.py` | 53 | the C publishers against the real code (see evidence levels). Stubs: operator new/new[]/delete/delete[], stack-check failure, libc `wcslen/wmemcpy/wmemmove/memmove/wmemcmp/wmemchr`, `__cxa_atexit`, the AVX2 wstring hash `0x68b350` (Unicorn has no ymm; reimplemented from its scalar tail `0x68b4d8`), five placeholder Attribute objects (their initializer `0x61d300` uses ymm stores), the Localizer lookup `0xaac9b0` in the name test, and the fake LivingEntity's `getHealth/getMaxHealth/heal` |

`./test.sh` runs all three; `./ps4check.sh` builds `build/ps4/effects195.o` with the shipping flags (undefined: `memcpy memset snprintf strchr strcmp strlen strncmp`).

## Risks / unverified

- **Gameplay on the console:** nothing has run on a PS4.
- **Thunk ABI:** the thunks are plain C functions in the plugin called through the game's vtable. The same call pattern (private vtable slot → plugin function) is console-proven for item names (`+0x108`), not yet for effects.
- **Phase timing for the HUD array:** relies on no UI scene being built before the item window. The init order is pinned; scene creation order was not traced beyond `0xa00900`'s caller `0x9fd06b`.
- **Multiplayer:** the PC server does not know ids 32..127 or the keys; the netsync handshake must carry them (coordinator).
- **Ids must be stable across sessions** (saves store the byte): take them from the ledger.
- **Two effects sharing a clone** share attribute-modifier ids (`+0x38`); with both active on one entity, the later application replaces the earlier one for that attribute.
- **Haste/mining fatigue dig speed** not audited. **Potion Creative entries** not implemented (table above).

## Background reading

Structure and naming were cross-checked against public LCE modding notes over an older 4J source leak (TU19), for example `coah80.github.io/slop-docs/modding/custom-potions` and the `MinecraftConsoles` mirror (`Minecraft.World/MobEffect.*`, `Potion*`, `PotionBrewing.*`). Nothing was copied, and every fact above comes from the 1.95 image. The 1.95 layouts differ from TU19: there is a registry instead of `effects[32]`, Potion objects instead of aux bitmasks, and the NBT `"Potion"` key.
