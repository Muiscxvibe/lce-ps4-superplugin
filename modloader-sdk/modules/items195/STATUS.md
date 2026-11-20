# items195: item kinds, recipes ABI 2, smelting, brewing for 1.95 (CUSA00265)

Image: `legacy-195-analysis.elf`, SHA-256 `c675e6ce…029f`. All addresses are RVAs. There are no 2.72 addresses.

The module only provides data-path constructors and publishers. It never installs a hook and never patches `.text`. Every native call goes through `lce195x_natives`, which `lce195x_natives_bind(&n, base)` fills on the console and fakes fill in tests.

All publishers return the same values:

| Return | Meaning |
|---|---|
| `1` | Done and post-verified |
| `0` | Refused before the first write or native allocation |
| `-1` | Partial or uncertain. Keep everything resident, never retry, report PARTIAL |

## Evidence levels

| Level | Meaning |
|---|---|
| **console-pattern** | The same native calls and offsets are already console-proven in Steel/r72, but not through this module |
| **unicorn** | The real retail function ran in `tests/test_unicorn_retail.py` |
| **pinned** | Bytes or RTTI were checked against the image in `tests/test_image_pins.py` |
| **host** | Fake-image test only (`tests/test_items195.cpp`) |
| **not possible** | With a data hook only; the reason is given |

Nothing in this module has run on a PS4.

## Coverage

| Feature | Status | RVAs / offsets | Evidence |
|---|---|---|---|
| Generic item | console-pattern (ingot) + host | ctor `0x354d80` (0xa0), vt `0x126e638` (56 slots), `+0x18` stack, `+0x28` pair `0x1f/3` (candidate mapping), icon `+0x70` via `0x67840` | pinned, host |
| Pickaxe / axe / shovel | console-pattern (Steel tools) + host | ctors `0x52bd50` / `0x3e3c0` / `0x60b060` (0xc8) → DiggerItem `0x1fcfd0`. Fields: `+0xc0` Tier*, `+0xb0` speed, `+0xb4` attack, `+0xb8` attack speed, `+0x1c` uses (`0x35ecd0`). vt `0x129e568` / `0x120b688` / `0x12be6c0` | pinned, host |
| Sword | console-pattern + host | ctor `0x6cefd0` (0xa8): `+0xa0` Tier*, `+0x9c` = damage + 4.0. vt `0x12de818` has **57** slots | pinned, host |
| Hoe | console-pattern (Steel) + host | Inline in Items::init `0x395d69..0x395dd6`: base ctor, vt `0x1267b68`, `+0xa0` Tier*, stack 1, `0x35ecd0(uses)`, `+0x9c` = damage + 1. Stock `+0x138` returns 0, so the enchantability override is installed | pinned, host |
| Tool tier | host + unicorn | Tier `{+0 level, +4 uses (0x35ee90), +8 speed (0x3aecc0), +0xc damage (0x35eea0), +0x10 enchant (0x3aece0)}`, one per item. Defaults are the Java wood/stone/iron/diamond/gold values. The stock Tier objects are runtime BSS (`0x1417820..`) | unicorn ran `0x1fd370` on a mod Tier |
| Tool mining speed / harvest level / durability / enchantability | host | All four come from the per-item Tier, read natively | as above |
| Repair item | host + unicorn | `Tier::getRepairItem 0x3aecf0` is an identity switch on the stock tiers and returns null for a mod Tier (unicorn). So slot `+0x148` isValidRepairItem(this, sp<II>&, sp<II>&) gets `lce195x_repair_dispatch`, which forwards to the class slot (`0x1fd380` / `0x6cf330` / `0x330b0` / `0x3af3e0`) | unicorn, host |
| Classification pair `+0x28` | pinned | type: sword 1, shovel 2, pickaxe 3, axe 4, hoe 5, armour 7..10. Material: wood 1, stone 2, iron 3, gold 4, diamond 5, leather 6, chain 7. Taken from Items::init stores (`0x394173`.., `0x39354a`.., `0x393682`.., `0x393416`..) | static |
| Armour | host; Steel armour was never enabled | ctor `0x32cd0(item, mat*, renderIndex, slot*)` (0xd0) writes `+0xa0` slot, `+0xa8` defence = `mat->prot[slot+8]`, `+0xac` toughness (float `mat+0x48`), `+0xb0` render, `+0xb8` mat, durability = `mat+0x28 × {13,15,16,11}[idx]` (`0xe72a00`). Slots: head `0x1415450`, chest `+8`, legs `+0x10`, feet `+0x18` (index 3..0). The private ArmorMaterial (0x50) borrows the iron sound (`0x14090f8`+0x40). Exact max_damage is applied after the ctor via `0x35ecd0`; enchantability comes from `mat+0x38` | pinned, host |
| Armour protection points | host | `+0xa8` is read by getDefaultAttributeModifiers `0x33880` → ARMOR attribute | static |
| **Armour layer texture** | **not possible** (data only) | Lookup `0xb040b0` reads `+0xb0`, indexes a 6-entry name table `0x145d7e0` {cloth, chain, iron, diamond, gold, turtle}, and builds `armor/<name>_<1\|2>`. Index ≥ 6 reads the cache map `0x145d8d0` (crash). Supported `armor_texture` values: NULL (iron), `minecraft:chainmail`, `iron`, `diamond`, `gold`. Leather is refused, because its dye overlay is gated on the stock leather pointer (`0xb03d4e`). A new texture needs the calls at `0xb03d36`/`0xb03eb8` redirected, plus a Textures name entry | static |
| Food | host + unicorn (fields) | FoodItem ctor `0x2d69d0(this, int nut, float sat, bool meat)` (0xb8), vt `0x12594d8` (**62** slots). Fields: `+0x9c` nutrition, `+0xa0` saturation, `+0xa5` canAlwaysEat, `+0xa6` fast (eat time 16 vs 32, `0x2f5cf0`), `+0xa8` MobEffectInstance*, `+0xb0` probability. setEatEffect `0x2f5ff0`. MEI ctor `0x491aa0` (0x18) | unicorn ran `0x2f5cf0`, `0x2f5ff0` |
| Food effects | host | One effect only: effect `0x2d7220` applies `+0xa8` with probability `+0xb0`. More than one, or an eat time other than 16/32, needs a `+0x1c0`/`+0xe8` override (refused). Stock effects resolve through `lce195x_stock_effect_id` + `MobEffect::byId 0x490d60` | pinned (name order) |
| Shaped recipe, count/aux result, aux-specific ingredients | host + unicorn (ctor) | ctor `0x6013d0` (0x40): `+0x1c` w, `+0x20` h, `+0x24` group, `+0x28` cells, `+0x30` result. Null cells become `[0x1416d98]`. `+0xc` classification, `+0x10` −1. ItemInstance `0x3b7720(this, Item*, count, aux)`. Matching: aux −1 is a wildcard, otherwise exact (`0x601bb4`) | unicorn, pinned |
| Shapeless recipe | host + unicorn (ctor) | RTTI `ShapelessRecipe` vt `0x12bbf58`, ctor `0x602e10(this[0x30], II* result, vector<II*>* ingredients, group)`: `+0x1c` group, `+0x20` result, `+0x28` vector (0x20 object, +8 begin/+0x10 end/+0x18 cap, grow `0x5d4130`). Stock adder `0x5ab2c0`. Matching `0x603110` uses the same aux rules | unicorn, pinned |
| Multiple recipes per output | host (opt-in `shared_output`) | The map at owner+8 is `unordered_map<int, vector<Recipe*>>`. operator[] `0x5ab110` is find-or-insert, and both stock adders append unconditionally (`0x5aaf13..0x5aaf6b`, `0x5ab7c2..0x5ab80d`). Crafting `getItemFor 0x5ab990` iterates list vector owner+0x50 (`0x5abe1a`), not the map. The r72 ABI 1 rule (bucket must be empty) still applies when `shared_output = 0` | pinned |
| Mods crafting tab | host | The tab registry is pointer membership (`modded_crafting_tabs_195/registry.c`) and handles any Recipe subclass. `lce195x_recipe_snapshot_all` returns the r72 list plus this module's list, and never a partial list | host |
| Smelting (multi-entry, XP, cooker mask) | host + unicorn (map insert) | Global `0x1416a80`, object 0xf8. Maps keyed by **ItemInstance pointer**: +0x00 result (`0x329640`), +0x50 XP float (`0x329820`), +0xa8 mask (`0x329a00`). Mask bits: furnace 1, blast 2, smoker 4, campfire 8 (`0x329ca0`). Lookups are linear scans (`0x3226f0`, `0x329e50`). New nodes go to the front and shadow stock entries. XP is paid by `0x32a530` | unicorn ran the real `0x329640` insert |
| **Fuel** | **experimental** | The opt-in composite backend delegates furnace attachment and lookup to `modules/fuel195`. The default build refuses custom fuel. | fuel195 host tests, image pins and composite startup tests |
| Brewing mixes (stock potions) | host | Vector `0x142e948` (+8/+0x10/+0x18, grow `0x583020`) of Mix {from Potion*, ItemInstancePredicate*, to Potion*} (0x18). Predicate {vt `0x12a62a8`, Item*, aux} (0x18), exactly as `0x562e30` builds it. `Potion::byId 0x55e560`. Potion ids 0..42 follow the order of `0x55e6d0` (pinned). A (from, reagent) pair that already exists is refused, because the first match wins | pinned, host |
| New potion types | **refused** | Registry `0x142e938` would accept one. But the name is a u32 string id (`0x565e10` → `0xaac9b0`), and Creative `0xa25790` only lists the static Potions pointers | static |
| New status effects | **refused** | Registry `0x14195c8` (MappedRegistry, ids 1..31) would accept one. But NBT and the packet carry a byte id, the name needs a string-table id, icons are stock frames 1..29, and clones of regen/poison/wither/hunger/instant effects do nothing (`applyEffectTick 0x490ed0` compares `this` with the static pointers) | static |
| New enchantments | **refused** | Registry `0x1415448` (explicit ids up to 92, growable). A clone of a stock subclass registered after `0x248fa0` would be listed by the table (`0x24d890`) and by Creative. But it needs a string-table name (`0x24aec0`), it shares the clone's behaviour, and the anvil code is unaudited | static |
| **Boat item (API 1.10)** | pinned + unicorn + host (needs the owner's header/package diff) | BoatItem vt `0x1219f18` (RTTI `8BoatItem`, 56 slots; differs from Item only at `+0x10/+0x18/+0x20/+0x38/+0x40`). ctor `0xd7640(this, int type)` = Item ctor `0x354d80`, vptr, `[+0x9c]` = type (`0xd7660`), `[+0x18]` = 1. Items::init types 0..5 at `0x399422`, `0x3a1f44..0x3a2424`. The use `0xd7c00` keeps `this` in rbx (`0xd7c1e`, spilled `0xd7e14`, reloaded `0xd8088`) and reads `[rbx+0x9c]` at `0xd80f9` to set the new Boat's type data. items195 builds `LCE_ITEM_FLAG_BOAT` generic items like throwables: Item ctor, verify the Item vptr, write `+0x9c`, stack `max_stack` or 1, private copy of the BoatItem table | `test_image_pins` (11 boat checks), `test_unicorn_retail` (real `0xd7640`, ctor stubbed: exactly 3 stores), `test_items195` boat block |
| FOIL | host-tested | The item's private `isFoil` slot uses the unconditional stock glint function from SimpleFoiledItem. | image pins and item publisher host test |
| HANDHELD on non-tools | host-tested | Sets the item flag that the stock `isHandEquipped` function reads. | image pins and item publisher host test |

## Phases and anticheat

| Publisher | Phase | Existing site | `.text`? |
|---|---|---|---|
| `lce195x_item_construct` | Held post-stock-items window, inside the `mc195_items_provider` collect() callback (before `lce195_native_items_publish`, `0x3af660`) | Item phase `0x477ccc` → `0x386070`. The shipping stack already owns this window | Nothing new; private vtables are data writes |
| `lce195x_recipe_providers_publish` | Pre-cache crafting owner callback, before `0x5a9ef1` → `0x5aa200` | `steel195_content_on_crafting_scoped` (r72 `content_process.cpp:119`) | Nothing new |
| `lce195x_furnace_publish` | After the ctor `0x325fd0` returns. The maps are only read by linear scans at smelt time, so any point on the startup thread before the first world load works | The furnace wrapper `0x325fa0` runs at `0x477cd1`, right after items. The existing Steel plan patches the call at `0x325fb6`. **Preferred:** publish from a later existing phase (the Creative group hook `0xa33027`, reached from `0xc800b7`), with the owner read from `[0x1416a80]`. That avoids a new patch | Data only, if run from an existing later phase. Otherwise the `0x325fb6` call patch must be one-shot and restored after it fires |
| `lce195x_brew_publish` | After PotionBrewing setup `0x562e30` (`0x47812f`), before world load | Same Creative hook phase (`0xc800b7` comes after `0x477c40`) | Data only |
| Effects / enchantments | — | Not implemented | — |

The `0x14xxxxx` globals read here (`0x1416a80`, `0x142e948`, `0x1415450..68`, `0x14090f8`) are data. Reading and writing them does not touch the hashed `.text` segment 0.

## Integration calls for the lead

1. **Environment** (once, after attestation):

   ```c
   static lce195x_env env;
   env.base = base;
   lce195x_natives_bind(&env.n, base);
   env.m = (lce195x_memory){ctx, read, write, held};
   ```

   `held` must return 1 only on the current phase's thread: the item window, the crafting callback, or the Creative/startup thread.

2. **Validation.**
   - `check_item`: `d->kind == LCE_ITEM_GENERIC ? <existing check> : lce195x_item_check(d, err, cap)`.
   - `check_ext`: `lce195x_check_ext(type, desc, err, cap)`.
   - `check_recipe`: drop the shapeless/count/aux refusals.
   - Capabilities: add `lce195x_capabilities()` (TOOLS, ARMOUR, FOOD, BREWING, SMELTING, RECIPES). Never add FUEL, EFFECTS or ENCHANTS.

3. **Items.** In backend195.cpp `prov_collect`, per plan item, replace the ingot construction with:

   ```c
   lce195x_item_spec s = {desc, id, icon, (uintptr_t)&lce195_sdk_name_dispatch,
       repair  = desc->repair_item ? env.n.item_by_id(plan->resolve(ctx, LCE_REF_ITEM, desc->repair_item)) : 0,
       effect  = desc->food_effect_count ? env.n.effect_by_id(lce195x_stock_effect_id(desc->food_effects[0].effect)) : 0};
   rc = lce195x_item_construct(&env, &records[i], &s);   /* 1 / 0 / -1 */
   ```

   - Catalog entry: `kind = lce195x_catalog_kind(desc)`, `max_stack = lce195x_catalog_max_stack(desc)`, `item = records[i].object`.
   - `prov_identity` → `lce195x_item_identity(&env, &records[i])`.
   - The name dispatcher keeps matching by object pointer.
   - Refuse a food item whose effect does not resolve (`effect == 0` → the constructor returns 0).

4. **Recipes.**
   - Register: `lce195x_recipe_provider_register("lce-sdk", b, fn)`, instead of the r72 seam.
   - In `fn(ctx, owner)`: convert each plan recipe with `lce195x_recipe_from_plan(&env, &plan->recipes[i], "lce-sdk", &defs[i])`, then call `lce195x_recipes_publish(&pub, defs, n)`.
   - Use a `static lce195x_recipe_publisher`, with `pub.env = &env` and `pub.owner = owner`.
   - At most 128 recipes per batch.
   - **One line** in r72 `super_content_steel_nbt_save_195/content_process.cpp` `steel195_content_on_crafting_scoped`, right after `(void)lce195_recipe_providers_publish(owner);`:

     ```c
     (void)lce195x_recipe_providers_publish(owner);
     ```

   - **One line** in r72 `additive_recipes_195/craft_ui_capture.cpp`: replace `lce195_recipe_snapshot(owner,recipes,categories,MC195_MOD_RECIPE_CAPACITY)` with `lce195x_recipe_snapshot_all(owner,recipes,categories,MC195_MOD_RECIPE_CAPACITY)`.
   - Once at boot, call `lce195x_bind_r72_snapshot(lce195_recipe_snapshot)`.

5. **Smelting.**
   - Convert with `lce195x_smelt_from_plan(&env, &plan->smelts[i], &defs[i])` (default mask: furnace).
   - Then call `lce195x_furnace_publish(&fp, defs, n)` with `fp.owner = *(uintptr_t*)(base + 0x1416a80)` (nonzero).
   - Run it in the phase described above.

6. **Brewing.** For each `LCE_EXT_BREW` ext entry, call `lce195x_brew_from_plan(&env, plan, &plan->ext[i], &b[k])`, then `lce195x_brew_publish(&bp, b, k)` in the same phase as smelting.

7. **Build.** Add `modules/items195/{items,recipes,furnace,effects,adapter}.cpp` to build-ps4.sh and the Makefile. `./ps4check.sh` proves they compile with those flags. The undefined symbols are only `mem*`, `str*` and `snprintf`.

## Integration (done)

The composite backend (`loader/retail195/backend195.cpp`, `composite195.cpp`) follows the calls above: every kind is built by `lce195x_item_construct` in `prov_collect`, recipes go through `lce195x_recipe_provider_register` + `lce195x_recipes_publish`, smelting and brewing are published in the late startup phase, and both one-line r72 changes are in `integration/shipping-plugin.patch`. One addition for events195: `lce195x_item_route_use()` writes the item-use thunk into slot `+0x40` of an item's private vtable (data only) and `lce195x_item_identity()` accepts it.

## Tests

`./test.sh` runs three suites:

| Suite | Checks | What it covers |
|---|---|---|
| `test_items195` | 302 | Host, ASan/UBSan: every kind, refusals before allocation, uncertainty, no retry, dispatchers, shaped/shapeless/shared buckets/snapshot merge, provider registry, furnace front insertion and postcheck, brewing with vector growth, plan adapters |
| `test_image_pins.py` | 81 | Image hash, RTTI behind every vtable, ctor field stores, slot values and counts, stock call sites, potion/effect name order |
| `test_unicorn_retail.py` | 17 | Real retail code: shaped/shapeless ctors and virtuals, Tier enchant read, getRepairItem on a mod tier, food duration and setEatEffect, FurnaceRecipes operator[] insert |

`./ps4check.sh` builds with the OpenOrbis flags into `build/ps4/items195.o`.

## Uncertain / unverified

- **Gameplay:** every behaviour on the console (mining, damage, equip and render, eating, crafting result, smelting, brewing).
- **Generic classification `0x1f/3`:** inherited from backend195; it is a candidate mapping.
- **Tier defaults:** the Java values. The stock 1.95 Tier objects could not be read statically (BSS).
- **Potion ids:** registration order is the assumed id order (counter `0x142e940` starts at 0). The names were pinned; the ids are inferred.
- **ItemInstancePredicate aux test:** not disassembled. The stock mixes use −1.
- **Armour material sound:** borrowed from iron. Unverified whether armour with a private material repairs or equips with the right sound in game.
- **Furnace from a later phase:** inferred safe, because all readers are linear scans at smelt time. A world loaded before the publish would not see the entries.
