# biomes195: new overworld biomes on Minecraft PS4 Edition 1.95 (CUSA00265)

All addresses are 1.95 RVAs from the analysis image `c675e6ce…029f`, where the file offset of code is RVA + 0x4000. `tests/test_image_pins.py` re-verifies 83 pins against that image. Nothing in this module has run on a console.

Evidence levels:
- **Unicorn:** retail code ran on the host in `tests/test_unicorn_retail.py` (38 checks). The stubs are listed in that file's docstring.
- **Pinned:** bytes, instructions or RTTI were checked against the image.
- **Host:** `tests/test_biomes195.cpp` under ASan/UBSan (137 checks, including 50 rounds of 8 threads racing on the first use of one layer).
- **Static:** disassembly only.

## How a mod biome is generated

1. **Object.** Each mod biome is a real `PlainsBiome` object. `0x53d140(this, id, 0, props)` calls the base ctor `0x83e60(this, id, 0xb, props)`. That ctor:
   - stores `biomes[id] = this` at `0x840cd`;
   - copies the props fields into the biome;
   - copies the name wstring into `+0x28`;
   - inserts the name into the name map `0x140a7c0`;
   - builds the spawn lists (plains mobs) and the decorator `+0x60`;
   - frees props.

   The module then copies the colour-enum fields that the static init writes for plains (`+0x8`, `+0x190..+0x19c`) from `Biome::plains`. It also sets the top and filler blocks.
2. **Selection.** `BiomeInitLayer` keeps its climate pools as **per-instance heap arrays**: `{Biome**, int}` at `+0x38` (warm), `+0x48` (medium), `+0x58` (cold) and `+0x68` (icy). They are built by the ctor `0x89ec0` for every level's layer stack (`BiomeSource 0x8d4c0` → `Layer::getDefaultLayers 0x3e0d20` → `0x3e27f3`). `getArea 0x8a290` returns `pool[nextRandom(len)]->id`.

   The module CASes slot `+0x20` of the BiomeInitLayer vtable (word `0x12163d8`, in **PT_SCE_RELRO**, not segment 0) to `lce_b195_get_area`. On the first call for each layer object, that function:
   - swaps each pool for a `new[]` copy (stock entries first, then the mod biomes), using `0xe6cfc0`, which matches the dtor's `delete[]`;
   - publishes the pointer first, then the length, because readers load the length first (`0x8a3c9` before `0x8a3d5`);
   - tail-calls the retail `getArea`.

   All later layers (zoom, edge, hills, shore, rivers) then process the mod id like any biome id.
3. **Colours (optional).** The module gives the biome a private copy of its current vtable, with the RTTI prefix and all 27 slots, and replaces only these slots:
   - `+0x60` grass;
   - `+0x68` foliage;
   - `+0xa8` water;
   - `+0x28` sky.

   The vptr is swapped by CAS. With colours left at 0, the stock getters apply: the grass and foliage colormaps follow the biome's temperature and rainfall (Unicorn-tested), and water and sky follow the plains colour enums.

## Coverage

| Feature | Status | RVAs / offsets | Evidence |
|---|---|---|---|
| Biome object at a free id | **Implemented** | `biomes[256]` BSS `0x1409fc0`. Size 0x1d8. Vptr `0x12a2bb0` (`11PlainsBiome`, 27 slots). Id `+0x68`. Base ctor stores `0x840cd`. Props 0x48: wstring `+0` (`+8` buf, `+0x18` size, `+0x20` capacity, assigned with `0x67840`), depth `+0x28`, scale `+0x2c`, temperature `+0x30`, rain `+0x34`, `+0x38` 0xffffff, snow `+0x3c`, rain flag `+0x3d`, parent `+0x40` = −1 | **Unicorn**: the real ctor on a mod object at id 170 checks the vptr, id, `biomes[170]`, depth/scale/temperature/rainfall at `+0x10..+0x1c`, snow/rain/parent, the name wstring, the top/filler ids `+0x50/+0x58`, the decorator, the name-map node and the spawn lists |
| Free ids | **Implemented** | 72 stock ids, max 167. Allowed ids are 168..254 and 50..126. The RegionHills mutation lookup `id+128` is only done for ids ≤ 127 (`0x5b874e`), and `getBiome(id+128)` returns null, so the biome is kept (`0x859e0`). An id is refused if its slot is not null | Pinned + Unicorn (`getBiome`) |
| Name | **Implemented** | wstring `+0x28` = the mod key (for example `gem:crystal_fields`), which is also the name-map key. 1.95 shows biome names only in debug output. No string id is involved, so `name_sid` is not needed | Unicorn |
| Temperature, rainfall, snow, rain, height base/variation | **Implemented** | Biome `+0x18/+0x1c/+0x20/+0x21/+0x10/+0x14`. The temperature category virtual `0x85990` (slot `+0x98`, used by BiomeEdge) agrees with the climate pool | Unicorn |
| Top / filler block | **Implemented** | `+0x50/+0x54` and `+0x58/+0x5c` hold (block id, data), written after the ctor (the base ctor computes grass/dirt there through the registry) | Unicorn (field positions) |
| **Generation (selection)** | **Implemented** | Pools at BIL `+0x38/+0x48/+0x58/+0x68`: stock 6/6/4/4 entries (desert×3, savanna×2, plains / forest, roofed, hills, plains, birch, swamp / forest, hills, taiga, plains / ice plains×3, cold taiga). RELRO slot `0x12163d8` = `0x8a290`. Its only references are this slot and its relocation, and nothing calls it directly | **Unicorn**: the real BIL ctor builds the pools; after the rewrite the real `getArea` returns the mod id for about 2/8 of the medium cells, deterministically for a seed, differently for another seed, and only in medium cells; ocean passes through. A real `ZoomLayer::getArea 0x7acfc0` reaches the hook through the patched vtable word, the layer is patched once, and the zoomed output contains the mod biome |
| Weight | **Implemented** | `weight` 10 = one pool entry (a vanilla entry), rounded, 1..16 entries. `weight` 0 = registered but never generated. Pools hold at most 64 entries. The climate comes from the temperature: ≥1.0 warm, ≥0.5 medium, ≥0.15 cold, else icy | Host |
| Thread safety of the rewrite | **Implemented** | CAS on the pool pointer, then a release store of the length. Losers leak their copy. The stock array is never freed (≤ 160 bytes per layer object) | Host (8-thread race) |
| Grass / foliage / water / sky RGB | **Implemented** | Private vtable slots `+0x60` (`0x85450` → GrassColor `0x337040`), `+0x68` (`0x854b0` → FoliageColor `0x2eef40`), `+0xa8` (`0x85a80`: colour map keyed by biome id, then parent `+0x24`), `+0x28` (`0x85120`, enum `+0x194`). The return value is `0xFFRRGGBB` | Unicorn (the stock getters on the mod biome); host (slots, chaining onto worldgen's copy). **The consumer's channel order has not been run** |
| Decoration and mod features in mod biomes | **Implemented, needs the worldgen195 patch** | Mod biomes exist with the stock PlainsBiome vptr before `lce_wg195_publish`, so worldgen195 records them and hooks plains decorate `0x53d4c0` like any plains. Colour vtables are chained on worldgen's copy, keeping its `+0x58` dispatch, and are registered with `lce_wg195_adopt_vptr` (`worldgen195-adopt.patch`: 57 lines; worldgen195's own suite still passes with it, 244 pins). `lce_wg195_set_mod_biome_lookup(lce_b195_lookup)` lets feature `biomes` lists name mod biomes | Host (adopt callback); patch verified against worldgen195's tests |
| Mob spawns | **Inherited** | The plains ctor fills the spawn lists (`+0x70..`). entities195 `le195_publish_spawns(m, resolver)` can add mod mobs to mod biomes if the resolver returns mod ids (the patched `lce_wg195_biome_lookup`) and it runs after `lce_b195_construct` | Unicorn (lists filled) |
| Missing-mod placeholders | **Implemented** | `placeholder = 1`: a plains clone at a ledger id whose mod is gone, never generated and without colours. This keeps saved chunks with that id loadable (`getBiome` returns `biomes[id]` raw, so a null slot would be dereferenced) | Host |
| Nether / End biomes | **Refused** | The Nether and End use fixed single-biome sources, with no layer stack | Static |
| Ocean / deep / river variants, mutated (+128) variants, hills/edge/shore variants of a mod biome | **Not provided** | Those layers switch on stock ids. A mod biome is passed through unchanged, and rivers cut through it as usual | Static |
| Negative temperature or depth (ocean-like, snowy −0.5) | **Refused** | `lce_biome_desc` uses unsigned milli fields. Land biomes only (depth 0..2) | — |
| Display name localisation | **Not applicable** | No biome name string id is used on 1.95 | Static |

## API (biomes195.h)

The functions in lifecycle order:

| Step | Function | When |
|---|---|---|
| 1 | `lce_b195_init(base, &natives, &mem)` | `lce_b195_natives_bind(&n, base)`. On the console, `mem.patch_ro8` is mprotect RW → CAS → restore R |
| 2 | `lce_b195_from_desc(desc, id, top_id, top_meta, filler_id, filler_meta, &b, err, cap)`, then `lce_b195_add(&b, err, cap)` | Per `LCE_EXT_BIOME`. Use `b.placeholder = 1` for ledger ids of missing mods |
| 3 | `lce_b195_construct()` | Title, before any world |
| 4 | `lce_b195_colours(lce_wg195_adopt_vptr)` | After `lce_wg195_publish` |
| 5 | `lce_b195_select()` | Before any world |
| 6 | Optional: `lce_b195_restore()` | — |

Other functions:
- `lce_b195_snapshot(&st)`: diagnostics.
- `lce_b195_lookup(key)` and `lce_b195_object(id)`: lookups.

Every step returns 1 for done, 0 for a clean refusal, or −1 for uncertain.

## Integration recipe (coordinator)

1. **IDs.** Reserve biome ids from the ledger in 168..254 (then 50..126). Keep them stable per key.
2. **Validation.** In `check_ext(LCE_EXT_BIOME)`, call `lce_b195_from_desc` with resolved block ids. Refuse dimension ≠ overworld, an unknown top/filler block, and an id outside the ranges. Remove the old "biomes are not possible" refusal in `composite195.cpp:118`.
3. **Late startup, at the title**, on the UI thread, while `[0x14648d0] == 0`. Keep this order:
   1. `lce_b195_construct()`
   2. `lce_wg195_set_mod_biome_lookup(lce_b195_lookup)`, then `lce_wg195_publish()` (feature biome masks can name mod biomes, because features are converted with `lce_wg195_from_desc` after the lookup is set)
   3. `lce_b195_colours(lce_wg195_adopt_vptr)`
   4. `le195_publish_spawns(m, lce_wg195_biome_lookup)`
   5. `lce_b195_select()`

   Log `LCE_SDK phase=biomes rc=… built=N coloured=C select=…`.
4. **The worldgen195 patch** is `modules/biomes195/worldgen195-adopt.patch`. Apply it with `patch -p0` from the SDK root. Without it, pass `NULL` to `lce_b195_colours`, and mod features skip mod biomes that have colours (stock plains decoration still runs).
5. **Build.** Add `modules/biomes195/biomes195.cpp` to `build-ps4.sh` and the Makefile. Its undefined symbols are `memcpy memset snprintf strchr strcmp strlen`.
6. **Anticheat.** The RELRO word `0x12163d8` is not in segment 0, and `ps4-transport/client_report.c:211-212` hashes `segmentInfo[0]` only, so `game_text_sha256` is unchanged. This is a **new hook kind** for the stack: the first write to a RELRO page. The mprotect has to succeed on RELRO; if `patch_ro8` returns 0 or −1, biomes still exist but never generate.
7. **Multiplayer.** Chunk data carries biome ids as bytes. Every client must have the same mod biomes at the same ids (the ledger). Clients without them read `biomes[id] == null`. Add biome ids to the netsync ledger.

## Risks

- **RELRO write on the PS4.** `sceKernelMprotect` on the RELRO segment has never been done in this stack. A failure is reported cleanly (0 or −1), and the biomes then don't generate.
- **Colour channel order.** The getters return `0xFFRRGGBB`, the same format as the colormap pixels, but no real consumer of `+0x60` was run.
- **Plains-copied fields.** `+0x8` (64-bit, purpose unknown) and `+0x190..+0x19c` (colour enums) are copied from plains. Any consumer keyed by them treats the mod biome as plains (for example a "biome visited" tracker).
- **The decorator** is plains' (the ctor in Unicorn was stubbed because it needs live Block objects). Mod biomes get plains flowers and grass plus any mod features.
- **Removing a mod** leaves its ids in saved chunks. Keep a placeholder for every ledger id without a mod, or such worlds may crash.
- **Worlds created before the mod** only get mod biomes in newly generated chunks. Chunk borders against old terrain show the usual seams.

## On-console test (after `make test`, the PS4 build and the patch)

Use a package with one biome, for example:

```json
{"key":"gem:crystal_fields","temperature_milli":800,"rainfall_milli":400,"weight":40,"top_block":"minecraft:sand","grass_rgb":4259792}
```

1. **Title.** Expect:
   - `LCE_SDK phase=biomes rc=1 built=1 coloured=1 select=1`;
   - no `ABORT`;
   - the worldgen line still `rc=1`.
2. **New world, default type, random seed.** Within about 2000 blocks of spawn, find sand-topped plains with mint grass (#40FFD0).
   - F3 biome name: `gem:crystal_fields`.
   - Expect it in temperate regions only, never next to deserts or ice without a transition.
3. **Same seed twice** (a new world with the same seed): the mod biome is at the same coordinates.
4. **Save, quit, reload.** The biome and its colours remain. New chunks keep generating it.
5. **Mod removed, placeholder kept by the ledger:** the world loads, and the area shows plains colours. Without a placeholder, a crash is expected: do not test this on a real save.
6. **Negative test.** The log line `select=0` or `select=-1` means the RELRO install failed. Report it: biomes then never generate, and nothing else changes.

## Sources

The public TU19 LCE source mirrors (`BiomeInitLayer.cpp`, `BiomeOverrideLayer.cpp`, `Biome.h`, via the coordinator's notes and `api.tangled.org` for `BiomeOverrideLayer.cpp`) were used only to confirm the structure:
- the climate pools are member arrays of the layer;
- `BiomeOverrideLayer` replays a fixed `GameRules\biomemap.bin` and is not a general hook;
- biome names are debug names.

No code was copied. Every 1.95 layout above comes from the 1.95 image.
