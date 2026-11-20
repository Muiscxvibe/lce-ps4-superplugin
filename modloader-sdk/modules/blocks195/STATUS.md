# blocks195: status, evidence and integration contract

This is the generic N-block backend module for CUSA00265 1.95. Every address below is an RVA in the 1.95 image (`legacy-195-analysis.elf`, SHA-256 `c675e6ce…029f`). None comes from the 2.72 trees.

**Nothing here has run on the console.** "Static" means disassembly of the pinned image. "Host" means `tests/test_blocks195.cpp`. That test builds its fake image from the **real** retail segments, with every `R_X86_64_RELATIVE` record applied at the host base. So every vtable, code and float pin is checked against retail bytes, while the natives are host fakes.

```
./test.sh       # g++ -std=c++17 -fsanitize=address,undefined -Wall -Wextra -Werror; 425 checks, 0 failures
./ps4check.sh   # clang++ with the build-ps4.sh target/flags -> build/ps4/blocks195.o (imports libc only)
```

## 1. Coverage

| Feature | Status | RVAs / offsets | Evidence |
|---|---|---|---|
| N blocks (1..19, the live-probed allowlist) | Host-tested | registry `0x140ad68` → object, vtable `0x1218840`, slots `+0x150` | Static: exactly 345 registrations in Block init `0x957a0`. The IDs never registered there are exactly the 21 probe-free IDs. Console ORE_PROBE `blocks=345` (mc195-nbt-refresh-live-u7rfkskg) |
| Construction | Host-tested. Same pattern as Lucky/Steel (Unicorn, never accepted on console) | OreBlock ctor `0x503a80` (52 bytes pinned) = `Block(Material*)` `0x91fb0` + vptr `0x1298cf0` + `0x924c0`. Alloc `0x1e8` (stock `0x972ee`) | For non-stone materials the module performs those same three steps with a different `Material*`. They are the whole ctor body |
| Hardness / resistance | Host-tested | `0x92a00` (+0x140; raises +0x144 to h·5), `0x92580` (+0x144 = r·3). Constants `0xe75774`=5.0, `0xe75770`=3.0 are pinned | Static. Stock calls them through vt+0x108/+0xb0 (`0x97311`, `0x97325`) |
| Material | Host-tested for STONE, WOOD, METAL, DIRT, SAND, WOOL; GLASS only with TRANSPARENT and PLANT only with NO_COLLIDE (API 1.7) | STONE `0x1418ec0` (`0x503a86`, `0x67fce8`), WOOD `0x1418890` (`0x53ddf8`), METAL `0x14183a8` (`0x99aa8`), DIRT `0x14188f0` (`0x207418`), SAND `0x1419028` (`0x2a0f16`), GLASS `0x1419040`, PLANT `0x1418ff8` (`0x138d56`), WOOL `0x1419018` | Static (ctor arguments). The Java-name mapping is inferred from which stock block uses each global |
| Sound | Host-tested | `0x92540` → +0x160. Globals: stone `0x1431e00`, wood `de8`, gravel `df0`, grass `df8`, metal `e08`, glass `e10`, cloth `e18`, sand `e20` | Static: stock IDs 1, 5, 13, 2, 66/101/167, 20/79/89, 35, 12 |
| Light emission | Host-tested | `0x92560` (vt+0xa8): +0x138 = (int)(f·15), with `0xe7576c`=15.0 | Static: glowstone `1.0` and torch `0.9375`. The module passes (L+0.5)/15 and verifies +0x138 == L |
| Slipperiness | Host-tested | float +0x180. `Block(Material*)` `0x91fb0` writes 0.6 at `0x92080` (also `0x92214` in `0x92150`), ice ctor `0x373eec` writes 0.98 | Static |
| Flammability | Host-tested (items phase) | Blocks::FIRE `0x140b328` (set by `0xc1520`, after Block init). Vtable `0x1252470`, must equal slot 51. `int& operator[]` `0x1475c0` on fire+0x1e8 (encouragement) and fire+0x238 (flammability) | Static: FireBlock::init `0x2ae7f0` makes 108 `0x1475c0` calls (planks 5/20). The module writes encouragement = max(1, flam/4) |
| Name (block) | Host-tested | private vtable copy (RTTI + 155 slots), +0x380 replaced (stock `0x94650`). +0x388/+0x390 are never touched | Lucky pattern (Unicorn) |
| Drops | Host-tested | +0x240 getResource (stock `0x503b00`) → `Item*` of `drop_item`. +0x360 count (stock `0x503bb0`) → min + nextInt(`0x593950`). +0x258 spawnResources `0x503c70` stays stock | Static: Block::spawnResources `0x93810` loops count×getResource, then popResource `0x7c2f0`. XP is 0 for unknown blocks (`0x503cbc`..). Fortune is ignored when the count is overridden |
| Registration | Host-tested | stock register `0xb8090` (slot[id], +0x128 = id, key map via vt+0x8). PRESENT `0xb82c0` before BY_KEY `0xb8120` (fatal when absent) | Static + Lucky Unicorn |
| BlockItem + map + name | Host-tested | alloc `0xb0`, ctor `0x7d1d0` (vtable `0x1218a50`, 57 slots), register `0x3af660` on `0x1417638`, map slot `0x3af480` on `0x1417788` (CAS 0→item), +0x108 replaced (stock `0x3af070`) | Same as `additive_block_items_195` (Unicorn). Static: 547 item registrations in `0x386070`, none using a mapped allowlist ID |
| Terrain, single texture | Host-tested | sprite `0xb8`, vtable `0x13451d0`, bind `0xd53b40` (prologue pinned), CAS Block+0x1a0 fallback(atlas+0xe8)→sprite | Lucky/Steel console pattern (`shared_terrain_195`). Here it is generalised to N blocks and unique tiles. **Wired (2026-11-19):** `shared_terrain_195/runtime.cpp` appends `b195_terrain_compose` output after its 2 fixed tiles and calls `b195_terrain_bind(first_index=2)` through `lce_sdk_terrain_*` (sdk_boot.cpp → composite `lce195c_terrain_*`, which forwards `terrain_held`/`terrain_uploaded` to the runtime's lease). Tests: `tests/test_terrain_atlas195.cpp` (real r72 layout/compose vs the bind rectangles, every count/size that fits), `test_composite195` (seam) |
| Terrain, 6 faces | Host-tested; orientation **unverified** | +0x1e0 per-face flag, set before registration. Registrar `0x94ea0` fills +0x1b0+8·i for i=0..5. Getter `0x95340` indexes by Direction+0x8 (`0x200600`) | Static. The D/U/N/S/W/E order is assumed from Java's data3d order. Check it on the console |
| Placement (facing/axis) | **Not possible with OreBlock**: refused | OreBlock has no state properties. StateDefinition comes from vt+0x478 (called by `0x924c0`) | It would need a directional class clone plus state-dependent face textures |
| Harvest tool | Material-implied only. "pickaxe" is accepted for STONE/METAL and "axe" for WOOD; everything else is refused | Material +0x170 | Assumes Java 1.12 semantics (Pickaxe: ROCK/IRON material; Axe: WOOD). **Not confirmed in the 1.95 tool code** |
| Harvest tier > wood | **Refused** | — | Java hard-codes tier checks per block identity in the pickaxe. Not located in 1.95 |
| Transparent / no-collide (API 1.7) | Host-tested | TRANSPARENT: GlassBlock `0x125dce0` slots `+0xd0 0x333d60`, `+0x100 0x333d70`, `+0x158 0x333d90`, `+0x188 0x353460`, `+0x1d0 0x353450`. NO_COLLIDE: `+0x1c8` -> FlowerBlock `0x139170` | Static (slot diffs against Block `0x1217e60`); see §7 |
| Replaceable | Refused | — | Needs the material/replace virtuals |
| Variants (API 1.7, 2..16 data values) | Host-tested | ColoredBlock `0x122d060` state slots; see §7 | Static + host (fake 0x924c0 builds the states through vt `+0x478`) |
| Box shapes (API 1.7) | Host-tested | `+0x178` getShape -> AABB from `0x170`; see §7 | Static + host |
| Multi-box shapes (API 1.11) | Unicorn + host | render shape 10 + `+0x190` list dispatcher + StairBlock `+0x1b0`; see §8 | Install pins, `tests/test_unicorn_boxes.py`, `test_boxes` |
| Block entities (`block_entity_slots>0`) | **9-slot storage blocks host-tested**; other sizes refused (see §4) | BE registry `0x140ae70`/`0x140aec0`, register `0xb9b30`, BE static init `0xb8bd0` | Static plus r72 Lucky custom BE (a POD record with no container) |
| Creative placement | Lead's mapped window | `b195_mapped_items()` mirrors `lce195_mapped_item` | The r72 window admits at most 16 entries, and Lucky and Steel use 2 |

## 2. W1: data hook instead of the `.text` patch at `0xb74de`

**Result: solid enough to prefer. It must still be proven on the console (T-run below).**

Facts from the image:

1. Every stock block registration calls register **through the registry object's vtable**. The sequence is `mov rbx,[0x140ad68]; mov rax,[rbx]; mov r13,[rax+0x28]; … call r13`. That makes 345 calls in `0x957a0`. The last is at `0xb74d8` with `esi=0x16d` (365, "jigsaw", JigsawBlock vtable `0x126fa48`). The next instructions (`0xb7517`) start the state-cache loop via vt+0x10.
2. `0xb8090` itself reaches the key map through `[vptr+0x8]`, which the copy leaves stock.
3. ID 365 occurs exactly once, and it is the final registration. It is not the largest-first; the order is not ascending (e.g. 364, 361, 362, 363, 365).

Mechanism:
- `b195_hook_install()` verifies the stock vptr, the RTTI `0x121e8e0` and all 10 slots. It checks that slot 365 is empty and copies `[ott, rtti, 10 slots]`. It sets +0x28 to `b195_register_relay` and CASes the object vptr. This is a heap write; no `.text` is touched.
- The relay always forwards to `0xb8090` first. On `id==365 && reg==hooked` (a one-shot CAS) it **restores the stock vptr**, then publishes. Publish re-verifies slot 365 holds a JigsawBlock and that at least 345 slots are occupied.
- After firing, the registry vptr is stock again. The r72 checks (`vt==0x1218840` in the mapped window and the BlockItem publisher) therefore still pass.

Risks:
- **Registry lifetime.** The registry is built by the static constructor `0x150cd0`. That constructor runs from the `.ctors` walk in DT_INIT `0x20` (list `0x1408820..0x1408aa8`, entry `0x1408840`), so it exists before `main`. Block init runs later, from `0x477c86`. It is **unverified** whether GoldHEN's `plugin_load` runs before or after the eboot's DT_INIT. If the global is still null, `b195_hook_install` returns 0 ("retry later"). The lead may poll from its startup worker until the global is non-null, and only fall back to §2.1 if block init would be reached first.
- **Race.** If the main thread loaded the old vptr at `0xb74be` just before our CAS, the 365 call bypasses the relay. `b195_hook_disarm()` then restores the vptr and reports `B195_HOOK_MISSED`. The items phase refuses cleanly because no block was published, so this fails closed.

**Console T-run needed:** log `*(base+0x140ad68)` and the slot-365 value at hook-install time, and `hook_state` / `hook_publish_result` in the items phase.

**Console answer (2026-11-19, mixed Lucky/Steel session with blockstates/newretail/retailall/rubyretail):** `LCE_SDK blocks_hook=0 waiting=1`, `blocks_hook retry=0`, then `phase=blocks rc=0 registry not constructed yet`; phases `block_hook=5 blocks=3 after_items=3`. The plugin loads **before** the eboot's static constructors, so the registry global is null at both SDK call sites and no SDK block (or block item) was published; `smelting retailall:smelt_ore: item unresolved` follows from it.

**W1b (host-tested, 2026-11-19):** with `b195_set_patch_ro8` (the composite passes the biomes195 `lce_ps4_mem_patch_ro8`), `b195_hook_install` arms while the global is still null by CASing the register slot `+0x28` of the registry **class** vtable `0x1218840` (RELRO, outside segment 0; expected = the relocated `0xb8090`, so an unrelocated image refuses) to `b195_register_relay`. Every registration still goes to `0xb8090` first; only id 365 on the object in `0x140ad68` (with the stock vptr) fires, the relay puts the slot back (patch_ro8) **before** publishing, and the object vptr is never touched (so the r72 `vt==0x1218840` checks hold). Disarm restores the slot (MISSED when 365 is already registered). Test: `test_hook_relro_slot` (fire through the slot, an unrelated object of the class registering 365 does not fire, slot pin, CAS mismatch, protection not restored = -1/UNCERTAIN, missed, disarm before the registry exists).

### 2.1 One-shot `.text` fallback

- `b195_text_plan()` reproduces `steel195_ore_plan`. It checks the 16 pristine bytes at `0xb74d8` and keeps the rel32 low byte `0x1d`, so only the aligned word at `0xb74e0` changes. The patched call is `call 0xcdab00`.
- `b195_text_restore()` CASes the original word back (RW → CAS → RX). It is idempotent and refuses a word it does not own. The lead's `lce195_oneshot_restore` (oneshot195.h) does the same, so either may be used.

## 3. Phase contract: what the lead must call

| When | Call | Requirement |
|---|---|---|
| Startup, after attestation, before Block init | `b195_configure(&m, base, &natives, &mem, plan.blocks, plan.block_count, plan.textures, plan.texture_count, names)` | `b195_natives_bind(&natives, base)` on the console. `names` may be NULL; use the en_US strings when you have them. One module per process |
| Same thread, right after | `b195_hook_install(&m)` | Returns 0: retry while the registry global is null. If 365 is already registered, the window has passed (refuse) |
| *(the game)* Block init | relay → publish | Nothing to call |
| *(alternative)* inside the lead's `0xb74de` window | `b195_blocks_publish(&m)` with `mem.blocks_held()==1` | Do not combine with the hook |
| After Block init, if the hook never fired | `b195_hook_disarm(&m)` | Always safe |
| Inside the held post-stock-items window (`0x477cc8` → `0x386070` returned), **after** SDK generic items are registered, if a drop references one | `b195_items_publish(&m)` | `mem.items_held()==1` on that thread. `0xc1520` has already set Blocks::FIRE by then (`0x477c8b`) |
| Same window | `b195_mapped_items(&m, out, cap)` → `mc195_register_mapped_item` / `lce195_mapped_window_enter` | Each entry carries the exact item vtable copy (only +0x108 differs) |
| Atlas growth | `b195_terrain_tile_count()`; `b195_terrain_compose(&m, tile, mip, buf, n)` per mip | Append after Lucky/Steel tiles. The `first_index` cell is row-major, 16 per row |
| After each upload, inside the terrain lease | `b195_terrain_detach()` before reload, then `b195_terrain_bind(&m, atlas, gen, w, h, stock_h, tile, first_index)` | `mem.terrain_held` and `mem.terrain_uploaded` must be real attestations. Done by `shared_terrain_195/runtime.cpp` (N-owner form, 2026-11-19): fixed tiles 0/1, SDK tiles from 2; bind rc 0 keeps the fallback, -1 fails the reload |
| Backend validation | `lce_backend.check_block = b195_backend_check_block` | Drops only the owning mod, with the reason |

Return values are 1 (complete), 0 (refused before any game-visible write; plugin-owned key storage may be filled) or -1 (partial/uncertain: keep the IDs reserved and never retry). A refused items phase after live blocks moves the module to `B195_UNCERTAIN`.

## 4. Block entities

**Storage blocks (`block_entity_slots = 9`), 2026-11-18, static + host.** The game decides a block has a block entity by the byte
`+0x14a` (set to 1 by the BaseEntityBlock ctor `0x424d0`, `0x424f9`) and then `__dynamic_cast` (`0xe6d020`) to `EntityBlock`
(typeinfo `0x12126a8`) and calls that subobject's vtable `+0x10` (`newBlockEntity`): `0x4107fd..0x410823`, and `0x40b6cf..0x40b701`
without a null check. BaseEntityBlock is `Block` (0x1e8) + EntityBlock vptr at `+0x1e8`; its typeinfo `0x12126c0` is a vmi with
`Block 0x1218338 @0` and `EntityBlock @0x1e8`. DispenserBlock (vtable `0x1239608`, typeinfo `0x1242080` si -> BaseEntityBlock) keeps its
EntityBlock vtable at `+0x4f0`: `{-0x1e8, ti, 0x227af0, 0x227b00 (Block dtor thunks), 0x208ef0 newBlockEntity}`; `0x208ef0` builds a
DispenserBlockEntity (new 0xb0, 9 slots, shared_ptr via `0x2359d0`, setLevel vt `+0x20`) and never reads the block.
The module builds a mod block the same way: OreBlock construction on a 0x1f0 object, `+0x1e8` = a private copy of that EntityBlock
vtable, `+0x14a = 1`, and the primary RTTI slot = a module `__si_class_type_info {vptr of the game's si typeinfo (an import, read from
DispenserBlock's typeinfo and required equal to OreBlock's), "N6lcesdk16ModStorageBlockE", BaseEntityBlock}`. The primary slots that
make it a container are copied from DispenserBlock: `+0x228` onRemove `0x209570` (getBlockEntity, type mask `0x3c00200`, dropContents
`0x1c5980`, comparator update, BaseEntityBlock::onRemove `0x508d0`), `+0x2c0` use `0x2085a0` (same lookup, player vt `+0xd70` opens the
3x3 screen), `+0x340` `0x43140` / `+0x398` `0x53010` (BaseEntityBlock, shared with ChestBlock), `+0x408/+0x410` comparator. Not copied:
the FACING/TRIGGERED state slots, tick/neighbour (dispensing), `+0x2d0` setPlacedBy (computes FACING). events195 routes BLOCK_USE with
`0x2085a0` as the original, so a mod can still consume the click. Every DispenserBlock slot, the secondary table, both typeinfos, the
vmi base list and the two `+0x14a` sites are pinned; a mismatch refuses only when a storage block is planned. Host tests: the layout on
the real relocated image, every pin refusal, and the RTTI trick under the host's Itanium `__dynamic_cast` (cast lands on `+0x1e8`,
unrelated casts fail). Open (superseded below for the title): 27/54-slot sizes (ChestBlockEntity has a renderer and
double-chest state; BarrelBlockEntity writes an OPEN state property the mod block lacks); console run.

**27-slot storage (`block_entity_slots = 27`), 2026-11-18, static + host.** The mod block is constructed by the real
`ChestBlock(type 0)` ctor `0x173060` on a 0x1f8 object (stock alloc `0x9b3a1`), so it is a ChestBlock for RTTI (`0x1225af0`, si ->
BaseEntityBlock), owns ChestBlock's FACING state definition and EntityBlock table (`+0x1e8` = `0x12255e0`, newBlockEntity `0x175800`
-> ChestBlockEntity, 27 slots). The private vtable is a copy of all 156 ChestBlock slots (it adds `+0x4d8` `0x1755f0`) with seven Block
cube slots put back: `+0xd0 0x92970`, `+0xd8 0x929a0`, `+0xe8` render shape `0x929d0` (chest `0x176300` returns 0x16 = entity-rendered),
`+0x178 0x92b40`, `+0x1a0 0x934c0`, `+0x1d0 0x93680`, `+0x438 0x94ea0`; then name/drop dispatchers and the usual setters, and `+0x12c=1`,
`+0x130=0xff` (what `0x924c0` derives for a solid cube). Opening: ChestBlock::use `0x174ba0` -> getContainer `0x174d70` -> player vt `+0xd70`
(ServerPlayer::openContainer `0xd07d20`): a Container that is neither an InteractionObject nor a PlayerEnderChestContainer gets the
generic ContainerOpenPacket and ChestMenu `0x17a750` with rows = size/9. Every slot value, the cube slots on both tables, the secondary
table, the ctor prologue and the alloc site are pinned. Host test: construction, slot diffs (exactly 8), items phase, pin refusal.
Open: console run; material is fixed to wood.

**Titles, the double chest and the chest model (2026-11-18, static + host).**
- *Correction:* ChestBlockEntity and DispenserBlockEntity are both InteractionObjects (RTTI `0x122a880` / `0x1242c20` ->
  RandomizableLockableContainerBlockEntity `0x12b7840` -> LockableContainerBlockEntity -> LockableContainer `@+0x40` -> InteractionObject `@0`,
  Container and Nameable virtual at `+0xf8` / `+0xa8`). ServerPlayer::openContainer `0xd07d20` casts with `0xaaa840` (Container -> InteractionObject)
  and, for both, sends menu type vt `+0x28` (chest `0x179830`: 0 = generic 9xN) and the title InteractionObject `+0x30` = `0x5985b0` ->
  `0x598460`: {text of the name record `[be+0x68]` (`0xb82300`), the record's `+0x90` string, bytes `+0xb9`/`+0xb8`}. The generic branch at
  `0xd082a4` (Nameable `+0x18` through vtable `-0x38`) is for Containers that are not InteractionObjects.
- *Title:* the record is set by RandomizableLockableContainerBlockEntity::setCustomName `0x597b30` (`mov rdi,[rdi+0x68]; jmp 0xb826f0`), the call
  ChestBlock::setPlacedBy `0x1735e0` (+0x368) makes at `0x173a1a` for an anvil-named item, with ItemInstance's defaults (`0x360a60` second = L"",
  `0x360bd0` b1 = 0, `0x360d00` b2 = 1). Both storage kinds now have a private EntityBlock table whose `newBlockEntity` (`+0x10`; sret: rdi = the
  shared_ptr, rsi = EntityBlock this, rdx = Level*, ecx = data, call site `0x40b6e4..0x40b701`) is `b195_new_block_entity_dispatch`: stock
  `0x175800`/`0x208ef0`, then setCustomName(be, display name, L"", 0, 1) when `[be+0x68]` is set. NBT keeps it ("CustomName": load `0x176d05` ->
  `0xb82450`, save `0x177310` -> `0xb821e0`). The 0x52ab0 "CustomName" reader of the old notes belongs to another class (`+0x38` string).
- *54 slots:* getContainer `0x174d70` uses the BE partner (`+0xc8/+0xd0`, set by the pairing code in BE `+0x40` `0x177970`); the pairing test
  `0x178a40` requires both BEs' blocks to `dynamic_cast` to ChestBlock **and be the same object** (`0x178b17`), the same y, an unpaired partner and
  equal FACING perpendicular to the pair axis. So two identical mod storage blocks pair (CompoundContainer `0x1b5760`, 54 slots, 6-row ChestMenu);
  a mod block never pairs with a vanilla chest or another mod's block. CompoundContainer's title (`0x65230` -> Nameable getName `0x1b5c00`) is the
  first half's custom name, else the second's. The kept ChestBlock slots (+0x2d0 getStateForPlacement `0x173530`, +0x368 setPlacedBy) set FACING.
- *Chest model:* the renderer map is keyed by BE vt slot 0 getType (`0x1e00201`; `0xb2b5a0`), which is also the save id (`0xba78c`) and the
  getContainer type check (`0x174dbb`), so it cannot change. Instead the wrapper swaps the BE's primary vptr (`0x1225618`, prefix {0xf8, 0xf8, 0,
  ti}, 63 slots, `0x1225810` = 0xb8 starts the next group) for a private copy whose `+0x70` getViewDistance (stock `0xbad20`, a constant) returns
  -1.0. The BE pass `0xb2b9d0` calls `+0x68` distanceToSqr (`0xbace0`) and `+0x70` and returns without rendering when `viewDist <= distSq`
  (`vucomisd; jbe` at `0xb2ba35`). The chunk compile `0xb85200` only asks whether a renderer exists (`0xb8665e`).
- *Pins (refuse the storage plan):* setCustomName code, `0x598489`, both InteractionObject `+0x30` slots, the BE vtable prefix/slot `+0x70`/next-group
  word, `0xbad20` bytes and the cull bytes. Host test `test_chest_storage`/`test_full_data_hook`: the wrappers through the private tables, title
  arguments, BE vtable exactly one slot different, refusal when `+0x70` is foreign.
- *Limits:* a BE created from NBT by the type factory (world load on the server level) does not pass newBlockEntity and keeps the stock vtable; the
  title still holds through CustomName. Client-level BEs in Java's flow are created through the block (getBlockEntity create path `0x4107fd`), so the
  rendered ones should be covered; confirm on the console after a reload. The dispenser kind has no BE renderer, so only the title applies.

### Earlier notes (before storage blocks)

- **Registration exists.** BE registration is `0xb9b30(index, factory, protocol, name)`, 32 stock calls `0xb8c32`..`0xb9aed`, in BE static init `0xb8bd0` (called at `0x477d16`, after Item init). The maps are names `0x140ae70` and registry `0x140aec0`. r72 `lucky_block_full_events_195/custom_block` already registers a custom BE type: base BE vtable `0x12e7328` (32 slots, load slot 6, save slot 7), BaseEntityBlock typeinfo `0x12126c0`, and +0x228 → `0x508d0` so the BE is removed. That covers save/load of a POD record.
- **A chest-like container needs four more pieces, none of them located:**
  - a `Container` implementation, with `LockableContainerBlockEntity` / `RandomizableLockableContainerBlockEntity` as bases (RTTI present);
  - a menu: `ChestMenu` typeinfo `0x122a8a0`, vtable `0x1225b18`;
  - LCE's fixed-layout UI scene: `21UIScene_ContainerMenu`, `29UIScene_AbstractContainerMenu`. Slot counts are effectively fixed (27, 54, 9, 5);
  - the open path (Block use → player open-container → `ClientboundContainerOpenPacket`) and item-list NBT save.
- **Next step.** Find the ChestBlock use virtual (`10ChestBlock` vtable `0x12250f0`) and its open-container call. Then decide whether to reuse `ChestBlockEntity` (`16ChestBlockEntity`) with 27 slots, or a full custom type.

## 5. Uncertain or unverified (marked in code)

1. Plugin load vs DT_INIT ordering (§2).
2. The face index order of the per-face slots.
3. That the stock registrar sets every face slot of an unknown texture name to atlas+0xe8. The module checks this and refuses the bind if not.
4. Harvest-tool semantics: Java-1.12 material rules.
5. Fire encouragement derived from the flammability value.
6. Gameplay (break, drop, save/reload, Creative, network) is untested everywhere. PC-server sync of these IDs does not exist (gap G14).
7. 10 of the 547 item registrations in `0x386070` take a non-immediate ID, so the static vacancy proof is incomplete. The live checks in the items phase cover it: slot, `0x385af0`, `0x385d00` and map walk, all before any write.

## 6. Pinned RVA list

- **Natives:** `0xe6cfd0` `0x67840` `0xcda2f0` `0xb8090` `0xb82c0` `0xb8120` `0x503a80` `0x91fb0` `0x924c0` `0x92a00` `0x92580` `0x92540` `0x92560` `0x94890` `0x94650` `0x503b00` `0x503bb0` `0x593950` `0x385af0` `0x385d00` `0x3af660` `0x7d1d0` `0x3af480` `0x3af070` `0x1475c0` `0xd53b40`
- **Data:** `0x140ad68` `0x1218840` `0x121e8e0` `0x1298cf0` `0x12a7070` `0x126fa48` `0x1218a50` `0x1417638` `0x1417720` `0x1417788` `0x140b328` `0x1252470` `0x13451d0` `0xe7576c` `0xe75770` `0xe75774`, plus the material and sound globals in §1
- **Sites:** `0xb74d8` / `0xb74de` / `0xb74e0` (`.text` fallback), `0x477c86` (Block init call), `0x477c8b` (`0xc1520`), `0x477ccc` (Item init)

## 7. API 1.7: variants, box shapes, transparent and no-collide blocks (2026-11-18, static + host)

**Construction.** A block with any API 1.7 feature is built like ColoredBlock's own ctor `0x1a96d0`: `Block::Block 0x91fb0`, then the
module's private vtable as the vptr, then the state tail `0x924c0`. `0x924c0` calls vt `+0x478` (state definition, stored at `+0x188`,
default state `+0x190` = states[0]) and sets `+0x12c` (solid-render) / `+0x130` (light block 255 or 0) from state `+0xf8` -> block
`+0x1d0`, so both come from the module's slots. The module checks `+0x12c == 0` for every non-full block and, for variants, that the
default state is data 0 and that data 0..n-1 round-trip through `+0x48`/`+0x50` to distinct states. Plain blocks keep the old OreBlock path.

**Default state fix (console 2026-11-19, static + host + real-image pins).** The first console run refused every SDK block with
`variant default state is not data 0`: on the real image states[0] of the COLOR StateDefinition is not WHITE (data 0). ColoredBlock's ctor
does not keep `0x924c0`'s default: after it, `0x1a9714` calls any() `0x7a050` (`[[def+0x28]]`), `0x1a9762` setValue(COLOR, WHITE) through
state vt `+0x158`, and `0x1a9779` registerDefaultState `0x7a040` (`mov [rdi+0x190], rsi; ret`). blocks195 now does the same for a variant
block: `+0x190` = getStateFromData(0) `0x1a9850` (defaultBlockState `0x7a4d0` + setValue(COLOR, byId(0)), so it is correct from any
default). Pinned: `0x924c0..0x924ee` (46 bytes, the states[0] default store), `0x7a040` (8 bytes) and the three call sites (only when a
plan has variants). The host fake `0x924c0` now orders the 16 states 3, 10, 1, ... like a real StateDefinition (the old fake put data 0 first,
which hid the bug).

**One failing block no longer kills the others.** Construction is per block (`construct_one`): a block whose construction or checks fail
is dropped alone (`dropped`, `drop_reason`, `m->dropped`; never registered, its half-built object leaks), the others register and get
block items. Only when every block fails is the phase uncertain. Content that names a dropped block (its block item in recipes or
smelting) still fails where it is resolved.

**Variants (`variant_count` 2..16).** Slots copied from ColoredBlock `0x122d060` (values and first 16 code bytes pinned): `+0x478` `0x1a9950`
(StateDefinition over the static 16-value COLOR property through `0xcf850`; uses `this` only as the owner), `+0x50` dataFromState `0x1a9900`,
`+0x268` drop aux `0x1a97b0` (not when the block drops another item). `+0x48` stateFromData is a module dispatcher that maps data >= n to 0
and calls `0x1a9850` (DyeColor::byId `0x21ff80` already clamps > 15). Placement: Block `+0x2d0` `0x940d0` tail-calls `+0x48` with the
item's level data; the BlockItem's `+0x58` (stock `0x3aeea0`, returns 0) becomes MultiBlockItem's `0x49f950` (`mov eax,esi; ret`) and the
item byte `+0x31` (setStackedByData `0x3a5a70`) is set to 1. Item names per variant: the `+0x108` dispatcher reads the stack aux at
`+0x3c` (getAuxValue `0x3557c0`). Textures per variant: `+0x458` getTexture(Direction*, state) is a module dispatcher; for variant > 0 it
returns the module sprite of that variant's face (Direction `+0x8`, `0x200600`) in the bound terrain generation, otherwise the stock
getter `0x95340` (variant 0 keeps the `+0x1a0`/`+0x1b0` sprites). ColoredBlock's own texture slots (`+0x438`/`+0x458`, 16 sprites at
`+0x1e8`) are **not** used, so storage blocks (EntityBlock vptr at `+0x1e8`) can also have variants. Each variant face is one more
terrain tile (`B195_MAX_TILES` 256).

**Box shapes.** Block `+0x178` getShape returns an `AABB*` (0x30 bytes, 6 doubles; built by `0x170` box(x0..z1), used by every static
shape, e.g. the enchanting table `0x2d38cd`). The default collision `+0x1c8` `0x93660` -> state `+0x110` `0xd0d80` -> block `+0x178`, and
`+0x190` copies `+0x178` into a TLS AABB, so the box is outline and collision. Slots: `+0x178` -> module dispatcher returning the
module's AABB (allocated through `0x170` in the block phase and read back), `+0xd0` and `+0x1d0` -> `0x929d0` (`xor eax,eax; ret`), `+0x1a0`
face shape -> module dispatcher (0 SOLID for a face the box covers fully, else 8 UNDEFINED; EnchantmentTable `0x252850` is the model).
**NO_COLLIDE**: `+0x1c8` -> `0x139170` (FlowerBlock, returns null) plus the same non-solid set. **TRANSPARENT**: the five GlassBlock
slots above; a box overrides `+0xd0`/`+0x1d0`.

**Pins.** OreBlock stock values of every replaced slot, ColoredBlock/GlassBlock/FlowerBlock slot values and code bytes, `0x170`, `0x929d0`,
`0x95340`, `0x200600`, and for block items BlockItem/MultiBlockItem `+0x58`, `0x3a5a70`, `0x3557c0`. A mismatch refuses before any game
write. Host tests: the full data hook with a 3-variant block, a slab, a transparent half-depth block and a no-collide plant, item names
by aux, variant sprites before/after terrain bind, and pin refusals (ColoredBlock, GlassBlock, FlowerBlock).

**Open (console T-run).** Direction data-value order (D U N S W E) for the face shapes and variant faces; that the chunk renderer draws
partial boxes through `+0x178` for these blocks as it does for the enchanting table; that saved chunks keep the data nibble (no 1.95
code reads it differently for mod ids); Creative shows data 0 only.

## 8. API 1.11: multi-box shapes (2026-11-19, static + Unicorn + host; needs the owner's header diff)

**The list.** Block vt `+0x190` (default `0x92ef0`) returns `{AABB **data; uint32 count}` (`+8`). The default fills a TLS list
with `+0x178`'s AABB and two nulls; every consumer skips null entries (`test r15,r15` at `0x92ce7`, `0xb3b1b8`, `0x676627`).

**Consumers (rx-traced, pinned at install, run in Unicorn):**
- Chunk tesselator `0xb32160`: `state +0x170` -> Block, `state +0x68` -> render shape, `dec; cmp 0x43` jump table at `0xb322d1`.
  Case 10 (`0xb32581`) calls `0xb3b160`: `block +0x190(state, level, pos)`, then for every box the renderer shape floats
  (`+0x84` x0, `+0x88` x1, `+0x8c` y0, `+0x90` y1, `+0x94` z0, `+0x98` z1, `+0x9d` not-full) and `0xb5be10` (block tesselation).
  Unicorn: the real `0xb3b160` draws exactly the non-null boxes with their coordinates.
- Collision: StairBlock `+0x1b0` `0x6765c0` asks `+0x60` (actual state, unless the flag at `[rbp+0x18]`), then `+0x190`, then
  `+0x1b8` addAABB per box. Only virtual calls on `this`, so it works for any block with our list. Unicorn: real code adds each box.
- Face culling: Block `+0x188` `0x92c90` renders a face if a list box does not reach that cell side (Unicorn: no neighbour query).

**Module.** `LCE_SHAPE_BOXES`: 1..8 boxes, `box_sets` 1 or `variant_count`. Each box is an AABB from `0x170` (read back), the list
points at the block's private AABB* array, `+0x178` returns the set's union, `+0x1a0` is SOLID only where every set covers the face,
`+0xd0`/`+0x1d0` false. Slots: `+0xe8` = `0x678ad0`, `+0x190` = `b195_box_list_dispatch`, `+0x1b0` = `0x6765c0` (not with NO_COLLIDE).

**Open (console).** Hand/inventory rendering of a shape-10 block (the item renderer was not traced); stair facing comes from the
variant (Creative/aux), not the placing player; connected fences/panes would need neighbour reads in the dispatcher (not attempted).

**Creative variants (not done, needs shipping code).** Creative shows data 0 only: the r72 catalog entry `lce195_additive_item`
(`third_party/r72/additive_items_195/catalog.h`) has no aux, and the Creative feed (`steel195_creative_*` in the shipping plugin)
adds one ItemInstance per entry. Showing variants needs an `aux` (or a variant count) in that entry and one ItemInstance per data
value in the shipping Creative builder: a shipping-plugin change for the coordinator/loader owner. The SDK side is ready (variant
names by aux `b195_item_name_dispatch`, MultiBlockItem placement by aux).
