# LCE|ModIds: multiplayer custom-content ID sync (gap G14), protocol v1

This protocol lets an SDK-modded PS4 1.95 client and the PC server agree on numeric item and block IDs before any custom stack moves between them. The server is authoritative. A client whose IDs disagree with the world is refused with a readable reason. IDs are never remapped silently.

The codec is `modules/netsync195/lce_modids_wire.h`. The server has a byte-identical copy at `server/new/Minecraft.World/LceModIdWire.h`, and both test suites `cmp` the two files.

## Transport

| | |
|---|---|
| Channel | `LCE|ModIds` (10 UTF-16 chars; the CustomPayload limit is 20) |
| Client to server | `ServerboundCustomPayloadPacket` **225** (reader `0x5fbe00`): `u8 225, u16 nchars, UTF-16BE channel, u16 len, payload`. `Packet::readPacket` maps 225 to the PC packet 250, which `PlayerConnection::handleCustomPayload` handles. |
| Server to client | `ClientboundCustomPayloadPacket` **250** (reader `0x18bbd0`, passthrough-verified, already on the `writePS4Packet` allowlist). It is queued with `shouldDelay` + `queueSend`, so each message is one whole frame, the same mechanism as `MC|LceNbt` (`LceDebugEngine.h`). The plugin matches `[250][u16 10][LCE|ModIds][u16 len == frame-25]` exactly and keeps the frame from the retail client. |
| Packet size | ≤ 4096 bytes per serverbound packet (`Coordinator::send_game_packet` cap). Each payload is at most 3968 bytes. |
| Byte order | All integers are big-endian. |

## Message header (10 bytes)

`u8 version (=1), u8 type, u32 session, u16 chunk index, u16 chunk count (1..64)`

`session` is a random odd nonce chosen by the server in its REQUEST and echoed in every later message.

| type | dir | body |
|---|---|---|
| 1 REQUEST | S→C | `u16 server_entries, u8[32] server_digest, u8 policy` (bit0 adopt, bit1 allow-missing-items) |
| 2 HELLO | C→S | chunk 0 only: `u32 sdk_api_version, u8 flags (bit0 ledger loaded), u16 total_entries, u8[32] digest`. Every chunk: `u16 n, n × entry` |
| 3 RESULT | S→C | `u8 verdict (0 accept, 1 reject), u16 code, u16 len, len bytes of ASCII text (≤480), u16 table_entries, u8[32] table_digest, u8 table_chunks` |
| 4 TABLE | S→C | `u16 n, n × entry`. This is the server's authoritative world table, in `table_chunks` chunks right after RESULT. |

### Entry (11 + key bytes)

`u8 kind (0 item, 1 block), u8 flags, u16 id, u8 max_stack (1..64), u8 material/item-kind, u16 a, u16 b, u8 key_len (1..63), key`

- **Flags:** `1` live (registered in the client's game this session), `2` block has a block-item, `4` transparent, `8` no-collide. The upper 4 bits must be zero.
- **Items:** `a` = max_damage. ID range 256..31999.
- **Blocks:** `a` = hardness×100, `b` = resistance×100. Both saturate at 0xfffe. ID range 1..4095. The block-item ID is `B<256 ? B : B+768` (retail 1.95 rule).
- **Keys:** the `lce_valid_key` charset, `modid:name`.

### Digest

The digest is SHA-256 of the canonical `LCEIDS 1` ledger text of the entries in the order sent: `"LCEIDS 1\n"` followed by `"item|block <key> <id>\n"` for each entry. When the client sends its whole ledger in file order, this equals the `sha256` trailer of `/data/lce-mods/reservations/sdk-ids-v1.txt`. The host test checks it against the real `lce_ledger_serialize`.

The digest proves the chunks were reassembled completely and in order. Properties are not covered; they are compared explicitly.

## Timing

1. The server sends REQUEST on a `PlayerConnection`'s first tick (after login), with the world table's size and digest.
2. The plugin's `lce_netsync_inbound_frame` (session worker thread, bridge lock held) posts the request. On the next UI tick, `lce_netsync_pump` sends all HELLO chunks with `send_game_packet`.
   - The client never sends unprompted, so a server without this feature never receives an unexpected payload.
   - A `PendingConnection` is never involved.
3. The server reassembles the chunks. Chunks must arrive in order with the same session and count; total and digest must match; keys and IDs must have no duplicates.
4. The server merges (below). On accept it persists any newly adopted IDs and registers their placeholders. It then sends RESULT + TABLE.
   - **Accept:** the connection's gate is opened for the matched IDs and the inventory/open container is resent.
   - **Reject:** chat lines `[Mods] ...` are sent, then `DisconnectPacket` (Kicked) follows 60 ticks (3 s) later.
5. Timeout: if no complete HELLO arrives within 200 ticks (10 s) and the world table is non-empty, the client is refused with code 10. The exception is a world that has only item-only entries while `allow-missing-items` is set; that client simply never sees mod stacks.

## Merge rules (server-authoritative; `lce_modids::merge`)

Only **live** client entries take part. Stale ledger entries from uninstalled mods are ignored and only logged.

| code | rule | effect |
|---|---|---|
| 0 | every live client entry matches the world (same kind+key+id, same max_stack and durability/hardness/has-item), or is new and adoptable | accept |
| 3 | same key, different ID | refuse: "`<key>` is item 2300 on this server but 2302 on your console" |
| 4 | the client's ID belongs to a different key in the world (including block-item aliasing) | refuse |
| 5 | the client's ID is a built-in server Item/Tile | refuse |
| 6 | same key+ID, different properties (mod version mismatch) | refuse |
| 7 | the world has entries the client does not have live | refuse with the missing mod namespaces. Exception: `allow-missing-items` waives item-only entries (items, and blocks ≥256), whose stacks are then withheld from that client. A missing placeable block (<256) is never waived, because chunks carry it. |
| 9 | the table would exceed 2048 entries | refuse |
| 11 | policy `locked` and the client brings new content | refuse |
| 1/2/12 | malformed, bad version, duplicate in HELLO | refuse |
| 8 | the server ledger could not be written | refuse |

- **New live client entries** (policy `adopt`, the default) are appended to the world table, so the first modded player defines the IDs. The table is append-only: IDs are never reused or removed.
- **Reporting:** the first failure is the message (printable ASCII, ≤480 bytes, "+N more" appended). Every failure goes to the server log as `[modids]`.

Policy is set with the file `lce-modids-policy.txt` in the server directory (the words `locked`, `allow-missing-items`), or with the environment variable `LCE_MODIDS_POLICY`, which overrides the file.

## Server world table

The file is `Windows64/GameHDD/<level-id>/lce-mod-ids.txt`, with the previous version kept as `.bak`. Writes go through a temporary file and `MoveFileEx(REPLACE_EXISTING|WRITE_THROUGH)`.

```
LCESRVIDS 1
item rubymod:ruby 2300 16 0 0 0 0
block rubymod:ore 254 64 0 300 1500 2
sha256 <64 hex of all preceding bytes>
```

Each line is `kind key id max_stack material a b flags`. Numbers must be canonical, and the live flag is never stored.

It is loaded at server start, after the world is chosen and **before** it loads, and placeholders are registered for every entry. A corrupt or tampered file, or an ID that a newer server build now uses itself, **aborts startup** (exit 5) with a log line. Loading the world anyway would drop every mod stack as an unknown ID (`ItemInstance::fromTag`).

## Placeholders on the server

| Kind | Placeholder |
|---|---|
| item | `PlaceholderItem` (`Item(id-256)`) with max stack and max damage. The description is the stick's. |
| block B < 256 | `PlaceholderTile(B)` with mapped material, hardness, resistance and solid/transparent, plus `TileItem(B-256)`. Placement, drops and chunk storage work as for a plain block. |
| block B ≥ 256 | Item-only: a `PlaceholderItem` at B+768. The server's chunk storage is 8-bit (`LevelChunk::setTileAndData` casts to `byte`), so these blocks **cannot be placed** on this server. The client's prediction is reverted by the server's block update. Nothing is corrupted. |

## Outgoing gate (`Connection::writePS4Packet`)

A custom item-space ID reaches a client only after its handshake accepted that ID.

| Packet | Handling |
|---|---|
| 103 ContainerSetSlot, 104 ContainerSetContent | disallowed stacks are emptied in a copy of the packet |
| 5 SetEquipment | dropped |
| item entities | withheld (40 pending-spawn path, next to the existing sapling rule) |

Before the handshake every custom ID is withheld. That covers the login inventory, which is resent after accept.
