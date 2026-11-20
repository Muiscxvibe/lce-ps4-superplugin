# Making mods for Minecraft PS4 Edition 1.95 (LCE mod loader SDK)

This guide takes you from nothing to a working mod. It then documents every field, rule, error message and API function.

If you are converting an existing Java or Bedrock mod, read [PORTING.md](PORTING.md) after the quick start. For what the console can do today, see [API_STATUS.md](API_STATUS.md).

---

## 1. Quick start (5 minutes)

Everything runs on a Linux PC with Python 3 and Pillow (`pip install pillow`). No PS4 toolchain is needed to make a data mod.

```sh
cd /home/codey/minecraft-lce-work/modloader-sdk

# 1. create a working example mod you can edit
python3 tools/lce_modpack.py new greengems --name "Green Gems" --dir mymods

# 2. edit mymods/greengems/mod.json and the PNGs in mymods/greengems/textures/

# 3. validate: errors first, then what today's console backend can publish
python3 tools/lce_modpack.py check mymods/greengems
#   ok: 2 items, 0 blocks, 2 recipes, 0 smelting, 2 textures, 2 strings
#   profile retail195: everything in this mod is publishable

# 4. compile the package
python3 tools/lce_modpack.py build mymods/greengems out
#   built out/greengems/mod.lcm (… bytes, sha256 …)

# 5. look inside the compiled package
python3 tools/lce_modpack.py dump out/greengems/mod.lcm
```

`out/greengems/` is the complete install folder. On the console it must end up at `/data/lce-mods/greengems/` (§9).

The scaffold contains two items and two shaped recipes. Copy its patterns:

```json
{
  "format": 1,
  "id": "greengems",
  "name": "Green Gems",
  "version": "1.0.0",
  "author": "you",
  "description": "Describe your mod here.",
  "target": "CUSA00265-1.95",
  "textures": {
    "greengems:gem":   "textures/gem.png",
    "greengems:shard": "textures/shard.png"
  },
  "items": [
    {"key": "greengems:gem",   "name": "Green Gem", "texture": "greengems:gem",   "creative_tab": "minecraft:materials"},
    {"key": "greengems:shard", "name": "Gem Shard", "texture": "greengems:shard", "creative_tab": "minecraft:materials"}
  ],
  "recipes": [
    {"key": "greengems:gem_from_shards", "type": "shaped", "pattern": ["SS", "SS"],
     "keys": {"S": "greengems:shard"}, "result": "greengems:gem", "craft_tab": "minecraft:materials"},
    {"key": "greengems:shard", "type": "shaped", "pattern": ["G", "F"],
     "keys": {"G": "minecraft:glass", "F": "minecraft:flint"}, "result": "greengems:shard", "craft_tab": "minecraft:materials"}
  ],
  "lang": {"en_US": {"greengems:gem": "Green Gem", "greengems:shard": "Gem Shard"}}
}
```

---

## 2. Core ideas (read once)

**Mod ID.** 2–31 characters from `a-z 0-9 _`, for example `greengems`. It is the folder name on the console, and it must equal `"id"` in `mod.json`. These IDs are reserved and refused, because the plugin uses those folders for other things: `minecraft`, `packs`, `lce`, `reservations`, `worlds`, `downloads`, `maps`, `updates`, `cache` and `staged`. Folder names with `-` (`selection-*`, `texture-packs`) are never mod IDs either.

**Keys.** Every item, block, texture, recipe, tab and string has a key `<modid>:<name>`:
- `<name>` may use `a-z 0-9 _ . / - #`; the whole key must be shorter than 64 bytes.
- You can only create keys in **your own** namespace. The loader rejects `othermod:thing` in your package, and you can never override vanilla content or another mod's strings.

**Vanilla references.** Write `minecraft:<name>` using the **1.95 Legacy names**, not modern Java names. For example:
- `minecraft:planks@0` for oak planks, `minecraft:wool@14` for red wool, `minecraft:dye@4` for lapis.
- `@<n>` is the data value (aux).
- `minecraft:#265` refers to an item by numeric ID.

The full list of 546 names and IDs is in `tools/data/vanilla_195.json`. `tools/lce_port.py` translates modern names automatically (PORTING.md §4).

**Numeric IDs are automatic and permanent.** You never choose numbers. On first load the console assigns each new key an ID from the mod range:
- Items: 2283–2397.
- Blocks: a list of 19 slots proven empty on the console.

The IDs are written to `/data/lce-mods/reservations/sdk-ids-v1.txt` (SHA-256 protected) **before** anything is registered. From then on the same key gets the same ID forever, whatever order mods are installed in, so worlds stay valid.

The rules:
- **Never rename a key** after people have used your mod. A renamed key is a new item, and the old one becomes "missing" in worlds.
- **Uninstalling a mod keeps its IDs reserved.** No other mod can take them.
- **Do not open a world that contains a mod's items or blocks while that mod is missing or failed.** The 1.95 block decoder does not tolerate unknown blocks. Placeholders that make this safe are gap G7.
- If the ledger is damaged, the loader refuses to load **any** mod and tells you to restore it, rather than handing out new numbers. A `.bak` copy of the previous version sits next to it.

**Fail-closed.** Every mod is validated completely before anything touches the game:
- A mod with any problem, including content the console backend cannot represent faithfully, is **dropped as a whole** with a reason in the log.
- Mods that depend on it are dropped too.
- Other mods still load.
- If the game side refuses the plan (a pinned signature mismatch, for example), **nothing** is registered.

**No code is required.** A data mod is just `mod.json` plus PNGs. Native C/C++ mods exist (§8) but are experimental and disabled on retail consoles.

---

## 3. `mod.json` reference

Top level:

| Field | Type | Required | Rules / default |
|---|---|---|---|
| `format` | int | no | `1` |
| `id` | string | **yes** | mod ID (§2) |
| `name` | string | **yes** | display name, up to 127 bytes UTF-8 |
| `version` | string | **yes** | dotted digits, e.g. `1`, `1.2`, `1.2.3.4` |
| `author` | string | no | up to 127 bytes |
| `description` | string | no | up to 511 bytes |
| `target` | string | no | must be `CUSA00265-1.95` |
| `dependencies` | object | no | `{"othermod": "1.0"}`: that mod must be installed, loaded and at least that version. Up to 16 |
| `textures` | object | no | §3.1 |
| `items` | array | no | §3.2 |
| `blocks` | array | no | §3.3 |
| `recipes` | array | no | §3.4 |
| `smelting` | array | no | §3.5 |
| `tabs` | array | no | §3.6 |
| `lang` | object | no | §3.7 |
| `effects`, `potions`, `brewing`, `enchantments`, `fuels`, `entities`, `ores`, `features`, `structures`, `biomes`, `sounds`, `particles`, `scripts` | | no | §3.8 (API 1.1) |
| `config` | object | no | §8.5 (API 1.8): the settings players can edit. It is not packaged: `build` writes the default `<out>/<id>/config` file |

Limits per mod:
- 1024 textures, 512 items, 256 blocks, 1024 recipes, 512 smelting, 8 tabs, 8192 strings.
- The package must stay under 8 MiB.
- On the current retail backend: at most 32 items and 32 recipes (shared native catalog).

### 3.1 `textures`

```json
"textures": {
  "mymod:ruby":       "textures/ruby.png",
  "mymod:ruby_block": {"file": "textures/ruby_block.png", "atlas": "terrain"}
}
```

| Field | Rules |
|---|---|
| key | your namespace |
| `file` | path relative to the folder that contains `mod.json`; PNG |
| `atlas` | `items` (default) for item icons, `terrain` for block faces |

- Images must be exactly **16×16**. Anything is converted to RGBA with straight alpha.
- `--resize` on `check`/`build` downscales larger images (nearest neighbour). Draw at 16×16 for good results.
- Item textures go into the item atlas (appended rows after the 288 stock icons). Stock icons are never changed.

### 3.2 `items`

```json
{"key": "mymod:ruby", "name": "Ruby", "texture": "mymod:ruby",
 "creative_tab": "minecraft:materials", "max_stack": 64}
```

| Field | Type | Default | Rules |
|---|---|---|---|
| `key` | string | — | required; must not equal a block key |
| `name` | string | key | English display name (overridden by `lang.en_US`) |
| `texture` | key | — | required; an `items`-atlas texture |
| `kind` | string | `generic` | `generic`, `food`, `sword`, `pickaxe`, `axe`, `shovel`, `hoe`, `helmet`, `chestplate`, `leggings`, `boots` |
| `max_stack` | int | 64 (1 for tools) | 1..64; tools and armour must be 1 |
| `max_damage` | int | 0 | durability; required (> 0) for tools and armour |
| `tier` | string | `none` | `none`, `wood`, `stone`, `iron`, `diamond`, `gold` |
| `food` | object | — | `{"hunger": 4, "saturation": 0.3, "always_edible": false}`; required for `food` |
| `attack_bonus` | number | 0 | extra weapon damage |
| `handheld` | bool | true for tools | render like a tool |
| `foil` | bool | false | enchantment glint |
| `creative_tab` | key | none | a vanilla group (§3.6) or one of your tabs |
| `throwable` | `true` or object | — | API 1.9, `generic` items only: using the item throws it like a snowball. Object fields (all optional): `speed` (0.1..4, snowball 1.5), `inaccuracy` (0..20, snowball 1), `damage` (health points dealt to the entity hit, 2 = one heart), `fire_ticks` (sets the hit entity on fire, ≤ 12000), `explode` (explosion radius at the impact, ≤ 8; with `fire_ticks` the explosion also starts fires), `effects` (≤ 8, like food effects: `effect`, `duration`, `amplifier`, `chance`) |
| `boat` | `"oak"` `"spruce"` `"birch"` `"jungle"` `"acacia"` `"dark_oak"` (or 0..5) | — | API 1.10, `generic` items only, not with `throwable`: using the item places the **stock boat** of that wood type. `max_stack` defaults to 1 |

**Throwables (API 1.9).** The item becomes a SnowballItem, and the stock snowball throw runs. The new snowball gets your kind's speed. When it hits something on the server, `damage`, `fire_ticks`, `effects` (entity hits) and `explode` (any hit) apply, and scripts get `ProjectileHit` (§8.3). Limits:
- The projectile **looks like a snowball**. 1.95 renders per entity type, and the type has no item sprite.
- A projectile saved in flight reloads as a plain snowball.
- Remote players see plain snowballs.
- The stock snowball knockback (0 damage) still applies.
- At most 16 throwable items per game.
`ItemUse` still fires before the throw.

**Boat items (API 1.10).** The item becomes a BoatItem (the stock place-a-boat use, with your wood type). The boat itself is the stock entity: stock model, speed and physics, and a broken boat drops the **stock** boat item of that wood (not yours). Chest boats, new wood types, custom boat models and other vehicles (minecarts) are not possible. `ItemUse` still fires before the boat is placed.

**Retail 1.95:** every kind publishes (items195 constructors). Food may use one vanilla **or mod** effect. The default profile refuses `fuel_ticks`; an experimental opt-in is described below. Other restrictions include a custom `armor_texture` (use none = iron, or `minecraft:chainmail/iron/diamond/gold`) and food with more than one effect. §4.1 has the full list.

### 3.3 `blocks`

```json
{"key": "mymod:ruby_block", "name": "Block of Ruby",
 "texture": "mymod:ruby_block",
 "hardness": 5.0, "resistance": 30.0, "material": "metal", "sound": "metal",
 "harvest_tier": "iron", "light": 0, "creative_tab": "minecraft:building_blocks"}
```

| Field | Default | Rules |
|---|---|---|
| `texture` | — | one terrain texture key, or `{"all":…, "top":…, "bottom":…, "side":…}` / `{"up","down","north","south","west","east"}` |
| `hardness` | 1.5 | stone 1.5, iron block 5, obsidian 50 |
| `resistance` | 6.0 | blast resistance |
| `material` | `stone` | `stone`, `wood`, `metal`, `dirt`, `sand`, `glass`, `plant`, `wool` |
| `sound` | `stone` | `stone`, `wood`, `gravel`, `grass`, `metal`, `glass`, `cloth`, `sand` |
| `light` | 0 | 0..15 |
| `harvest_tier` | `none` | minimum tool tier |
| `drop` | itself | item key dropped when broken |
| `drops` | `[1,1]` | `[min,max]`, 0..64 |
| `transparent` | false | see-through like glass (retail: the GlassBlock render/occlusion set; faces against the same block are skipped) |
| `no_item` | false | no block-item is created (then `drop` is required) |

Each block also creates a **block-item** with the same key, so recipes can use and produce it. Its numeric item ID is derived from the block ID (`id < 256 ? id : id + 768`, the game's own rule).

**Retail 1.95:** full cubes, one-box shapes, transparent and non-colliding blocks and metadata variants publish (blocks195). Storage blocks: `storage_slots: 9` gives the block a real 9-slot container (the game's DispenserBlockEntity: 3x3 screen, saved with the chunk, contents dropped when broken, hoppers and comparators work); `storage_slots: 27` makes the block a ChestBlock with the mod's cube textures (ChestBlockEntity, chest screen, no chest model inside the cube); two identical 27-slot blocks side by side open as one 54-slot double chest (the game pairs only the same block, never a vanilla chest). Both kinds title their screen with the block's display name, like an anvil-named chest (saved as `CustomName`). Other sizes are refused. Refused: `placement` other than fixed, `replaceable`, material `glass` without `transparent`, material `plant` without `no_collision`, `harvest_tier` above wood, a harvest tool that does not match the material.

**Variants and shapes (API 1.7).**

```json
{"key": "stonemod:marble", "name": "Marble", "texture": "stonemod:marble",
 "variants": [{}, {"name": "Black Marble", "texture": {"top": "stonemod:marble_black_top", "side": "stonemod:marble_black"}},
              {"name": "Grey Marble", "texture": "stonemod:marble_grey"}]},
{"key": "stonemod:marble_slab", "name": "Marble Slab", "texture": "stonemod:marble", "box": [0, 0, 0, 16, 8, 16]}
{"key": "stonemod:marble_stairs", "name": "Marble Stairs", "texture": "stonemod:marble", "variants": [{}, {"name": "Marble Stairs (E)"}, {"name": "Marble Stairs (S)"}, {"name": "Marble Stairs (W)"}],
 "boxes": [[0, 0, 0, 16, 8, 16], [0, 8, 8, 16, 16, 16]], "rotate_boxes": true}
```

| Field | Rules |
|---|---|
| `variants` | 2..16 entries. Entry `n` is data value `n`; entry 0 is the block itself and stays `{}` (its name and texture are the block's). Each later entry may set `name` and `texture` (same forms as the block's `texture`; missing faces fall back to the variant's first face, then to the block's texture). `set_block(..., data)`, recipes with `result_aux`, `/give` with a data value and the drop all keep the variant; the block-item stacks by data value and places its own variant. Data values past the last variant read as variant 0 |
| `box` | `[x0, y0, z0, x1, y1, z1]` in 1/16 block (0..16, min < max per axis, not the full cube): the outline **and** the collision box. A face is solid (supports torches, culls the neighbour) only where the box covers it fully. One box; for several use `boxes` |
| `boxes` | list of 1..8 boxes like `box` | — | API 1.11 multi-box block: every box is drawn (render shape 10, the stairs tesselator), collides, and culls faces. The outline is their union. A face is solid only if a box covers it in every state. Cannot be combined with `box` |
| `variant_boxes` | one `boxes` list per variant | — | API 1.11: the shape follows the data value (top/bottom/double slab, stairs facings). Every list has the same box count |
| `rotate_boxes` | bool | false | API 1.11, with `boxes` and exactly 4 variants: variant v is `boxes` turned v × 90° clockwise seen from above (north, east, south, west stairs) |

Retail 1.95 notes: variants reuse ColoredBlock's 16-value state property (the block's states are its 16 data values); the Creative/mapped list shows only data value 0. Collision follows the box exactly (the default collision path reads the same shape). `no_collision` blocks have no collision box and are not solid-render; with `box` they still have the outline. On the console the block faces show the stock fallback sprite for now, and block-items are not in Creative (API_STATUS.md §0, gap G1).

### 3.4 `recipes`

**Shaped.** `pattern` has 1–3 rows of 1–3 characters. A space is an empty cell. Each symbol is defined in `keys`.

```json
{"key": "mymod:ruby_block", "type": "shaped",
 "pattern": ["RRR", "RRR", "RRR"], "keys": {"R": "mymod:ruby"},
 "result": "mymod:ruby_block", "count": 1, "craft_tab": "minecraft:materials"}
```

**Shapeless.** 1–9 ingredients, in any order.

```json
{"key": "mymod:rubies", "type": "shapeless",
 "ingredients": ["mymod:ruby_block"], "result": "mymod:ruby", "count": 9}
```

**Ingredients and results** are keys, optionally with `@<data value>`:
- `"minecraft:wool@14"` means red wool only.
- `"minecraft:wool"` means any wool.
- The object form `{"item": "minecraft:dye", "aux": 4}` also works.
- `count` is 1..64.

**`craft_tab`** chooses where the recipe appears in the crafting menu (§3.6).

**Retail 1.95:** shaped and shapeless recipes, `count` 1..64, result data values, exact ingredient data values (`wool@14` matches only red wool) and several recipes for one output all publish through the ABI-2 publisher (items195). Up to 128 recipes per boot.

### 3.5 `smelting`

```json
{"key": "mymod:ruby_from_ore", "input": "mymod:ruby_ore", "result": "mymod:ruby", "count": 1, "xp": 0.7}
```

- One recipe per input (data value included). Two mods smelting the same input collide, and the later mod is dropped.
- **Retail 1.95:** published at the title screen into the game's furnace table (items195), up to 64. XP is applied by the game.

### 3.6 Tabs and groups

**Vanilla groups.** Use these keys for `creative_tab` and `craft_tab`:

| Key | Crafting category | Creative category |
|---|---|---|
| `minecraft:tools`, `minecraft:combat`, `minecraft:weapons` | Tools | Tools |
| `minecraft:materials` | Materials | Materials |
| `minecraft:food` | Food | Food |
| `minecraft:armour`, `minecraft:armor` | Armour | Tools |
| `minecraft:redstone`, `minecraft:mechanisms` | Tech | Redstone |
| `minecraft:transportation`, `minecraft:transport` | Transport | Transport |
| `minecraft:building_blocks`, `minecraft:structures` | Misc | Building |
| `minecraft:decorations` | Misc | Decoration |
| `minecraft:brewing` | Misc | Brewing |
| `minecraft:misc` | Misc | Misc |

On the console:
- Every mod item appears in the separate **Mods** Creative tab, because stock tabs are never modified. Your `creative_tab` is kept as metadata.
- Recipes appear in the **Mods** crafting tab under the matching category.

**Your own tab:**

```json
"tabs": [{"key": "mymod:rubies", "name": "Rubies", "icon": "mymod:ruby", "creative": true, "crafting": true}]
```

Then use `"creative_tab": "mymod:rubies"`. On the current retail backend your tab merges into the Mods tab (gap G6).

### 3.7 `lang`

```json
"lang": {
  "en_US": {"mymod:ruby": "Ruby", "mymod:ruby_block": "Block of Ruby"},
  "fr_FR": {"mymod:ruby": "Rubis"}
}
```

- Keys must be in your namespace. Values can be up to 255 bytes of UTF-8. Locales look like `en_US` or `fr_FR`.
- Item names are shown from `en_US`, falling back to `name`. Other locales are stored for when game-language selection lands (gap G8).
- Characters outside the Basic Multilingual Plane show as `?`.

---

### 3.8 More content (API 1.1)

Every section below is validated by `lce_modpack.py check`, packaged into `mod.lcm` and loaded by the core. Whether the **console** can publish it today depends on the backend (see the coverage matrix in API_STATUS.md §0). The loader drops a mod with an exact reason when the backend cannot publish something, rather than half-loading it. A complete worked example of every section is `tests/fixtures/allcontent/mod.json`, which is built and loaded by the tests.

**Texture atlases.**

| `atlas` | Size | For |
|---|---|---|
| `items` | 16×16 | item icons |
| `terrain` | 16×16 | block faces |
| `armor` | 64×32 | armour layer (`armor_texture`) |
| `entity` | 64×32 or 64×64 | mob skins |
| `icon` | 16×16 | effect and particle icons |

**Extra item fields.**

| Field | Meaning |
|---|---|
| `armor_points` | 1..20, required for armour |
| `armor_texture` | an `armor` texture |
| `repair_item` | item used to repair it |
| `enchantability` | enchanting power |
| `mining_speed` | tools |
| `fuel_ticks` | furnace burn time; coal = 1600 |
| `food.effects` | list of `{"effect", "duration", "amplifier", "chance"}` |
| `food.eat_ticks` | eating time |

**Extra block fields.**

| Field | Meaning |
|---|---|
| `storage_slots` | 1..54: a block entity with an inventory. Retail 1.95: 9 (DispenserBlockEntity, 3x3 screen) or 27 (the block is a wooden ChestBlock: ChestBlockEntity, the 3-row chest screen; material must be `wood`; two of the same block side by side open as a 54-slot double chest). The screen title is the block's display name (static evidence; not yet run on a console) |
| `placement` | `fixed`, `facing` or `axis` |
| `harvest_tool` | `pickaxe`, `axe`, `shovel`, `hoe`, `shears` or `sword` |
| `flammability` | how easily it burns |
| `slipperiness` | 0.6 is normal; ice is 0.98 |
| `replaceable`, `no_collision` | booleans (`no_collision`: walk through it; retail uses FlowerBlock's empty collision slot) |
| `variants`, `box` | API 1.7, see "Variants and shapes" above |

**`effects`:**
```json
{"key": "m:sparkle", "name": "Sparkle", "color": "#ffee00", "beneficial": 1, "icon": "m:fx", "clone_of": "minecraft:glowing"}
```
- `clone_of` reuses a vanilla effect's behaviour: regeneration, poison, wither, hunger, health boost, absorption, saturation and instant health/damage run the vanilla code with your effect, and speed, slowness, haste, mining fatigue, strength, weakness and luck copy its attribute modifiers.
- **Retail 1.95 (effects195):**
  - The colour becomes the nearest vanilla effect colour.
  - `icon` must be a vanilla effect key (`minecraft:speed`): the icon frames live in the UI movie.
  - `instant: 1` needs an instant clone.
  - Clones of effects the game only tests by identity are refused: night vision, invisibility, jump boost, resistance, fire resistance, water breathing, blindness, nausea, glowing, levitation, slow falling, conduit power, dolphin's grace, bad omen, hero of the village.
  - Ids 32..127 come from the ledger, and saves keep them.
- Custom behaviour comes from `LCE_EV_EFFECT_TICK` (Lua `EffectTick`: `entity`, `amplifier`, `effect_id`, `effect`). It runs every 20 ticks for an effect without `clone_of`, and on every tick the cloned vanilla behaviour runs. Lua: `lce.subscribe("EffectTick", function(e) if e.effect == "m:sparkle" then lce.heal(e.entity, 1) end end)`.
- Names: `name` (en_US) and any `lang` entry for the effect key, in every language the package translates.

**`potions`:**
```json
{"key": "m:sparkle_potion", "name": "Potion of Sparkle", "effects": [{"effect": "m:sparkle", "duration": 3600, "amplifier": 0}], "splash": 1, "lingering": 0}
```
- Effects can be vanilla or mod effects, up to 8.
- The splash, lingering and tipped-arrow forms are automatic: the item stores the potion key, and the vanilla gunpowder and dragon's-breath mixes apply.
- Names: "Potion of X", "Splash Potion of X", "Lingering Potion of X" and "Arrow of X", where X is `name` without "Potion of ". Override any of them with `lang` keys `<key>`, `<key>.splash`, `<key>.lingering` and `<key>.arrow`.
- Mod potions are obtained by brewing. They do not appear in Creative yet.

**`brewing`:**
```json
{"key": "m:brew", "input": "minecraft:awkward", "reagent": "m:gem", "output": "m:sparkle_potion"}
```
Vanilla bases: `water`, `mundane`, `thick`, `awkward`, and the standard potions. `input` and `output` can also be mod potions, so you can chain brews (awkward → mod potion → stronger mod potion).

**`enchantments`:**
```json
{"key": "m:shine", "name": "Shine", "max_level": 3, "weight": 5, "applies_to": ["pickaxe", "sword"], "treasure": 0, "clone_of": "minecraft:sharpness"}
```
- **Retail 1.95 (enchants195):**
  - `clone_of` must be one of sharpness, smite, bane_of_arthropods, impaling, protection, fire_protection, feather_falling, blast_protection or projectile_protection. Omit it for an enchantment with no built-in effect: it is still listed on the table and in the anvil and saved, and scripts react to it.
  - Enchantments the game reads by identity are refused as clones, because a clone would do nothing: efficiency, fortune, looting, silk touch, unbreaking, mending, thorns and the like.
  - `max_level` is 1..10.
  - Ids 100..1023 come from the ledger.
  - Enchanting tables and the anvil pick the enchantment up automatically.

**`fuels`** (any item, including vanilla; one value per item across all mods):
```json
{"item": "minecraft:stick", "burn_ticks": 150}
```
`burn_ticks` is 1..72000. The default retail build and `retail195` profile refuse custom fuel. For an experimental candidate, build with `LCE_EXPERIMENTAL_FUEL=1` and check using `--profile retail195-experimental-fuel`; `fuel_ticks` on an item and `fuels` declarations share a 64-item limit. Declaring both for the same item is a collision and is rejected before publication. On PS4, direct placement in an ordinary furnace was verified for inline 1600-tick fuel and explicit 800-tick fuel, including save and reload. Hopper and Quick Move behavior remain unverified; stock insertion rules may apply. See INTEGRATION.md for details.

**`entities`:**
```json
{"key": "m:grove_zombie", "base_mob": "minecraft:zombie",
 "health": 30, "attack_damage": 4, "move_speed": 0.28, "category": "monster", "spawn_weight": 10, "min_group": 1, "max_group": 2,
 "goals": [{"goal": "wander", "priority": 3, "speed": 1.2}],
 "spawn_biomes": ["minecraft:plains"], "sound_ambient": "minecraft:entity.zombie.ambient"}
```
- Goals: `wander`, `look_at_player`, `panic`, `melee_attack`, `target_player`, `flee_sun`, `follow_tempt_item`, `breed`, `swim`, `avoid_entity`.
- Retail 1.95 uses the base mob's model, renderer, skin, display name, and loot. See the support table below for attribute and goal restrictions.

**`ores`** (shorthand for an ore feature):
```json
{"key": "m:gem_ore_gen", "block": "m:gem_ore", "size": 6, "per_chunk": 4, "min_y": 5, "max_y": 40}
```

**`features`:** `kind` is one of `ore`, `tree`, `flower`, `lake` or `structure`, with `dimension`, `block`/`block2`, `structure`, `size`, `per_chunk`, `min_y`/`max_y`, `rarity` (1 in N chunks) and `biomes`.

**`structures`:** use `size_x/y/z`, a `palette` of block keys, and either `blocks` (indices, `index = (y*size_z + z)*size_x + x`) or `layers` with a `legend`. A space or `"keep"` leaves the existing block.
```json
{"key": "m:shrine", "size_x": 3, "size_y": 2, "size_z": 1, "palette": ["minecraft:stone", "m:gem_ore"],
 "legend": {"S": "minecraft:stone", "G": "m:gem_ore"}, "layers": [["SSS"], [" G "]]}
```

**`biomes`:**
```json
{"key": "m:gem_fields", "name": "Gem Fields", "temperature": 0.8, "rainfall": 0.4, "top_block": "minecraft:grass",
 "filler_block": "minecraft:dirt", "grass": "#55ff99", "foliage": "#33cc66", "water": "#3f76e4", "sky": "#78a7ff", "weight": 5, "features": ["m:gem_ore_gen"]}
```

- **Retail 1.95 (biomes195):**
  - The biome is a real biome object at a free id (168..254, then 50..126; the ledger keeps it).
  - It generates in new chunks of the overworld, in the climate band given by `temperature`: 1.0 and above warm, 0.5 and above temperate, 0.15 and above cold, below that icy.
  - `weight` 10 is one vanilla biome entry, 1..16 entries.
  - Grass, foliage, water and sky are exact RGB values; leave a colour out to use the vanilla colormap.
  - Features and spawn lists may name the biome key.
  - Nether/End biomes and hills/shore/river variants are not generated.
  - If the mod is later removed, its biome id keeps a placeholder so old chunks still load.

**`sounds`:** either `{"key": "m:ding", "clone_of": "minecraft:random.orb"}` to replay a vanilla sound event, or `{"key": "m:chime", "file": "sounds/chime.wav", "volume": 1.0, "pitch": 1.0}` for **your own audio**.
- The file must be a 16-bit mono WAV at 8–48 kHz, at most about 95 s at 22.05 kHz (4 MiB). Convert `.ogg` first, or let `lce_autoport.py --ffmpeg` do it.
- On retail 1.95 (audio195) the sound is a real sound event that plays through the game's own Miles mixer, with distance fade, stereo pan and the sound-volume option.
- `lce.play_sound(level, x, y, z, "m:chime")` plays it. Up to 8 mod sounds play at once.

**`particles`:**
```json
{"key": "m:sparkles", "clone_of": "minecraft:reddust", "tint": "#ffff00"}
```
Particle descriptors are currently stored as metadata. Use `lce.spawn_tinted_particles` from an event to emit a tinted stock particle.

**`scripts`:** Lua behaviour without native code.
```json
"scripts": {"main.lua": "scripts/main.lua"}
```
The script API is in §8.5.

**Commands and keybinds** contain callbacks, so they are registered from native code or scripts (`register_command`, `register_keybind`), not from `mod.json`.

## 4. Checking and building

| Command | What it does | Exit code |
|---|---|---|
| `lce_modpack.py new <id> [--name N] [--dir D]` | Creates a valid starter mod and never overwrites | 0 |
| `lce_modpack.py check <src> [--profile retail195\|full] [--resize]` | Validates everything (keys, references, vanilla names, textures, limits), then lists what the chosen backend would drop | 0 ok; 1 errors; 2 profile problems |
| `lce_modpack.py build <src> <out> [--profile P] [--resize]` | Writes `<out>/<id>/mod.lcm` and a copy of `mod.json`. Profile problems are printed as warnings | 0 / 1 |
| `lce_modpack.py dump <mod.lcm>` | Prints the decoded package | 0 / 1 |

- **Output is deterministic:** the same source always gives byte-identical `mod.lcm` (tested).
- The `retail195` profile mirrors exactly what the composite console backend (`loader/retail195/composite195.cpp`) accepts. `tests/test_tools.py` runs the C backend's own checks on `tests/fixtures/allcontent` and `tests/fixtures/retailall` (`build/host/test_composite195 --verdicts`) and fails if the two disagree on any key.

### 4.1 The `retail195` profile (what the console accepts)

The console backend is all-or-nothing **per mod**: one refused entry drops the whole mod, with the first reason in the log. `check` lists every refused entry. Full evidence and RVAs: API_STATUS.md §0.

| Content | Accepted | Refused (reason in one line) |
|---|---|---|
| Items | `generic`; `pickaxe` `axe` `shovel` `hoe` `sword` (tier wood..gold, durability, mining speed, attack bonus, enchantability, repair item); `helmet` `chestplate` `leggings` `boots` (protection points, durability, layer `minecraft:chainmail/iron/diamond/gold` or none = iron); `food` (hunger, saturation, eat time 16/32, always-edible, **one** vanilla or mod effect); `throwable` generic items (API 1.9: speed 0.1..4, inaccuracy 0..20, damage, fire, explosion, effects; up to 16); `boat` generic items (API 1.10: place a stock boat of a wood type 0..5) | fuel (`fuel_ticks`), FOIL glint, custom armour layer texture, leather layer, food with 2+ effects, stack > 1 for tools/armour, a throwable that is not `generic`, a custom projectile model |
| Blocks | Full cubes, `box`, `boxes` and `variant_boxes`. Supports per-face textures, 2–16 metadata variants, and 9-slot or 27-slot storage as described in §3.3. | Other container sizes, facing or axis placement, replaceable blocks, and harvest tier above wood. |
| Recipes | shaped and shapeless, result count 1..64 and data value, ingredient data values (`minecraft:wool@14`), several recipes per output, up to 128 | — |
| Smelting | up to 64, XP, count/aux results | two recipes for one input item |
| Brewing | stock or mod potion → stock or mod potion, with a mod or vanilla reagent | from = to, `minecraft:empty` |
| Effects | new status effects: vanilla-clone behaviour (forwarded tick / attribute modifiers), or none + `EffectTick` script; nearest vanilla colour, vanilla icon frame, translated names | clone of an identity-checked effect, new icon art, `instant` without an instant clone |
| Potions | new potion types with up to 8 vanilla/mod effects; splash, lingering and tipped-arrow forms; names per variant | Creative entries (brew them) |
| Enchantments | new enchantments, levels I..X, table weight, treasure, item categories; damage/protection clones or no built-in effect | clone of an identity-read enchantment (efficiency, fortune, unbreaking, ...) |
| Biomes | new overworld biomes: generation weight per climate band, top/filler blocks, height, grass/foliage/water/sky colours, features, spawns | nether/end biomes, ocean-depth biomes, hill/shore/river variants |
| Fuel | experimental opt-in only; see §3.8 | refused by the default `retail195` profile and build |
| Entities | clones of 56 vanilla mobs: health, speed, attack (Monster bases), extra goals wander/look/swim/target/avoid/melee (Monster)/breed (Animal), natural spawns per biome, vanilla mob sounds | custom skin texture, custom drops, hitbox size, panic/flee-sun/tempt goals, custom audio |
| Features | Ores, trees (3–8 high), lakes (`min_y` ≥ 6), and structures that fit one chunk. Features can generate in the Overworld and End. Custom biome filters apply in the Overworld. | Flowers, Nether features, and custom biome filters in the End. |
| Sounds, particles | clones of a vanilla sound event (volume/pitch); **custom audio** from a 16-bit mono WAV / tinted clone of a vanilla particle | music/record streams, mob sound slots that name a mod sound / a new particle texture |
| Scripts | `scripts/main.lua` (§8) | an event name the runtime does not know (`ServerTick`, not `server_tick`) |
| Strings | every locale; the current game language is used | — |
| Limits | 32 SDK items (shared 64-entry catalog), 19 blocks, 16 entity types, 32 features | |

---

## 5. What happens on the console

1. **Discover.** At game start the loader lists `/data/lce-mods/`:
   - It ignores `packs`, `reservations`, dotfiles and invalid names.
   - It reads `<id>/mod.lcm` (up to 8 MiB) and checks the SHA-256, the format, the API version and the target game.
   - The mod ID must equal the folder name.
   - A `native.prx` next to a package makes the mod **rejected**, because native mods are disabled.
2. **Register.** Each package's content is replayed through the same API a native mod would use (`lce_api`). Every call is checked: keys in your namespace, ranges, duplicates.
3. **Validate.** Every reference must resolve: textures, items in recipes, tabs, dependencies and versions. The loop repeats until stable, so dependants of a dropped mod drop too.
4. **Allocate IDs** from the ledger and **persist** it. If writing fails, nothing loads.
5. **Arm the console backend.** It registers the `lce-sdk` item and recipe providers with the game's startup content stack and installs the block-registry data hook. Scripts already ran in step 2.
6. During game start each phase publishes its part, in the game's own order: blocks (after the last stock block), items of every kind (item window), BlockItems and drops, recipes (crafting pre-cache), then at the title screen furnace recipes, brewing mixes, entity types, spawns, renderers and world generation. INTEGRATION.md §2 has the table.

**Log lines** go to the plugin log:

```
lce-mods: discovered 3 mod(s), 3 staged
lce-mods: dropped rubymod: block rubymod:ruby_ore: retail 1.95 blocks: harvest tiers above wood are hard-coded per block in the tool classes (not implemented) (-4)
lce-mods: published 2 mod(s): 9 items, 1 blocks, 5 recipes, 1 smelting, 5 textures (0 placeholders)
LCE_SDK mod=greengems v=1.0.0 state=3 status=0 items=2 blocks=0 recipes=2 smelts=0 reason=-
LCE_SDK caps=… events=1 scripts=1
LCE_SDK boot=1 status=…
LCE_SDK phase=after_items rc=1 …
LCE_SDK phases block_hook=1 blocks=1 items=1 after_items=1 recipes=1 smelt_brew=1 entity_types=1 spawns=1 worldgen=1 renderers=1 world_entry=1
```

`state` values: 0 discovered, 1 rejected (package or code problem), 2 staged, 3 live, 4 dropped (validation, dependency, ID or backend refusal).

**Status codes** (`lce_status`):

| Code | Name | Typical cause |
|---|---|---|
| 0 | `LCE_OK` | |
| −1 | `LCE_E_INVALID` | malformed field, key outside your namespace, folder/ID mismatch |
| −2 | `LCE_E_COLLISION` | duplicate key, two mods smelting the same input, a recorded ID now taken by the game |
| −3 | `LCE_E_PHASE` | registration after `lce_mod_main` returned |
| −4 | `LCE_E_UNSUPPORTED` | content the active backend cannot publish (see API_STATUS) |
| −5 | `LCE_E_FULL` | ID range or capacity exhausted |
| −6 | `LCE_E_BACKEND` | game-side guard refused (signature/pin mismatch) |
| −7 | `LCE_E_IO` | ledger could not be written |
| −8 | `LCE_E_HASH` | package corrupted (re-download) |
| −9 | `LCE_E_VERSION` | built for another API major or game build |
| −10 | `LCE_E_NOTFOUND` | unknown item/texture/tab, or a missing dependency |
| −11 | `LCE_E_LEDGER` | ID ledger corrupt: restore `sdk-ids-v1.txt` from `.bak` |

**Enable and disable.** A mod loads when its folder exists. To disable it, move the folder out of `/data/lce-mods/`, and read §2 about worlds first. An optional `enabled`-marker mode exists in the loader (`LCE_LOADER_REQUIRE_ENABLED`) for a future Mods-menu toggle.

---

## 6. Recipes for common situations

- **Storage block** (9 items ↔ block): a shaped 3×3 recipe for the block, plus a shapeless 1 → 9 recipe (`"count": 9`) for the reverse. Both publish on retail.
- **Tool using a stick:** `"pattern": ["RRR", " S ", " S "], "keys": {"R": "mymod:ruby", "S": "minecraft:stick"}` with a `pickaxe` result (`tier`, `max_damage`).
- **Any planks:** use `"minecraft:planks"` without `@`. For oak only, use `minecraft:planks@0` (matched exactly by the native recipe code).
- **Coloured dye ingredients:** `minecraft:dye@<n>` (0 ink sac … 15 bone meal); see the converter table.

---

## 7. Testing without a console

```sh
make test      # builds the example packages and runs every host test (ASan/UBSan)
```

- `tests/test_loader.cpp` loads real packages through the core against a simulated game with the exact 1.95 shapes: 2268/2398 item vector, 366 block slots, atlases.
- `tests/test_retail195_bridge.cpp` runs the console backend through the shipping plugin's **own** item bridge, window and catalog code.
- `tests/test_composite195.cpp` loads `tests/fixtures/retailall` (one of everything the console accepts) and `allcontent` (one of everything the format allows) through the loader, the composite backend and every startup phase, in the console's order, on a fake image.

To check your own mod the same way, copy `tests/fixtures/retailall` as a template and run `build/host/test_composite195 --verdicts out/<id>/mod.lcm`.

---

## 8. Scripting (Lua) and runtime services

A package may carry `scripts/main.lua`. It runs **once, during registration** (before IDs are allocated), in a fresh sandboxed Lua 5.4.8 VM that belongs to your mod alone. Registration calls made by the script are validated and published exactly like `mod.json` content; event callbacks then run on the game's server thread.

```json
"scripts": {"main.lua": "scripts/main.lua"}
```

```lua
-- scripts/main.lua
lce.register_item{key = "mymod:spark", name = "Spark", texture = "mymod:spark"}
local ticks = 0
lce.subscribe("ServerTick", function(e) ticks = ticks + 1 end)
lce.subscribe("PlayerJoin", function(e) lce.log("welcome " .. tostring(e.name)) end)
lce.subscribe("Chat", function(e) if e.message == "!quiet" then return true end end)  -- true = cancel
```

### 8.1 Sandbox

| Limit | Value |
|---|---|
| Libraries | `base` (no `dofile`/`loadfile`/`require`), `table`, `string`, `math`, `utf8`, `coroutine`. No `io`, `os`, `debug`, `package` |
| `load` | text chunks only; bytecode is refused |
| Memory | 4 MiB per mod on the console (allocation over the cap fails with a Lua memory error) |
| Instructions | 20,000,000 per VM, accumulated across registration and event callbacks. Once the limit trips, the VM stops executing for the rest of the session, even inside `pcall`. |
| `print` / `lce.log` | go to the plugin log as `LCE_SDK script: …` |

A script error during registration **rejects the mod** (`state=1`, reason `script main.lua: <Lua message>`).

### 8.2 Registration (only while `main.lua` runs)

| Lua | Same as `mod.json` |
|---|---|
| `lce.register_texture{key, atlas, rgba}` | §3.1. Use `items` for item icons or `terrain` for block faces and pass a 1024-byte binary string of 16×16 row-major RGBA8 pixels with straight alpha. Returns success and status. |
| `lce.register_item{key, name, texture, max_stack, kind, creative_tab, max_damage, tier, food_hunger, food_saturation_milli, use_ticks, always_edible, fuel_ticks}` | §3.2 (`kind` is the numeric `lce_item_kind`; `tier` is numeric `lce_tool_tier`, 0..5; retail tools need 1..5 and `max_stack = 1`); food uses `kind = 1`, `food_hunger` in half-shanks, `food_saturation_milli` as saturation × 1000, optional `use_ticks` 0/16/32 and `always_edible` boolean; `fuel_ticks` defaults to 0, accepts integers 0..72000, and needs the experimental fuel backend when nonzero. |
| `lce.register_block{key, name, texture, hardness_milli, …}` | §3.3. Accepts per-face textures, variants, one-box and multi-box geometry, storage slots and block properties. |
| `lce.register_recipe{key, result, count, shapeless, cells, width, height}` | §3.4 |
| `lce.register_smelting{key, input, result, count, xp_milli}` | §3.5 |
| `lce.register_fuel{item, burn_ticks}` | §3.8; integer ticks 1..72000. Returns success and status. It requires the experimental fuel backend. |
| `lce.register_feature{key, kind, dimension, block, block2, structure, size, per_chunk, min_y, max_y, rarity, biomes}` | world-generation feature; `kind` is numeric (1 ore, 2 tree, 4 lake, 5 structure), and `biomes` is an optional array of biome keys. Returns success and status. |
| `lce.register_entity{key, base_mob, max_health_milli, move_speed_milli, attack_damage_milli, category, spawn_weight, min_group, max_group, goals, spawn_biomes, sound_ambient, sound_hurt, sound_death, spawn_egg}` | §3.8. Registers a custom type with a vanilla base and returns success and status. |
| `lce.register_biome{key, name, temperature_milli, rainfall_milli, top_block, filler_block, grass_rgb, foliage_rgb, water_rgb, sky_rgb, weight, height_base_milli, height_variation_milli, features}` | Overworld land biome in §3.8. Omitted numeric fields default to zero. Climate and height values use thousandths. Colours are integer `0xRRGGBB` values. |
| `lce.register_structure{key, size_x, size_y, size_z, palette, blocks}` | Structure template in §3.8. Supply the palette and cell indices as dense arrays. Cells are ordered by x, then z, then y. Index 0 preserves the existing block, and `palette[1]` still requires a valid block key. |
| `lce.subscribe(event, fn)` | events below |

#### Textures

```lua
local rgba = string.rep(string.char(83, 214, 107, 255), 16 * 16)
local ok, status = lce.register_texture{key = "mymod:gem_icon", atlas = "items", rgba = rgba}
assert(ok, "register_texture failed: " .. status)
assert(lce.register_item{key = "mymod:gem", name = "Gem", texture = "mymod:gem_icon"})
```

Use your mod namespace and register each texture key once across the manifest and Lua. The loader copies the bytes and stages them for the atlas owner to apply during startup or resource reload. A malformed Lua descriptor raises an error, and loader rejections return `false` and the status code. Declare content that uses a Lua-generated texture in Lua so the packer can still validate manifest texture references. Changing Lua-generated textures requires restarting the mod session.

#### Blocks

`lce.register_block` reads descriptor tables and returns `ok, status` from the
registration planner. The game backend checks the block again before publication.

- `texture` is a terrain texture key or a table with `all`, `side`, and individual
  face keys `down`, `up`, `north`, `south`, `west`, and `east`. `bottom` and `top`
  are aliases for `down` and `up`. Set `all` or `down` for the base block.
- `variants = {{}, {name = "Blue", texture = "m:blue"}, …}` lists 2–16 metadata
  states. The first entry represents data value 0 and uses the base name and texture.
  Subsequent entries can override the name or per-face texture. Missing variant faces
  use the variant's down texture, then the corresponding base face.
- `box = {x0, y0, z0, x1, y1, z1}` sets single-box geometry in sixteenths of a block.
  `boxes = {{…}, …}` uses 1–8 boxes for every state. `variant_boxes = {{{…}}, …}`
  lists an equal number of boxes for each state. `boxes` with `rotate_boxes = true`
  and four variants generates successive 90° rotations. Coordinates are integers in
  0–16 and each minimum must be below its corresponding maximum.
- `storage_slots = 9` uses the dispenser inventory. `storage_slots = 27` requires an
  opaque, colliding wood cube (`material = 1`) with no variants. Two compatible
  adjacent blocks can form a 54-slot double chest. See §3.3 for retention and backend
  limits.
- Optional properties include numeric `material`, `sound`, `placement`, and
  `harvest_tier` (the C enums), `hardness_milli`, `resistance_milli`,
  `slipperiness_milli`, `light_emission`, `flammability`, `drop_min`, and
  `drop_max`, item key `drop`, `harvest_tool`, and booleans `transparent`,
  `no_item`, `no_collision`, `random_tick`, and `replaceable`. Retail 1.95
  placement and block flag restrictions apply as in §3.3.

#### Mobs

This Lua package registers a zombie clone with 30 health points and adds a wandering goal.

```lua
local ok, status = lce.register_entity{
    key = "m:grove_zombie",
    base_mob = "minecraft:zombie",
    max_health_milli = 30000,
    category = 1,
    spawn_weight = 10,
    min_group = 1,
    max_group = 2,
    spawn_biomes = {"minecraft:plains"},
    goals = {{kind = 1, priority = 3, speed_milli = 1200}},
    spawn_egg = true
}
assert(ok, "could not stage mob " .. status)
```

`key` and `base_mob` are required. Zero attributes inherit the base values. `spawn_weight` defaults to zero and opts out of natural spawns. Set `category` to 0 for creature, 1 for monster, 2 for water, or 3 for ambient. Omit `spawn_biomes` to inherit the base mob's eligible biomes in the selected category. `spawn_egg = true` uses the base mob's egg name and colours where that mob has an egg.

Each goal takes the numeric `lce_ai_goal_kind` in `kind`, plus optional `priority`, `speed_milli`, and `param`. Retail 1.95 accepts up to eight added goals. It supports wander (1), look at player (2), melee (4) and target (5) on monsters, breed (8) on animals, swim (9), and avoid (10). For target and avoid, `param` can name a compatible vanilla mob or `minecraft:player`. Use matching vanilla mob sound slots such as `minecraft:entity.zombie.hurt`.

`true, 0` means the loader staged the descriptor. The startup plan still checks the base, goal and sound compatibility, and renderer readiness. This Lua binding supports the existing native clone path on 1.95. Custom skins, display names, drops, hitboxes, egg RGB colours, and remote client synchronisation require further engine support.

#### Biomes and structures

Biome and structure registration return `ok, status`. A successful call stages
the descriptor for the loader's commit checks. A backend without the capability
returns `false, LCE_E_UNSUPPORTED` (−4). Malformed Lua tables raise an error.

```lua
assert(lce.register_biome{
  key = "m:amber_grove", name = "Amber Grove", temperature_milli = 800,
  rainfall_milli = 400, weight = 20, grass_rgb = 0x9a8427,
})
assert(lce.register_structure{
  key = "m:shrine", size_x = 2, size_y = 1, size_z = 2,
  palette = {"minecraft:bedrock", "minecraft:stone"}, blocks = {1, 0, 1, 1},
})
assert(lce.register_feature{
  key = "m:shrine_gen", kind = 5, dimension = 0, structure = "m:shrine",
  per_chunk = 1, min_y = 64, max_y = 80, biomes = {"m:amber_grove"},
})
```

Use the feature's `biomes` list to restrict placement. Retail 1.95 places
features when new chunks in the Overworld (`dimension = 0`) or End
(`dimension = 2`) decorate. It refuses Nether features (`dimension = 1`),
and the loader has no custom dimension registration API. Retail structure
templates fit one chunk, with limits of 16 × 64 × 16 blocks after applying
the loader's size limit. A template needs a structure feature to generate.

Workshop-compatible globals map onto the same calls: `registerItem`, `registerBlock`, `registerShapedRecipe`, `registerShapelessRecipe`, `registerSmeltingRecipe`, `registerEvent`. `registerItemTier`, `registerArmorMaterial`, `registerSapling`, `registerOreFeature` and `registerTreeFeature` are accepted as **no-ops** and counted as denied, so an old script still loads; declare those things in `mod.json` instead.

### 8.3 Events

| Name (`lce.subscribe`, or `lce.EV.<NAME>`) | Payload fields | Cancel (return `true`) |
|---|---|---|
| `ServerTick` | `tick` | no |
| `WorldLoad`, `WorldUnload` | — | no |
| `PlayerJoin`, `PlayerLeave` | `name`, `player` | no |
| `Chat` | `message`, `player` | yes |
| `ItemUse` | `item`/`item_id`, `aux`, `player`; API 1.2: `level`, `x`, `y`, `z` (the player's block position), `hand` | no |
| `BlockBreak`, `BlockPlace` | `x`, `y`, `z`, `block`/`block_id`, `player`; API 1.2: `level`, `face` | no |
| `BlockUse` (API 1.2) | as `BlockBreak`, for a right-click on your block | yes: `true` consumes the click |
| `EntityDamage`, `EntityDeath` | `amount`, `entity`, `source`; API 1.2: `level`, `x`, `y`, `z`, `attacker`, `attacker_item_id` | damage only |
| `EntityInteract` (API 1.2) | `entity`, `source` = `attacker` = the player, `level`, `x`, `y`, `z` | yes: `true` = handled (the entity's own interaction is skipped) |
| `EntityTick` (API 1.2) | `entity`, `level`, `x`, `y`, `z` | no |
| `EffectTick` | `entity`, `amplifier`, `effect_id`, `effect` (the effect key) for your status effects | no |
| `BlockTick` (API 1.2) | as `BlockBreak`, no `player` | no. Scheduled and random ticks of your block; random ticks need the block marked random-ticking (served once the retail195 composite installs the route) |
| `InventoryTick` (API 1.3) | `item_id`, `player` = `entity` (the holder), `slot`, `selected`, `level`, `x`, `y`, `z` | no. Every tick for every inventory slot holding your item |
| `BlockNeighbor` (API 1.3) | as `BlockBreak`, `face` = side of the neighbour that changed (0 down, 1 up, 2 north, 3 south, 4 west, 5 east; -1 not adjacent) | no |
| `BlockStepOn` (API 1.3) | as `BlockBreak`, `entity` (= `player`) is any entity walking on your block | no |
| `BlockEntityInside` (API 1.3) | as `BlockBreak`, `entity` is any entity inside your block's cell | no |
| `ProjectileHit` (API 1.9) | `item_id` (your throwable item), `projectile`, `entity` (what was hit; `nil` for a block), `damage`, `level`, `x`, `y`, `z` (the impact block) | no. Server thread, after the item's `throwable` fields were applied, before the snowball is removed |
| aliases | `ServerTickEvent`, `PlayerJoinEvent`, `PlayerBlockBreakEvent`, `ItemCompleteUseEvent` | |

- `ItemUse` fires for your **generic** items (the item's private vtable routes `Item::use`).
- `BlockBreak`/`BlockPlace`/`BlockUse`/`BlockTick`/`BlockNeighbor`/`BlockStepOn`/`BlockEntityInside` fire for your blocks; `InventoryTick` for your items.
- API 1.3 events are provisional ids until `lce_mod_api.h` lists them; test `capabilities2` (`LCE_CAP2_EV_INVENTORY_TICK`, `_NEIGHBOR`, `_BLOCK_COLLIDE`, `_BLOCK_TICK`) before relying on them.
- `EntityDamage`/`EntityDeath`/`EntityTick`/`EntityInteract` fire only for entities a mod spawned with `spawn_entity`.
- Entity events run with a level-only frame (the host player's view of the same level), so level and entity services work there; services that need a player refuse.
- Names are case-sensitive: `server_tick` is an error.
- **One callback per event**: a second `lce.subscribe` for the same event replaces the first. Dispatch to several handlers yourself, as the generated ports do.
- A callback's result is read with `lua_toboolean`, so returning `0` cancels. Return `nil`/`false` to continue.
- API 1.2 fields exist only when the backend fills them (`LCE_CAP2_EV_CONTEXT`). Test for `nil`.

#### Pace mod work on the server thread

`lce.subscribe_tick(interval, callback[, offset])` sets the `ServerTick` callback for this mod. It runs when `e.tick % interval == offset`. The interval is an integer from 1 through 4,294,967,295. The offset defaults to 0 and must be smaller than the interval. Registration returns `true, 0` on success or `false, LCE_E_UNSUPPORTED` when the backend lacks `LCE_CAP_EV_TICK`. A later `lce.subscribe("ServerTick", ...)` replaces it with an every-tick callback.

```lua
local interval = lce.config("performance.refresh_interval", 20, 1, 1200)
local batch = lce.config("performance.refresh_batch", 4, 1, 64)
lce.subscribe_tick(interval, function(e)
    -- process_next_refresh is a mod-owned function that returns false when idle.
    for i = 1, batch do
        if not process_next_refresh(e.tick) then break end
    end
end, 1 % interval)
```

`lce.tick_stats()` reports `callbacks_run` (including attempts that raised errors) and `callbacks_skipped` (off-cadence or malformed tick deliveries). The counters belong to the mod's VM and accumulate across re-registration and world changes until the VM is closed. An off-cadence delivery skips script entry and payload table allocation. Check `type(lce.subscribe_tick) == "function"` if your package also targets older loaders.

The cadence follows the host's delivered server tick sequence, including across world changes. Missed ticks do not replay. Clear world-specific work on `WorldUnload` and follow the usual event-payload lifetime rules. Use a batch limit to bound how many jobs your mod starts per callback.

Native mods can use `lce_tick_pacing_due` from `include/lce_tick_pacing.h` with a regular `LCE_EV_SERVER_TICK` subscription. Check `LCE_CAP_EV_TICK` before registering. Pacing controls your mod's work. Client render timing, frame rates, chunk generation and global scheduling stay under their existing owners.

### 8.4 Runtime services (inside an event callback)

| Lua | C (`lce_api`) | Console status |
|---|---|---|
| `lce.send_chat([player,] text)` | `send_chat` | native `ClientboundChatPacket` path (static; console test T12) |
| `lce.give_item(player, key, n[, aux])` | `give_item` | native `Inventory::add 0x37d4d0`, the GiveItemCommand path; anything that does not fit is dropped at the player's feet (static) |
| `lce.set_block(level, x, y, z, key[, data])` | `set_block` | r72 `mc195_world_place_block_update` (console-proven Lucky helper), bound in `integration/r72_world_bind195.cpp` |
| `lce.get_block(level, x, y, z)` | `get_block` | native `getBlockState 0x3f2820` + encoder `0x91bc0` → `id \| meta << 12` (the r72 readback pair) |
| `lce.spawn_entity(level, key, x, y, z)` | `spawn_entity` | r72 `mc195_world_spawn_entity_ex`; the new entity gets the damage/death routes |
| `lce.drop_item(level, x, y, z, key[, n])` | `drop_item` | native `Block::popResource` (static) |
| `lce.add_effect(entity, {effect=, duration_ticks=, amplifier=})` | `add_effect` | native `MobEffectInstance` + `addEffect` (static) |
| `lce.play_sound(level, x, y, z, key)` | `play_sound` | `ServerLevel::playSound 0x3f3fe0` with a vanilla name or your sound clone (static) |
| — | `schedule(ticks, fn, user)` | server-tick scheduler (host-tested) |
| `lce.translate(key)` | `translate` | your `lang` table in the current game language, falling back to `en_US` |
| `lce.explode(level, x, y, z, radius[, fire[, destroy]])` (API 1.2) | `explode` | r72 `mc195_world_explode` (console-proven Lucky helper); every chunk within 2r+1 must be loaded |
| `lce.set_fire(entity, ticks)` (API 1.2) | `set_fire` | fire timer `+0x1c0` (r72 nbt_fire pins); `0` = native `clearFire`; never shortens |
| `lce.heal(entity, amount)` (API 1.2) | `heal` | native `getHealth`/`setHealth`/`getMaxHealth`/`isAlive` (r72 nbt_health pins); clamped to max |
| `lce.consume_held_item(player, n)` (API 1.2) | `consume_held_item` | native `setCount` + `Inventory::setChanged` (r72 nbt_item_count pins) |
| `lce.damage_entity(entity, amount[, attacker])` (API 1.2) | `damage_entity` | virtual `hurt` (+0x248) with `DamageSource::GENERIC` (static `0x1413b68`). API 1.3: `attacker` = the event's player builds `DamageSource::playerAttack` (`0x1f22a0`, an EntityDamageSource, so kills, aggro and knockback credit the player); other attackers are refused |
| `lce.damage_held_item(player, n)` (API 1.2) | `damage_held_item` | native `ItemInstance::hurt 0x3b8750` (Unbreaking applies); breaking shrinks the stack |
| `lce.teleport(entity, x, y, z)` (API 1.2) | `teleport` | virtual `teleportTo` (+0x670; players go through their connection) with position readback |
| `lce.set_motion(entity, dx, dy, dz)` (API 1.2) | `set_motion` | virtual `push` (+0x230) to the exact velocity; `hasImpulse` makes the tracker send it |
| `lce.spawn_particles(level, type_id, x, y, z[, count[, dx, dy, dz[, speed]]])` (API 1.3) | `spawn_particles` | `ServerLevel::sendParticles 0xcfd290` (ClientboundLevelParticlesPacket to players in range); `type_id` = ParticleType id (`+0x28`) |
| `lce.spawn_tinted_particles(level, rgb, x, y, z[, count[, spread]])` (API 1.12) | `spawn_tinted_particles` | Emits the stock BlockRemover particle with color `0xRRGGBB` to nearby players through the stock particle packet. Count accepts 1–1000 and defaults to 1. Spread accepts 0–64 and defaults to 0. |
| `lce.item_tag_get/set/remove(player, key[, value])` (API 1.4) | `item_tag_*` | the main-hand stack's CompoundTag (`+0x30`): real `get 0x1b7400` / `putInt` / `putDouble` / `putString` / `remove`, then `Inventory::setChanged` |
| `lce.register_channel(ch)` / `lce.send_payload(player_or_nil, ch, bytes)` + event `CustomPayload` (API 1.4) | `register_channel` / `send_payload` / `LCE_EV_CUSTOM_PAYLOAD` | `ClientboundCustomPayloadPacket` (ctor `0x18ba10`) server -> client; client -> server on the host player's listener `+0x340` |
| `lce.send_to_server(ch, bytes)` + event `ClientPayload` (API 1.6) | `send_to_server` / `LCE_EV_CLIENT_PAYLOAD` | client-side networking for remote PS4s running the mod: the server's `send_payload` arrives as `ClientPayload` (`ev.channel`, `ev.data`, no `ev.player`) on the client thread (world services are refused there); answer with `send_to_server` (immediate inside the handler, else sent within about 1 s; on the host it loops back to its own server as `CustomPayload`). Hooked through the packet factory map `0x142dcb0` (data only). Through a PC server, mod channels are relayed to the other PS4 clients (`server/mod-channels.patch`) |
| `lce.entity_nbt_get(entity, path)` (API 1.5) | `entity_nbt_get` | read-only: the entity's real save (`Entity::saveWithoutId 0x27bc70`) into a temporary CompoundTag that is deleted afterwards. `path`: `"Health"`, `"Pos[1]"`, `"Attributes[0]/Base"`, `"ActiveEffects[#]"` (list length). Scalars only |
| `lce.entity_data_get/set/remove(entity, key[, value])` (API 1.5) | `entity_data_*` | persistent mod data (Forge `getPersistentData`), saved and loaded **by the game** with the entity: stored as entity scoreboard tags `lce\|<key>=<value>` (the `Tags` list). Uses the game's own tag-set insert `0x2c5150` / erase `0x2c50f0`, exactly as `Entity::load` does. Values: int32, number (exact), ASCII string <= 200 chars. Never on players; at most 1024 tags per entity |

Every service:
- refuses outside the server-thread event frame that called you;
- refuses a player or level that is not the frame's own;
- caches nothing between callbacks.

Each API 1.2 service has a `LCE_CAP2_SVC_*` bit in `lce_api.capabilities2`.
For tinted particles, native mods check `lce_api.struct_size` and `LCE_CAP2_SVC_TINTED_PARTICLES` before calling the appended field. Lua mods can check for the binding before subscribing to events so the same package runs on older loaders. If the binding is present and the backend lacks the service, the call returns `LCE_E_UNSUPPORTED` (-4).

```lua
local spawn_tinted_particles = lce.spawn_tinted_particles
if type(spawn_tinted_particles) == "function" then
    lce.subscribe("BlockBreak", function(ev)
        spawn_tinted_particles(ev.level, 0x1280ff, ev.x + 0.5, ev.y + 0.5, ev.z + 0.5)
    end)
end
```

### 8.5 Config files (API 1.8)

Settings that players can change without rebuilding the mod. This is the counterpart of Forge's `ForgeConfigSpec` and `Configuration`.

**Declare the settings in `mod.json`:**

```json
"config": {
  "general.ore_rate":    {"default": 8, "min": 1, "max": 64, "comment": "Ruby veins per chunk"},
  "general.speed":       {"default": 1.25, "min": 0.1},
  "general.enable_wand": {"default": true},
  "general.greeting":    "Hello",
  "general.ores":        {"default": ["minecraft:iron_ore", "minecraft:gold_ore"]}
}
```

- A bare value is the same as `{"default": value}`.
- Keys are dotted parts of `[A-Za-z0-9_-]`, below 64 bytes. At most 128 keys.
- The default is a boolean, an integer, a number, a string (at most 255 bytes) or a list of those.
- `min` and `max` apply to numbers only.

`lce_modpack.py build` writes the commented default file next to `mod.lcm`:

```toml
[general]
# Ruby veins per chunk
# Range: 1 ~ 64
ore_rate = 8
speed = 1.25
enable_wand = true
greeting = "Hello"
ores = ["minecraft:iron_ore", "minecraft:gold_ore"]
```

**Install it** as `/data/lce-mods/<id>/config`. Players edit this file and restart the game.

**Read it from Lua.** `lce.config` works in any phase, including while `main.lua` registers content, so a setting can switch content on or off:

```lua
local rate  = lce.config("general.ore_rate", 8, 1, 64)   -- integer default -> integer, clamped to the range
local speed = lce.config("general.speed", 1.0)           -- number
if lce.config("general.enable_wand", true) then          -- boolean
  lce.register_item{key = "mymod:wand", name = "Wand", texture = "mymod:wand"}
end
local hi  = lce.config("general.greeting", "Hi")         -- string
local raw = lce.config("general.ores")                   -- no default: the raw value or nil (lists are raw text)
```

**Read it from C** with `config_get`, `config_int(ctx, key, def, min, max)`, `config_double`, `config_string`. Pass `min > max` for no range. Check `api->struct_size` and `LCE_CAP2_CONFIG` first.

**File format.** The file is a subset of TOML, the format Forge 1.13+ uses:
- `[section]` headers and `key = value` lines; keys under a section become `section.key`.
- `#` starts a comment.
- Values: integers (also `0x` hex), numbers, `true` or `false` (read as 1 or 0), `"strings"` with `\"`, `\\`, `\n` and `\t` escapes, and `'literal'` strings.
- Anything else, such as an array, is kept as raw text.

**What the loader does with the file:**
- It reads the file once, before natives and scripts run. Then it keeps the values read-only, so any thread may read them.
- A missing file, a missing key, a wrong type or an out-of-range value gives the default. A broken file never drops the mod.
- It skips bad lines and logs them as `lce-mods: [warn] <id>: config: N line(s) skipped, first at line L`. Keys under a broken `[section` header are skipped too.
- The file is limited to 16 KiB and 128 values. A repeated key keeps its first value.
- `[[arrays of tables]]` are not supported.

**Ported mods.** `tools/lce_autoport.py` turns Forge config specs into this section:
- `ForgeConfigSpec`/`ModConfigSpec` builder chains: `comment`, `push`/`pop`, `define`, `defineInRange`, `defineEnum` (stored as the name) and `defineList`.
- 1.12 `Configuration.getInt`, `getBoolean`, `getFloat`, `getString`, `getStringList` and `get` (keys become `category.name`).
- Each ported value is also listed as `CFG["key"] = lce.config(...)` at the top of the generated `main.lua`.
- Not mapped: values with a computed default or name (reported), 1.12 `@Config` annotation classes, and Fabric config libraries.

---

## 9. Native mods (C/C++ API) — experimental

Native mods are **disabled on retail**. The loader has no PRX loader wired up (`load_native = NULL`), and unknown modules break the anticheat module allowlist. The API is stable and fully tested in-process, so behaviour mods can be written now and enabled later.

Header: `include/lce_mod_api.h` (C ABI, no C++ runtime needed).

```c
#include "lce_mod_api.h"

static uint8_t wand_px[16 * 16 * 4];   /* RGBA, copied by the loader */

static int on_use(void *user, lce_event ev, const void *payload) {
    const lce_ev_item_use *u = payload;   /* u->item_id, u->aux, u->player (opaque) */
    (void)user; (void)ev;
    return u->item_id == 0;               /* nonzero = ask to cancel (if supported) */
}

lce_status lce_mod_main(const lce_api *api, lce_mod_ctx *ctx) {
    if ((api->version >> 16) != LCE_MOD_API_MAJOR) return LCE_E_VERSION;

    lce_texture_desc t = {0};
    t.struct_size = sizeof t; t.key = "wandmod:i_wand"; t.width = t.height = 16; t.rgba = wand_px;
    if (api->register_texture(ctx, &t)) return LCE_E_INVALID;

    lce_item_desc it = {0};
    it.struct_size = sizeof it; it.key = "wandmod:wand"; it.name = "Wand"; it.texture = "wandmod:i_wand";
    if (api->register_item(ctx, &it)) return LCE_E_INVALID;

    if (api->capabilities & LCE_CAP_EV_ITEM_USE)
        api->subscribe(ctx, LCE_EV_ITEM_USE, on_use, NULL);
    return LCE_OK;
}
```

(This mirrors `native_main` in `tests/test_loader.cpp`, which runs in the test suite.)

### Lifecycle

| Phase | Your code may |
|---|---|
| `LCE_PHASE_INIT` | `lce_mod_main` runs once. Register anything, `subscribe`, `log`. `resolve_*` works for `minecraft:` keys only |
| `LCE_PHASE_REGISTER` | data packages replay here; registration from natives is closed (`LCE_E_PHASE`) |
| `LCE_PHASE_COMMIT` | loader only: validation, IDs, backend |
| `LCE_PHASE_RUNNING` | events arrive; `resolve_item`/`resolve_block` return numeric IDs for every live key |
| `LCE_PHASE_FAILED` | nothing was registered; no events |

### Functions (`lce_api`)

| Function | Notes |
|---|---|
| `log(ctx, level, msg)` | prefixed with your mod ID; truncated to about 180 bytes |
| `phase(ctx)` | current `lce_phase` |
| `register_texture/item/block/recipe/smelting/tab/string` | descriptors are copied (strings and pixels), so your buffers can be temporary. Returns `lce_status` |
| `register_entity`, `register_ore` | `LCE_E_UNSUPPORTED` in API 1.0 |
| `resolve_item(ctx, key)` / `resolve_block` | numeric ID or −1 |
| `subscribe(ctx, ev, fn, user)` | `LCE_E_UNSUPPORTED` if the backend lacks the capability; test `api->capabilities` first |
| `config_get` / `config_int` / `config_double` / `config_string` (API 1.8) | your `/data/lce-mods/<id>/config` (§8.5); any phase, any thread |

### Events

| Event | Payload | Thread |
|---|---|---|
| `LCE_EV_SERVER_TICK` | `lce_ev_tick` | server thread, 20 Hz |
| `LCE_EV_CLIENT_TICK` | `lce_ev_tick` | client thread |
| `LCE_EV_WORLD_LOAD/UNLOAD` | none | server thread |
| `LCE_EV_BLOCK_BREAK/PLACE` | `lce_ev_block` | server thread, cancellable |
| `LCE_EV_ITEM_USE` | `lce_ev_item_use` | server thread, cancellable |

Rules for callbacks:
- Never block or sleep.
- Never keep pointers from a payload past the callback.
- Do not call `register_*`.

### Forward and backward compatibility

- Always set `struct_size = sizeof(descriptor)`. A mod built against API 1.0 keeps working on later 1.x loaders.
- A newer mod's longer descriptor is read up to the fields the loader knows.
- Check `api->struct_size` before calling a function added in a later minor version.

---

## 10. Distributing

- **Package files:** `<id>/mod.lcm` (required), `<id>/mod.json` (optional, informational) and `<id>/config` (optional, the default settings of §8.5). This fits the signed content catalog's 8-files-per-item limit.
  - `config` is meant to be edited on the console. Not decided yet: whether a catalog update should overwrite a config file the player edited.
- **Signed catalog:** `lce-ps4-superplugin/tools/lce_content.py add mods <id> <version> "<name>" out/<id>/` (the main engineer publishes; the plugin verifies the Ed25519 catalog signature and every file's SHA-256).
  - Install root for the `mods` repo: `/data/lce-mods/<id>/`.
  - `lce_content.py install-ps4` currently supports only skin and texture packs; extending it is listed in INTEGRATION.md.
- **Versioning:** bump `version` on every release, and keep keys stable (§2).
