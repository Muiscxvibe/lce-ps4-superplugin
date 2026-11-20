# Mod API status for Minecraft PS4 Edition 1.95 (CUSA00265)

Last updated: 2026-11-18 (composite retail195 backend; effects, potions, enchantments, biomes and custom audio added). This assessment covers what can be registered today, how reliable each mechanism is, and what still stops arbitrary mods from being ported.

All addresses are RVAs relative to the main image base (`eboot.bin`, observed base `0x400000`). Every RVA comes from the 1.95 image:

- Reconstructed ELF SHA-256 `c675e6ce601b6457a1c1466b0da3d9ccd7415f6b0db9f8e9da7b6e511816029f` (`minecraft-ps4-research/private-direct-legacy-capture/legacy-195-analysis.elf`).
- Live RX-segment SHA-256 `231a6bfbd4fde617ceb3a8c431b7e7860427c45b1fad657bf6355c8ec401c682`, confirmed on the console in `runtime-tests/mc195-probe-n4styrgo`.

**Do not reuse addresses from `registry-re/`, `game-api/`, `mod-api-recipes/`, `mod-api-worldgen/`, `tick_hook/` or `lua-runtime/ps4`.** Those were taken from the 2.72 "Legacy" image (`e1d0aadc…cc58`), which is a different build.

## How to read the evidence levels

| Level | Meaning |
|---|---|
| **Console** | A console run log shows the native result. Each one is cited. A native postcheck line is **not** proof of gameplay (crafting, use, save) unless that is stated. |
| **Unicorn** | The real retail code ran on the host in emulation against the pinned image. |
| **Host** | Only modelled-memory unit tests exist. |
| **Static** | Disassembly or analysis only. |
| **None** | Nothing exists. |

"SDK" in the tables below means what this SDK does with the mechanism today.

---

## 0. Coverage matrix (composite retail195 backend)

This section is authoritative for what the SDK does on the console today. The "SDK today" lines in §1 describe each mechanism's history; where they differ, §0 wins.

**Columns.**
- **Console-ready (pattern-proven):** the SDK path uses only native calls, offsets and hook kinds that the shipping stack (Steel/Lucky/F3) already ran on this console. The SDK path itself is host-tested and wired into the patch, but has **not** run on a PS4 yet (INTEGRATION.md §7 tests it).
- **Simulated / host-tested only:** wired and host-tested (fake image, often plus image pins or Unicorn runs of the real retail code), but the native calls have never run on any console in any project.
- **Not possible (why):** refused by the backend with that reason, so the owning mod drops instead of publishing something wrong.

Module test counts (all green, `make test`):

| Module | Checks |
|---|---|
| items195 | 302 + 81 image pins + 17 Unicorn |
| blocks195 | 404 (fake image built from the real retail segments) |
| entities195 | 262 + 104 image pins |
| worldgen195 | 4594 + 244 pins |
| events195 | 86 + 10 Unicorn + RVA audit |
| script | 58 |
| ui195 | 26 |
| **strings195** | 27 + 34 pins + 79 Unicorn |
| **effects195** | 127 + 39 pins + 53 Unicorn |
| **enchants195** | 99 + 54 pins + 86 Unicorn |
| **biomes195** | 137 + 83 pins + 38 Unicorn |
| **audio195** | 71 + 41 pins + 19 Unicorn |
| netsync195 | ~11,100 |
| server | 1104 |
| composite end-to-end | 8 scenarios, including `newretail`: effects with the real publisher, food with a mod effect, EFFECT_TICK reaching Lua, potions, brewing chains through mod potions, enchantments, a biome and custom audio in console phase order |

| Content | Console-ready (pattern-proven) | Simulated / host-tested only | Not possible + why | Evidence, RVAs |
|---|---|---|---|---|
| **Items** (generic) | yes: ingot pattern `alloc 0xa0` → `0x354d80`, icon wstring `+0x70`, private vtable with `+0x108` names, catalog → `0x3af660`, Mods Creative group | — | — | Console: Steel `mc195-steel-nbt-live-bx8w0hrq` (6 items, names, icons, Creative). Host: `test_retail195`, `test_retail195_bridge` (real r72 window/catalog), `test_composite195` |
| **Blocks** | — | OreBlock clone (`0x503a80` / `Block(Material*) 0x91fb0` + vptr `0x1298cf0`), setters `0x92a00/0x92580/0x92540/0x92560`, register through the registry **data hook** (vptr swap, relay at `+0x28`, fires after id 365 at `0xb74d8`), BlockItem `0x7d1d0` + map `0x3af480`, names `+0x380`, drops `+0x240/+0x360`, fire odds `0x1475c0`, block-break/place event routes; **API 1.7**: 2..16 metadata variants (ColoredBlock state slots `0x1a9950/0x1a9850/0x1a9900/0x1a97b0`, MultiBlockItem `+0x58` `0x49f950`, per-variant sprites through a `+0x458` dispatcher), one-box shapes (`+0x178` AABB from `0x170`, face shapes `+0x1a0`), transparent (GlassBlock slots) and no-collide (`+0x1c8` `0x139170`); **terrain textures (gap G1, 2026-11-19)**: the shipping terrain owner `shared_terrain_195/runtime.cpp` appends the SDK tiles after its two fixed Lucky/Steel tiles (cells 2.., up to 62 tiles at 16/32 px, 30 at 64 px) and calls `b195_terrain_bind` (sprite `0xb8`, vtable `0x13451d0`, bind `0xd53b40`, CAS `+0x1a0` / `+0x1b0..` from the atlas fallback `+0xe8`) inside its reload lease with its own held/uploaded attestations; block items and variant sprites use the same sprites | block storage beyond 9-slot dispensers and 27-slot chests; facing/axis placement (stairs/slab halves are chosen as variants, not by the placing player); replaceable; fences/panes that **connect** to neighbours (static multi-box shapes work: API 1.11 below); harvest tier > wood (hard-coded per block); no Creative entry in SDK-only mode (the mapped Creative window is Lucky-owned) | blocks195 STATUS (Unicorn for the Lucky/Steel clone pattern, never accepted on console); `test_blocks195` on a fake image built from the real retail segments |
| **Block entities** | — | **storage blocks, 9 slots (blocks195)**: a BaseEntityBlock-shaped mod block (0x1f0 object, EntityBlock vptr `+0x1e8`, `hasBlockEntity` `+0x14a`, module `__si_class_type_info` deriving from BaseEntityBlock `0x12126c0`) whose EntityBlock `newBlockEntity` is `DispenserBlock 0x208ef0`, with the dispenser's use `0x2085a0`, onRemove `0x209570` (drops), triggerEvent/playerDestroy and comparator slots; BLOCK_USE still routed first; **27 slots**: the mod block is a real `ChestBlock(0)` (`0x173060`, ChestBlockEntity via `0x175800`, chest screen through ServerPlayer::openContainer `0xd07d20` -> ChestMenu `0x17a750`) with Block's cube render/shape slots restored; **54** = two adjacent identical 27-slot blocks (stock pairing `0x178a40` requires the same Block object, CompoundContainer -> 6-row ChestMenu); **title** = the block's display name via a private EntityBlock `newBlockEntity` wrapper that calls setCustomName `0x597b30` (openContainer sends InteractionObject `+0x30` `0x598460`, the `[be+0x68]` name record); **no chest model**: a private ChestBlockEntity vtable whose `+0x70` view distance is -1 so the BE pass `0xb2b9d0` culls it | sizes other than 9/27/54; console run (title and model cull are static + host); a BE re-created from NBT without passing newBlockEntity keeps the stock vtable (the name survives through `CustomName`) | blocks195 STATUS §4; `test_blocks195` (real DispenserBlock/RTTI bytes, Itanium `dynamic_cast` on the built layout) |
| **Tools** (pickaxe/axe/shovel/hoe) | class ctors `0x52bd50`/`0x3e3c0`/`0x60b060`, hoe inline (`0x395d69..`), vtables `0x129e568`/`0x120b688`/`0x12be6c0`/`0x1267b68`: the same classes Steel registered on the console | per-item `Tier` object (`+0xc0`), mining speed, durability `0x35ecd0`, enchantability override `+0x138`, repair override `+0x148` | — | Console: Steel tools registered (use/mining **not** tested). Unicorn: `0x1fd370` on a mod Tier, `getRepairItem 0x3aecf0` |
| **Weapons** (sword) | ctor `0x6cefd0`, vtable `0x12de818` (57 slots): the Steel sword class | per-item Tier damage `+0x9c`; optional FOIL uses the stock glint function via the private item vtable | — | as above |
| **Armour** | — | ctor `0x32cd0` with a private `ArmorMaterial` (protection per slot, durability, enchantability, iron sound), slots `0x1415450..` | **custom layer textures**: the lookup `0xb040b0` indexes a fixed 6-name table `0x145d7e0` by render index (index ≥ 6 reads a cache map and crashes); only `minecraft:chainmail/iron/diamond/gold` (or none = iron) layers. Leather: dye overlay gated on the stock leather pointer | items195 STATUS; Steel armour was never enabled |
| **Food** | — | `FoodItem 0x2d69d0` (`+0x9c` hunger, `+0xa0` saturation, `+0xa5` always-edible, `+0xa6` fast), one eat effect via `MobEffectInstance 0x491aa0` + `setEatEffect 0x2f5ff0`: a stock effect **or a mod effect** (effects195 publishes in the item window before the food is constructed) | 2+ eat effects (needs an `addEatEffect +0x1c0` override); eat time other than 16/32 | Unicorn: `0x2f5cf0`, `0x2f5ff0`; `test_composite195` [registries]: food MEI -> mod effect object |
| **Status effects** | — | **new effects (effects195)**: `0x490db0` ctor (0x80 objects) + private vtable whose tick/instant/isDurationEffectTick slots forward to the retail functions with `this` = the `clone_of` stock pointer (regeneration, poison, wither, hunger, health boost, absorption, saturation, instant health/damage really work); attribute effects (speed, slowness, haste, mining fatigue, strength, weakness, luck) copy the stock modifiers through the real `addAttributeModifier 0x4917d0`; ids 32..127 from the ledger via the registry `0x14195c8` `+0x28` (byte id in NBT/packets is zero-extended); count global `0x14195d0` raised for the HUD array; name = strings195 sid at `+0x58`; icon = a stock frame (`+0x5c`); colour = nearest `eMinecraftColour` effect enum; `LCE_EV_EFFECT_TICK` (+ Lua `EffectTick`) every 20 ticks without `clone_of` | clones of identity-checked effects (night vision, invisibility, jump boost, resistance, fire resistance, water breathing, blindness, nausea, glowing, levitation, slow falling, conduit power, dolphins grace, bad omen, hero of the village: game code tests `hasEffect(MobEffects::X)` by pointer); new icon **art** (icons are frames of the `fourj.MobEffect` SWF); exact RGB (colour-table enum) | effects195 STATUS: host 127, pins 39, **Unicorn 53** (real registry ctor + initializers, real byId on 32..64, real `MobEffectInstance::tick 0x491bc0` -> thunk -> real `applyEffectTick 0x490ed0` healed at durations 100/50, NBT save/load of ids 32/64); `test_composite195` [registries] (real publisher, EFFECT_TICK -> Lua) |
| **Potions** | — | **new potion types (effects195)**: 0x38 objects registered at the counter `0x142e940` (43+) in the DefaultedMappedRegistry `0x142e938`; items store the key in NBT `"Potion"` (`setPotion 0x562cd0`), so potion, **splash, lingering and tipped arrow** are the same object; four name sids (`0x55e620` picks +0/+4/+8/+0xc by item kind: "Potion of X", "Splash Potion of X", "Lingering Potion of X", "Arrow of X", each overridable in `lang`); effects = stock or mod MobEffectInstances; brewing into and out of mod potions | Creative entries for mod potions (stock Creative uses `MenuBuilder::POTION` builders, vt `0x1328e30`; mod potions are obtained by brewing) | effects195 **Unicorn**: real byId/`get` by key for ids 43/44, unknown key -> water, real `Potion::getName 0x55e620` reads the 4 mod sids, `getKey` round trip; `test_composite195` [registries]: awkward -> mod potion -> stronger mod potion mixes |
| **Enchantments** | — | **new enchantments (enchants195)**: stock class ctors (base `0x24ab10`, Damage `0x1f1a40`, Protection `0x58d970`, Impaler `0x6f6b30`) + a game-heap private vtable that points only at **retail** functions (maxLevel -> a pinned `mov $N,%eax; ret`, getDescriptionId -> `0x24aeb0`, treasure/curse -> `0x24ae30`/`0x24b090`); registered at ids 100..1023 (ledger) through the MappedRegistry `[0x1415448]` `+0x28` in the late phase (the stock bootstrap `0x248fa0` runs at `0x478134`); 1.95 keeps **no cached valid-enchantment list**: the table `0x24d890` and Creative books read the registry list live; name = strings195 sid at `+0x10`; levels I..X; category from `applies_to`; `clone_of` none (pure data/script enchantment) or a virtual-behaviour class | clone_of thorns and every identity-read enchantment (efficiency, fortune, looting, silk touch, unbreaking, mending, frost walker, depth strider, ...): 1.95 reads them through the static `Enchantments::X` pointer; fire/blast protection clones give damage reduction only | enchants195 STATUS: host 99, pins 54, **Unicorn 86** (real registry + 36-entry bootstrap, real ctors/register, damage bonus and costs equal stock sharpness, table lists them, real `getFullname` = "Gem Edge VII") |
| **Smelting** | — | `FurnaceRecipes [0x1416a80]` maps `+0x00/+0x50/+0xa8` via `0x329640/0x329820/0x329a00`, XP, cooker mask; published at the title (late phase), all readers are linear scans at smelt time | two recipes for one input (the first wins) | Unicorn ran the real `0x329640` insert; Steel furnace never deployed |
| **Shapeless recipes** | crafting-tab registry (pointer membership) and owner list `+0x50` are console-proven for shaped | `ShapelessRecipe 0x602e10` (vt `0x12bbf58`), ABI-2 publisher | — | Unicorn: ctor + virtuals |
| **Counts / data values** | shaped ctor `0x6013d0` + `ItemInstance 0x3b7720` (console postcheck `STEEL_CONTENT recipe=2`; no confirmed craft) | result count 1..64, result aux, aux-exact ingredients (`0x601bb4`), several recipes per output (bucket `0x5ab110` is find-or-insert) | — | items195 recipes row; `test_composite195` (wool@14, count 4) |
| **Brewing** | — | Mix vector `0x142e948` append (Mix {from, predicate vt `0x12a62a8`, to}), stock **and mod** potions (resolved after the potions phase), published at the title | — | items195 pins (potion order); host; `test_composite195` [registries] |
| **Fuel** | — | — | Experimental build only (`LCE_EXPERIMENTAL_FUEL=1`). Item `fuel_ticks` and `register_fuel` share a 64-item limit and use a late startup attachment to the furnace factories and fuel slot. Hopper and Quick Move behavior remains unverified. | fuel195 host and retail-image tests, composite startup and failure tests; PS4 direct placement and save/reload verified |
| **Entities: types** | — | clone of 56 vanilla mobs: `0x28d9c0` after `0x28dc10`, mask `base \| (k+1)<<34`, private vtable `+0x60` GetType, factory thunks, save/load by key, published at the title before any Level | ender dragon, wither, giant, endermite/pillager/ravager, armor stand, non-living | entities195 STATUS; 104 pins |
| **Projectiles (thrown items, API 1.9)** | `"throwable"` on a generic item | **wired (composite diff staged with the loader owner, 2026-11-19)**: items195 builds a throwable as a **SnowballItem** (private copy of `0x12cae00`, Item ctor `0x354d80`, `+0x40` = `p195_item_use` -> stock use `0x637970`; the events195 ITEM_USE route goes over it); after the items, one RELRO word, Snowball vt `0x12ca530+0x860` shoot, swaps the new Snowball's vptr to a per-kind copy and applies the kind's speed/inaccuracy; its `+0x880` onHit wrapper reports server hits; the composite applies damage/fire/effects/explosion through services195 inside an events195 level frame and dispatches `LCE_EV_PROJECTILE_HIT` (Lua `ProjectileHit`), then stock `0x637490` | custom projectile models (renders as a snowball; per-type renderer); a projectile saved in flight reloads as a snowball; remote clients see plain snowballs; 16 kinds | projectiles195 STATUS: host 63, image pins 34; composite scenario "API 1.9 throwables" (46); test_loader §O; test_package ITEM3; porter `ThrowableTests` |
| **Multi-box blocks (API 1.11)** | `"boxes"`, `"variant_boxes"`, `"rotate_boxes"` | **module live, header/package/loader diff with the loader owner (2026-11-19)**: blocks195 gives the block render shape 10 (StairBlock `+0xe8` `0x678ad0`), a `+0x190` list dispatcher returning module-owned `{AABB **data; u32 count}` lists (per variant through ColoredBlock `+0x50` `0x1a9900`), StairBlock `+0x1b0` `0x6765c0` for collision, `+0x178` = the union. The tesselator switch `0xb322d1` case 10 calls `0xb3b160`, which draws every list box through `0xb5be10`; Block `+0x188` `0x92c90` culls faces against the list | connected fences/panes, stairs that pick their facing from the player (variants instead), custom models | blocks195 install pins (StairBlock slots/code, `0xb3b193`, the case-10 call); Unicorn `test_unicorn_boxes` (real `0x6765c0`, `0xb3b160`, `0x92c90` over a module-style list); host `test_blocks195` boxes; `test_loader` §Q (with the diff). Console: inventory/hand rendering of a shape-10 block and the Creative variant entries are unverified |
| **Boat items (API 1.10)** | `"boat"` on a generic item | **module live, header/package/loader diff with the loader owner (2026-11-19)**: items195 builds the item as a **BoatItem** (private copy of `0x1219f18`, 56 slots; Item ctor `0x354d80`, then `[+0x9c]` = type, `[+0x18]` = 1, exactly what `BoatItem(int)` `0xd7640` does); the stock use `0xd7c00` reads `[this+0x9c]` (`0xd80f9`) and sets the boat's type data. events195 accepts `0xd7c00` as an ITEM_USE original | custom boat models/speeds (the Boat renderer and physics are per type, constants in code), new wood types, chest boats, minecarts (MinecartItem `0x476240` keeps a type pointer at `+0xa0` past the 0xa0 Item and registers a dispenser behaviour). The placed boat drops the **stock** boat item | items195 pins (RTTI, 56 slots, ctor stores, `+0x9c` read, Items::init types 0..5 at `0x399422..0x3a2424`) + Unicorn (real ctor `0xd7640` == Item ctor + 3 stores); events195 audit (`BoatItem+0x40`); host `test_items195`, `test_loader` §P, `test_composite195` [API 1.10 boat items], `test_package` (with the diff) |
| **Entities: models** | — | the base mob's renderer is shared: renderer map entry `0xbd5220` on `*(Minecraft+0x220)` (Minecraft* = `[0x1464780]`). Hard rule: types without renderers block world entry | new models/geometry | `getRenderer 0xbd54c0` does an unchecked find |
| **Entities: textures** | — | — | custom skins: the memory-texture path (`MobSkinMemTextureProcessor 0x133c368`, candidate `0xd63940`) is unverified; refused (no silent fallback) | entities195 STATUS |
| **Entities: AI** | — | extra goals on top of the base AI: wander `0x77f550`, look `0x43a0c0`, melee `0x462f20` (Monster), target `0x4a7970`, avoid `0x3d290`, breed `0xe2950` (Animal), float/swim `0x2e9180` | replacing base goals (raw pointers in subclasses); panic (inlined), flee-sun (unanalysed), tempt (by-value item set) | addGoal `0x2d6260` |
| **Entities: spawning** | — | biome spawn lists (MobSpawnerData `0x1214758`, lists `+0x78/+0x98/+0x118/+0x178`) | — | host |
| **Entities: drops** | — | — | loot is data-driven: `+0xd80` returns a native loot-table `ResourceLocation*`; no loot-table registry hook | entities195 STATUS |
| **Entities: eggs** | — | spawn egg with the base mob's colours (`0x28cba0` → egg map `0x1415e78`) | custom RGB colours (colours are `eMinecraftColour` enums) | host |
| **Biomes** | — | **new biomes (biomes195)**: the retail `PlainsBiome` ctor `0x53d140` builds the object and itself fills `Biome::biomes[id]` (`0x1409fc0`) at free ids 168..254 then 50..126 (ledger); generation: the `BiomeInitLayer::getArea` slot (`0x12163b8+0x20`, RELRO data, **outside segment 0**) points at a hook that appends the mod biomes to each layer instance's 4 climate pools on first use and calls the original `0x8a290`, deterministic per seed, spawn chunks included; top/filler blocks, temperature/rainfall, height base/variation; grass/foliage/water/sky RGB via a private biome vtable; worldgen195 decorates mod biomes and features/spawns may name them; a ledger placeholder keeps a removed mod's biome id loadable | nether/end biomes (single fixed biome); hills/edge/shore/river/mutated variants of a mod biome; negative depth (oceans) | biomes195 STATUS: host 137, pins 83, **Unicorn 38** (real layer ctor + real `getArea` returns the mod id for ~2/8 of medium cells, stable per seed; real `ZoomLayer::getArea 0x7acfc0` reaches the hook through the patched slot; real `PlainsBiome` ctor at id 170) |
| **Ores / features** | `OreFeature::place 0x503f60` with the exact object layout (Unicorn, steel fixture) | Biome decorate **data hook** (private vtable per class, `+0x58`, vptr CAS of 72 biomes), private Java Random per chunk, ores/trees `0x6f5b30`/lakes `0x3db8d0`, overworld + End, published at the title | flowers (need a FlowerBlock); Nether (`0x4b8090` never calls decorate); retro-generation of old chunks | worldgen195 STATUS; 244 pins; `test_worldgen195` 4594 checks |
| **Structures** | — | templates ≤ 16×255×16 written with `setTileAndData 0x3efa80` (flags 2), clipped to the origin chunk | larger templates (cross-chunk writes) | host |
| **Dimensions** | — | — | dimensions are fixed classes with their own level sources and numeric ids wired into level/save/portal code; no registry | worldgen195 STATUS |
| **Sounds** | — | clones of a vanilla `SoundEvent` (523 names) with volume/pitch; **custom audio (audio195)**: real `SoundEvent`s (ids 523+, `0x6672c0`), the package's 16-bit PCM played through the game's Miles driver (`AIL_*` wrappers `0xe1f970`..`0xe22110`) from a private `SoundEngine` vtable (`+0x28` play / `+0x38` playUI; everything else forwarded), distance fade + pan + sound-volume/mute; `play_sound` by mod key | Ogg in the plugin (convert at pack time); music/record streams; mob sound slots naming mod sounds | audio195 STATUS: host 71, pins 41, **Unicorn 19** (real `AIL_WAV_info` on the built WAV, real wstring assign + `SoundEvent::register`) |
| **Particles** | — | Vanilla particle IDs through `spawn_particles`. The API 1.12 `spawn_tinted_particles` service emits the stock BlockRemover particle with RGB through the existing packet. Registered clone descriptors remain metadata. | New particle textures require a particle atlas integration. | events195 STATUS, ui195 STATUS |
| **Localization** | item display names through the private `+0x108` dispatcher (console-proven by Steel names) | **mod string ids (strings195)**: (sid -> UTF-16) inserted into every language map of the live Localizer (`[0x1440530]`, lookup `0xd39880`) with the game's own loader sequence (wstring `0x67840`, `emplace_front 0xd5a9e0`, `_Hash::_Insert 0x7ed670`), sids `0x4C430000..0x4C43FFFF` (none of the 4888 stock ids); re-ensured every title tick (the Localizer is rebuilt on a language change by `0xafc510`); used by effect, potion and enchantment names in `en_US` + every package locale; language selection `fr_CA→fr_FR→en_US` (`translate`); block names `+0x380` | translated Creative/catalog text (the shared catalog takes ASCII only) | strings195 STATUS: host 27, pins 34, **Unicorn 79** (real language-node ctor, real inserts with rehashes, real lookups) |
| **Creative tabs** | the private **Mods** Creative group (`0xa33027`, `0xa336a0`) for items | paging model for > 50 items (ui195, compositor side not wired) | several distinct tabs or stock-tab placement (project rule: stock tabs untouched; gap G6) | Console: Steel Creative |
| **Menus / screens** | — | — | 1.95 registers no UIScene at runtime (the scene map `0xe8af4c` is 2.72); screens can only be drawn by the plugin compositor | ui195 STATUS |
| **HUD** | — | overlay model (8 lines, anchor, colour) | compositor draw is not wired | ui195 |
| **Commands** | — | `/name` parsing, `/help`, replies via `send_chat`, host-typed submit queue | **no console producer yet**: data/script mods cannot declare commands (native-only descriptor, native mods disabled), and the chat-intake hook (`+0x300` listener slot, static candidate) is not patched | events195 |
| **Keybinds** | — | pad chords that avoid the debug menu, delivered on the server tick | same: native-only descriptor; the `scePadRead` filter call (`lce_sdk_pad_filter`) is exported but not patched into `debug_pad_sessions_195` | events195 |
| **Events: tick** | Mc195SharedScope subscriber (`0xc859e8`, per-player `0xce4380`): the scope F3/Lucky run on the console | Native tick pacing helper and Lua paced subscriptions with mod-local callback counters | Client frame telemetry and global budgets require separate owner support | Composite test exercises packaged Lua scheduling through `lce195_ev_scope_tick`. |
| **Events: block break / place** | break: private block vtable `+0x3e0` (Lucky routes the same slot on console) | place: BlockItem `+0x28` useOn `0x7d4c0` | events for vanilla blocks | events195 |
| **Events: use item** | private item vtable `+0x40` → `Item::use 0x3aedb0` (Lucky use hook pattern) for generic items | — | tools/armour/food classes (their `+0x40` is not the stock `Item::use`; not routed) | `test_composite195` |
| **Events: block use (API 1.2)** | — | private block vtable `+0x2c0` (stock `Block::use 0x940b0`). Calling convention checked on the real `CraftingTableBlock::use` in Unicorn. Cancellable (consumes the click) | vanilla blocks | events195 |
| **Events: entity interact / entity tick (API 1.2)** | — | mod-spawned entities: private vtable copy `+0xe50` interact (Cow `0x1d21c0` milking path), `+0xa8` tick (`Entity::tick 0x275680` → baseTick `+0xb0`), with a level-only frame | players, vanilla entities | events195 |
| **Events: block tick (API 1.2)** | — | Block `+0x1f0` tick routed; random-tick gate found (tickBlocks `0xcf61f0` calls tick only when Block `+0x140` isTicking = byte `+0x149`, set by `setTicking +0x128`); `lce195_ev_set_block_random_ticking` | composite install + a random-tick block flag in the header | events195 STATUS |
| **Events: inventory tick / neighbour / step-on / entity inside (API 1.3)** | — | Item `+0xc8`, Block `+0x208`, `+0x2c8`, `+0x328` routed (stock `ret` bodies pinned; MapItem/Tnt/Note/Magma/Cactus ABIs), host + Unicorn | composite install; enum ids provisional | events195 STATUS |
| **Services: explode / set_block / spawn (API 1.2 binding)** | r72 Lucky world helpers (console-proven), bound by `integration/r72_world_bind195.cpp` | `get_block` (native reader pair) | `give_item` (needs a pad ticket) | events195 |
| **Services: heal / set_fire / consume / damage_held_item / damage_entity / teleport / set_motion / give_item (API 1.2)** | — | natives pinned from the r72 nbt_* modules or traced from vanilla callers (GiveItemCommand → `Inventory::add`, Cactus/kill → DamageSource statics, `ServerPlayer::teleportTo +0x670`, `Entity::push +0x230`); `tests/audit.py`, host and Unicorn tests | non-player attackers (a mob's `shared_ptr` control block) | events195 |
| **Client-side networking (API 1.6)** | — | remote PS4 clients: packet factory map `0x142dcb0` entries for ids 250/4 redirected (data only) so received payload packets get a private vtable (`handle +0x28`); `LCE_EV_CLIENT_PAYLOAD`, `send_to_server` (`ServerboundCustomPayloadPacket 0x5fbd70` + `ClientPacketListener::send 0xb91460`), SetTime pump, host loopback; remote players' server-side payloads are now framed. PC server relay patch (`server/mod-channels.patch`). Host + Unicorn (real create -> handle -> listener) | console run; loader call to `lce195_net_client_install` and the header entries | events195 STATUS, server STATUS |
| **Services: entity NBT (API 1.5)** | — | `entity_nbt_get` (real `saveWithoutId 0x27bc70` into a temporary CompoundTag, paths with list indices) and `entity_data_get/set/remove` (persistent mod data as entity tags, the game's tag-set insert/erase as in `Entity::load`); host + **Unicorn** (real save writes the mod tag into `Tags`) | writes to vanilla keys (the tag is a copy); data on players | events195 STATUS |
| **Events: entity damage / death** | — | private vtable copy on mod-spawned entities, `+0x248` hurt (cancellable), `+0xa00` die | players and vanilla entities | events195 |
| **Events: player join / leave** | scope-derived (first retained tick with a readable UUID), console pattern | — | — | events195 |
| **Events: chat** | — | host-typed submit queue, cancellable | remote players' chat (listener `+0x300` hook not installed) | events195 |
| **Scripting** | — | Lua 5.4.8 sandbox per mod (4 MiB, 20 M instructions, uncatchable budget), registration during REGISTER, events through the sink | `io`/`os`/`debug`/`package`, bytecode | script 49 checks; only the 2.72 console ever ran this VM |
| **Config files (API 1.8)** | — | `/data/lce-mods/<id>/config` (TOML subset, `loader/config_store.h`) read by the loader in DISCOVER through the host `read_file` that already reads `mod.lcm`; `config_get/int/double/string` + Lua `lce.config`; no game memory involved. `lce_modpack.py` writes the default file from `mod.json` `"config"`; the porter maps ForgeConfigSpec/ModConfigSpec builders and 1.12 `Configuration` calls. Host: `test_config` (parser, fuzz, packer round trip), `test_loader` §N, script §2f, `ConfigPortTests` | catalog policy for user-edited files on update; 1.12 `@Config` annotation classes; Fabric config libraries | MODDING §8.5 |
| **Multiplayer sync** | — | `LCE\|ModIds` handshake: client (netsync195, linked into `lcemodloader.o`, hooks in the patch) and PC server (`server/server-sync.patch`); world ledger, placeholders, per-connection gate | custom content on a server **without** the server patch (the PC server drops unknown IDs) | netsync195/server STATUS |

**Reading this table for a port.** A mod made only of console-ready rows is the one to test first (INTEGRATION.md §7 T1–T4). Everything in the middle column publishes from the same build and has a dedicated console test (T5–T14); report each one before relying on it.

---

## 1. Registration surfaces

### 1.1 Items — Console (registration, names, icons, Creative); gameplay unverified

| Mechanism | RVA / offset | Evidence |
|---|---|---|
| Item registry receiver | `0x1417638` | Console |
| `Item::items` vector header `{begin,end,cap}` | `0x1417720`. Live 2268 slots, capacity 2398, 547 occupied | Console (`mc195-probe-zpf5w_3j`, `ORE_PROBE item_count=2268 item_capacity=2398`) |
| Register `(reg,id,key,item)`. **Overwrites an occupied slot** | `0x3af660` | Console (Steel) |
| Lookup by ID / key present / item by key (fatal if absent) / ID of item | `0x385af0` / `0x385d00` / `0x385e20` / `0x385940` | Console |
| ID remap trap: 1024..2254 maps to −768 | `0x385a60` | Static |
| Base Item: ctor, vtable (56 slots), size `0xa0` | ctor `0x354d80`, vtable `0x126e638` | Console (Steel ingot) |
| Item fields | `+0x18` u32 maxStack, `+0x1c` u32 maxDamage, `+0x20` sprite, `+0x28` classification pair, `+0x70` icon wstring | Console |
| Name virtual `(out wstring*, item, stack)` | slot `+0x108` → `0x3af070` | Console (private vtable names) |
| Use / enchantability virtuals | `+0x40` → `0x3aedb0`; `+0x138` → `0x3af3c0` | Console (Lucky use hook) |
| Tool vtables | pickaxe `0x129e568`; sword `0x12de818` (ctor `0x6cefd0`); axe `0x120b688` (`0x3e3c0`); shovel `0x12be6c0` (`0x60b060`); hoe `0x1267b68`; armour `0x12095f8` (`0x32cd0`) | Console for six Steel items; armour gated |
| Startup item-phase hook (`.text` call patch) | `0x477cc8` → `0x386070` | Console |

**Proven on console.** In `runtime-tests/mc195-steel-nbt-live-bx8w0hrq`, six Steel items registered with names, icons and Creative entries. The log reads `STEEL_TOOLS prepare=3 registry=2 names=2 recipes=2 creative=3 count=6 icons=6`. Use, crafting and save/reload were **not** tested.

**SDK today.** The SDK supports generic (material) items through the `mc195_items_provider` seam (`lucky_items_bridge.h`):

- Construction follows the console-proven ingot pattern: `alloc 0xa0`, `0x354d80`, verify fields, set the icon wstring through `0x67840`, classification `0x000000030000001f`.
- Each item gets a private vtable copy with only `+0x108` replaced.
- Host-tested against the **real** r72 bridge, window and catalog code (`tests/test_retail195_bridge.cpp`).
- Not yet run on the console.

### 1.2 Blocks — Host + Unicorn; never accepted on console

| Mechanism | RVA / offset | Evidence |
|---|---|---|
| Block registry | global `0x140ad68`, object `0xcc0`, vtable `0x1218840`. 366 slots (0..365) at `+0x150` | Static + Console probe |
| Register virtual `(reg,id,key,block)` / present / by key / ID cap 365 | `+0x28` → `0xb8090` / `0xb82c0` / `0xb8120` / `0xb84c0` | Unicorn |
| OreBlock: ctor, size, vtable (155 slots) | `0x503a80`, `0x1f0`, `0x1298cf0` | Unicorn |
| Setters: hardness / resistance / sound / texture name | `0x92a00` / `0x92580` / `0x92540` / `0x94890` | Unicorn |
| BlockItem: ctor, vtable, size `0xb0` | `0x7d1d0(item,block)`, `0x1218a50` | Unicorn |
| Block→item map insert/find | `0x3af480` on `0x1417788` | Unicorn |
| Block name virtual | `+0x380` → `0x94650`. **Never** touch `+0x388`/`+0x390` | Unicorn |
| State encode / decode (decode dereferences unknown IDs unchecked) | `0x91bc0` / `0x91c50` | Static |
| Post-stock-block window (`.text` patch) | `0xb74de` (window `0xb74d8`) | Console (Lucky) |
| Terrain atlas: stock 256×624, register `+0x438` → `0x94ea0`, face getter `+0x458` | `shared_terrain_195/runtime.cpp` (two fixed tiles + SDK tiles from cell 2) | Console for Lucky/Steel ore; SDK tiles host-tested (`test_terrain_atlas195`, `test_composite195`) |

**Free block IDs.** The ORE_PROBE observed exactly 21 slots where both the block slot and the mapped item slot were empty: 253, 254, 261, 262, 280–290, 294 and 314–318 (`runtime-tests/mc195-lucky-cleanup-r24-s9v_d5zy/log-before`). Lucky owns 253 and Steel Ore owns 261.

Block 256 is refused, because item 1024 crosses the decoder boundary. IDs missing from the static item table (8, 10, 36, 51, 64…) are real blocks that have no item, so they are **not** free.

**SDK today.**
- The core, the package format, the ledger (block allowlist) and the mock backend fully support blocks, with tests.
- The retail backend **does not** advertise blocks.
- `lce_modpack.py check --profile retail195` and the loader both report it.

### 1.3 Recipes (crafting) — Console postcheck only; no confirmed craft

| Mechanism | RVA / offset | Evidence |
|---|---|---|
| Recipes static ctor wrapper / owner ctor / owner global | `0x5a0710` / `0x5a0740` / `0x142f278` | Console |
| Injection window: cache builder call (`.text` patch) | `0x5a9ef1` → `0x5aa200`. Guard `df e8 0a 03 00 00 48 8b 05 eb 1b da 00 48 8b 00` | Console |
| Owner layout | `+0x08/+0x10` output-key map; `+0x50` group-0 vector; `+0x110` cache records (`0x48` each) | Console |
| ItemInstance ctor `(this,Item*,count,aux)`, `0xa8` bytes | `0x3b7720` | Console |
| ShapedRecipe ctor `(this[0x40],w,h,cells,result,group)` | `0x6013d0` | Console postcheck |
| Output key / map insert / vector grow | `0x3b86f0` / `0x5ab110` / `0x5d3150` | Console postcheck |
| Stock parser: treats IDs above 1023 as blocks | `0x5aa520`; failing `dynamic_cast` → `0x1409f50` → `0x92530` derefs `+0x150` | Static: **avoid** |

**Proven on console.** Only native postcheck lines, such as `STEEL_CONTENT recipe=2` (`mc195-probe-tq8i0udc`). No run records a confirmed craft that consumed ingredients and produced output.

**Shared publisher.** `additive_recipes_195/shaped.cpp` (`lce195_shaped_publish`, ABI 1) has these limits:
- Inputs are always wildcard aux `-1`.
- Output count is 1 and aux is 0.
- One recipe per output (the output-key map bucket must be empty).
- At most 32 recipes per batch.

**SDK today.** The SDK supports shaped recipes within exactly those limits:
- Through `lce195_recipe_provider_register("lce-sdk")`.
- Cells are cropped to the publisher's row-major layout.
- The category maps to the Mods crafting tab's 7 categories.
- The loader drops a mod with a precise reason instead of publishing a wrong recipe (gap **G4**).

### 1.4 Smelting — Host + image-pin tests only

- Furnace singleton wrapper `0x325fa0`. The ctor call `0x325fb6` is the hook site. Global `0x1416a80`, size `0xf8`.
- Three maps:

  | Owner offset | Helper | Contents |
  |---|---|---|
  | `+0x00` | `0x329640` | input → result |
  | `+0x50` | `0x329820` | output → XP |
  | `+0xa8` | `0x329a00` | input → mask |

- Publisher: `steel_furnace_195/native.cpp`. **Never deployed.**
- **SDK today:** core and mock only; the retail backend does not advertise it (gap **G5**).

### 1.5 Crafting tabs and Creative inventory — Console

| Mechanism | RVA / offset | Evidence |
|---|---|---|
| Mods crafting tab: 7 categories `TOOLS, MATERIALS, FOOD, ARMOUR, TECH, TRANSPORT, MISC`, capacity 512 | recipe traversal `0x9ae720`, filter patch `0x9ae7c0`, input `0x9ac010` | Console r18: `CRAFT_MODS match=1 made=1`, user "ayyy it works". One presentation failure is still open |
| Creative category array, 12×`0x20` | `0x143d020` (TabSpecs `0x143d010` must be null at hook time) | Console |
| Creative group constructor / `new[]` hook | `0xa25790` / `0xa33027` | Console |
| Item row helper (never use the tile helper `0xa33590`) | `0xa336a0` | Console |
| Private Mods Creative group, 50 per page, 512 rows | `additive_items_195/creative.cpp` | Console (Steel: "all six Steel item icons appeared in the added Mods Creative tab") |

**SDK today.**
- Every SDK item lands in the private **Mods** Creative group, because stock tabs are never modified (project rule). A requested stock category is kept only as metadata.
- Recipe `craft_tab` selects one of the 7 Mods crafting categories.
- Custom mod tabs (`register_tab`) are validated and kept, but collapse into the single Mods tab on retail (gap **G6**).

### 1.6 Textures, atlases and models — Console for items; terrain limited

- **Item atlas.** `items.png` is 256×288 with 288 live sprites (280 static plus clock P0–3 and compass P0–3).
  - Mods append 16 px rows: `x=(i%16)*16`, `y=288+(i/16)*16`.
  - The stock UV rescale is in `steel_process_owner_195/resource.cpp`.
  - The sprite class check accepts only `0x13451d0`, `0x132efb8` and `0x132f070` (the ClockAtlasSprite fix).
  - The native bind is `0xd53b40`.
  - Atlas-seed hook: `0xd3d6d8` → `0xd3e070`.
  - Console-verified (6 Steel icons). One reload regression is recorded (`mc195-reload-56rcizeg`, reason 5).
- **Terrain atlas.** 256×624, append tiles (`additive_terrain_lucky_195/layout.c`). `shared_terrain_195/runtime.cpp` keeps its two fixed tiles (Lucky 0, Steel 1) and appends the SDK block tiles after them (weak `lce_sdk_terrain_*` references, gap G1). The SDK tiles share the fixed 512 KiB tile buffer: (2+n)·size²·4 bytes, so up to 62 tiles at 16/32 px and 30 at 64 px; packs of 128 px and up already exceed the 4096-row layout limit for the fixed tiles. If they do not fit, or the pack size is not a power of two, the SDK faces keep the stock fallback for that generation.
- **Models.** Only flat 16×16 item icons and full-cube blocks exist. No JSON or geometry models. BlockItem inventory rendering goes through the block route (`+0x170`/`+0x178`).
- **SDK registration.** Manifest, C, and Lua can stage 16×16 RGBA8 item icons and terrain tiles. The item provider supplies the icon bytes to the shared item atlas owner. The terrain owner attests the uploaded generation before binding the SDK sprites and requires its reload lease to detach them. Texture registration success reports staging. The atlas owners report application for their generation.

### 1.7 Strings and localisation — Console for item names only

- Localizer singleton `0x1440530`, lookup `0xd39880`, `getDescriptionId` `0x24aeb0`.
- Custom dialog string IDs `0xfde30000`–`0xfde30004` are used only when the stock lookup returns the missing sentinel `base+0xf0fdc0` (`mods_native_labels_195`).
- Item names use a private vtable at `+0x108`. Block names use `+0x380`. No global string table is changed.
- **Mod string ids (strings195, 2026-11-18).** The u32 ids that effect, potion and enchantment names use resolve through `[Localizer+0xa0]`, an unordered_map<u32, wstring> per language. strings195 inserts mod ids `0x4C430000..` into every language map with the game's own loader calls, and re-checks them every title tick because a language change rebuilds the Localizer.
- **SDK today:**
  - Item display names come from the package `lang` (`en_US`), falling back to `name`.
  - The shared catalog only accepts printable ASCII, so the SDK passes an ASCII fallback there while the UTF-16 name comes from the dispatcher.
  - Other locales are stored but not selected, because there is no game-language query yet (gap **G8**).

### 1.8 Entities and mobs — Static + spawn helpers; no custom types

- Native entity registry: initializer `0x28dc10` → `0x28d9c0(id,typeMask)`, 96 registrations. IDs differ from PC (pig 90, sheep 91, armor stand 30, polar bear 102).
- EntityIO factory `0x28d290(Pair*, CompoundTag*, Level*)`. Server insert is Level vtable `+0x1d8` = `0xcfdf00`.
- CompoundTag: ctor `0x6d1210`, put `0x1af4a0`, get `0x1b7400`.
- Spawning existing types works in the Lucky stack. New entity types, models, AI and sync have **never** been attempted.
- **SDK today:** the descriptor (`lce_entity_desc`) and package section are reserved. Registration returns `LCE_E_UNSUPPORTED` (gap **G9**).

### 1.9 Worldgen — Static + Unicorn; **nothing ever hooked on console**

- OreFeature: ctor `0x503e10`, vtable `0x12991d8`, place `0x503f60`, Java RNG `0x593950`, predicate `base+0x1401258`.
- Decorator chain: Biome vtable `0x1216218+0x58` → `0x85430` → decorator `0x87290` (candidate hook), ore stage `0x88970`.
- Fresh-chunk post-process `0x40ffb0`.
- All of it is uninstalled (`steel_ore_*_195`). The fork PRX `203d4cd8…` "has not been installed or tested on PS4".
- **SDK today:** `lce_ore_desc` is reserved. The converter reports Java/Bedrock worldgen files as skipped (gap **G10**).
- **Biomes (biomes195, 2026-11-18).** The 1.95 biome selection is `BiomeInitLayer` (RTTI `14BiomeInitLayer`, vtable `0x12163b8`, `getArea 0x8a290`). It keeps 4 climate pools as members of each layer instance, built fresh per world. The SDK replaces the class's `getArea` slot in the RELRO vtable, which is data outside segment 0. The hook appends the mod biomes to each instance's pools on first use and then runs the original. See modules/biomes195/STATUS.md.

### 1.10 Sounds — Static

- 1.95 sound IDs: `neo-ps4-mob-candidate/Minecraft.World/PS4SoundMap195.h` (238/244 mapped). Packet 62 is ClientboundSound.
- Native playback is only analysed for XP (`0x54ebc0`).
- **Custom audio (audio195, 2026-11-18):** no sound-bank writer is needed. Mod sounds become real `SoundEvent`s (registry `0x1430570`, ids 523+), and a private `SoundEngine` vtable plays their PCM through the game's own Miles driver (module STATUS has every RVA). Gap **G11** is closed apart from the console run.

### 1.11 Skins and texture packs — Console (separate path, not part of this SDK)

- Downloaded skin packs: `crashlog-build/dyn_skins.cpp`.
  - Scans `/data/lce-mods/packs/<slug>/{SkinPack.pck,lce.desc}`, then generic catalog → owner load → scoped publish under a texture-cache lease.
  - Globals: storage `0x147cb78`, app `0x1464780`, skin manager `0x143ef80`. DLC 2002 is reserved.
- This works and ships. The SDK reserves the folder names `packs` and `reservations` so the two systems never collide.

### 1.12 UI and menus — Console (plugin-drawn)

- **Store → Mods hub.** The plugin draws it with its own compositor (`mod_menu_frontend_*`).
  - Title Store control ID 5 at scene `+0xe50`, input bridge `0x7d6010`.
  - The title tick detour `0xa81b50` schedules work through `mc195_gre_set_title_operation`.
- **No native UIScene registration.** The scene map `0xe8af4c` is for 2.72.
- **SDK today:** no UI API (gap **G12**). The per-mod report (`lce_mod_report`: state, reason, counts) is designed to feed the Mods hub list.

### 1.13 Commands — None

- No command registry hook exists.
- The PC-server route is chat `/lce-action <id> …` (`remote_actions.cpp`): give is 247, mob spawn is 66 (gap **G13**).

### 1.14 Events and ticks — Console for the scopes the Lucky stack uses

| Event | 1.95 site | Evidence |
|---|---|---|
| Server/connection tick | `0xc859e8` (continues `0xc859f0`); per-player `0xce4380`; paused `0xc84750`; server singleton `0x14648d0` | Console (Lucky, F3) |
| Item use (server) | call `0xd0ff4a` (CAS word `0xd0ff48`) → `ItemStack::use` `0x3b8190`; per-item via private vtable `+0x40` | Console (Lucky) |
| Block destroy | `0xd0e8b0` → `0xd0fab0` → Block vtable `+0x340`; per-block via private vtables | Console (Lucky) |
| World epoch / login / remove / autosave | `0xc862e0` / `0xcc3780` / `0xcc85c0` / `0xc84920` | Console (NBT save 2/2) |
| UI/title tick | `0xa81b50` detour; `shared_tick_195.c` fanout | Console |

- The shared multi-subscriber owner is `Mc195SharedScope` (`SHARED_SCOPE_INTEGRATION.md`).
- **SDK today:**
  - The event bus, subscription and cancellation in the core are complete and tested (`lce_loader_dispatch`). `lce_sdk_dispatch()` is exported for hooks.
  - The retail backend does not yet advertise `LCE_CAP_EV_*`, because no `Mc195SharedScope` subscriber is wired to the SDK (gap **G3**). The subscriber is the next small step: the scope already exists on the console.

### 1.15 Networking with the PC server — Static/converter work; **custom IDs are not synced**

- Packet table: `/home/codey/minecraft-lce-work/ps4-195-packet-ids.json`.
- Items travel as an i16 ID (writer `0x1f4970`, `0x1f5c60`). ItemEntity metadata index 11 uses serializer 4.
- The PC server (`neo-ps4-mob-candidate`) has **no** mapping for IDs 2268+. Tagged stacks are rejected by its converter.
- CustomPayload is 225 serverbound and 250 clientbound, with a channel of 20 characters or fewer.
- **SDK today:** none (gap **G14**). Treat SDK content as single-player/local only until the server consumes the ID ledger (see INTEGRATION.md §6).

### 1.16 Scripting (Lua) — VM only; never bound to the engine

- Vanilla Lua 5.4.8 (`compatibility/ruby/vendor`, decimal-point fix).
- Persistent `lua_State` runtime: `lua-runtime/callback_runtime.c`.
- Workshop API reconstruction: `registerItem`, `registerShapedRecipe`, `registerOreFeature`, `registerEvent` and others (`compatibility/ruby/registrations.json`).
- The VM self-test passed on the **2.72** console only (`live-readback-20261031/lua-result-persistent.log`). No Lua code is in the 1.95 plugin. There is no sandbox: a memory cap and instruction hook exist, but scripts can catch the hook.
- **Assessment:** not adopted as the SDK's primary layer. Data packages cover the declarative 80% (the same calls the Workshop scripts make, see `mod-api-catalog/contracts.json`), with deterministic validation and no VM on the startup thread.
- The Lua runtime is the right **behaviour** layer later, on top of the event bus (G3). Its `register*` calls should map one-to-one onto `lce_api` (an adapter would call `api->register_item` etc.), and its event callbacks onto `subscribe`. It needs a real sandbox (no `io`/`os`/`debug`, a non-catchable instruction limit) before third-party scripts run. See gap **G15**.

---

## 2. Hooking constraints (apply to every backend)

- **Prefer data writes.** Private vtable copies of game objects keep the RTTI prefix and every slot, replace exactly one, and CAS the object vptr. They are not covered by the `.text` hash.
- **Code patches** use `mc195_publish_call` (`native_threeway_repeat_candidate/source/legacy195/call_publish.c`):
  - 8-byte-aligned CAS of a call word to a relay in a private page (±0x10000000…).
  - `sceKernelMprotect` RW → CAS → restore.
  - Returns REJECTED, INSTALLED or RESTORE_FAILED.
  - CAS does not serialize other cores' instruction streams, so install only after image attestation and before the game reaches the site.
- **Anticheat.** `ps4-transport/client_report.c:146-166` SHA-256s `eboot.bin` segment 0 at report time. `account-service/client_policy.py` bans any hash that is not in `official-builds.json` and any unknown module.
  - The accepted hash for `r8-anticheat-20261117220643` is `b4e71242…ae5b`. That is not the stock RX hash `231a6bfb…c682`, so the shipping plugin already leaves deterministic `.text` patches whose hash is registered per build. (Likely: same segment range. Unverified: both hashes covering identical bytes.)
  - **The SDK adds no `.text` patch.** Enabling the content stack that the SDK rides (item/crafting/atlas/block-init call patches) changes the hash. That new hash must be measured and registered with `publish_plugin.sh`, or the one-shot patches must be restored after they fire (INTEGRATION.md §5).
  - Native mod PRXs would be new modules and would be banned by `allowed_modules`. That is one reason native mods are disabled.
- **Privileged syscalls** (IDPS/OpenPSID) raise SIGSYS unless the process is GoldHEN-jailbroken first. The SDK uses only file I/O, `getdents`, `sceKernelQueryMemoryProtection` and thread IDs.

---

## 3. Gaps that prevent porting arbitrary mods

Status as of the composite backend. "Wired" = in the backend, the phase dispatcher and the patch; "console" = needs the INTEGRATION.md §7 run.

| # | Gap | Status now | What is left | Size |
|---|---|---|---|---|
| G1 | Retail block backend | **Wired** (blocks195: data hook, N ≤ 19 OreBlock clones, BlockItems, drops, fire, names; terrain textures through the shipping terrain owner, host-tested, pending the loader owner's integration diff) | Console run of the terrain bind (T7), Creative entries for block-items in SDK-only mode, harvest tiers > wood, non-cube shapes, placement states. Console acceptance (T7) | M |
| G2 | Tool, food and armour item classes | **Wired** (items195 constructors for every kind inside `prov_collect`) | Gameplay on console (mining, damage, equip, eat: T5); custom armour layers | S |
| G3 | Event backend | **Wired** (events195 subscriber in `Mc195SharedScope`, item-use/block-break/place routes, entity routes) | Remote-player chat hook, pad filter call site, `EFFECT_TICK` | S |
| G4 | Recipes: shapeless, counts, aux, shared outputs | **Wired** (ABI 2 publisher + the one-line r72 changes) | First confirmed craft on console (T3/T6) | S |
| G5 | Smelting backend | **Wired** (late phase, `[0x1416a80]`) | Console smelt (T6) | S |
| G6 | Several Creative/crafting tabs | Open | Paging model exists (ui195); real extra tabs would touch the stock category array (project rule forbids) | M |
| G7 | Placeholders for uninstalled mods | Open (retail refuses placeholders) | Generic placeholder item + icon owner; placeholder block (the decoder `0x91c50` crashes on unknown IDs) | M |
| G8 | Locale selection | **Wired** (`translate`, GameSettings language) | Translated item display names (the dispatcher uses `en_US`), catalog text is ASCII | S |
| G9 | Custom entities | **Wired** for clones (types, spawns, eggs, renderers, extra AI) | Custom skins (memory texture), loot tables, hitboxes, replacing AI, server-side sync of custom masks | L |
| G10 | Worldgen | **Wired** (ores, trees, lakes, structures via the decorate data hook; **biomes**: biomes195, the BiomeInitLayer pools through the RELRO `getArea` slot) | Nether hook site, flowers, biome variants; console run (T17) | M |
| G11 | Custom sounds | **Wired** (audio195: SoundEvents + Miles samples from a private SoundEngine vtable) | Console run (T18); music streams; mob sound slots naming mod sounds | S |
| G17 | Effects, potions, enchantments | **Wired** (strings195 Localizer ids, effects195, enchants195) | Console run (T15/T16); Creative entries for mod potions; new effect icon art | S |
| G18 | Fuel | Experimental | Runtime binding, new and saved furnace attachment, and manual slot acceptance are implemented. Direct placement and save/reload passed on PS4; hopper and Quick Move remain unverified. | M |
| G12 | UI API | Models only (HUD, Creative paging) | Compositor draw; no native UIScene registry exists | L |
| G13 | Commands | Engine done (events195) | A console producer: chat-intake hook or a script/package descriptor | S |
| G14 | Multiplayer sync | **Wired** client (netsync195 in `lcemodloader.o`, transport hunks in the patch) + server patch | Running both on hardware (server STATUS steps 1–9) | M |
| G15 | Sandboxed scripting | **Wired** (Lua 5.4.8 per mod; registration, events incl. API 1.3 InventoryTick/BlockNeighbor/BlockStepOn/BlockEntityInside, the 1.1 and 1.2 services, player-attributed damage) | composite install of the 1.3 routes + header enum; console run (T13) | S |
| G16 | Startup ownership (SDK-only owner) | **Done in the patch** (W1 + every phase call site) | Console boot (T1) | — |

**What "port any mod" realistically means now.** These content kinds are expressible and publish from one build:
- items of every kind, recipes of every shape, smelting;
- **new status effects, potions (splash/lingering/arrows) and brewing chains through them**;
- **new enchantments** and **new biomes**;
- opaque blocks, ores/structures, mob clones;
- **Custom audio**, tinted stock particle emission, and translations.

What remains is listed in §0's last column: fuel (experimental; see G18), block entities, dimensions, mob skins/loot, native screens, plus the per-type limits named there (identity-checked effect/enchantment clones, new icon art, nether biomes).

## 4. ABI policy (`include/lce_mod_api.h`)

- `LCE_MOD_API_MAJOR` changes only on breaking changes; the loader refuses other majors. `LCE_MOD_API_MINOR` grows by appending fields or functions.
- Every descriptor starts with `struct_size`. The loader copies only `min(struct_size, sizeof)` bytes and zero-fills the rest; this is tested with a shorter descriptor in `tests/test_loader.cpp`.
- Function tables are append-only. `api->struct_size` tells a mod which entries exist, and `api->capabilities` tells it what the active backend can really do.
- The package format (`LCE_PKG_FORMAT`) is versioned independently. The loader checks the package header's API major and minimum minor. The Python packer currently writes API 1.1, so scripts using newer optional bindings check for them at runtime. The manifest's `version` is the mod's release version.
