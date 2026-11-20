# strings195: runtime string ids for 1.95 (CUSA00265)

Image: `legacy-195-analysis.elf`, SHA-256 `c675e6ce…029f`. All addresses are RVAs. Stock media: `MediaOrbis.arc` `c49fc194…eae`.

strings195 makes mod text resolvable through the game's own string-id lookup, so every name path that takes a u32 string id (enchantments `0x24aec0`, potions `0x565e10`, effects, biomes, UI labels) shows mod text. No hook, no `.text` write, no media change: entries are inserted into the live Localizer hash map **by the game's own insert functions** (the exact sequence of the stock `languages.loc` loader).

Return values: `1` done and verified, `0` not ready / refused before any write, `-1` partial or uncertain.

## Mechanism (evidence)

| Fact | RVA / offset | Evidence |
|---|---|---|
| Every name path: `0xaac9b0(id)` → `[0x1440530]` Localizer → `Localizer::get 0xd39880`; null Localizer → literal `0xeeeda2` | `0xaac9b0..0xaac9cf` | pinned; **Unicorn** (real `0xaac9b0` resolves a mod id) |
| `get` hashes the id: FNV-1a 64 over the 4 LE bytes, fold `h ^ h>>32`, `& mask(+0x38)`, `maxidx(+0x40)` adjust, bucket vector `+0x20` (2 iterators × 0x18, node at +0x10), list head `+0x08`; node key `+0x10`, wstring `+0x18` (data `+0x20`, size `+0x30`, cap `+0x38`, SSO < 8); ids > `0xfffffffd` and misses return `0xf0fdc0` | `0xd39880..0xd39960` | pinned; Unicorn |
| Current map = `[Localizer+0xa0]` = language node `+0x38`; `+0x98` = node `+0x88` | ctor `0xd37a40`, `0xd37d51..0xd37d62` | pinned; Unicorn |
| Language nodes: outer list head `[Localizer+0x10]`; node `+0x10` language wstring (`en-EN`, `de-DE` …), `+0x38` string map (0x50: mask 7, maxidx 8, max_load_factor 1.0 initially) | `0xd59ed0` | **Unicorn ran the real node constructor** |
| Stock insert: `list::emplace_front 0xd5a9e0(map, pair&&)` (node 0x40 via operator new `0xe6cfd0`, moves the wstring) then `_Hash::_Insert 0x7ed670(ret, map, &key, iterator{·,·,node} by value)`: links the bucket, **rehashes with the game's code** past the load factor (`+0x48`), and frees the new node itself if the id exists (`0x7ed7d6`) | loader `0xd38f3f`, `0xd38f77` | pinned; **Unicorn ran both, incl. several real rehashes** |
| Wstring assign `0x67840(this, const wchar_t*, n)`; wstring = {+0 proxy, +8 buf/ptr, +0x18 size, +0x20 cap} | `0x6785c`, `0x67920` | pinned; Unicorn |
| The Localizer is **deleted and rebuilt** by `0xafc510` (new 0xd8 + ctor `0xd37a40`, stores `[0x1440530]`), called only from startup `0xaff71e` and the language-change path `0x9d985f` | `0xafc510..0xafc61b` | pinned (exactly two call sites) |
| Reserved mod id range `0x4C430000..0x4C43FFFF`: no id of the 4888 stock `languages.loc` ids, disjoint from the plugin's `0xfde30000..4` | `languages.loc` in `MediaOrbis.arc` | pinned (parsed) |

**What the module does** (`s195_ensure`): reads `[0x1440530]`; fast path when every entry already resolves through `[+0xa0]` (no native call). Otherwise, for every language node of the outer list (plus the current map if not among them) and every missing entry: copy the text into game heap, build a `pair<u32, wstring>` temporary (SSO-empty), call `0x67840`, `0xd5a9e0`, `0x7ed670`, verify the node is findable with the same bucket walk as `0xd39880` and holds the text, free the temporaries with operator delete. Text per language: exact match of `lang` (`-`/`_`, case folded), then 2-letter prefix, else `texts[0]`. Idempotent; call it every title/UI tick.

## Tests

| Suite | Checks | What |
|---|---|---|
| `tests/test_strings195.cpp` (ASan/UBSan, fake image with a Dinkumware-shaped map) | 27 | helpers, UTF-8, validation, per-language text, fast path (no allocation), rebuilt Localizer, duplicate/uncertain paths, allocation failure → 0 then success, a 400-point read-failure sweep (never rc 1 without all entries), 200 entries |
| `tests/test_image_pins.py` | 34 | every offset/constant above, both rebuild call sites, the `languages.loc` id range |
| `tests/test_unicorn_retail.py` | 79 | the **host-compiled module** drives the real `0x67840`/`0xd5a9e0`/`0x7ed670` in Unicorn on real language nodes from `0xd59ed0`; real `0xd39880` and `0xaac9b0` return the mod texts (long/heap, non-ASCII, surrogates), de-DE node gets the German text, 43 stock entries intact across real rehashes, 60 more ids, module find == real get for 136 ids, rebuilt Localizer re-inserted, refusals |

`./test.sh` runs all three; `./ps4check.sh` builds `build/ps4/strings195.o` with the shipping PS4 flags (undefined: `memcmp memcpy memset snprintf`).

Harness notes (`tests/uc195.py`, shared with enchants195): only PLT imports are stubbed (`new/delete`, `wmemcpy ×2`, `memcpy/memmove`, `wcslen`, `wmemchr`, `wmemcmp`, atomics, `__dynamic_cast` by Itanium RTTI walk, `vswprintf`); an unstubbed import aborts the test. Unicorn 2.1.4 has no VEX.256: the few zero/copy forms used (`vmovdqu/vmovups ymm`, `vxorps/vpxor ymm` self-zero, `vzeroupper`) are emulated exactly, and the leaf `hash<wstring> 0x68b350` (AVX2 loop for ≥ 8 units) is replaced by its scalar model, which is checked against the real function on short keys.

## Integration (coordinator)

```c
static s195_env senv = { base, NULL, read, write, held, s195_native_call };   /* same read/write/held as lce195x_memory */
/* plan time: one sid per name-bearing ext entry, s195_sid(index) with a stable index (ledger order) */
/* every title/UI tick (lce_sdk_ui_tick), and once right before publishing names: */
int rc = s195_ensure(&senv, entries, n, &rep);   /* 0 = Localizer not created yet: try next tick */
```

- `held` must be 1 on the native UI/title thread only. Log `LCE_SDK strings rc=… langs=… inserted=… present=… failed=…` when `inserted` or `failed` is non-zero.
- Build: add `modules/strings195/strings195.cpp` to `build-ps4.sh` and the Makefile.
- Package `lang` tables map to `s195_text{lang,"utf16",len}`; `texts[0]` must be `en_US` (fallback).

## Risks / unverified

- **Threads.** The insert (and a rehash it may trigger) runs on the calling thread while other threads may call `0xd39880`. The stock loader also writes the map on one thread, but only while the Localizer is being built. Run `s195_ensure` on the UI thread that draws menus, at the title (before a world). A rehash under a concurrent reader is the main residual risk; it happens only on first insertion after each rebuild.
- `[+0xa0]` switching between languages without a rebuild is not seen in the image (the language change rebuilds); the module inserts into every language node anyway.
- DLC/texture packs ship their own `languages.loc` (`/app0/orbis/DLC`, `0xee065c`); if a pack replaces the Localizer, the next tick re-inserts.
- Console: never run.

## On-console test (strings)

1. Boot to the title with one mod that names an enchantment. Expect `LCE_SDK strings rc=1 langs=N inserted=K failed=0` once, then nothing more (fast path).
2. Change the game language in Settings and return to the title: one more `inserted=K` line (rebuilt Localizer), and the name shows in the new language (or the English fallback).
3. A `rc=-1` or `failed>0` line is a fail: collect the log and stop.
