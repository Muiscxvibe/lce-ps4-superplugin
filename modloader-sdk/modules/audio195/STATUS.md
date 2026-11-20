# audio195: custom audio (real new sounds) for 1.95 (CUSA00265)

Image: `legacy-195-analysis.elf`, SHA-256 `c675e6ce…029f`. All addresses are RVAs. Nothing here has run on a PS4.

## Mechanism

No `.text` patch and no new sound bank. Mod sounds are real game objects driven through the game's own Miles Sound System:

1. **SoundEvent.** Each mod sound becomes a real `SoundEvent`, built exactly like the stock static constructors at `0x63c540..`:
   - `operator new(0x38)` (`0xe6cfd0`), then an empty wstring (+0x18 size 0, +0x20 capacity 7).
   - The UTF-16 name (`modid:name`) is set with `0x67840`.
   - +0x28 = −1, +0x2c = 16.0f range, +0x30 = 1.
   - It is registered with `SoundEvent::register 0x6672c0`. That takes the next id from `[0x1430568]` (523 after the stock events) and inserts into the registry `0x1430570`, which events195 `play_sound` indexes at +0xe8.
   - Packets carry the id as a varint, so there is no range limit.
2. **Engine hook.** The `SoundEngine` (`[Minecraft 0x1464780] + 0x298`, ctor `0x7c27a0`, vtable `0x12ffc90`, 22 slots, RTTI `11SoundEngine`) gets a **private vtable copy** (vptr CAS: a data write).
   - Only `+0x28 play(ev, inst, x, y, z, vol, pitch)` and `+0x38 playUI(ev, vol, pitch)` are replaced.
   - A SoundEvent* that is not a mod sound is forwarded unchanged to the stock `0x7c2a80` / `0x7c2c10`.
   - Both entries are only ever reached through the vtable (no direct calls).
3. **Playback.** A mod event plays its RIFF/WAVE image, built at boot from the package's 16-bit mono PCM, on a pool of 8 Miles sample handles. The handles come from the game's digital driver (`[SoundEngine+0x240]+0x10`, opened 48 kHz / 16-bit / stereo at `0xdf7e02`). The statically linked Miles API calls used:
   - `AIL_allocate_sample_handle 0xe1f970`
   - `AIL_set_sample_file 0xe1fe30`: RIFF parsed by `AIL_WAV_info 0xe22110`, PCM branch `0xe256b0`
   - `AIL_set_sample_volume_levels 0xe20c50`
   - `AIL_set_sample_playback_rate_factor 0xe205a0`
   - `AIL_start_sample 0xe20360`
   - `AIL_sample_status 0xe20f30` (4 = playing)
   - `AIL_end_sample 0xe204b0` (steal the oldest when all 8 are busy)

   The Miles wrappers take the Miles API mutex themselves.
4. **Spatial.**
   - Volume: `vol` × linear distance fade (range 16 × max(vol, 1), the Minecraft rule) × the engine's world sound volume `+0x138`. The volume is 0 while `+0x134` (mute) is set, the same rule as `0x7c1e36`. `playUI` uses `+0x13c`.
   - Pan: from listener 0 (`+0x154` x/y/z, forward x/z `+0x160`/`+0x168`, written at `0x7c224e..`). The right vector is (−fz, 0, fx), and |pan| ≤ 0.6.
   - Pitch: `pitch × descriptor pitch`, clamped to 0.5..2.
5. **inst bookkeeping.** For a non-null `inst`, the hook does the same thing the stock play does (`0xd2ba20(inst, 1)`, `inst+0xc = 0`).
6. **Services.** `lce195_sound_set_custom(key, id)` makes `play_sound(level, x, y, z, "modid:name")` work through the same `ServerLevel` path as vanilla sounds. The server broadcasts the id, the local client looks the id up in the registry, and the engine hook plays it.

There is no Ogg Vorbis decoder in the plugin, and Miles in this build registers only the Bink audio decoder. Packages therefore carry 16-bit mono PCM: `lce_modpack.py` reads a `.wav`, and `autoport --ffmpeg` converts `.ogg`.

## Evidence

| Item | Level |
|---|---|
| SoundEngine RTTI, 22 slots, play/playUI slots, `+0x298` store, vptr lea, `+0x240`/`+0x20` queue, null-event early return, inst bookkeeping, float args | pinned (`tests/test_image_pins.py`, 41) |
| Driver opened 48000/16/2, stored at impl+0x10 | pinned |
| Volume/mute/UI volume fields, listener fields | pinned |
| Every AIL address: each wrapper logs its own name (`@AIL_xxx(` format strings) | pinned |
| `AIL_WAV_info` accepts the audio195 WAV (format 1, data ptr/len, rate, 16 bits, mono); rejects a non-WAVE image | **Unicorn** (real code) |
| wstring assign `0x67840` on the SoundEvent layout (heap and inline buffers) | **Unicorn** |
| `SoundEvent::register 0x6672c0`: id = counter, counter + 1, insert(registry, id, rl, ev), returns ev | **Unicorn** (ResourceLocation helpers and map insert stubbed and recorded) |
| Stock play with a null event returns before Miles | **Unicorn** |
| WAV builder, checks, register/hook/unhook state machine, forward vs. play, gain/pan maths, mute, UI path, pool reuse/steal, failure counting | host (`tests/test_audio195.cpp`, 71, ASan/UBSan); the C-built WAV is byte-compared with the Python reference |
| Audible output, Miles accepting a second sample source next to the event system, pan direction | **not verified** (console test) |

Unicorn stubs: operator new `0xe6cfd0` (bump allocator), free `0xe6cfb0`, wmemmove/wmemcpy imports `0xe6d110`/`0xe6d120`, Miles lock/unlock `0xe34eb0`/`0xe34fa0`, ResourceLocation helpers `0xcda2f0`/`0xcda0b0`/`0xcdab00`, map insert `0x6673d0`.

## Phase

Late title phase (composite `audio`), main thread, while no world exists: `a195_register_events`, then `a195_hook_engine`. Minecraft and its SoundEngine exist at the title.

## Refused / limits

- More than 64 sounds, more than 4 MiB PCM per sound, or rates outside 8000..48000.
- Stereo or 8-bit sources (the packer converts mono 16-bit only).
- Streaming music replacement (records/menu music use `.binka` streams): not implemented.
- Mob sound slots naming a mod sound (entities195 still reuses vanilla mob sound slots).
- Over 8 simultaneous mod sounds: the oldest is stopped.

## Risks

- Multiplayer: another PS4 that joins without the mod receives an unknown sound id; the stock client looks it up and plays nothing (null event, early return). The PC server never generates mod sound ids.
- Pan sign: (−fz, fx) is derived from the Minecraft axes. If left and right are swapped on the console, flip the sign in `a195_spatial`.
- A second consumer of Miles sample handles next to the event system: Miles supports it, but it has not run on this build.

## Console test

1. At the title, expect `LCE_SDK phase=audio rc=1 N custom sound(s) registered (ids from 523) + SoundEngine vtable copy`.
2. In a test world, a script `lce.play_sound(level, x, y, z, "modid:chime")` (or the `/` command route) plays the chime at the position, louder when closer.
3. Turn around: the sound moves between the left and right channels.
4. Options, sound volume 0: silent. 50 %: quieter.
5. Vanilla sounds are unchanged (walk, break a block, open a chest).
