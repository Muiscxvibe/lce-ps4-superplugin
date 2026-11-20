# worldgen195: mod world generation on Minecraft PS4 Edition 1.95 (CUSA00265)

All addresses are 1.95 RVAs from the analysis image `c675e6ce…029f`, where the code file offset is RVA + 0x4000. `tests/audit.py` re-verifies 244 pins against that image, including every table in `worldgen195.cpp`. Nothing in this module has been installed or run on a console.

Evidence levels used below:
- **Static:** disassembly, pinned by the audit.
- **Host:** the fake-image test `tests/test_worldgen195.cpp` under ASan/UBSan, 4594 checks.
- **Unicorn/native:** retail code executed on the host. For this module the only native evidence is the existing `steel_ore_own_chunk_195/test_native.py` fixture. It executes the real `OreFeature::place 0x503f60` with the same object layout this module writes: vptr `0x12991d8`, `+0xc` id, `+0x14` size, `+0x18` the natural-stone predicate `base+0x1401258`.

## Coverage

| Feature | Status | RVAs / offsets | Evidence |
|---|---|---|---|
| Data hook on Biome decorate (no `.text` patch) | **Implemented, host-tested only** | `Biome* biomes[256]` lives in BSS at `0x1409fc0`, so it is RW. The ctor `0x83e60` stores `biomes[id]=this` at `0x840cd` and the id at `+0x68`. 72 biomes are built by the static init `0x7f900`. All objects are on the heap, which is writable. 25 concrete classes, each with a 27-slot primary vtable. Decorate sits at slot `+0x58` (base `0x85430` → decorator `+0x60` → vt `+0x10`). One private vtable copy per class: RTTI kept, only `+0x58` changed. Each object's vptr is swapped by CAS, and the CAS rolls back on a race. `lce_wg195_restore()` puts the stock vptrs back | Static + Host |
| Decorate call sites, pos and Random | Pinned | `OverworldLevelSource::postProcess 0x508bb0` (call `0x509220`), Flat `0x2e6840` (`0x2e6fc8`), End `0x6dec40` (`0x6ded84`). pos is `(cx*16, 0, cz*16)`. The Random is the level source's `+0x30` (Java layout: seed `+0`, setSeed `0x596c70`, nextInt `0x593950`). Nether `0x4b8090` never calls decorate | Static |
| Generic N-ore publisher | **Implemented, host-tested only** | Supports up to 32 features. Our dispatch first forwards to the stock decorate, so stock generation is unchanged. It then places mod ores with a **private** Java Random derived from the stock state captured before stock decoration, the chunk coordinates and a per-feature salt. The live Random is never passed on, and it is checked unchanged afterwards; if it changed, the module disables itself. Vein centres stay inside the origin chunk for sizes 2..24, and `0x503f60` adds its own +8. The block is written with flags `0x74` via `0x3efa80`. Options: an optional replace predicate (private vtable, `{id, meta}`), a biome bitmask, a y range and rarity. Gates: the level must be a genuine `ServerLevel` (vt `0x1340d50`) and pos must be chunk-aligned with y=0 | Static + Host; native placement pattern is Unicorn (steel fixture) |
| Trees | **Implemented, host-tested only** | `TreeFeature` ctor `0x6f5b30(this, doUpdate, baseHeight, trunk BlockState*, leaves BlockState*, vines)`, vt `0x12e6598`, place `0x6f5eb0`. Fields are read back after construction. Trunks are placed at local 3..12. The base height must be 3..8 | Static + Host |
| Lakes | **Implemented, host-tested only** | `LakeFeature` ctor `0x3db8d0(this, Block*)` (`+0x10`), vt `0x1273c48`, place `0x3db900`. Place shifts by (-8,0,-8) and fills a 16-wide box, so the centre is +8 and the box stays inside the chunk. min_y must be ≥ 6 | Static + Host |
| Flowers | **Refused** | `FlowerFeature 0x2ea330` needs a FlowerBlock (Block vt `+0x4e8`). Generic SDK blocks are not FlowerBlocks | Static |
| Structures from block templates | **Implemented, host-tested only** | `Level::setTileAndData 0x3efa80(level, x,y,z, id, meta, flags)` with flags 2, the same as the non-updating `Feature::setBlock 0x2a4960`. Heightmap is `0x3f20c0`. Templates are limited to ≤16×255×16. Every write is clipped to the origin chunk, so there are no cross-chunk writes. Cells are indexed `(y*size_z+z)*size_x+x` (the lead's convention); palette entries are `(id<<4)|meta`, and index 0 keeps the existing block | Static + Host |
| Nether features | **Not possible via this hook** | `NetherLevelSource::postProcess 0x4b8090` places its features directly and never calls Biome decorate. It would need its own hook site | Static |
| New biomes | **Not feasible (documented)** | The biome *array* has free ids (256 slots, max id 167). However, the biome *source* chooses ids through hard-coded layer code, and the 1.95 GenLayer biome selection was not located. A new Biome object would never be chosen. It would also need a new Biome subclass ctor (25 concrete classes, all with ctor-only vptr refs) and save-format biome ids | Static |
| Dimensions | **Not feasible (documented)** | Dimensions are fixed classes, each with its own level source. RTTI: `NormalDimension 0x12946a0`, `OverworldLevelSource 0x1299240`, `ServerLevel 0x1340d50`, `ServerChunkCache 0x1340378`, plus Hell and End. Their numeric ids are wired into level/save/portal code. There is no registry | Static |
| Retro-generation of old chunks | **No** | Features only run inside fresh-chunk decoration. The populated bit is set at `0x41000f`, saved at `0x5003d3` and loaded at `0x501b49` (the stock path is unchanged) | Static |

## Phase / call-site contract (lead)

Return values: 1 = done, 0 = clean refusal with nothing changed, -1 = uncertain (keep storage, disable worldgen for the process).

1. `lce_wg195_init(base, &natives, &mem)`. Call once after attestation. Bind the natives with `lce_wg195_natives_bind(&n, base)`. On the console, `mem.read` must be protection-checked and `mem.cas8` must be an 8-byte aligned atomic compare-exchange.
2. `lce_wg195_from_desc(desc, block_id, meta, replace_id, replace_meta, &f, err, cap)` for each `LCE_EXT_FEATURE`. Block ids come from `plan->resolve(ctx, LCE_REF_BLOCK, key)`. For TREE, fill in `trunk_state`/`leaves_state` as `Block+0x190` default states. For LAKE, fill in `lake_block`. For STRUCTURE, build an `lce_wg195_structure` from the `lce_structure_desc` (resolve each palette key to `(id<<4)|meta`) and keep it alive for the life of the process. Then call `lce_wg195_add_feature(&f, err, cap)`.
3. `lce_wg195_prepare()`. Call it after blocks are registered. It runs the tree/lake native ctors and reads them back.
4. `lce_wg195_publish()`. Call it after the biome static init `0x7f900` and **before the first world is created or loaded**, at the title screen.
5. Optional: `lce_wg195_restore()` on disable. The private vtables stay alive.

Diagnostics come from `lce_wg195_snapshot()`, which reports calls, decorated, skipped, placements, refused writes and errors. Biome keys resolve through `lce_wg195_biome_lookup()`, which takes native names and Java 1.13+ aliases. entities195 uses the same lookup for spawn biomes.

Anticheat: no `.text` bytes are changed. The writes go to heap Biome vptrs and our own data.

## Open

- A native-execution fixture for the dispatch path. The OreFeature placement itself is covered by the steel fixture.
- Console acceptance.
- Nether hook site.
- GenLayer RE for new biomes.
