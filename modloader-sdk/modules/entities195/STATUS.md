# entities195: custom mobs on Minecraft PS4 Edition 1.95 (CUSA00265)

Every address is a 1.95 RVA from the analysis image `c675e6ce…029f` (code file offset = RVA + 0x4000). `tests/audit.py` re-derives and checks every one (104 pins) and regenerates `base_mobs195.inc` (56 clonable mobs). Nothing in this module has run on a console.

Evidence levels: **Static** (disassembly, pinned by `tests/audit.py`), **Host** (fake-image unit test `tests/test_entities195.cpp`, ASan/UBSan), **Console** (cited log). This module has no Console or Unicorn evidence. One native-execution attempt is described under "Open RE".

## Coverage

| Feature | Status | RVAs / offsets | Evidence |
|---|---|---|---|
| New entity type id (clone of a vanilla mob) | **Implemented. Host-tested only** | registrar `0x28d9c0(factory, int id, const wstring& name, u64 mask, const wstring& oldName)`, the same call that the initializer `0x28dc10` makes 96 times (called from `0x477d1b`). REGISTRY `0x1415fa8`: `+0x90` mask→id, `+0xe8` id→mask vector, `+0x100` mask→key (`0xba480`). KNOWN set `0x14160f8`, old-name vector `0x1416198`, factory map `0x1416148` | Static + Host |
| Save / reload of custom mobs | **Implemented (by construction). Host-tested only** | The save id is `0x27bb60`: REGISTRY+0x100 lookup of `GetType()` (slot `+0x60`). Load path: `0x28d290` → key → mask → factory map → `0x283770` (requires mask bit 27) → our thunk. A custom mob saves as `"modid:name"`. If the mod is removed, the stock factory returns nothing and the entity is dropped | Static |
| Unique type mask | **Implemented** | `mask = base | (k+1)<<34`. instanceof is `(type & S) == S` (465 inline `not eax; test eax,S` sites). Exact checks are 64-bit `cmp rax, imm` (450). Above bit 31, only bits 32 and 33 are used (Pillager `0x15d540041`, Endermite `0x24d540042`, Ravager `0x25d540041`). The maps hash all 8 bytes (FNV, `0x148e60`) | Static. Host property test: for every base × 16 indices × all known masks, instanceof is unchanged and the mask is never equal to a vanilla mask |
| Per-type private vtable | **Implemented. Host** | Each private vtable keeps the RTTI prefix and every primary slot (473–532 slots, pinned per base). `+0x60` is replaced by `le195_get_type`. Before replacing, the code checks that the stock slot is exactly `b8 <mask> c3` | Static + Host |
| Health / speed / attack | **Implemented. Host** | `getAttribute` vt `+0xab8` → ModifiableAttributeInstance (vt `0x1287fd0`) `+0x20` setBaseValue(double). Keys: `*0x142ff40` max health (id 0), `*0x142ff58` speed (id 3), `*0x142ff68` attack (id 4), from init `0x61d8c3`. After that, `setHealth` `+0x9c0`(`getMaxHealth` `+0xa90` = `0x427e20`), the same as LivingEntity `_init` `0x41e290`. On load, the saved Attributes and Health override these values | Static + Host |
| Sounds | **Implemented: reuse of vanilla sounds only** | hurt `+0xa18`, death `+0xa20`, ambient `+0xd70` return `SoundEvent*` globals (e.g. zombie `0x1431620`/`0x1431600`/`0x14315e0`). A slot is copied from another mob's vtable only after its code bytes are verified as the `this`-free `lea rax,[rip+X]; mov rax,[rax]; ret` | Static + Host |
| Custom sound assets | **Not possible here** | Gap G11 (no audio-bank writer) | — |
| Drops | **Refused (fail closed)** | Loot is data-driven: `+0xd80` returns a native loot-table `ResourceLocation*` (creeper `0x140d440`). There is no loot-table registry hook and no `dropDeathLoot` RE | Static |
| AI goals (added on top of the base AI) | **Implemented for 7 kinds. Host** | addGoal `0x2d6260(sel, prio, Goal*, canDelete)`: WrappedGoal `{goal, prio, running +0xc, canDelete +0xd}`, goalSelector Mob+`0x800`, targetSelector Mob+`0x850`. Ctors with sizes: Float `0x2e9180` 0x18, WaterAvoidingRandomStroll `0x77f550` 0x48, LookAtPlayer `0x43a0c0` 0x48 (Creeper's args 8.0f, 0.02f, 0x168, 0x168), MeleeAttack `0x462f20` 0x60, NearestAttackableTarget `0x4a7970` 0x80 (ti, 1, 0, 1), AvoidEntity `0x3d290` 0x68 (6.0f, v, 1.2v, 3.0f), Breed `0xe2950` 0x48. typeinfo Player `0x12a5440`. Any other class's typeinfo is read from its vtable[-1] | Static (argument registers checked in each ctor prologue) + Host |
| Replacing base goals | **Not done (unsafe)** | `registerGoals` (`+0xcd0`) is tail-called by `Mob::_init` `0x486b80` inside the constructor, before our vptr swap. Concrete classes keep raw pointers to their own goals (e.g. Zombie), so deleting them would leave dangling pointers | Static |
| PANIC / FLEE_SUN / TEMPT goals | **Refused** | PanicGoal is inlined in its callers (Pig `0x52d0d0`: Goal base `0x2e7b10` plus a vptr write). TemptGoal `0x6d7030` takes a by-value 16-byte item set. The FleeSunGoal ctor `0x2e7a60` is not analysed | Static |
| Renderer / model | **Implemented: the base renderer is shared** | Dispatcher = `Minecraft+0x220` (0x140 bytes, ctor `0xbd36c0`, set at `0xc72b0f`). The map is at dispatcher+0, `operator[]` `0xbd5220`. `getRenderer` `0xbd54c0` does an **unchecked** `find` for living masks, so the custom mask must be inserted before the mob renders | Static + Host |
| Custom 64×32/64×64 skin | **Not implemented. Refused by default** (`texture_fallback=1` uses the base skin) | Renderer vtable `+0x60` is `getTextureLocation` (CreeperRenderer `0xbb7070` → `ResourceLocation 0x145f7e8`). `ResourceLocation(int texId)` is `0xcda240`. The texture-name table is `0x1407600` (324 entries, `mob/creeper` = 29). A mob skin from memory would need the Textures memory-texture path: `MobSkinMemTextureProcessor` vt `0x133c368` is used at `0xc538b0`/`0xd635d0`/`0xd63b80`, with candidate loader `0xd63940`, but none of it is verified. Path to finish this: a private renderer vtable copy with `+0x60` returning our own ResourceLocation, plus a memory-texture upload | Static |
| Natural spawning | **Implemented. Host** | `Biome::biomes[256]` `0x1409fc0`. MobSpawnerData (0x48 bytes, vt `0x1214758`): `+8` weight, `+0x10` u64 mask, `+0x18/+0x1c` min/max, `+0x20` empty string (cap 15 at `+0x40`). Lists are `vector<MobSpawnerData*>`: monster `+0x78`, creature `+0x98`, water `+0x118`, ambient `+0x178` (Biome ctor `0x83e60`, ocean `0x4d6390`). Spawning creates the entity by mask (`0x283770`) | Static + Host |
| Spawn eggs | **Implemented: base mob colours and name** | `0x28cba0(out[0x98], u32 code, name, colour1, colour2, 0)` → egg map `0x1415e78` (keyed by ResourceLocation). The egg spawns by name (`0x66ce50`). `code` is FNV-1a of the UTF-16LE string `"IDS_"+NAME` (49 of 56 match the registry name). Colours are eMinecraftColour enums (colour table), not RGB, so custom RGB is impossible without a colour-table extension | Static + Host |
| Display name | **Base mob name** | `getName` `0x282710`/`0x282910` translates `entity.<OldName>.name` from the old-name vector, and we register the base's old name. A custom name needs a localiser entry (G8) | Static |
| Hitbox size | **Refused** | Set inside each native ctor | — |
| Client / server sync | **Local only** | AddMob (24, reader `0x183170`) carries a VarInt type, and the client creates the entity by numeric id (`0x28d1b0`, handler `0xb9a46e`), so split-screen and same-process hosting work. Attributes packet 44 carries the 64-bit mask (ctor `0x1a3ba0`). The PC server (`neo-ps4-mob-candidate`) does not know ids 150+ or the custom masks (G14) | Static |
| Base mobs | 56 rows | Excluded: ender_dragon (multipart special case in `0x283770`), wither, giant, endermite/pillager/ravager (masks use bits 32/33), armor_stand, and every non-living entity | Static |

## Phase / call-site contract (lead)

Each call is made exactly once, in this order. Return values: 1 = done; 0 = refused, nothing game-visible written; -1 = uncertain (keep storage, never retry).

1. `le195_configure(&m, base, &natives, &mem, &opt, plan, err, cap)`. Call it after image attestation. It only validates. It assigns ids 150+ in plan order and writes `plan->ext[i].id`. Use `le195_check_entity()` as the backend `check_ext` for `LCE_EXT_ENTITY`. Bind natives with `le195_natives_bind(&natives, base)`. On the console, `mem` is the protection-checked read/write pair, and an 8-byte write must be one atomic store.
2. `le195_publish_types(&m)`. Call it on the startup thread **after `0x28dc10` returned** (call site `0x477d1b`, right after the item phase `0x477cc8`) and **before any Level exists**. It checks that the registry is populated (ids 50/90/200), that ids 150+ and the masks are free, and that each vanilla GetType is `mov eax,mask; ret`. It then registers and reads back `REGISTRY+0xe8[id]`.
3. `le195_publish_spawns(&m, lce_wg195_biome_lookup)`. Optional. Same thread, after the biome static init `0x7f900` and before any Level exists.
4. `le195_publish_renderers(&m, *(Minecraft+0x220))`. Call it on the main thread, after `0xc72b0f` and before any world is entered. **If phase 2 returned 1 but phase 4 did not, the lead must keep world entry disabled**, because a saved custom mob would crash `0xbd54c0`.
5. Runtime: no hooks. The game calls the factory thunks (`le195_factory(k)`) and `le195_get_type` itself.

Plugin requirement: the console build must define `le195_call_factory` through `le195_natives_bind`. No `.text` bytes are changed; every write is a heap or BSS data write. Anticheat: no `.text` hash change.

## Limits

16 custom types and 8 extra goals per type.

## Open RE

- A native-execution fixture of `0x28d9c0` was attempted. The image was mapped RWX on the host, relocated, and `malloc`/`free`/`memmove`/16-bit `wmemcpy` were patched into the PLT GOT. It faults inside the large TU static initializer `0x2d3040` at `0xb7a9a4` (called from `0x37a0d4`) on an imported, unrelocated C++ RTTI data symbol. Finishing it needs those imports resolved or the EntityIO containers constructed by hand.
- Custom skin upload (see the table above).
- Loot tables.
- Replacing goals.
