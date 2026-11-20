# Automatic porting of Java mods to Minecraft PS4 Edition 1.95 (`tools/lce_autoport.py`)

`lce_autoport.py` turns a NeoForge, Forge (1.7.10 to 1.21) or Fabric mod jar into an SDK source package: `mod.json`, textures, `scripts/main.lua` and `PORT_REPORT.md`. It then runs `lce_modpack.py check --profile full` and `--profile retail195`, and builds `mod.lcm`.

It goes further than `tools/lce_port.py`, which converts only the data files (models, blockstates, lang, JSON recipes). `lce_autoport.py` also reads the mod's **bytecode** to recover what Java mods set in code:
- item kinds, food values, tool and armour materials;
- block hardness, light and harvest level;
- recipes registered from code, smelting and fuels;
- ore generators, entities and their AI/attributes;
- effects, potions and enchantments (retail: `clone_of` guessed from the vanilla base class / damage-protection overrides; effect tick code -> an `EffectTick` stub, LCE_EV_EFFECT_TICK);
- behaviour overrides and event subscribers.

**Safety.** Static analysis only. Each class file is parsed in pure Python (`tools/autoport/classfile.py`), and an abstract interpreter evaluates registration code symbolically (`tools/autoport/interp.py`). Mod code is never loaded or executed:
- Jars are opened read-only, and entry sizes are capped.
- Nothing is written to a path taken from an entry name.
- The only external program it can run is the optional CFR decompiler (`--decompile`). CFR gets copies of the selected `.class` files in a fresh scratch directory, under generated names.

```
mod.jar ──► jar.py ──────────► loader / MC version / namespace -> LCE id
        ├─► classfile.py + mappings.py (SRG / intermediary names)
        │     └─► interp.py (abstract interpretation) ─► extract.py (registry facts) ─► describe.py (SDK fields)
        ├─► lce_port.convert_java on a filtered copy (models, blockstates, textures, lang, 1.13+ recipes, worldgen)
        ├─► assets.py (1.7.10 texture layout, .lang, sounds.json, loot tables, tags, 1.12 recipes, data maps, biome modifiers)
        └─► behaviour.py (overrides + @SubscribeEvent ─► Lua stubs, provable patterns translated, CFR excerpts)
              ▼
port.py: merge, sanitise ranges, validate with lce_modpack.validate (each rejected entry dropped and reported),
         write mod.json + PORT_REPORT.md, run `check --profile full|retail195`, build mod.lcm
```

## 1. Usage

```sh
# one mod
python3 -I tools/lce_autoport.py port path/to/mod.jar build/autoport/src/mymod [--hints h.json] [--decompile]
        [--build-dir build/autoport/pkg] [--no-build] [--mappings fields.csv methods.csv | file.tiny]
        [--fallbacks newer.json] [--work DIR] [--json] [--ffmpeg /usr/bin/ffmpeg]

# many mods (a directory searched recursively, or a text file with one path per line)
python3 -I tools/lce_autoport.py survey ~/mods --out build/autoport/survey [--jobs 8] [--timeout 300] [--limit N] [--keep]

# the 1.95 content-level table this document is based on (exit 1 if a claim stops holding)
python3 -I tools/lce_autoport.py content-level
```

### `port` writes

| Path | Contents |
|---|---|
| `<out>/mod.json` | the package source |
| `<out>/textures/...` | 16×16 icons and faces, 64×32/64×64 skins, 64×32 armour layers |
| `<out>/scripts/main.lua` | behaviour stubs |
| `<out>/java/` | full Java/bytecode of long methods, for reference (not packaged) |
| `<out>/PORT_REPORT.md` | the report (§6) |
| `<out>/autoport.json` | machine-readable counts |

### Exit codes

`port` exits 0 when the package passes `check --profile full`.

`survey` runs every jar in its own `python3 -I` subprocess, with a timeout. Unless `--keep` is given, it deletes the per-jar output and writes only `survey.md` and `survey.json`.

### Hints

The `lce_port.py` hints format (PORTING.md §3) is accepted, plus these keys:

| Key | Meaning |
|---|---|
| `items.<name>` / `blocks.<name>` | any `mod.json` field. It overrides what was extracted, and is reported |
| `entities.<name>` | any entity field (`base_mob`, `texture`, `spawn_biomes`...) |
| `aliases` | exact replacements for vanilla names and tags |
| `newer_fallbacks` | `{"minecraft:netherite_ingot": "minecraft:diamond"}`: an explicit substitute for content newer than 1.95. Each use is reported as *guessed* |
| `skip` | registry names or keys to leave out |
| `lce_id`, `namespace`, `era` (`old` or `modern`) | overrides for the detection |
| `craft_tab`, `shapeless_as_shaped` | passed to `lce_port.py` |

`--fallbacks file.json` gives the same table as `newer_fallbacks` on the command line.

## 2. Content level of 1.95 (evidence)

Sources: `tools/data/vanilla_195.json`, the 546 item-registration call sites of the pinned 1.95 image; `modules/events195/sound_ids_195.inc`, 665 sound events from the same image; `modules/entities195/base_mobs195.inc`, the 56 cloneable LivingEntity types. The claims below are checked by `lce_autoport.py content-level` and `tests/test_autoport.py`.

1. **Pre-flattening names with data values.** The table uses 1.12-style names (`planks`, `wool`, `red_flower`, `stonebrick`, `dye`, `log2`...).
   - Every numeric ID up to 452 equals the Java 1.12 ID: stone 1, `carved_pumpkin` 86 (Java 1.12 `pumpkin`), `slime` 165, `stone_slab_3` 182 (Java `stone_slab2`), `stone_slab_4` 205 (Java `purpur_slab`), diamond 264, `netherbrick` 405, `iron_nugget` 452.
   - So 1.7.10–1.12 mods map almost one-to-one. The few renames are in `OLD_TO_LCE` (`tools/autoport/vanilla.py`).
2. **All of Java 1.13 Update Aquatic is present** as items:
   - `trident` 454, `turtle_shell_piece` (scute) 455, `turtle_helmet` 456, `kelp`, `dried_kelp`, `dried_kelp_block`, `fish_bucket`/`salmon_bucket`/`puffer_bucket`/`tropical_bucket`, `nautilus_core` (heart of the sea), `nautilus`, `phantom_membrane` 465;
   - `conduit`, `coral_block`, `coral`, `coral_fan`, `coral_fan_dead`, `sea_grass`, `sea_pickle`, `blue_ice`, `turtle_egg`;
   - the per-wood trapdoors, buttons and pressure plates, `stripped_log_*`, `wood`, and the prismarine stairs.
   - The sound table has the trident, conduit, turtle, phantom, drowned and dolphin events. `drowned`, `dolphin`, `phantom`, `turtle` and the fish are cloneable base mobs.
3. **Java 1.14 Village & Pillage is present, with a few gaps.**
   - Present: `crossbow` 471, `banner_pattern` 472, `sweet_berries` 473, `sign_<wood>`, `lantern`, `bell`, `campfire`, `barrel`, `scaffolding`, `bamboo`, `bamboo_sapling`, `lectern`, `loom`, `composter`, `smoker`, `blastfurnace`, `grindstone`, `stonecutter2`, `cartography_table`, `fletching_table`, `smithing_table`, `sweet_berry_bush`, `leather_horse_armor`, `smoothstone`.
   - Present stairs: `stone_stairs`, `andesite_stairs`, `granite_stairs`, `diorite_stairs`, `smooth_*_stairs`, `mossy_*_stairs`, `red_nether_brick_stairs`, `end_brick_stairs`.
   - Sounds exist for the pillager, ravager, panda, wandering trader, cat, bell, campfire, composter, grindstone, lantern, loom, stonecutter, cartography table, crossbow, bamboo, scaffolding, sweet berry bush, barrel, smoker and blast furnace.
   - **Missing.** These names are not in the table: `cornflower`, `lily_of_the_valley`, `wither_rose`, `suspicious_stew`, `jigsaw`, and the separate 1.14 dyes (`black_dye`, `brown_dye`, `blue_dye`, `white_dye`).
     - The 1.14 walls and slabs (`brick_wall`, `andesite_slab`...) are not names either. They may exist as data values of `cobblestone_wall`/`stone_slab_2..4`, but nothing local proves which.
     - There are no fox sounds or entities (foxes are 1.14).
     - Pillager and ravager have sounds but are not in the cloneable base-mob list.
   - **Flower data values cannot be confirmed.** Cornflower and lily of the valley could be `red_flower@9`/`@10` (as in Bedrock), but the 1.95 catalog (`minecraft-ps4-research/tab-menu/vanilla-lists/catalog.json`) records item IDs only, not data values or creative-menu variants. So the porter reports `minecraft:cornflower` and the other three as **not in 1.95 (Java 1.14 content)**, never as a silent data value. If you confirm a data value on the console, add it as an alias or `newer_fallbacks`.
4. **No Java 1.15 or later content.**
   - No 1.15 bees or honey: no `bee_nest`/`beehive`/`honey*` names, and no bee sounds.
   - No 1.16 nether content: no `netherite*`, `crimson_*`/`warped_*`, `soul_*` (only `soul_sand`), `blackstone`, `basalt`, `chain`, `lodestone`, `target` or `respawn_anchor`; no piglin, hoglin or strider sounds.
   - Nothing from 1.17 onwards (copper, amethyst, deepslate...).

**Equivalent Java content level: about 1.14** (all of 1.13, most of 1.14), with 1.12 naming.

The porter translates every vanilla reference through `VanillaResolver` (`tools/autoport/vanilla.py`), in this order:

| Step | Source |
|---|---|
| 1 | hints aliases |
| 2 | `OLD_TO_LCE` (1.7.10–1.12 mods only) |
| 3 | `EXTRA_JAVA_TO_LCE` (1.13/1.14 names; every target is asserted to exist in the table by the tests) |
| 4 | `lce_port.JAVA_TO_LCE` |
| 5 | the 1.95 name itself |

Anything else is **reported, never substituted silently**:
- **"not in 1.95 (Java 1.16 content, newer than the 1.95 ~1.14 level)"** for known newer content, under *Impossible*. The list is `_NEWER_EXACT`/`_NEWER_RE`.
- **"has no known 1.95 equivalent"** otherwise, under *Needs manual work*.

The recipe or drop that needs the item is skipped, unless `newer_fallbacks` names a substitute.

## 3. What is extracted

### Loader detection

| File | Gives |
|---|---|
| `META-INF/neoforge.mods.toml`, `META-INF/mods.toml` | loader, mod list, display name, version; `minecraft` `versionRange` gives the MC version |
| `mcmod.info` | Forge 1.7.10–1.12 (`mcversion`) |
| `fabric.mod.json`, `quilt.mod.json` | best effort |
| nested jars (`META-INF/jarjar`, `META-INF/jars`) | listed, not analysed |

- **Era.** It is `old` (1.7.10–1.12, pre-flattening names and SRG) or `modern` (1.13+). Year-style versions (`26.2`) count as modern.
- **Namespace.** It is the declared mod id that has assets, else the namespace with the most files. `jar.lce_id` maps it to an LCE id: CamelCase becomes snake_case, invalid characters become `_`, and it is cut to 31 characters with reserved ids avoided.

### Name mappings (`tools/autoport/mappings.py`)

- **Official MCP mappings are auto-detected per Minecraft version** (`find_mcp`): `mcp_stable-<n>-<mc>.zip` / `mcp_snapshot-<date>-<mc>.zip` from the Forge maven in `$LCE_MCP_DIR`, `build/forge100/mappings`, `build/mappings`, `tools/data/mcp` or `~/.cache/lce-autoport/mcp`. Only archives of the same minor version fill gaps (SRG ids are re-used across versions). The porter never downloads anything. When an archive is found, the paragraph below (built-in table) does not apply. Results of the 100-mod Forge survey: `build/forge100/RESULTS.md`.
- **attack_bonus** is Java total melee damage minus the 1.95 kind base (sword 4, axe 3, pickaxe 2, shovel 1), because it replaces the tier damage and the console adds the base.
- **Metadata sub-items** (setHasSubtypes) are ported as separate items when getSubItems is statically provable; `item@N` references resolve to them.

- **Forge 1.7.10–1.12 release jars** name Minecraft members by SRG (`func_77655_b`, `field_151045_i`). No MCP mappings exist on this machine (searched read-only: no `fields.csv`, `methods.csv`, `*.srg` or `*.tsrg` with SRG ids). The porter therefore carries a **built-in partial table**: about 180 methods (builders, behaviour overrides, services) and about 440 fields (Items, Blocks, Material, step sounds, Potion, attributes, creative tabs).
  - It was compiled by hand and is **not verified against MCP data**. Every vanilla reference resolved through it is marked *guessed* in the report.
  - Spot checks agree with real jars: in FoodExpansion, `func_77655_b`/`func_111206_d`/`func_77637_a` take the unlocalized name, texture and tab strings, and `func_77626_a` returns 16 (eat time). In InventoryPets, `field_151045_i` (diamond) and `field_151043_k` (gold ingot) are the most-used fields.
  - The table is also applied to Forge 1.13–1.16 jars. SRG ids of long-lived members (`func_77659_a`, `field_151045_i`...) did not change in those versions; newer members stay unmapped and are reported.
  - `--mappings fields.csv methods.csv` replaces the table with real MCP data.
  - Unmapped obfuscated names are counted and listed in the report.
- **Fabric** uses intermediary names (`net/minecraft/class_1792`). The porter reads the local Tiny v2 file `.../baritone-1.21.11/fabric/.gradle/unimined/local/mappings/mappings.tiny` (intermediary → Mojang names, read-only) when it exists.
- **NeoForge 1.20.5+** jars use Mojang names and need no mappings.
- **Forge 1.17–1.20.4** (`m_123_`/`f_123_`) needs per-version mappings that are not on this machine. Those names are reported as unmapped.

### Registrations (`extract.py`)

- **The abstract interpreter.**
  - It tracks constants (`ldc`, `iconst`, `bipush`, `sipush`, `fconst`...), with folding and float32 rounding (`0.6000000238` becomes `0.6`).
  - Objects: `new`/`dup`/`<init>` with their arguments, and every later call on them (builder chains, setter returns).
  - Static fields across classes: `getstatic` lazily runs the owner's `<clinit>` and the methods that `putstatic` the field.
  - Arrays; `StringBuilder` and `invokedynamic` string concatenation; lambdas and method references (`invokedynamic` → implementation method, run on demand).
  - Calls into the mod's own classes are inlined with the real argument values, and subclass constructors run with `this` bound to the object being built. Zero-argument static init methods run once.
  - Branches are walked linearly, which is what registration code needs.
  - A step budget bounds the work: 150,000 steps per method and 10 million per jar. Very large jars that exhaust it are flagged *"bytecode facts are partial"*.
- **Old Forge:**
  - `GameRegistry.registerItem`/`registerBlock` (name argument, or `getUnlocalizedName().substring(5)`), `register`, `RegistryEvent.Register` `getRegistry().register/registerAll`, `setRegistryName`.
  - Objects found in static fields without a recognised registration are added as *guessed*.
- **Modern:**
  - `DeferredRegister.register(name, supplier)` (the lambda is followed), `registerItem`/`registerBlock`/`registerSimpleItem`/`registerSimpleBlock`/`registerSimpleBlockItem`.
  - `Registry.register(BuiltInRegistries.X, id, obj)` (vanilla, NeoForge and Fabric), `RegisterEvent` helpers, and Registrate `item(...)`/`block(...).register()`.
- Registrations of other registries (block entities, menus, recipe serializers, data components, sounds, tabs) are listed under *Needs manual work* or *Impossible*.

### Items (`describe.py`)

| Field | Old Forge | Modern |
|---|---|---|
| `kind` | vanilla base class: `ItemFood`, `ItemSword`/`Pickaxe`/`Axe`/`Spade`/`Hoe`, `ItemArmor`, `ItemTool` + name | `SwordItem`..., `ArmorItem`, `Item.Properties.sword/pickaxe/...` (1.21.2+) |
| food | `ItemFood(hunger, saturation[, wolf])` from `super(...)`; `setPotionEffect(id, sec, amp, p)` (×20 ticks); `setAlwaysEdible`; `getMaxItemUseDuration` → `eat_ticks` | `Properties.food(FoodProperties.Builder.nutrition().saturationModifier().effect().alwaysEdible().fast())` |
| tool | `EnumHelper.addToolMaterial(name, harvest, uses, efficiency, damage, ench)` or vanilla `ToolMaterial.X`: tier from harvest level, `max_damage` = uses, `mining_speed`, `enchantability`, `attack_bonus` = damage + vanilla per-kind base (sword 4, axe 3, pick/shovel 1) | `Tiers.X`, `SimpleTier`/`ForgeTier`, mod enum tiers (field order level/uses/speed/damage/enchantment, *guessed*) |
| armour | `EnumHelper.addArmorMaterial(name, factor, int[4], ench)` (1.7.10 head-first; 1.9+ feet-first reversed), vanilla `ArmorMaterial.X`: `max_damage` = [11, 16, 15, 13][slot] × factor, `armor_points` | `ArmorItem(material, Type.X, props)`, `durability(Type.X.getDurability(n))`; armour points *guessed* when the material is a registry entry |
| other | `setMaxStackSize`/`maxStackSize` field, `setMaxDamage`, `setFull3D` → `handheld`, `setCreativeTab(CreativeTabs.tabX)`, `hasEffect` → `foil`, `getItemEnchantability`, `getBurnTime` → `fuel_ticks`, `setTextureName` / `registerIcons` strings → icon | `stacksTo`, `durability`, `rarity` (dropped), `fireResistant` (impossible) |

Block items, bows, throwables, records, potions and spawn eggs are reported. Spawn eggs give the entity egg colours.

### Blocks

| Field | Old Forge | Modern |
|---|---|---|
| material | `Block(Material.rock/iron/wood/...)` | `mapColor` |
| hardness | `setHardness` (or the `blockHardness` field) | `strength(h, r)`, `destroyTime`, `instabreak` |
| resistance | `setResistance(r)` → blast resistance `r × 3 / 5` (1.7.10–1.12 scale) | `strength(h, r)`, `explosionResistance` |
| light | `setLightLevel(f)` → `round(f × 15)` | `lightLevel(state -> 15)`; the lambda is evaluated |
| sound | `setStepSound(soundTypeX)` | `sound(SoundType.X)` |
| harvest | `setHarvestLevel(tool, level)` | `requiresCorrectToolForDrops`; `data/minecraft/tags/block/mineable/<tool>` and `needs_<tier>_tool` |
| drops | `getItemDropped`/`quantityDropped` constant returns (`1 + rand.nextInt(3)` → `[1, 3]`) | loot tables `data/<ns>/loot_table(s)/blocks/*.json` with one item entry and `set_count` |
| other | | `noOcclusion` → transparent, `noCollission`, `friction` → slipperiness, `replaceable` |

- **Textures.** In order: models/blockstates via `lce_port.py`; rotation-only blockstates (`facing`/`axis`), which also give `placement`; `setBlockTextureName`, `registerBlockIcons` strings, and `_top`/`_side`/`_bottom` files.
- A missing icon gets a generated placeholder, reported as *guessed*.
- Stairs, slabs, fences, crops, containers and other non-cube shapes are ported as full cubes, and *manual* is reported.

### Recipes

| Source | Notes |
|---|---|
| `GameRegistry.addRecipe/addShapedRecipe` (`ItemStack`, `Object...`) | pattern strings, `Character` keys, mod objects, `ItemStack(x, count, meta)`, vanilla `Items.X`/`Blocks.X` |
| `addShapelessRecipe` | |
| `ShapedOreRecipe`/`ShapelessOreRecipe` | ore dictionary names go through `OREDICT`, or through the mod's own `OreDictionary.registerOre` |
| 1.12 (name, group, output, ...) signatures | |
| `CraftingManager` | |
| `GameRegistry.addSmelting`, `FurnaceRecipes.addSmelting*` | smelting |
| 1.12 `assets/<ns>/recipes/*.json` | including `data`, `forge:ore_dict` and `_constants.json` |
| 1.13+ `data/<ns>/recipe(s)` | through `lce_port.py`, with tags resolved from `MODERN_TAGS` or the mod's own `data/*/tags/item(s)` (first member, *guessed*) |

- Duplicates seen through several call paths are merged by a structural signature.
- Recipes needing content that is not ported, or not in 1.95, are skipped with the reason.

### Other content

| Content | Sources |
|---|---|
| **fuels** | `IFuelHandler.getBurnTime` (`if (stack.getItem() == X) return N` pairs); `getBurnTime` overrides; NeoForge `data_maps/item/furnace_fuels.json` |
| **ores** | `IWorldGenerator`: `new WorldGenMinable(block, size)` (or `(block, meta, size, target)` / `IBlockState`); `per_chunk` from the enclosing loop bound; `min_y`/`max_y` from the `y` argument range (`10 + rand.nextInt(30)` → 10–39); dimension assumed overworld (*guessed*) |
| ores (modern) | `configured_feature`/`placed_feature` through `lce_port.py` |
| **entities** | `EntityRegistry.registerModEntity`/`registerGlobalEntityID` (name, egg colours), `addSpawn` (weight, group, category); modern `EntityType.Builder.of(Foo::new, MobCategory.X).sized(w, h)` and `add_spawns` biome modifiers |
| entity `base_mob` | the superclass (table below); attributes from `applyEntityAttributes` (`getEntityAttribute(maxHealth).setBaseValue(v)`) or `createAttributes().add(Attributes.X, v)`; AI goals from `tasks.addTask`/`goalSelector.addGoal` |
| entity drops | loot table, or `getDropItem` |
| entity skin | `textures/entity/*.png` matched by name, if 64×32 or 64×64 |
| **effects** | `Potion` subclasses (1.7.10: allocated with `(id, isBad, colour)` + `setPotionName`; 1.12 and modern `MobEffect(category, colour)` registrations) |
| **potions** | `new Potion(new MobEffectInstance(...))` |
| **brewing** | `addMix` / `BrewingRecipeRegistry.addRecipe` |
| **enchantments** | `Enchantment` subclasses (`super(id, weight, type)`, `getMaxLevel`); 1.21 `data/<ns>/enchantment/*.json` |
| **sounds** | `sounds.json` + `.ogg` (manual unless `--ffmpeg`, §5) |
| **lang** | `en_US.lang` (`item.<unlocalized>.name`, `tile.<x>.name`) and `en_us.json`; title-cased fallback (*guessed*) |

### Behaviour → `scripts/main.lua`

- **What gets a stub.** Every non-data override in a ported item, block or entity class, and every `@SubscribeEvent` method.
- **Binding.** The stubs target `modules/script/lce_script.c` (`lce.EV.*`, `lce.subscribe`, payload field names), through `LUA_BINDING` (`behaviour.py`). The binding keeps one callback per event and reads its result with `lua_toboolean`, so the script has one dispatcher per event and handlers return nil/false (true cancels). Events the binding does not expose are never subscribed.
- **Filtering.** Stubs subscribe to the matching event and filter on `lce.resolve_item/resolve_block(key)`.
- **Comments.** Each stub carries the method's CFR Java (`--decompile`, with SRG names replaced) or a bytecode listing. Long ones are excerpted, with the full text in `<out>/java/`.
- **Translated automatically** (provable patterns only):
  - `addPotionEffect(new PotionEffect(const, const, const))` → `lce.add_effect`;
  - `heal(4/8/16)` → `instant_health` I/II/III (exactly equal);
  - a constant chat message → `lce.send_chat`.
- **Food-only effects become data.** An `onFoodEaten`/`onItemUseFinish` that only applies constant effects becomes `food.effects` in `mod.json` instead of Lua.
- **Everything else is manual**, and the reason is named:
  - explosion, fire, spawning, set block, durability, drop, damage, sound, motion and GUI need services or events that lce_api 1.1 lacks;
  - field writes;
  - non-constant arguments.

## 4. Mapping tables (summary; full tables in `tools/autoport/vanilla.py`)

### Entity base mob from the superclass

| Mod entity extends | `base_mob` |
|---|---|
| `EntityZombie`/`Zombie`, `EntitySkeleton`/`Skeleton`, `EntityCreeper`, `EntitySpider`, `EntityEnderman`/`EnderMan`, `EntityPigZombie`/`ZombifiedPiglin`, `EntityBlaze`, `EntityWitch`, `EntitySlime`, `EntityGhast`... | the same mob (`zombie_pigman` for pig zombies) |
| `EntityPig`, `EntityCow`, `EntitySheep`, `EntityChicken`, `EntityWolf`, `EntityHorse`, `EntityVillager`, `EntityIronGolem` → `villager_golem`, `EntitySnowman` → `snowman`, `EntityMooshroom`, `EntityOcelot`/`Cat`, `Dolphin`, `Drowned`, `Phantom`, `Turtle`, `Panda`, `WanderingTrader`... | the same mob |
| `EntityMob`/`Monster`/`EntityLiving`/`Mob` | `zombie` (*guessed*) |
| `EntityAnimal`/`Animal`/`EntityCreature`/`PathfinderMob` | `pig` (*guessed*) |
| `EntityTameable` | `wolf` (*guessed*) |
| `EntityWaterMob` | `squid` (*guessed*) |
| `EntityFlying` | `ghast` (*guessed*) |
| `EntityAmbientCreature` | `bat` (*guessed*) |
| `EntityThrowable`, `EntityArrow`, `EntityFireball`, `Projectile`, minecarts, boats, falling blocks... | **impossible** (entities195 clones LivingEntity types only) |

### AI goals

| Java task | SDK goal |
|---|---|
| `EntityAISwimming`/`FloatGoal` | `swim` |
| `EntityAIWander`/`RandomStrollGoal` | `wander` |
| `EntityAIWatchClosest`/`LookAtPlayerGoal` | `look_at_player` |
| `EntityAIPanic` | `panic` |
| `EntityAIAttackOnCollide`/`MeleeAttackGoal` | `melee_attack` |
| `EntityAINearestAttackableTarget` (player class only) | `target_player` |
| `EntityAITempt` | `follow_tempt_item` |
| `EntityAIMate` | `breed` |
| `EntityAIAvoidEntity` | `avoid_entity` |
| `EntityAIFleeSun` | `flee_sun` |

Look-idle and hurt-by-target are ignored. Other (custom) tasks are reported as manual.

### Behaviour → event

| Java method | LCE event | Notes |
|---|---|---|
| `onItemRightClick`, `onItemUse`, `use`, `useOn` | `ITEM_USE` | filtered by item id |
| `onFoodEaten`, `onItemUseFinish`, `finishUsingItem` | `ITEM_USE` (approximation) or `food.effects` | |
| `onBlockDestroyedByPlayer`, `breakBlock`, `playerDestroy` | `BLOCK_BREAK` | filtered by block id |
| `onBlockPlacedBy`, `onBlockAdded`, `setPlacedBy` | `BLOCK_PLACE` | |
| entity `onDeath`/`die` | `ENTITY_DEATH` | |
| `attackEntityFrom`/`hurt` | `ENTITY_DAMAGE` | |
| `hitEntity`, `onUpdate`/`inventoryTick`, `onBlockActivated`/`use` (block), `onEntityWalking`/`stepOn`, `onEntityCollidedWithBlock`, `updateTick`/`randomTick`, entity ticks and interaction | none | manual stub; the missing event is named |
| `randomDisplayTick`/`animateTick` | none | impossible: client particles |

### Forge/NeoForge events

| Event | LCE event |
|---|---|
| `PlayerInteractEvent.RightClickItem` | `ITEM_USE` |
| `BlockEvent.BreakEvent` | `BLOCK_BREAK` |
| `PlaceEvent` | `BLOCK_PLACE` |
| `LivingHurtEvent`/`LivingAttackEvent`/`LivingDamageEvent` | `ENTITY_DAMAGE` |
| `LivingDeathEvent`/`LivingDropsEvent` | `ENTITY_DEATH` |
| `PlayerLoggedIn`/`Out` | `PLAYER_JOIN`/`PLAYER_LEAVE` |
| `ServerChatEvent` | `CHAT` |
| `ServerTickEvent`/`WorldTickEvent`/`LevelTickEvent` | `SERVER_TICK` |
| `ClientTickEvent` | `CLIENT_TICK` |
| `WorldEvent.Load`/`LevelEvent.Load`, `ServerStartedEvent` | `WORLD_LOAD` |

Rendering, GUI, tooltip and HUD events are impossible. Registration and lifecycle events are handled by the data port. All others are listed as manual.

### Other tables

| What | Mapping |
|---|---|
| Materials | `rock` → stone, `iron`/`anvil` → metal, `wood`, `ground`/`grass`/`clay`/`snow` → dirt, `sand`, `glass`/`ice`, `cloth`/`carpet`/`web` → wool, `plants`/`leaves`/`vine`/`cactus` → plant |
| Step sounds | `soundTypeX` and `SoundType.X` → `stone`/`wood`/`gravel`/`grass`/`metal`/`glass`/`cloth`/`sand` |
| Creative tabs | `tabBlock`, `tabFood`... and the 1.19+ `CreativeModeTabs` → the SDK vanilla groups. Mod tabs are reported (all mod items appear in the console's Mods tab) |
| Potion IDs 1–27 and `Potion.X`/`MobEffects.X` fields | the 1.95 effect registry order (`modules/items195/effects.cpp`) |

## 5. Limits

- **Statically resolvable content only.**
  - Content built in loops over config arrays, from reflection, from config files or from data the mod loads at runtime is reported unresolved.
  - Registrate builders inside a separate library jar are not followed. Create's blocks come from its blockstates and models instead.
- **The interpreter is flow-insensitive.** The last write wins and both branches of a condition contribute. Registration code is straight-line in practice; behaviour translation checks every side effect, so a branchy method is never auto-translated.
- **SRG names** come from the official MCP archive found for the jar's MC version (§3); the built-in table is only a fallback.
- **Metadata variants of mod items and blocks** (`setHasSubtypes`, multi-state blocks) port as one item or block, and are reported.
- **Block shapes, block entities, GUIs, custom models and renderers, fluids, dimensions, structures and biomes** are not representable in `mod.json`, or not automatically. They are reported.
- **Sounds.**
  - Without `--ffmpeg` they are manual.
  - `--ffmpeg PATH` is an opt-in that decodes each event's first `.ogg` to a 16-bit mono 22 kHz WAV with a local ffmpeg. The file is copied under a generated name, and ffmpeg never sees jar paths.
  - It is **not exercised by the tests**: the test policy allows no external program other than CFR.
- The Lua stubs are loaded by the tests in a real Lua 5.4 built from `modules/script/lua` with a mock `lce` table.
- **Retail 1.95 today** (`--profile retail195`) loads only generic items and shaped count-1 recipes.
  - Most ports report profile problems (exit 2) that are true of the current backend (gaps G1/G2/G4/G5).
  - The porter keeps the full content and does not split mods automatically. Split by hand as in PORTING.md §5.

## 6. `PORT_REPORT.md`

Per mod, the report has these parts:
- a found/ported table;
- the loader and version evidence;
- four sections: **Ported automatically**, **Guessed (check these)**, **Needs manual work** and **Impossible on 1.95**. Every line names its evidence (class.method, registration call or file);
- the verbatim output of `lce_modpack.py check --profile full` and `--profile retail195`, and of the build.

## 7. What would need downloads (not done)

| Download | Unlocks |
|---|---|
| MCP mappings: `mcp_stable-12-1.7.10` and `mcp_stable-39-1.12` (`fields.csv`, `methods.csv`) | exact SRG → MCP names for every 1.7.10–1.12 jar (the built-in table covers the porter's needs only) |
| MCPConfig / official mappings per version for Forge 1.17–1.20.4 (`m_`/`f_` SRG ids → Mojang names) | bytecode facts for those jars (today: names reported unmapped, assets still ported) |
| Intermediary + Mojang/Yarn Tiny files for older Fabric versions | names of classes that no longer exist in 1.21.11 (the local file covers the current ones) |
| A pure-Python Ogg Vorbis decoder (or permission to run the local ffmpeg routinely) | automatic sound conversion without an external program |
| A 1.95 creative-menu or data-value dump | settles whether cornflower / lily of the valley exist as `red_flower@9/@10`, and the 1.14 walls/slabs as data values |

## 8. Survey results (2026-11-18)

### All candidate jars

The run covered all 3,551 candidate jars on this machine: 2,792 distinct files after SHA-256 de-duplication. Results are in `build/autoport/survey-all/survey.md` and `survey.json`.

- **Every distinct jar ported, and every resulting package passes `check --profile full`.** 51 jars failed in the first pass: a crash, invalid long keys, per-mod limits and the Lua size limit. They were fixed and re-run.
- **About 350 of the jars declare content.** The rest are libraries, APIs, client and UI mods, or JDK jars.

| Content | ported / found |
|---|---|
| items | 13,761 / 16,571 |
| blocks | 9,026 / 17,540 |
| recipes | 5,882 / 67,157 (27,954 of the found recipes are crafting or smelting; the rest are machine recipes with no 1.95 equivalent) |
| entities | 431 / 1,558 |

The biggest losses:
- the per-mod limits (512 items, 256 blocks; split such mods by hand);
- non-living entities;
- recipes that use content which is not ported or not in 1.95;
- registry objects built at runtime (reflection, config loops, Registrate).

### Named test set

The named set is in `build/autoport/survey-named/`, with the ports in `build/autoport/named-ports/`.

| Mod | items | blocks | recipes | entities |
|---|---|---|---|---|
| FoodExpansion | 26/26 | | 28/29 | |
| InventoryPets | 92/92 | 6/6 | 131/131 | 7/10 |
| TragicMC | 205/210 | 100/103 | 210/391 | 51/84 |
| Lucky Block (NeoForge) | | 1/1 | 2/3 | |
| Create | 163/163 | 118/118 | 53 of 669 crafting | |
