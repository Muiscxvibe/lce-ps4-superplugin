# Porting Java / Bedrock mods to Minecraft PS4 1.95

`tools/lce_port.py` turns the **data** of a simple content mod into an SDK source package: `mod.json` plus 16×16 textures. `tools/lce_modpack.py` then compiles that into `mod.lcm`, as described in [MODDING.md](MODDING.md).

The data importer writes `PORT_REPORT.md` with conversion decisions, detected features requiring manual work, and the selected SDK backend profile result. It reports file evidence for features such as shaders, dimensions, Bedrock scripts and resource-pack visuals. For Java bytecode patterns, also see the [advanced JAR importer](docs/AUTOPORT.md).

```
 Java mod (.jar or folder)        Bedrock add-on (BP + RP)
            \                         /
             lce_port.py java / bedrock          -> out/<id>/mod.json, textures/, PORT_REPORT.md
                          |
             lce_modpack.py check  (repeat after editing the port)
                          |
             lce_modpack.py build  -> <id>/mod.lcm  -> /data/lce-mods/<id>/
```

## 1. Java mods (Forge, NeoForge, Fabric, old 1.12 layouts)

```sh
python3 tools/lce_port.py java  path/to/mod.jar  build/port/mymod  [--modid NS] [--hints hints.json] [--shapeless-as-shaped]
python3 tools/lce_modpack.py check build/port/mymod
python3 tools/lce_modpack.py build build/port/mymod out
```

The input can be a `.jar`/`.zip` or an unpacked folder.

| Source file | Becomes | Notes |
|---|---|---|
| `META-INF/mods.toml`, `neoforge.mods.toml`, `fabric.mod.json`, `mcmod.info` | name, version, author, description | the version is reduced to dotted digits |
| `assets/<ns>/lang/en_us.json` or `en_US.lang` | item and block names (`lang.en_US`) | keys `item.<ns>.<x>`, `block.<ns>.<x>`, legacy `item.<x>.name` |
| `assets/<ns>/models/item/<x>.json` with `layer0` | **item** `<id>:<x>` with its icon | `layer1` overlays are dropped (reported); 3D/custom models are skipped |
| `assets/<ns>/blockstates/<x>.json` (one variant) + `models/block/*.json` (`cube_all`, or `top`/`bottom`/`side`, or `end`/`side`) | **block** `<id>:<x>` with faces | Multipart and multivariant block states require manual work. The advanced JAR importer recognizes additional shapes from Java bytecode. |
| `assets/<ns>/textures/**.png` | 16×16 RGBA | larger images are downscaled; animated strips use frame 0 (reported) |
| `data/<ns>/recipe[s]/*.json`: `crafting_shaped` | shaped recipe | |
| …`crafting_shapeless` | shapeless recipe | `--shapeless-as-shaped` writes a fixed layout instead (reported) |
| …`smelting`, `blasting`, `smoking` | furnace recipe | blasting/smoking become furnace; duplicate inputs are skipped |
| other recipe types (stonecutting, smithing, custom serializers) | skipped | |
| `data/<ns>/worldgen/configured_feature/*.json` and matching placed features | simple ore features | Review the generated placement and block references. |
| Other worldgen, dimension and rendering files, Java code | manual work reported for recognized files | Review `PORT_REPORT.md` for evidence and porting guidance. |

Things Java mods set **in code** (hardness, tool tier, durability, food values, harvest level) are not in the data files. Supply them in a hints file (§3). Otherwise the converter uses these defaults and lists each guess:
- blocks: stone-like, hardness 1.5;
- names ending in `_pickaxe` etc. become tools with durability 250 and tier iron;
- food: hunger 4, saturation 0.3.

## 2. Bedrock add-ons

```sh
python3 tools/lce_port.py bedrock path/to/BP path/to/RP build/port/mymod [--hints hints.json]
```

| Source | Becomes |
|---|---|
| `BP/manifest.json` header | name, description, version (`[1,0,3]` becomes `1.0.3`) |
| `RP/texts/en_US.lang` | names (`item.<id>.name`, `tile.<id>.name`, `pack.name`) |
| `BP/items/*.json` with `minecraft:icon` + `RP/textures/item_texture.json` | items; `minecraft:max_stack_size`, `minecraft:food` (nutrition, saturation, can_always_eat), `minecraft:durability` (becomes a tool), `minecraft:foil`/`glint` |
| `BP/blocks/*.json` with `minecraft:material_instances` + `terrain_texture.json` | blocks; `destructible_by_mining.seconds_to_destroy` becomes hardness, `destructible_by_explosion.explosion_resistance`, `light_emission` |
| `BP/recipes/*.json`: `recipe_shaped`, `recipe_shapeless` (ingredient `count` expands), `recipe_furnace` (tag `furnace`) | recipes / smelting |
| `entities/`, `features/` | Entity clones when a base mob is supplied in hints, and simple ore features |
| `spawn_rules/`, `feature_rules/`, `loot_tables/`, `scripts/`, `functions/` | Reported for manual work |

Blocks with states, permutations or custom geometry (other than a full block) are skipped or reported.

## 3. Hints file

All fields are optional. Item and block names are the file stems (`ruby_pickaxe`), not full keys.

```json
{
  "lce_id": "rubymod",
  "aliases": {"forge:gems/ruby": "rubymod:ruby", "minecraft:some_modern_item": "minecraft:stone@3"},
  "craft_tab": "minecraft:materials",
  "items": {
    "ruby_pickaxe": {"kind": "pickaxe", "tier": "iron", "max_damage": 900},
    "ruby_apple":   {"kind": "food", "food": {"hunger": 6, "saturation": 0.8}},
    "ruby":         {"creative_tab": "minecraft:materials", "max_stack": 64, "foil": false}
  },
  "blocks": {
    "ruby_ore": {"hardness": 3.0, "resistance": 15.0, "material": "stone", "sound": "stone",
                 "harvest_tier": "iron", "drop": "rubymod:ruby", "drops": [1, 2], "light": 0}
  },
  "tabs": [{"key": "rubymod:rubies", "name": "Rubies", "icon": "rubymod:ruby"}]
}
```

| Field | Meaning |
|---|---|
| `lce_id` | mod ID if the source namespace is not a valid LCE mod ID (hyphens and caps are converted automatically) |
| `aliases` | exact replacements for item IDs or tags (`forge:…`/`c:…` tags or `minecraft:` names) the converter does not know |
| `craft_tab` | crafting tab for every converted recipe |
| `items.<name>` / `blocks.<name>` | any `mod.json` item/block field (MODDING.md §3.2/§3.3) |
| `tabs` | copied into `mod.json` |

## 4. Vanilla name translation

Java 1.13+ flattened names do not exist on 1.95, which uses pre-flattening names with data values. The converter maps them through `JAVA_TO_LCE` in `tools/lce_port.py`. Every mapping it applies is listed in `PORT_REPORT.md` under *Converted*.

| Modern | 1.95 | Modern | 1.95 |
|---|---|---|---|
| `oak_planks` … `dark_oak_planks` | `planks@0..5` | `white_wool` … `black_wool` | `wool@0..15` |
| `oak_log` … `jungle_log` | `log@0..3` | `acacia_log`, `dark_oak_log` | `log2@0..1` |
| `bone_meal`, `lapis_lazuli`, `ink_sac`, `<colour>_dye` | `dye@15`, `dye@4`, `dye@0`, `dye@15−colour` | `charcoal` | `coal@1` |
| `granite`/`diorite`/`andesite` (+`polished_`) | `stone@1..6` | `stone_bricks`, `mossy_`/`cracked_`/`chiseled_` | `stonebrick@0..3` |
| `<colour>_terracotta`, `<colour>_stained_glass`, `<colour>_concrete`, `<colour>_carpet`, `<colour>_bed` | `stained_hardened_clay@n`, `stained_glass@n`, `concrete@n`, `carpet@n`, `bed@n` | `sugar_cane`, `cobweb`, `lily_pad`, `melon_slice` | `reeds`, `web`, `waterlily`, `melon` |
| `cod`/`salmon`/`tropical_fish`/`pufferfish` | `fish@0..3` | `grass_block`, `smooth_stone`, `magma_block` | `grass`, `smoothstone`, `magma` |

Unchanged names (`stick`, `iron_ingot`, `diamond`, `apple`…) pass straight through. Names that do not exist on 1.95 (for example `netherite_ingot`, `copper_ingot`, `amethyst_shard`) are **errors**: add an `aliases` entry pointing at your own replacement item, or at a vanilla item you choose.

Common tags (`#minecraft:planks`, `#forge:ingots/iron`, `#c:iron_ingots`, `#forge:rods/wooden`…) are in `JAVA_TAGS`. Wood, wool, sand and coal tags become "any data value" ingredients.

## 5. Worked example (built and tested by `make test`)

`examples/java-rubymod/src` is a Forge-layout mod:
- 3 items: ruby, ruby pickaxe, ruby apple.
- 2 blocks: ruby block, ruby ore.
- 7 recipes: shaped, shapeless, tag-based, vanilla-output, smelting and blasting.
- A worldgen feature.

```sh
python3 tools/lce_port.py java examples/java-rubymod/src build/port/rubymod --hints examples/java-rubymod/hints.json
python3 tools/lce_modpack.py build build/port/rubymod build/pkg
```

The report records a duplicate blasting recipe, a converted ore feature and two ingredient mappings. The default target check flags the block harvest tiers.

The tests then:
- parse the package in C and fuzz it with 20,000 mutations (`test_package`);
- load it with two other mods and a native mod against the simulated game (`test_loader`). That checks IDs (blocks 262 and 280, items from 2283), numeric recipe cells (stick = 280), the ledger being persisted before publish, stable IDs on a second boot, dependency drops, a corrupt-ledger refusal and partial-publish reporting;
- confirm that the **retail** backend drops this mod with the exact reason (`lce_modpack.py check --profile retail195`: "block rubymod:ruby_ore: harvest tiers above wood are hard-coded per block"; the converter maps the Java harvest level; set `harvest_tier` to `wood` or `none` in the output to make it publish).

`examples/bedrock-sapphire` is the Bedrock equivalent: one item, one block, shaped and shapeless recipes, and an entity that is reported as skipped. It ports to mod ID `gem`.

### Checking backend compatibility

The importer checks the generated package against `retail195` by default. Select a different profile using `--profile`. The `full` profile checks the package schema and `retail195-experimental-fuel` checks against a fuel-enabled SDK build.

```sh
python3 tools/lce_port.py bedrock examples/bedrock-sapphire/BP examples/bedrock-sapphire/RP build/port/gem --require-profile
python3 tools/lce_modpack.py check build/port/gem --profile retail195
```

With `--require-profile`, the importer exits 1 for conversion or schema errors and 2 for incompatible profile content. It still writes the editable package and report. Without that option, the exit code reflects conversion errors. Review the reported source features in both cases. A profile pass covers the generated package, and [API_STATUS.md](API_STATUS.md) tracks runtime and console verification.

## 6. Porting behaviour (code)

Data conversion covers what Workshop/Forge mods declare. Behaviour needs one of these, in order of availability:

1. **Native C API events.** For native ports, compare the API major, check `api->struct_size` before accessing appended fields, and check the applicable `capabilities` or `capabilities2` flags and callback pointer before calling a service. See [the public API header](include/lce_mod_api.h).
2. **Lua scripts.** Implement supported events and services with the Lua 5.4 runtime. See [MODDING.md](MODDING.md).
3. **New backends.** Effects, potions, enchantments, biomes and custom audio now publish on retail 1.95 (strings195, effects195, enchants195, biomes195, audio195; API_STATUS.md §0).
   - `lce_autoport.py` emits them with these heuristics:
     - enchantment classes extending the damage/protection/impaler enchantments, or overriding their bonus methods, get `clone_of` sharpness/protection/impaling;
     - instant and attack-damage effect classes get an instant or strength/weakness clone;
     - per-tick effect code becomes an `EffectTick` Lua stub;
     - `.ogg` sounds become 16-bit mono WAV with `--ffmpeg`.
   - See [API_STATUS.md](API_STATUS.md) for UI status, experimental fuel and per-type limits.
4a. **Boat items (API 1.10).** `ItemBoat`/`BoatItem` subclasses and instances become `"boat"` with the wood type read from the `EntityBoat.Type`/`Boat.Type` constant in the constructor. Types 1.95 lacks (mangrove, cherry, bamboo, pale oak, mod woods) are ported as oak (guessed); chest boats are reported as manual (no chest boats in 1.95).
4. **Thrown items (API 1.9).** `lce_autoport.py` detects an item `onItemRightClick`/`use` that creates an `EntityThrowable` / `ThrowableEntity` / `ThrowableItemProjectile` / snowball-class entity. It writes `"throwable"` on the item:
   - `speed`/`inaccuracy` come from the `shoot`/`setHeadingFromThrower`/`shootFromRotation` call;
   - `damage` comes from `attackEntityFrom`/`hurt`, `fire_ticks` from `setFire`, `explode` from `createExplosion`/`explode`, and `effects` from `addPotionEffect`, all read in the projectile's `onImpact`/`onHit*`.
   `ItemSnowball`/`SnowballItem` subclasses become `"throwable": true`; egg and ender pearl subclasses do too, reported as guessed because the chicken and teleport are lost. The projectile entity class is reported as ported (it flies and renders as a snowball) instead of "non-living entity: impossible". Other impact behaviour is not ported: write it by hand in a Lua `ProjectileHit` handler (MODDING.md §8.3).
5. **Config.** `lce_autoport.py` ports Forge config specs (`ForgeConfigSpec`/`ModConfigSpec` builders, 1.12 `Configuration`) to mod.json `"config"`. `lce_modpack.py build` writes the editable `config` file, and scripts read the values with `lce.config` (MODDING.md §8.5). The generated `main.lua` starts with `CFG["key"] = lce.config(...)` for each value, so a manual port can replace `Config.X.get()` with `CFG["key"]`.

Never assume a Windows/PC mod binary, a BO3 plugin, a shader or a resource-pack JSON model works on the console.
