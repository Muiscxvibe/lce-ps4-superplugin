# Server side of LCE|ModIds (G14): status

The protocol is described in [PROTOCOL.md](PROTOCOL.md). The client half is in `../modules/netsync195/STATUS.md`.

## Files

| Path | What |
|---|---|
| `base/` | Unmodified copies of the 3 server files the patch touches. Hashes are in `patch-base.sha256`, taken 2026-11-17 22:42 |
| `patched/` | The same 3 files with the hooks added |
| `new/Minecraft.World/LceModIdWire.h` | Wire codec. Byte-identical to `modules/netsync195/lce_modids_wire.h` |
| `new/Minecraft.World/LceModIdCore.h` | World ledger, HELLO reassembly, merge rules, RESULT/TABLE encoders. STL only, host-tested |
| `new/Minecraft.World/LceModIdRegistry.h` | Engine glue: placeholder Item/Tile, startup load, atomic persist, per-connection gate, `filter_outgoing` |
| `new/Minecraft.Client/LceModIdHandshake.h` | PlayerConnection glue: REQUEST, timeout, HELLO handling, chat + delayed kick, inventory resend |
| `server-sync.patch` | Unified diff for the original tree (`cd neo-ps4-mob-candidate && patch -p1 < .../server-sync.patch`). Regenerate it with `./make-patch.sh` |
| `tests/` | Host tests (`make test`) |

The new code is header-only, so the patch changes no CMake source lists. Every hook is inside `#if defined(LCE_PS4_195_COMPAT)`.

### Hooks (7 small hunks)

- **`PlayerConnection.cpp`**
  - include
  - `~PlayerConnection` → `forget`
  - `tick()` after `connection->tick()` → `lce_modids_srv::tick`
  - top of `handleCustomPayload` → `handle_payload`
- **`Connection.cpp`**
  - include
  - `writePS4Packet` after the local/listener check → `filter_outgoing`
  - the `stackId==6` withhold rule also withholds disallowed custom stacks
- **`ServerMain.cpp`**
  - include
  - after `BootstrapWorldForServer` and before `NetworkGameInitData` → `lce_modids_srv::startup("Windows64/GameHDD/<level-id>")`; on failure, exit 5

## Verified

- **`make test`**: `test_modids_core` passes 1104 checks with 0 failures (g++ `-fsanitize=address,undefined -fno-sanitize-recover=all -Werror`). It covers:
  - SHA-256 FIPS vectors, including a million `a`s.
  - The digest equals the LCEIDS text hash.
  - The entry codec: round trip, every truncation, and the range, charset and flag rules.
  - The world ledger: round trip; every single-byte flip is rejected; non-canonical numbers, a stored live flag, duplicate keys or IDs, and a wrong header are all rejected.
  - Reassembly of HELLOs produced by the real client module: 0, 1, 40 and 500 entries (multi-chunk). Out-of-order chunks, a version change, a type change, a session change, trailing bytes, a tampered digest, duplicates and the limit are caught. 3000 random corruptions never crash and never yield invalid entries.
  - Every merge code: adoption, conflict, ID taken (including block-item aliasing), built-in, props, locked, missing, the allow-missing waiver (and no waiver for placeable blocks), stale entries ignored, multi-failure message bounds, and capacity.
  - RESULT + TABLE for 601 entries, decoded and verified by the client.
  - Chat line wrapping.
  - It also `cmp`s the two codec copies and re-hashes `base/`.
- **Compile check with the real toolchain** (clang-cl + xwin, flags and PCH from `build/windows64-clang` via `ninja -t commands`, objects in the scratchpad only): `Connection.cpp`, `PlayerConnection.cpp` (server target) and `ServerMain.cpp` compile with the hooks. A full link was **not** done.
- **`patch --dry-run`**:
  - Applies cleanly to the `base/` snapshot, and the result is byte-identical to `patched/` + `new/`.
  - Against the live `PlayerConnection.cpp`, which the minigame engineer edited at 2026-11-18 03:08, it applies with offsets of 3–10 lines and no fuzz.

## Not verified (needs the console or a live server)

- Linking `Minecraft.Server` and runtime behaviour on the server.
  - `Item::stick` and `Tile::stone` description IDs at placeholder construction.
  - `MoveFileExA` on Wine.
  - Creating the world directory for a brand-new world.
- Delivery as whole frames. The server sends REQUEST/RESULT/TABLE as `shouldDelay` + `queueSend`, as `MC|LceNbt` does. Whether a burst of about 40 TABLE frames each arrives as its own frame at the plugin is unverified.
- A plugin without netsync passes the REQUEST frame to the retail client. The 250 layout is verified (passthrough), but that the retail handler ignores an unknown channel is assumed, not tested.
- Whether the PS4 retail client tolerates the 3 s kick and shows chat in time. The disconnect reason is `eDisconnect_Kicked`; the 1.95 enum mapping of packet 255 was not re-checked.
- The placeholder Tile's behaviour against the PS4's own block rules: harvest tool, light, collision for `NO_COLLIDE`, block entities. Server-side tool durability does not decrease, because placeholders have no `mineBlock`/`hurtEnemy`.
- `material` and `b` (resistance) semantics. `setExplodeable(b/100)`, and LCE multiplies by 3. Only server-side explosions are affected.

## Known risks

1. **Unpatched clients on a world with placed custom blocks (<256).** Chunks are sent before the 10 s timeout and are not filtered, so a vanilla or old-plugin console sees unknown block states until it is kicked. Items are gated; blocks are not. A future fix is to hold chunk sends until accept, or remap them per connection.
2. A block placed by a modded player is stored in the world. After that, every client must have that mod. This is by design (code 7), and it is clearly reported.
3. The PC LCE client is treated like an unmodded PS4: it gets REQUEST, which its handler should ignore, and it is refused on a modded world.
4. A corrupt `lce-mod-ids.txt` stops the server (exit 5) on purpose. Restore it from `lce-mod-ids.txt.bak`.
5. **Incident during this work (2026-11-18 03:04):** while building the scratch compile overlay, a symlinked directory made `cp --remove-destination` write `patched/Minecraft.Server/Windows64/ServerMain.cpp` into the original `neo-ps4-mob-candidate/Minecraft.Server/Windows64/ServerMain.cpp`.
   - The original is byte-identical in `base/Minecraft.Server/Windows64/ServerMain.cpp` (sha256 `70de2422…`, verified against the live tree just before).
   - A restore was blocked by the permission classifier and has been escalated to the user. Until it is restored, a server build of that tree fails, because the include of `LceModIdRegistry.h` has no header in that tree.
   - Restore: `cp -p modloader-sdk/server/base/Minecraft.Server/Windows64/ServerMain.cpp neo-ps4-mob-candidate/Minecraft.Server/Windows64/ServerMain.cpp`.

## Test plan for the main engineer

1. Restore `ServerMain.cpp` (risk 5). Check with `cd neo-ps4-mob-candidate && sha256sum -c ../modloader-sdk/server/patch-base.sha256`; `PlayerConnection.cpp` may legitimately differ now.
2. Apply the patch: `cd neo-ps4-mob-candidate && patch -p1 --dry-run < ../modloader-sdk/server/server-sync.patch`, then run it without `--dry-run`.
3. Build with the toolchain environment from `~/minecraft-lce-work/build-neo-server.sh`, then `cmake --build build/windows64-clang --target Minecraft.Server`.
4. Restart with `lce-playtest-de75uaq9/restart.sh`. The startup log must show `[modids] world ledger Windows64/GameHDD/world/lce-mod-ids.txt: 0 entries`.
5. Unmodded PS4 join, as a regression check: the log shows the REQUEST being sent and nothing else; the world ledger is empty, so there is no timeout kick. Play normally.
6. Build the plugin with `../modules/netsync195` and its `client-integration.patch`, and install it with an SDK mod (for example rubymod) with the game closed. Join. Expect:
   - plugin log `[netsync] server requested mod IDs: sent N entries` then `accepted ... digest ok`;
   - server log `[modids] <name>: adopt item rubymod:ruby (2283)` and `accept`;
   - the file `lce-mod-ids.txt` is created.
7. Put the mod item in a chest, then leave, restart the server and rejoin. The item must still be in the chest (it persisted, and the placeholder was registered before load). Move it around the inventory and drop and pick it up. Place the custom block if its ID is <256.
8. Change the client ledger so the same key has a different ID, or use a second console with a different install order. The join must be refused with the chat line and a kick after about 3 s. The plugin log shows `REJECTED (code 3)`.
9. With the world ledger non-empty, join with an unmodded console. Expect a code 7 refusal after the empty HELLO, or code 10 for a plugin without netsync.

## Mod channel relay (API 1.6 client networking), 2026-11-18

The PC server runs no mod code, so client-side mods on PS4s talking through a PC server need a hub. `mod-channels.patch`
(apply **after** `server-sync.patch`: `patch -p1 < server-sync.patch && patch -p1 < mod-channels.patch`; regenerate with
`./make-modch-patch.sh`) adds two header-only files and three one-line hooks inside the existing `LCE_PS4_195_COMPAT` blocks of
`PlayerConnection.cpp` (include, `forget` in the destructor, `relay` right after the LCE|ModIds `handle_payload`):

- `new/Minecraft.World/LceModChannelCore.h`: policy. A mod channel is `modid:name`, 3..20 of `[a-z0-9_:.-]`, never `mc|`/`lce|`
  (the events195 rule). Payloads over 32000 bytes are dropped; a per-connection token bucket (burst 64 messages / 256 KiB,
  32 msg/s, 128 KiB/s) drops floods. Vanilla channels go on to the vanilla handler untouched.
- `new/Minecraft.Client/LceModChannelRelay.h`: glue. A mod channel message from one client (PS4 packet 225 -> PC 250) is
  re-sent to every *other* connected player as a CustomPayloadPacket 250 with its own copy of the data, through `queueSend` with
  `shouldDelay` (whole-frame delivery, as the ModIds handshake), under `PlayerList::m_playersCS`.
- `tests/test_modchannel.cpp` (`make test`): 220 checks on the policy and on the real glue template over fake engine types.
- Applies with offsets (no fuzz) to the current `neo-ps4-mob-candidate` files on top of `server-sync.patch` (checked on a scratch
  copy; the live tree and service were not touched). Not built with the server's CMake yet, not run.
