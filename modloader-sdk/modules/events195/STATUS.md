# events195 + services195 status (CUSA00265 1.95)

Event sources, runtime services, commands, keybinds and localisation for the
retail 1.95 backend. All RVAs are from image `c675e6ce…029f` (base 0 in the
reconstructed ELF). No `.text` is patched by this module: every event source is
a data/vtable hook or a subscriber on the existing console-proven call hooks.

Evidence: **console** = proven by the shipping r72 stack this rides; **static** =
this module's RE (disassembly, verified by `tests/audit.py`); **host** =
modelled-memory unit test only (`tests/test_ev195.cpp`, 53 checks).

## Events → `lce_event`

| Feature | Pattern / evidence | RVAs | Status |
|---|---|---|---|
| `SERVER_TICK` | `Mc195SharedScope` subscriber on the console-proven Connection-tick hook; the first retained player is the per-world "anchor" that drives one 20 Hz tick | scope `0xc859e8`, per-player `0xce4380`, singleton `0x14648d0`, ServerPlayer vt `0x1341168` | console pattern / host-tested |
| `WORLD_LOAD` / `WORLD_UNLOAD` | scope session change; optional exact signal via the epoch exit / remove transition | `0xc862e0`, `0xcc85c0` | console pattern / host-tested |
| `PLAYER_JOIN` / `PLAYER_LEAVE` | first retained tick with a readable UUID (survives respawn) = join; `ServerPlayer::remove` or 2 s of missed ticks = leave | login `0xcc3780`, remove `0xcc85c0`, UUID wstring `+0x2d8`, name getter `+0x640 → 0x54f150` at `+0x9a8` | console pattern / host-tested |
| `ITEM_USE` | per-item private vtable slot `+0x40` (stock `Item::use` 0x3aedb0); thunk borrows a view and dispatches, then always runs the original | `0x3aedb0`, item vt `0x126e638` | console pattern (Lucky use hook) / host-tested |
| `BLOCK_BREAK` | per-block private vtable slot `+0x3e0` (`playerWillDestroy`, stock 0x94800); same slot Lucky routes on the console | `0x94800`, OreBlock vt `0x1298cf0` | console pattern (Lucky) / host-tested |
| `BLOCK_PLACE` | mod `BlockItem` private vtable slot `+0x28` (`useOn`, stock 0x7d4c0). Forward the Direction pointer and test-only flag, then read back the clicked cell or neighbour after a real successful placement | Caller `0x3b7dd8–0x3b7e26`, Direction face index `+0x08`, getState `+0x38 → 0x3f2820`, state→block `+0x170` | Retail ABI audited and host-tested |
| `ENTITY_DAMAGE` (cancellable) / `ENTITY_DEATH` | private primary-vtable copy installed **only** on entities a mod spawned (`lce195_ev_track_entity`), slots `+0x248` (`hurt`) and `+0xa00` (`die`); `hurt` cancellable; `die` gated on the `+0x604` already-dead flag | `LivingEntity` vt `0x12782d8`, hurt `0x424580`, die `0x426600`, dead flag `+0x604` | static / host-tested |
| `CHAT` (cancellable) | the host player's chat is captured from the submit queue (see Commands); a static candidate data hook exists for remote players at `ServerGamePacketListenerImpl` vtable `+0x300` (stock `handleChat` 0x5ff9b0, message wstring at packet `+0x28`) | listener vt `0x13408c8`, `+0x300 → 0x5ff9b0` | host-tested (queue); static (packet hook, unverified for live multiplayer) |
| `EFFECT_TICK` | reserved: delivered by the host when a custom effect ticks; no native source wired (custom effects are not registered yet) | — | not wired |

The subscriber functions (`lce195_ev_scope_tick/finish/failed`) match the
`Mc195ScopeSubscriber` ABI and are added to the list in `super.cpp` (see
**Integration**). A scope fault latches `failed` and disables all dispatch
(`lce195_ev_scope_failed`), matching the r72 fail-closed rule.

## Runtime services (`lce_api` 1.1) — `services195`

All refuse unless the caller is on the native server thread inside a current
events195 frame (the player/level must be the frame's own, or NULL). Built on
the console-proven r72 world helpers; the three world-mutating ones are
delegated to those helpers through `lce195_world_ops` (the lead binds them to
`lucky_block_world_events_195` / `debug_give_195`).

| Service | Mechanism | RVAs | Status |
|---|---|---|---|
| `give_item` | `debug_give_195` native give into the player inventory | ctor `0x3b7720`, ref-count `0x1214808`, bind `0x68d30` | console (give) via ops / host-tested |
| `set_block` / `get_block` | `world.cpp` setBlock vtable `+0x160 → 0xcfddf0`, getter `+0x38 → 0x3f2820`, encode `0x91bc0` | — | console (Lucky) via ops / host-tested |
| `spawn_entity` | `entity.cpp` EntityIO `0x28d290`, insert vtable `+0x1d8 → 0xcfdf00` | — | console (Lucky) via ops / host-tested |
| `drop_item` | builds an `ItemInstance` (same as Lucky `item.cpp`) and calls `Block::popResource` | new `0xe6cfd0`, ctor `0x3b7720`, bind `0x68d30`, popResource `0x7c2f0`, item-by-id `0x385af0` | console pattern / host-tested |
| `send_chat` | `ClientboundChatPacket` (type custom) + `_Ref_count` + `enable_shared`, then `PlayerList::broadcastAll` (all) or `connection->send` (one) | chat ctor `0x188220`, vt `0x1228438`, packet ref-count vt `0x1220858`, enable_shared `0x14fb90`, broadcast `0xcc7310`, send `0xcbb710`, conn `+0xb60` | static / host-tested |
| `add_effect` | `MobEffectInstance` ctor `0x491aa0`, `ServerPlayer::addEffect` `+0x968 → 0xd05800`, `getEffect` `+0x960 → 0x4232f0`; instant effects refused (need `applyInstantenousEffect`) | effect registry `0x14195c8`, lookup `0x4c34f0/0x4c3250/0x4c38f0/0x4c3a10`, key ctor `0xcda2f0` | static (layout from Lucky `player_effect`) / host-tested |
| `play_sound` | `ServerLevel::playSound` `+0x1c0 → 0x3f3fe0` with the `SoundEvent*` from the registry vector `0x1430570 +0xe8/+0xf0`; range 16.0 | playSound `0x3f3fe0`, registry `0x1430570` | static (from the XP sound path) / host-tested |
| `schedule` | server-tick scheduler in events195 (runs `fn` after N anchor ticks) | — | host-tested |
| `translate` | `lce195_l10n_locale()` + per-mod string tables with `fr_CA→fr_FR→en_US` fallback | see Localisation | host-tested |

## Commands & keybinds

- **Commands.** `/name` or `/modid:name` registered with `register_command`
  (reach the backend as `LCE_EXT_COMMAND`). A chat line that the host types (or
  `lce195_cmd_submit`) is delivered as `LCE_EV_CHAT`; if not cancelled and it
  starts with `/`, it is parsed on the server thread and the reply is sent back
  with `send_chat`. `/help` is built in. **host-tested.** A data hook for remote
  players' chat (the `+0x300` listener slot above) is the console path for
  multiplayer; **static/unverified**.
- **Keybinds.** `register_keybind` chords map to pad bitmasks that avoid the
  debug menu: L3/R3 are hidden from the game only while chorded, and the touchpad
  / D-pad-hold variants require a `LCE195_HOLD_MS` hold. `lce195_keys_filter` is
  called from the existing `scePadRead` GOT entry (`debug_pad_sessions_195`,
  `mc195_pad_entry`) **after** the debug chord ran, and suppressed while the debug
  menu is visible. Key edges are queued (lock-free) and delivered on the anchor
  tick. **host-tested**; the GOT entry and debug-menu suppression are the existing
  console mechanism.

## Localisation

- Current language: `GameSettings[pad0]` at `0x14404c0`, byte `+0x90` language /
  `+0x91` locale (written by the in-game Languages menu, selector `0xa695a0` →
  `0xafc2e0`). Mapped to a `ll_CC` code by `lce195_l10n_from_xc`, derived from the
  selector's own `XC_LANGUAGE`/`XC_LOCALE` tables at `0xeeb750`/`0xeeb7b0` and the
  LCE source `App_Defines.h`. Falls back to the PS4 system language
  (`lce195_l10n_from_sce`, bound by the lead to `sceSystemServiceParamGetInt(1)`),
  then `en_US`. **static** (the XC mapping) / **host-tested** (selection logic).
- `translate()` reads the loader's `lce_plan_string` list via `lce195_l10n_load`.

## Sounds / particles

- Vanilla sound ids: `sound_ids_195.inc` (523 unique names, generated by
  `tools/gen_sounds.py`). `play_sound` resolves a vanilla name or a registered
  clone. Custom sound clones (`register_sound`, clone_of + volume/pitch) are
  **host-tested**; raw PCM is refused (no sound-bank format, gap G11).
- Particles: see `../ui195/` — a tinted clone of a vanilla particle is data;
  a new particle texture is not possible.

## Capabilities advertised

`lce195_ev_capabilities()` returns `EV_TICK | EV_WORLD | EV_PLAYER | EV_ITEM_USE |
EV_BLOCK | COMMANDS | KEYBINDS`, plus `EV_ENTITY` when the host provides a
data-word CAS and an is-code predicate. `services195` advertises `SERVICES`
separately (the backend sets the bit after `lce_loader_set_services`).

## Integration (for the lead)

1. **Subscriber.** In `super_content_steel_nbt_save_195/super.cpp`, add to the
   `Mc195ScopeSubscriber subscribers[]` list (after F3/Lucky), before
   `mc195_shared_scope_start`:
   ```cpp
   subscribers[count++] = {nullptr, lce195_ev_scope_tick, lce195_ev_scope_finish, lce195_ev_scope_failed, /*allow_paused=*/false};
   ```
   Call `lce195_ev_init(&host, lce_sdk_dispatch_adapter, loader)` first, where the
   host fills `read/write_word/is_code/thread_id/view_current/view_borrow` from the
   existing `mc195_lucky_candidate_*` callbacks and `mc195_lucky_server_current` /
   `mc195_lucky_server_borrow`. The sink forwards to `lce_sdk_dispatch`.
2. **Thunks.** When the item/block/BlockItem private vtables are built (items195),
   call `lce195_ev_route_item/route_block/route_block_item` with the stock slot
   value, and write the thunk address into the copied slot. For mod entities
   spawned via services, `spawn_entity(track=1)` calls `lce195_ev_track_entity`.
3. **Services.** `lce195_services_init(natives, world_ops, resolver)` then
   `lce195_services_fill(&services_api)` then `lce_loader_set_services(l, &services_api)`
   and OR `LCE_CAP_SERVICES` into the backend caps.
4. **Commands/keybinds.** Drain `LCE_EXT_COMMAND`/`LCE_EXT_KEYBIND` from the plan
   into `lce195_cmd_register` / `lce195_keys_register`. Call `lce195_keys_filter`
   from `mc195_pad_entry` after the debug chord; `lce195_cmd_submit` from the
   existing `/lce-*` chat intake, or let the `+0x300` data hook feed remote chat.
5. **Exceptions.** Build `ev195.cpp`/`services195.cpp` with `-fexceptions`
   (`ps4check.sh` does): the item/block/entity thunks must let a native game
   exception unwind through the frame guard, exactly as the r72 server scope
   requires. The SDK core loader stays `-fno-exceptions`.

## Not done / uncertain

- The `send_chat`, `add_effect` and `play_sound` native call sequences are
  **static** (disassembly + layout from console-proven r72 helpers), not yet run
  on the console. They fail closed (verify vtables/refcounts before every native
  call) but gameplay is unverified.
- The multiplayer chat/command data hook (`+0x300`) is a static candidate; the
  single-player host path (submit queue) is the tested one.
- `EFFECT_TICK` has no native source until custom effects register.

## API 1.2 additions (2026-11-18, in progress)

| Feature | Evidence | Status |
|---|---|---|
| `LCE_EV_BLOCK_USE` | Private Block vtable `+0x2c0` (plain Block/OreBlock use the false-returning `0x940b0`). Client and server callers pass Block, Level, BlockPos, BlockState, `shared_ptr<Player>*`, hand in `r9d`, three hit floats in `xmm0..2`, then Direction and simulate in stack slots 0 and 1. A handler can consume a server click before the original runs. Simulation always delegates to the original | Retail ABI audited (`tests/audit.py`) and host-tested |
| Payload context (`level`, `x/y/z`, `hand`, `face`) | Entity position `+0x170` doubles, Level from the thunk arguments | static |
| `heal` | getHealth `0x4244d0`, setHealth `0x4244f0`, getMaxHealth `0x427e20`, isAlive `0x427240`. Byte pins from r72 `nbt_health_player_195` (console-proven) are checked at init | static |
| `set_fire` | fire timer int32 `+0x1c0`, clearFire `0x2762a0`, countdown `0x275f41`. Pins from r72 `nbt_fire_player_195`. CAS write; never shortens | static |
| `consume_held_item` | Inventory `+0x7d0`, selected `+0xfc`, setCount `0x3626c0`, setChanged `0x380b70`. Pins and ownership checks from r72 `nbt_item_count_195` | static |
| `explode`, `set_block`, `spawn_entity` (+ entity tracking), chunk-loaded check | `integration/r72_world_bind195.cpp` → r72 `mc195_world_explode` / `place_block_update` / `spawn_entity_ex` / `loaded_chunk` (console-proven Lucky Block helpers) | bound; not run on console |
| `BLOCK_TICK` | Block `+0x1f0` = `tick` (stock `0x936c0` `ret`), `+0x1e8` randomTick `0x936b0` forwards to it (Unicorn). **Random-tick gate found**: ServerLevel `+0x280` tickBlocks `0xcf61f0` (profiler string "Tick blocks" at `0xcf556c` in the level tick) walks the queued positions, resolves the block through Level `+0x98`, and calls `tick(block, level, pos, state, level->random +0x1b8)` only if Block `+0x140` isTicking (`0x92aa0`: `mov al,[rdi+0x149]`) is true (`0xcf63a6` → `0xcf63ea`). `+0x128` setTicking `0x92a90` writes the same byte (LCE `Tile::setTicking`); no subclass overrides either slot. Scheduled ticks use the same slot (`0xcf7479`, `0xcf82eb`). `lce195_ev_route_block_tick` + `lce195_ev_set_block_random_ticking` (CAS of word `+0x148`, only behind the stock getter) | routed + flag, host + Unicorn (setTicking/isTicking); composite install pending |
| `ENTITY_INTERACT` | Entity `+0xe50` = interact (Entity `0x27b400`, Mob `0x48e460`, Animal `0x10ae0`). The only slot Cow, Sheep and MushroomCow all override from Animal except `+0x348`: Cow `0x1d21c0` reads the held item (player vt `+0xae0`), compares it with the bucket global, else calls Animal's. `+0x348` is only the prompt query (returns string hashes). Routed on the private copy of mod-spawned entities; `true` = handled | static, host-tested |
| `ENTITY_TICK` | `+0xa8` = tick: Entity `0x275680` calls vt `+0xb0`. `+0xb0` = baseTick: LivingEntity `0x41f1c0` references IN_WALL `0x1413b28` and DROWN `0x1413b38`, as Java's LivingEntity.baseTick does. LivingEntity `0x42d120` calls `0x275680` directly. Routed on the private copy of mod-spawned entities, with a level-only frame | static, host + Unicorn |
| `get_block` | ServerLevel vtable `+0x38` getBlockState `0x3f2820` + state encoder `0x91bc0` (id \| meta<<12), the r72 placement readback pair | bound in `integration/r72_world_bind195.cpp` |
| `damage_entity` | DamageSource statics in Java declaration order (initialiser `0x245f00`). `0x1413b48` = CACTUS (CactusBlock::entityInside `0x1596c0` → hurt `+0x248`, 1.0). `0x1413b60` = OUT_OF_WORLD (LivingEntity::kill `+0x70` → hurt FLT_MAX). `0x1413b68` = GENERIC (next). Object vtable `0x1236418` is checked per call. API 1.3 attacker = the frame's player: `DamageSource::playerAttack 0x1f22a0` (id `0x1e`/`0x3c`, used by Player `+0xd98` attack `0x54a1b0` and Parrot's cookie `0x52499e`) → operator new(0x40) + EntityDamageSource ctor `0x2871a0` (strong copy of the pair at `+0x20/+0x28`, vtable `0x124cb48`) → hurt → deleting dtor (vt `+8`, `0x2ba860`), the Cat `0x165d2e..0x165e38` sequence. `mobAttack 0x1f2080` exists but a mob's control block cannot be named safely, so non-player attackers are refused | static, host + Unicorn (factory end to end) |
| `damage_held_item` | ItemInstance::getMaxDamage `0x3b8730` (Item+0x1c), hurt `0x3b8750` (amount, Random; Unbreaking via `0x1fcf60`), setDamage `0x3b86e0`. Level Random `+0x1b8`. Broken → shrink + reset, as hurtAndBreak | static, host + Unicorn (getter/setter) |
| `teleport` | vt `+0x670`: Entity::teleportTo `0x284180` (moveTo `+0x218`, teleport flag `+0x378`). ServerPlayer `0xd0ca30` → connection `+0xb60` teleport `0xcc8240`, the same slot as 2.72's `+0x670`. Position readback | static, host |
| `set_motion` | vt `+0x230` Entity::push `0x27b3b0`: deltaMovement `+0x188/+0x190/+0x198` += d, hasImpulse `+0x2a9` = 1. Called with target minus current | static, host + Unicorn |
| `give_item` | GiveItemCommand `0x3333f0` builds a stack (Item::byId, ctor `0x3b7720`, bind `0x68d30`) and calls Inventory::add `0x37d4d0` with inv = `[player+0x7d0]`. The module does the same per max-size stack and drops any remainder with Block::popResource at the player's feet. The container broadcast syncs the slot | static, host |


## API 1.3 additions (2026-11-18)

| Feature | Evidence | Status |
|---|---|---|
| `LCE_EV_BLOCK_NEIGHBOR` | Block `+0x208` neighborChanged (stock `0x936f0` `ret`). TntBlock `0x6eda60` and NoteBlock `0x4d4a90` both start with Level::hasNeighborSignal `0x401bf0(level=rdx, pos=rcx)`; DoorBlock `0x210250` overrides it too. Overridden by Diode, Dispenser, Door, Note, PistonBase, RedStoneWire, BaseRail, Sponge, Hopper, Tnt (28 of 37 blocks, none of the plain ones). ABI (block, state, level, pos, Block* from, BlockPos* fromPos); `face` = side of fromPos (Java Direction order) | routed, host |
| `LCE_EV_BLOCK_STEP_ON` | Block `+0x2c8` stepOn (stock `0x940c0`). Overridden only by Magma `0x43eed0` (reads `[rcx]` = entity), Slime, Campfire, TurtleEgg. ABI (block, level, pos, `shared_ptr<Entity>*`) | routed, host |
| `LCE_EV_BLOCK_ENTITY_INSIDE` | Block `+0x328` entityInside (stock `0x94180`). Cactus `0x1596c0` (`[r8]` = entity, hurt CACTUS), Web, SweetBerryBush, pressure plates, Campfire, SoulSand, Lily, Liquid, Tnt. ABI (block, state, level, pos, `shared_ptr<Entity>*`) | routed, host |
| `LCE_EV_ITEM_INVENTORY_TICK` | Item `+0xc8` inventoryTick (stock `0x3af000` `ret`). MapItem `0x445340` checks `[rdx+0x248]` isClientSide first (Unicorn: unmapped stack/entity pointers are not touched on a client level), copies `[rsi]`/`[rsi+8]` (stack pair) and `[rcx]` (entity pair), keeps `r9d` (selected). Payload: `aux` = slot, `hand` 0 = selected, 2 = not held | routed, host + Unicorn |

| `damage_entity` mob attacker | Every Entity keeps `weak_ptr<this>` at `+0x8/+0x10` (enable_shared_from_this): Cat::doHurtTarget `0x165cbf` locks it with `0x68e60(out, obj, ctrl, throw)` (= weak_ptr::lock, `_Incref_nz` CAS loop; with throw = 0 an expired pointer leaves `out` empty, branch `0x68f75`) and passes it to `DamageSource::mobAttack 0x1f2080` (id `0x1d`). The service does the same: lock, mobAttack, hurt, deleting dtor, release. Requires `[att+8] == att`, a living entity of the frame level, not removed | host + Unicorn (lock live/expired, mobAttack holds the mob) |
| `spawn_particles` (service, header field pending) | ServerLevel::sendParticles `0xcfd290(level, ParticleType*, bool override, count, int* data, ndata, x, y, z, dx, dy, dz, speed)` builds ClientboundLevelParticlesPacket (ctor `0x18d080`, vtable `0x1228ab8`) and sends it to players in range. ParticleType has no RTTI; its int id is at `+0x28` (getId `0x526750`). byId `0x526780` uses `operator[]` on the map `0x142e1b8` (inserts on a miss), so the module walks the map's node list read-only (sentinel `[map+8]`, key `+0x10`, value `+0x18`, from `0x5267d0`) and re-checks the id | host |

API 1.12 adds `spawn_tinted_particles` for the stock BlockRemover particle. The retail caller at `0x87dd10` converts packed RGB to three channels divided by 255 and reads its ParticleType from `0x142e060`. The registry initializer at `0x526023` registers that same object by its ID. The SDK resolves the type through both routes before calling the RGB overload at `0xcfd730`. The stock packet constructor at `0x18d1a0` keeps the usual range limiter and stores a color flag at `+0x68` and three float channels at `+0x6c/+0x70/+0x74`. The client listener at `0xba4030` uses those channels when spawning the particle through the level at `0x3f42c0`. Host tests exercise validation and the frame guard. Unicorn runs the retail packet constructor and confirms the client's spawn arguments. Console rendering remains to be verified.

NBT leads (not implemented yet): ItemInstance::load `0x3b7740` / save `0x3befe0`. The tag is a raw `CompoundTag*` at stack `+0x30`
(replaced through vt `+0x30` delete, copied through vt `+0x40`). CompoundTag (vtable `0x122ede0`): contains(key, type) `0x1b7bf0`,
getCompound `0x1af120`, putString `0x1af3b0` ("id"), putByte `0x1b7460` ("Count"), putShort `0x1b7550` ("Damage"). Keys are
`std::wstring*` (assign `0x67840`).

| item NBT (`item_tag_get/set/remove`) | ItemInstance `+0x30` CompoundTag* (load `0x3b7740`, save `0x3befe0`), getOrCreateTag `0x3ba900`. CompoundTag = `std::map<wstring, Tag*>` at `+8`; get `0x1b7400`, remove `0x1b8630`, putInt `0x1b25e0`, putDouble `0x1b7910`, putString `0x1af3b0`; the full put/get map is in `natives195.h`. Tag vtables Int `0x126e2c8` / Byte / Short / Long / Float / Double `0x123cfd0` / String `0x12d83a8` (wstring at `+8`). Reads go through `get` + vtable check + `+8`; writes through the native put functions, then Inventory::setChanged | host + Unicorn (real put/get/remove on a real CompoundTag) |
| mod networking (`send_payload`, `LCE_EV_CUSTOM_PAYLOAD`) | ClientboundCustomPayloadPacket ctor `0x18ba10` (channel wstring `+0x28`, data `+0x50` freed with delete[] by dtor `0x18bab0`, len `+0x58`), sent like ServerPlayer `0xd08bf0` does (_Ref_count with virtual delete, `connection->send 0xcbb710`). Receive: ServerboundCustomPayloadPacket::handle `0x5fbfc0` → listener vt `+0x340` (stock `0xcf10a0`); events195 installs a private listener vtable on the host player once a mod channel is registered; registered channels are dispatched, all others go to the stock handler. Frame only for the host player. Client-side send/receive (remote PS4 clients running the mod) is not implemented | host (send, route, dispatch, vanilla passthrough) |

Storage leads (not implemented): DispenserBlock overrides `+0x398` (`0x53010`, block-entity lookup through Level vt `+0x10`, then vt `+0xb8`), `+0x340` (`0x43140`) and `+0x2c0` use (`0x2085a0`); `+0x3b8` returns the render shape (`2` = model). A mod container needs a block-entity factory slot and a menu-open path (player vt `+0xd80` openMenu as in CraftingTableBlock::use); blocks195 currently refuses `block_entity_slots`.

Entity NBT leads (not implemented):
- Entity::load is `0x27cc90` (vt `+0x290`); saveWithoutId is `0x27bc70` (non-virtual). Both reference "Motion", "Invulnerable", "Tags" and similar keys.
- Entity scoreboard tags are a `std::set<wstring>`: head at `+0x368`, size at `+0x370`. The only insert is `0x2c5150(out, set, hint, wstring*)`, called by load, which keeps at most 1024. No add/remove helpers exist; TagCommand looks inlined or absent.
- LCE's load drops unknown keys, so there is no Forge-style persistent data. Two possible routes:
  - Read-only entity NBT: save into a temporary CompoundTag (ctor `0x1b6df0`), read the key, delete the tag through vt `+0x30`.
  - Persistent mod data stored as `modid:key=value` entity tags: needs a tree erase (still to find), or a rebuild of the set through load.

Every route is accepted only while the slot holds the stock `ret` body (all pinned in `tests/audit.py`). The event ids are
provisional (`ev195.h`, guarded by `LCE_HAVE_EV_1_3`) until `include/lce_mod_api.h` adds them; capabilities2 reports
`LCE_CAP2_EV_BLOCK_TICK / NEIGHBOR / BLOCK_COLLIDE / INVENTORY_TICK` only once the composite has installed a route of that kind.
The block slots live on the mod block's private OreBlock vtable copy (155 slots, so `+0x328` fits); the item slot on the SDK item copy (62 slots).
Tests now: 173 host checks, audit 84 code RVAs / 28 slots, 48 Unicorn checks.

Tests: `tests/test_ev195.cpp` covers heal, set_fire, consume, explode, BLOCK_USE and the payload context (86 checks).
`tests/test_unicorn_events195.py` runs the REAL 1.95 code for Block::use, randomTick→tick, `CraftingTableBlock::use`
(simulate flag, client Level), clearFire, setChanged, setCount and getHealth. `tests/audit.py` covers 31 code RVAs, 14 vtable slots
and 5 byte pins.

Also fixed: `services_glue195.cpp` never set `api.struct_size`, so `lce195_services_fill` installed nothing and every console service reported UNSUPPORTED.

## API 1.5: entity NBT (2026-11-18)

| Feature | Evidence | Status |
|---|---|---|
| `entity_nbt_get` (read-only vanilla save data) | `Entity::saveWithoutId 0x27bc70(entity, CompoundTag*)` returns the tag (`mov rax,r14` at `0x27c903`); writes `Pos`/`Motion`/`Rotation` ListTags, `FallDistance`, `Fire` (short `+0x1c0`), `Air`, `OnGround`, `Dimension`, `Invulnerable`, `PortalCooldown`, `UUID` (vt `+0x618`), `CustomName` (`+0x650`), ..., `Tags`, then vt `+0x2b8` addAdditionalSaveData and passengers (`0x27ba70`). The module saves into a temporary tag (new `0x58` + ctor `0x1b6df0`, vtable `0x122ede0`) and deletes it through vt `+0x30` (`0x1b7010` = dtor `0x1b6ec0` + delete, the call saveWithoutId itself uses at `0x27c7e6`). Paths: `/`-separated keys, `[n]` list index, `[#]` list length. ListTag vtable `0x1278268` (getId 9), vector<Tag*> `+0x10/+0x18` (ctor `0x41cbf0`, add `0x40e850`) | host + **Unicorn** (real ctor + real saveWithoutId + real `get`; Fire, Pos[1], Motion[1], Tags; real deleting dtor frees the tree) |
| `entity_data_get/set/remove` (persistent mod data) | LCE drops unknown keys on load, so mod data rides the entity scoreboard tags: `std::set<wstring>` object `+0x360` (head `+0x368`, size `+0x370`; node left/parent/right `+0/+8/+0x10`, color `+0x18`, isnil `+0x19`, wstring `+0x20`). Saved as `Tags` (`0x27c605..0x27c6f9`) and loaded by `Entity::load 0x27cc90` (at most 1024, `0x27dac5`), which rebuilds the set with `_Erase(set, root) 0x2c50f0`, a head reset, and `insert(out, set, false, wstring*) 0x2c5150` per entry (`0x27da18..0x27db13`). Entries are `lce\|<key>=i<int32>` / `d<16 hex IEEE bits>` / `s<ASCII>`. Set: native insert (refused at 1024). Replace/remove: copy every entry out, check the tree (in-order walk agrees with the size), then the same erase + reset + insert sequence as load. Players are refused for writes | host (BST fake with the real node layout, 1024 limit, corrupt-size refusal) + **Unicorn** (real insert keeps a sorted set and ignores duplicates; real erase + reset + reinsert; the real save writes the replaced mod entry into `Tags`) |

Pins (services195 `kPins`, re-checked by `tests/audit.py`): saveWithoutId prologue + `0x27c605` (`mov rax,[r12+0x368]`), CompoundTag ctor, insert/erase prologues, erase body `0x2c5100` (isnil `+0x19`) and `0x2c511f` (cap `+0x40`), load's clear `0x27da26` (`lea r15,[rbx+0x360]`) and its calls `0x27da34` → erase, `0x27daf9` → insert. A mismatch unbinds the natives (service UNSUPPORTED).

Header: landed in `include/lce_mod_api.h` (`LCE_HAVE_ENTITY_NBT_1_5`, `LCE_CAP2_SVC_ENTITY_NBT (1u << 20)`, `LCE_CAP2_SVC_ENTITY_DATA (1u << 21)`, four `lce_api` fields after `send_payload`; loader defaults return UNSUPPORTED until `lce_loader_set_services` adopts these).
Tests now: 226 host checks, audit 92 code RVAs / 32 slots, 85 Unicorn checks.

## API 1.6: client-side mod networking (2026-11-18)

| Feature | Evidence | Status |
|---|---|---|
| Server -> client mod payloads on remote PS4s (`LCE_EV_CLIENT_PAYLOAD`) | LCE keeps the TU packet ids. `Packet::map 0x5115e0(id, ..., create)` fills `unordered_map<int, create>` `0x142dcb0` at static init (map object at `0x5115fe`, node key `+0x10` / create `+0x18` written at `0x511646`). id 250 = ClientboundCustomPayloadPacket, create `0x514e40` (registered at `0x5112af`); id 4 = ClientboundSetTimePacket, create `0x511890` (`0x50b2d7`). Remote clients build every received packet with that create function; the client thread then calls packet vt `+0x28` handle(packet, listener): payload `0x18bd90` -> ClientPacketListener (vtable `0x132eb18`) `+0x110` `0xba2a90`, SetTime `0x1a2620` -> `+0x200`. **Data hook only**: once a channel is registered, `lce195_net_client_install` CASes the two map entries to wrappers that call the stock create and give the new packet a private 11-slot vtable copy with `+0x28` routed. Registered channels are dispatched (never reach the vanilla handler); others call the stock handle. SetTime (sent every second) is the client pump | host (map walk, refusal without id 250, private tables, dispatch, passthrough) + **Unicorn** (real create `0x514e40`/`0x511890` -> real handle -> listener vt `+0x110`/`+0x200` with `(listener, shared_ptr<packet>*)`; the weak pair at `+0x18/+0x20` set by create) |
| `send_to_server` | ServerboundCustomPayloadPacket (new `0x60`, ctor `0x5fbd70`: channel `+0x28`, data `+0x50` owned, delete[] in dtor `0x5fbc50`, len `+0x58`, Unicorn), shared like every packet, sent with `ClientPacketListener::send 0xb91460` (early-out on `[listener+8]`, pinned) — the game's own client payload sender `0xc6e260` does the same. Inside a ClientPayload handler: sent at once on the client thread; elsewhere on a remote client: queued (32 messages / 64 KiB) and sent by the next SetTime pump; on the host: looped back to its own server next tick as `LCE_EV_CUSTOM_PAYLOAD` from the host player | host + Unicorn (ctor layout) |
| Threading | The host's own client never runs a mod: while its server ticks (`lce195_ev_host_server_active`: anchor view live and a tick within 3 s), client payloads are queued and dispatched on the next server tick. A remote client has no server thread, so dispatching on its client thread never overlaps another mod callback | host |
| Remote players' server-side frames | client -> server payloads now get a frame for **any** player whose own last scope view is still current on this thread (was host only), so services work on remote senders | host |
| Install points | `lce195_net_client_install()` runs from the scope tick (host), from the pad filter (bounded retry), and should be called by the loader after it forwards channels to events195 (remote clients have no scope tick). Refused (no change) until static init has filled the map and both entries hold the stock create | host |

PC server counterpart: `../../server/mod-channels.patch` (relay of mod channels between PS4 clients; see `server/STATUS.md`).
Header: landed (`LCE_HAVE_NET_1_6`, `LCE_EV_CLIENT_PAYLOAD` = CUSTOM_PAYLOAD + 1, CAP2 bits 22/23, `lce_api.send_to_server`). The composite calls `lce195_net_client_install()` after each channel registration and retries it from `phase_after_items`, `phase_late` and every `lce_sdk_ui_tick` (remote clients too).
Tests now: 259 host checks, audit 101 code RVAs / 37 slots, 98 Unicorn checks.
