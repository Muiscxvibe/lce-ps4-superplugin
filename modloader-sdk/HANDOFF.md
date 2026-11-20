# HANDOFF: SDK content helper (2026-11-19, end of the seventh session)

State at this handoff (live tree):
- make test: ALL HOST TESTS PASSED (re-check after the 1.11 diff lands). build-ps4: 3b05f006…fe06, 84 undefined (API 1.10).
- **Task 1 (vehicles: boat items, API 1.10): LANDED** through the owner (VERIFY OK). Fixture `tests/fixtures/boats` 1.0.0,
  mod.lcm 3a95abf1fcdaa57d0cff78b24e9a383639c422274ed78af2fd2e12d8cc6338c8 (confirmed from the live build). Details: items195 STATUS
  "Boat item" row, API_STATUS "Boat items" row, MODDING `boat`, PORTING 4a.
- **Task 2 (multi-box shapes, API 1.11): header diff APPLIED by the owner on the live tree** (live test_loader 235/0, blocks195 575/0 + Unicorn 9/0,
  fixture hashes confirmed from live builds; wait for/ask the owner's full make test + verify-patch report).
  - Diff: `scratchpad/stage/boxes-1.11.diff`. The staged tree is `scratchpad/stage/boxes` (make test green, build-ps4 ccff8918…, imports same).
  - Check it landed: `grep -c BLOCK_BOXES_1_11 include/lce_mod_api.h` (0 = not applied). If not, resend; rebase the diff if the owner files moved.
  - Live already: blocks195 code + install pins + `tests/test_unicorn_boxes.py` (9/0, in test.sh), packer `boxes`/`variant_boxes`/`rotate_boxes`
    + BLCK4 writer, test_loader §Q (guarded), fixture `tests/fixtures/shapes` 1.0.0 (mod.lcm 74bc3762c29211b494614946f675f082277f50ac9724c4630f010608d654f3bb, live build; in PKGS), docs (MODDING, API_STATUS row, blocks195 STATUS §8).
  - Not done: Creative variants (needs an aux in the r72 catalog entry + the shipping Creative builder; proposal in blocks195 STATUS §8, owner/coordinator
    decision), connected fences/panes, player-facing stair placement, hand/inventory render of shape 10 (untraced).
  - Both fixtures' sha256 were sent to the coordinator (main) for publishing.
- **Next, in order:** 3 recipe types/machines without fuel, 4 screens from existing menus, 5 armour layers/mob skins/loot/particles.
  - Task 3 plan (not started; no RE yet): there is no container service. Machine = 9/27-slot storage block + LCE_EV_BLOCK_TICK/server tick +
    new services `container_get/container_set(level, x,y,z, slot)` through the BlockEntity Container vtable (RE: level getBlockEntity, Container
    getItem/setItem/getContainerSize/setChanged slots on DispenserBlockEntity/ChestBlockEntity) and a declarative mod.json `"machine_recipes"`
    (input item+count -> output, ticks) run by the SDK on ticks. Header/services change through the owner.
  - Task 4: see "Custom screens" RE below (openContainer generic-menu rule). Task 5: see Known gaps.
- Coordination: reply to the coordinator via SendMessage `main` after each task; owner = `a2e30c502be656630`.

## Rules

- No PS4 / FTP / 192.168.1.175. No installs, publishing, pushes or restarts. Never add Claude or Co-Authored-By attribution.
- The loader-owner helper (agent id `a2e30c502be656630`) owns `loader/retail195/*`, `include/lce_mod_api.h`, `loader/loader.cpp`,
  `loader/backend.h`, `integration/sdk_boot.cpp` and `id_space_195.h`. Send it exact diffs with SendMessage, and guard code
  with feature macros until its change lands.
- Do **not** apply `server/mod-channels.patch` to the live `neo-ps4-mob-candidate` tree. The coordinator will integrate it
  after the stress-test work lands.
- Skip fuel entirely (coordinator instruction).
- Cite every retail address with an audit pin (`modules/events195/tests/audit.py`, services195 `kPins`, blocks195 code pins).
  Use Unicorn tests on the real code where feasible. Keep MODDING.md, API_STATUS.md, the module STATUS.md files, the Lua
  bindings and `tools/autoport` in sync.

## RE tooling (scratchpad, may be cleared)

- `/tmp/claude-1000/-home-codey/60a3b6db-c449-419e-94de-a1f64da20c0f/scratchpad/rx/p "<python>"` runs Python with `rx.py`
  loaded. It provides `dis(addr, n)`, `q(addr)`, `by(addr, n)`, `callers(target)` (cached call index), `vt_of('ClassName')`
  (RTTI to vtable), `funcstart`, `find_bytes`, `wstr_at`.
- `.../scratchpad/re/lea.py 0xTARGET` lists RIP-relative `lea` references; run it with the analysis venv python.
- Image: `/home/codey/minecraft-ps4-research/private-direct-legacy-capture/legacy-195-analysis.elf`, base 0.
  - Typeinfo vptrs are unresolved imports there (value `0x10`).
  - Some `.bss` pointers are zero (static init has not run): the EntityDataAccessor id pointers are at `0x1415a38..0x1415a88`.

## Done in this session (second helper, 2026-11-18)

Earlier (first helper): API 1.5 entity NBT, 9-slot storage blocks, API 1.6 client networking — see events195/blocks195 STATUS.md.


- **Task 1: API 1.7 block variants and box shapes.** The header, loader and BLCK3 changes landed through the loader owner.
  - lce_block_desc gains `variant_count` (2..16), `shape`/`box[6]` (1/16 units), `variant_names` and `variant_textures`, plus
    `LCE_CAP2_BLOCK_STATES`.
  - blocks195 (STATUS.md §7):
    - Variants: ColoredBlock state slots, MultiBlockItem `+0x58`, the stacked byte `+0x31`, per-variant sprites (`+0x458` dispatcher).
    - Box: `+0x178` returns an AABB (`0x170`); face shapes come from `+0x1a0`.
    - NO_COLLIDE: `+0x1c8` = `0x139170`. TRANSPARENT: the GlassBlock slot set.
  - lce_modpack: `"variants"` and `"box"`. Porter: `describe.block_shape` (slab-like bases, constant getBoundingBox/getShape boxes).
  - Tests:
    - blocks195 `test_states_shapes`.
    - `test_loader` section M (fixture `tests/fixtures/blockstates`).
    - test_autoport `BlockShapeTests`.
- **Task 2 (partial): 27-slot storage.**
  - The mod block is a real ChestBlock(0) with the Block cube slots restored (STATUS §4); test `test_chest_storage`.
  - Packer, porter and MODDING are updated.
  - **Not done: custom screen title** (leads below).
  - 54 slots (two adjacent 27-slot blocks forming a double chest) is unverified.

## Done in the third session

- **Task 2 finished (static + host, blocks195 STATUS §4 "Titles, the double chest and the chest model").** Private EntityBlock table for
  both storage kinds; `b195_new_block_entity_dispatch` wraps `0x175800`/`0x208ef0` and calls setCustomName `0x597b30` (title = display name,
  persisted as CustomName). Chest BEs get a private vtable (+0x70 view distance = -1) so the BE pass `0xb2b9d0` culls the chest model.
  54 slots = stock pairing (same Block object, `0x178b17`). New natives `chest_new_be`, `disp_new_be`, `set_custom_name` (module header only).
  Open: console run; a BE re-created from NBT by the type factory keeps the stock vtable (model may reappear after reload; title survives).

## Done in the fourth session

- **Task 3: per-mod config files (API 1.8).** MODDING §8.5, API_STATUS §0 row "Config files".
  - `loader/config_store.h` (header-only): a TOML-subset parser plus typed lookups. Bad lines are skipped and counted; no `strtoll`, so the plugin
    link gains no import. Test: `tests/test_config.cpp` (65 checks, fuzz). It also reads the packer's `build/pkg/allcontent/config` back.
  - **Owner diff (applied by `a2e30c502be656630`):** `/tmp/claude-1000/-home-codey/60a3b6db-c449-419e-94de-a1f64da20c0f/scratchpad/stage/config-1.8.diff`
    (scratchpad, may be cleared; the full text was in the SendMessage).
    - Header: `LCE_HAVE_CONFIG_1_8`, `LCE_CAP2_CONFIG` (bit 25), and `config_get/int/double/string` appended to `lce_api`.
    - `loader.cpp`: reads `<root>/<id>/config` in DISCOVER, logs warnings, sets the loader-provided cap bit.
    - `loader.h`: `lce_loader_set_config_text`.
    - A staged copy with the diff passed `test_loader` (182 checks, new §N), `test_config` and `./build-ps4.sh` (imports identical).
  - Lua: `lce.config(key[, default[, min, max]])` in `modules/script/lce_script.c` (guarded). Test §2f in test_script.cpp.
  - Packer: mod.json `"config"`; `build` writes `<out>/<id>/config` and removes a stale one. Test: `test_tools.py` `test_config_section`.
    The `allcontent` fixture has a config section.
  - Porter: `tools/autoport/configs.py`.
    - It maps ForgeConfigSpec/ModConfigSpec builders and 1.12 `Configuration`. Chained calls on a Builder *parameter* return Ret; `_root()` follows them.
    - It writes a `CFG["key"] = lce.config(...)` header into the generated main.lua.
    - Test: `ConfigPortTests`, with stubs added to the f114/neo/f112 jars.
  - Open: whether the catalog installer should overwrite a player-edited `config` on update (MODDING §10).

## Done in the fifth session (2026-11-19)

Coordinator order for this session: 1 G1 terrain, 2 non-living entities (projectiles, then vehicles), 3 recipe types/machines
without fuel, 4 screens from existing menus, 5 multi-box shapes + creative variants, 6 armour layers/mob skins/loot/particles.

- **Task 1, G1 terrain textures for SDK blocks: LANDED** (loader owner applied it, VERIFY OK, lcemodloader.o 9db4de91…).
  - `r72 shared_terrain_195/runtime.cpp` (live edit, hunk in shipping-plugin.patch): 4 weak `lce_sdk_terrain_*` refs; SDK tiles packed
    after the 2 fixed tiles in the existing buffers (`sdk_fit`: (2+n)·size²·4 <= tiles0, n <= 62); bind at first_index 2 after Lucky/Steel
    (rc 0 = fallback, -1 = fail the reload); detach also calls the SDK. Pristine sha 5129389b…; unpatched build = prebuilt md5 84d1a800.
  - composite `lce195c_terrain_*` + lease forwarding of b195 terrain_held/uploaded; sdk_boot `lce_sdk_terrain_tiles/wanted/compose/bind/detach`;
    super.cpp installs the terrain owner in SDK-only sessions when wanted; build-crashlog sdk-block rebuilds `shared_terrain_runtime.o`.
  - Tests: `tests/test_terrain_atlas195.cpp` (41355), composite seam. INTEGRATION §4 text-hash state 4 (the 4 existing terrain hooks).
  - Coordinator must apply `integration/upgrade-w1h.patch` to the live tree.
- **Console triage (08:06 run, mixed Lucky/Steel + blockstates/newretail/retailall/rubyretail):**
  - 8 "red squares" were correct: the fixture `gem.png` was solid red. Fixtures now have distinct art (generator kept in the
    scratchpad `art/gen.py`), all four fixture mods bumped to 1.0.1 (retailall d55c74cd…, newretail 0d24ec61…, blockstates 7c7adc70…).
  - **Real bug: no SDK block was ever published.** The plugin loads before the eboot's static ctors, so the registry global is null at
    both SDK hook calls. **W1b** in blocks195 (live, `test_hook_relro_slot`): `b195_set_patch_ro8` + install patches the registry CLASS
    vtable slot `0x1218840+0x28` (RELRO) when the global is null; restored at id 365 before publish. **Needs the one-line composite diff**
    (`scratchpad/stage/w1b-blocks-owner.diff`: `b195_set_patch_ro8(&g_blocks, c->bind.biomes_mem.patch_ro8, ...)` after configure),
    LANDED (composite195.cpp line 430; make test, build-ps4 84 undefined, verify-patch VERIFY OK at the handoff). Console: expect `LCE_SDK blocks_hook=1 waiting=0`, `phase=blocks rc=1`.
  - The invisible row-3 Creative slot is not SDK content (all 20 catalog entries accounted for); ask for its hover name.
- **Task 2, projectiles: module done, not wired.** `modules/projectiles195` (STATUS.md has the mechanism, pins, limits): host 48, image pins 34,
  in `make modules-test`. Header/composite proposal (API 1.9: `LCE_ITEM_FLAG_THROWABLE`, `projectile_*` item fields, `LCE_EV_PROJECTILE_HIT`,
  `LCE_CAP2_PROJECTILES` bit 26) sent to the owner. Next: after the owner decides, wire it (prepare: configure; item window: adopt BEFORE the
  ITEM_USE route; after_items: install; hit callback through services), then packer `"throwable"`, porter, Lua, MODDING. Vehicles not started.
- Tasks 3-6 not started.

## Done in the sixth session (2026-11-19, continued)

- **Console blocks bug FIXED (live, landed with owner log line).** Console: `phase=blocks rc=-1 variant default state is not data 0` killed all 5 blocks.
  Cause: 0x924c0 makes states[0] the default (0x924e0..e7); on the real image that is not WHITE. ColoredBlock ctor 0x1a96d0 re-registers
  (any 0x7a050 @0x1a9714, setValue COLOR WHITE @0x1a9762, registerDefaultState 0x7a040 = store +0x190 @0x1a9779). blocks195 now stores
  getStateFromData(0) 0x1a9850 into +0x190 for variant blocks; pins 0x924c0..0x924ee, 0x7a040, the 3 call sites. Per-block isolation:
  `construct_one`; failed block -> `dropped`/`drop_reason`, not registered; only all-fail = uncertain. Owner added
  `LCE_SDK block <key> dropped (not registered): <reason>` (lcemodloader.o bb273ca1…). Fixtures unchanged (no republish). blocks195 488 checks.
  The fake 0x924c0 now orders states (i*7+3)%16 (old fake hid the bug).
- **Projectiles (task 1): everything done; composite/build/package diff with the owner** (`scratchpad/stage/projectiles-1.9.diff`, 669 lines;
  applied to a fresh live copy: make test, build-ps4 c32faf53… 84 undefined identical, verify-patch OK). If the owner has not applied it, re-check
  `grep -c p195_ loader/retail195/composite195.cpp` (0 = not applied) and resend. Design (see projectiles195 STATUS §3):
  - items195 builds THROWABLE generic items as SnowballItem copies (`lce195x_set_throw_use`, `use_base`); events195 routes ITEM_USE over
    p195_item_use (`lce195_ev_allow_item_use_original`); `p195_bind_item` (no write) + `p195_install` in after_items.
  - hit -> `lce195_ev_level_call` (level frame; only inside the server scope tick) -> services damage/fire/effects/explode -> LCE_EV_PROJECTILE_HIT.
  - Package ITEM3 (=20, 32 bytes), MAX_SECTIONS 20. Packer `"throwable"` (live). Lua `ProjectileHit` (live). Porter `throwable_info` +
    entity "ported as projectile" (live, ThrowableTests). Fixture `tests/fixtures/throwables` 1.0.0 (live; enters PKGS with the diff).
    New fixture mod.lcm sha256 7c7deb51f28c6d6883d252bb7c9c249003ca79d67da262b19c10296225a85bdb (built in scratchpad; not published).
  - Docs updated: MODDING §3.2 + §4.1 + §8.3, API_STATUS row, PORTING §6.4, INTEGRATION §4 (in the diff), projectiles195 STATUS.

- **Vehicles (task 2): feasibility done, not implemented.** Feasible part = mod *boat items* placing a stock boat of a wood type:
  BoatItem ctor `0xd7640` = Item ctor `0x354d80`, vptr `0x1219f18` (from lea at `0xd7652`), `+0x9c` = boat type int, `+0x18` stack = 1;
  object fits the generic 0xa0. Vtable diffs vs Item: `+0x10` `0xd86d0`, `+0x18/+0x20` dtors `0x13c340/0x13c3c0`, `+0x28` `0x3aed80`,
  `+0x40` use `0xd7c00`. Same pattern as the throwable class swap in items195 (class_vt = BoatItem, write +0x9c after the Item ctor);
  needs a descriptor field (`vehicle`/boat type 0..5) -> header/package change through the owner (could use ITEM3's reserved word if
  amended before it lands, else API 1.10). Custom vehicle models/speeds: impossible (Boat renderer per type; physics constants in code).
  MinecartItem ctor `0x476240` stores a type pointer at `+0xa0` (object > 0xa0) and registers a dispenser behaviour (alloc 0x10): not attempted.

- **Multi-box shapes (task 5): RE started (rx-verified, not pinned).** Block vt `+0x190` (default `0x92ef0`) returns a box LIST
  `{AABB **data; uint32 count}` (default: a TLS one-element list from `+0x178`, data[1]=data[2]=0); Block `+0x188` (`0x92c90`) iterates it
  (`cmp dword [ret+8],0`, `data[i]`) per face via `0x200600` (direction axis). StairBlock `0x12d2af8` overrides `+0x190` (`0x676940`, builds
  the list from state via `+0x150` getValue) and `+0x170`/`+0x180`; FenceBlock `0x1250fe8`/ThinFence(pane) `0x12e09a0`/HalfSlab `0x1264aa8`
  override `+0x158`, `+0x178`, `+0x188`. Plan: per-block private `+0x190` dispatcher returning a module-owned static list of AABBs (0x170)
  built at construction (needs: all `+0x190` callers listed with `callers` on the slot users, the list layout pinned, Unicorn on `0x92c90`
  with our list). Facing-dependent stairs need state (variants) -> later. Top slab = existing single `box` already.
- **Tasks 3, 4, 6 not started** (see the RE notes below for screens).

## Next, in the coordinator's order

4. **Non-living entities.** RE started (verified with `rx`, not yet pinned in an audit). Plan and facts:
   - Throwables are cheapest. Snowball vtable `0x12ca530`: `+0x860` shoot (stock `0x6e15a0`), `+0x878` getOwner `0x6e36c0`, `+0x880`
     onHit `0x637490` (the last primary slot; `+0x888` = offset-to-top -0x440 of the secondary vtable). `+0x3d8` = handleEntityEvent (3 = particles).
     The object is 0x4c0 bytes (`operator new 0xe6cfd0`), and the ctor `Snowball(level, shared_ptr<LivingEntity>)` is `0x6371a0`.
     Its callers are SnowballItem::use `0x637b46` and `0x63a2b0` (dispense, probably).
   - SnowballItem vtable `0x12cae00`, `+0x40` use = `0x637970` (sret, this, level, shared_ptr<Player>*, hand).
     - Sequence: `getItemInHand +0xae0`; shrink `0x3557d0` unless instabuild `[player+0x94b]`; sound level vt `+0x1c0`; `isClientSide [level+0x248]`.
     - Then the ctor; shared_ptr control-block vtable `0x12dacc0+0x10`; enable_shared_from_this `0x6931d0`.
     - Then shoot vt `+0x860` (xRot `[p+0x1a4]`, yRot `[p+0x1a0]`, 0, 1.5, 1.0); `addFreshEntity` level vt `+0x1d8`; award stat player vt `+0xe00`.
   - onHit: HitResult `+0x20` is the hit entity (shared_ptr). Blaze mask `0x5d540045` gives 3 damage. `DamageSource::thrown(self_sp, owner_sp)` is
     `0x1f26e0` (also called from `0x6e3c0a`, `0x6e4415`). Self comes from `0x68e60` (shared_from_this), the owner from vt `+0x878`.
   - Proposed design (no `.text` writes, no ABI copy of `use`):
     - A mod "throwable" item gets the stock `0x637970` behind a wrapper that sets a thread-local "pending kind".
     - One RELRO CAS (the biomes195 `patch_ro8` precedent) of the Snowball vtable word `0x12ca530+0x860` goes to a hook. When pending, the hook
       swaps the new Snowball's vptr to a per-kind private copy of the vtable, whose `+0x880` runs declarative on-hit effects (damage via `0x1f26e0`
       + hurt `+0x248`, effect, fire, explode) and a new `LCE_EV_PROJECTILE_HIT`, then the stock `0x637490`.
     - Limits: renders as a snowball (the 1.95 renderer is per type, with no item-sprite entity data: there is no ThrowableItemProjectile RTTI).
       A projectile saved in flight reloads as a plain snowball.
     - Needs from the owner: a header ext type `projectile` + the event, and composite wiring.
   - Vehicles: Boat vt `0x1219678`, BoatItem `0x1219f18`, Minecart `0x12809a0`, MinecartItem `0x1284218` are located only.
     Other projectiles located: Egg `0x12e1800`, EggItem `0x12400f8`, Arrow `0x120a748`, ThrownPotion `0x12e3270`, Enderpearl `0x12e20d0`, fireballs `0x1274208`/`0x12c9148`.
5. **Recipe types / machines without fuel.** Not started. Do not touch fuel.
6. **Custom screens from existing menus.**
   - openContainer gives any Container that is not an InteractionObject the generic chest screen, with rows = getContainerSize()/9 (ChestMenu `0x17a750`).
   - InteractionObject containers send their own menu type (vt `+0x28`).
   - Dispenser screen: player vt `+0xd70` with a DispenserBlockEntity.

Other RE facts:
- Block vt `+0xe8` is getRenderShape: 0 = cube model, 1 = cross (FlowerBlock `0x1391c0`), 0x16 = entity-rendered (chest). A "cross" plant shape is one slot away.
- BlockStateImpl vtable `0x1218f90` (`+0x18` = owner block). State `+0xf8` goes to block `+0x1d0`; state `+0x110` goes to block `+0x178`.

## Known gaps / notes

- **Client particles:** the 1.95 ParticleType ids are an int-keyed enum (map `0x142e1b8`) with almost no name strings, so a
  Java-name→id table could not be verified. Left as a documented gap.
- The keybind pad filter (`lce195_keys_filter`) is still not wired into the shipping pad hook (API_STATUS). The composite's
  `lce195c_net_tick` from `lce_sdk_ui_tick` is what installs the client packet hook on remote clients.
- `entity_data_*` relies on the in-order walk agreeing with the set size. A corrupt tree is refused before any write.
