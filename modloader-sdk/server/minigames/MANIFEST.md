# Minigame source patch

This bundle contains only the two requested source changes and their own test harness support. It contains no game assets, binaries, SDK changes, or neighboring protocol work.

- `minigame-host.patch`: apply with `patch -p1 < .../minigame-host.patch` from the root of a compatible server source tree. It edits `Minecraft.Client/MinigameHost.cpp` and adds `Minecraft.Client/MinigameTumbleSpawn.h` and `tools/test_minigame_tumble_spawn.cpp`.
- `bot-harness.patch`: optional test-only changes to `perf-test/mg_bot.py`; apply with `patch -p1 < .../bot-harness.patch` from a workspace root containing `perf-test/mg_bot.py`. It adds early-disconnect options used in the vote test.

## Provenance and merge conditions

No exact independent pre-edit copy was found. The host preimage was mechanically reconstructed from the current candidate by reversing only the four known edits: the helper include, top-layer spawn lookup, `votesFor` lookup, and departing-vote retally. The bot preimage was reconstructed by removing the two added options and restoring the original bot hold expression. These are reconstructed preimages, not verified original snapshots.

Both patches were applied to temporary copies of those preimages. The patched files matched the current source and harness byte for byte, and the patched spawn test compiled and passed. An older independent host snapshot exists in `agent-gaps/baseline`, but it differs and is not claimed as an exact base. On other trees, review the contexts around `tumbleSpawn`, `votesFor`, and `Host::left` and merge mechanically; do not replace the whole file. The target must already contain Claude's Tumble generator, current player state/vote packet handling, and server-level `getTile` support. Do not apply over a tree that already includes these changes.

The spawn code selects the nearest matching top-layer block within the generated radius and logs then retains the old spawn if none is found. This does not guarantee unique spawn positions or exclude pre-existing blocks of the same material. The vote change preserves the existing ready reset and broadcasts a recomputed tally in lobby, countdown, and map-select; the lookup avoids recreating a player whose removal is in progress.

## Validation and limitations

The spawn unit test covers all five generated shapes, radii 4, 10 and 22, 16 targets per shape, nearest-support selection, and no-support handling. It previously passed under ASan/UBSan; the bundle application check also compiled it with `-Wall -Wextra -Werror` and passed.

An isolated server candidate passed the two-bot Tumble cycle. The departure test in `perf-test/runs/codex-vote-leave-20261120b/server.log` showed vote total 2, pf02 disconnect, the unchanged ready reset sent to pf01, then the updated total 1 sent to pf01. The bot's stream parser did not log the received vote packet, so this establishes the server transmit trace, not PS4 display behavior. No PS4 behavior or native Tumble voting is established by these tests.

## Patch SHA-256

- `minigame-host.patch`: `c6d415a4ad57f2999c221d7029bbf59e043bff1e755a30e92c8f7586d44ec241`
- `bot-harness.patch`: `a00012b42688b8dfeac265b82c36c30ed77ab15fa4be7e89927c62c884d2407f`
