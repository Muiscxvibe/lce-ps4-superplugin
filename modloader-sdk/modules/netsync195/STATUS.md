# netsync195: client side of LCE|ModIds (G14)

This module sends the console's SDK ID ledger to the PC server when the server asks for it, and reads the server's verdict and authoritative table. The protocol is in [../../server/PROTOCOL.md](../../server/PROTOCOL.md).

## Files

| Path | What |
|---|---|
| `lce_modids_wire.h` | Wire codec: header-only, freestanding (no STL, heap or exceptions). Byte-identical to `server/new/Minecraft.World/LceModIdWire.h` |
| `netsync_client.h/.cpp` | `lce_netsync::Client`: HELLO encoder (pure), frame matcher, worker→UI mailbox, RESULT/TABLE verification. Uses static storage of about 160 KB |
| `netsync_sdk.h/.cpp` | `entries_from_sdk(ledger, plan, published)`: one entry per ledger entry, in ledger order, with live flags and properties taken from the committed plan |
| `netsync_plugin.cpp` | C ABI: `lce_netsync_configure_from_loader`, `lce_netsync_inbound_frame`, `lce_netsync_pump`, `lce_netsync_state`, `lce_netsync_message` |
| `base/` | Copies of `ps4-transport/native_bridge.c`, `ps4-transport/session_runtime.cpp` and `modloader-sdk/integration/sdk_boot.cpp`. Hashes are in `patch-base.sha256` |
| `patched/` | The same files with the hooks |
| `client-integration.patch` | Proposed patch (`-p1` from `~/minecraft-lce-work`): paths `ps4-transport/...` and `modloader-sdk/integration/sdk_boot.cpp` |
| `tests/test_netsync.cpp` | Host tests (`make test`) |

**Integrated (2026-11-18).** The `sdk_boot.cpp` hunk of `client-integration.patch` is merged into `integration/sdk_boot.cpp` (the SDK's own file), netsync195 is linked into `build/ps4/lcemodloader.o`, and the two `ps4-transport` hunks are part of `integration/shipping-plugin.patch`. `base/` and `patched/` stay as the module's provenance record.

## Where the client sends and receives (read-only analysis of ps4-transport)

**Send path.**

1. The game's own serverbound packets go through `NetworkPlayerSony::SendData`.
2. `lce_player_send_hook` (`player_send_hook.c`) catches them for local→host and calls `lce_native_bridge_send` (`native_bridge.c`).
3. That takes the bridge spin lock, calls `lce_session_send`, and the bytes are framed to the server.

The plugin already injects whole serverbound packets through `lce_session_coordinator::Coordinator::send_game_packet` (`session_coordinator.cpp:441`, cap of 4096 bytes). That is how `mc195_remote_send_text` in crashlog-build sends its `0xde` chat packets. netsync uses the same call. Every HELLO packet is at most 4096 bytes; this was tested with 2048 entries and 63-byte keys, which gives 38 packets.

**Receive path.**

1. `lce_native_bridge_tick` runs on the session worker, holding the bridge lock.
2. It calls `lce_session_tick`, which calls `receive_frame` (`native_bridge.c`).
3. `receive_frame` already offers every server frame to the weak `lce_remote_inbound_frame` (`remote_nbt.cpp`, the `MC|LceNbt` bridge) before pushing it to the retail client.

The patch adds a second weak hook, `lce_netsync_inbound_frame`, in front of it. Because the lock is held and is not recursive (`Runtime::bridge_lock` is a spin lock), the hook only posts to the mailbox and never sends.

**Pump.** `session_runtime.cpp` `Runtime::run_ui_task` runs on the owned game tick. The patch calls the weak `lce_netsync_pump(phase()==Active, send_game_packet thunk, &coordinator_)` there. When the phase leaves Active, the in-flight handshake is reset.

**Configure.** In `sdk_boot.cpp`, after `lce_loader_commit`, the patch calls the weak `lce_netsync_configure_from_loader(g_loader, final_state==2, log)`.

All three hooks are weak symbols, so a plugin built without this module behaves exactly as before. If the SDK never booted, the first REQUEST is answered with an empty table, so the server can tell a modless console from a modded one.

## Verified

- **`make test`**: `test_netsync` passes about 11,100 checks with 0 failures (ASan/UBSan; the count varies slightly because of the thread test). It covers:
  - Golden bytes of a one-entry HELLO packet.
  - The worst case of 2048×63-byte keys, every packet ≤ 4096 bytes.
  - `configure` rejects duplicate keys, block-item aliasing, invalid entries and more than 2048 entries.
  - `entries_from_sdk` against handmade `lce_ledger`/`lce_plan`: live flags, max stack 0→64, durability, hardness/resistance saturation, transparent and NO_ITEM, unpublished → not live, and a too-small cap.
  - The digest equals the trailer written by the real SDK `lce_ledger_serialize`.
  - A full handshake against the real server core: accept with a verified table; conflict refusal with the exact message; a vanilla console refused as missing; a tampered table detected, with a warning logged.
  - Frame matching: other channels (`MC|LceNbt`), partial and two-packet frames, and chat all pass through untouched; an unknown version is swallowed.
  - Send failure, reset on leaving a session, wrong-session results ignored, message sanitising, the C ABI, and 4000 fuzzed frame sets.
  - A worker/UI two-thread stress test.
  - It also `cmp`s the two codec copies and re-hashes `base/`.
- **ThreadSanitizer** (scratch build, `setarch -R`): clean over 3 runs. It found one real race (request session and sequence were separate words), which is fixed with a single packed atomic word.
- **`client-integration.patch`**: `patch --dry-run`, then a real apply, on scratch copies of the current `ps4-transport/native_bridge.c`, `session_runtime.cpp` and `integration/sdk_boot.cpp`. It applies cleanly and the result is identical to `patched/`. The patched `native_bridge.c` and `sdk_boot.cpp` pass `-fsyntax-only` on the host.

## Not verified

- Building with the OpenOrbis toolchain and linking into the shipping plugin. The SDK itself is not linked into the shipping plugin yet (`INTEGRATION.md`).
- On console:
  - that `run_ui_task` runs every frame while Active;
  - that `send_game_packet` packets land on packet boundaries (the existing chat injection relies on the same assumption);
  - that the server's `shouldDelay` frames each arrive as exactly one frame, including the TABLE burst.
- "Live" means the loader planned the content and boot state is 2 (armed). The backend registers items later, inside the item window (`prov_collect`). A later publish failure would not clear the live flag.
- The client cannot register server-only IDs at runtime. The table is used only for verification and logging, and a refusal is shown only in the plugin log, the server chat and the kick. There is no native dialog yet. `lce_netsync_state` and `lce_netsync_message` are exposed for one.

## Test plan (main engineer)

1. Apply `client-integration.patch` to the plugin tree. Add `netsync_client.cpp`, `netsync_sdk.cpp` and `netsync_plugin.cpp` to the plugin build, which also links the SDK (`build-ps4.sh`).
2. Install with `install_session_plugin.py`, with the game closed and an SDK mod in `/data/lce-mods`.
3. Join the patched server (server/STATUS.md test plan, steps 6–9). Then read `/data/minecraft-lce-custom-dialog.log` over FTP and look for the `[netsync]` lines.
