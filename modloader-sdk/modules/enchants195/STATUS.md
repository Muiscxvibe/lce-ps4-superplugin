# enchants195: new enchantments for 1.95 (CUSA00265)

Image: `legacy-195-analysis.elf`, SHA-256 `c675e6ce…029f`. All addresses are RVAs.

A mod enchantment is a real `Enchantment` object built by the **stock class constructor**, given a **private vtable whose overrides are all retail functions**, and registered at a free numeric id through the **registry's own virtual `register`**. The game never calls plugin code for an enchantment. Names come from strings195. Data writes only: heap objects, our vtable copy, the object's vptr. No `.text`.

Return values: `1` done and verified, `0` refused before any game write or allocation (temporaries for the key check are freed), `-1` partial or uncertain.

## Evidence

| Fact | RVA / offset | Evidence |
|---|---|---|
| Registry `[0x1415448]`: `MappedRegistry<ResourceLocation, Enchantment*>` (vt `0x1255398`, object 0x150, ctor `0x2be3d0`), created by static init `0x2d3040`, so it exists at plugin start | `0x2d305b..0x2d307e` | pinned; **Unicorn** (real ctor) |
| Slots: `+0x20` containsKey(RL&) `0x2beb00`, `+0x28` register(id, RL&, obj) `0x2bec20` (id vector `+0xe8/+0xf0` grows, value→id map `+0x90`, then `put` → key maps and the hash list at `+0x100/+0x108`), `+0x38` getId `0x2bef00`, `+0x40` byId `0x2bf020` (negative/out-of-range → null) | | pinned; Unicorn |
| Stock bootstrap `0x248fa0` (36 enchantments, ids 0..92 with gaps) is called at `0x478134`, **after** the item window `0x477cc8` | | pinned; **Unicorn ran the whole bootstrap** (36 registered) |
| `Enchantment` (0x30): `+0` vptr, `+8` category, `+0x10` description id (u32 string id), `+0x18` EquipmentSlot** / `+0x20` count, `+0x28` Rarity*; base ctor `0x24ab10(this, rarity, category, slots, n)`, base vt `0x1245d68` (19 slots) | | pinned; Unicorn |
| Slots used: `+0x00` getRarity, `+0x10` minLevel, `+0x18` maxLevel, `+0x20/+0x28` min/max cost, `+0x30` getDamageProtection, `+0x38` getDamageBonus, `+0x40` checkCompatibility, `+0x50` getDescriptionId, `+0x58` getFullname `0x24aec0`, `+0x60` canEnchant, `+0x68` doPostAttack, `+0x70` doPostHurt, `+0x78` isTreasureOnly, `+0x80` isCurse | base vt | pinned; Unicorn ran each on mod objects |
| Subclasses: `DamageEnchantment` ctor `0x1f1a40(this, rarity, type 0/1/2, slots, n)` 0x38, WEAPON, vt `0x1236350`; `ProtectionEnchantment` `0x58d970(this, rarity, Type*, slots, n)` 0x38, FEET if FALL else ARMOR, vt `0x12ac170`; `TridentImpalerEnchantment` `0x6f6b30(this, rarity, slots, n)` 0x30, TRIDENT, vt `0x12e6680` | | pinned; Unicorn |
| Subclasses **override getDescriptionId** (damage table `0xe859a8[type]`, protection `Type+0`) and ignore `+0x10`; slot `+0x50` is therefore set to the base getter `0x24aeb0` (`mov 0x10(%rdi),%eax; ret`) and `+0x10 = name_sid` | `0x1f1b70`, `0x58dae0` | pinned; Unicorn (getDescriptionId → mod sid) |
| getMaxLevel → retail `mov $N,%eax; ret` functions, N = 1..10: `0x24ae30 0x48c0 0x158c0 0x3ca20 0x1f1ad0 0x12180 0x5db00 0x24d10 0x58420 0x319f0` (16-aligned, after padding); the module re-reads the 6 bytes at runtime before using one | | pinned; Unicorn |
| isTreasureOnly/isCurse → `0x24ae30` (1) / `0x24b090`, `0x24b0a0` (0); canEnchant → `0x24b040` (category `+8`) when a category is set | | pinned; Unicorn |
| Rarity globals `0x142f250 +8k` (COMMON 10, UNCOMMON 5, RARE 2, VERY_RARE 1), weight `+0x30` (`0x59d310`); the module picks the object whose weight matches | | pinned; Unicorn (EnchantmentInstance weight) |
| Category globals (enum order ALL, ARMOR, FEET, LEGS, CHEST, HEAD, WEAPON, DIGGER, FISHING_ROD, BREAKABLE, BOW, CROSSBOW, WEARABLE, TRIDENT): `0x14154b8 0x14154c0 0x14154c8 0x14154d0 0x14154d8 0x14154e0 0x1415480 0x1415488 0x1415490 0x14154e8 0x14154f0 0x14154f8 0x1415500 0x1415508`, class vtables `0x1254ee8 … 0x1255158` (RTTI `11AllCategory` … `15TridentCategory`); the module checks each object's vptr | static init `0x2d3119..0x2d3756` | pinned |
| Protection `Type` globals `0x142ed40 +8k` (ALL, FIRE, FALL, EXPLOSION, PROJECTILE) = {name id, minCost, levelCost, span} copied from `0xea7c00` | `0x5e4437..` | pinned; Unicorn |
| **Everything lists the registry live**: enchanting table `getAvailableEnchantmentResults 0x24d890` iterates `reg+0x108`, checks isTreasureOnly, category (books skip it), max..min level and the cost window; Creative books `0xa31da5` iterate `reg+0x108`; NBT apply `0x248d00` looks up the short id via byId and null-checks it (`0x248d13`), so an unknown id is skipped | | pinned; **Unicorn ran the table: mod enchantments are candidates, with the right weight and level** |
| getFullname `0x24aec0` = `aac9b0(getDescriptionId())` + `" "` + level text (`0xe8bd80[level]`, 11 ids; index 11 is not an id → max level 10), curse colour `0x1a1` | | pinned; **Unicorn: `"Gem Edge VII"`** via strings195 |
| Behaviour runs through virtuals per enchantment on the item: `GetDamageProtectionIteration 0x24bf60` → `+0x30`, `GetDamageBonusIteration 0x24c0a0` → `+0x38`, `DoPostHurt 0x24c220` → `+0x70`, `DoPostDamage 0x24c710` → `+0x68` | | pinned (disassembly); Unicorn: clone `getDamageBonus` == stock sharpness |
| Typed accessors read static `Enchantments::X` pointers (`0x14155c0..0x14156d8`, filled at `0x250cba..` by `registry.get(key)`). Pointers of protection, feather_falling, projectile_protection, sharpness, smite, bane_of_arthropods, impaling are **never read** (only stored); thorns is read at `0x6e0ce8` (doPostHurt picks a stock-thorns item and exits if none); fire/blast protection once each (burn time / knockback); all others (fire_aspect, mending, …) several times | | pinned (reference counts) |

## Clone policy

| `clone_of` | Result |
|---|---|
| none / `""` | base `Enchantment`, no built-in effect (scripts read the level from NBT). Default category BREAKABLE |
| sharpness, smite, bane_of_arthropods | full effect (damage bonus; bane slowness via `+0x68`); incompatible with the stock damage enchantments (class check `0x1f1b80`) |
| impaling | full effect (damage vs. water mobs) |
| protection, feather_falling, projectile_protection | full effect |
| fire_protection, blast_protection | damage reduction works; the burn-time / knockback reduction is keyed to the stock object (identity read) |
| thorns and every other stock enchantment | **refused** with the reason: its effect is read through the static `Enchantments::X` pointer, a clone would do nothing |

## API (`enchants195.h`)

`e195_clone_from_key`, `e195_category_from_applies` (lce item-kind bitmask → category), `e195_rarity_index`, `e195_check`, `e195_publish(env, specs, n, records)`, `e195_verify`. Spec: `key "modid:name"`, persistent `id` 100..1023 (NBT/packets carry a short), `clone`, `category`, `max_level` 1..10, `weight`, `treasure`, `curse`, `name_sid` (strings195). The env is `s195_env` (same memory/held callbacks as items195 plus `call = s195_native_call`).

## Tests

| Suite | Checks | What |
|---|---|---|
| `tests/test_enchants195.cpp` (ASan/UBSan, fake image) | 99 | clone policy (accepted / identity-refused / unknown), applies→category, spec checks, a 4-enchantment publish (vptr, slot overrides, RTTI kept, category override, rarity, Type), 10 refusals with no registry change and temporaries freed, batch refusal, ctor/register uncertainty → -1 |
| `tests/test_image_pins.py` | 54 | every RVA/offset/RTTI/slot/constant above, identity-read reference counts |
| `tests/test_unicorn_retail.py` | 86 | real registry + real stock bootstrap; the host-compiled module publishes 5 enchantments (sharpness/protection/none/impaling/feather clones) through the real ctors and `register`; real virtuals return max level, rarity weight, mod string id, treasure/curse; damage bonus and costs equal stock sharpness; table `0x24d890` lists them with the real cost windows (treasure only with allowTreasure); real `getFullname` = `"Gem Edge VII"` through a Localizer filled by strings195; refusals leave the heap and registry untouched |

`./test.sh` runs all three (strings195's `tests/uc195.py` harness is shared); `./ps4check.sh` builds `build/ps4/enchants195.o` (needs `strings195.o` for `s195_sid_reserved`; libc: `memcmp memcpy memset snprintf strchr strcmp strlen strncmp`).

TU19-based public notes (relayed by the coordinator: coah80 slop-docs "custom enchantments", Project Lodestone) were not fetched; their claims were checked against 1.95 instead: there is **no** cached `validEnchantments` list in 1.95 (the table iterates the registry live), names are u32 string ids (not wstring keys), and level text exists for levels up to 10.

## Integration (coordinator)

1. Plan time: map `lce_enchant_desc` → `e195_spec`: `clone = e195_clone_from_key(desc->clone_of)` (refuse the mod with `err` on -1), `category = e195_category_from_applies(desc->applies_to)`, `weight`, `treasure`, `max_level`, `id` from the ID ledger (new range "enchantment", 100..1023, persistent), `name_sid = s195_sid(k)`. `check_ext(LCE_EXT_ENCHANT)` → `e195_check`.
2. Names: add `{name_sid, texts}` to the strings195 entry list (from `desc->name` + package `lang`).
3. **Late title phase** (`lce_sdk_ui_tick`, no world, after `0x478134` has long returned): `s195_ensure(...)` then `e195_publish(&senv, specs, n, recs)`; log `LCE_SDK phase=enchants rc=… published N`. Never retry after -1.
4. Capabilities: advertise `LCE_CAP_ENCHANTS` when the registry global and `[0x142f250]` are non-null at boot.
5. Multiplayer: the enchantment id travels inside item NBT. Peers without the mod skip unknown ids (null-checked), so it degrades to "no enchantment"; netsync should carry the enchantment ledger range like items.

## Risks / unverified

- Never run on a PS4. Gameplay (table rolls, anvil combining, damage numbers, save/reload) needs the console steps below.
- **Creative books**: the Creative group build reads the registry live (`0xa31da5`), but if it runs before the late title phase the mod books are missing from Creative (the table/anvil/loot still work). Check on console.
- Anvil code was not audited beyond using the same virtuals (`checkCompatibility`, `getMaxLevel`, `canEnchant`).
- Level 1 text uses stock id `0x66e0206a`; levels 2..10 use the stock roman numerals.
- Registry writes happen on the UI thread at the title with no world; the registry is not locked.

## On-console test (enchantments)

Mod: one enchantment `gemmod:gem_edge` (clone sharpness, max level 7, weight 2, name "Gem Edge"), id from the ledger.
1. Title log: `LCE_SDK strings rc=1 … inserted=…` and `LCE_SDK phase=enchants rc=1 published 1`. Any `rc=-1`: stop.
2. Creative (search the Mods / enchanted books list): an enchanted book "Gem Edge I…VII" may appear (record present / absent; absent is not a failure).
3. Survival, enchanting table with 15 bookshelves, an iron sword, level 30 option, several rerolls: "Gem Edge" appears in the hints/results (rare weight 2: try ≥ 20 rerolls; record count).
4. Hover the enchanted sword: "Gem Edge <roman>". Hit a zombie with Gem Edge V vs. a plain sword: more damage (same as Sharpness V).
5. Anvil: Gem Edge book + sword works; Gem Edge + Sharpness on one sword is refused (incompatible).
6. Save, quit, relaunch: the sword keeps "Gem Edge". Remove the mod and reload a copy of that world: the enchantment disappears, no crash.
